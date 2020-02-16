/* These are second variants of low-pass/high-pass filters that are better than
** the ones used in the main audio mixer. The reason we use a different ones for
** the main audio mixer is because it makes it sound closer to real Amigas.
**
** These ones are used for low-pass filtering when loading samples w/ 2x downsampling.
*/

#include <stdio.h>
#include <stdbool.h>
#include <math.h>
#include "pt2_audio.h" // DENORMAL_OFFSET constant
#include "pt2_helpers.h"

typedef struct filterState_t
{
	double dBuffer, b0, b1;
} filterState_t;

static void calcFilterCoeffs(double dSr, double dHz, filterState_t *filter)
{
	filter->b0 = tan((M_PI * dHz) / dSr);
	filter->b1 = 1.0 / (1.0 + filter->b0);
}

static double doLowpass(filterState_t *filter, double dIn)
{
	double dOutput;

	dOutput = (filter->b0 * dIn + filter->dBuffer) * filter->b1;
	filter->dBuffer = filter->b0 * (dIn - dOutput) + dOutput + DENORMAL_OFFSET;

	return dOutput;
}

bool lowPassSample8Bit(int8_t *buffer, int32_t length, int32_t sampleFrequency, double cutoff)
{
	filterState_t filter;

	if (buffer == NULL || length == 0 || cutoff == 0.0)
		return false;

	calcFilterCoeffs(sampleFrequency, cutoff, &filter);

	filter.dBuffer = 0.0;
	for (int32_t i = 0; i < length; i++)
	{
		int32_t sample;
		sample = (int32_t)doLowpass(&filter, buffer[i]);
		buffer[i] = (int8_t)CLAMP(sample, INT8_MIN, INT8_MAX);
	}
	
	return true;
}

bool lowPassSample8BitUnsigned(uint8_t *buffer, int32_t length, int32_t sampleFrequency, double cutoff)
{
	filterState_t filter;

	if (buffer == NULL || length == 0 || cutoff == 0.0)
		return false;

	calcFilterCoeffs(sampleFrequency, cutoff, &filter);

	filter.dBuffer = 0.0;
	for (int32_t i = 0; i < length; i++)
	{
		int32_t sample;
		sample = (int32_t)doLowpass(&filter, buffer[i] - 128);
		sample = CLAMP(sample, INT8_MIN, INT8_MAX);
		buffer[i] = (uint8_t)(sample + 128);
	}
		
	return true;
}

bool lowPassSample16Bit(int16_t *buffer, int32_t length, int32_t sampleFrequency, double cutoff)
{
	filterState_t filter;

	if (buffer == NULL || length == 0 || cutoff == 0.0)
		return false;

	calcFilterCoeffs(sampleFrequency, cutoff, &filter);

	filter.dBuffer = 0.0;
	for (int32_t i = 0; i < length; i++)
	{
		int32_t sample;
		sample = (int32_t)doLowpass(&filter, buffer[i]);
		buffer[i] = (int16_t)CLAMP(sample, INT16_MIN, INT16_MAX);
	}

	return true;
}

bool lowPassSample32Bit(int32_t *buffer, int32_t length, int32_t sampleFrequency, double cutoff)
{
	filterState_t filter;

	if (buffer == NULL || length == 0 || cutoff == 0.0)
		return false;

	calcFilterCoeffs(sampleFrequency, cutoff, &filter);

	filter.dBuffer = 0.0;
	for (int32_t i = 0; i < length; i++)
	{
		int64_t sample;
		sample = (int64_t)doLowpass(&filter, buffer[i]);
		buffer[i] = (int32_t)CLAMP(sample, INT32_MIN, INT32_MAX);
	}

	return true;
}

bool lowPassSampleFloat(float *buffer, int32_t length, int32_t sampleFrequency, double cutoff)
{
	filterState_t filter;

	if (buffer == NULL || length == 0 || cutoff == 0.0)
		return false;

	calcFilterCoeffs(sampleFrequency, cutoff, &filter);

	filter.dBuffer = 0.0;
	for (int32_t i = 0; i < length; i++)
		buffer[i] = (float)doLowpass(&filter, buffer[i]);
		
	return true;
}

bool lowPassSampleDouble(double *buffer, int32_t length, int32_t sampleFrequency, double cutoff)
{
	filterState_t filter;

	if (buffer == NULL || length == 0 || cutoff == 0.0)
		return false;

	calcFilterCoeffs(sampleFrequency, cutoff, &filter);
	
	filter.dBuffer = 0.0;
	for (int32_t i = 0; i < length; i++)
		buffer[i] = doLowpass(&filter, buffer[i]);
		
	return true;
}
