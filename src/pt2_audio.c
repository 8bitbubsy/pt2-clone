// the audio filters and BLEP synthesis were coded by aciddose

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
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <limits.h>
#include "pt2_math.h"
#include "pt2_audio.h"
#include "pt2_header.h"
#include "pt2_helpers.h"
#include "pt2_blep.h"
#include "pt2_config.h"
#include "pt2_tables.h"
#include "pt2_textout.h"
#include "pt2_visuals.h"
#include "pt2_scopes.h"
#include "pt2_mod2wav.h"
#include "pt2_pat2smp.h"
#include "pt2_sync.h"
#include "pt2_structs.h"
#include "pt2_rcfilter.h"
#include "pt2_ledfilter.h"
#include "pt2_downsamplers2x.h"

#define STEREO_NORM_FACTOR 0.5 /* cumulative mid/side normalization factor (1/sqrt(2))*(1/sqrt(2)) */

#define INITIAL_DITHER_SEED 0x12345000

static volatile bool ledFilterEnabled;
static volatile uint8_t filterModel;
static bool amigaPanFlag;
static int32_t randSeed = INITIAL_DITHER_SEED, stereoSeparation = 100;
static uint32_t audLatencyPerfValInt, audLatencyPerfValFrac;
static uint64_t tickTime64, tickTime64Frac;
static double *dMixBufferL, *dMixBufferR, *dMixBufferLUnaligned, *dMixBufferRUnaligned;
static double dPrngStateL, dPrngStateR, dSideFactor;
static blep_t blep[AMIGA_VOICES];
static rcFilter_t filterLoA500, filterHiA500, filterLoA1200, filterHiA1200;
static ledFilter_t filterLED;
static SDL_AudioDeviceID dev;

static void processFiltersA1200_NoLED(int32_t numSamples);
static void processFiltersA1200_LED(int32_t numSamples);
static void processFiltersA500_NoLED(int32_t numSamples);
static void processFiltersA500_LED(int32_t numSamples);
static void (*processFiltersFunc)(int32_t);

// for audio/video syncing
static uint32_t tickTimeLen, tickTimeLenFrac;

// globalized
audio_t audio;
paulaVoice_t paula[AMIGA_VOICES];

bool intMusic(void); // defined in pt2_replayer.c

static void updateFilterFunc(void)
{
	if (filterModel == FILTERMODEL_A500)
	{
		if (ledFilterEnabled)
			processFiltersFunc = processFiltersA500_LED;
		else
			processFiltersFunc = processFiltersA500_NoLED;
	}
	else // A1200
	{
		if (ledFilterEnabled)
			processFiltersFunc = processFiltersA1200_LED;
		else
			processFiltersFunc = processFiltersA1200_NoLED;
	}
}

void setLEDFilter(bool state, bool doLockAudio)
{
	if (ledFilterEnabled == state)
		return; // same state as before!

	const bool audioWasntLocked = !audio.locked;
	if (doLockAudio && audioWasntLocked)
		lockAudio();

	clearLEDFilterState(&filterLED);

	editor.useLEDFilter = state;
	ledFilterEnabled = editor.useLEDFilter;
	updateFilterFunc();

	if (doLockAudio && audioWasntLocked)
		unlockAudio();
}

void toggleLEDFilter(void)
{
	const bool audioWasntLocked = !audio.locked;
	if (audioWasntLocked)
		lockAudio();

	clearLEDFilterState(&filterLED);

	editor.useLEDFilter ^= 1;
	ledFilterEnabled = editor.useLEDFilter;
	updateFilterFunc();

	if (audioWasntLocked)
		unlockAudio();
}

static void calcAudioLatencyVars(int32_t audioBufferSize, int32_t audioFreq)
{
	double dInt, dFrac;

	if (audioFreq == 0)
		return;

	const double dAudioLatencySecs = audioBufferSize / (double)audioFreq;

	dFrac = modf(dAudioLatencySecs * editor.dPerfFreq, &dInt);

	// integer part
	audLatencyPerfValInt = (int32_t)dInt;

	// fractional part (scaled to 0..2^32-1)
	dFrac *= UINT32_MAX+1.0;
	audLatencyPerfValFrac = (uint32_t)dFrac;
}

void setSyncTickTimeLen(uint32_t timeLen, uint32_t timeLenFrac)
{
	tickTimeLen = timeLen;
	tickTimeLenFrac = timeLenFrac;
}

void lockAudio(void)
{
	if (dev != 0)
		SDL_LockAudioDevice(dev);

	audio.locked = true;

	audio.resetSyncTickTimeFlag = true;
	resetChSyncQueue();
}

void unlockAudio(void)
{
	if (dev != 0)
		SDL_UnlockAudioDevice(dev);

	audio.resetSyncTickTimeFlag = true;
	resetChSyncQueue();

	audio.locked = false;
}

void mixerUpdateLoops(void) // updates Paula loop (+ scopes)
{
	for (int32_t i = 0; i < AMIGA_VOICES; i++)
	{
		const moduleChannel_t *ch = &song->channels[i];
		if (ch->n_samplenum == editor.currSample)
		{
			const moduleSample_t *s = &song->samples[editor.currSample];

			paulaSetData(i, ch->n_start + s->loopStart);
			paulaSetLength(i, s->loopLength >> 1);
		}
	}
}

