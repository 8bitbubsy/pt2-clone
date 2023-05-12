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

enum
{
	WAV_FORMAT_PCM = 0x0001,
	WAV_FORMAT_IEEE_FLOAT = 0x0003
};

bool loadWAVSample(FILE *f, uint32_t filesize, moduleSample_t *s)
{
	uint16_t audioFormat, numChannels, bitsPerSample;
	uint32_t sampleRate;

	// zero out chunk pointers and lengths
	uint32_t fmtPtr  = 0; uint32_t fmtLen = 0;
	uint32_t dataPtr = 0; uint32_t dataLen = 0;
	uint32_t inamPtr = 0;  int32_t inamLen = 0;
	uint32_t xtraPtr = 0; uint32_t xtraLen = 0;
	uint32_t smplPtr = 0; uint32_t smplLen = 0;

	// look for wanted chunks and set up pointers + lengths
	fseek(f, 12, SEEK_SET);

	uint32_t bytesRead = 0;
	while (!feof(f) && bytesRead < (uint32_t)filesize-12)
	{
		uint32_t chunkID, chunkSize;

		fread(&chunkID, 4, 1, f); if (feof(f)) break;
		fread(&chunkSize, 4, 1, f); if (feof(f)) break;

		uint32_t endOfChunk = (ftell(f) + chunkSize) + (chunkSize & 1);
		switch (chunkID)
		{
			case 0x20746D66: // "fmt "
			{
				fmtPtr = ftell(f);
				fmtLen = chunkSize;
			}
			break;

			case 0x61746164: // "data"
			{
				dataPtr = ftell(f);
				dataLen = chunkSize;
			}
			break;

			case 0x5453494C: // "LIST"
			{
				if (chunkSize >= 4)
				{
					fread(&chunkID, 4, 1, f);
					if (chunkID == 0x4F464E49) // "INFO"
					{
						bytesRead = 0;
						while (!feof(f) && bytesRead < chunkSize)
						{
							fread(&chunkID, 4, 1, f);
							fread(&chunkSize, 4, 1, f);

							switch (chunkID)
							{
								case 0x4D414E49: // "INAM"
								{
									inamPtr = ftell(f);
									inamLen = chunkSize;
								}
								break;

								default: break;
							}

							bytesRead += chunkSize + (chunkSize & 1);
						}
					}
				}
			}
			break;

			case 0x61727478: // "xtra"
			{
				xtraPtr = ftell(f);
				xtraLen = chunkSize;
			}
			break;

			case 0x6C706D73: // "smpl"
			{
				smplPtr = ftell(f);
				smplLen = chunkSize;
			}
			break;

			default: break;
		}

		bytesRead += chunkSize + (chunkSize & 1);
		fseek(f, endOfChunk, SEEK_SET);
	}

	// we need at least "fmt " and "data" - check if we found them sanely
	if (fmtPtr == 0 || fmtLen < 16 || dataPtr == 0 || dataLen == 0)
	{
		displayErrorMsg("NOT A WAV !");
		return false;
	}

	// ---- READ "fmt " CHUNK ----
	fseek(f, fmtPtr, SEEK_SET);
	fread(&audioFormat, 2, 1, f);
	fread(&numChannels, 2, 1, f);
	fread(&sampleRate,  4, 1, f);
	fseek(f, 6, SEEK_CUR);
	fread(&bitsPerSample, 2, 1, f);
	int32_t sampleLength = dataLen;
	// ---------------------------

	if (sampleRate == 0 || sampleLength == 0 || sampleLength >= (int32_t)filesize*(bitsPerSample/8))
	{
		displayErrorMsg("WAV CORRUPT !");
		return false;
	}

	if (audioFormat != WAV_FORMAT_PCM && audioFormat != WAV_FORMAT_IEEE_FLOAT)
	{
		displayErrorMsg("WAV UNSUPPORTED !");
		return false;
	}

	if (numChannels == 0 || numChannels > 2)
	{
		displayErrorMsg("WAV UNSUPPORTED !");
		return false;
	}

	if (audioFormat == WAV_FORMAT_IEEE_FLOAT && bitsPerSample != 32 && bitsPerSample != 64)
	{
		displayErrorMsg("WAV UNSUPPORTED !");
		return false;
	}

	if (bitsPerSample != 8 && bitsPerSample != 16 && bitsPerSample != 24 && bitsPerSample != 32 && bitsPerSample != 64)
	{
		displayErrorMsg("WAV UNSUPPORTED !");
		return false;
	}

	bool downSample = false;
	if (sampleRate > 22050 && !config.noDownsampleOnSmpLoad)
	{
		if (askBox(ASKBOX_DOWNSAMPLE, "DOWNSAMPLE ?"))
			downSample = true;
	}

	// ---- READ SAMPLE DATA ----
	fseek(f, dataPtr, SEEK_SET);

	int8_t *smpDataPtr = &song->sampleData[s->offset];

	if (bitsPerSample == 8) // 8-BIT INTEGER SAMPLE
	{
		if (sampleLength > config.maxSampleLength*4)
			sampleLength = config.maxSampleLength*4;

		uint8_t *audioDataU8 = (uint8_t *)malloc(sampleLength * sizeof (uint8_t));
		if (audioDataU8 == NULL)
		{
			statusOutOfMemory();
			return false;
		}

		// read sample data
		if (fread(audioDataU8, 1, sampleLength, f) != (size_t)sampleLength)
		{
			free(audioDataU8);
			displayErrorMsg("I/O ERROR !");
			return false;
		}

		// convert from stereo to mono (if needed)
		if (numChannels == 2)
		{
			sampleLength >>= 1;
			for (int32_t i = 0; i < sampleLength-1; i++) // add right channel to left channel
			{
				int32_t smp32 = (audioDataU8[(i << 1) + 0] - 128) + (audioDataU8[(i << 1) + 1] - 128);
				smp32 = 128 + (smp32 >> 1);
				audioDataU8[i] = (uint8_t)smp32;
			}
		}

		// 2x downsampling
		if (downSample)
		{
			downsample2x8BitU(audioDataU8, sampleLength);
			sampleLength >>= 1;
		}

		if (sampleLength > config.maxSampleLength)
			sampleLength = config.maxSampleLength;

		turnOffVoices();
		for (int32_t i = 0; i < sampleLength; i++)
			smpDataPtr[i] = audioDataU8[i] - 128;

		free(audioDataU8);
	}
	else if (bitsPerSample == 16) // 16-BIT INTEGER SAMPLE
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

		// convert from stereo to mono (if needed)
		if (numChannels == 2)
		{
			sampleLength >>= 1;
			for (int32_t i = 0; i < sampleLength-1; i++) // add right channel to left channel
				audioDataS16[i] = (audioDataS16[(i << 1) + 0] + audioDataS16[(i << 1) + 1]) >> 1;;
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
	else if (bitsPerSample == 24) // 24-BIT INTEGER SAMPLE
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
		uint8_t *audioDataU8 = (uint8_t *)audioDataS32;
		for (int32_t i = 0; i < sampleLength; i++)
		{
			audioDataU8[0] = 0;
			fread(&audioDataU8[1], 3, 1, f);
			audioDataU8 += sizeof (int32_t);
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
			assert(smp32 >= -128 && smp32 <= 127); // shouldn't happen according to dAmp (but just in case)
			smpDataPtr[i] = (int8_t)smp32;
		}

		free(audioDataS32);
	}
	else if (audioFormat == WAV_FORMAT_PCM && bitsPerSample == 32) // 32-BIT INTEGER SAMPLE
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
			assert(smp32 >= -128 && smp32 <= 127); // shouldn't happen according to dAmp (but just in case)
			smpDataPtr[i] = (int8_t)smp32;
		}

		free(audioDataS32);
	}
	else if (audioFormat == WAV_FORMAT_IEEE_FLOAT && bitsPerSample == 32) // 32-BIT FLOATING POINT SAMPLE
	{
		sampleLength >>= 2;
		if (sampleLength > config.maxSampleLength*4)
			sampleLength = config.maxSampleLength*4;

		uint32_t *audioDataU32 = (uint32_t *)malloc(sampleLength * sizeof (uint32_t));
		if (audioDataU32 == NULL)
		{
			statusOutOfMemory();
			return false;
		}

		// read sample data
		if (fread(audioDataU32, 4, sampleLength, f) != (size_t)sampleLength)
		{
			free(audioDataU32);
			displayErrorMsg("I/O ERROR !");
			return false;
		}

		float *fAudioDataFloat = (float *)audioDataU32;

		// convert from stereo to mono (if needed)
		if (numChannels == 2)
		{
			sampleLength >>= 1;
			for (int32_t i = 0; i < sampleLength-1; i++) // add right channel to left channel
				fAudioDataFloat[i] = (fAudioDataFloat[(i * 2) + 0] + fAudioDataFloat[(i * 2) + 1]) * 0.5f;
		}

		// 2x downsampling
		if (downSample)
		{
			downsample2xFloat(fAudioDataFloat, sampleLength);
			sampleLength >>= 1;
		}

		if (sampleLength > config.maxSampleLength)
			sampleLength = config.maxSampleLength;

		float fAmp = 1.0f;
		const float fPeak = getFloatPeak(fAudioDataFloat, sampleLength);
		if (fPeak > 0.0f)
			fAmp = INT8_MAX / fPeak;

		turnOffVoices();
		for (int32_t i = 0; i < sampleLength; i++)
		{
			int32_t smp32 = (int32_t)roundf(fAudioDataFloat[i] * fAmp);
			assert(smp32 >= -128 && smp32 <= 127); // shouldn't happen according to dAmp (but just in case)
			smpDataPtr[i] = (int8_t)smp32;
		}

		free(audioDataU32);
	}
	else if (audioFormat == WAV_FORMAT_IEEE_FLOAT && bitsPerSample == 64) // 64-BIT FLOATING POINT SAMPLE
	{
		sampleLength >>= 3;
		if (sampleLength > config.maxSampleLength*4)
			sampleLength = config.maxSampleLength*4;

		uint32_t *audioDataU32 = (uint32_t *)malloc(sampleLength * (sizeof (uint32_t) * 2));
		if (audioDataU32 == NULL)
		{
			statusOutOfMemory();
			return false;
		}

		// read sample data
		if (fread(audioDataU32, 8, sampleLength, f) != (size_t)sampleLength)
		{
			free(audioDataU32);
			displayErrorMsg("I/O ERROR !");
			return false;
		}

		double *dAudioDataDouble = (double *)audioDataU32;

		// convert from stereo to mono (if needed)
		if (numChannels == 2)
		{
			sampleLength >>= 1;
			for (int32_t i = 0; i < sampleLength-1; i++) // add right channel to left channel
				dAudioDataDouble[i] = (dAudioDataDouble[(i * 2) + 0] + dAudioDataDouble[(i * 2) + 1]) * 0.5;
		}

		// 2x downsampling
		if (downSample)
		{
			downsample2xDouble(dAudioDataDouble, sampleLength);
			sampleLength >>= 1;
		}

		if (sampleLength > config.maxSampleLength)
			sampleLength = config.maxSampleLength;

		double dAmp = 1.0;
		const double dPeak = getDoublePeak(dAudioDataDouble, sampleLength);
		if (dPeak > 0.0)
			dAmp = INT8_MAX / dPeak;

		turnOffVoices();
		for (int32_t i = 0; i < sampleLength; i++)
		{
			int32_t smp32 = (int32_t)round(dAudioDataDouble[i] * dAmp);
			assert(smp32 >= -128 && smp32 <= 127); // shouldn't happen according to dAmp (but just in case)
			smpDataPtr[i] = (int8_t)smp32;
		}

		free(audioDataU32);
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

	// ---- READ "smpl" chunk ----
	if (smplPtr != 0 && smplLen > 52)
	{
		int32_t loopStart, loopEnd;
		uint32_t loopFlags;

		fseek(f, smplPtr + 28, SEEK_SET); // seek to first wanted byte

		fread(&loopFlags, 4, 1, f);
		fseek(f, 12, SEEK_CUR);
		fread(&loopStart, 4, 1, f);
		fread(&loopEnd, 4, 1, f);
		loopEnd++;

		if (loopFlags) // loop enabled?
		{
			int32_t loopLength = loopEnd - loopStart;

			if (downSample)
			{
				// we already downsampled 2x, so we're half the original length
				loopStart >>= 1;
				loopLength >>= 1;
			}

			loopStart &= ~1;
			loopLength &= ~1;

			if (loopLength < 2 || loopStart+loopLength >= s->length)
			{
				loopStart = 0;
				loopLength = 2;
			}

			s->loopStart = loopStart;
			s->loopLength = loopLength;
		}
	}
	// ---------------------------

	// ---- READ "xtra" chunk ----
	if (xtraPtr != 0 && xtraLen >= 8)
	{
		uint16_t tempVol;

		fseek(f, xtraPtr + 4, SEEK_SET); // seek to first wanted byte

		// volume (0..256)
		fseek(f, 2, SEEK_CUR);
		fread(&tempVol, 2, 1, f);
		if (tempVol > 256)
			tempVol = 256;

		tempVol >>= 2; // 0..256 -> 0..64

		s->volume = (int8_t)tempVol;
	}
	// ---------------------------

	// ---- READ "INAM" chunk ----
	if (inamPtr != 0 && inamLen > 0)
	{
		fseek(f, inamPtr, SEEK_SET); // seek to first wanted byte

		for (int32_t i = 0; i < 21; i++)
		{
			if (i < inamLen)
				s->text[i] = (char)fgetc(f);
			else
				s->text[i] = '\0';
		}

		s->text[21] = '\0';
		s->text[22] = '\0';
	}
	// ---------------------------

	return true;
}
