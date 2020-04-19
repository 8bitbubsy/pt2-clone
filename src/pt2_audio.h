/* The "LED" filter and BLEP routines were coded by aciddose.
** Low-pass filter is based on https://bel.fi/alankila/modguide/interpolate.txt */

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
bool renderToWav(char *fileName, bool checkIfFileExist);
void toggleAmigaPanMode(void);
void toggleA500Filters(void);
void paulaStopDMA(uint8_t ch);
void paulaStartDMA(uint8_t ch);
void paulaSetPeriod(uint8_t ch, uint16_t period);
void paulaSetVolume(uint8_t ch, uint16_t vol);
void paulaSetLength(uint8_t ch, uint16_t len);
void paulaSetData(uint8_t ch, const int8_t *src);
void lockAudio(void);
void unlockAudio(void);
void mixerUpdateLoops(void);
void mixerKillVoice(uint8_t ch);
void turnOffVoices(void);
void mixerCalcVoicePans(uint8_t stereoSeparation);
void mixerSetSamplesPerTick(int32_t val);
void mixerClearSampleCounter(void);
void outputAudio(int16_t *target, int32_t numSamples);
