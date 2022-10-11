// for finding memory leaks in debug mode with Visual Studio 
#if defined _DEBUG && defined _MSC_VER
#include <crtdbg.h>
#endif

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include "pt2_textout.h"
#include "pt2_sampler.h" // fixSampleBeep()
#include "pt2_audio.h"
#include "pt2_visuals.h"
#include "pt2_helpers.h"
#include "pt2_config.h"
#include "pt2_sampling.h"
#include "pt2_downsample2x.h"
#include "pt2_askbox.h"
#include "pt2_replayer.h"

enum
{
	WAV_FORMAT_PCM = 0x0001,
	WAV_FORMAT_IEEE_FLOAT = 0x0003
};

enum
{
	SAMPLE_IFF = 0,
	SAMPLE_AIFF = 1,
	SAMPLE_WAV = 2
};

static bool loadWAVSample(UNICHAR *fileName, char *entryName)
{
	uint16_t audioFormat, numChannels, bitsPerSample;
	uint32_t sampleRate;

	// zero out chunk pointers and lengths
	uint32_t fmtPtr  = 0; uint32_t fmtLen = 0;
	uint32_t dataPtr = 0; uint32_t dataLen = 0;
	uint32_t inamPtr = 0;  int32_t inamLen = 0;
	uint32_t xtraPtr = 0; uint32_t xtraLen = 0;
	uint32_t smplPtr = 0; uint32_t smplLen = 0;

	bool wavSampleNameFound = false;

	moduleSample_t *s = &song->samples[editor.currSample];

	FILE *f = UNICHAR_FOPEN(fileName, "rb");
	if (f == NULL)
	{
		displayErrorMsg("FILE I/O ERROR !");
		return false;
	}

	fseek(f, 0, SEEK_END);
	uint32_t fileSize = ftell(f);
	if (fileSize == 0)
	{
		fclose(f);

		displayErrorMsg("NOT A WAV !");
		return false;
	}

	// look for wanted chunks and set up pointers + lengths
	fseek(f, 12, SEEK_SET);

	uint32_t bytesRead = 0;
	while (!feof(f) && bytesRead < fileSize-12)
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
						while (!feof(f) && (bytesRead < chunkSize))
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

							bytesRead += (chunkSize + (chunkSize & 1));
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
	if ((fmtPtr == 0 || fmtLen < 16) || (dataPtr == 0 || dataLen == 0))
	{
		fclose(f);
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

	if (sampleRate == 0 || sampleLength == 0 || sampleLength >= (int32_t)fileSize*(bitsPerSample/8))
	{
		fclose(f);
		displayErrorMsg("WAV CORRUPT !");
		return false;
	}

	if (audioFormat != WAV_FORMAT_PCM && audioFormat != WAV_FORMAT_IEEE_FLOAT)
	{
		fclose(f);
		displayErrorMsg("WAV UNSUPPORTED !");
		return false;
	}

	if ((numChannels == 0) || (numChannels > 2))
	{
		fclose(f);
		displayErrorMsg("WAV UNSUPPORTED !");
		return false;
	}

	if (audioFormat == WAV_FORMAT_IEEE_FLOAT && bitsPerSample != 32 && bitsPerSample != 64)
	{
		fclose(f);
		displayErrorMsg("WAV UNSUPPORTED !");
		return false;
	}

	if (bitsPerSample != 8 && bitsPerSample != 16 && bitsPerSample != 24 && bitsPerSample != 32 && bitsPerSample != 64)
	{
		fclose(f);
		displayErrorMsg("WAV UNSUPPORTED !");
		return false;
	}

	bool forceDownSampling = false;
	if (sampleRate > 22050 && !config.noDownsampleOnSmpLoad)
	{
		if (askBox(ASKBOX_DOWNSAMPLE, "DOWNSAMPLE ?"))
			forceDownSampling = true;
	}

	// ---- READ SAMPLE DATA ----
	fseek(f, dataPtr, SEEK_SET);

	int8_t *smpPtr = &song->sampleData[editor.currSample * config.maxSampleLength];

	if (bitsPerSample == 8) // 8-BIT INTEGER SAMPLE
	{
		if (sampleLength > config.maxSampleLength*4)
			sampleLength = config.maxSampleLength*4;

		uint8_t *audioDataU8 = (uint8_t *)malloc(sampleLength * sizeof (uint8_t));
		if (audioDataU8 == NULL)
		{
			fclose(f);
			statusOutOfMemory();
			return false;
		}

		// read sample data
		if (fread(audioDataU8, 1, sampleLength, f) != (size_t)sampleLength)
		{
			fclose(f);
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
		if (forceDownSampling)
		{
			downsample2x8BitU(audioDataU8, sampleLength);
			sampleLength >>= 1;
		}

		if (sampleLength > config.maxSampleLength)
			sampleLength = config.maxSampleLength;

		turnOffVoices();
		for (int32_t i = 0; i < sampleLength; i++)
			smpPtr[i] = audioDataU8[i] - 128;

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
			fclose(f);
			statusOutOfMemory();
			return false;
		}

		// read sample data
		if (fread(audioDataS16, 2, sampleLength, f) != (size_t)sampleLength)
		{
			fclose(f);
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
		if (forceDownSampling)
		{
			downsample2x16Bit(audioDataS16, sampleLength);
			sampleLength >>= 1;
		}

		if (sampleLength > config.maxSampleLength)
			sampleLength = config.maxSampleLength;

		double dAmp = 1.0;
		if (forceDownSampling) // we already normalized
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
			smpPtr[i] = (int8_t)smp32;
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
			fclose(f);
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
		if (forceDownSampling)
		{
			downsample2x32Bit(audioDataS32, sampleLength);
			sampleLength >>= 1;
		}

		if (sampleLength > config.maxSampleLength)
			sampleLength = config.maxSampleLength;

		double dAmp = 1.0;
		if (forceDownSampling) // we already normalized
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
			smpPtr[i] = (int8_t)smp32;
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
			fclose(f);
			statusOutOfMemory();
			return false;
		}

		// read sample data
		if (fread(audioDataS32, 4, sampleLength, f) != (size_t)sampleLength)
		{
			fclose(f);
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
		if (forceDownSampling)
		{
			downsample2x32Bit(audioDataS32, sampleLength);
			sampleLength >>= 1;
		}

		if (sampleLength > config.maxSampleLength)
			sampleLength = config.maxSampleLength;

		double dAmp = 1.0;
		if (forceDownSampling) // we already normalized
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
			smpPtr[i] = (int8_t)smp32;
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
			fclose(f);
			statusOutOfMemory();
			return false;
		}

		// read sample data
		if (fread(audioDataU32, 4, sampleLength, f) != (size_t)sampleLength)
		{
			fclose(f);
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
		if (forceDownSampling)
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
			smpPtr[i] = (int8_t)smp32;
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
			fclose(f);
			statusOutOfMemory();
			return false;
		}

		// read sample data
		if (fread(audioDataU32, 8, sampleLength, f) != (size_t)sampleLength)
		{
			fclose(f);
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
		if (forceDownSampling)
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
			smpPtr[i] = (int8_t)smp32;
		}

		free(audioDataU32);
	}

	if (sampleLength < config.maxSampleLength) // clear rest of sample data
		memset(&song->sampleData[s->offset + sampleLength], 0, config.maxSampleLength - sampleLength);

	// set sample length
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

		if (forceDownSampling)
		{
			// we already downsampled 2x, so we're half the original length
			loopStart >>= 1;
			loopEnd >>= 1;
		}

		loopStart &= ~1;
		loopEnd &= ~1;

		if (loopFlags)
		{
			if (loopStart+(loopEnd-loopStart) <= s->length)
			{
				s->loopStart = loopStart;
				s->loopLength = loopEnd - loopStart;

				if (s->loopLength < 2)
				{
					s->loopStart = 0;
					s->loopLength = 2;
				}
			}
		}
	}
	// ---------------------------

	// ---- READ "xtra" chunk ----
	if (xtraPtr != 0 && xtraLen >= 8)
	{
		int16_t tempVol;

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

		wavSampleNameFound = true;
	}
	// ---------------------------

	fclose(f);

	// copy over sample name
	if (!wavSampleNameFound)
	{
		int32_t nameLen = (int32_t)strlen(entryName);
		for (int32_t i = 0; i < 21; i++)
			s->text[i] = (i < nameLen) ? (char)entryName[i] : '\0';

		s->text[21] = '\0';
		s->text[22] = '\0';
	}

	// remove .wav from end of sample name (if present)
	int32_t nameLen = (uint32_t)strlen(s->text);
	if (nameLen >= 4 && !_strnicmp(&s->text[nameLen-4], ".WAV", 4))
		 memset(&s->text[nameLen-4], '\0', 4);

	editor.sampleZero = false;
	editor.samplePos = 0;

	fixSampleBeep(s);
	fillSampleRedoBuffer(editor.currSample);

	if (ui.samplingBoxShown)
	{
		removeSamplingBox();
		ui.samplingBoxShown = false;
	}

	updateCurrSample();

	updateWindowTitle(MOD_IS_MODIFIED);
	return true;
}

