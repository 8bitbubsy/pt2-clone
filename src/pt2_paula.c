/* Simple Paula emulator by 8bitbubsy (with BLEP synthesis by aciddose).
** Limitation: The audio output frequency can't be below 31389Hz ( ceil(PAULA_PAL_CLK / 113.0) )
**
** WARNING: These functions must not be called while paulaGenerateSamples() is running!
**          If so, lock the audio first so that you're sure it's not running.
*/

#include <stdint.h>
#include <stdbool.h>
#include "pt2_paula.h"
#include "pt2_blep.h"
#include "pt2_rcfilters.h"
#include "pt2_math.h"

typedef struct voice_t
{
	volatile bool DMA_active;

	// internal registers
	bool DMATriggerFlag, nextSampleStage;
	int8_t AUD_DAT[2]; // DMA data buffer
	const int8_t *location; // current location
	uint16_t lengthCounter; // current length
	int32_t sampleCounter; // how many bytes left in AUD_DAT
	double dSample; // currently held sample point (multiplied by volume)
	double dDelta, dPhase;

	// for BLEP synthesis
	double dLastDelta, dLastPhase, dLastDeltaMul, dBlepOffset, dDeltaMul;

	// registers modified by Paula functions
	const int8_t *AUD_LC; // location (data pointer)
	uint16_t AUD_LEN;
	double AUD_PER_delta, AUD_PER_deltamul;
	double AUD_VOL;
} paulaVoice_t;

static bool useLEDFilter, useLowpassFilter, useHighpassFilter;
static int8_t nullSample[0xFFFF*2]; // buffer for NULL data pointer
static double dPaulaOutputFreq, dPeriodToDeltaDiv;
static blep_t blep[PAULA_VOICES];
static onePoleFilter_t filterLo, filterHi;
static twoPoleFilter_t filterLED;
static paulaVoice_t paula[PAULA_VOICES];

