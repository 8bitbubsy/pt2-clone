#pragma once

#include <stdint.h>
#include <stdbool.h>

void setupAmigaFilters(double dAudioFreq);
void resetAmigaFilterStates(void);
void setAmigaFilterModel(uint8_t amigaModel);
void setLEDFilter(bool state);
void toggleAmigaFilterModel(void);

extern void (*processAmigaFilters)(double *, double *, int32_t);