static bool loadIFFSample(UNICHAR *fileName, char *entryName)
{
	char tmpCharBuf[23];
	uint16_t sampleRate;
	int32_t sampleLoopStart, sampleLoopLength;

	// zero out chunk pointers and lengths
	uint32_t vhdrPtr = 0; uint32_t vhdrLen = 0;
	uint32_t bodyPtr = 0; uint32_t bodyLen = 0;
	uint32_t namePtr = 0;  int32_t nameLen = 0;

	moduleSample_t *s = &song->samples[editor.currSample];

	FILE *f = UNICHAR_FOPEN(fileName, "rb");
	if (f == NULL)
	{
		displayErrorMsg("FILE I/O ERROR !");
		return false;
	}

	fseek(f, 0, SEEK_END);
	int32_t fileSize = ftell(f);
	if (fileSize == 0)
	{
		displayErrorMsg("IFF IS CORRUPT !");
		return false;
	}

	fseek(f, 8, SEEK_SET);
	fread(tmpCharBuf, 1, 4, f);
	bool is16Bit = !strncmp(tmpCharBuf, "16SV", 4);

	int32_t sampleLength = 0;
	bool nameFound = false;
	uint32_t sampleVolume = 65536; // max volume

	fseek(f, 12, SEEK_SET);
	while (!feof(f) && ftell(f) < fileSize-12)
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
		fclose(f);
		displayErrorMsg("NOT A VALID IFF !");
		return false;
	}

	// kludge for some really strange IFFs
	if (bodyLen == 0)
		bodyLen = fileSize - bodyPtr;

	if (bodyPtr+bodyLen > (uint32_t)fileSize)
		bodyLen = fileSize - bodyPtr;

	fseek(f, vhdrPtr, SEEK_SET);
	fread(&sampleLoopStart,  4, 1, f); sampleLoopStart  = SWAP32(sampleLoopStart);
	fread(&sampleLoopLength, 4, 1, f); sampleLoopLength = SWAP32(sampleLoopLength);
	fseek(f, 4, SEEK_CUR);
	fread(&sampleRate, 2, 1, f); sampleRate = SWAP16(sampleRate);
	fseek(f, 1, SEEK_CUR);

	if (fgetc(f) != 0) // sample type
	{
		fclose(f);
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
		fclose(f);
		displayErrorMsg("NOT A VALID IFF !");
		return false;
	}

	bool forceDownSampling = false;
	if (sampleRate > 22050 && !config.noDownsampleOnSmpLoad)
	{
		if (askBox(ASKBOX_DOWNSAMPLE, "DOWNSAMPLE ?"))
			forceDownSampling = true;
	}

	int32_t maxSampleLength = config.maxSampleLength;
	if (is16Bit)
		maxSampleLength *= 2;

	if (forceDownSampling)
		maxSampleLength *= 2;

	if (sampleLength > maxSampleLength)
		sampleLength = maxSampleLength;

	int8_t *sampleData = (int8_t *)malloc(sampleLength);
	if (sampleData == NULL)
	{
		fclose(f);
		statusOutOfMemory();
		return false;
	}

	if (is16Bit)
	{
		sampleLength >>= 1;
		sampleLoopStart >>= 1;
		sampleLoopLength >>= 1;
	}

	if (forceDownSampling)
	{
		sampleLoopStart >>= 1;
		sampleLoopLength >>= 1;
	}

	turnOffVoices();

	fseek(f, bodyPtr, SEEK_SET);
	if (is16Bit) // FT2-specific 16SV format (little-endian samples)
	{
		fread(sampleData, 1, sampleLength << 1, f);
		int16_t *ptr16 = (int16_t *)sampleData;

		// 2x downsampling
		if (forceDownSampling)
		{
			downsample2x16Bit(ptr16, sampleLength);
			sampleLength >>= 1;
		}

		if (sampleLength > config.maxSampleLength)
			sampleLength = config.maxSampleLength;

		double dAmp = 1.0;
		if (forceDownSampling) // we already normalized
		{
			dAmp = INT8_MAX / (double)INT16_MAX;
		}
		else
		{
			const double dPeak = get16BitPeak(ptr16, sampleLength);
			if (dPeak > 0.0)
				dAmp = INT8_MAX / dPeak;
		}

		int8_t *smpPtr = &song->sampleData[s->offset];
		for (int32_t i = 0; i < sampleLength; i++)
		{
			int32_t smp32 = (int32_t)round(ptr16[i] * dAmp);
			assert(smp32 >= -128 && smp32 <= 127); // shouldn't happen according to dAmp (but just in case)
			smpPtr[i] = (int8_t)smp32;
		}
	}
	else
	{
		fread(sampleData, 1, sampleLength, f);

		// 2x downsampling
		if (forceDownSampling)
		{
			downsample2x8Bit(sampleData, sampleLength);
			sampleLength >>= 1;
		}

		if (sampleLength > config.maxSampleLength)
			sampleLength = config.maxSampleLength;

		memcpy(&song->sampleData[s->offset], sampleData, sampleLength);
	}

	free(sampleData);

	if (sampleLength < config.maxSampleLength) // clear rest of sample data
		memset(&song->sampleData[s->offset + sampleLength], 0, config.maxSampleLength - sampleLength);

	// set sample length
	if (sampleLength & 1)
	{
		if (++sampleLength > config.maxSampleLength)
			sampleLength = config.maxSampleLength;
	}

	sampleLoopStart &= ~1;
	sampleLoopLength &= ~1;

	if (sampleLoopLength < 2)
	{
		sampleLoopStart = 0;
		sampleLoopLength = 2;
	}

	if (sampleLoopStart >= sampleLength || sampleLoopLength > sampleLength)
	{
		sampleLoopStart = 0;
		sampleLoopLength = 2;
	}

	if (sampleLoopStart+sampleLoopLength > sampleLength)
	{
		sampleLoopStart = 0;
		sampleLoopLength = 2;
	}

	if (sampleLoopStart > sampleLength-2)
	{
		sampleLoopStart = 0;
		sampleLoopLength = 2;
	}

	// set sample attributes
	s->volume = (int8_t)sampleVolume;
	s->fineTune = 0;
	s->length = sampleLength;
	s->loopStart = sampleLoopStart;
	s->loopLength = sampleLoopLength;

	// read name
	if (namePtr != 0 && nameLen > 0)
	{
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

		nameFound = true;
	}

	fclose(f);

	// copy over sample name
	memset(s->text, '\0', sizeof (s->text));

	if (nameFound)
	{
		nameLen = (uint32_t)strlen(tmpCharBuf);
		if (nameLen > 21)
			nameLen = 21;

		memcpy(s->text, tmpCharBuf, nameLen);
	}
	else
	{
		nameLen = (uint32_t)strlen(entryName);
		if (nameLen > 21)
			nameLen = 21;

		memcpy(s->text, entryName, nameLen);
	}

	// remove .iff from end of sample name (if present)
	nameLen = (uint32_t)strlen(s->text);
	if (nameLen >= 4 && !strncmp(&s->text[nameLen-4], ".IFF", 4))
		memset(&s->text[nameLen-4], '\0', 4);

	editor.sampleZero = false;
	editor.samplePos = 0;

	fixSampleBeep(s);
	fillSampleRedoBuffer(editor.currSample);
	updateCurrSample();

	updateWindowTitle(MOD_IS_MODIFIED);
	return false;
}