void paulaSetup(double dOutputFreq, uint32_t amigaModel)
{
	assert(dOutputFreq != 0.0);
	dPaulaOutputFreq = dOutputFreq;
	dPeriodToDeltaDiv = PAULA_PAL_CLK / dPaulaOutputFreq;

	useLowpassFilter = useHighpassFilter = true;
	clearOnePoleFilterState(&filterLo);
	clearOnePoleFilterState(&filterHi);
	clearTwoPoleFilterState(&filterLED);

	/* Amiga 500/1200 filter emulation
	**
	** aciddose:
	** First comes a static low-pass 6dB formed by the supply current
	** from the Paula's mixture of channels A+B / C+D into the opamp with
	** 0.1uF capacitor and 360 ohm resistor feedback in inverting mode biased by
	** dac vRef (used to center the output).
	**
	** R = 360 ohm
	** C = 0.1uF
	** Low Hz = 4420.97~ = 1 / (2pi * 360 * 0.0000001)
	**
	** Under spice simulation the circuit yields -3dB = 4400Hz.
	**
	** Next comes a bog-standard Sallen-Key filter ("LED") with:
	** R1 = 10K ohm
	** R2 = 10K ohm
	** C1 = 6800pF
	** C2 = 3900pF
	**  Q = 0.660 (8bitbubsy: edited with correct nominal)
	**
	** This filter is optionally bypassed by an MPF-102 JFET chip when
	** the LED filter is turned off.
	**
	** Under spice simulation the circuit yields -3dB = 2800Hz.
	** 90 degrees phase = 3000Hz (so, should oscillate at 3kHz!)
	**
	** The buffered output of the Sallen-Key passes into an RC high-pass with:
	** R = 1.39K ohm (1K ohm + 390 ohm)
	** C = 22uF (also C = 330nF, for improved high-frequency)
	**
	** High Hz = 5.2~ = 1 / (2pi * 1390 * 0.000022)
	** Under spice simulation the circuit yields -3dB = 5.2Hz.
	**
	** 8bitbubsy:
	** Keep in mind that many of the Amiga schematics that are floating around on
	** the internet have wrong RC values! They were most likely very early schematics
	** that didn't change before production (or changes that never reached production).
	** This has been confirmed by measuring the components on several Amiga motherboards.
	**
	** Correct values for A500, >rev3 (?) (A500_R6.pdf):
	** - 1-pole RC 6dB/oct low-pass: R=360 ohm, C=0.1uF
	** - Sallen-key low-pass ("LED"): R1/R2=10k ohm, C1=6800pF, C2=3900pF
	** - 1-pole RC 6dB/oct high-pass: R=1390 ohm (1000+390), C=22.33uF (22+0.33)
	**
	** Correct values for A1200, all revs (A1200_R2.pdf):
	** - 1-pole RC 6dB/oct low-pass: R=680 ohm, C=6800pF
	** - Sallen-key low-pass ("LED"): R1/R2=10k ohm, C1=6800pF, C2=3900pF (same as A500)
	** - 1-pole RC 6dB/oct high-pass: R=1360 ohm (1000+360), C=22uF
	*/
	double R, C, R1, R2, C1, C2, cutoff, qfactor;

	if (amigaModel == MODEL_A500)
	{
		// A500 1-pole (6dB/oct) RC low-pass filter:
		R = 360.0; // R321 (360 ohm)
		C = 1e-7;  // C321 (0.1uF)
		cutoff = 1.0 / (PT2_TWO_PI * R * C); // ~4420.971Hz
		setupOnePoleFilter(dPaulaOutputFreq, cutoff, &filterLo);

		// A500 1-pole (6dB/oct) RC high-pass filter:
		R = 1390.0;   // R324 (1K ohm) + R325 (390 ohm)
		C = 2.233e-5; // C334 (22uF) + C335 (0.33uF)
		cutoff = 1.0 / (PT2_TWO_PI * R * C); // ~5.128Hz
		setupOnePoleFilter(dPaulaOutputFreq, cutoff, &filterHi);
	}
	else
	{
		/* Don't use the A1200 low-pass filter since its cutoff
		** is well above human hearable range anyway (~34.4kHz).
		** We don't do volume PWM, so we have nothing we need to
		** filter away.
		*/
		useLowpassFilter = false;

		// A1200 1-pole (6dB/oct) RC high-pass filter:
		R = 1360.0; // R324 (1K ohm resistor) + R325 (360 ohm resistor)
		C = 2.2e-5; // C334 (22uF capacitor)
		cutoff = 1.0 / (PT2_TWO_PI * R * C); // ~5.319Hz
		setupOnePoleFilter(dPaulaOutputFreq, cutoff, &filterHi);
	}

	// Note: A500 rev3 (old) -may- be C1 = 7500pF (cutoff = 2942.776Hz, qfactor = 0.693375)
	
	// 2-pole (12dB/oct) RC low-pass filter ("LED" filter, same values on A500/A1200):
	R1 = 10000.0; // R322 (10K ohm)
	R2 = 10000.0; // R323 (10K ohm)
	C1 = 6.8e-9;  // C322 (6800pF)
	C2 = 3.9e-9;  // C323 (3900pF)
	cutoff = 1.0 / (PT2_TWO_PI * pt2_sqrt(R1 * R2 * C1 * C2)); // ~3090.533Hz
	qfactor = pt2_sqrt(R1 * R2 * C1 * C2) / (C2 * (R1 + R2)); // ~0.660225
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

static void audxper(int32_t ch, uint16_t period)
{
	paulaVoice_t *v = &paula[ch];

	int32_t realPeriod = period;
	if (realPeriod == 0)
		realPeriod = 65536; // On Amiga: period 0 = period 65536 (1+65535)
	else if (realPeriod < 113)
		realPeriod = 113; // close to what happens on real Amiga (and low-limit needed for BLEP synthesis)

	// to be read on next sampling step (or on DMA trigger)
	v->AUD_PER_delta = dPeriodToDeltaDiv / realPeriod;
	v->AUD_PER_deltamul = 1.0 / v->AUD_PER_delta; // for BLEP synthesis (prevents division in inner mixing loop)

	// handle BLEP synthesis edge-cases

	if (v->dLastDelta == 0.0)
		v->dLastDelta = v->AUD_PER_delta;

	if (v->dLastDeltaMul == 0.0)
		v->dLastDeltaMul = v->AUD_PER_deltamul;
}

static void audxvol(int32_t ch, uint16_t vol)
{
	int32_t realVol = vol & 127;
	if (realVol > 64)
		realVol = 64;

	// multiplying sample point by this also scales the sample from -128..127 -> -1.000 .. ~0.992
	paula[ch].AUD_VOL = realVol * (1.0 / (128.0 * 64.0));
}

static void audxlen(int32_t ch, uint16_t len)
{
	paula[ch].AUD_LEN = len;
}

static void audxdat(int32_t ch, const int8_t *src)
{
	if (src == NULL)
		src = nullSample;

	paula[ch].AUD_LC = src;
}

static inline void refetchPeriod(paulaVoice_t *v) // Paula stage
{
	// set BLEP stuff
	v->dLastPhase = v->dPhase;
	v->dLastDelta = v->dDelta;
	v->dLastDeltaMul = v->dDeltaMul;
	v->dBlepOffset = v->dLastPhase * v->dLastDeltaMul;

	// Paula only updates period (delta) during period refetching (this stage)
	v->dDelta = v->AUD_PER_delta;
	v->dDeltaMul = v->AUD_PER_deltamul;

	v->nextSampleStage = true;
}

static void startDMA(int32_t ch)
{
	paulaVoice_t *v = &paula[ch];

	if (v->AUD_LC == NULL)
		v->AUD_LC = nullSample;

	// immediately update AUD_LC/AUD_LEN
	v->location = v->AUD_LC;
	v->lengthCounter = v->AUD_LEN;

	// make Paula fetch new samples immediately
	v->sampleCounter = 0;
	v->DMATriggerFlag = true;

	refetchPeriod(v);
	v->dPhase = 0.0; // kludge: must be cleared *after* refetchPeriod()

	v->DMA_active = true;
}

static void stopDMA(int32_t ch)
{
	paula[ch].DMA_active = false;
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

		default: return;
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

		default: return;
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

		default: return;
	}
}

