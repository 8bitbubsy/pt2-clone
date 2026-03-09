// for finding memory leaks in debug mode with Visual Studio 
#if defined _DEBUG && defined _MSC_VER
#include <crtdbg.h>
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <math.h>
#include "pt2_helpers.h" // ABS()
#include "pt2_config.h"

// 2x downsamplers for main audio mixer

// 19-tap half-band FIR coefficients (sinc w/ cutoff=0.5, window = kaiser-bessel w/ beta=6.0)
static const double c0 =  0.5; // center point
static const double c1 =  0.30770457137782852852;
static const double c3 = -0.07765651545790620836;
static const double c5 =  0.02553750191646480053;
static const double c7 = -0.00628026647195643276;
static const double c9 =  0.00052603669344336991;

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

// 2x downsampler for sample loaders (no sample delay)

#define NUM_TAPS 19

// 19-tap half-band FIR coefficients (sinc w/ cutoff=0.5, window = kaiser-bessel w/ beta=6.0)
static const double coeffs[NUM_TAPS] =
{
	 0.0005260366934434,
	 0.0000000000000000,
	-0.0062802664719564,
	 0.0000000000000000,
	 0.0255375019164648,
	 0.0000000000000000,
	-0.0776565154579062,
	 0.0000000000000000,
	 0.3077045713778285,
	 0.5000000000000000,
	 0.3077045713778285,
	 0.0000000000000000,
	-0.0776565154579062,
	 0.0000000000000000,
	 0.0255375019164648,
	 0.0000000000000000,
	-0.0062802664719564,
	 0.0000000000000000,
	 0.0005260366934434
};

static double dDownsample2x(double *dSamples, int32_t offset, int32_t sampleLength)
{
	double dVal = 0.0;
	for (int32_t i = 0; i < NUM_TAPS; i++)
	{
		const int32_t tapOffset = offset + (i - ((NUM_TAPS/2)-1));

		double dSmp;
		if (tapOffset < 0 || tapOffset >= sampleLength)
			dSmp = 0.0;
		else
			dSmp = dSamples[tapOffset];

		dVal += dSmp * coeffs[i];
	}

	return dVal;
}

static float fDownsample2x(float *fSamples, int32_t offset, int32_t sampleLength)
{
	double dVal = 0.0f;
	for (int32_t i = 0; i < NUM_TAPS; i++)
	{
		const int32_t tapOffset = offset + (i - ((NUM_TAPS/2)-1));

		double dSmp;
		if (tapOffset < 0 || tapOffset >= sampleLength)
			dSmp = 0.0;
		else
			dSmp = fSamples[tapOffset];

		dVal += dSmp * coeffs[i];
	}

	return (float)dVal;
}

static double dDownsample2x_U8(uint8_t *samplesU8, int32_t offset, int32_t sampleLength)
{
	double dVal = 0.0;
	for (int32_t i = 0; i < NUM_TAPS; i++)
	{
		const int32_t tapOffset = offset + (i - ((NUM_TAPS/2)-1));

		double dSmp;
		if (tapOffset < 0 || tapOffset >= sampleLength)
			dSmp = 0.0;
		else
			dSmp = samplesU8[tapOffset];

		dVal += (dSmp - 128) * coeffs[i];
	}

	return dVal;
}

static double dDownsample2x_S8(int8_t *samplesS8, int32_t offset, int32_t sampleLength)
{
	double dVal = 0.0;
	for (int32_t i = 0; i < NUM_TAPS; i++)
	{
		const int32_t tapOffset = offset + (i - ((NUM_TAPS/2)-1));

		double dSmp;
		if (tapOffset < 0 || tapOffset >= sampleLength)
			dSmp = 0.0;
		else
			dSmp = samplesS8[tapOffset];

		dVal += dSmp * coeffs[i];
	}

	return dVal;
}

static double dDownsample2x_S16(int16_t *samplesS16, int32_t offset, int32_t sampleLength)
{
	double dVal = 0.0;
	for (int32_t i = 0; i < NUM_TAPS; i++)
	{
		const int32_t tapOffset = offset + (i - ((NUM_TAPS/2)-1));

		double dSmp;
		if (tapOffset < 0 || tapOffset >= sampleLength)
			dSmp = 0.0;
		else
			dSmp = samplesS16[tapOffset];

		dVal += dSmp * coeffs[i];
	}

	return dVal;
}

static double dDownsample2x_S32(int32_t *samplesS32, int32_t offset, int32_t sampleLength)
{
	double dVal = 0.0;
	for (int32_t i = 0; i < NUM_TAPS; i++)
	{
		const int32_t tapOffset = offset + (i - ((NUM_TAPS/2)-1));

		double dSmp;
		if (tapOffset < 0 || tapOffset >= sampleLength)
			dSmp = 0.0;
		else
			dSmp = samplesS32[tapOffset];

		dVal += dSmp * coeffs[i];
	}

	return dVal;
}

