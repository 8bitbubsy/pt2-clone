/* The "LED" filter and BLEP routines were coded by aciddose.
** Low-pass filter is based on https://bel.fi/alankila/modguide/interpolate.txt */

// for finding memory leaks in debug mode with Visual Studio 
#if defined _DEBUG && defined _MSC_VER
#include <crtdbg.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <SDL2/SDL.h>
#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif
#include <math.h> // sqrt(),tan(),M_PI
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <limits.h>
#include "pt2_audio.h"
#include "pt2_header.h"
#include "pt2_helpers.h"
#include "pt2_blep.h"
#include "pt2_config.h"
#include "pt2_tables.h"
#include "pt2_palette.h"
#include "pt2_textout.h"
#include "pt2_visuals.h"
#include "pt2_scopes.h"

#define INITIAL_DITHER_SEED 0x12345000

typedef struct ledFilter_t
{
	double dLed[4];
} ledFilter_t;

typedef struct ledFilterCoeff_t
{
	double dLed, dLedFb;
} ledFilterCoeff_t;

typedef struct voice_t
{
	volatile bool active;
	const int8_t *data, *newData;
	int32_t length, newLength, pos;
	double dVolume, dDelta, dDeltaMul, dPhase, dLastDelta, dLastDeltaMul, dLastPhase, dPanL, dPanR;
} paulaVoice_t;

static volatile int8_t filterFlags;
static volatile bool audioLocked;
static int8_t defStereoSep;
static bool amigaPanFlag, wavRenderingDone;
static uint16_t ch1Pan, ch2Pan, ch3Pan, ch4Pan, oldPeriod;
static int32_t sampleCounter, maxSamplesToMix, randSeed = INITIAL_DITHER_SEED;
static uint32_t oldScopeDelta;
static double *dMixBufferL, *dMixBufferR, *dMixBufferLUnaligned, *dMixBufferRUnaligned, dOldVoiceDelta, dOldVoiceDeltaMul;
static double dPrngStateL, dPrngStateR;
static blep_t blep[AMIGA_VOICES], blepVol[AMIGA_VOICES];
static lossyIntegrator_t filterLo, filterHi;
static ledFilterCoeff_t filterLEDC;
static ledFilter_t filterLED;
static paulaVoice_t paula[AMIGA_VOICES];
static SDL_AudioDeviceID dev;

// globalized
bool forceMixerOff = false;
int32_t samplesPerTick;

bool intMusic(void); // defined in pt_modplayer.c
void storeTempVariables(void); // defined in pt_modplayer.c

void calcMod2WavTotalRows(void);

static uint16_t bpm2SmpsPerTick(uint32_t bpm, uint32_t audioFreq)
{
	uint32_t ciaVal;
	double dFreqMul;

	if (bpm == 0)
		return 0;

	ciaVal = (uint32_t)(1773447 / bpm); // yes, PT truncates here
	dFreqMul = ciaVal * (1.0 / CIA_PAL_CLK);

	int32_t smpsPerTick = (int32_t)((audioFreq * dFreqMul) + 0.5);
	return (uint16_t)smpsPerTick;
}

static void generateBpmTables(void)
{
	for (uint32_t i = 32; i <= 255; i++)
	{
		audio.bpmTab[i-32] = bpm2SmpsPerTick(i, audio.audioFreq);
		audio.bpmTab28kHz[i-32] = bpm2SmpsPerTick(i, 28836);
		audio.bpmTab22kHz[i-32] = bpm2SmpsPerTick(i, 22168);
	}
}

void setLEDFilter(bool state)
{
	editor.useLEDFilter = state;

	if (editor.useLEDFilter)
		filterFlags |=  FILTER_LED_ENABLED;
	else
		filterFlags &= ~FILTER_LED_ENABLED;
}

void toggleLEDFilter(void)
{
	editor.useLEDFilter ^= 1;

	if (editor.useLEDFilter)
		filterFlags |=  FILTER_LED_ENABLED;
	else
		filterFlags &= ~FILTER_LED_ENABLED;
}

static void calcCoeffLED(double dSr, double dHz, ledFilterCoeff_t *filter)
{
	static double dFb = 0.125;

#ifndef NO_FILTER_FINETUNING
	/* 8bitbubsy: makes the filter curve sound (and look) much closer to the real deal.
	** This has been tested against both an A500 and A1200 (real units). */
	dFb *= 0.62;
#endif

	if (dHz < dSr/2.0)
		filter->dLed = ((2.0 * M_PI) * dHz) / dSr;
	else
		filter->dLed = 1.0;

	filter->dLedFb = dFb + (dFb / (1.0 - filter->dLed)); // Q ~= 1/sqrt(2) (Butterworth)
}

void calcCoeffLossyIntegrator(double dSr, double dHz, lossyIntegrator_t *filter)
{
	double dOmega = ((2.0 * M_PI) * dHz) / dSr;
	filter->b0 = 1.0 / (1.0 + (1.0 / dOmega));
	filter->b1 = 1.0 - filter->b0;
}

static void clearLossyIntegrator(lossyIntegrator_t *filter)
{
	filter->dBuffer[0] = 0.0; // L
	filter->dBuffer[1] = 0.0; // R
}

static void clearLEDFilter(ledFilter_t *filter)
{
	filter->dLed[0] = 0.0; // L
	filter->dLed[1] = 0.0;
	filter->dLed[2] = 0.0; // R
	filter->dLed[3] = 0.0;
}

