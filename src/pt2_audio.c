// for finding memory leaks in debug mode with Visual Studio 
#if defined _DEBUG && defined _MSC_VER
#include <crtdbg.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <math.h>
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
#include "pt2_audio.h"
#include "pt2_helpers.h"
#include "pt2_config.h"
#include "pt2_textout.h"
#include "pt2_scopes.h"
#include "pt2_sync.h"
#include "pt2_downsample2x.h"
#include "pt2_replayer.h"
#include "pt2_paula.h"
#include "pt2_amigafilters.h"

// cumulative mid/side normalization factor (1/sqrt(2))*(1/sqrt(2))
#define STEREO_NORM_FACTOR 0.5

#define INITIAL_DITHER_SEED 0x12345000

static uint8_t panningMode;
static int32_t randSeed = INITIAL_DITHER_SEED, stereoSeparation = 100;
static uint32_t audLatencyPerfValInt, audLatencyPerfValFrac;
static uint64_t tickTime64, tickTime64Frac;
static double *dMixBufferL, *dMixBufferR;
static double dPrngStateL, dPrngStateR, dSideFactor;
static SDL_AudioDeviceID dev;

// for audio/video syncing
static uint32_t tickTimeLen, tickTimeLenFrac;

audio_t audio; // globalized

static void calcAudioLatencyVars(int32_t audioBufferSize, int32_t audioFreq)
{
	double dInt, dFrac;

	if (audioFreq == 0)
		return;

	const double dAudioLatencySecs = audioBufferSize / (double)audioFreq;

	dFrac = modf(dAudioLatencySecs * hpcFreq.dFreq, &dInt);

	// integer part
	audLatencyPerfValInt = (uint32_t)dInt;

	// fractional part (scaled to 0..2^32-1)
	audLatencyPerfValFrac = (uint32_t)((dFrac * (UINT32_MAX+1.0)) + 0.5); // rounded
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

#define NORM_FACTOR 2.0 /* nominally correct, but can clip from high-pass filter overshoot */

static inline void processMixedSamplesAmigaPanning(int32_t i, int16_t *out)
{
	int32_t smp32;
	double dPrng;

	double dL = dMixBufferL[i];
	double dR = dMixBufferR[i];

	// normalize w/ phase-inversion (A500/A1200 has a phase-inverted audio signal)
	dL *= NORM_FACTOR * (-INT16_MAX / (double)PAULA_VOICES);
	dR *= NORM_FACTOR * (-INT16_MAX / (double)PAULA_VOICES);

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
	dL *= NORM_FACTOR * (-INT16_MAX / (double)PAULA_VOICES);
	dR *= NORM_FACTOR * (-INT16_MAX / (double)PAULA_VOICES);

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
	dL *= NORM_FACTOR * (-INT16_MAX / (double)PAULA_VOICES);
	dR *= NORM_FACTOR * (-INT16_MAX / (double)PAULA_VOICES);

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
	dL *= NORM_FACTOR * (-INT16_MAX / (double)PAULA_VOICES);
	dR *= NORM_FACTOR * (-INT16_MAX / (double)PAULA_VOICES);

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
	if (audio.oversamplingFlag) // 2x oversampling
	{
		// mix and filter channels (at 2x rate)
		paulaGenerateSamples(dMixBufferL, dMixBufferR, numSamples*2);
		processAmigaFilters(dMixBufferL, dMixBufferR, numSamples*2);

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
		paulaGenerateSamples(dMixBufferL, dMixBufferR, numSamples);
		processAmigaFilters(dMixBufferL, dMixBufferR, numSamples);

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

static void fillVisualsSyncBuffer(void)
{
	chSyncData_t chSyncData;

	if (audio.resetSyncTickTimeFlag)
	{
		audio.resetSyncTickTimeFlag = false;

		tickTime64 = SDL_GetPerformanceCounter() + audLatencyPerfValInt;
		tickTime64Frac = audLatencyPerfValFrac;
	}

	if (song != NULL)
	{
		moduleChannel_t *ch = song->channels;
		paulaVoice_t *v = paula;
		syncedChannel_t *sc = chSyncData.channels;

		for (int32_t i = 0; i < PAULA_VOICES; i++, ch++, sc++, v++)
		{
			sc->flags = v->syncFlags | ch->syncFlags;
			ch->syncFlags = v->syncFlags = 0; // clear sync flags

			sc->volume = v->syncVolume;
			sc->period = v->syncPeriod;
			sc->triggerData = v->syncTriggerData;
			sc->triggerLength = v->syncTriggerLength;
			sc->newData = v->AUD_LC;
			sc->newLength = v->AUD_LEN * 2;
			sc->vuVolume = ch->syncVuVolume;
			sc->analyzerVolume = ch->syncAnalyzerVolume;
			sc->analyzerPeriod = ch->syncAnalyzerPeriod;
		}

		chSyncData.timestamp = tickTime64;
		chQueuePush(chSyncData);
	}

	tickTime64 += tickTimeLen;
	tickTime64Frac += tickTimeLenFrac;
	if (tickTime64Frac > UINT32_MAX)
	{
		tickTime64Frac &= UINT32_MAX;
		tickTime64++;
	}
}

static void SDLCALL audioCallback(void *userdata, Uint8 *stream, int len)
{
	if (editor.mod2WavOngoing || editor.pat2SmpOngoing) // send silence to sound output device
	{
		memset(stream, 0, len);
		return;
	}

	int16_t *streamOut = (int16_t *)stream;

	uint32_t samplesLeft = (uint32_t)len >> 2;
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

		const uint32_t remainingTickSamples = ((uint64_t)audio.tickSampleCounter64 + UINT32_MAX) >> 32; // ceil rounding (upwards)

		uint32_t samplesTodo = samplesLeft;
		if (samplesTodo > remainingTickSamples)
			samplesTodo = remainingTickSamples;

		outputAudio(streamOut, samplesTodo);
		streamOut += samplesTodo << 1;

		samplesLeft -= samplesTodo;
		audio.tickSampleCounter64 -= (int64_t)samplesTodo << 32;
	}

	(void)userdata;
}

void audioSetStereoSeparation(uint8_t percentage) // 0..100 (percentage)
{
	assert(percentage <= 100);

	stereoSeparation = percentage;
	dSideFactor = (percentage / 100.0) * STEREO_NORM_FACTOR;
}

void generateBpmTable(double dAudioFreq, bool vblankTimingFlag)
{
	const bool audioWasntLocked = !audio.locked;
	if (audioWasntLocked)
		lockAudio();

	for (int32_t bpm = 32; bpm <= 255; bpm++)
	{
		double dBpmHz;
		
		if (vblankTimingFlag)
			dBpmHz = AMIGA_PAL_VBLANK_HZ;
		else
			dBpmHz = ciaBpm2Hz(bpm);

		const double dSamplesPerTick = dAudioFreq / dBpmHz;

		// convert to rounded 32.32 fixed-point
		const int32_t i = bpm - 32;
		audio.samplesPerTickTable[i] = (int64_t)((dSamplesPerTick * (UINT32_MAX+1.0)) + 0.5);
	}

	audio.tickSampleCounter64 = 0;

	if (audioWasntLocked)
		unlockAudio();
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
		double dTimeFrac = modf(hpcFreq.dFreq / dHz, &dTimeInt);
		const int32_t timeInt = (int32_t)dTimeInt;
	
		dTimeFrac = floor((dTimeFrac * (UINT32_MAX+1.0)) + 0.5); // fractional part (scaled to 0..2^32-1)

		audio.tickLengthTable[bpm-32] = ((uint64_t)timeInt << 32) | (uint32_t)dTimeFrac;
	}
}

void updateReplayerTimingMode(void)
{
	const bool audioWasntLocked = !audio.locked;
	if (audioWasntLocked)
		lockAudio();

	const bool vblankTimingMode = (editor.timingMode == TEMPO_MODE_VBLANK);
	generateBpmTable(audio.outputRate, vblankTimingMode);
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
	audio.oversamplingFlag = (audio.outputRate < 96000); // we do 2x oversampling if the audio output rate is below 96kHz

	const int32_t audioFrequency = audio.oversamplingFlag ? audio.outputRate*2 : audio.outputRate;
	const uint32_t maxSamplesToMix = (int32_t)ceil(audioFrequency / (REPLAYER_MIN_BPM / 2.5));

	dMixBufferL = (double *)malloc((maxSamplesToMix + 1) * sizeof (double));
	dMixBufferR = (double *)malloc((maxSamplesToMix + 1) * sizeof (double));

	if (dMixBufferL == NULL || dMixBufferR == NULL)
	{
		// these two are free'd later

		showErrorMsgBox("Out of memory!");
		return false;
	}

	paulaSetOutputFrequency(audio.outputRate, audio.oversamplingFlag);
	audioSetStereoSeparation(config.stereoSeparation);
	updateReplayerTimingMode(); // also generates the BPM table (audio.bpmTable)
	setAmigaFilterModel(config.filterModel);
	setLEDFilter(false);
	setupAmigaFilters(audio.outputRate);
	calcAudioLatencyVars(audio.audioBufferSize, audio.outputRate);

	clearMixerDownsamplerStates();
	audio.resetSyncTickTimeFlag = true;

	audio.samplesPerTick64 = audio.samplesPerTickTable[125-32]; // BPM 125
	audio.tickSampleCounter64 = 0; // zero tick sample counter so that it will instantly initiate a tick

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

	if (dMixBufferL != NULL)
	{
		free(dMixBufferL);
		dMixBufferL = NULL;
	}

	if (dMixBufferR != NULL)
	{
		free(dMixBufferR);
		dMixBufferR = NULL;
	}
}

void toggleAmigaPanMode(void)
{
	panningMode = (panningMode + 1) % 3;

	const bool audioWasntLocked = !audio.locked;
	if (audioWasntLocked)
		lockAudio();

	if (panningMode == 0)
		audioSetStereoSeparation(config.stereoSeparation);
	else if (panningMode == 1)
		audioSetStereoSeparation(0);
	else
		audioSetStereoSeparation(100);

	if (audioWasntLocked)
		unlockAudio();

	if (panningMode == 0)
		displayMsg("CUSTOM PANNING");
	else if (panningMode == 1)
		displayMsg("CENTERED PANNING");
	else
		displayMsg("AMIGA PANNING");
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
