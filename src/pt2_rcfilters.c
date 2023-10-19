#include "pt2_math.h"
#include "pt2_rcfilters.h"

#define SMALL_NUMBER (1E-4)

/* 1-pole RC low-pass/high-pass filter, based on:
** https://www.musicdsp.org/en/latest/Filters/116-one-pole-lp-and-hp.html
*/

void setupOnePoleFilter(double audioRate, double cutOff, onePoleFilter_t *f)
{
	if (cutOff >= audioRate/2.0)
		cutOff = (audioRate/2.0) - SMALL_NUMBER;

	const double a = 2.0 - pt2_cos((PT2_TWO_PI * cutOff) / audioRate);
	const double b = a - pt2_sqrt((a * a) - 1.0);

	f->a1 = 1.0 - b;
	f->a2 = b;
}

void clearOnePoleFilterState(onePoleFilter_t *f)
{
	f->tmpL = f->tmpR = 0.0;
}

void onePoleLPFilter(onePoleFilter_t *f, const double in, double *out)
{
	f->tmpL = (in * f->a1) + (f->tmpL * f->a2);
	*out = f->tmpL;
}

void onePoleLPFilterStereo(onePoleFilter_t *f, const double *in, double *out)
{
	// left channel
	f->tmpL = (in[0] * f->a1) + (f->tmpL * f->a2);
	out[0] = f->tmpL;

	// right channel
	f->tmpR = (in[1] * f->a1) + (f->tmpR * f->a2);
	out[1] = f->tmpR;
}

void onePoleHPFilter(onePoleFilter_t *f, const double in, double *out)
{
	f->tmpL = (in * f->a1) + (f->tmpL * f->a2);
	*out = in - f->tmpL;
}

void onePoleHPFilterStereo(onePoleFilter_t *f, const double *in, double *out)
{
	// left channel
	f->tmpL = (in[0] * f->a1) + (f->tmpL * f->a2);
	out[0] = in[0] - f->tmpL;

	// right channel
	f->tmpR = (in[1] * f->a1) + (f->tmpR * f->a2);
	out[1] = in[1] - f->tmpR;
}

/* 2-pole RC low-pass filter with Q factor, based on:
** https://www.musicdsp.org/en/latest/Filters/38-lp-and-hp-filter.html
*/

void setupTwoPoleFilter(double audioRate, double cutOff, double qFactor, twoPoleFilter_t *f)
{
	if (cutOff >= audioRate/2.0)
		cutOff = (audioRate/2.0) - SMALL_NUMBER;

	const double a = 1.0 / pt2_tan((PT2_PI * cutOff) / audioRate);
	const double b = 1.0 / qFactor;

	f->a1 = 1.0 / (1.0 + b * a + a * a);
	f->a2 = 2.0 * f->a1;
	f->b1 = 2.0 * (1.0 - a*a) * f->a1;
	f->b2 = (1.0 - b * a + a * a) * f->a1;
}

void clearTwoPoleFilterState(twoPoleFilter_t *f)
{
	f->tmpL[0] = f->tmpL[1] = f->tmpL[2] = f->tmpL[3] = 0.0;
	f->tmpR[0] = f->tmpR[1] = f->tmpR[2] = f->tmpR[3] = 0.0;
}

void twoPoleLPFilter(twoPoleFilter_t *f, const double in, double *out)
{
	const double LOut = (in * f->a1) + (f->tmpL[0] * f->a2) + (f->tmpL[1] * f->a1) - (f->tmpL[2] * f->b1) - (f->tmpL[3] * f->b2);

	// shift states
	f->tmpL[1] = f->tmpL[0];
	f->tmpL[0] = in;
	f->tmpL[3] = f->tmpL[2];
	f->tmpL[2] = LOut;

	// set output
	*out = LOut;
}

void twoPoleLPFilterStereo(twoPoleFilter_t *f, const double *in, double *out)
{
	const double LOut = (in[0] * f->a1) + (f->tmpL[0] * f->a2) + (f->tmpL[1] * f->a1) - (f->tmpL[2] * f->b1) - (f->tmpL[3] * f->b2);
	const double ROut = (in[1] * f->a1) + (f->tmpR[0] * f->a2) + (f->tmpR[1] * f->a1) - (f->tmpR[2] * f->b1) - (f->tmpR[3] * f->b2);

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