static inline void lossyIntegratorLED(ledFilterCoeff_t filterC, ledFilter_t *filter, double *dIn, double *dOut)
{
	// left channel "LED" filter
	filter->dLed[0] += filterC.dLed * (dIn[0] - filter->dLed[0])
		+ filterC.dLedFb * (filter->dLed[0] - filter->dLed[1]) + DENORMAL_OFFSET;
	filter->dLed[1] += filterC.dLed * (filter->dLed[0] - filter->dLed[1]) + DENORMAL_OFFSET;
	dOut[0] = filter->dLed[1];

	// right channel "LED" filter
	filter->dLed[2] += filterC.dLed * (dIn[1] - filter->dLed[2])
		+ filterC.dLedFb * (filter->dLed[2] - filter->dLed[3]) + DENORMAL_OFFSET;
	filter->dLed[3] += filterC.dLed * (filter->dLed[2] - filter->dLed[3]) + DENORMAL_OFFSET;
	dOut[1] = filter->dLed[3];
}

void lossyIntegrator(lossyIntegrator_t *filter, double *dIn, double *dOut)
{
	/* Low-pass filter implementation taken from:
	** https://bel.fi/alankila/modguide/interpolate.txt
	**
	** This implementation has a less smooth cutoff curve compared to the old one, so it's
	** maybe not the best. However, I stick to this one because it has a higher gain
	** at the end of the curve (closer to real tested Amiga 500). It also sounds much closer when
	** comparing whitenoise on an A500. */

	// left channel low-pass
	filter->dBuffer[0] = (filter->b0 * dIn[0]) + (filter->b1 * filter->dBuffer[0]) + DENORMAL_OFFSET;
	dOut[0] = filter->dBuffer[0];

	// right channel low-pass
	filter->dBuffer[1] = (filter->b0 * dIn[1]) + (filter->b1 * filter->dBuffer[1]) + DENORMAL_OFFSET;
	dOut[1] = filter->dBuffer[1];
}

void lossyIntegratorMono(lossyIntegrator_t *filter, double dIn, double *dOut)
{
	filter->dBuffer[0] = (filter->b0 * dIn) + (filter->b1 * filter->dBuffer[0]) + DENORMAL_OFFSET;
	*dOut = filter->dBuffer[0];
}

void lossyIntegratorHighPass(lossyIntegrator_t *filter, double *dIn, double *dOut)
{
	double dLow[2];

	lossyIntegrator(filter, dIn, dLow);

	dOut[0] = dIn[0] - dLow[0]; // left channel high-pass
	dOut[1] = dIn[1] - dLow[1]; // right channel high-pass
}

void lossyIntegratorHighPassMono(lossyIntegrator_t *filter, double dIn, double *dOut)
{
	double dLow;

	lossyIntegratorMono(filter, dIn, &dLow);

	*dOut = dIn - dLow;
}

/* adejr/aciddose: these sin/cos approximations both use a 0..1
** parameter range and have 'normalized' (1/2 = 0db) coeffs
**
** the coeffs are for LERP(x, x * x, 0.224) * sqrt(2)
** max_error is minimized with 0.224 = 0.0013012886 */
static double sinApx(double fX)
{
	fX = fX * (2.0 - fX);
	return fX * 1.09742972 + fX * fX * 0.31678383;
}

static double cosApx(double fX)
{
	fX = (1.0 - fX) * (1.0 + fX);
	return fX * 1.09742972 + fX * fX * 0.31678383;
}

void lockAudio(void)
{
	if (dev != 0)
		SDL_LockAudioDevice(dev);

	audioLocked = true;
}

void unlockAudio(void)
{
	if (dev != 0)
		SDL_UnlockAudioDevice(dev);

	audioLocked = false;
}

void clearPaulaAndScopes(void)
{
	uint8_t i;
	double dOldPanL[4], dOldPanR[4];

	// copy old pans
	for (i = 0; i < AMIGA_VOICES; i++)
	{
		dOldPanL[i] = paula[i].dPanL;
		dOldPanR[i] = paula[i].dPanR;
	}

	lockAudio();
	memset(paula, 0, sizeof (paula));
	unlockAudio();

	// store old pans
	for (i = 0; i < AMIGA_VOICES; i++)
	{
		paula[i].dPanL = dOldPanL[i];
		paula[i].dPanR = dOldPanR[i];
	}

	clearScopes();
}

void mixerUpdateLoops(void) // updates Paula loop (+ scopes)
{
	moduleChannel_t *ch;
	moduleSample_t *s;

	for (uint8_t i = 0; i < AMIGA_VOICES; i++)
	{
		ch = &modEntry->channels[i];
		if (ch->n_samplenum == editor.currSample)
		{
			s = &modEntry->samples[editor.currSample];
			paulaSetData(i, ch->n_start + s->loopStart);
			paulaSetLength(i, s->loopLength / 2);
		}
	}
}

static void mixerSetVoicePan(uint8_t ch, uint16_t pan) // pan = 0..256
{
	double dPan;

	/* proper 'normalized' equal-power panning is (assuming pan left to right):
	** L = cos(p * pi * 1/2) * sqrt(2);
	** R = sin(p * pi * 1/2) * sqrt(2); */
	dPan = pan * (1.0 / 256.0); // 0.0..1.0

	paula[ch].dPanL = cosApx(dPan);
	paula[ch].dPanR = sinApx(dPan);
}

