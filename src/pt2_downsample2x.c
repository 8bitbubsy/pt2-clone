// for finding memory leaks in debug mode with Visual Studio 
#if defined _DEBUG && defined _MSC_VER
#include <crtdbg.h>
#endif

#include <stdint.h>
#include <stdbool.h>
#include "pt2_helpers.h" // CLAMP

static double state[2];

/*
** - all-pass halfband filters (2x downsample) -
**
** 8bitbubsy: Not sure who coded these. Possibly aciddose,
** or maybe he found it on the internet somewhere...
*/

static double f(const double in, double *b, const double c)
{
	const double x = (in - *b) * c;
	const double out = *b + x;
	*b = in + x;

	return out;
}

static double d2x(const double *input, double *b)
{
	return (f(input[0], &b[0], 0.150634765625) + f(input[1], &b[1], -0.3925628662109375)) * 0.5;
}

// Warning: These can exceed original range because of undershoot/overshoot!

void downsample2xDouble(double *buffer, int32_t originalLength)
{
	state[0] = state[1] = 0.0;

	const double *input = buffer;
	const int32_t length = originalLength / 2;
	for (int32_t i = 0; i < length; i++, input += 2)
		buffer[i] = d2x(input, state);
}

void downsample2xFloat(float *buffer, int32_t originalLength)
{
	double in[2];

	state[0] = state[1] = 0.0;

	const float *input = buffer;
	const int32_t length = originalLength / 2;
	for (int32_t i = 0; i < length; i++, input += 2)
	{
		in[0] = input[0];
		in[1] = input[1];

		buffer[i] = (float)d2x(in, state);
	}
}

// Warning: These are slow and use normalization to prevent clipping from undershoot/overshoot!

bool downsample2x8BitU(uint8_t *buffer, int32_t originalLength)
{
	state[0] = state[1] = 0.0;

	double *dBuffer = (double *)malloc(originalLength * sizeof (double));
	if (dBuffer == NULL)
		return false;

	for (int32_t i = 0; i < originalLength; i++)
		dBuffer[i] = (buffer[i] - 128) * (1.0 / (INT8_MAX+1.0));

	const double *input = dBuffer;
	double dPeak = 0.0;

	const int32_t length = originalLength / 2;
	for (int32_t i = 0; i < length; i++, input += 2)
	{
		double dOut = d2x(input, state);
		dBuffer[i] = dOut;

		dOut = ABS(dOut);
		if (dOut > dPeak)
			dPeak = dOut;
	}

	// normalize

	double dAmp = 1.0;
	if (dPeak > 0.0)
		dAmp = INT8_MAX / dPeak;

	for (int32_t i = 0; i < length; i++)
		buffer[i] = (uint8_t)round(dBuffer[i] * dAmp) + 128;

	free(dBuffer);

	return true;
}

bool downsample2x8Bit(int8_t *buffer, int32_t originalLength)
{
	state[0] = state[1] = 0.0;

	double *dBuffer = (double *)malloc(originalLength * sizeof (double));
	if (dBuffer == NULL)
		return false;

	for (int32_t i = 0; i < originalLength; i++)
		dBuffer[i] = buffer[i] * (1.0 / (INT8_MAX+1.0));

	const double *input = dBuffer;
	double dPeak = 0.0;

	const int32_t length = originalLength / 2;
	for (int32_t i = 0; i < length; i++, input += 2)
	{
		double dOut = d2x(input, state);
		dBuffer[i] = dOut;

		dOut = ABS(dOut);
		if (dOut > dPeak)
			dPeak = dOut;
	}

	// normalize

	double dAmp = 1.0;
	if (dPeak > 0.0)
		dAmp = INT8_MAX / dPeak;

	for (int32_t i = 0; i < length; i++)
		buffer[i] = (int8_t)round(dBuffer[i] * dAmp);

	free(dBuffer);

	return true;
}

bool downsample2x16Bit(int16_t *buffer, int32_t originalLength)
{
	state[0] = state[1] = 0.0;

	double *dBuffer = (double *)malloc(originalLength * sizeof (double));
	if (dBuffer == NULL)
		return false;

	for (int32_t i = 0; i < originalLength; i++)
		dBuffer[i] = buffer[i] * (1.0 / (INT16_MAX+1.0));

	const double *input = dBuffer;
	double dPeak = 0.0;

	const int32_t length = originalLength / 2;
	for (int32_t i = 0; i < length; i++, input += 2)
	{
		double dOut = d2x(input, state);
		dBuffer[i] = dOut;

		dOut = ABS(dOut);
		if (dOut > dPeak)
			dPeak = dOut;
	}

	// normalize

	double dAmp = 1.0;
	if (dPeak > 0.0)
		dAmp = INT16_MAX / dPeak;

	for (int32_t i = 0; i < length; i++)
		buffer[i] = (int16_t)round(dBuffer[i] * dAmp);

	free(dBuffer);

	return true;
}

bool downsample2x32Bit(int32_t *buffer, int32_t originalLength)
{
	state[0] = state[1] = 0.0;

	double *dBuffer = (double *)malloc(originalLength * sizeof (double));
	if (dBuffer == NULL)
		return false;

	for (int32_t i = 0; i < originalLength; i++)
		dBuffer[i] = buffer[i] * (1.0 / (INT32_MAX+1.0));

	const double *input = dBuffer;
	double dPeak = 0.0;

	const int32_t length = originalLength / 2;
	for (int32_t i = 0; i < length; i++, input += 2)
	{
		double dOut = d2x(input, state);
		dBuffer[i] = dOut;

		dOut = ABS(dOut);
		if (dOut > dPeak)
			dPeak = dOut;
	}

	// normalize

	double dAmp = 1.0;
	if (dPeak > 0.0)
		dAmp = INT32_MAX / dPeak;

	for (int32_t i = 0; i < length; i++)
		buffer[i] = (int32_t)round(dBuffer[i] * dAmp);

	free(dBuffer);

	return true;
}