void mixerKillVoice(int32_t ch)
{
	const bool audioWasntLocked = !audio.locked;
	if (audioWasntLocked)
		lockAudio();

	memset(&paula[ch], 0, sizeof (paulaVoice_t));
	memset(&blep[ch], 0, sizeof (blep_t));

	stopScope(ch); // it should be safe to clear the scope now
	memset(&scope[ch], 0, sizeof (scope_t));

	if (audioWasntLocked)
		unlockAudio();
}

void turnOffVoices(void)
{
	const bool audioWasntLocked = !audio.locked;
	if (audioWasntLocked)
		lockAudio();

	for (int32_t i = 0; i < AMIGA_VOICES; i++)
		mixerKillVoice(i);

	clearRCFilterState(&filterLoA500);
	clearRCFilterState(&filterLoA1200);
	clearRCFilterState(&filterHiA500);
	clearRCFilterState(&filterHiA1200);
	clearLEDFilterState(&filterLED);

	resetAudioDithering();

	editor.tuningFlag = false;

	if (audioWasntLocked)
		unlockAudio();
}

void resetCachedMixerPeriod(void)
{
	paulaVoice_t *v = paula;
	for (int32_t i = 0; i < AMIGA_VOICES; i++, v++)
	{
		v->oldPeriod = -1;
		v->dOldVoiceDelta = 0.0;
		v->dOldVoiceDeltaMul = 1.0;
	}
}

// the following routines are only called from the mixer thread.

void paulaSetPeriod(int32_t ch, uint16_t period)
{
	double dPeriodToDeltaDiv;
	paulaVoice_t *v = &paula[ch];

	int32_t realPeriod = period;
	if (realPeriod == 0)
		realPeriod = 1+65535; // confirmed behavior on real Amiga
	else if (realPeriod < 113)
		realPeriod = 113; // close to what happens on real Amiga (and needed for BLEP synthesis)

	if (editor.songPlaying)
	{
		v->syncPeriod = realPeriod;
		v->syncFlags |= SET_SCOPE_PERIOD;
	}
	else
	{
		scopeSetPeriod(ch, realPeriod);
	}

	// if the new period was the same as the previous period, use cached delta
	if (realPeriod != v->oldPeriod)
	{
		v->oldPeriod = realPeriod;

		// this period is not cached, calculate mixer deltas

		// during PAT2SMP, use different audio output rates
		if (editor.isSMPRendering)
			dPeriodToDeltaDiv = editor.pat2SmpHQ ? (PAULA_PAL_CLK / PAT2SMP_HI_FREQ) : (PAULA_PAL_CLK / PAT2SMP_LO_FREQ);
		else
			dPeriodToDeltaDiv = audio.dPeriodToDeltaDiv;

		v->dOldVoiceDelta = dPeriodToDeltaDiv / realPeriod;

		if (audio.oversamplingFlag || editor.isSMPRendering)
			v->dOldVoiceDelta *= 0.5; // /2 since we do 2x oversampling

		// for BLEP synthesis (prevents division in inner mix loop)
		v->dOldVoiceDeltaMul = 1.0 / v->dOldVoiceDelta;
	}

	v->dNewDelta = v->dOldVoiceDelta;
	if (v->dLastDelta == 0.0) // for BLEP
		v->dLastDelta = v->dNewDelta;

	v->dNewDeltaMul = v->dOldVoiceDeltaMul;
	if (v->dLastDeltaMul == 0.0) // for BLEP
		v->dLastDeltaMul = v->dNewDeltaMul;
}

void paulaSetVolume(int32_t ch, uint16_t vol)
{
	paulaVoice_t *v = &paula[ch];

	int32_t realVol = vol;

	// this is what WinUAE does
	realVol &= 127;
	if (realVol > 64)
		realVol = 64;
	// ------------------------

	// multiplying by this also scales the sample from -128..127 -> -1.0 .. ~0.99
	v->dScaledVolume = realVol * (1.0 / (128.0 * 64.0));

	if (editor.songPlaying)
	{
		v->syncVolume = (uint8_t)realVol;
		v->syncFlags |= SET_SCOPE_VOLUME;
	}
	else
	{
		scope[ch].volume = (uint8_t)realVol;
	}
}

void paulaSetLength(int32_t ch, uint16_t len)
{
	paulaVoice_t *v = &paula[ch];

	int32_t realLength = len;
	if (realLength == 0)
	{
		realLength = 1+65535;
		/* Confirmed behavior on real Amiga. We have room for this
		** even at the last sample slot, so it will never overflow!
		**
		** PS: I don't really know if it's possible for ProTracker to
		** set a Paula length of 0, but I fully support this Paula
		** behavior just in case.
		*/
	}

	realLength <<= 1; // we work with bytes, not words

	v->newLength = realLength;
	if (editor.songPlaying)
		v->syncFlags |= SET_SCOPE_LENGTH;
	else
		scope[ch].newLength = realLength;
}

void paulaSetData(int32_t ch, const int8_t *src)
{
	paulaVoice_t *v = &paula[ch];

	if (src == NULL)
		src = &song->sampleData[RESERVED_SAMPLE_OFFSET]; // 128K reserved sample

	v->newData = src;
	if (editor.songPlaying)
		v->syncFlags |= SET_SCOPE_DATA;
	else
		scope[ch].newData = src;
}

