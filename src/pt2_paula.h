#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "pt2_header.h"

enum
{
	MODEL_A1200 = 0,
	MODEL_A500  = 1,
};

#define PAULA_VOICES 4
#define PAULA_PAL_CLK AMIGA_PAL_CCK_HZ
#define PAL_PAULA_MIN_PERIOD 113
#define PAL_PAULA_MIN_SAFE_PERIOD 124
#define PAL_PAULA_MAX_HZ (PAULA_PAL_CLK / (double)PAL_PAULA_MIN_PERIOD)
#define PAL_PAULA_MAX_SAFE_HZ (PAULA_PAL_CLK / (double)PAL_PAULA_MIN_SAFE_PERIOD)

void paulaSetup(double dOutputFreq, uint32_t amigaModel);
void paulaDisableFilters(void); // disables low-pass & high-pass filters ("LED" filter is kept)

int8_t *paulaGetNullSamplePtr(void);

void paulaWriteByte(uint32_t address, uint8_t data8);
void paulaWriteWord(uint32_t address, uint16_t data16);
void paulaWritePtr(uint32_t address, const int8_t *ptr);

// output is -4.00 .. 3.97 (can be louder because of high-pass filter)
void paulaGenerateSamples(double *dOutL, double *dOutR, int32_t numSamples);
