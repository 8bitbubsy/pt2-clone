#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "pt2_replayer.h"

// for the low-pass/high-pass filters in the SAMPLER screen
#define FILTERS_BASE_FREQ (PAULA_PAL_CLK / 214.0)

// too many bits makes little sense here

#define BPM_FRAC_BITS 52
#define BPM_FRAC_SCALE (1ULL << BPM_FRAC_BITS)
#define BPM_FRAC_MASK (BPM_FRAC_SCALE-1)

#define TICK_TIME_FRAC_BITS 52
#define TICK_TIME_FRAC_SCALE (1ULL << TICK_TIME_FRAC_BITS)
#define TICK_TIME_FRAC_MASK (TICK_TIME_FRAC_SCALE-1)

typedef struct audio_t
{
	volatile bool locked, isSampling;

	bool ledFilterEnabled, oversamplingFlag;
	
	uint32_t amigaModel, outputRate, audioBufferSize;

	uint32_t tickSampleCounter, samplesPerTickInt, samplesPerTickIntTab[(MAX_BPM-MIN_BPM)+1];
	uint64_t tickSampleCounterFrac, samplesPerTickFrac, samplesPerTickFracTab[(MAX_BPM-MIN_BPM)+1];

	// for audio sampling
	bool rescanAudioDevicesSupported;

	// for audio/video syncing
	bool resetSyncTickTimeFlag;
	uint32_t tickTimeIntTab[(MAX_BPM-MIN_BPM)+1];
	uint64_t tickTimeFracTab[(MAX_BPM-MIN_BPM)+1];
} audio_t;

void setAmigaFilterModel(uint8_t model);
void toggleAmigaFilterModel(void);
void setLEDFilter(bool state);
void toggleLEDFilter(void);

void updateReplayerTimingMode(void);
void generateBpmTable(double dAudioFreq, bool vblankTimingFlag);
uint16_t get16BitPeak(int16_t *sampleData, uint32_t sampleLength);
uint32_t get32BitPeak(int32_t *sampleData, uint32_t sampleLength);
float getFloatPeak(float *fSampleData, uint32_t sampleLength);
double getDoublePeak(double *dSampleData, uint32_t sampleLength);
void normalize16BitTo8Bit(int16_t *sampleData, uint32_t sampleLength);
void normalize32BitTo8Bit(int32_t *sampleData, uint32_t sampleLength);
void normalizeFloatTo8Bit(float *fSampleData, uint32_t sampleLength);
void normalizeDoubleTo8Bit(double *dSampleData, uint32_t sampleLength);
void toggleAmigaPanMode(void);
void lockAudio(void);
void unlockAudio(void);
void audioSetStereoSeparation(uint8_t percentage);
void outputAudio(int16_t *target, int32_t numSamples);
bool setupAudio(void);
void audioClose(void);

extern audio_t audio; // pt2_audio.c