// Warning: These can exceed original range because of undershoot/overshoot!

void downsample2xDouble(double *buffer, uint32_t originalLength)
{
	int32_t offset = 0;
	for (uint32_t i = 0; i < originalLength / 2; i++, offset += 2)
		buffer[i] = dDownsample2x(buffer, offset, originalLength);
}

void downsample2xFloat(float *buffer, uint32_t originalLength)
{
	int32_t offset = 0;
	for (uint32_t i = 0; i < originalLength / 2; i++, offset += 2)
		buffer[i] = fDownsample2x(buffer, offset, originalLength);
}

// Warning: These are slow and use normalization to prevent clipping from undershoot/overshoot!

bool downsample2x8BitU(uint8_t *buffer, uint32_t originalLength)
{
	uint32_t newLength = originalLength / 2;
	if (newLength > (uint32_t)config.maxSampleLength)
		newLength = config.maxSampleLength;

	double *dBuffer = (double *)malloc(newLength * sizeof (double));
	if (dBuffer == NULL)
		return false;

	double dPeak = 0.0;

	int32_t offset = 0;
	for (uint32_t i = 0; i < newLength; i++, offset += 2)
	{
		dBuffer[i] = dDownsample2x_U8(buffer, offset, originalLength);

		const double dAbsSmp = ABS(dBuffer[i]);
		if (dAbsSmp > dPeak)
			dPeak = dAbsSmp;
	}

	// normalize

	double dAmp = 1.0;
	if (dPeak > 0.0)
		dAmp = INT8_MAX / dPeak;

	for (uint32_t i = 0; i < newLength; i++)
		buffer[i] = (uint8_t)round(dBuffer[i] * dAmp) + 128;

	free(dBuffer);
	return true;
}

bool downsample2x8Bit(int8_t *buffer, uint32_t originalLength)
{
	uint32_t newLength = originalLength / 2;
	if (newLength > (uint32_t)config.maxSampleLength)
		newLength = config.maxSampleLength;

	double *dBuffer = (double *)malloc(newLength * sizeof (double));
	if (dBuffer == NULL)
		return false;

	double dPeak = 0.0;

	int32_t offset = 0;
	for (uint32_t i = 0; i < newLength; i++, offset += 2)
	{
		dBuffer[i] = dDownsample2x_S8(buffer, offset, originalLength);

		const double dAbsSmp = ABS(dBuffer[i]);
		if (dAbsSmp > dPeak)
			dPeak = dAbsSmp;
	}

	// normalize

	double dAmp = 1.0;
	if (dPeak > 0.0)
		dAmp = INT8_MAX / dPeak;

	for (uint32_t i = 0; i < newLength; i++)
		buffer[i] = (int8_t)round(dBuffer[i] * dAmp);

	free(dBuffer);
	return true;
}

bool downsample2x16Bit(int16_t *buffer, uint32_t originalLength)
{
	uint32_t newLength = originalLength / 2;
	if (newLength > (uint32_t)config.maxSampleLength)
		newLength = config.maxSampleLength;

	double *dBuffer = (double *)malloc(newLength * sizeof (double));
	if (dBuffer == NULL)
		return false;

	double dPeak = 0.0;

	int32_t offset = 0;
	for (uint32_t i = 0; i < newLength; i++, offset += 2)
	{
		dBuffer[i] = dDownsample2x_S16(buffer, offset, originalLength);

		const double dAbsSmp = ABS(dBuffer[i]);
		if (dAbsSmp > dPeak)
			dPeak = dAbsSmp;
	}

	// normalize

	double dAmp = 1.0;
	if (dPeak > 0.0)
		dAmp = INT16_MAX / dPeak;

	for (uint32_t i = 0; i < newLength; i++)
		buffer[i] = (int16_t)round(dBuffer[i] * dAmp);

	free(dBuffer);
	return true;
}

bool downsample2x32Bit(int32_t *buffer, uint32_t originalLength)
{
	uint32_t newLength = originalLength / 2;
	if (newLength > (uint32_t)config.maxSampleLength)
		newLength = config.maxSampleLength;

	double *dBuffer = (double *)malloc(newLength * sizeof (double));
	if (dBuffer == NULL)
		return false;

	double dPeak = 0.0;

	int32_t offset = 0;
	for (uint32_t i = 0; i < newLength; i++, offset += 2)
	{
		dBuffer[i] = dDownsample2x_S32(buffer, offset, originalLength);

		const double dAbsSmp = ABS(dBuffer[i]);
		if (dAbsSmp > dPeak)
			dPeak = dAbsSmp;
	}

	// normalize

	double dAmp = 1.0;
	if (dPeak > 0.0)
		dAmp = INT32_MAX / dPeak;

	for (uint32_t i = 0; i < newLength; i++)
		buffer[i] = (int32_t)round(dBuffer[i] * dAmp);

	free(dBuffer);
	return true;
}