static bool loadRAWSample(UNICHAR *fileName, char *entryName)
{
	moduleSample_t *s = &song->samples[editor.currSample];

	FILE *f = UNICHAR_FOPEN(fileName, "rb");
	if (f == NULL)
	{
		displayErrorMsg("FILE I/O ERROR !");
		return false;
	}

	fseek(f, 0, SEEK_END);
	int32_t fileSize = ftell(f);
	fseek(f, 0, SEEK_SET);

	fileSize &= ~1;
	if (fileSize > config.maxSampleLength)
		fileSize = config.maxSampleLength;

	turnOffVoices();

	fread(&song->sampleData[s->offset], 1, fileSize, f);
	fclose(f);

	if (fileSize < config.maxSampleLength)
		memset(&song->sampleData[s->offset + fileSize], 0, config.maxSampleLength - fileSize);

	// set sample attributes
	s->volume = 64;
	s->fineTune = 0;
	s->length = fileSize;
	s->loopStart = 0;
	s->loopLength = 2;

	// copy over sample name
	int32_t nameLen = (uint32_t)strlen(entryName);
	for (int32_t i = 0; i < 21; i++)
		s->text[i] = (i < nameLen) ? (char)entryName[i] : '\0';

	s->text[21] = '\0';
	s->text[22] = '\0';

	editor.sampleZero = false;
	editor.samplePos = 0;

	fixSampleBeep(s);
	fillSampleRedoBuffer(editor.currSample);

	if (ui.samplingBoxShown)
	{
		removeSamplingBox();
		ui.samplingBoxShown = false;
	}

	updateCurrSample();

	updateWindowTitle(MOD_IS_MODIFIED);
	return true;
}