void mixerKillVoice(uint8_t ch)
{
	paulaVoice_t *v;
	scopeChannelExt_t *s;

	v = &paula[ch];
	s = &scopeExt[ch];

	v->active = false;
	v->dVolume = 0.0;

	s->active = false;
	s->didSwapData = false;

	memset(&blep[ch], 0, sizeof (blep_t));
	memset(&blepVol[ch], 0, sizeof (blep_t));
}

void turnOffVoices(void)
{
	for (uint8_t i = 0; i < AMIGA_VOICES; i++)
		mixerKillVoice(i);

	clearLossyIntegrator(&filterLo);
	clearLossyIntegrator(&filterHi);
	clearLEDFilter(&filterLED);

	resetAudioDithering();

	editor.tuningFlag = false;
}

void paulaStopDMA(uint8_t ch)
{
	scopeExt[ch].active = paula[ch].active = false;
}

void paulaStartDMA(uint8_t ch)
{
	const int8_t *dat;
	int32_t length;
	paulaVoice_t *v;
	scopeChannel_t s, *sc;
	scopeChannelExt_t *se;

	// trigger voice

	v  = &paula[ch];

	dat = v->newData;
	if (dat == NULL)
		dat = &modEntry->sampleData[RESERVED_SAMPLE_OFFSET]; // dummy sample

	length = v->newLength;
	if (length < 2)
		length = 2; // for safety

	v->dPhase = 0.0;
	v->pos = 0;
	v->data = dat;
	v->length = length;
	v->active = true;

	// trigger scope

	sc = &scope[ch];
	se = &scopeExt[ch];
	s = *sc; // cache it

	dat = se->newData;
	if (dat == NULL)
		dat = &modEntry->sampleData[RESERVED_SAMPLE_OFFSET]; // dummy sample

	s.length = length;
	s.data = dat;

	s.pos = 0;
	s.posFrac = 0;

	// data/length is already set from replayer thread (important)
	s.loopFlag = se->newLoopFlag;
	s.loopStart = se->newLoopStart;

	se->didSwapData = false;
	se->active = true;

	*sc = s; // update it
}

void resetCachedMixerPeriod(void)
{
	oldPeriod = 0;
}

void paulaSetPeriod(uint8_t ch, uint16_t period)
{
	double dPeriodToDeltaDiv;
	paulaVoice_t *v;

	v = &paula[ch];

	if (period == 0)
	{
		v->dDelta = 0.0; // confirmed behavior on real Amiga
		v->dDeltaMul = 1.0; // for BLEP synthesis
		setScopeDelta(ch, 0);
		return;
	}

	if (period < 113)
		period = 113; // confirmed behavior on real Amiga

	// if the new period was the same as the previous period, use cached deltas
	if (period == oldPeriod)
	{
		v->dDelta = dOldVoiceDelta;
		v->dDeltaMul = dOldVoiceDeltaMul; // for BLEP synthesis
		setScopeDelta(ch, oldScopeDelta);
	}
	else 
	{
		// this period is not cached, calculate mixer/scope deltas

#if SCOPE_HZ != 64
#error Scope Hz is not 64 (2^n), change rate calc. to use doubles+round in pt2_scope.c
#endif
		oldPeriod = period;

		// if we are rendering pattern to sample (PAT2SMP), use different frequencies
		if (editor.isSMPRendering)
			dPeriodToDeltaDiv = editor.pat2SmpHQ ? (PAULA_PAL_CLK / 28836.0) : (PAULA_PAL_CLK / 22168.0);
		else
			dPeriodToDeltaDiv = audio.dPeriodToDeltaDiv;

		v->dDelta = dPeriodToDeltaDiv / period;
		v->dDeltaMul = 1.0 / v->dDelta; // for BLEP synthesis

		// cache these
		dOldVoiceDelta = v->dDelta;
		dOldVoiceDeltaMul = v->dDeltaMul; // for BLEP synthesis
		oldScopeDelta = (PAULA_PAL_CLK * (65536UL / SCOPE_HZ)) / period;

		setScopeDelta(ch, oldScopeDelta);
	}

	// for BLEP synthesis

	if (v->dLastDelta == 0.0)
		v->dLastDelta = v->dDelta;

	if (v->dLastDeltaMul == 0.0)
		v->dLastDeltaMul = v->dDeltaMul;
}

void paulaSetVolume(uint8_t ch, uint16_t vol)
{
	vol &= 127; // confirmed behavior on real Amiga

	if (vol > 64)
		vol = 64; // confirmed behavior on real Amiga

	paula[ch].dVolume = vol * (1.0 / 64.0);
}

void paulaSetLength(uint8_t ch, uint16_t len)
{
	if (len == 0)
	{
		len = 65535;
		/* confirmed behavior on real Amiga (also needed for safety)
		** And yes, we have room for this, it will never overflow!
		*/
	}

	// our mixer works with bytes, not words. Multiply by two
	scopeExt[ch].newLength = paula[ch].newLength = len * 2;
}

void paulaSetData(uint8_t ch, const int8_t *src)
{
	uint8_t smp;
	moduleSample_t *s;
	scopeChannelExt_t *se, tmp;

	smp = modEntry->channels[ch].n_samplenum;
	assert(smp <= 30);
	s = &modEntry->samples[smp];

	// set voice data
	if (src == NULL)
		src = &modEntry->sampleData[RESERVED_SAMPLE_OFFSET]; // dummy sample

	paula[ch].newData = src;

	// set external scope data
	se = &scopeExt[ch];
	tmp = *se; // cache it

	tmp.newData = src;
	tmp.newLoopFlag = (s->loopStart + s->loopLength) > 2;
	tmp.newLoopStart = s->loopStart;

	*se = tmp; // update it
}

