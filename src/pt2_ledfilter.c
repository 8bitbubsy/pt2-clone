/* Biquad low-pass filter with resonance, from:
** https://www.musicdsp.org/en/latest/Filters/38-lp-and-hp-filter.html
*/

#include "pt2_ledfilter.h"
#include "pt2_math.h"

void clearLEDFilterState(ledFilter_t *f)
{
	f->LIn1 = f->LIn2 = f->LOut1 = f->LOut2 = 0.0;
	f->RIn1 = f->RIn2 = f->ROut1 = f->ROut2 = 0.0;
}

void calcLEDFilterCoeffs(double sr, double hz, double qfactor, ledFilter_t *filter)
{
	const double c = 1.0 / pt2_tan((PT2_PI * hz) / sr);
	const double r = 1.0 / qfactor;

	filter->a1 = 1.0 / (1.0 + r * c + c * c);
	filter->a2 = 2.0 * filter->a1;
	filter->a3 = filter->a1;
	filter->b1 = 2.0 * (1.0 - c*c) * filter->a1;
	filter->b2 = (1.0 - r * c + c * c) * filter->a1;
}

void LEDFilter(ledFilter_t *f, const double *in, double *out)
{
	const double LOut = (f->a1 * in[0]) + (f->a2 * f->LIn1) + (f->a3 * f->LIn2) - (f->b1 * f->LOut1) - (f->b2 * f->LOut2);
	const double ROut = (f->a1 * in[1]) + (f->a2 * f->RIn1) + (f->a3 * f->RIn2) - (f->b1 * f->ROut1) - (f->b2 * f->ROut2);

	// shift states

	f->LIn2 = f->LIn1;
	f->LIn1 = in[0];
	f->LOut2 = f->LOut1;
	f->LOut1 = LOut;

	f->RIn2 = f->RIn1;
	f->RIn1 = in[1];
	f->ROut2 = f->ROut1;
	f->ROut1 = ROut;

	// set output
	out[0] = LOut;
	out[1] = ROut;
}
