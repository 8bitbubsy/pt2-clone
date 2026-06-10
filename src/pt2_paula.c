/* Simple Paula emulator (with BLEP synthesis by aciddose).
** Limitation: The audio output frequency can't be below 31389Hz ( ceil(PAULA_PAL_CLK / 113.0) )
**
** WARNING: These functions must not be called while paulaGenerateSamples() is running!
**          If so, lock the audio first so that you're sure it's not running.
*/

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>
#include "pt2_header.h" // PI
#include "pt2_audio.h"
#include "pt2_paula.h"
#include "pt2_blep.h"
#include "pt2_rcfilters.h"

typedef struct voice_t
{
	volatile bool active;

	// internal registers
	bool sampleJustStarted, nextSampleStage;
	int8_t AUD_DAT[2]; // DMA data buffer
	const int8_t *location; // current location
	uint16_t lengthCounter; // current length
	int32_t sampleCounter; // how many bytes left in AUD_DAT
	float fSample; // currently held sample point (multiplied by volume)
	float fDelta, fPhase;
	float fBlepDelta, fBlepPhase;

	// registers modified by Paula functions
	const int8_t *storedLocation; // data pointer
	uint16_t storedLength;
	float fStoredVol, fStoredDelta;
} paulaVoice_t;

static bool useLEDFilter, useLowpassFilter, useHighpassFilter;
static int8_t nullSample[0xFFFF*2]; // buffer for NULL data pointer
static float fPeriodToDeltaDiv;
static double dPaulaOutputFreq;
static blep_t blep[PAULA_VOICES];
static onePoleFilter_t filterLo, filterHi;
static twoPoleFilter_t filterLED;
static paulaVoice_t paula[PAULA_VOICES];

void paulaSetup(double dOutputFreq, uint32_t amigaModel)
{
	ASSERT(dOutputFreq != 0.0);
	dPaulaOutputFreq = dOutputFreq;
	fPeriodToDeltaDiv = (float)(PAULA_PAL_CLK / dPaulaOutputFreq);

	clearBlepState();

	useLowpassFilter = useHighpassFilter = true;
	clearOnePoleFilterState(&filterLo);
	clearOnePoleFilterState(&filterHi);
	clearTwoPoleFilterState(&filterLED);

	/*
	** Amiga 500/1200 filters
	**
	** RC values for Amiga 500 (rev 6A):
	** - 1-pole (6dB/oct) RC low-pass: R=360 ohm, C=0.1uF
	** - 2-pole (12dB/oct) Sallen-Key low-pass ("LED"): R1/R2=10k ohm, C1=6800pF, C2=3900pF
	** - 1-pole (6dB/oct) RC high-pass: R=1390 ohm (1000+390), C=22.33uF (22+0.33)
	**
	** RC values for Amiga 1200 (rev 1D4):
	** - 1-pole (6dB/oct) RC low-pass: R=680 ohm, C=6800pF
	** - 2-pole (12dB/oct) Sallen-Key low-pass ("LED"): R1/R2=10k ohm, C1=6800pF, C2=3900pF
	** - 1-pole (6dB/oct) RC high-pass: R=1360 ohm (1000+360), C=22uF
	*/
	double R, C, R1, R2, C1, C2, cutoff, qfactor;

	if (amigaModel == MODEL_A1200)
	{
		// Amiga 1200 rev 1D4

		/* Don't handle the A1200 low-pass filter since its cutoff
		** is well above human hearable range anyway (~34.4kHz).
		** We don't do volume PWM, so we have nothing we need to
		** filter away.
		*/
		useLowpassFilter = false;

		// A1200 1-pole (6dB/oct) RC high-pass filter:
		R = 1360.0; // R324 (1K ohm resistor) + R325 (360 ohm resistor)
		C = 2.2e-5; // C334 (22uF capacitor)
		cutoff = 1.0 / ((2.0 * PI) * R * C); // ~5.319Hz
		setupOnePoleFilter(dPaulaOutputFreq, cutoff, &filterHi);
	}
	else
	{
		// Amiga 500 rev 6A

		// A500 1-pole (6dB/oct) RC low-pass filter:
		R = 360.0; // R321 (360 ohm)
		C = 1e-7;  // C321 (0.1uF)
		cutoff = 1.0 / ((2.0 * PI) * R * C); // ~4420.971Hz
		setupOnePoleFilter(dPaulaOutputFreq, cutoff, &filterLo);

		// A500 1-pole (6dB/oct) RC high-pass filter:
		R = 1390.0;   // R324 (1K ohm) + R325 (390 ohm)
		C = 2.233e-5; // C334 (22uF) + C335 (0.33uF)
		cutoff = 1.0 / ((2.0 * PI) * R * C); // ~5.128Hz
		setupOnePoleFilter(dPaulaOutputFreq, cutoff, &filterHi);
	}

	// 2-pole (12dB/oct) Sallen-Key low-pass filter ("LED" filter, same values on A500/A1200):
	R1 = 10000.0; // R322 (10K ohm)
	R2 = 10000.0; // R323 (10K ohm)
	C1 = 6.8e-9;  // C322 (6800pF)
	C2 = 3.9e-9;  // C323 (3900pF)
	cutoff = 1.0 / ((2.0 * PI) * sqrt(R1 * R2 * C1 * C2)); // ~3090.533Hz
	qfactor = sqrt(R1 * R2 * C1 * C2) / (C2 * (R1 + R2)); // ~0.660225
	setupTwoPoleFilter(dPaulaOutputFreq, cutoff, qfactor, &filterLED);
}

