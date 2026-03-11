#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <math.h>
#include "pt2_config.h" // config.maxSampleLength

/* Halfband FIR sinc filter notes:
** 1) Number of taps should be 4m+3 (3, 7, 11, 15, 19, ...) for correct symmetry
** 2) If using a Kaiser-Bessel window, a beta value of 4..6 is ideal
** 3) More than 59 taps is probably not worth it for this use case.
**    ProTracker is not THAT high fidelity!
*/

#define NUM_TAPS 59
#define CENTER_TAP ((NUM_TAPS - 1) / 2)

// halfband FIR coefficients (59 taps - sinc w/ cutoff=0.5, window = kaiser-bessel w/ beta=6.0)
#define C00  0.5
#define C01  0.317275338453959332429832330
#define C03 -0.103033217521460065957406016
#define C05  0.058655027161666546675622413
#define C07 -0.038686821707762795996554672
#define C09  0.027010251682923310662109984
#define C11 -0.019253555428127143434036128
#define C13  0.013744735388793834912624092
#define C15 -0.009701875724279194340704535
#define C17  0.006701484164885845168369016
#define C19 -0.004483856763479422517792994
#define C21  0.002871267468943607733405932
#define C23 -0.001730565633353817406661634
#define C25  0.000955363288980086585409912
#define C27 -0.000457530415141523959417225
#define C29  0.000163252766930700981672792

/* Code for generating Cxx coeff constants:
**
** double besselI0(double z)
** {
**     double s = 1.0, ds = 1.0, d = 2.0;
**     const double zz = z * z;
** 
**     do
**     {
**         ds *= zz / (d * d);
**         s += ds;
**         d += 2.0;
**     }
**     while (ds > s*(1E-15));
** 
**     return s;
** }
** 
** void printHalfbandCoeffs(int32_t numTaps, double kaiserBeta)
** {
**     printf("#define C00  0.5\n");
**     for (int32_t i = 0; i < 1 + (numTaps / 4); i++)
**     {
**         const double n = 1 + (i * 2);
** 
**         // Kaiser-Bessel window
**         const double kn = n / (double)((numTaps - 1) / 2);
**         const double window = besselI0(kaiserBeta * sqrt(1.0 - kn * kn)) / besselI0(kaiserBeta);
** 
**         const double x = ((n == 0.0) ? 0.5 : (sin(n * (PI * 0.5)) / (n * PI))) * window;
**         printf("#define C%02d ", (int32_t)n);
**         if (x >= 0.0) printf(" ");
**         printf("%.27f\n", x);
**     }
** }
**
** Then: printHalfbandCoeffs(59, 6.0);
*/

// ----------------------------------------------------------
// 2x downsampler for main audio mixer (simpler/faster, but has output sample delay)
// ----------------------------------------------------------

static double t01_L,t02_L,t03_L,t04_L,t05_L,t06_L,t07_L,t08_L,t09_L,t10_L,t11_L,t12_L,t13_L,t14_L;
static double t15_L,t16_L,t17_L,t18_L,t19_L,t20_L,t21_L,t22_L,t23_L,t24_L,t25_L,t26_L,t27_L,t28_L,t29_L;
static double t01_R,t02_R,t03_R,t04_R,t05_R,t06_R,t07_R,t08_R,t09_R,t10_R,t11_R,t12_R,t13_R,t14_R;
static double t15_R,t16_R,t17_R,t18_R,t19_R,t20_R,t21_R,t22_R,t23_R,t24_R,t25_R,t26_R,t27_R,t28_R,t29_R;

void clearDownsample2xStates(void)
{
	t01_L=t02_L=t03_L=t04_L=t05_L=t06_L=t07_L=t08_L=t09_L=t10_L=t11_L=t12_L=t13_L=t14_L=
	t15_L=t16_L=t17_L=t18_L=t19_L=t20_L=t21_L=t22_L=t23_L=t24_L=t25_L=t26_L=t27_L=t28_L=t29_L = 0.0;

	t01_R=t02_R=t03_R=t04_R=t05_R=t06_R=t07_R=t08_R=t09_R=t10_R=t11_R=t12_R=t13_R=t14_R=
	t15_R=t16_R=t17_R=t18_R=t19_R=t20_R=t21_R=t22_R=t23_R=t24_R=t25_R=t26_R=t27_R=t28_R=t29_R = 0.0;
}

