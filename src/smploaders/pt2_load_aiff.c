#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <math.h>
#include "../pt2_header.h"
#include "../pt2_config.h"
#include "../pt2_structs.h"
#include "../pt2_textout.h"
#include "../pt2_visuals.h"
#include "../pt2_helpers.h"
#include "../pt2_replayer.h"
#include "../pt2_askbox.h"
#include "../pt2_downsample2x.h"
#include "../pt2_audio.h"

static uint32_t getAIFFSampleRate(uint8_t *in)
{
	/* 80-bit IEEE-754 to unsigned 32-bit integer (rounded).
	** Sign bit is ignored.
	*/

#define EXP_BIAS 16383

	const uint16_t exp15 = ((in[0] & 0x7F) << 8) | in[1];
	const uint64_t mantissaBits = *(uint64_t *)&in[2];
	const uint64_t mantissa63 = SWAP64(mantissaBits) & INT64_MAX;

	double dExp = exp15 - EXP_BIAS;
	double dMantissa = mantissa63 / (INT64_MAX+1.0);

	double dResult = (1.0 + dMantissa) * exp2(dExp);
	return (uint32_t)round(dResult);
}

bool loadAIFFSample(FILE *f, uint32_t filesize, moduleSample_t *s)
{
	uint8_t sampleRateBytes[10];
	uint16_t bitDepth, numChannels;
	uint32_t offset, sampleRate;

	// zero out chunk pointers and lengths
	uint32_t commPtr = 0; uint32_t commLen = 0;
	uint32_t ssndPtr = 0; uint32_t ssndLen = 0;

	bool unsigned8bit = false;

	fseek(f, 12, SEEK_SET);
	while (!feof(f) && (uint32_t)ftell(f) < filesize-12)
	{
		uint32_t blockName, blockSize;

		fread(&blockName, 4, 1, f); if (feof(f)) break;
		fread(&blockSize, 4, 1, f); if (feof(f)) break;

		blockName = SWAP32(blockName);
		blockSize = SWAP32(blockSize);

		switch (blockName)
		{
			case 0x434F4D4D: // "COMM"
			{
				commPtr = ftell(f);
				commLen = blockSize;
			}
			break;

			case 0x53534E44: // "SSND"
			{
				ssndPtr = ftell(f);
				ssndLen = blockSize;
			}
			break;

			default: break;
		}

		fseek(f, blockSize + (blockSize & 1), SEEK_CUR);
	}

	if (commPtr == 0 || commLen < 18 || ssndPtr == 0)
	{
		displayErrorMsg("NOT A VALID AIFF!");
		return false;
	}

	// kludge for some really strange AIFFs
	if (ssndLen == 0)
		ssndLen = filesize - ssndPtr;

	if (ssndPtr+ssndLen > (uint32_t)filesize)
		ssndLen = filesize - ssndPtr;

	fseek(f, commPtr, SEEK_SET);
	fread(&numChannels, 2, 1, f); numChannels = SWAP16(numChannels);
	fseek(f, 4, SEEK_CUR);
	fread(&bitDepth, 2, 1, f); bitDepth = SWAP16(bitDepth);
	fread(sampleRateBytes, 1, 10, f);

	fseek(f, 4 + 2 + 1, SEEK_CUR);

	if (numChannels != 1 && numChannels != 2) // sample type
	{
		displayErrorMsg("UNSUPPORTED AIFF!");
		return false;
	}

	if (bitDepth != 8 && bitDepth != 16 && bitDepth != 24 && bitDepth != 32)
	{
		displayErrorMsg("UNSUPPORTED AIFF!");
		return false;
	}

	// read compression type (if present)
	if (commLen > 18)
	{
		char compType[4];
		fread(&compType, 1, 4, f);
		if (memcmp(compType, "NONE", 4))
		{
			displayErrorMsg("UNSUPPORTED AIFF!");
			return false;
		}
	}

	sampleRate = getAIFFSampleRate(sampleRateBytes);

	// sample data chunk

	fseek(f, ssndPtr, SEEK_SET);

	fread(&offset, 4, 1, f);
	if (offset > 0)
	{
		displayErrorMsg("UNSUPPORTED AIFF!");
		return false;
	}

	fseek(f, 4, SEEK_CUR);

	ssndLen -= 8; // don't include offset and blockSize datas

	int32_t sampleLength = ssndLen;
	if (sampleLength == 0)
	{
		displayErrorMsg("NOT A VALID AIFF!");
		return false;
	}

	bool downSample = false;
	if (sampleRate > 22050 && !config.noDownsampleOnSmpLoad)
	{
		if (askBox(ASKBOX_DOWNSAMPLE, "DOWNSAMPLE ?"))
			downSample = true;
	}

	int8_t *smpDataPtr = &song->sampleData[s->offset];

	if (bitDepth == 8) // 8-BIT INTEGER SAMPLE
	{
		if (sampleLength > config.maxSampleLength*4)
			sampleLength = config.maxSampleLength*4;

		int8_t *audioDataS8 = (int8_t *)malloc(sampleLength * sizeof (int8_t));
		if (audioDataS8 == NULL)
		{
			statusOutOfMemory();
			return false;
		}

		// read sample data
		if (fread(audioDataS8, 1, sampleLength, f) != (size_t)sampleLength)
		{
			free(audioDataS8);
			displayErrorMsg("I/O ERROR !");
			return false;
		}

		if (unsigned8bit)
		{
			for (int32_t i = 0; i < sampleLength; i++)
				audioDataS8[i] ^= 0x80;
		}

		// convert from stereo to mono (if needed)
		if (numChannels == 2)
		{
			sampleLength >>= 1;
			for (int32_t i = 0; i < sampleLength-1; i++) // add right channel to left channel
				audioDataS8[i] = (audioDataS8[(i * 2) + 0] + audioDataS8[(i * 2) + 1]) >> 1;;
		}

		// 2x downsampling
		if (downSample)
		{
			downsample2x8Bit(audioDataS8, sampleLength);
			sampleLength >>= 1;
		}

		if (sampleLength > config.maxSampleLength)
			sampleLength = config.maxSampleLength;

		turnOffVoices();
		for (int32_t i = 0; i < sampleLength; i++)
			smpDataPtr[i] = audioDataS8[i];

		free(audioDataS8);
	}
	else if (bitDepth == 16) // 16-BIT INTEGER SAMPLE
	{
		sampleLength >>= 1;
		if (sampleLength > config.maxSampleLength*4)
			sampleLength = config.maxSampleLength*4;

		int16_t *audioDataS16 = (int16_t *)malloc(sampleLength * sizeof (int16_t));
		if (audioDataS16 == NULL)
		{
			statusOutOfMemory();
			return false;
		}

		// read sample data
		if (fread(audioDataS16, 2, sampleLength, f) != (size_t)sampleLength)
		{
			free(audioDataS16);
			displayErrorMsg("I/O ERROR !");
			return false;
		}

		// fix endianness
		for (int32_t i = 0; i < sampleLength; i++)
			audioDataS16[i] = SWAP16(audioDataS16[i]);

		// convert from stereo to mono (if needed)
		if (numChannels == 2)
		{
			sampleLength >>= 1;
			for (int32_t i = 0; i < sampleLength-1; i++) // add right channel to left channel
				audioDataS16[i] = (audioDataS16[(i << 1) + 0] + audioDataS16[(i << 1) + 1]) >> 1;
		}

		// 2x downsampling
		if (downSample)
		{
			downsample2x16Bit(audioDataS16, sampleLength);
			sampleLength >>= 1;
		}

		if (sampleLength > config.maxSampleLength)
			sampleLength = config.maxSampleLength;

		double dAmp = 1.0;
		if (downSample) // we already normalized
		{
			dAmp = INT8_MAX / (double)INT16_MAX;
		}
		else
		{
			const double dPeak = get16BitPeak(audioDataS16, sampleLength);
			if (dPeak > 0.0)
				dAmp = INT8_MAX / dPeak;
		}

		turnOffVoices();
		for (int32_t i = 0; i < sampleLength; i++)
		{
			int32_t smp32 = (int32_t)round(audioDataS16[i] * dAmp);
			assert(smp32 >= -128 && smp32 <= 127); // shouldn't happen according to dAmp (but just in case)
			smpDataPtr[i] = (int8_t)smp32;
		}

		free(audioDataS16);
	}
	else if (bitDepth == 24) // 24-BIT INTEGER SAMPLE
	{
		sampleLength /= 3;
		if (sampleLength > config.maxSampleLength*4)
			sampleLength = config.maxSampleLength*4;

		int32_t *audioDataS32 = (int32_t *)malloc(sampleLength * sizeof (int32_t));
		if (audioDataS32 == NULL)
		{
			statusOutOfMemory();
			return false;
		}

		// read sample data
		if (fread(&audioDataS32[sampleLength >> 2], 3, sampleLength, f) != (size_t)sampleLength)
		{
			free(audioDataS32);
			displayErrorMsg("I/O ERROR !");
			return false;
		}

		// convert to 32-bit
		uint8_t *audioDataU8 = (uint8_t *)audioDataS32 + sampleLength;
		for (int32_t i = 0; i < sampleLength; i++)
		{
			audioDataS32[i] = (audioDataU8[0] << 24) | (audioDataU8[1] << 16) | (audioDataU8[2] << 8);
			audioDataU8 += 3;
		}

		// convert from stereo to mono (if needed)
		if (numChannels == 2)
		{
			sampleLength >>= 1;
			for (int32_t i = 0; i < sampleLength-1; i++) // add right channel to left channel
			{
				int64_t smp = ((int64_t)audioDataS32[(i << 1) + 0] + audioDataS32[(i << 1) + 1]) >> 1;
				audioDataS32[i] = (int32_t)smp;
			}
		}

		// 2x downsampling
		if (downSample)
		{
			downsample2x32Bit(audioDataS32, sampleLength);
			sampleLength >>= 1;
		}

		if (sampleLength > config.maxSampleLength)
			sampleLength = config.maxSampleLength;

		double dAmp = 1.0;
		if (downSample) // we already normalized
		{
			dAmp = INT8_MAX / (double)INT32_MAX;
		}
		else
		{
			const double dPeak = get32BitPeak(audioDataS32, sampleLength);
			if (dPeak > 0.0)
				dAmp = INT8_MAX / dPeak;
		}

		turnOffVoices();
		for (int32_t i = 0; i < sampleLength; i++)
		{
			int32_t smp32 = (int32_t)round(audioDataS32[i] * dAmp);
			smpDataPtr[i] = (int8_t)smp32;
		}

		free(audioDataS32);
	}
	else if (bitDepth == 32) // 32-BIT INTEGER SAMPLE
	{
		sampleLength >>= 2;
		if (sampleLength > config.maxSampleLength*4)
			sampleLength = config.maxSampleLength*4;

		int32_t *audioDataS32 = (int32_t *)malloc(sampleLength * sizeof (int32_t));
		if (audioDataS32 == NULL)
		{
			statusOutOfMemory();
			return false;
		}

		// read sample data
		if (fread(audioDataS32, 4, sampleLength, f) != (size_t)sampleLength)
		{
			free(audioDataS32);
			displayErrorMsg("I/O ERROR !");
			return false;
		}

		// fix endianness
		for (int32_t i = 0; i < sampleLength; i++)
			audioDataS32[i] = SWAP32(audioDataS32[i]);

		// convert from stereo to mono (if needed)
		if (numChannels == 2)
		{
			sampleLength >>= 1;
			for (int32_t i = 0; i < sampleLength-1; i++) // add right channel to left channel
			{
				int64_t smp = ((int64_t)audioDataS32[(i << 1) + 0] + audioDataS32[(i << 1) + 1]) >> 1;
				audioDataS32[i] = (int32_t)smp;
			}
		}

		// 2x downsampling
		if (downSample)
		{
			downsample2x32Bit(audioDataS32, sampleLength);
			sampleLength >>= 1;
		}

		if (sampleLength > config.maxSampleLength)
			sampleLength = config.maxSampleLength;

		double dAmp = 1.0;
		if (downSample) // we already normalized
		{
			dAmp = INT8_MAX / (double)INT32_MAX;
		}
		else
		{
			const double dPeak = get32BitPeak(audioDataS32, sampleLength);
			if (dPeak > 0.0)
				dAmp = INT8_MAX / dPeak;
		}

		turnOffVoices();
		for (int32_t i = 0; i < sampleLength; i++)
		{
			int32_t smp32 = (int32_t)round(audioDataS32[i] * dAmp);
			smpDataPtr[i] = (int8_t)smp32;
		}

		free(audioDataS32);
	}

	if (sampleLength & 1)
	{
		if (++sampleLength > config.maxSampleLength)
			sampleLength = config.maxSampleLength;
	}

	s->length = sampleLength;
	s->fineTune = 0;
	s->volume = 64;
	s->loopStart = 0;
	s->loopLength = 2;

	return true;
}