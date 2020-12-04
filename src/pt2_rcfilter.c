// 1-pole 6dB/oct RC filters, code by aciddose (I think?)


#include <stdint.h>
#include <math.h>
#include "pt2_rcfilter.h"

void calcRCFilterCoeffs(double dSr, double dHz, rcFilter_t *f)
{
	const double pi = 4.0 * atan(1.0); // M_PI can not be trusted

	const double c = (dHz < (dSr / 2.0)) ? tan((pi * dHz) / dSr) : 1.0;
	f->c = c;
	f->c2 = f->c * 2.0;
	f->g = 1.0 / (1.0 + f->c);
	f->cg = f->c * f->g;
}

void clearRCFilterState(rcFilter_t *f)
{
	f->buffer[0] = 0.0; // left channel
	f->buffer[1] = 0.0; // right channel
}

// aciddose: input 0 is resistor side of capacitor (low-pass), input 1 is reference side (high-pass)
static inline double getLowpassOutput(rcFilter_t *f, const double input_0, const double input_1, const double buffer)
{
	double dOutput = DENORMAL_OFFSET;

	dOutput += buffer * f->g + input_0 * f->cg + input_1 * (1.0 - f->cg);

	return dOutput;
}

void RCLowPassFilterStereo(rcFilter_t *f, const double *in, double *out)
{
	double output;

	// left channel RC low-pass
	output = getLowpassOutput(f, in[0], 0.0, f->buffer[0]);
	f->buffer[0] += (in[0] - output) * f->c2;
	out[0] = output;

	// right channel RC low-pass
	output = getLowpassOutput(f, in[1], 0.0, f->buffer[1]);
	f->buffer[1] += (in[1] - output) * f->c2;
	out[1] = output;
}

void RCHighPassFilterStereo(rcFilter_t *f, const double *in, double *out)
{
	double low[2];

	RCLowPassFilterStereo(f, in, low);

	out[0] = in[0] - low[0]; // left channel high-pass
	out[1] = in[1] - low[1]; // right channel high-pass
}

void RCLowPassFilter(rcFilter_t *f, const double in, double *out)
{
	double output = getLowpassOutput(f, in, 0.0, f->buffer[0]);
	f->buffer[0] += (in - output) * f->c2;
	*out = output;
}

void RCHighPassFilter(rcFilter_t *f, const double in, double *out)
{
	double low;

	RCLowPassFilter(f, in, &low);
	*out = in - low; // high-pass
}
