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
#include "pt2_visuals_sync.h"
#include "pt2_downsample2x.h"
#include "pt2_replayer.h"
#include "pt2_paula.h"

// cumulative mid/side normalization factor (1/sqrt(2))*(1/sqrt(2))
#define STEREO_NORM_FACTOR 0.5

static uint8_t panningMode;
static int32_t stereoSeparation = 100;
static double *dMixBufferL, *dMixBufferR, dSideFactor;
static SDL_AudioDeviceID dev;

audio_t audio; // globalized

void setAmigaFilterModel(uint8_t model)
{
	if (audio.amigaModel == model)
		return; // same state as before!

	const bool audioWasntLocked = !audio.locked;
	if (audioWasntLocked)
		lockAudio();

	audio.amigaModel = model;

	const int32_t paulaMixFrequency = audio.oversamplingFlag ? audio.outputRate*2 : audio.outputRate;
	paulaSetup(paulaMixFrequency, audio.amigaModel);

	if (audioWasntLocked)
		unlockAudio();
}

void toggleAmigaFilterModel(void)
{
	const bool audioWasntLocked = !audio.locked;
	if (audioWasntLocked)
		lockAudio();

	audio.amigaModel ^= 1;

	const int32_t paulaMixFrequency = audio.oversamplingFlag ? audio.outputRate*2 : audio.outputRate;
	paulaSetup(paulaMixFrequency, audio.amigaModel);

	if (audioWasntLocked)
		unlockAudio();

	if (audio.amigaModel == MODEL_A500)
		displayMsg("AUDIO: AMIGA 500");
	else
		displayMsg("AUDIO: AMIGA 1200");
}

void setLEDFilter(bool state)
{
	if (audio.ledFilterEnabled == state)
		return; // same state as before!

	const bool audioWasntLocked = !audio.locked;
	if (audioWasntLocked)
		lockAudio();

	audio.ledFilterEnabled = state;
	paulaWriteByte(0xBFE001, (uint8_t)audio.ledFilterEnabled << 1);

	if (audioWasntLocked)
		unlockAudio();
}

