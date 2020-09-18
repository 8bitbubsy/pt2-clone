#include <stdint.h>
#include <math.h>
#include "pt2_rcfilter.h" // DENORMAL_OFFSET definition
#include "pt2_ledfilter.h"

/* Imperfect Amiga "LED" filter implementation. This may be further improved in the future.
** Based upon ideas posted by mystran @ the kvraudio.com forum.
**
** This filter may not function correctly used outside the fixed-cutoff context here!
*/

void clearLEDFilterState(ledFilter_t *filterLED)
{
	filterLED->buffer[0] = 0.0; // left channel
	filterLED->buffer[1] = 0.0;
	filterLED->buffer[2] = 0.0; // right channel
	filterLED->buffer[3] = 0.0;
}

static double sigmoid(double x, double coefficient)
{
	/* aciddose:
	** Coefficient from:
	**   0.0 to  inf (linear)
	**  -1.0 to -inf (linear)
	*/
	return x / (x + coefficient) * (coefficient + 1.0);
}

void calcLEDFilterCoeffs(const double sr, const double hz, const double fb, ledFilter_t *filter)
{
	/* aciddose:
	** tan() may produce NaN or other bad results in some cases!
	** It appears to work correctly with these specific coefficients.
	*/

	const double pi = 4.0 * atan(1.0); // M_PI can not be trusted

	const double c = (hz < (sr / 2.0)) ? tan((pi * hz) / sr) : 1.0;
	const double g = 1.0 / (1.0 + c);

	// aciddose: dirty compensation
	const double s = 0.5;
	const double t = 0.5;
	const double ic = c > t ? 1.0 / ((1.0 - s*t) + s*c) : 1.0;
	const double cg = c * g;
	const double fbg = 1.0 / (1.0 + fb * cg*cg);

	filter->c = c;
	filter->ci = g;
	filter->feedback = 2.0 * sigmoid(fb, 0.5);
	filter->bg = fbg * filter->feedback * ic;
	filter->cg = cg;
	filter->c2 = c * 2.0;
}

void LEDFilter(ledFilter_t *f, const double *in, double *out)
{
	const double in_1 = DENORMAL_OFFSET;
	const double in_2 = DENORMAL_OFFSET;

	const double c = f->c;
	const double g = f->ci;
	const double cg = f->cg;
	const double bg = f->bg;
	const double c2 = f->c2;

	double *v = f->buffer;

	// left channel
	const double estimate_L = in_2 + g*(v[1] + c*(in_1 + g*(v[0] + c*in[0])));
	const double y0_L = v[0]*g + in[0]*cg + in_1 + estimate_L * bg;
	const double y1_L = v[1]*g + y0_L*cg + in_2;

	v[0] += c2 * (in[0] - y0_L);
	v[1] += c2 * (y0_L - y1_L);
	out[0] = y1_L;

	// right channel
	const double estimate_R = in_2 + g*(v[3] + c*(in_1 + g*(v[2] + c*in[1])));
	const double y0_R = v[2]*g + in[1]*cg + in_1 + estimate_R * bg;
	const double y1_R = v[3]*g + y0_R*cg + in_2;

	v[2] += c2 * (in[1] - y0_R);
	v[3] += c2 * (y0_R - y1_R);
	out[1] = y1_R;
}
