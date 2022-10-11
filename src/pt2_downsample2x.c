// for finding memory leaks in debug mode with Visual Studio 
#if defined _DEBUG && defined _MSC_VER
#include <crtdbg.h>
#endif

/* High-quality /2 decimator from
** https://www.musicdsp.org/en/latest/Filters/231-hiqh-quality-2-decimators.html
*/

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include "pt2_helpers.h" // ABS()

// ----------------------------------------------------------
// reserved for main audio channel mixer, PAT2SMP and MOD2WAV
// ----------------------------------------------------------

static double R1_L, R2_L, R3_L, R4_L, R5_L, R6_L, R7_L, R8_L, R9_L;
static double R1_R, R2_R, R3_R, R4_R, R5_R, R6_R, R7_R, R8_R, R9_R;

void clearMixerDownsamplerStates(void)
{
	R1_L = R2_L = R3_L = R4_L = R5_L = R6_L = R7_L = R8_L = R9_L = 0.0;
	R1_R = R2_R = R3_R = R4_R = R5_R = R6_R = R7_R = R8_R = R9_R = 0.0;
}

double decimate2x_L(double x0, double x1)
{
	const double h0 =  8192.0 / 16384.0;
	const double h1 =  5042.0 / 16384.0;
	const double h3 = -1277.0 / 16384.0;
	const double h5 =   429.0 / 16384.0;
	const double h7 =  -116.0 / 16384.0;
	const double h9 =    18.0 / 16384.0;

	double h9x0 = h9*x0;
	double h7x0 = h7*x0;
	double h5x0 = h5*x0;
	double h3x0 = h3*x0;
	double h1x0 = h1*x0;
	double R10  = R9_L+h9x0;

	R9_L = R8_L+h7x0;
	R8_L = R7_L+h5x0;
	R7_L = R6_L+h3x0;
	R6_L = R5_L+h1x0;
	R5_L = R4_L+h1x0+h0*x1;
	R4_L = R3_L+h3x0;
	R3_L = R2_L+h5x0;
	R2_L = R1_L+h7x0;
	R1_L = h9x0;

	return R10;
}

double decimate2x_R(double x0, double x1)
{
	const double h0 =  8192.0 / 16384.0;
	const double h1 =  5042.0 / 16384.0;
	const double h3 = -1277.0 / 16384.0;
	const double h5 =   429.0 / 16384.0;
	const double h7 =  -116.0 / 16384.0;
	const double h9 =    18.0 / 16384.0;

	double h9x0 = h9*x0;
	double h7x0 = h7*x0;
	double h5x0 = h5*x0;
	double h3x0 = h3*x0;
	double h1x0 = h1*x0;
	double R10  = R9_R+h9x0;

	R9_R = R8_R+h7x0;
	R8_R = R7_R+h5x0;
	R7_R = R6_R+h3x0;
	R6_R = R5_R+h1x0;
	R5_R = R4_R+h1x0+h0*x1;
	R4_R = R3_R+h3x0;
	R3_R = R2_R+h5x0;
	R2_R = R1_R+h7x0;
	R1_R = h9x0;

	return R10;
}

// ----------------------------------------------------------
// ----------------------------------------------------------
// ----------------------------------------------------------

static double R1, R2, R3, R4, R5, R6, R7, R8, R9;

static void clearDownsamplerState(void)
{
	R1 = R2 = R3 = R4 = R5 = R6 = R7 = R8 = R9 = 0.0;
}

static double decimate2x(double x0, double x1)
{
	const double h0 =  8192.0 / 16384.0;
	const double h1 =  5042.0 / 16384.0;
	const double h3 = -1277.0 / 16384.0;
	const double h5 =   429.0 / 16384.0;
	const double h7 =  -116.0 / 16384.0;
	const double h9 =    18.0 / 16384.0;

	double h9x0 = h9*x0;
	double h7x0 = h7*x0;
	double h5x0 = h5*x0;
	double h3x0 = h3*x0;
	double h1x0 = h1*x0;
	double R10  = R9+h9x0;

	R9 = R8+h7x0;
	R8 = R7+h5x0;
	R7 = R6+h3x0;
	R6 = R5+h1x0;
	R5 = R4+h1x0+h0*x1;
	R4 = R3+h3x0;
	R3 = R2+h5x0;
	R2 = R1+h7x0;
	R1 = h9x0;

	return R10;
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
	{
		double dSmp = dBuffer[i] * dAmp;

		// faster than calling round()
		     if (dSmp < 0.0) dSmp -= 0.5;
		else if (dSmp > 0.0) dSmp += 0.5;

		buffer[i] = (uint8_t)dSmp + 128;
	}

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
	{
		double dSmp = dBuffer[i] * dAmp;

		// faster than calling round()
		     if (dSmp < 0.0) dSmp -= 0.5;
		else if (dSmp > 0.0) dSmp += 0.5;

		buffer[i] = (int8_t)dSmp;
	}

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
	{
		double dSmp = dBuffer[i] * dAmp;

		// faster than calling round()
		     if (dSmp < 0.0) dSmp -= 0.5;
		else if (dSmp > 0.0) dSmp += 0.5;

		buffer[i] = (int16_t)dSmp;
	}

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
	{
		double dSmp = dBuffer[i] * dAmp;

		// faster than calling round()
		     if (dSmp < 0.0) dSmp -= 0.5;
		else if (dSmp > 0.0) dSmp += 0.5;

		buffer[i] = (int32_t)dSmp;
	}

	free(dBuffer);
	return true;
}
