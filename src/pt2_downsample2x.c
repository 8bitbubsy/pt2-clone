#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <math.h>
#include "pt2_config.h" // config.maxSampleLength

#define NUM_TAPS 59 /* should be 4m+3 (3, 7, 11, 15, 19, ...) */
#define CENTER_TAP ((NUM_TAPS - 1) / 2)

// half-band FIR coeffs (Remez algorithm - numtaps=59, bands=[0.0, 0.2, 0.3, 0.5], desired=[1.0, 0.0])
#define C00  0.50000000000000f
#define C01  0.31679609962928f
#define C03 -0.10163877066856f
#define C05  0.05646939759172f
#define C07 -0.03589869172828f
#define C09  0.02384893442862f
#define C11 -0.01596102646846f
#define C13  0.01054794795196f
#define C15 -0.00678935474634f
#define C17  0.00420731862183f
#define C19 -0.00248066436637f
#define C21  0.00137207386220f
#define C23 -0.00069823637245f
#define C25  0.00031710491117f
#define C27 -0.00012143320790f
#define C29  0.00003501888526f

/* Python code for generating the Cxx coefficient constants:
**
** import scipy
**
** numtaps = 59 # should be 4m+3 (3, 7, 11, 15, 19, ...)
** bands = [0.0, 0.2, 0.3, 0.5]
** desired = [1.0, 0.0]
** h = scipy.signal.remez(numtaps, bands, desired)
**
** print('#define C00 % .14ff' % h[(numtaps-1)//2])
** for i in range(1+(numtaps//4)):
**     print('#define C%02d % .14ff' % (1+(i*2), h[((numtaps-1)//2)+1+(i*2)]))
*/

// ----------------------------------------------------------
// 2x downsampler for main audio mixer (simpler/faster, but has output sample delay)
// ----------------------------------------------------------

static float t01_L,t02_L,t03_L,t04_L,t05_L,t06_L,t07_L,t08_L,t09_L,t10_L,t11_L,t12_L,t13_L,t14_L;
static float t15_L,t16_L,t17_L,t18_L,t19_L,t20_L,t21_L,t22_L,t23_L,t24_L,t25_L,t26_L,t27_L,t28_L,t29_L;
static float t01_R,t02_R,t03_R,t04_R,t05_R,t06_R,t07_R,t08_R,t09_R,t10_R,t11_R,t12_R,t13_R,t14_R;
static float t15_R,t16_R,t17_R,t18_R,t19_R,t20_R,t21_R,t22_R,t23_R,t24_R,t25_R,t26_R,t27_R,t28_R,t29_R;

void clearDownsample2xStates(void)
{
	t01_L=t02_L=t03_L=t04_L=t05_L=t06_L=t07_L=t08_L=t09_L=t10_L=t11_L=t12_L=t13_L=t14_L=
	t15_L=t16_L=t17_L=t18_L=t19_L=t20_L=t21_L=t22_L=t23_L=t24_L=t25_L=t26_L=t27_L=t28_L=t29_L = 0.0f;

	t01_R=t02_R=t03_R=t04_R=t05_R=t06_R=t07_R=t08_R=t09_R=t10_R=t11_R=t12_R=t13_R=t14_R=
	t15_R=t16_R=t17_R=t18_R=t19_R=t20_R=t21_R=t22_R=t23_R=t24_R=t25_R=t26_R=t27_R=t28_R=t29_R = 0.0f;
}

