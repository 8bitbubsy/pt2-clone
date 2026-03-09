// for finding memory leaks in debug mode with Visual Studio 
#if defined _DEBUG && defined _MSC_VER
#include <crtdbg.h>
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <math.h>
#include "pt2_helpers.h" // ABS()

// 19-tap half-band FIR coefficients (sinc w/ cutoff=0.5, window = kaiser-bessel w/ beta=6.0)
static const double c0 =  0.5; // center point
static const double c1 =  0.30770457137782852852;
static const double c3 = -0.07765651545790620836;
static const double c5 =  0.02553750191646480053;
static const double c7 = -0.00628026647195643276;
static const double c9 =  0.00052603669344336991;

// ----------------------------------------------------------
// reserved for main audio channel mixer, PAT2SMP and MOD2WAV
// ----------------------------------------------------------

static double tmp1_L, tmp2_L, tmp3_L, tmp4_L, tmp5_L, tmp6_L, tmp7_L, tmp8_L, tmp9_L;
static double tmp1_R, tmp2_R, tmp3_R, tmp4_R, tmp5_R, tmp6_R, tmp7_R, tmp8_R, tmp9_R;

void clearMixerDownsamplerStates(void)
{
	tmp1_L = tmp2_L = tmp3_L = tmp4_L = tmp5_L = tmp6_L = tmp7_L = tmp8_L = tmp9_L = 0.0;
	tmp1_R = tmp2_R = tmp3_R = tmp4_R = tmp5_R = tmp6_R = tmp7_R = tmp8_R = tmp9_R = 0.0;
}

double decimate2x_L(double s1, double s2)
{
	const double x0 = s2 * c0;
	const double x1 = s1 * c1;
	const double x3 = s1 * c3;
	const double x5 = s1 * c5;
	const double x7 = s1 * c7;
	const double x9 = s1 * c9;

	const double out = tmp9_L + x9;

	tmp9_L = tmp8_L + x7;
	tmp8_L = tmp7_L + x5;
	tmp7_L = tmp6_L + x3;
	tmp6_L = tmp5_L + x1;
	tmp5_L = tmp4_L + x1 + x0;
	tmp4_L = tmp3_L + x3;
	tmp3_L = tmp2_L + x5;
	tmp2_L = tmp1_L + x7;
	tmp1_L =          x9;

	return out;
}

double decimate2x_R(double s1, double s2)
{
	const double x0 = s2 * c0;
	const double x1 = s1 * c1;
	const double x3 = s1 * c3;
	const double x5 = s1 * c5;
	const double x7 = s1 * c7;
	const double x9 = s1 * c9;

	const double out = tmp9_R + x9;

	tmp9_R = tmp8_R + x7;
	tmp8_R = tmp7_R + x5;
	tmp7_R = tmp6_R + x3;
	tmp6_R = tmp5_R + x1;
	tmp5_R = tmp4_R + x1 + x0;
	tmp4_R = tmp3_R + x3;
	tmp3_R = tmp2_R + x5;
	tmp2_R = tmp1_R + x7;
	tmp1_R =          x9;

	return out;
}

// ----------------------------------------------------------
// ----------------------------------------------------------
// ----------------------------------------------------------

static double tmp1, tmp2, tmp3, tmp4, tmp5, tmp6, tmp7, tmp8, tmp9;

static void clearDownsamplerState(void)
{
	tmp1 = tmp2 = tmp3 = tmp4 = tmp5 = tmp6 = tmp7 = tmp8 = tmp9 = 0.0;
}

static double decimate2x(double s1, double s2)
{
	const double x0 = s2 * c0;
	const double x1 = s1 * c1;
	const double x3 = s1 * c3;
	const double x5 = s1 * c5;
	const double x7 = s1 * c7;
	const double x9 = s1 * c9;

	const double out = tmp9 + x9;

	tmp9 = tmp8 + x7;
	tmp8 = tmp7 + x5;
	tmp7 = tmp6 + x3;
	tmp6 = tmp5 + x1;
	tmp5 = tmp4 + x1 + x0;
	tmp4 = tmp3 + x3;
	tmp3 = tmp2 + x5;
	tmp2 = tmp1 + x7;
	tmp1 =        x9;

	return out;
}

// Warning: These can exceed original range because of undershoot/overshoot!

void downsample2xDouble(double *buffer, uint32_t originalLength)
{
	clearDownsamplerState();

	const double *input = buffer;
	const uint32_t length = originalLength / 2;

	for (uint32_t i = 0; i < length; i++, input += 2)
		buffer[i] = decimate2x(input[0], input[1]);
}