static inline void nextSample(paulaVoice_t *v, blep_t *b)
{
	if (v->sampleCounter == 0)
	{
		// it's time to read new samples from DMA

		// don't update AUD_LEN/AUD_LC yet on DMA trigger
		if (!v->DMATriggerFlag)
		{
			if (--v->lengthCounter == 0)
			{
				v->lengthCounter = v->AUD_LEN;
				v->location = v->AUD_LC;
			}
		}

		v->DMATriggerFlag = false;

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
	v->dSample = v->AUD_DAT[0] * v->AUD_VOL; // -128..127 * 0.0 .. 1.0

	// fill BLEP buffer if the new sample differs from the old one
	if (v->dSample != b->dLastValue)
	{
		if (v->dLastDelta > v->dLastPhase)
			blepAdd(b, v->dBlepOffset, b->dLastValue - v->dSample);

		b->dLastValue = v->dSample;
	}

	// progress AUD_DAT buffer
	v->AUD_DAT[0] = v->AUD_DAT[1];
	v->sampleCounter--;
}

// output is -4.00 .. 3.97 (can be louder because of high-pass filter)
void paulaGenerateSamples(double *dOutL, double *dOutR, int32_t numSamples)
{
	double *dMixBufSelect[PAULA_VOICES];

	dMixBufSelect[0] = dOutL;
	dMixBufSelect[1] = dOutR;
	dMixBufSelect[2] = dOutR;
	dMixBufSelect[3] = dOutL;

	if (numSamples <= 0)
		return;

	// clear mix buffer block
	memset(dOutL, 0, numSamples * sizeof (double));
	memset(dOutR, 0, numSamples * sizeof (double));

	// mix samples

	paulaVoice_t *v = paula;
	blep_t *b = blep;

	for (int32_t i = 0; i < PAULA_VOICES; i++, v++, b++)
	{
		if (!v->DMA_active || v->location == NULL || v->AUD_LC == NULL)
			continue;

		double *dMixBuffer = dMixBufSelect[i]; // what output channel to mix into (L, R, R, L)
		for (int32_t j = 0; j < numSamples; j++)
		{
			if (v->nextSampleStage)
			{
				v->nextSampleStage = false;
				nextSample(v, b);
			}

			double dSample = v->dSample; // current sample, pre-multiplied by vol, scaled to -1.0 .. 0.992
			if (b->samplesLeft > 0)
				dSample = blepRun(b, dSample);

			dMixBuffer[j] += dSample;

			v->dPhase += v->dDelta;
			if (v->dPhase >= 1.0)
			{
				v->dPhase -= 1.0;
				refetchPeriod(v);
			}
		}
	}

	// apply Amiga filters
	for (int32_t i = 0; i < numSamples; i++)
	{
		double dOut[2];

		dOut[0] = dOutL[i];
		dOut[1] = dOutR[i];

		if (useLowpassFilter)
			onePoleLPFilterStereo(&filterLo, dOut, dOut);

		if (useLEDFilter)
			twoPoleLPFilterStereo(&filterLED, dOut, dOut);

		if (useHighpassFilter)
			onePoleHPFilterStereo(&filterHi, dOut, dOut);

		dOutL[i] = dOut[0];
		dOutR[i] = dOut[1];
	}
}
