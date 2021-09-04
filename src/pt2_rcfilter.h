#pragma once

#include <stdint.h>
#include <stdbool.h>

typedef struct rcFilter_t
{
	double tmp[2], c1, c2;
} rcFilter_t;

void calcRCFilterCoeffs(double sr, double hz, rcFilter_t *f);
void clearRCFilterState(rcFilter_t *f);
void RCLowPassFilterStereo(rcFilter_t *f, const double *in, double *out);
void RCHighPassFilterStereo(rcFilter_t *f, const double *in, double *out);
void RCLowPassFilter(rcFilter_t *f, const double in, double *out);
void RCHighPassFilter(rcFilter_t *f, const double in, double *out);