void toggleA500Filters(void)
{
	if (filterFlags & FILTER_A500)
	{
		filterFlags &= ~FILTER_A500;
		displayMsg("FILTER MOD: A1200");
	}
	else
	{
		filterFlags |= FILTER_A500;
		clearLossyIntegrator(&filterLo);
		displayMsg("FILTER MOD: A500");
	}
}

void mixChannels(int32_t numSamples)
{
	const int8_t *dataPtr;
	double dTempSample, dTempVolume;
	blep_t *bSmp, *bVol;
	paulaVoice_t *v;

	memset(dMixBufferL, 0, numSamples * sizeof (double));
	memset(dMixBufferR, 0, numSamples * sizeof (double));

	for (int32_t i = 0; i < AMIGA_VOICES; i++)
	{
		v = &paula[i];
		if (!v->active)
			continue;

		bSmp = &blep[i];
		bVol = &blepVol[i];

		for (int32_t j = 0; j < numSamples; j++)
		{
			dataPtr = v->data;
			if (dataPtr == NULL)
			{
				dTempSample = 0.0;
				dTempVolume = 0.0;
			}
			else
			{
				dTempSample = dataPtr[v->pos] * (1.0 / 128.0);
				dTempVolume = v->dVolume;
			}

			if (dTempSample != bSmp->dLastValue)
			{
				if (v->dLastDelta > v->dLastPhase)
				{
					// div->mul trick: v->dLastDeltaMul is 1.0 / v->dLastDelta
					blepAdd(bSmp, v->dLastPhase * v->dLastDeltaMul, bSmp->dLastValue - dTempSample);
				}

				bSmp->dLastValue = dTempSample;
			}

			if (dTempVolume != bVol->dLastValue)
			{
				blepVolAdd(bVol, bVol->dLastValue - dTempVolume);
				bVol->dLastValue = dTempVolume;
			}

			if (bSmp->samplesLeft > 0) dTempSample += blepRun(bSmp);
			if (bVol->samplesLeft > 0) dTempVolume += blepRun(bVol);

			dTempSample *= dTempVolume;

			dMixBufferL[j] += dTempSample * v->dPanL;
			dMixBufferR[j] += dTempSample * v->dPanR;

			v->dPhase += v->dDelta;

			// PAT2SMP needs multi-step, so use while() here (will be only one iteration in normal mixing mode)
			while (v->dPhase >= 1.0)
			{
				v->dPhase -= 1.0;

				v->dLastPhase = v->dPhase;
				v->dLastDelta = v->dDelta;
				v->dLastDeltaMul = v->dDeltaMul;

				if (++v->pos >= v->length)
				{
					v->pos = 0;

					// re-fetch new Paula register values now
					v->length = v->newLength;
					v->data = v->newData;
				}
			}
		}
	}
}

void resetAudioDithering(void)
{
	randSeed = INITIAL_DITHER_SEED;
	dPrngStateL = 0.0;
	dPrngStateR = 0.0;
}

static inline int32_t random32(void)
{
	// LCG random 32-bit generator (quite good and fast)
	randSeed *= 134775813;
	randSeed++;
	return randSeed;
}

static inline void processMixedSamplesA1200(int32_t i, int16_t *out)
{
	int32_t smp32;
	double dOut[2], dPrng;

	dOut[0] = dMixBufferL[i];
	dOut[1] = dMixBufferR[i];

	// don't process any low-pass filter since the cut-off is around 28-31kHz on A1200

	// process "LED" filter
	if (filterFlags & FILTER_LED_ENABLED)
		lossyIntegratorLED(filterLEDC, &filterLED, dOut, dOut);

	// process high-pass filter
	lossyIntegratorHighPass(&filterHi, dOut, dOut);

	// normalize and flip phase (A500/A1200 has an inverted audio signal)
	dOut[0] *= -(INT16_MAX / AMIGA_VOICES);
	dOut[1] *= -(INT16_MAX / AMIGA_VOICES);

	// left channel - 1-bit triangular dithering (high-pass filtered)
	dPrng = random32() * (0.5 / INT32_MAX); // -0.5..0.5
	dOut[0] = (dOut[0] + dPrng) - dPrngStateL;
	dPrngStateL = dPrng;
	smp32 = (int32_t)dOut[0];
	CLAMP16(smp32);
	out[0] = (int16_t)smp32;

	// right channel - 1-bit triangular dithering (high-pass filtered)
	dPrng = random32() * (0.5 / INT32_MAX); // -0.5..0.5
	dOut[1] = (dOut[1] + dPrng) - dPrngStateR;
	dPrngStateR = dPrng;
	smp32 = (int32_t)dOut[1];
	CLAMP16(smp32);
	out[1] = (int16_t)smp32;
}