void downsample2xFloat(float *buffer, uint32_t originalLength)
{
	clearDownsamplerState();

	const float *input = buffer;
	const uint32_t length = originalLength / 2;

	for (uint32_t i = 0; i < length; i++, input += 2)
		buffer[i] = (float)decimate2x(input[0], input[1]);
}

// Warning: These are slow and use normalization to prevent clipping from undershoot/overshoot!

bool downsample2x8BitU(uint8_t *buffer, uint32_t originalLength)
{
	double *dBuffer = (double *)malloc(originalLength * sizeof (double));
	if (dBuffer == NULL)
		return false;

	for (uint32_t i = 0; i < originalLength; i++)
		dBuffer[i] = (buffer[i] - 128) * (1.0 / (INT8_MAX+1.0));

	const double *input = dBuffer;
	double dPeak = 0.0;

	clearDownsamplerState();
	const uint32_t length = originalLength / 2;
	for (uint32_t i = 0; i < length; i++, input += 2)
	{
		double dOut = decimate2x(input[0], input[1]);
		dBuffer[i] = dOut;

		dOut = ABS(dOut);
		if (dOut > dPeak)
			dPeak = dOut;
	}

	// normalize

	double dAmp = 1.0;
	if (dPeak > 0.0)
		dAmp = INT8_MAX / dPeak;

	for (uint32_t i = 0; i < length; i++)
		buffer[i] = (uint8_t)round(dBuffer[i] * dAmp) + 128;

	free(dBuffer);
	return true;
}

bool downsample2x8Bit(int8_t *buffer, uint32_t originalLength)
{
	double *dBuffer = (double *)malloc(originalLength * sizeof (double));
	if (dBuffer == NULL)
		return false;

	for (uint32_t i = 0; i < originalLength; i++)
		dBuffer[i] = buffer[i] * (1.0 / (INT8_MAX+1.0));

	const double *input = dBuffer;
	double dPeak = 0.0;

	clearDownsamplerState();
	const uint32_t length = originalLength / 2;
	for (uint32_t i = 0; i < length; i++, input += 2)
	{
		double dOut = decimate2x(input[0], input[1]);
		dBuffer[i] = dOut;

		dOut = ABS(dOut);
		if (dOut > dPeak)
			dPeak = dOut;
	}

	// normalize

	double dAmp = 1.0;
	if (dPeak > 0.0)
		dAmp = INT8_MAX / dPeak;

	for (uint32_t i = 0; i < length; i++)
		buffer[i] = (int8_t)round(dBuffer[i] * dAmp);

	free(dBuffer);
	return true;
}

bool downsample2x16Bit(int16_t *buffer, uint32_t originalLength)
{
	double *dBuffer = (double *)malloc(originalLength * sizeof (double));
	if (dBuffer == NULL)
		return false;

	for (uint32_t i = 0; i < originalLength; i++)
		dBuffer[i] = buffer[i] * (1.0 / (INT16_MAX+1.0));

	const double *input = dBuffer;
	double dPeak = 0.0;

	clearDownsamplerState();
	const uint32_t length = originalLength / 2;
	for (uint32_t i = 0; i < length; i++, input += 2)
	{
		double dOut = decimate2x(input[0], input[1]);
		dBuffer[i] = dOut;

		dOut = ABS(dOut);
		if (dOut > dPeak)
			dPeak = dOut;
	}

	// normalize

	double dAmp = 1.0;
	if (dPeak > 0.0)
		dAmp = INT16_MAX / dPeak;

	for (uint32_t i = 0; i < length; i++)
		buffer[i] = (int16_t)round(dBuffer[i] * dAmp);

	free(dBuffer);
	return true;
}

bool downsample2x32Bit(int32_t *buffer, uint32_t originalLength)
{
	double *dBuffer = (double *)malloc(originalLength * sizeof (double));
	if (dBuffer == NULL)
		return false;

	for (uint32_t i = 0; i < originalLength; i++)
		dBuffer[i] = buffer[i] * (1.0 / (INT32_MAX+1.0));

	const double *input = dBuffer;
	double dPeak = 0.0;

	clearDownsamplerState();
	const uint32_t length = originalLength / 2;
	for (uint32_t i = 0; i < length; i++, input += 2)
	{
		double dOut = decimate2x(input[0], input[1]);
		dBuffer[i] = dOut;

		dOut = ABS(dOut);
		if (dOut > dPeak)
			dPeak = dOut;
	}

	// normalize

	double dAmp = 1.0;
	if (dPeak > 0.0)
		dAmp = INT32_MAX / dPeak;

	for (uint32_t i = 0; i < length; i++)
		buffer[i] = (int32_t)round(dBuffer[i] * dAmp);

	free(dBuffer);
	return true;
}