float downsample2x_L(float sample1, float sample2)
{
	const float x00 = sample2 * C00, x01 = sample1 * C01;
	const float x03 = sample1 * C03, x05 = sample1 * C05;
	const float x07 = sample1 * C07, x09 = sample1 * C09;
	const float x11 = sample1 * C11, x13 = sample1 * C13;
	const float x15 = sample1 * C15, x17 = sample1 * C17;
	const float x19 = sample1 * C19, x21 = sample1 * C21;
	const float x23 = sample1 * C23, x25 = sample1 * C25;
	const float x27 = sample1 * C27, x29 = sample1 * C29;

	const float out = t29_L + x29;

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

float downsample2x_R(float sample1, float sample2)
{
	const float x00 = sample2 * C00, x01 = sample1 * C01;
	const float x03 = sample1 * C03, x05 = sample1 * C05;
	const float x07 = sample1 * C07, x09 = sample1 * C09;
	const float x11 = sample1 * C11, x13 = sample1 * C13;
	const float x15 = sample1 * C15, x17 = sample1 * C17;
	const float x19 = sample1 * C19, x21 = sample1 * C21;
	const float x23 = sample1 * C23, x25 = sample1 * C25;
	const float x27 = sample1 * C27, x29 = sample1 * C29;

	const float out = t29_R + x29;

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

static const float fHalfbandKernel[NUM_TAPS] =
{
	C29, 0.0f,
	C27, 0.0f,
	C25, 0.0f,
	C23, 0.0f,
	C21, 0.0f,
	C19, 0.0f,
	C17, 0.0f,
	C15, 0.0f,
	C13, 0.0f,
	C11, 0.0f,
	C09, 0.0f,
	C07, 0.0f,
	C05, 0.0f,
	C03, 0.0f,
	C01, C00,
	C01, 0.0f,
	C03, 0.0f,
	C05, 0.0f,
	C07, 0.0f,
	C09, 0.0f,
	C11, 0.0f,
	C13, 0.0f,
	C15, 0.0f,
	C17, 0.0f,
	C19, 0.0f,
	C21, 0.0f,
	C23, 0.0f,
	C25, 0.0f,
	C27, 0.0f,
	C29
};

static double dDownsample2x(double *fSamples, int32_t offset, int32_t sampleLength)
{
	double dVal = 0.0;
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

		dVal += dSmp * (double)fHalfbandKernel[i];
	}

	return dVal;
}

static float fDownsample2x(float *fSamples, int32_t offset, int32_t sampleLength)
{
	float fVal = 0.0f;
	for (int32_t i = 0; i < NUM_TAPS; i++)
	{
		const int32_t tapOffset = offset + (i - CENTER_TAP);

		float fSmp;
		if (tapOffset < 0)
			fSmp = fSamples[0];
		else if (tapOffset >= sampleLength)
			fSmp = fSamples[sampleLength-1];
		else
			fSmp = fSamples[tapOffset];

		fVal += fSmp * fHalfbandKernel[i];
	}

	return fVal;
}

static float fDownsample2x_U8(uint8_t *samplesU8, int32_t offset, int32_t sampleLength)
{
	float fVal = 0.0f;
	for (int32_t i = 0; i < NUM_TAPS; i++)
	{
		const int32_t tapOffset = offset + (i - CENTER_TAP);

		float fSmp;
		if (tapOffset < 0)
			fSmp = samplesU8[0];
		else if (tapOffset >= sampleLength)
			fSmp = samplesU8[sampleLength-1];
		else
			fSmp = samplesU8[tapOffset];

		fVal += (fSmp - 128.0f) * fHalfbandKernel[i];
	}

	return fVal;
}

static float fDownsample2x_S8(int8_t *samplesS8, int32_t offset, int32_t sampleLength)
{
	float fVal = 0.0f;
	for (int32_t i = 0; i < NUM_TAPS; i++)
	{
		const int32_t tapOffset = offset + (i - CENTER_TAP);

		float fSmp;
		if (tapOffset < 0)
			fSmp = samplesS8[0];
		else if (tapOffset >= sampleLength)
			fSmp = samplesS8[sampleLength-1];
		else
			fSmp = samplesS8[tapOffset];

		fVal += fSmp * fHalfbandKernel[i];
	}

	return fVal;
}

static float fDownsample2x_S16(int16_t *samplesS16, int32_t offset, int32_t sampleLength)
{
	float fVal = 0.0f;
	for (int32_t i = 0; i < NUM_TAPS; i++)
	{
		const int32_t tapOffset = offset + (i - CENTER_TAP);

		float fSmp;
		if (tapOffset < 0)
			fSmp = samplesS16[0];
		else if (tapOffset >= sampleLength)
			fSmp = samplesS16[sampleLength-1];
		else
			fSmp = samplesS16[tapOffset];

		fVal += fSmp * fHalfbandKernel[i];
	}

	return fVal;
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

		dVal += dSmp * (double)fHalfbandKernel[i];
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

	float *fBuffer = (float *)malloc(newLength * sizeof (float));
	if (fBuffer == NULL)
		return false;

	float fPeak = 0.0f;

	int32_t offset = 0;
	for (uint32_t i = 0; i < newLength; i++, offset += 2)
	{
		fBuffer[i] = fDownsample2x_U8(buffer, offset, originalLength);

		const float fAbsSmp = fabsf(fBuffer[i]);
		if (fAbsSmp > fPeak)
			fPeak = fAbsSmp;
	}

	// normalize

	float fAmp = 1.0f;
	if (fPeak > 0.0f)
		fAmp = INT8_MAX / fPeak;

	for (uint32_t i = 0; i < newLength; i++)
		buffer[i] = (uint8_t)roundf(fBuffer[i] * fAmp) + 128;

	free(fBuffer);
	return true;
}

bool downsample2x8Bit(int8_t *buffer, uint32_t originalLength)
{
	uint32_t newLength = originalLength / 2;
	if (newLength > (uint32_t)config.maxSampleLength)
		newLength = config.maxSampleLength;

	float *fBuffer = (float *)malloc(newLength * sizeof (float));
	if (fBuffer == NULL)
		return false;

	float fPeak = 0.0f;

	int32_t offset = 0;
	for (uint32_t i = 0; i < newLength; i++, offset += 2)
	{
		fBuffer[i] = fDownsample2x_S8(buffer, offset, originalLength);

		const float fAbsSmp = fabsf(fBuffer[i]);
		if (fAbsSmp > fPeak)
			fPeak = fAbsSmp;
	}

	// normalize

	float fAmp = 1.0f;
	if (fPeak > 0.0f)
		fAmp = INT8_MAX / fPeak;

	for (uint32_t i = 0; i < newLength; i++)
		buffer[i] = (int8_t)roundf(fBuffer[i] * fAmp);

	free(fBuffer);
	return true;
}

bool downsample2x16Bit(int16_t *buffer, uint32_t originalLength)
{
	uint32_t newLength = originalLength / 2;

	if (newLength > (uint32_t)config.maxSampleLength)
		newLength = config.maxSampleLength;

	float *fBuffer = (float *)malloc(newLength * sizeof (float));
	if (fBuffer == NULL)
		return false;

	float fPeak = 0.0f;

	int32_t offset = 0;
	for (uint32_t i = 0; i < newLength; i++, offset += 2)
	{
		fBuffer[i] = fDownsample2x_S16(buffer, offset, originalLength);

		const float fAbsSmp = fabsf(fBuffer[i]);
		if (fAbsSmp > fPeak)
			fPeak = fAbsSmp;
	}

	// normalize

	float fAmp = 1.0f;
	if (fPeak > 0.0f)
		fAmp = INT16_MAX / fPeak;

	for (uint32_t i = 0; i < newLength; i++)
		buffer[i] = (int16_t)roundf(fBuffer[i] * fAmp);

	free(fBuffer);
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
