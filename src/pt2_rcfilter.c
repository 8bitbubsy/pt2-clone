/* 1-pole 6dB/oct RC filters, from:
** https://www.musicdsp.org/en/latest/Filters/116-one-pole-lp-and-hp.html
**
** There's no frequency pre-warping with tan(), but doing that would
** result in a cutoff that sounded slightly too low.
*/

#include "pt2_math.h"
#include "pt2_rcfilter.h"

void calcRCFilterCoeffs(double sr, double hz, rcFilter_t *f)
{
	const double a = (hz < sr / 2.0) ? pt2_cos((PT2_TWO_PI * hz) / sr) : 1.0;
	const double b = 2.0 - a;
	const double c = b - pt2_sqrt((b * b) - 1.0);

	f->c1 = 1.0 - c;
	f->c2 = c;
}

void clearRCFilterState(rcFilter_t *f)
{
	f->tmp[0] = f->tmp[1] = 0.0;
}

void RCLowPassFilterStereo(rcFilter_t *f, const double *in, double *out)
{
	// left channel
	f->tmp[0] = (f->c1 * in[0]) + (f->c2 * f->tmp[0]);
	out[0] = f->tmp[0];

	// right channel
	f->tmp[1] = (f->c1 * in[1]) + (f->c2 * f->tmp[1]);
	out[1] = f->tmp[1];
}

void RCHighPassFilterStereo(rcFilter_t *f, const double *in, double *out)
{
	// left channel
	f->tmp[0] = (f->c1 * in[0]) + (f->c2 * f->tmp[0]);
	out[0] = in[0] - f->tmp[0];

	// right channel
	f->tmp[1] = (f->c1 * in[1]) + (f->c2 * f->tmp[1]);
	out[1] = in[1] - f->tmp[1];
}

void RCLowPassFilter(rcFilter_t *f, const double in, double *out)
{
	f->tmp[0] = (f->c1 * in) + (f->c2 * f->tmp[0]);
	*out = f->tmp[0];
}

void RCHighPassFilter(rcFilter_t *f, const double in, double *out)
{
	f->tmp[0] = (f->c1 * in) + (f->c2 * f->tmp[0]);
	*out = in - f->tmp[0];
}