static inline void processMixedSamplesA500(int32_t i, int16_t *out)
{
	int32_t smp32;
	double dOut[2], dPrng;

	dOut[0] = dMixBufferL[i];
	dOut[1] = dMixBufferR[i];

	// process low-pass filter
	lossyIntegrator(&filterLo, dOut, dOut);

	// process "LED" filter
	if (filterFlags & FILTER_LED_ENABLED)
		lossyIntegratorLED(filterLEDC, &filterLED, dOut, dOut);

	// process high-pass filter
	lossyIntegratorHighPass(&filterHi, dOut, dOut);

	dOut[0] *= -(INT16_MAX / AMIGA_VOICES);
	dOut[1] *= -(INT16_MAX / AMIGA_VOICES);

	// left channel - 1-bit triangular dithering (high-pass filtered)
	dPrng = random32() * (0.5 / INT32_MAX); // -0.5..0.5
	dOut[0] = (dOut[0] + dPrng) - dPrngStateL;
	dPrngStateL = dPrng;
	smp32 = (int32_t)dOut[0];
	CLAMP16(smp32);
	out[0] = (int16_t)smp32;

	// right channel - 1-bit triangular dithering (high-pass filtered)
	dPrng = random32() * (0.5 / INT32_MAX); // -0.5..0.5
	dOut[1] = (dOut[1] + dPrng) - dPrngStateR;
	dPrngStateR = dPrng;
	smp32 = (int32_t)dOut[1];
	CLAMP16(smp32);
	out[1] = (int16_t)smp32;
}

void outputAudio(int16_t *target, int32_t numSamples)
{
	int16_t *outStream, out[2];
	int32_t j;

	mixChannels(numSamples);

	if (editor.isSMPRendering)
	{
		// render to sample (PAT2SMP)

		for (j = 0; j < numSamples; j++)
		{
			processMixedSamplesA1200(j, out);
			editor.pat2SmpBuf[editor.pat2SmpPos++] = (int16_t)((out[0] + out[1]) >> 1); // mix to mono

			if (editor.pat2SmpPos >= MAX_SAMPLE_LEN)
			{
				editor.smpRenderingDone = true;
				updateWindowTitle(MOD_IS_MODIFIED);
				break;
			}
		}
	}
	else
	{
		// render to stream

		outStream = target;
		if (filterFlags & FILTER_A500)
		{
			for (j = 0; j < numSamples; j++)
			{
				processMixedSamplesA500(j, out);
				*outStream++ = out[0];
				*outStream++ = out[1];
			}
		}
		else
		{
			for (j = 0; j < numSamples; j++)
			{
				processMixedSamplesA1200(j, out);
				*outStream++ = out[0];
				*outStream++ = out[1];
			}
		}
	}
}

static void SDLCALL audioCallback(void *userdata, Uint8 *stream, int len)
{
	int16_t *out;
	int32_t sampleBlock, samplesTodo;

	(void)userdata;

	if (forceMixerOff) // during MOD2WAV
	{
		memset(stream, 0, len);
		return;
	}

	out = (int16_t *)stream;

	sampleBlock = len >> 2;
	while (sampleBlock)
	{
		samplesTodo = (sampleBlock < sampleCounter) ? sampleBlock : sampleCounter;
		if (samplesTodo > 0)
		{
			outputAudio(out, samplesTodo);
			out += (uint32_t)samplesTodo * 2;

			sampleBlock -= samplesTodo;
			sampleCounter -= samplesTodo;
		}
		else
		{
			if (editor.songPlaying)
				intMusic();

			sampleCounter = samplesPerTick;
		}
	}
}

static void calculateFilterCoeffs(void)
{
	double dCutOffHz;

	/* Amiga 500 filter emulation, by aciddose
	**
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
	** In the Amiga 1200, the low-pass cutoff is 26kHz+, so the
	** static low-pass filter is disabled in the mixer in A1200 mode.
	**
	** Next comes a bog-standard Sallen-Key filter ("LED") with:
	** R1 = 10K ohm
	** R2 = 10K ohm
	** C1 = 6800pF
	** C2 = 3900pF
	** Q ~= 1/sqrt(2)
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
	*/

	// Amiga 500 rev6 RC low-pass filter:
	const double dLp_R = 360.0; // R321 - 360 ohm resistor
	const double dLp_C = 1e-7;  // C321 - 0.1uF capacitor
	dCutOffHz = 1.0 / ((2.0 * M_PI) * dLp_R * dLp_C); // ~4420.97Hz
#ifndef NO_FILTER_FINETUNING
	dCutOffHz += 580.0; // 8bitbubsy: finetuning to better match A500 low-pass testing
#endif
	calcCoeffLossyIntegrator(audio.dAudioFreq, dCutOffHz, &filterLo);

	// Amiga Sallen-Key "LED" filter:
	const double dLed_R1 = 10000.0; // R322 - 10K ohm resistor
	const double dLed_R2 = 10000.0; // R323 - 10K ohm resistor
	const double dLed_C1 = 6.8e-9;  // C322 - 6800pF capacitor
	const double dLed_C2 = 3.9e-9;  // C323 - 3900pF capacitor
	dCutOffHz = 1.0 / ((2.0 * M_PI) * sqrt(dLed_R1 * dLed_R2 * dLed_C1 * dLed_C2)); // ~3090.53Hz
#ifndef NO_FILTER_FINETUNING
	dCutOffHz -= 300.0; // 8bitbubsy: finetuning to better match A500 & A1200 "LED" filter testing
#endif
	calcCoeffLED(audio.dAudioFreq, dCutOffHz, &filterLEDC);

	// Amiga RC high-pass filter:
	const double dHp_R = 1000.0 + 390.0; // R324 - 1K ohm resistor + R325 - 390 ohm resistor
	const double dHp_C = 2.2e-5; // C334 - 22uF capacitor
	dCutOffHz = 1.0 / ((2.0 * M_PI) * dHp_R * dHp_C); // ~5.20Hz
#ifndef NO_FILTER_FINETUNING
	dCutOffHz += 1.5; // 8bitbubsy: finetuning to better match A500 & A1200 high-pass testing
#endif
	calcCoeffLossyIntegrator(audio.dAudioFreq, dCutOffHz, &filterHi);
}