double downsample2x_L(double sample1, double sample2)
{
	const double x00 = sample2 * C00, x01 = sample1 * C01;
	const double x03 = sample1 * C03, x05 = sample1 * C05;
	const double x07 = sample1 * C07, x09 = sample1 * C09;
	const double x11 = sample1 * C11, x13 = sample1 * C13;
	const double x15 = sample1 * C15, x17 = sample1 * C17;
	const double x19 = sample1 * C19, x21 = sample1 * C21;
	const double x23 = sample1 * C23, x25 = sample1 * C25;
	const double x27 = sample1 * C27, x29 = sample1 * C29;

	const double out = t29_L + x29;

	t29_L = t28_L + x27;
	t28_L = t27_L + x25;
	t27_L = t26_L + x23;
	t26_L = t25_L + x21;
	t25_L = t24_L + x19;
	t24_L = t23_L + x17;
	t23_L = t22_L + x15;
	t22_L = t21_L + x13;
	t21_L = t20_L + x11;
	t20_L = t19_L + x09;
	t19_L = t18_L + x07;
	t18_L = t17_L + x05;
	t17_L = t16_L + x03;
	t16_L = t15_L + x01;
	t15_L = t14_L + x01 + x00;
	t14_L = t13_L + x03;
	t13_L = t12_L + x05;
	t12_L = t11_L + x07;
	t11_L = t10_L + x09;
	t10_L = t09_L + x11;
	t09_L = t08_L + x13;
	t08_L = t07_L + x15;
	t07_L = t06_L + x17;
	t06_L = t05_L + x19;
	t05_L = t04_L + x21;
	t04_L = t03_L + x23;
	t03_L = t02_L + x25;
	t02_L = t01_L + x27;
	t01_L =         x29;

	return out;
}

double downsample2x_R(double sample1, double sample2)
{
	const double x00 = sample2 * C00, x01 = sample1 * C01;
	const double x03 = sample1 * C03, x05 = sample1 * C05;
	const double x07 = sample1 * C07, x09 = sample1 * C09;
	const double x11 = sample1 * C11, x13 = sample1 * C13;
	const double x15 = sample1 * C15, x17 = sample1 * C17;
	const double x19 = sample1 * C19, x21 = sample1 * C21;
	const double x23 = sample1 * C23, x25 = sample1 * C25;
	const double x27 = sample1 * C27, x29 = sample1 * C29;

	const double out = t29_R + x29;

	t29_R = t28_R + x27;
	t28_R = t27_R + x25;
	t27_R = t26_R + x23;
	t26_R = t25_R + x21;
	t25_R = t24_R + x19;
	t24_R = t23_R + x17;
	t23_R = t22_R + x15;
	t22_R = t21_R + x13;
	t21_R = t20_R + x11;
	t20_R = t19_R + x09;
	t19_R = t18_R + x07;
	t18_R = t17_R + x05;
	t17_R = t16_R + x03;
	t16_R = t15_R + x01;
	t15_R = t14_R + x01 + x00;
	t14_R = t13_R + x03;
	t13_R = t12_R + x05;
	t12_R = t11_R + x07;
	t11_R = t10_R + x09;
	t10_R = t09_R + x11;
	t09_R = t08_R + x13;
	t08_R = t07_R + x15;
	t07_R = t06_R + x17;
	t06_R = t05_R + x19;
	t05_R = t04_R + x21;
	t04_R = t03_R + x23;
	t03_R = t02_R + x25;
	t02_R = t01_R + x27;
	t01_R =         x29;

	return out;
}

// ----------------------------------------------------------
// 2x downsamplers for sample loaders
// ----------------------------------------------------------

static const double halfbandSincKernel[NUM_TAPS] =
{
	C29, 0.0,
	C27, 0.0,
	C25, 0.0,
	C23, 0.0,
	C21, 0.0,
	C19, 0.0,
	C17, 0.0,
	C15, 0.0,
	C13, 0.0,
	C11, 0.0,
	C09, 0.0,
	C07, 0.0,
	C05, 0.0,
	C03, 0.0,
	C01, C00,
	C01, 0.0,
	C03, 0.0,
	C05, 0.0,
	C07, 0.0,
	C09, 0.0,
	C11, 0.0,
	C13, 0.0,
	C15, 0.0,
	C17, 0.0,
	C19, 0.0,
	C21, 0.0,
	C23, 0.0,
	C25, 0.0,
	C27, 0.0,
	C29
};

