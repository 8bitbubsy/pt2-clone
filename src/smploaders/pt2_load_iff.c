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

bool loadIFFSample(FILE *f, uint32_t filesize, moduleSample_t *s)
{
	char tmpCharBuf[23];
	uint16_t sampleRate;
	int32_t loopStart, loopLength;

	// zero out chunk pointers and lengths
	uint32_t vhdrPtr = 0; uint32_t vhdrLen = 0;
	uint32_t bodyPtr = 0; uint32_t bodyLen = 0;
	uint32_t namePtr = 0;  int32_t nameLen = 0;

	fseek(f, 8, SEEK_SET);
	fread(tmpCharBuf, 1, 4, f);
	bool is16Bit = !strncmp(tmpCharBuf, "16SV", 4);

	int32_t sampleLength = 0;
	uint32_t sampleVolume = 65536; // max volume

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
			case 0x56484452: // VHDR
			{
				vhdrPtr = ftell(f);
				vhdrLen = blockSize;
			}
			break;

			case 0x4E414D45: // NAME
			{
				namePtr = ftell(f);
				nameLen = blockSize;
			}
			break;

			case 0x424F4459: // BODY
			{
				bodyPtr = ftell(f);
				bodyLen = blockSize;
			}
			break;

			default: break;
		}

		fseek(f, blockSize + (blockSize & 1), SEEK_CUR);
	}

	if (vhdrPtr == 0 || vhdrLen < 20 || bodyPtr == 0)
	{
		displayErrorMsg("NOT A VALID IFF !");
		return false;
	}

	// kludge for some really strange IFFs
	if (bodyLen == 0)
		bodyLen = filesize - bodyPtr;

	if (bodyPtr+bodyLen > (uint32_t)filesize)
		bodyLen = filesize - bodyPtr;

	fseek(f, vhdrPtr, SEEK_SET);
	fread(&loopStart,  4, 1, f); loopStart  = SWAP32(loopStart);
	fread(&loopLength, 4, 1, f); loopLength = SWAP32(loopLength);
	fseek(f, 4, SEEK_CUR);
	fread(&sampleRate, 2, 1, f); sampleRate = SWAP16(sampleRate);
	fseek(f, 1, SEEK_CUR);

	if (fgetc(f) != 0) // sample type
	{
		displayErrorMsg("UNSUPPORTED IFF !");
		return false;
	}

	fread(&sampleVolume, 4, 1, f); sampleVolume = SWAP32(sampleVolume);
	if (sampleVolume > 65536)
		sampleVolume = 65536;

	sampleVolume = (sampleVolume + 512) / 1024; // rounded
	if (sampleVolume > 64)
		sampleVolume = 64;

	sampleLength = bodyLen;

	if (sampleLength == 0)
	{
		displayErrorMsg("NOT A VALID IFF !");
		return false;
	}

	bool downSample = false;
	if (sampleRate > 22050 && !config.noDownsampleOnSmpLoad)
	{
		if (askBox(ASKBOX_DOWNSAMPLE, "DOWNSAMPLE ?"))
			downSample = true;
	}

	int32_t maxSampleLength = config.maxSampleLength;
	if (is16Bit)
		maxSampleLength *= 2;

	if (downSample)
		maxSampleLength *= 2;

	if (sampleLength > maxSampleLength)
		sampleLength = maxSampleLength;

	int8_t *sampleData = (int8_t *)malloc(sampleLength);
	if (sampleData == NULL)
	{
		statusOutOfMemory();
		return false;
	}

	if (is16Bit)
	{
		sampleLength >>= 1;
		loopStart >>= 1;
		loopLength >>= 1;
	}

	if (downSample)
	{
		loopStart >>= 1;
		loopLength >>= 1;
	}

	turnOffVoices();

	int8_t *smpDataPtr = &song->sampleData[s->offset];

	fseek(f, bodyPtr, SEEK_SET);
	if (is16Bit) // FT2-specific 16SV format (little-endian samples)
	{
		fread(sampleData, 1, sampleLength << 1, f);
		int16_t *ptr16 = (int16_t *)sampleData;

		// 2x downsampling
		if (downSample)
		{
			downsample2x16Bit(ptr16, sampleLength);
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
			const double dPeak = get16BitPeak(ptr16, sampleLength);
			if (dPeak > 0.0)
				dAmp = INT8_MAX / dPeak;
		}

		for (int32_t i = 0; i < sampleLength; i++)
		{
			int32_t smp32 = (int32_t)round(ptr16[i] * dAmp);
			assert(smp32 >= -128 && smp32 <= 127); // shouldn't happen according to dAmp (but just in case)
			smpDataPtr[i] = (int8_t)smp32;
		}
	}
	else
	{
		fread(sampleData, 1, sampleLength, f);

		// 2x downsampling
		if (downSample)
		{
			downsample2x8Bit(sampleData, sampleLength);
			sampleLength >>= 1;
		}

		if (sampleLength > config.maxSampleLength)
			sampleLength = config.maxSampleLength;

		memcpy(smpDataPtr, sampleData, sampleLength);
	}

	free(sampleData);

	if (sampleLength & 1)
	{
		if (++sampleLength > config.maxSampleLength)
			sampleLength = config.maxSampleLength;
	}

	loopStart &= ~1;
	loopLength &= ~1;

	if (loopLength < 2 || loopStart+loopLength > sampleLength)
	{
		loopStart = 0;
		loopLength = 2;
	}

	// set sample attributes
	s->volume = (int8_t)sampleVolume;
	s->fineTune = 0;
	s->length = sampleLength;
	s->loopStart = loopStart;
	s->loopLength = loopLength;

	if (namePtr != 0 && nameLen > 0)
	{
		// read name

		fseek(f, namePtr, SEEK_SET);
		memset(tmpCharBuf, 0, sizeof (tmpCharBuf));

		if (nameLen > 21)
		{
			fread(tmpCharBuf, 1, 21, f);
			fseek(f, nameLen - 21, SEEK_CUR);
		}
		else
		{
			fread(tmpCharBuf, 1, nameLen, f);
		}

		// copy over sample name
		memset(s->text, '\0', sizeof (s->text));

		nameLen = (uint32_t)strlen(tmpCharBuf);
		if (nameLen > 21)
			nameLen = 21;

		memcpy(s->text, tmpCharBuf, nameLen);
	}

	return true;
}