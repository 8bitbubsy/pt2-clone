#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "pt2_header.h"

typedef struct voice_t
{
	volatile bool DMA_active;

	// internal values (don't modify directly!)
	bool DMATriggerFlag, nextSampleStage;
	int8_t AUD_DAT[2]; // DMA data buffer
	const int8_t *location; // current location
	uint16_t lengthCounter; // current length
	int32_t sampleCounter; // how many bytes left in AUD_DAT
	double dSample; // current sample point

	// registers modified by Paula functions
	const int8_t *AUD_LC; // location
	uint16_t AUD_LEN; // length (in words)
	double AUD_PER_delta, AUD_PER_deltamul; // delta
	double AUD_VOL; // volume

	double dDelta, dPhase;

	// for BLEP synthesis
	double dLastDelta, dLastPhase, dLastDeltaMul, dBlepOffset, dDeltaMul;

	// used for pt2_sync.c (visualizers)
	uint8_t syncFlags;
	uint8_t syncVolume;
	int32_t syncPeriod;
	int32_t syncTriggerLength;
	const int8_t *syncTriggerData;
} paulaVoice_t;

#define PAULA_VOICES 4
#define PAULA_PAL_CLK AMIGA_PAL_CCK_HZ
#define PAL_PAULA_MIN_PERIOD 113
#define PAL_PAULA_MIN_SAFE_PERIOD 124
#define PAL_PAULA_MAX_HZ (PAULA_PAL_CLK / (double)PAL_PAULA_MIN_PERIOD)
#define PAL_PAULA_MAX_SAFE_HZ (PAULA_PAL_CLK / (double)PAL_PAULA_MIN_SAFE_PERIOD)

void paulaSetOutputFrequency(double dAudioFreq, bool oversampling2x);
void paulaSetDMACON(uint16_t bits); // $DFF096 register write (only controls paula DMA)
void paulaSetPeriod(int32_t ch, uint16_t period);
void paulaSetVolume(int32_t ch, uint16_t vol);
void paulaSetLength(int32_t ch, uint16_t len);
void paulaSetData(int32_t ch, const int8_t *src);
void paulaGenerateSamples(double *dOutL, double *dOutR, int32_t numSamples);

extern paulaVoice_t paula[PAULA_VOICES]; // pt2_paula.c