void paulaStopDMA(int32_t ch)
{
	paulaVoice_t *v = &paula[ch];

	v->DMA_active = false;

	if (editor.songPlaying)
		v->syncFlags |= STOP_SCOPE;
	else
		scope[ch].active = false;
}

void paulaStartDMA(int32_t ch)
{
	paulaVoice_t *v = &paula[ch];

	const int8_t *dat = v->newData;
	if (dat == NULL)
		dat = &song->sampleData[RESERVED_SAMPLE_OFFSET]; // 128K reserved sample

	int32_t length = v->newLength; // in bytes, not words
	if (length == 0)
		length = 1+65535;

	v->dPhase = v->dLastPhase = 0.0;
	v->pos = 0;
	v->data = dat;
	v->length = length;
	v->dDelta = v->dLastDelta = v->dNewDelta;
	v->dDeltaMul = v->dLastDeltaMul = v->dNewDeltaMul;

	/* Read first sample data point into cache now.
	**
	** (multiplying by dScaledVolume will also change the scale
	**  from -128..127 to -1.0 .. ~0.99.)
	*/
	v->dCachedSamplePoint = v->data[0] * v->dScaledVolume;

	v->DMA_active = true;

	if (editor.songPlaying)
	{
		v->syncTriggerData = dat;
		v->syncTriggerLength = length;
		v->syncFlags |= TRIGGER_SCOPE;
	}
	else
	{
		scope_t *s = &scope[ch];
		s->newData = dat;
		s->newLength = length;
		scopeTrigger(ch);
	}
}

void toggleFilterModel(void)
{
	const bool audioWasntLocked = !audio.locked;
	if (audioWasntLocked)
		lockAudio();

	clearRCFilterState(&filterLoA500);
	clearRCFilterState(&filterLoA1200);
	clearRCFilterState(&filterHiA500);
	clearRCFilterState(&filterHiA1200);
	clearLEDFilterState(&filterLED);

	filterModel ^= 1;
	updateFilterFunc();

	if (filterModel == FILTERMODEL_A500)
		displayMsg("AUDIO: AMIGA 500");
	else
		displayMsg("AUDIO: AMIGA 1200");

	if (audioWasntLocked)
		unlockAudio();
}