static int32_t getAIFFRate(uint8_t *in)
{
	int32_t exp = (int32_t)(((in[0] & 0x7F) << 8) | in[1]);
	uint32_t lo = (in[2] << 24) | (in[3] << 16) | (in[4] << 8) | in[5];
	uint32_t hi = (in[6] << 24) | (in[7] << 16) | (in[8] << 8) | in[9];

	if (exp == 0 && lo == 0 && hi == 0)
		return 0;

	exp -= 16383;

	double dOut = ldexp(lo, -31 + exp) + ldexp(hi, -63 + exp);
	return (int32_t)(dOut + 0.5);
}

static bool loadAIFFSample(UNICHAR *fileName, char *entryName)
{
	uint8_t sampleRateBytes[10];
	uint16_t bitDepth, numChannels;
	uint32_t offset, sampleRate;

	// zero out chunk pointers and lengths
	uint32_t commPtr = 0; uint32_t commLen = 0;
	uint32_t ssndPtr = 0; uint32_t ssndLen = 0;

	bool unsigned8bit = false;

	moduleSample_t *s = &song->samples[editor.currSample];

	FILE *f = UNICHAR_FOPEN(fileName, "rb");
	if (f == NULL)
	{
		displayErrorMsg("FILE I/O ERROR !");
		return false;
	}

	fseek(f, 0, SEEK_END);
	int32_t fileSize = ftell(f);
	if (fileSize == 0)
	{
		displayErrorMsg("AIFF IS CORRUPT !");
		return false;
	}

	fseek(f, 12, SEEK_SET);
	while (!feof(f) && ftell(f) < fileSize-12)
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
		fclose(f);
		displayErrorMsg("NOT A VALID AIFF!");
		return false;
	}

	// kludge for some really strange AIFFs
	if (ssndLen == 0)
		ssndLen = fileSize - ssndPtr;

	if (ssndPtr+ssndLen > (uint32_t)fileSize)
		ssndLen = fileSize - ssndPtr;

	fseek(f, commPtr, SEEK_SET);
	fread(&numChannels, 2, 1, f); numChannels = SWAP16(numChannels);
	fseek(f, 4, SEEK_CUR);
	fread(&bitDepth, 2, 1, f); bitDepth = SWAP16(bitDepth);
	fread(sampleRateBytes, 1, 10, f);

	fseek(f, 4 + 2 + 1, SEEK_CUR);

	if (numChannels != 1 && numChannels != 2) // sample type
	{
		fclose(f);
		displayErrorMsg("UNSUPPORTED AIFF!");
		return false;
	}

	if (bitDepth != 8 && bitDepth != 16 && bitDepth != 24 && bitDepth != 32)
	{
		fclose(f);
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
			fclose(f);
			displayErrorMsg("UNSUPPORTED AIFF!");
			return false;
		}
	}

	sampleRate = getAIFFRate(sampleRateBytes);

	// sample data chunk

	fseek(f, ssndPtr, SEEK_SET);

	fread(&offset, 4, 1, f);
	if (offset > 0)
	{
		fclose(f);
		displayErrorMsg("UNSUPPORTED AIFF!");
		return false;
	}

	fseek(f, 4, SEEK_CUR);

	ssndLen -= 8; // don't include offset and blockSize datas

	int32_t sampleLength = ssndLen;
	if (sampleLength == 0)
	{
		fclose(f);
		displayErrorMsg("NOT A VALID AIFF!");
		return false;
	}

	bool forceDownSampling = false;
	if (sampleRate > 22050 && !config.noDownsampleOnSmpLoad)
	{
		if (askBox(ASKBOX_DOWNSAMPLE, "DOWNSAMPLE ?"))
			forceDownSampling = true;
	}

	int8_t *smpPtr = &song->sampleData[editor.currSample * config.maxSampleLength];

	if (bitDepth == 8) // 8-BIT INTEGER SAMPLE
	{
		if (sampleLength > config.maxSampleLength*4)
			sampleLength = config.maxSampleLength*4;

		int8_t *audioDataS8 = (int8_t *)malloc(sampleLength * sizeof (int8_t));
		if (audioDataS8 == NULL)
		{
			fclose(f);
			statusOutOfMemory();
			return false;
		}

		// read sample data
		if (fread(audioDataS8, 1, sampleLength, f) != (size_t)sampleLength)
		{
			fclose(f);
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
		if (forceDownSampling)
		{
			downsample2x8Bit(audioDataS8, sampleLength);
			sampleLength >>= 1;
		}

		if (sampleLength > config.maxSampleLength)
			sampleLength = config.maxSampleLength;

		turnOffVoices();
		for (int32_t i = 0; i < sampleLength; i++)
			smpPtr[i] = audioDataS8[i];

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
			fclose(f);
			statusOutOfMemory();
			return false;
		}

		// read sample data
		if (fread(audioDataS16, 2, sampleLength, f) != (size_t)sampleLength)
		{
			fclose(f);
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
		if (forceDownSampling)
		{
			downsample2x16Bit(audioDataS16, sampleLength);
			sampleLength >>= 1;
		}

		if (sampleLength > config.maxSampleLength)
			sampleLength = config.maxSampleLength;

		double dAmp = 1.0;
		if (forceDownSampling) // we already normalized
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
			smpPtr[i] = (int8_t)smp32;
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
			fclose(f);
			statusOutOfMemory();
			return false;
		}

		// read sample data
		if (fread(&audioDataS32[sampleLength >> 2], 3, sampleLength, f) != (size_t)sampleLength)
		{
			fclose(f);
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
		if (forceDownSampling)
		{
			downsample2x32Bit(audioDataS32, sampleLength);
			sampleLength >>= 1;
		}

		if (sampleLength > config.maxSampleLength)
			sampleLength = config.maxSampleLength;

		double dAmp = 1.0;
		if (forceDownSampling) // we already normalized
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
			smpPtr[i] = (int8_t)smp32;
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
			fclose(f);
			statusOutOfMemory();
			return false;
		}

		// read sample data
		if (fread(audioDataS32, 4, sampleLength, f) != (size_t)sampleLength)
		{
			fclose(f);
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
		if (forceDownSampling)
		{
			downsample2x32Bit(audioDataS32, sampleLength);
			sampleLength >>= 1;
		}

		if (sampleLength > config.maxSampleLength)
			sampleLength = config.maxSampleLength;

		double dAmp = 1.0;
		if (forceDownSampling) // we already normalized
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
			smpPtr[i] = (int8_t)smp32;
		}

		free(audioDataS32);
	}

	if (sampleLength < config.maxSampleLength) // clear rest of sample data
		memset(&song->sampleData[s->offset + sampleLength], 0, config.maxSampleLength - sampleLength);

	// set sample length
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

	fclose(f);

	// copy over sample name
	int32_t nameLen = (int32_t)strlen(entryName);
	for (int32_t i = 0; i < 21; i++)
		s->text[i] = (i < nameLen) ? (char)entryName[i] : '\0';

	s->text[21] = '\0';
	s->text[22] = '\0';

	// remove .aiff from end of sample name (if present)
	nameLen = (uint32_t)strlen(s->text);
	if (nameLen >= 5 && !_strnicmp(&s->text[nameLen-5], ".AIFF", 5))
		memset(&s->text[nameLen-5], '\0', 5);

	editor.sampleZero = false;
	editor.samplePos = 0;

	fixSampleBeep(s);
	fillSampleRedoBuffer(editor.currSample);

	if (ui.samplingBoxShown)
	{
		removeSamplingBox();
		ui.samplingBoxShown = false;
	}

	updateCurrSample();

	updateWindowTitle(MOD_IS_MODIFIED);
	return true;
}