void mixerCalcVoicePans(uint8_t stereoSeparation)
{
	uint8_t scaledPanPos = (stereoSeparation * 128) / 100;

	ch1Pan = 128 - scaledPanPos;
	ch2Pan = 128 + scaledPanPos;
	ch3Pan = 128 + scaledPanPos;
	ch4Pan = 128 - scaledPanPos;

	mixerSetVoicePan(0, ch1Pan);
	mixerSetVoicePan(1, ch2Pan);
	mixerSetVoicePan(2, ch3Pan);
	mixerSetVoicePan(3, ch4Pan);
}

bool setupAudio(void)
{
	SDL_AudioSpec want, have;

	want.freq = ptConfig.soundFrequency;
	want.format = AUDIO_S16;
	want.channels = 2;
	want.callback = audioCallback;
	want.userdata = NULL;
	want.samples = ptConfig.soundBufferSize;

	dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
	if (dev == 0)
	{
		showErrorMsgBox("Unable to open audio device: %s", SDL_GetError());
		return false;
	}

	if (have.freq < 32000) // lower than this is not safe for one-step mixer w/ BLEP
	{
		showErrorMsgBox("Unable to open audio: The audio output rate couldn't be used!");
		return false;
	}

	if (have.format != want.format)
	{
		showErrorMsgBox("Unable to open audio: The sample format (signed 16-bit) couldn't be used!");
		return false;
	}

	maxSamplesToMix = (int32_t)ceil((have.freq * 2.5) / 32.0);

	dMixBufferLUnaligned = (double *)MALLOC_PAD(maxSamplesToMix * sizeof (double), 256);
	dMixBufferRUnaligned = (double *)MALLOC_PAD(maxSamplesToMix * sizeof (double), 256);

	editor.mod2WavBuffer = (int16_t *)malloc(sizeof (int16_t) * maxSamplesToMix);

	if (dMixBufferLUnaligned == NULL || dMixBufferRUnaligned == NULL || editor.mod2WavBuffer == NULL)
	{
		showErrorMsgBox("Out of memory!");
		return false;
	}

	dMixBufferL = (double *)ALIGN_PTR(dMixBufferLUnaligned, 256);
	dMixBufferR = (double *)ALIGN_PTR(dMixBufferRUnaligned, 256);

	audio.audioBufferSize = have.samples;
	ptConfig.soundFrequency = have.freq;
	audio.audioFreq = ptConfig.soundFrequency;
	audio.dAudioFreq = (double)ptConfig.soundFrequency;
	audio.dPeriodToDeltaDiv = PAULA_PAL_CLK / audio.dAudioFreq;

	mixerCalcVoicePans(ptConfig.stereoSeparation);
	defStereoSep = ptConfig.stereoSeparation;

	filterFlags = ptConfig.a500LowPassFilter ? FILTER_A500 : 0;

	calculateFilterCoeffs();
	generateBpmTables();

	samplesPerTick = 0;
	sampleCounter = 0;

	SDL_PauseAudioDevice(dev, false);
	return true;
}

void audioClose(void)
{
	if (dev > 0)
	{
		SDL_PauseAudioDevice(dev, true);
		SDL_CloseAudioDevice(dev);
		dev = 0;
	}

	if (dMixBufferLUnaligned != NULL)
	{
		free(dMixBufferLUnaligned);
		dMixBufferLUnaligned = NULL;
	}

	if (dMixBufferRUnaligned != NULL)
	{
		free(dMixBufferRUnaligned);
		dMixBufferRUnaligned = NULL;
	}

	if (editor.mod2WavBuffer != NULL)
	{
		free(editor.mod2WavBuffer);
		editor.mod2WavBuffer = NULL;
	}
}

void mixerSetSamplesPerTick(int32_t val)
{
	samplesPerTick = val;
}

void mixerClearSampleCounter(void)
{
	sampleCounter = 0;
}

void toggleAmigaPanMode(void)
{
	amigaPanFlag ^= 1;
	if (!amigaPanFlag)
	{
		mixerCalcVoicePans(defStereoSep);
		displayMsg("AMIGA PANNING OFF");
	}
	else
	{
		mixerCalcVoicePans(100);
		displayMsg("AMIGA PANNING ON");
	}
}

// PAT2SMP RELATED STUFF

uint32_t getAudioFrame(int16_t *outStream)
{
	int32_t smpCounter, samplesToMix;

	if (!intMusic())
		wavRenderingDone = true;

	smpCounter = samplesPerTick;
	while (smpCounter > 0)
	{
		samplesToMix = smpCounter;
		if (samplesToMix > maxSamplesToMix)
			samplesToMix = maxSamplesToMix;

		outputAudio(outStream, samplesToMix);
		outStream += (uint32_t)samplesToMix * 2;

		smpCounter -= samplesToMix;
	}

	return (uint32_t)samplesPerTick * 2; // * 2 for stereo
}

