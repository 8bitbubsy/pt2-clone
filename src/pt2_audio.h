#pragma once

#include <stdint.h>
#include <stdbool.h>

// adding this forces the FPU to enter slow mode
#define DENORMAL_OFFSET 1e-10

typedef struct rcFilter_t
{
	double buffer[2];
	double c, c2, g, cg;
} rcFilter_t;

typedef struct audio_t
{
	volatile bool locked;
	bool forceMixerOff;
	uint16_t bpmTab[256-32], bpmTab28kHz[256-32], bpmTab22kHz[256-32], bpmTabMod2Wav[256-32];
	uint32_t outputRate, audioBufferSize;
	double dPeriodToDeltaDiv;
} audio_t;

extern audio_t audio; // pt2_audio.c

void resetCachedMixerPeriod(void);
void resetAudioDithering(void);
void calcRCFilterCoeffs(const double sr, const double hz, rcFilter_t *f);
void clearRCFilterState(rcFilter_t *f);
void RCLowPassFilter(rcFilter_t *f, const double *in, double *out);
void RCHighPassFilter(rcFilter_t *f, const double *in, double *out);
void RCLowPassFilterMono(rcFilter_t *f, const double in, double *out);
void RCHighPassFilterMono(rcFilter_t *f, const double in, double *out);
void normalize32bitSigned(int32_t *sampleData, uint32_t sampleLength);
void normalize16bitSigned(int16_t *sampleData, uint32_t sampleLength);
void normalize8bitFloatSigned(float *fSampleData, uint32_t sampleLength);
void normalize8bitDoubleSigned(double *dSampleData, uint32_t sampleLength);
void setLEDFilter(bool state);
void toggleLEDFilter(void);
void toggleAmigaPanMode(void);
void toggleA500Filters(void);
void paulaStopDMA(int32_t ch);
void paulaStartDMA(int32_t ch);
void paulaSetPeriod(int32_t ch, uint16_t period);
void paulaSetVolume(int32_t ch, uint16_t vol);
void paulaSetLength(int32_t ch, uint16_t len);
void paulaSetData(int32_t ch, const int8_t *src);
void lockAudio(void);
void unlockAudio(void);
void mixerUpdateLoops(void);
void mixerKillVoice(int32_t ch);
void turnOffVoices(void);
void mixerCalcVoicePans(uint8_t stereoSeparation);
void mixerSetSamplesPerTick(uint32_t val);
void mixerClearSampleCounter(void);
void outputAudio(int16_t *target, int32_t numSamples);