bool loadSample(UNICHAR *fileName, char *entryName)
{
	if (editor.sampleZero)
	{
		statusNotSampleZero();
		return false;
	}

	FILE *f = UNICHAR_FOPEN(fileName, "rb");
	if (f == NULL)
	{
		displayErrorMsg("FILE I/O ERROR !");
		return false;
	}

	fseek(f, 0, SEEK_END);
	uint32_t fileSize = ftell(f);
	fseek(f, 0, SEEK_SET);

	// first, check heades before we eventually load as RAW
	if (fileSize > 16)
	{
		uint32_t ID;
		fread(&ID, 4, 1, f);

		/* Reject FLAC files, since they are not supported.
		** This is a dumb test since any sample *could* start
		** with "fLaC", but in 99.999% of cases it won't happen.
		*/
		if (ID == 0x43614C66) // "fLaC"
		{
			displayErrorMsg("NOT SUPPORTED !");
			return false;
		}

		// check if it's actually a WAV sample
		if (ID == 0x46464952) // "RIFF"
		{
			fseek(f, 4, SEEK_CUR);
			fread(&ID, 4, 1, f);

			if (ID == 0x45564157) // "WAVE"
			{
				fclose(f);
				return loadWAVSample(fileName, entryName);
			}
		}
		else if (ID == 0x4D524F46) // "FORM"
		{
			fseek(f, 4, SEEK_CUR);
			fread(&ID, 4, 1, f);

			// check if it's an Amiga IFF sample
			if (ID == 0x58565338 || ID == 0x56533631) // "8SVX" (normal) and "16SV" (FT2 sample)
			{
				fclose(f);
				return loadIFFSample(fileName, entryName);
			}

			// check if it's an AIFF sample
			else if (ID == 0x46464941) // "AIFF"
			{
				fclose(f);
				return loadAIFFSample(fileName, entryName);
			}

			else if (ID == 0x43464941) // "AIFC" (compressed AIFF)
			{
				fclose(f);
				displayErrorMsg("UNSUPPORTED AIFF!");
				return false;
			}
		}
	}

	// nope, continue loading as RAW
	fclose(f);

	return loadRAWSample(fileName, entryName);
}