static int32_t SDLCALL mod2WavThreadFunc(void *ptr)
{
	uint32_t size, totalSampleCounter, totalRiffChunkLen;
	FILE *fOut;
	wavHeader_t wavHeader;

	fOut = (FILE *)ptr;
	if (fOut == NULL)
		return true;

	// skip wav header place, render data first
	fseek(fOut, sizeof (wavHeader_t), SEEK_SET);

	wavRenderingDone = false;

	totalSampleCounter = 0;
	while (editor.isWAVRendering && !wavRenderingDone && !editor.abortMod2Wav)
	{
		size = getAudioFrame(editor.mod2WavBuffer);
		if (size > 0)
		{
			fwrite(editor.mod2WavBuffer, sizeof (int16_t), size, fOut);
			totalSampleCounter += size;
		}

		editor.ui.updateMod2WavDialog = true;
	}

	if (totalSampleCounter & 1)
		fputc(0, fOut); // pad align byte

	if ((ftell(fOut) - 8) > 0)
		totalRiffChunkLen = ftell(fOut) - 8;
	else
		totalRiffChunkLen = 0;

	editor.ui.mod2WavFinished = true;
	editor.ui.updateMod2WavDialog = true;

	// go back and fill the missing WAV header
	fseek(fOut, 0, SEEK_SET);

	wavHeader.chunkID = 0x46464952; // "RIFF"
	wavHeader.chunkSize = totalRiffChunkLen;
	wavHeader.format = 0x45564157; // "WAVE"
	wavHeader.subchunk1ID = 0x20746D66; // "fmt "
	wavHeader.subchunk1Size = 16;
	wavHeader.audioFormat = 1;
	wavHeader.numChannels = 2;
	wavHeader.sampleRate = audio.audioFreq;
	wavHeader.bitsPerSample = 16;
	wavHeader.byteRate = wavHeader.sampleRate * wavHeader.numChannels * (wavHeader.bitsPerSample / 8);
	wavHeader.blockAlign = wavHeader.numChannels * (wavHeader.bitsPerSample / 8);
	wavHeader.subchunk2ID = 0x61746164; // "data"
	wavHeader.subchunk2Size = totalSampleCounter * (wavHeader.bitsPerSample / 8);

	fwrite(&wavHeader, sizeof (wavHeader_t), 1, fOut);
	fclose(fOut);

	return true;
}

bool renderToWav(char *fileName, bool checkIfFileExist)
{
	FILE *fOut;
	struct stat statBuffer;

	if (checkIfFileExist)
	{
		if (stat(fileName, &statBuffer) == 0)
		{
			editor.ui.askScreenShown = true;
			editor.ui.askScreenType = ASK_MOD2WAV_OVERWRITE;

			pointerSetMode(POINTER_MODE_MSG1, NO_CARRY);
			setStatusMessage("OVERWRITE FILE?", NO_CARRY);

			renderAskDialog();

			return false;
		}
	}

	if (editor.ui.askScreenShown)
	{
		editor.ui.askScreenShown = false;
		editor.ui.answerNo = false;
		editor.ui.answerYes = false;
	}

	fOut = fopen(fileName, "wb");
	if (fOut == NULL)
	{
		displayErrorMsg("FILE I/O ERROR");
		return false;
	}

	storeTempVariables();
	calcMod2WavTotalRows();
	restartSong();

	editor.blockMarkFlag = false;

	pointerSetMode(POINTER_MODE_MSG2, NO_CARRY);
	setStatusMessage("RENDERING MOD...", NO_CARRY);

	editor.ui.disableVisualizer = true;
	editor.isWAVRendering = true;
	renderMOD2WAVDialog();

	editor.abortMod2Wav = false;

	editor.mod2WavThread = SDL_CreateThread(mod2WavThreadFunc, NULL, fOut);
	if (editor.mod2WavThread != NULL)
	{
		SDL_DetachThread(editor.mod2WavThread);
	}
	else
	{
		editor.ui.disableVisualizer = false;
		editor.isWAVRendering = false;

		displayErrorMsg("THREAD ERROR");

		pointerSetMode(POINTER_MODE_IDLE, DO_CARRY);
		statusAllRight();

		return false;
	}

	return true;
}

