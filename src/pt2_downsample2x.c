#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <math.h>
#include "pt2_config.h" // config.maxSampleLength

#define NUM_TAPS 59 /* should be 4m+3 (3, 7, 11, 15, 19, ...) */
#define CENTER_TAP ((NUM_TAPS - 1) / 2)

// half-band FIR coeffs (Remez algorithm - numtaps=59, bands=[0.0, 0.2, 0.3, 0.5], desired=[1.0, 0.0])
#define C00  0.500000000000001776356839400
#define C01  0.316796099629279681586524475
#define C03 -0.101638770668561695398324218
#define C05  0.056469397591722876594833025
#define C07 -0.035898691728282271229399925
#define C09  0.023848934428624003062369141
#define C11 -0.015961026468464808297786917
#define C13  0.010547947951963959276056038
#define C15 -0.006789354746338562181240395
#define C17  0.004207318621831869671912063
#define C19 -0.002480664366371574183767201
#define C21  0.001372073862198802066819647
#define C23 -0.000698236372446042839051694
#define C25  0.000317104911171300647004800
#define C27 -0.000121433207895608810135933
#define C29  0.000035018885257113032771856

/* Python code for generating the Cxx coefficient constants:
**
** import scipy
**
** numtaps = 59 # should be 4m+3 (3, 7, 11, 15, 19, ...)
** bands = [0.0, 0.2, 0.3, 0.5]
** desired = [1.0, 0.0]
** h = scipy.signal.remez(numtaps, bands, desired)
**
** print('#define C00 % .27f' % h[(numtaps-1)//2])
** for i in range(1+(numtaps//4)):
**     print('#define C%02d % .27f' % (1+(i*2), h[((numtaps-1)//2)+1+(i*2)]))
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

static const double halfbandKernel[NUM_TAPS] =
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

		dVal += dSmp * halfbandKernel[i];
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

		dVal += dSmp * halfbandKernel[i];
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

		dVal += (dSmp - 128) * halfbandKernel[i];
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

		dVal += dSmp * halfbandKernel[i];
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

		dVal += dSmp * halfbandKernel[i];
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

		dVal += dSmp * halfbandKernel[i];
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
