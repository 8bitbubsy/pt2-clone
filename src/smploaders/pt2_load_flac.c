// hide miniflac compiler warnings
#ifdef _MSC_VER
#pragma warning(disable: 4146)
#pragma warning(disable: 4201)
#pragma warning(disable: 4244)
#pragma warning(disable: 4245)
#pragma warning(disable: 4267)
#pragma warning(disable: 4334)
#endif

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#define MINIFLAC_IMPLEMENTATION
#include "miniflac.h"
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

#define MAX_FLAC_BLOCK_SIZE 65535

#define INC_BUFFER \
	bytesLeft -= bytesHandled; \
	bufferPtr += bytesHandled;

static double *dSmpBuf;
static uint8_t numChannels, bitDepth;
static uint64_t totalSamples;

static bool writeSamples(uint64_t sampleIndex, int32_t **samples, uint32_t numSamples);

bool loadFLACSample(FILE *f, uint32_t filesize, moduleSample_t *s)
{
	int32_t *samples[2];
	uint32_t sampleRate = 44100;
	miniflac_t decoder;

	dSmpBuf = NULL;

	uint8_t *fileBuffer = (uint8_t *)malloc(filesize);
	if (fileBuffer == NULL)
		goto oomError;

	samples[0] = (int32_t *)malloc(sizeof (int32_t) * MAX_FLAC_BLOCK_SIZE);
	samples[1] = (int32_t *)malloc(sizeof (int32_t) * MAX_FLAC_BLOCK_SIZE);
	if (samples[0] == NULL || samples[1] == NULL)
		goto oomError;

	if (fread(fileBuffer, 1, filesize, f) != filesize)
		goto decodeError;

	uint8_t *bufferPtr = fileBuffer;
	uint32_t bytesHandled, bytesLeft = filesize;

	miniflac_init(&decoder, MINIFLAC_CONTAINER_NATIVE);
	if (miniflac_sync(&decoder, bufferPtr, bytesLeft, &bytesHandled) != MINIFLAC_OK) goto decodeError;
	INC_BUFFER

	int32_t loopStart = 0, loopLength = 2;
	s->volume = 64;
	
	while (decoder.state == MINIFLAC_METADATA)
	{
		if (decoder.metadata.header.type == MINIFLAC_METADATA_STREAMINFO)
		{
			if (miniflac_streaminfo_sample_rate(&decoder, bufferPtr, bytesLeft, &bytesHandled, &sampleRate) != MINIFLAC_OK) goto decodeError;
			INC_BUFFER
			if (miniflac_streaminfo_channels(&decoder, bufferPtr, bytesLeft, &bytesHandled, &numChannels) != MINIFLAC_OK) goto decodeError;
			INC_BUFFER
			if (miniflac_streaminfo_bps(&decoder, bufferPtr, bytesLeft, &bytesHandled, &bitDepth) != MINIFLAC_OK) goto decodeError;
			INC_BUFFER
			if (miniflac_streaminfo_total_samples(&decoder, bufferPtr, bytesLeft, &bytesHandled, &totalSamples) != MINIFLAC_OK) goto decodeError;
			INC_BUFFER
		}
		else if (decoder.metadata.header.type == MINIFLAC_METADATA_APPLICATION && !memcmp(bufferPtr, "riff", 4))
		{
			const uint8_t *data = bufferPtr + 4;

			uint32_t chunkID  = *(uint32_t *)data; data += 4;
			uint32_t chunkLen = *(uint32_t *)data; data += 4;

			if (chunkID == 0x61727478 && chunkLen >= 8) // "xtra"
			{
				data += 6;

				// volume (0..256)
				uint16_t tmpVol = *(uint16_t *)data;
				if (tmpVol > 256)
					tmpVol = 256;

				s->volume = (uint8_t)((tmpVol + 2) / 4); // 0..256 -> 0..64 (rounded)
			}

			if (chunkID == 0x6C706D73 && chunkLen > 52) // "smpl"
			{
				data += 28; // seek to first wanted byte

				uint32_t numLoops = *(uint32_t *)data; data += 4;
				if (numLoops == 1)
				{
					data += 4+4+4; // skip "samplerData", "identifier" and "loopType"

					         loopStart = *(uint32_t *)data; data += 4;
					uint32_t loopEnd   = *(uint32_t *)data; data += 4;

					loopLength = (loopEnd+1) - loopStart;
				}
			}
		}

		if (miniflac_sync_native(&decoder, bufferPtr, bytesLeft, &bytesHandled) != MINIFLAC_OK) break;
		INC_BUFFER
	}

	if (totalSamples == 0 || numChannels == 0)
		goto decodeError;

	if (numChannels > 2 || (bitDepth != 8 && bitDepth != 16 && bitDepth != 24))
	{
		displayErrorMsg("UNSUPPORTED FLAC !");
		goto error;
	}

	bool downSample = false;
	if (sampleRate > 22050 && !config.noDownsampleOnSmpLoad)
	{
		if (askBox(ASKBOX_DOWNSAMPLE, "DOWNSAMPLE ?"))
			downSample = true;
	}

	if (totalSamples > (uint64_t)(config.maxSampleLength*2))
		totalSamples = config.maxSampleLength*2;

	dSmpBuf = (double *)calloc(totalSamples, sizeof (double));
	if (dSmpBuf == NULL)
		goto oomError;
	
	int64_t sampleIndex = 0;
	while (true)
	{
		if (miniflac_decode(&decoder, bufferPtr, bytesLeft, &bytesHandled, samples) != MINIFLAC_OK) break;
		INC_BUFFER

		const int32_t numSamples = decoder.frame.header.block_size;
		if (!writeSamples(sampleIndex, samples, numSamples)) break;
		sampleIndex += numSamples;

		if (miniflac_sync_native(&decoder, bufferPtr, bytesLeft, &bytesHandled) != MINIFLAC_OK) break;
		INC_BUFFER
	}

	int32_t sampleLength = (int32_t)totalSamples;

	if (downSample)
	{
		downsample2xDouble(dSmpBuf, sampleLength);
		sampleLength /= 2;
	}

	double dAmp = 0.0;
	const double dPeak = getDoublePeak(dSmpBuf, sampleLength);
	if (dPeak > 0.0)
		dAmp = INT8_MAX / dPeak;

	int8_t *smpDataPtr = &song->sampleData[s->offset];

	turnOffVoices();
	for (int32_t i = 0; i < sampleLength; i++)
		smpDataPtr[i] = (int8_t)round(dSmpBuf[i] * dAmp);

	if (sampleLength & 1)
	{
		if (++sampleLength > config.maxSampleLength)
			sampleLength = config.maxSampleLength;
		else
			smpDataPtr[sampleLength-1] = 0;
	}

	if (downSample)
	{
		// we already downsampled 2x, so we're half the original length
		loopStart >>= 1;
		loopLength >>= 1;
	}

	loopStart &= ~1;
	loopLength &= ~1;

	if (loopLength < 2 || loopStart+loopLength > sampleLength)
	{
		loopStart = 0;
		loopLength = 2;
	}

	s->fineTune = 0;
	s->loopStart = loopStart;
	s->loopLength = loopLength;
	s->length = sampleLength;

	if (dSmpBuf != NULL) { free(dSmpBuf); dSmpBuf = NULL; }
	if (fileBuffer != NULL) free(fileBuffer);
	if (samples[0] != NULL) free(samples[0]);
	if (samples[1] != NULL) free(samples[1]);

	return true;

decodeError:
	displayErrorMsg("FLAC LOAD ERROR !");
	goto error;
oomError:
	statusOutOfMemory();
error:
	if (dSmpBuf != NULL) { free(dSmpBuf); dSmpBuf = NULL; }
	if (fileBuffer != NULL) free(fileBuffer);
	if (samples[0] != NULL) free(samples[0]);
	if (samples[1] != NULL) free(samples[1]);

	return false;

	(void)filesize;
}

static bool writeSamples(uint64_t sampleIndex, int32_t **samples, uint32_t numSamples)
{
	if (sampleIndex >= totalSamples)
		return false;

	uint32_t samplesTodo = numSamples;
	if (sampleIndex+samplesTodo > totalSamples)
		samplesTodo = totalSamples - sampleIndex;

	double dMul = 1.0 / (1 << (bitDepth-1));
	double *dPtr = (double *)dSmpBuf + sampleIndex;

	if (numChannels == 1)
	{
		int32_t *src32 = samples[0];
		for (uint32_t i = 0; i < samplesTodo; i++)
			dPtr[i] = (double)(src32[i] * dMul);
	}
	else
	{
		dMul *= 0.5;

		int32_t *src32L = samples[0], *src32R = samples[1];
		for (uint32_t i = 0; i < samplesTodo; i++)
			dPtr[i] = (double)((src32L[i] + src32R[i]) * dMul);
	}

	return true;
}
