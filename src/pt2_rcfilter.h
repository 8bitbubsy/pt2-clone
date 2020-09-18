#pragma once

#include <stdint.h>
#include <stdbool.h>

// adding this prevents denormalized numbers, which is slow
#define DENORMAL_OFFSET 1e-15

typedef struct rcFilter_t
{
	double buffer[2];
	double c, c2, g, cg;
} rcFilter_t;

void calcRCFilterCoeffs(const double sr, const double hz, rcFilter_t *f);
void clearRCFilterState(rcFilter_t *f);
void RCLowPassFilterStereo(rcFilter_t *f, const double *in, double *out);
void RCHighPassFilterStereo(rcFilter_t *f, const double *in, double *out);
void RCLowPassFilter(rcFilter_t *f, const double in, double *out);
void RCHighPassFilter(rcFilter_t *f, const double in, double *out);

