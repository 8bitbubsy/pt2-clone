/* The "LED" filter and BLEP routines were coded by aciddose.
** Low-pass filter is based on https://bel.fi/alankila/modguide/interpolate.txt */

#pragma once

#include <stdint.h>
#include <stdbool.h>

typedef struct lossyIntegrator_t
{
	double dBuffer[2], b0, b1;
} lossyIntegrator_t;

void resetCachedMixerPeriod(void);
void resetDitherSeed(void);
void calcCoeffLossyIntegrator(double dSr, double dHz, lossyIntegrator_t *filter);
void lossyIntegrator(lossyIntegrator_t *filter, double *dIn, double *dOut);
void lossyIntegratorHighPass(lossyIntegrator_t *filter, double *dIn, double *dOut);
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
void clearPaulaAndScopes(void);
void mixerUpdateLoops(void);
void mixerKillVoice(uint8_t ch);
void turnOffVoices(void);
void mixerCalcVoicePans(uint8_t stereoSeparation);
void mixerSetSamplesPerTick(int32_t val);
void mixerClearSampleCounter(void);
void outputAudio(int16_t *target, int32_t numSamples);