void paulaDisableFilters(void) // disables low-pass/high-pass filter ("LED" filter is kept)
{
	useHighpassFilter = false;
	useLowpassFilter = false;
}

int8_t *paulaGetNullSamplePtr(void)
{
	return nullSample;
}

static inline void refetchPeriod(paulaVoice_t *v) // Paula stage
{
	v->fBlepPhase = v->fPhase;
	v->fBlepDelta = v->fDelta;

	// Paula only updates period (delta) during period refetching (this stage)
	v->fDelta = v->fStoredDelta;

	v->nextSampleStage = true;
}

static inline void nextSample(paulaVoice_t *v, blep_t *b) // Paula stage
{
	if (v->sampleCounter == 0)
	{
		// it's time to read new samples from DMA

		// don't update AUD_LEN/AUD_LC yet on DMA trigger
		if (!v->sampleJustStarted)
		{
			if (--v->lengthCounter == 0)
			{
				v->lengthCounter = v->storedLength;
				v->location = v->storedLocation;
			}
		}

		v->sampleJustStarted = false;

		// fill DMA data buffer
		v->AUD_DAT[0] = *v->location++;
		v->AUD_DAT[1] = *v->location++;
		v->sampleCounter = 2;
	}

	/* Pre-compute current sample point.
	** Output volume is only read from AUDxVOL at this stage,
	** and we don't emulate volume PWM anyway, so we can
	** pre-multiply by volume here.
	*/
	v->fSample = v->AUD_DAT[0] * v->fStoredVol; // -128..127 * 0.0f .. 1.0f

	// fill BLEP buffer if the new sample differs from the old one
	if (v->fSample != b->fLastValue)
	{
		if (v->fBlepDelta > v->fBlepPhase) // also checks if v->fBlepDelta > 0.0f
		{
			const float fBlepOffset = v->fBlepPhase / v->fBlepDelta;
			blepAdd(b, fBlepOffset, b->fLastValue - v->fSample);
		}

		b->fLastValue = v->fSample;
	}

	// progress AUD_DAT buffer
	v->AUD_DAT[0] = v->AUD_DAT[1];
	v->sampleCounter--;
}

static void audxper(int32_t ch, uint16_t period)
{
	paulaVoice_t *v = &paula[ch];

	int32_t realPeriod = period;
	if (realPeriod == 0)
		realPeriod = 65536; // On Amiga: period 0 = period 65536 (1+65535)
	else if (realPeriod < 113)
		realPeriod = 113; // close to what happens on real Amiga (and low-limit needed for BLEP synthesis)

	// to be read on next sampling step (or on DMA trigger)
	v->fStoredDelta = fPeriodToDeltaDiv / (float)realPeriod;

	// BLEP synthesis edge-case
	if (v->fBlepDelta == 0.0f)
		v->fBlepDelta = v->fDelta;
}

static void audxvol(int32_t ch, uint16_t vol)
{
	int32_t realVol = vol & 127;
	if (realVol > 64)
		realVol = 64;

	// multiplying sample point by this also scales the sample from -128..127 -> -1.000 .. ~0.992
	paula[ch].fStoredVol = realVol * (1.0f / (128.0f * 64.0f));
}

static void audxlen(int32_t ch, uint16_t len)
{
	paula[ch].storedLength = len;
}

static void audxdat(int32_t ch, const int8_t *src)
{
	if (src == NULL)
		src = nullSample;

	paula[ch].storedLocation = src;
}

static void startDMA(int32_t ch)
{
	paulaVoice_t *v = &paula[ch];

	if (v->storedLocation == NULL)
		v->storedLocation = nullSample;

	// immediately update these
	v->location = v->storedLocation;
	v->lengthCounter = v->storedLength;

	// make Paula fetch new samples immediately
	v->sampleCounter = 0;
	v->sampleJustStarted = true;
	refetchPeriod(v);

	// kludge: must be cleared *after* refetchPeriod()
	v->fPhase = 0.0f;

	v->active = true;
}