// for MOD2WAV - ONLY used for a visual percentage counter, so accuracy is not important
void calcMod2WavTotalRows(void)
{
	bool pBreakFlag, posJumpAssert, calcingRows;
	int8_t n_pattpos[AMIGA_VOICES], n_loopcount[AMIGA_VOICES];
	uint8_t modRow, pBreakPosition, ch, pos;
	int16_t modOrder;
	uint16_t modPattern;
	note_t *note;

	// for pattern loop
	memset(n_pattpos, 0, sizeof (n_pattpos));
	memset(n_loopcount, 0, sizeof (n_loopcount));

	modEntry->rowsCounter = 0;
	modEntry->rowsInTotal = 0;

	modRow = 0;
	modOrder = 0;
	modPattern = modEntry->head.order[0];
	pBreakPosition = 0;
	posJumpAssert = false;
	pBreakFlag = false;
	calcingRows = true;

	memset(editor.rowVisitTable, 0, MOD_ORDERS * MOD_ROWS);
	while (calcingRows)
	{
		editor.rowVisitTable[(modOrder * MOD_ROWS) + modRow] = true;

		for (ch = 0; ch < AMIGA_VOICES; ch++)
		{
			note = &modEntry->patterns[modPattern][(modRow * AMIGA_VOICES) + ch];
			if (note->command == 0x0B) // Bxx - Position Jump
			{
				modOrder = note->param - 1;
				pBreakPosition = 0;
				posJumpAssert = true;
			}
			else if (note->command == 0x0D) // Dxx - Pattern Break
			{
				pBreakPosition = (((note->param >> 4) * 10) + (note->param & 0x0F));
				if (pBreakPosition > 63)
					pBreakPosition = 0;

				posJumpAssert = true;
			}
			else if (note->command == 0x0F && note->param == 0) // F00 - Set Speed 0 (stop)
			{
				calcingRows = false;
				break;
			}
			else if (note->command == 0x0E && (note->param >> 4) == 0x06) // E6x - Pattern Loop
			{
				pos = note->param & 0x0F;
				if (pos == 0)
				{
					n_pattpos[ch] = modRow;
				}
				else
				{
					// this is so ugly
					if (n_loopcount[ch] == 0)
					{
						n_loopcount[ch] = pos;

						pBreakPosition = n_pattpos[ch];
						pBreakFlag = true;

						for (pos = pBreakPosition; pos <= modRow; pos++)
							editor.rowVisitTable[(modOrder * MOD_ROWS) + pos] = false;
					}
					else
					{
						if (--n_loopcount[ch])
						{
							pBreakPosition = n_pattpos[ch];
							pBreakFlag = true;

							for (pos = pBreakPosition; pos <= modRow; pos++)
								editor.rowVisitTable[(modOrder * MOD_ROWS) + pos] = false;
						}
					}
				}
			}
		}

		modRow++;
		modEntry->rowsInTotal++;

		if (pBreakFlag)
		{
			modRow = pBreakPosition;
			pBreakPosition = 0;
			pBreakFlag = false;
		}

		if (modRow >= MOD_ROWS || posJumpAssert)
		{
			modRow = pBreakPosition;
			pBreakPosition = 0;
			posJumpAssert = false;

			modOrder = (modOrder + 1) & 0x7F;
			if (modOrder >= modEntry->head.orderCount)
			{
				modOrder = 0;
				calcingRows = false;
				break;
			}

			modPattern = modEntry->head.order[modOrder];
			if (modPattern > MAX_PATTERNS-1)
				modPattern = MAX_PATTERNS-1;
		}

		if (editor.rowVisitTable[(modOrder * MOD_ROWS) + modRow])
		{
			// row has been visited before, we're now done!
			calcingRows = false;
			break;
		}
	}
}

void normalize32bitSigned(int32_t *sampleData, uint32_t sampleLength)
{
	int32_t sample, sampleVolPeak;
	uint32_t i;
	double dGain;

	sampleVolPeak = 0;
	for (i = 0; i < sampleLength; i++)
	{
		sample = ABS(sampleData[i]);
		if (sampleVolPeak < sample)
			sampleVolPeak = sample;
	}

	if (sampleVolPeak >= INT32_MAX)
		return; // sample is already normalized

	// prevent division by zero!
	if (sampleVolPeak <= 0)
		sampleVolPeak = 1;

	dGain = (double)INT32_MAX / sampleVolPeak;
	for (i = 0; i < sampleLength; i++)
	{
		sample = (int32_t)(sampleData[i] * dGain);
		sampleData[i] = (int32_t)sample;
	}
}

void normalize16bitSigned(int16_t *sampleData, uint32_t sampleLength)
{
	uint32_t i;
	int32_t sample, sampleVolPeak, gain;

	sampleVolPeak = 0;
	for (i = 0; i < sampleLength; i++)
	{
		sample = ABS(sampleData[i]);
		if (sampleVolPeak < sample)
			sampleVolPeak = sample;
	}

	if (sampleVolPeak >= INT16_MAX)
		return; // sample is already normalized

	if (sampleVolPeak < 1)
		return;

	gain = (INT16_MAX * 65536) / sampleVolPeak;
	for (i = 0; i < sampleLength; i++)
		sampleData[i] = (int16_t)((sampleData[i] * gain) >> 16);
}

void normalize8bitFloatSigned(float *fSampleData, uint32_t sampleLength)
{
	uint32_t i;
	float fSample, fSampleVolPeak, fGain;

	fSampleVolPeak = 0.0f;
	for (i = 0; i < sampleLength; i++)
	{
		fSample = fabsf(fSampleData[i]);
		if (fSampleVolPeak < fSample)
			fSampleVolPeak = fSample;
	}

	if (fSampleVolPeak <= 0.0f)
		return;

	fGain = INT8_MAX / fSampleVolPeak;
	for (i = 0; i < sampleLength; i++)
		fSampleData[i] *= fGain;
}

void normalize8bitDoubleSigned(double *dSampleData, uint32_t sampleLength)
{
	uint32_t i;
	double dSample, dSampleVolPeak, dGain;

	dSampleVolPeak = 0.0;
	for (i = 0; i < sampleLength; i++)
	{
		dSample = fabs(dSampleData[i]);
		if (dSampleVolPeak < dSample)
			dSampleVolPeak = dSample;
	}

	if (dSampleVolPeak <= 0.0)
		return;

	dGain = INT8_MAX / dSampleVolPeak;
	for (i = 0; i < sampleLength; i++)
		dSampleData[i] *= dGain;
}
