#include <math.h>
#include "pt2_header.h"
#include "pt2_rcfilters.h"

#define SMALL_NUMBER (1E-4)

// 1-pole 6dB/oct RC low-pass filter (Direct Form II)
void setupOnePoleFilter(double audioRate, double cutOff, onePoleFilter_t *f)
{
	// this implementation should be OK as long as cutoff is below audioRate/4

	if (cutOff >= audioRate/2.0)
		cutOff = (audioRate/2.0) - SMALL_NUMBER;

	const double b1 = exp((-2.0 * PI) * cutOff / audioRate);
	const double a0 = 1.0 - b1;

	f->b1 = (float)b1;
	f->a0 = (float)a0;
}

void clearOnePoleFilterState(onePoleFilter_t *f)
{
	f->tmpL = f->tmpR = 0.0f;
}

void onePoleLPFilter(onePoleFilter_t *f, const float in, float *out)
{
	f->tmpL = (in * f->a0) + (f->tmpL * f->b1);
	*out = f->tmpL;
}

void onePoleLPFilterStereo(onePoleFilter_t *f, const float *in, float *out)
{
	// left channel
	f->tmpL = (in[0] * f->a0) + (f->tmpL * f->b1);
	out[0] = f->tmpL;

	// right channel
	f->tmpR = (in[1] * f->a0) + (f->tmpR * f->b1);
	out[1] = f->tmpR;
}

void onePoleHPFilter(onePoleFilter_t *f, const float in, float *out)
{
	f->tmpL = (in * f->a0) + (f->tmpL * f->b1);
	*out = in - f->tmpL;
}

void onePoleHPFilterStereo(onePoleFilter_t *f, const float *in, float *out)
{
	// left channel
	f->tmpL = (in[0] * f->a0) + (f->tmpL * f->b1);
	out[0] = in[0] - f->tmpL;

	// right channel
	f->tmpR = (in[1] * f->a0) + (f->tmpR * f->b1);
	out[1] = in[1] - f->tmpR;
}

/* 2-pole RC low-pass filter with Q factor, based on:
** https://www.musicdsp.org/en/latest/Filters/38-lp-and-hp-filter.html
*/

void setupTwoPoleFilter(double audioRate, double cutOff, double qFactor, twoPoleFilter_t *f)
{
	if (cutOff >= audioRate/2.0)
		cutOff = (audioRate/2.0) - SMALL_NUMBER;

	const double a = 1.0 / tan((PI * cutOff) / audioRate);
	const double r = 1.0 / qFactor; // resonance

	const double a1 = 1.0 / (1.0 + r * a + a * a);
	const double a2 = 2.0 * a1;
	const double b1 = 2.0 * (1.0 - a*a) * a1;
	const double b2 = (1.0 - r * a + a * a) * a1;

	f->a1 = (float)a1;
	f->a2 = (float)a2;
	f->b1 = (float)b1;
	f->b2 = (float)b2;
}

void clearTwoPoleFilterState(twoPoleFilter_t *f)
{
	f->tmpL[0] = f->tmpL[1] = f->tmpL[2] = f->tmpL[3] = 0.0f;
	f->tmpR[0] = f->tmpR[1] = f->tmpR[2] = f->tmpR[3] = 0.0f;
}

void twoPoleLPFilter(twoPoleFilter_t *f, const float in, float *out)
{
	const float LOut = (in * f->a1) + (f->tmpL[0] * f->a2) + (f->tmpL[1] * f->a1) - (f->tmpL[2] * f->b1) - (f->tmpL[3] * f->b2);

	// shift states
	f->tmpL[1] = f->tmpL[0];
	f->tmpL[0] = in;
	f->tmpL[3] = f->tmpL[2];
	f->tmpL[2] = LOut;

	// set output
	*out = LOut;
}

void twoPoleLPFilterStereo(twoPoleFilter_t *f, const float *in, float *out)
{
	const float LOut = (in[0] * f->a1) + (f->tmpL[0] * f->a2) + (f->tmpL[1] * f->a1) - (f->tmpL[2] * f->b1) - (f->tmpL[3] * f->b2);
	const float ROut = (in[1] * f->a1) + (f->tmpR[0] * f->a2) + (f->tmpR[1] * f->a1) - (f->tmpR[2] * f->b1) - (f->tmpR[3] * f->b2);

	// shift states

	f->tmpL[1] = f->tmpL[0];
	f->tmpL[0] = in[0];
	f->tmpL[3] = f->tmpL[2];
	f->tmpL[2] = LOut;

	f->tmpR[1] = f->tmpR[0];
	f->tmpR[0] = in[1];
	f->tmpR[3] = f->tmpR[2];
	f->tmpR[2] = ROut;

	// set outputs

	out[0] = LOut;
	out[1] = ROut;
}