static void stopDMA(int32_t ch)
{
	paula[ch].active = false;
}

void paulaWriteByte(uint32_t address, uint8_t data8)
{
	if (address == 0)
		return;

	switch (address)
	{
		// CIA-A ("LED" filter control only)
		case 0xBFE001:
		{
			const bool oldLedFilterState = useLEDFilter;

			useLEDFilter = !!(data8 & 2);
			if (useLEDFilter != oldLedFilterState)
				clearTwoPoleFilterState(&filterLED);
		}
		break;

		default:
			return;
	}
}

void paulaWriteWord(uint32_t address, uint16_t data16)
{
	if (address == 0)
		return;

	switch (address)
	{
		// DMACON
		case 0xDFF096:
		{
			if (data16 & 0x8000)
			{
				// set
				if (data16 & 1) startDMA(0);
				if (data16 & 2) startDMA(1);
				if (data16 & 4) startDMA(2);
				if (data16 & 8) startDMA(3);
			}
			else
			{
				// clear
				if (data16 & 1) stopDMA(0);
				if (data16 & 2) stopDMA(1);
				if (data16 & 4) stopDMA(2);
				if (data16 & 8) stopDMA(3);
			}
		}
		break;

		// AUDxLEN
		case 0xDFF0A4: audxlen(0, data16); break;
		case 0xDFF0B4: audxlen(1, data16); break;
		case 0xDFF0C4: audxlen(2, data16); break;
		case 0xDFF0D4: audxlen(3, data16); break;

		// AUDxPER
		case 0xDFF0A6: audxper(0, data16); break;
		case 0xDFF0B6: audxper(1, data16); break;
		case 0xDFF0C6: audxper(2, data16); break;
		case 0xDFF0D6: audxper(3, data16); break;

		// AUDxVOL
		case 0xDFF0A8: audxvol(0, data16); break;
		case 0xDFF0B8: audxvol(1, data16); break;
		case 0xDFF0C8: audxvol(2, data16); break;
		case 0xDFF0D8: audxvol(3, data16); break;

		default:
			return;
	}
}

void paulaWritePtr(uint32_t address, const int8_t *ptr)
{
	if (address == 0)
		return;

	switch (address)
	{
		// AUDxDAT
		case 0xDFF0A0: audxdat(0, ptr); break;
		case 0xDFF0B0: audxdat(1, ptr); break;
		case 0xDFF0C0: audxdat(2, ptr); break;
		case 0xDFF0D0: audxdat(3, ptr); break;

		default:
			return;
	}
}

void clearBlepState(void)
{
	memset(blep, 0, sizeof (blep));
}

// output is -4.00 .. 3.97 (can be louder because of high-pass filter)
void paulaGenerateSamples(float *fOutL, float *fOutR, int32_t numSamples)
{
	if (numSamples <= 0)
		return;

	float *fMixBufSelect[PAULA_VOICES] = { fOutL, fOutR, fOutR, fOutL };

	// clear mix buffer block
	memset(fOutL, 0, numSamples * sizeof (float));
	memset(fOutR, 0, numSamples * sizeof (float));

	// mix samples

	paulaVoice_t *v = paula;
	blep_t *b = blep;

	for (int32_t i = 0; i < PAULA_VOICES; i++, v++, b++)
	{
		if (!v->active || v->location == NULL || v->storedLocation == NULL)
			continue;

		float *fMixBuffer = fMixBufSelect[i]; // what output channel to mix into (L, R, R, L)
		for (int32_t j = 0; j < numSamples; j++)
		{
			if (v->nextSampleStage)
			{
				v->nextSampleStage = false;
				nextSample(v, b);
			}

			float fSample = v->fSample; // current sample, pre-multiplied by vol, scaled to -1.0 .. 0.992
			if (b->samplesLeft > 0)
				fSample = blepRun(b, fSample);

			fMixBuffer[j] += fSample;

			v->fPhase += v->fDelta;
			if (v->fPhase >= 1.0f)
			{
				v->fPhase -= 1.0f;
				refetchPeriod(v);
			}
		}
	}

	// apply Amiga filters
	for (int32_t i = 0; i < numSamples; i++)
	{
		float fOut[2];

		fOut[0] = fOutL[i];
		fOut[1] = fOutR[i];

		if (useLowpassFilter)
			onePoleLPFilterStereo(&filterLo, fOut, fOut);

		if (useLEDFilter)
			twoPoleLPFilterStereo(&filterLED, fOut, fOut);

		if (useHighpassFilter)
			onePoleHPFilterStereo(&filterHi, fOut, fOut);

		fOutL[i] = fOut[0];
		fOutR[i] = fOut[1];
	}
}
