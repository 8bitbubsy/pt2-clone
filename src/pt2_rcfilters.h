#pragma once

#include <stdint.h>
#include <stdbool.h>

typedef struct onePoleFilter_t
{
	double tmpL, tmpR, a1, a2;
} onePoleFilter_t;

typedef struct twoPoleFilter_t
{
	double tmpL[4], tmpR[4], a1, a2, b1, b2;
} twoPoleFilter_t;

void setupOnePoleFilter(double audioRate, double cutOff, onePoleFilter_t *f);
void clearOnePoleFilterState(onePoleFilter_t *f);
void onePoleLPFilterStereo(onePoleFilter_t *f, const double *in, double *out);
void onePoleHPFilterStereo(onePoleFilter_t *f, const double *in, double *out);
void onePoleLPFilter(onePoleFilter_t *f, const double in, double *out);
void onePoleHPFilter(onePoleFilter_t *f, const double in, double *out);

void setupTwoPoleFilter(double audioRate, double cutOff, double qFactor, twoPoleFilter_t *f);
void clearTwoPoleFilterState(twoPoleFilter_t *f);
void twoPoleLPFilter(twoPoleFilter_t *f, const double in, double *out);
void twoPoleLPFilterStereo(twoPoleFilter_t *f, const double *in, double *out);