static double dDownsample2x(double *dSamples, int32_t offset, int32_t sampleLength)
{
	double dVal = 0.0;
	for (int32_t i = 0; i < NUM_TAPS; i++)
	{
		const int32_t tapOffset = offset + (i - CENTER_TAP);

		double dSmp;
		if (tapOffset < 0)
			dSmp = dSamples[0];
		else if (tapOffset >= sampleLength)
			dSmp = dSamples[sampleLength-1];
		else
			dSmp = dSamples[tapOffset];

		dVal += dSmp * halfbandSincKernel[i];
	}

	return dVal;
}

static float fDownsample2x(float *fSamples, int32_t offset, int32_t sampleLength)
{
	double dVal = 0.0f;
	for (int32_t i = 0; i < NUM_TAPS; i++)
	{
		const int32_t tapOffset = offset + (i - CENTER_TAP);

		double dSmp;
		if (tapOffset < 0)
			dSmp = fSamples[0];
		else if (tapOffset >= sampleLength)
			dSmp = fSamples[sampleLength-1];
		else
			dSmp = fSamples[tapOffset];

		dVal += dSmp * halfbandSincKernel[i];
	}

	return (float)dVal;
}

static double dDownsample2x_U8(uint8_t *samplesU8, int32_t offset, int32_t sampleLength)
{
	double dVal = 0.0;
	for (int32_t i = 0; i < NUM_TAPS; i++)
	{
		const int32_t tapOffset = offset + (i - CENTER_TAP);

		double dSmp;
		if (tapOffset < 0)
			dSmp = samplesU8[0];
		else if (tapOffset >= sampleLength)
			dSmp = samplesU8[sampleLength-1];
		else
			dSmp = samplesU8[tapOffset];

		dVal += (dSmp - 128) * halfbandSincKernel[i];
	}

	return dVal;
}

static double dDownsample2x_S8(int8_t *samplesS8, int32_t offset, int32_t sampleLength)
{
	double dVal = 0.0;
	for (int32_t i = 0; i < NUM_TAPS; i++)
	{
		const int32_t tapOffset = offset + (i - CENTER_TAP);

		double dSmp;
		if (tapOffset < 0)
			dSmp = samplesS8[0];
		else if (tapOffset >= sampleLength)
			dSmp = samplesS8[sampleLength-1];
		else
			dSmp = samplesS8[tapOffset];

		dVal += dSmp * halfbandSincKernel[i];
	}

	return dVal;
}

static double dDownsample2x_S16(int16_t *samplesS16, int32_t offset, int32_t sampleLength)
{
	double dVal = 0.0;
	for (int32_t i = 0; i < NUM_TAPS; i++)
	{
		const int32_t tapOffset = offset + (i - CENTER_TAP);

		double dSmp;
		if (tapOffset < 0)
			dSmp = samplesS16[0];
		else if (tapOffset >= sampleLength)
			dSmp = samplesS16[sampleLength-1];
		else
			dSmp = samplesS16[tapOffset];

		dVal += dSmp * halfbandSincKernel[i];
	}

	return dVal;
}

static double dDownsample2x_S32(int32_t *samplesS32, int32_t offset, int32_t sampleLength)
{
	double dVal = 0.0;
	for (int32_t i = 0; i < NUM_TAPS; i++)
	{
		const int32_t tapOffset = offset + (i - CENTER_TAP);

		double dSmp;
		if (tapOffset < 0)
			dSmp = samplesS32[0];
		else if (tapOffset >= sampleLength)
			dSmp = samplesS32[sampleLength-1];
		else
			dSmp = samplesS32[tapOffset];

		dVal += dSmp * halfbandSincKernel[i];
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

		const double dAbsSmp = fabs(dBuffer[i]);
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

		const double dAbsSmp = fabs(dBuffer[i]);
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

		const double dAbsSmp = fabs(dBuffer[i]);
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

		const double dAbsSmp = fabs(dBuffer[i]);
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
