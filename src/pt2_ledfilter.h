#pragma once

#include <stdint.h>
#include <stdbool.h>

typedef struct ledFilter_t
{
	double buffer[4];
	double c, ci, feedback, bg, cg, c2;
} ledFilter_t;

void clearLEDFilterState(ledFilter_t *filterLED);
void calcLEDFilterCoeffs(const double sr, const double hz, const double fb, ledFilter_t *filter);
void LEDFilter(ledFilter_t *f, const double *in, double *out);