void toggleLEDFilter(void)
{
	const bool audioWasntLocked = !audio.locked;
	if (audioWasntLocked)
		lockAudio();

	audio.ledFilterEnabled ^= 1;
	paulaWriteByte(0xBFE001, (uint8_t)audio.ledFilterEnabled << 1);

	if (audioWasntLocked)
		unlockAudio();
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

#define NORM_FACTOR 2.0 /* nominally correct, but can clip from high-pass filter overshoot */

static inline void processMixedSamplesAmigaPanning(int32_t i, int16_t *out)
{
	int32_t smp32;

	double dL = dMixBufferL[i];
	double dR = dMixBufferR[i];

	// normalize w/ phase-inversion (A500/A1200 has a phase-inverted audio signal)
	dL *= NORM_FACTOR * (-INT16_MAX / (double)PAULA_VOICES);
	dR *= NORM_FACTOR * (-INT16_MAX / (double)PAULA_VOICES);

	// left channel
	smp32 = (int32_t)dL;
	CLAMP16(smp32);
	out[0] = (int16_t)smp32;

	// right channel
	smp32 = (int32_t)dR;
	CLAMP16(smp32);
	out[1] = (int16_t)smp32;
}

static inline void processMixedSamples(int32_t i, int16_t *out)
{
	int32_t smp32;

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

	// left channel
	smp32 = (int32_t)dL;
	CLAMP16(smp32);
	out[0] = (int16_t)smp32;

	// right channel
	smp32 = (int32_t)dR;
	CLAMP16(smp32);
	out[1] = (int16_t)smp32;
}

static inline void processMixedSamplesAmigaPanning_2x(int32_t i, int16_t *out) // 2x oversampling
{
	int32_t smp32;
	double dL, dR;

	// 2x downsampling (decimation)
	const uint32_t offset1 = (i << 1) + 0;
	const uint32_t offset2 = (i << 1) + 1;
	dL = decimate2x_L(dMixBufferL[offset1], dMixBufferL[offset2]);
	dR = decimate2x_R(dMixBufferR[offset1], dMixBufferR[offset2]);

	// normalize w/ phase-inversion (A500/A1200 has a phase-inverted audio signal)
	dL *= NORM_FACTOR * (-INT16_MAX / (double)PAULA_VOICES);
	dR *= NORM_FACTOR * (-INT16_MAX / (double)PAULA_VOICES);

	// left channel
	smp32 = (int32_t)dL;
	CLAMP16(smp32);
	out[0] = (int16_t)smp32;

	// right channel
	smp32 = (int32_t)dR;
	CLAMP16(smp32);
	out[1] = (int16_t)smp32;
}

static inline void processMixedSamples_2x(int32_t i, int16_t *out) // 2x oversampling
{
	int32_t smp32;
	double dL, dR;

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

	// left channel
	smp32 = (int32_t)dL;
	CLAMP16(smp32);
	out[0] = (int16_t)smp32;

	// right channel
	smp32 = (int32_t)dR;
	CLAMP16(smp32);
	out[1] = (int16_t)smp32;
}

void outputAudio(int16_t *target, int32_t numSamples)
{
	if (audio.oversamplingFlag) // 2x oversampling
	{
		paulaGenerateSamples(dMixBufferL, dMixBufferR, numSamples*2);

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
		paulaGenerateSamples(dMixBufferL, dMixBufferR, numSamples);

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

static void SDLCALL audioCallback(void *userdata, Uint8 *stream, int len)
{
	if (editor.mod2WavOngoing || editor.pat2SmpOngoing) // send silence to sound output device
	{
		memset(stream, 0, len);
		return;
	}

	int16_t *streamOut = (int16_t *)stream;

	uint32_t samplesLeft = (uint32_t)len / 4;
	while (samplesLeft > 0)
	{
		if (audio.tickSampleCounter == 0) // new replayer tick
		{
			if (editor.songPlaying)
			{
				intMusic(); // PT replayer ticker (also sets audio.samplesPerTickInt and audio.samplesPerTickFrac)
				fillVisualsSyncBuffer();
			}

			audio.tickSampleCounter = audio.samplesPerTickInt;

			audio.tickSampleCounterFrac += audio.samplesPerTickFrac;
			if (audio.tickSampleCounterFrac >= BPM_FRAC_SCALE)
			{
				audio.tickSampleCounterFrac &= BPM_FRAC_MASK;
				audio.tickSampleCounter++;
			}
		}

		uint32_t samplesToMix = samplesLeft;
		if (samplesToMix > audio.tickSampleCounter)
			samplesToMix = audio.tickSampleCounter;

		outputAudio(streamOut, samplesToMix);
		streamOut += samplesToMix * 2; // *2 for stereo

		audio.tickSampleCounter -= samplesToMix;
		samplesLeft -= samplesToMix;
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

	for (int32_t bpm = MIN_BPM; bpm <= MAX_BPM; bpm++)
	{
		const int32_t i = bpm - MIN_BPM; // index for tables

		double dBpmHz;
		if (vblankTimingFlag)
			dBpmHz = AMIGA_PAL_VBLANK_HZ;
		else
			dBpmHz = ciaBpm2Hz(bpm);

		const double dSamplesPerTick = dAudioFreq / dBpmHz;

		double dSamplesPerTickInt;
		double dSamplesPerTickFrac = modf(dSamplesPerTick, &dSamplesPerTickInt);

		audio.samplesPerTickIntTab[i] = (uint32_t)dSamplesPerTickInt;
		audio.samplesPerTickFracTab[i] = (uint64_t)((dSamplesPerTickFrac * BPM_FRAC_SCALE) + 0.5); // rounded
	}

	audio.tickSampleCounter = 0;
	audio.tickSampleCounterFrac = 0;

	if (audioWasntLocked)
		unlockAudio();
}

static void generateTickLengthTable(bool vblankTimingFlag)
{
	for (int32_t bpm = MIN_BPM; bpm <= MAX_BPM; bpm++)
	{
		const int32_t i = bpm - MIN_BPM; // index for tables

		double dHz;
		if (vblankTimingFlag)
			dHz = AMIGA_PAL_VBLANK_HZ;
		else
			dHz = ciaBpm2Hz(bpm);

		// BPM -> Hz -> tick length for performance counter (syncing visuals to audio)
		const double dTickTime = (double)hpcFreq.freq64 / dHz;

		double dTimeInt;
		double dTimeFrac = modf(dTickTime, &dTimeInt);

		audio.tickTimeIntTab[i] = (uint32_t)dTimeInt;
		audio.tickTimeFracTab[i] = (uint64_t)((dTimeFrac * TICK_TIME_FRAC_SCALE) + 0.5); // rounded
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

	dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, SDL_AUDIO_ALLOW_ANY_CHANGE);
	if (dev == 0)
	{
		showErrorMsgBox("Couldn't open audio device:\n\"%s\"\n\nDo you have an audio device enabled and plugged in?", SDL_GetError());
		return false;
	}

	// lower than this is not safe for the BLEP synthesis in the mixer
	const int32_t minFreq = (int32_t)(PAULA_PAL_CLK / 113.0 / 2.0)+1; // /2 because we do 2x oversampling
	if (have.freq < minFreq)
	{
		showErrorMsgBox("Unable to open audio: An audio rate below %dHz can't be used!", minFreq);
		return false;
	}

	if (have.format != AUDIO_S16)
	{
		showErrorMsgBox("Couldn't open audio device:\nThis program only supports 16-bit audio streams. Sorry!");
		return false;
	}

	audio.outputRate = have.freq;
	audio.audioBufferSize = have.samples;
	audio.oversamplingFlag = (audio.outputRate < 96000); // we do 2x oversampling if the audio output rate is below 96kHz
	audio.amigaModel = config.amigaModel;

	uint32_t maxFrequency = audio.outputRate;
	if (maxFrequency < config.mod2WavOutputFreq)
		maxFrequency = config.mod2WavOutputFreq;

	maxFrequency *= 2; // oversampling

	const int32_t paulaMixFrequency = audio.oversamplingFlag ? audio.outputRate*2 : audio.outputRate;
	int32_t maxSamplesPerTick = (int32_t)ceil(maxFrequency / (MIN_BPM / 2.5)) + 1;

	dMixBufferL = (double *)malloc(maxSamplesPerTick * sizeof (double));
	dMixBufferR = (double *)malloc(maxSamplesPerTick * sizeof (double));

	if (dMixBufferL == NULL || dMixBufferR == NULL)
	{
		// these two are free'd later
		showErrorMsgBox("Out of memory!");
		return false;
	}

	paulaSetup(paulaMixFrequency, audio.amigaModel);
	audioSetStereoSeparation(config.stereoSeparation);
	updateReplayerTimingMode(); // also generates the BPM table (audio.samplesPerTickIntTab & audio.samplesPerTickFracTab)
	setLEDFilter(false);
	calcAudioLatencyVars(audio.audioBufferSize, audio.outputRate);

	clearMixerDownsamplerStates();
	audio.resetSyncTickTimeFlag = true;

	audio.samplesPerTickInt = audio.samplesPerTickIntTab[125-MIN_BPM]; // BPM 125
	audio.samplesPerTickFrac = audio.samplesPerTickFracTab[125-MIN_BPM]; // BPM 125

	audio.tickSampleCounter = 0; // zero tick sample counter so that it will instantly initiate a tick
	audio.tickSampleCounterFrac = 0;

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