void mixChannels(int32_t numSamples)
{
	double *dMixBufSelect[AMIGA_VOICES] = { dMixBufferL, dMixBufferR, dMixBufferR, dMixBufferL };

	memset(dMixBufferL, 0, numSamples * sizeof (double));
	memset(dMixBufferR, 0, numSamples * sizeof (double));

	paulaVoice_t *v = paula;
	blep_t *bSmp = blep;

	for (int32_t i = 0; i < AMIGA_VOICES; i++, v++, bSmp++)
	{
		/* We only need to test for a NULL-pointer once.
		** When pointers are messed with in the tracker, the mixer
		** is temporarily forced offline, and its voice pointers are
		** cleared to prevent expired pointer addresses.
		*/
		if (!v->DMA_active || v->data == NULL)
			continue;

		double *dMixBuf = dMixBufSelect[i]; // what output channel to mix into (L, R, R, L)
		for (int32_t j = 0; j < numSamples; j++)
		{
			double dSmp = v->dCachedSamplePoint;
			if (dSmp != bSmp->dLastValue)
			{
				if (v->dLastDelta > v->dLastPhase)
				{
					// v->dLastDeltaMul is (1.0 / v->dLastDelta) (pre-computed for speed, div -> mul)
					blepAdd(bSmp, v->dLastPhase * v->dLastDeltaMul, bSmp->dLastValue - dSmp);
				}

				bSmp->dLastValue = dSmp;
			}

			if (bSmp->samplesLeft > 0)
				dSmp = blepRun(bSmp, dSmp);

			dMixBuf[j] += dSmp;

			v->dPhase += v->dDelta;
			if (v->dPhase >= 1.0) // deltas can't be >= 1.0, so this is safe
			{
				v->dPhase -= 1.0;

				// Paula only updates period (delta) during sample fetching
				v->dDelta = v->dNewDelta;
				v->dDeltaMul = v->dNewDeltaMul;
				// --------------------------------------------------------

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

				/* Read sample into cache now.
				** Also change volume here as well. It has recently been
				** discovered that Paula only updates its volume during period
				** fetching (when it's reading the next sample point).
				**
				** (multiplying by dScaledVolume will also change the scale
				**  from -128..127 to -1.0 .. ~0.99.)
				*/
				v->dCachedSamplePoint = v->data[v->pos] * v->dScaledVolume;
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

static void processFiltersA1200_NoLED(int32_t numSamples)
{
	// apply filters
	for (int32_t i = 0; i < numSamples; i++)
	{
		double dOut[2];

		dOut[0] = dMixBufferL[i];
		dOut[1] = dMixBufferR[i];

		// low-pass filter
		RCLowPassFilterStereo(&filterLoA1200, dOut, dOut);

		// high-pass RC filter
		RCHighPassFilterStereo(&filterHiA1200, dOut, dOut);

		dMixBufferL[i] = dOut[0];
		dMixBufferR[i] = dOut[1];
	}
}

static void processFiltersA1200_LED(int32_t numSamples)
{
	// apply filters
	for (int32_t i = 0; i < numSamples; i++)
	{
		double dOut[2];

		dOut[0] = dMixBufferL[i];
		dOut[1] = dMixBufferR[i];

		// low-pass filter
		RCLowPassFilterStereo(&filterLoA1200, dOut, dOut);

		// "LED" Sallen-Key filter
		LEDFilter(&filterLED, dOut, dOut);

		// high-pass RC filter
		RCHighPassFilterStereo(&filterHiA1200, dOut, dOut);

		dMixBufferL[i] = dOut[0];
		dMixBufferR[i] = dOut[1];
	}
}

static void processFiltersA500_NoLED(int32_t numSamples)
{
	for (int32_t i = 0; i < numSamples; i++)
	{
		double dOut[2];

		dOut[0] = dMixBufferL[i];
		dOut[1] = dMixBufferR[i];

		// low-pass RC filter
		RCLowPassFilterStereo(&filterLoA500, dOut, dOut);

		// high-pass RC filter
		RCHighPassFilterStereo(&filterHiA500, dOut, dOut);

		dMixBufferL[i] = dOut[0];
		dMixBufferR[i] = dOut[1];
	}
}

static void processFiltersA500_LED(int32_t numSamples)
{
	for (int32_t i = 0; i < numSamples; i++)
	{
		double dOut[2];

		dOut[0] = dMixBufferL[i];
		dOut[1] = dMixBufferR[i];

		// low-pass RC filter
		RCLowPassFilterStereo(&filterLoA500, dOut, dOut);

		// "LED" Sallen-Key filter
		LEDFilter(&filterLED, dOut, dOut);

		// high-pass RC filter
		RCHighPassFilterStereo(&filterHiA500, dOut, dOut);

		dMixBufferL[i] = dOut[0];
		dMixBufferR[i] = dOut[1];
	}
}

#define NORM_FACTOR 2.0 /* nominally correct, but can clip from high-pass filter overshoot */

static inline void processMixedSamplesAmigaPanning(int32_t i, int16_t *out)
{
	int32_t smp32;
	double dPrng;

	double dL = dMixBufferL[i];
	double dR = dMixBufferR[i];

	// normalize w/ phase-inversion (A500/A1200 has a phase-inverted audio signal)
	dL *= NORM_FACTOR * (-INT16_MAX / (double)AMIGA_VOICES);
	dR *= NORM_FACTOR * (-INT16_MAX / (double)AMIGA_VOICES);

	// left channel - 1-bit triangular dithering (high-pass filtered)
	dPrng = random32() * (0.5 / INT32_MAX); // -0.5..0.5
	dL = (dL + dPrng) - dPrngStateL;
	dPrngStateL = dPrng;
	smp32 = (int32_t)dL;
	CLAMP16(smp32);
	out[0] = (int16_t)smp32;

	// right channel - 1-bit triangular dithering (high-pass filtered)
	dPrng = random32() * (0.5 / INT32_MAX); // -0.5..0.5
	dR = (dR + dPrng) - dPrngStateR;
	dPrngStateR = dPrng;
	smp32 = (int32_t)dR;
	CLAMP16(smp32);
	out[1] = (int16_t)smp32;
}

static inline void processMixedSamples(int32_t i, int16_t *out)
{
	int32_t smp32;
	double dPrng;

	double dL = dMixBufferL[i];
	double dR = dMixBufferR[i];

	// apply stereo separation
	const double dOldL = dL;
	const double dOldR = dR;
	double dMid  = (dOldL + dOldR) * STEREO_NORM_FACTOR;
	double dSide = (dOldL - dOldR) * dSideFactor;
	dL = dMid + dSide;
	dR = dMid - dSide;

	// normalize w/ phase-inversion (A500/A1200 has a phase-inverted audio signal)
	dL *= NORM_FACTOR * (-INT16_MAX / (double)AMIGA_VOICES);
	dR *= NORM_FACTOR * (-INT16_MAX / (double)AMIGA_VOICES);

	// left channel - 1-bit triangular dithering (high-pass filtered)
	dPrng = random32() * (0.5 / INT32_MAX); // -0.5..0.5
	dL = (dL + dPrng) - dPrngStateL;
	dPrngStateL = dPrng;
	smp32 = (int32_t)dL;
	CLAMP16(smp32);
	out[0] = (int16_t)smp32;

	// right channel - 1-bit triangular dithering (high-pass filtered)
	dPrng = random32() * (0.5 / INT32_MAX); // -0.5..0.5
	dR = (dR + dPrng) - dPrngStateR;
	dPrngStateR = dPrng;
	smp32 = (int32_t)dR;
	CLAMP16(smp32);
	out[1] = (int16_t)smp32;
}


static inline void processMixedSamplesAmigaPanning_2x(int32_t i, int16_t *out) // 2x oversampling
{
	int32_t smp32;
	double dPrng, dL, dR;

	// 2x downsampling (decimation)
	const uint32_t offset1 = (i << 1) + 0;
	const uint32_t offset2 = (i << 1) + 1;
	dL = decimate2x_L(dMixBufferL[offset1], dMixBufferL[offset2]);
	dR = decimate2x_R(dMixBufferR[offset1], dMixBufferR[offset2]);

	// normalize w/ phase-inversion (A500/A1200 has a phase-inverted audio signal)
	dL *= NORM_FACTOR * (-INT16_MAX / (double)AMIGA_VOICES);
	dR *= NORM_FACTOR * (-INT16_MAX / (double)AMIGA_VOICES);

	// left channel - 1-bit triangular dithering (high-pass filtered)
	dPrng = random32() * (0.5 / INT32_MAX); // -0.5..0.5
	dL = (dL + dPrng) - dPrngStateL;
	dPrngStateL = dPrng;
	smp32 = (int32_t)dL;
	CLAMP16(smp32);
	out[0] = (int16_t)smp32;

	// right channel - 1-bit triangular dithering (high-pass filtered)
	dPrng = random32() * (0.5 / INT32_MAX); // -0.5..0.5
	dR = (dR + dPrng) - dPrngStateR;
	dPrngStateR = dPrng;
	smp32 = (int32_t)dR;
	CLAMP16(smp32);
	out[1] = (int16_t)smp32;
}

static inline void processMixedSamples_2x(int32_t i, int16_t *out) // 2x oversampling
{
	int32_t smp32;
	double dPrng, dL, dR;

	// 2x downsampling (decimation)
	const uint32_t offset1 = (i << 1) + 0;
	const uint32_t offset2 = (i << 1) + 1;
	dL = decimate2x_L(dMixBufferL[offset1], dMixBufferL[offset2]);
	dR = decimate2x_R(dMixBufferR[offset1], dMixBufferR[offset2]);

	// apply stereo separation
	const double dOldL = dL;
	const double dOldR = dR;
	double dMid  = (dOldL + dOldR) * STEREO_NORM_FACTOR;
	double dSide = (dOldL - dOldR) * dSideFactor;
	dL = dMid + dSide;
	dR = dMid - dSide;

	// normalize w/ phase-inversion (A500/A1200 has a phase-inverted audio signal)
	dL *= NORM_FACTOR * (-INT16_MAX / (double)AMIGA_VOICES);
	dR *= NORM_FACTOR * (-INT16_MAX / (double)AMIGA_VOICES);

	// left channel - 1-bit triangular dithering (high-pass filtered)
	dPrng = random32() * (0.5 / INT32_MAX); // -0.5..0.5
	dL = (dL + dPrng) - dPrngStateL;
	dPrngStateL = dPrng;
	smp32 = (int32_t)dL;
	CLAMP16(smp32);
	out[0] = (int16_t)smp32;

	// right channel - 1-bit triangular dithering (high-pass filtered)
	dPrng = random32() * (0.5 / INT32_MAX); // -0.5..0.5
	dR = (dR + dPrng) - dPrngStateR;
	dPrngStateR = dPrng;
	smp32 = (int32_t)dR;
	CLAMP16(smp32);
	out[1] = (int16_t)smp32;
}

void outputAudio(int16_t *target, int32_t numSamples)
{
	if (editor.isSMPRendering)
	{
		// render to sample (PAT2SMP)

		int32_t samplesTodo = numSamples;
		if (editor.pat2SmpPos+samplesTodo > MAX_SAMPLE_LEN)
			samplesTodo = MAX_SAMPLE_LEN-editor.pat2SmpPos;

		// mix channels (with 2x oversampling, PAT2SMP needs it)
		mixChannels(samplesTodo*2);

		double *dOutStream = &editor.dPat2SmpBuf[editor.pat2SmpPos];
		for (int32_t i = 0; i < samplesTodo; i++)
		{
			// 2x downsampling (decimation)
			double dL, dR;
			const uint32_t offset1 = (i << 1) + 0;
			const uint32_t offset2 = (i << 1) + 1;
			dL = decimate2x_L(dMixBufferL[offset1], dMixBufferL[offset2]);
			dR = decimate2x_R(dMixBufferR[offset1], dMixBufferR[offset2]);

			dOutStream[i] = (dL + dR) * 0.5; // normalized to -128..127 later
		}

		editor.pat2SmpPos += samplesTodo;
		if (editor.pat2SmpPos >= MAX_SAMPLE_LEN)
		{
			editor.smpRenderingDone = true;
			updateWindowTitle(MOD_IS_MODIFIED);
		}
	}
	else
	{
		if (audio.oversamplingFlag) // 2x oversampling
		{
			// mix and filter channels (at 2x rate)
			mixChannels(numSamples*2);
			processFiltersFunc(numSamples*2);

			// downsample, normalize and dither
			int16_t out[2];
			int16_t *outStream = target;
			if (stereoSeparation == 100)
			{
				for (int32_t i = 0; i < numSamples; i++)
				{
					processMixedSamplesAmigaPanning_2x(i, out);
					*outStream++ = out[0];
					*outStream++ = out[1];
				}
			}
			else
			{
				for (int32_t i = 0; i < numSamples; i++)
				{
					processMixedSamples_2x(i, out);
					*outStream++ = out[0];
					*outStream++ = out[1];
				}
			}
		}
		else
		{
			// mix and filter channels
			mixChannels(numSamples);
			processFiltersFunc(numSamples);

			// normalize and dither
			int16_t out[2];
			int16_t *outStream = target;
			if (stereoSeparation == 100)
			{
				for (int32_t i = 0; i < numSamples; i++)
				{
					processMixedSamplesAmigaPanning(i, out);
					*outStream++ = out[0];
					*outStream++ = out[1];
				}
			}
			else
			{
				for (int32_t i = 0; i < numSamples; i++)
				{
					processMixedSamples(i, out);
					*outStream++ = out[0];
					*outStream++ = out[1];
				}
			}
		}

	}
}

static void fillVisualsSyncBuffer(void)
{
	chSyncData_t chSyncData;

	if (audio.resetSyncTickTimeFlag)
	{
		audio.resetSyncTickTimeFlag = false;

		tickTime64 = SDL_GetPerformanceCounter() + audLatencyPerfValInt;
		tickTime64Frac = audLatencyPerfValFrac;
	}

	moduleChannel_t *c = song->channels;
	paulaVoice_t *v = paula;
	syncedChannel_t *s = chSyncData.channels;

	for (int32_t i = 0; i < AMIGA_VOICES; i++, c++, s++, v++)
	{
		s->flags = v->syncFlags | c->syncFlags;
		c->syncFlags = v->syncFlags = 0; // clear sync flags

		s->volume = v->syncVolume;
		s->period = v->syncPeriod;
		s->triggerData = v->syncTriggerData;
		s->triggerLength = v->syncTriggerLength;
		s->newData = v->newData;
		s->newLength = v->newLength;
		s->vuVolume = c->syncVuVolume;
		s->analyzerVolume = c->syncAnalyzerVolume;
		s->analyzerPeriod = c->syncAnalyzerPeriod;
	}

	chSyncData.timestamp = tickTime64;
	chQueuePush(chSyncData);

	tickTime64 += tickTimeLen;
	tickTime64Frac += tickTimeLenFrac;
	if (tickTime64Frac > 0xFFFFFFFF)
	{
		tickTime64Frac &= 0xFFFFFFFF;
		tickTime64++;
	}
}

static void SDLCALL audioCallback(void *userdata, Uint8 *stream, int len)
{
	if (audio.forceSoundCardSilence) // during MOD2WAV
	{
		memset(stream, 0, len);
		return;
	}

	int16_t *streamOut = (int16_t *)stream;

	int32_t samplesLeft = len >> 2;
	while (samplesLeft > 0)
	{
		if (audio.tickSampleCounter64 <= 0)
		{
			// new replayer tick

			if (editor.songPlaying)
			{
				intMusic();
				fillVisualsSyncBuffer();
			}

			audio.tickSampleCounter64 += audio.samplesPerTick64;
		}

		const int32_t remainingTick = (audio.tickSampleCounter64 + UINT32_MAX) >> 32; // ceil rounding (upwards)

		int32_t samplesToMix = samplesLeft;
		if (samplesToMix > remainingTick)
			samplesToMix = remainingTick;

		outputAudio(streamOut, samplesToMix);
		streamOut += samplesToMix<<1;

		samplesLeft -= samplesToMix;
		audio.tickSampleCounter64 -= (int64_t)samplesToMix << 32;
	}

	(void)userdata;
}

static void calculateFilterCoeffs(void)
{
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
	** In the Amiga 1200, the low-pass cutoff is ~34kHz, so the
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
	** - 1-pole RC 6dB/oct high-pass: R=1390 ohm (1000+390), C=22uF
	*/
	double dAudioFreq = audio.outputRate;
	double R, C, R1, R2, C1, C2, fc, fb;

	if (audio.oversamplingFlag)
		dAudioFreq *= 2.0; // 2x oversampling

	// A500 1-pole (6db/oct) static RC low-pass filter:
	R = 360.0; // R321 (360 ohm)
	C = 1e-7;  // C321 (0.1uF)
	fc = (1.0 / (PT2_TWO_PI * R * C)); // cutoff = ~4420.97Hz
	calcRCFilterCoeffs(dAudioFreq, fc, &filterLoA500);

	// A1200 1-pole (6db/oct) static RC low-pass filter:
	R = 680.0;  // R321 (680 ohm)
	C = 6.8e-9; // C321 (6800pF)
	fc = (1.0 / (PT2_TWO_PI * R * C)); // cutoff = ~34419.32Hz
	calcRCFilterCoeffs(dAudioFreq, fc, &filterLoA1200);

	// Sallen-Key filter ("LED" filter, same values on A500/A1200):
	R1 = 10000.0; // R322 (10K ohm)
	R2 = 10000.0; // R323 (10K ohm)
	C1 = 6.8e-9;  // C322 (6800pF)
	C2 = 3.9e-9;  // C323 (3900pF)
	fc = 1.0 / (PT2_TWO_PI * pt2_sqrt(R1 * R2 * C1 * C2)); // cutoff = ~3090.53Hz
	fb = 0.125/2.0; // Fb = 0.125 : Q ~= 1/sqrt(2) (Butterworth) (8bb: was 0.125, but /2 gives a closer gain!)
	calcLEDFilterCoeffs(dAudioFreq, fc, fb, &filterLED);

	// A500 1-pole (6dB/oct) static RC high-pass filter:
	R = 1390.0;   // R324 (1K ohm) + R325 (390 ohm)
	C = 2.233e-5; // C334 (22uF) + C335 (0.33uF)
	fc = 1.0 / (PT2_TWO_PI * R * C); // cutoff = ~5.13Hz
	calcRCFilterCoeffs(dAudioFreq, fc, &filterHiA500);

	// A1200 1-pole (6dB/oct) static RC high-pass filter:
	R = 1390.0; // R324 (1K ohm resistor) + R325 (390 ohm resistor)
	C = 2.2e-5; // C334 (22uF capacitor)
	fc = 1.0 / (PT2_TWO_PI * R * C); // cutoff = ~5.20Hz
	calcRCFilterCoeffs(dAudioFreq, fc, &filterHiA1200);
}

void mixerSetStereoSeparation(uint8_t percentage) // 0..100 (percentage)
{
	assert(percentage <= 100);

	stereoSeparation = percentage;
	dSideFactor = (percentage / 100.0) * STEREO_NORM_FACTOR;
}

static double ciaBpm2Hz(int32_t bpm)
{
	if (bpm == 0)
		return 0.0;

	const uint32_t ciaPeriod = 1773447 / bpm; // yes, PT truncates here
	return (double)CIA_PAL_CLK / (ciaPeriod+1); // +1, CIA triggers on underflow
}

static void generateBpmTables(bool vblankTimingFlag)
{
	for (int32_t bpm = 32; bpm <= 255; bpm++)
	{
		double dBpmHz;
		
		if (vblankTimingFlag)
			dBpmHz = AMIGA_PAL_VBLANK_HZ;
		else
			dBpmHz = ciaBpm2Hz(bpm);

		const double dSamplesPerTick      = audio.outputRate / dBpmHz;
		const double dSamplesPerTick28kHz = PAT2SMP_HI_FREQ  / dBpmHz; // PAT2SMP hi quality
		const double dSamplesPerTick20kHz = PAT2SMP_LO_FREQ  / dBpmHz; // PAT2SMP low quality

		// convert to rounded 32.32 fixed-point
		const int32_t i = bpm - 32;
		audio.bpmTable[i]      = (int64_t)((dSamplesPerTick      * (UINT32_MAX+1.0)) + 0.5);
		audio.bpmTable28kHz[i] = (int64_t)((dSamplesPerTick28kHz * (UINT32_MAX+1.0)) + 0.5);
		audio.bpmTable20kHz[i] = (int64_t)((dSamplesPerTick20kHz * (UINT32_MAX+1.0)) + 0.5);
	}
}

static void generateTickLengthTable(bool vblankTimingFlag)
{
	for (int32_t bpm = 32; bpm <= 255; bpm++)
	{
		double dHz;

		if (vblankTimingFlag)
			dHz = AMIGA_PAL_VBLANK_HZ;
		else
			dHz = ciaBpm2Hz(bpm);

		// BPM -> Hz -> tick length for performance counter (syncing visuals to audio)
		double dTimeInt;
		double dTimeFrac = modf(editor.dPerfFreq / dHz, &dTimeInt);
		const int32_t timeInt = (int32_t)dTimeInt;
	
		dTimeFrac = floor((UINT32_MAX+1.0) * dTimeFrac); // fractional part (scaled to 0..2^32-1)

		audio.tickLengthTable[bpm-32] = ((uint64_t)timeInt << 32) | (uint32_t)dTimeFrac;
	}
}

void updateReplayerTimingMode(void)
{
	const bool audioWasntLocked = !audio.locked;
	if (audioWasntLocked)
		lockAudio();

	const bool vblankTimingMode = (editor.timingMode == TEMPO_MODE_VBLANK);
	generateBpmTables(vblankTimingMode);
	generateTickLengthTable(vblankTimingMode);

	if (audioWasntLocked)
		unlockAudio();
}

bool setupAudio(void)
{
	SDL_AudioSpec want, have;

	want.freq = config.soundFrequency;
	want.samples = (uint16_t)config.soundBufferSize;
	want.format = AUDIO_S16;
	want.channels = 2;
	want.callback = audioCallback;
	want.userdata = NULL;

	dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
	if (dev == 0)
	{
		showErrorMsgBox("Unable to open audio device: %s", SDL_GetError());
		return false;
	}

	// lower than this is not safe for the BLEP synthesis in the mixer
	const int32_t minFreq = (int32_t)(PAULA_PAL_CLK / 113.0 / 2.0)+1; // /2 because we do 2x oversampling
	if (have.freq < minFreq)
	{
		showErrorMsgBox("Unable to open audio: An audio rate below %dHz can't be used!", minFreq);
		return false;
	}

	if (have.format != want.format)
	{
		showErrorMsgBox("Unable to open audio: The sample format (signed 16-bit) couldn't be used!");
		return false;
	}

	audio.outputRate = have.freq;
	audio.audioBufferSize = have.samples;
	audio.dPeriodToDeltaDiv = (double)PAULA_PAL_CLK / audio.outputRate;

	// we do 2x oversampling if the audio output rate is below 96kHz
	audio.oversamplingFlag = (audio.outputRate < 96000);

	updateReplayerTimingMode(); // also generates the BPM tables used below

	const int32_t lowestBPM = 32;
	const int32_t pat2SmpMaxSamples = (audio.bpmTable20kHz[lowestBPM-32] + (1LL + 31)) >> 32; // ceil (rounded upwards)
	const int32_t renderMaxSamples = (audio.bpmTable[lowestBPM-32] + (1LL + 31)) >> 32; // ceil (rounded upwards)
	const int32_t maxSamplesToMix = MAX(pat2SmpMaxSamples, renderMaxSamples) * 2; // *2 for headroom (XXX: buggy code somewhere?)

	dMixBufferLUnaligned = (double *)MALLOC_PAD(maxSamplesToMix * sizeof (double), 256);
	dMixBufferRUnaligned = (double *)MALLOC_PAD(maxSamplesToMix * sizeof (double), 256);

	if (dMixBufferLUnaligned == NULL || dMixBufferRUnaligned == NULL)
	{
		showErrorMsgBox("Out of memory!");
		return false;
	}

	dMixBufferL = (double *)ALIGN_PTR(dMixBufferLUnaligned, 256);
	dMixBufferR = (double *)ALIGN_PTR(dMixBufferRUnaligned, 256);

	mixerSetStereoSeparation(config.stereoSeparation);

	filterModel = config.filterModel;
	ledFilterEnabled = false;
	calculateFilterCoeffs();

	audio.samplesPerTick64 = audio.bpmTable[125-32]; // BPM 125
	audio.tickSampleCounter64 = 0; // zero tick sample counter so that it will instantly initiate a tick

	calcAudioLatencyVars(audio.audioBufferSize, audio.outputRate);

	resetCachedMixerPeriod();
	clearMixerDownsamplerStates();
	audio.resetSyncTickTimeFlag = true;

	updateFilterFunc();
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
}

void toggleAmigaPanMode(void)
{
	const bool audioWasntLocked = !audio.locked;
	if (audioWasntLocked)
		lockAudio();

	amigaPanFlag ^= 1;
	if (!amigaPanFlag)
	{
		mixerSetStereoSeparation(config.stereoSeparation);
		displayMsg("AMIGA PANNING OFF");
	}
	else
	{
		mixerSetStereoSeparation(100);
		displayMsg("AMIGA PANNING ON");
	}

	if (audioWasntLocked)
		unlockAudio();
}

uint16_t get16BitPeak(int16_t *sampleData, uint32_t sampleLength)
{
	uint16_t samplePeak = 0;
	for (uint32_t i = 0; i < sampleLength; i++)
	{
		uint16_t sample = ABS(sampleData[i]);
		if (samplePeak < sample)
			samplePeak = sample;
	}

	return samplePeak;
}

uint32_t get32BitPeak(int32_t *sampleData, uint32_t sampleLength)
{
	uint32_t samplePeak = 0;
	for (uint32_t i = 0; i < sampleLength; i++)
	{
		uint32_t sample = ABS(sampleData[i]);
		if (samplePeak < sample)
			samplePeak = sample;
	}

	return samplePeak;
}

float getFloatPeak(float *fSampleData, uint32_t sampleLength)
{
	float fSamplePeak = 0.0f;
	for (uint32_t i = 0; i < sampleLength; i++)
	{
		const float fSample = fabsf(fSampleData[i]);
		if (fSamplePeak < fSample)
			fSamplePeak = fSample;
	}

	return fSamplePeak;
}

double getDoublePeak(double *dSampleData, uint32_t sampleLength)
{
	double dSamplePeak = 0.0;
	for (uint32_t i = 0; i < sampleLength; i++)
	{
		const double dSample = fabs(dSampleData[i]);
		if (dSamplePeak < dSample)
			dSamplePeak = dSample;
	}

	return dSamplePeak;
}

void normalize16BitTo8Bit(int16_t *sampleData, uint32_t sampleLength)
{
	const uint16_t samplePeak = get16BitPeak(sampleData, sampleLength);
	if (samplePeak == 0 || samplePeak >= INT16_MAX)
		return;

	const double dGain = (double)INT16_MAX / samplePeak;
	for (uint32_t i = 0; i < sampleLength; i++)
	{
		const int32_t sample = (const int32_t)(sampleData[i] * dGain);
		sampleData[i] = (int16_t)sample;
	}
}

void normalize32BitTo8Bit(int32_t *sampleData, uint32_t sampleLength)
{
	const uint32_t samplePeak = get32BitPeak(sampleData, sampleLength);
	if (samplePeak == 0 || samplePeak >= INT32_MAX)
		return;

	const double dGain = (double)INT32_MAX / samplePeak;
	for (uint32_t i = 0; i < sampleLength; i++)
	{
		const int32_t sample = (const int32_t)(sampleData[i] * dGain);
		sampleData[i] = (int32_t)sample;
	}
}

void normalizeFloatTo8Bit(float *fSampleData, uint32_t sampleLength)
{
	const float fSamplePeak = getFloatPeak(fSampleData, sampleLength);
	if (fSamplePeak <= 0.0f)
		return;

	const float fGain = INT8_MAX / fSamplePeak;
	for (uint32_t i = 0; i < sampleLength; i++)
		fSampleData[i] *= fGain;
}

void normalizeDoubleTo8Bit(double *dSampleData, uint32_t sampleLength)
{
	const double dSamplePeak = getDoublePeak(dSampleData, sampleLength);
	if (dSamplePeak <= 0.0)
		return;

	const double dGain = INT8_MAX / dSamplePeak;
	for (uint32_t i = 0; i < sampleLength; i++)
		dSampleData[i] *= dGain;
}
