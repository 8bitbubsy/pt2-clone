#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <math.h>
#ifndef _WIN32
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#endif
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

#ifdef HAS_LIBFLAC

// hide POSIX warning for fileno()
#ifdef _MSC_VER
#pragma warning(disable: 4996)
#endif

#ifdef EXTERNAL_LIBFLAC
#include <FLAC/stream_decoder.h>
#else
#include "../libflac/FLAC/stream_decoder.h"
#endif

static bool loopEnabled;
static int8_t *smpBuf8;
static int16_t *smpBuf16;
static int32_t *smpBuf24, sampleLength, loopStart, loopLength;
static uint32_t numChannels, bitDepth, sampleRate, samplesRead;
static moduleSample_t *smp;

static FLAC__StreamDecoderReadStatus read_callback(const FLAC__StreamDecoder *decoder, FLAC__byte buffer[], size_t *bytes, void *client_data);
static FLAC__StreamDecoderSeekStatus seek_callback(const FLAC__StreamDecoder *decoder, FLAC__uint64 absolute_byte_offset, void *client_data);
static FLAC__StreamDecoderTellStatus tell_callback(const FLAC__StreamDecoder *decoder, FLAC__uint64 *absolute_byte_offset, void *client_data);
static FLAC__StreamDecoderLengthStatus length_callback(const FLAC__StreamDecoder *decoder, FLAC__uint64 *stream_length, void *client_data);
static FLAC__bool eof_callback(const FLAC__StreamDecoder *decoder, void *client_data);
static void metadata_callback(const FLAC__StreamDecoder *decoder, const FLAC__StreamMetadata *metadata, void *client_data);
static FLAC__StreamDecoderWriteStatus write_callback(const FLAC__StreamDecoder *decoder, const FLAC__Frame *frame, const FLAC__int32 *const buffer[], void *client_data);
static void error_callback(const FLAC__StreamDecoder *decoder, FLAC__StreamDecoderErrorStatus status, void *client_data);

bool loadFLACSample(FILE *f, uint32_t filesize, moduleSample_t *s)
{
	FLAC__StreamDecoder *decoder = FLAC__stream_decoder_new();
	if (decoder == NULL)
	{
		displayErrorMsg("FLAC LOAD ERROR !");
		goto error;
	}

	FLAC__stream_decoder_set_metadata_respond_all(decoder);

	FLAC__StreamDecoderInitStatus initStatus =
		FLAC__stream_decoder_init_stream
		(
			decoder,
			read_callback, seek_callback,
			tell_callback, length_callback,
			eof_callback, write_callback,
			metadata_callback, error_callback,
			f
		);

	if (initStatus != FLAC__STREAM_DECODER_INIT_STATUS_OK)
	{
		displayErrorMsg("FLAC LOAD ERROR !");
		goto error;
	}

	smp = s;
	s->volume = 64;
	loopStart = 0;
	loopLength = 2;
	smpBuf8 = NULL;
	smpBuf16 = NULL;
	smpBuf24 = NULL;

	FLAC__stream_decoder_process_until_end_of_stream(decoder);
	FLAC__stream_decoder_finish(decoder);
	FLAC__stream_decoder_delete(decoder);

	// sample has been read to smpBufX now (depending on bitdepth)

	bool downSample = false;
	if (sampleRate > 22050 && !config.noDownsampleOnSmpLoad)
	{
		if (askBox(ASKBOX_DOWNSAMPLE, "DOWNSAMPLE ?"))
			downSample = true;
	}

	int8_t *smpDataPtr = &song->sampleData[s->offset];

	if (bitDepth == 8)
	{
		if (downSample)
		{
			if (!downsample2x8Bit(smpBuf8, sampleLength))
			{
				statusOutOfMemory();
				goto error;
			}

			sampleLength /= 2;
		}

		turnOffVoices();
		memcpy(smpDataPtr, smpBuf8, sampleLength * sizeof (int8_t));
		free(smpBuf8);
	}
	else if (bitDepth == 16)
	{
		if (downSample)
		{
			if (!downsample2x16Bit(smpBuf16, sampleLength))
			{
				statusOutOfMemory();
				goto error;
			}

			sampleLength /= 2;
		}

		double dAmp = 1.0;
		if (downSample) // we already normalized
		{
			dAmp = INT8_MAX / (double)INT16_MAX;
		}
		else
		{
			const double dPeak = get16BitPeak(smpBuf16, sampleLength);
			if (dPeak > 0.0)
				dAmp = INT8_MAX / dPeak;
		}

		turnOffVoices();
		for (int32_t i = 0; i < sampleLength; i++)
		{
			int32_t smp32 = (int32_t)round(smpBuf16[i] * dAmp);
			assert(smp32 >= -128 && smp32 <= 127); // shouldn't happen according to dAmp (but just in case)
			smpDataPtr[i] = (int8_t)smp32;
		}

		free(smpBuf16);
	}
	else if (bitDepth == 24)
	{
		if (downSample)
		{
			if (!downsample2x32Bit(smpBuf24, sampleLength))
			{
				statusOutOfMemory();
				goto error;
			}

			sampleLength /= 2;
		}

		double dAmp = 1.0;
		if (downSample) // we already normalized
		{
			dAmp = INT8_MAX / (double)INT32_MAX;
		}
		else
		{
			const double dPeak = get32BitPeak(smpBuf24, sampleLength);
			if (dPeak > 0.0)
				dAmp = INT8_MAX / dPeak;
		}

		turnOffVoices();
		for (int32_t i = 0; i < sampleLength; i++)
		{
			int32_t smp32 = (int32_t)round(smpBuf24[i] * dAmp);
			assert(smp32 >= -128 && smp32 <= 127); // shouldn't happen according to dAmp (but just in case)
			smpDataPtr[i] = (int8_t)smp32;
		}

		free(smpBuf24);
	}

	if (sampleLength & 1)
	{
		if (++sampleLength > config.maxSampleLength)
			sampleLength = config.maxSampleLength;
	}

	if (downSample)
	{
		loopStart /= 2;
		loopLength /= 2;
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

	return true;

error:
	if (smpBuf8 != NULL) free(smpBuf8);
	if (smpBuf16 != NULL) free(smpBuf16);
	if (smpBuf24 != NULL) free(smpBuf24);
	if (decoder != NULL) FLAC__stream_decoder_delete(decoder);

	return false;

	(void)filesize;
}

static FLAC__StreamDecoderReadStatus read_callback(const FLAC__StreamDecoder *decoder, FLAC__byte buffer[], size_t *bytes, void *client_data)
{
	FILE *file = (FILE *)client_data;
	if (*bytes > 0)
	{
		*bytes = fread(buffer, sizeof (FLAC__byte), *bytes, file);
		if (ferror(file))
			return FLAC__STREAM_DECODER_READ_STATUS_ABORT;
		else if (*bytes == 0)
			return FLAC__STREAM_DECODER_READ_STATUS_END_OF_STREAM;
		else
			return FLAC__STREAM_DECODER_READ_STATUS_CONTINUE;
	}
	else
	{
		return FLAC__STREAM_DECODER_READ_STATUS_ABORT;
	}

	(void)decoder;
}

static FLAC__StreamDecoderSeekStatus seek_callback(const FLAC__StreamDecoder *decoder, FLAC__uint64 absolute_byte_offset, void *client_data)
{
	FILE *file = (FILE *)client_data;

	if (absolute_byte_offset > INT32_MAX)
		return FLAC__STREAM_DECODER_SEEK_STATUS_ERROR;

	if (fseek(file, (int32_t)absolute_byte_offset, SEEK_SET) < 0)
		return FLAC__STREAM_DECODER_SEEK_STATUS_ERROR;
	else
		return FLAC__STREAM_DECODER_SEEK_STATUS_OK;

	(void)decoder;
}

static FLAC__StreamDecoderTellStatus tell_callback(const FLAC__StreamDecoder *decoder, FLAC__uint64 *absolute_byte_offset, void *client_data)
{
	FILE *file = (FILE *)client_data;
	int32_t pos = ftell(file);

	if (pos < 0)
	{
		return FLAC__STREAM_DECODER_TELL_STATUS_ERROR;
	}
	else
	{
		*absolute_byte_offset = (FLAC__uint64)pos;
		return FLAC__STREAM_DECODER_TELL_STATUS_OK;
	}

	(void)decoder;
}

static FLAC__StreamDecoderLengthStatus length_callback(const FLAC__StreamDecoder *decoder, FLAC__uint64 *stream_length, void *client_data)
{
	FILE *file = (FILE *)client_data;
	struct stat filestats;

	if (fstat(fileno(file), &filestats) != 0)
	{
		return FLAC__STREAM_DECODER_LENGTH_STATUS_ERROR;
	}
	else
	{
		*stream_length = (FLAC__uint64)filestats.st_size;
		return FLAC__STREAM_DECODER_LENGTH_STATUS_OK;
	}

	(void)decoder;
}

static FLAC__bool eof_callback(const FLAC__StreamDecoder *decoder, void *client_data)
{
	FILE *file = (FILE *)client_data;
	return feof(file) ? true : false;

	(void)decoder;
}

static void metadata_callback(const FLAC__StreamDecoder *decoder, const FLAC__StreamMetadata *metadata, void *client_data)
{
	if (metadata->type == FLAC__METADATA_TYPE_STREAMINFO && metadata->data.stream_info.total_samples != 0)
	{
		bitDepth = metadata->data.stream_info.bits_per_sample;
		numChannels = metadata->data.stream_info.channels;
		sampleRate = metadata->data.stream_info.sample_rate;

		int64_t tmp64 = metadata->data.stream_info.total_samples;
		if (tmp64 > config.maxSampleLength*2)
			tmp64 = config.maxSampleLength*2;

		sampleLength = (int32_t)tmp64;

		if (bitDepth == 8)
			smpBuf8 = (int8_t *)calloc(1, config.maxSampleLength * 2 * sizeof (int8_t));
		else if (bitDepth == 16)
			smpBuf16 = (int16_t *)calloc(1, config.maxSampleLength * 2 * sizeof (int16_t));
		else if (bitDepth == 24)
			smpBuf24 = (int32_t *)calloc(1, config.maxSampleLength * 2 * sizeof (int32_t));

		// alloc result is tasted later
	}

	// check for RIFF chunks (loop/vol/pan information)
	else if (metadata->type == FLAC__METADATA_TYPE_APPLICATION && !memcmp(metadata->data.application.id, "riff", 4))
	{
		const uint8_t *data = (const uint8_t *)metadata->data.application.data;

		uint32_t chunkID  = *(uint32_t *)data; data += 4;
		uint32_t chunkLen = *(uint32_t *)data; data += 4;

		if (chunkID == 0x61727478 && chunkLen >= 8) // "xtra"
		{
			data += 6;

			// volume (0..256)
			uint16_t tmpVol = *(uint16_t *)data;
			if (tmpVol > 256)
				tmpVol = 256;

			smp->volume = (uint8_t)((tmpVol + 2) / 4); // 0..256 -> 0..64 (rounded)
		}

		if (chunkID == 0x6C706D73 && chunkLen > 52) // "smpl"
		{
			data += 28; // seek to first wanted byte

			uint32_t numLoops = *(uint32_t *)data; data += 4;
			if (numLoops == 1)
			{
				data += 4+4; // skip "samplerData" and "identifier"

				uint32_t loopType  = *(uint32_t *)data; data += 4;
				         loopStart = *(uint32_t *)data; data += 4;
				uint32_t loopEnd   = *(uint32_t *)data; data += 4;

				loopLength = (loopEnd+1) - loopStart;
				loopEnabled = (loopType == 0);
			}
		}
	}

	else if (metadata->type == FLAC__METADATA_TYPE_VORBIS_COMMENT)
	{
		uint32_t tmpSampleRate = 0;
		for (uint32_t i = 0; i < metadata->data.vorbis_comment.num_comments; i++)
		{
			const char *tag = (const char *)metadata->data.vorbis_comment.comments[i].entry;
			uint32_t length = metadata->data.vorbis_comment.comments[i].length;

			if (length > 6 && !memcmp(tag, "TITLE=", 6))
			{
				length -= 6;
				if (length > 22)
					length = 22;

				memcpy(smp->text, &tag[6], length);
				smp->text[22] = '\0';
			}

			// the following tags haven't been tested!
			else if (length > 11 && !memcmp(tag, "SAMPLERATE=", 11))
			{
				tmpSampleRate = atoi(&tag[11]);
			}
			else if (length > 10 && !memcmp(tag, "LOOPSTART=", 10))
			{
				loopStart = atoi(&tag[10]);
			}
			else if (length > 11 && !memcmp(tag, "LOOPLENGTH=", 11))
			{
				loopLength = atoi(&tag[11]);
			}

			if (tmpSampleRate > 0)
				sampleRate = tmpSampleRate;
		}
	}

	(void)client_data;
	(void)decoder;
}

static FLAC__StreamDecoderWriteStatus write_callback(const FLAC__StreamDecoder *decoder, const FLAC__Frame *frame, const FLAC__int32 *const buffer[], void *client_data)
{

	if (sampleLength == 0 || numChannels == 0)
	{
		displayErrorMsg("FLAC LOAD ERROR !");
		return FLAC__STREAM_DECODER_WRITE_STATUS_ABORT;
	}

	if (numChannels > 2)
	{
		displayErrorMsg("UNSUPPORTED FLAC !");
		return FLAC__STREAM_DECODER_WRITE_STATUS_ABORT;
	}

	if (bitDepth != 8 && bitDepth != 16 && bitDepth != 24)
	{
		displayErrorMsg("UNSUPPORTED FLAC !");
		return FLAC__STREAM_DECODER_WRITE_STATUS_ABORT;
	}

	if ((bitDepth == 8 && smpBuf8 == NULL) || (bitDepth == 16 && smpBuf16 == NULL) || (bitDepth == 24 && smpBuf24 == NULL))
	{
		statusOutOfMemory();
		return FLAC__STREAM_DECODER_WRITE_STATUS_ABORT;
	}

	if (frame->header.number.sample_number == 0)
		samplesRead = 0;

	uint32_t blockSize = frame->header.blocksize;

	bool doAbort = false;

	const uint32_t samplesAllocated = config.maxSampleLength * 2;
	if (samplesRead+blockSize >= samplesAllocated)
	{
		blockSize = samplesAllocated-samplesRead;
		doAbort = true;
	}
	
	if (blockSize > 0)
	{
		if (numChannels == 2) // mix to mono
		{
			const int32_t *src_L = buffer[0];
			const int32_t *src_R = buffer[1];

			if (bitDepth == 8)
			{
				int8_t *dst8 = smpBuf8 + samplesRead;
				for (uint32_t i = 0; i < blockSize; i++)
					dst8[i] = (int8_t)((src_L[i] + src_R[i]) >> 1);
			}
			else if (bitDepth == 16)
			{
				int16_t *dst16 = smpBuf16 + samplesRead;
				for (uint32_t i = 0; i < blockSize; i++)
					dst16[i] = (int16_t)((src_L[i] + src_R[i]) >> 1);
			}
			else if (bitDepth == 24)
			{
				int32_t *dst24 = smpBuf24 + samplesRead;
				for (uint32_t i = 0; i < blockSize; i++)
					dst24[i] = (int32_t)((src_L[i] + src_R[i]) >> 1);
			}
		}
		else // mono sample
		{
			const int32_t *src = buffer[0];

			if (bitDepth == 8)
			{
				int8_t *dst8 = smpBuf8 + samplesRead;
				for (uint32_t i = 0; i < blockSize; i++)
					dst8[i] = (int8_t)src[i];
			}
			else if (bitDepth == 16)
			{
				int16_t *dst16 = smpBuf16 + samplesRead;
				for (uint32_t i = 0; i < blockSize; i++)
					dst16[i] = (int16_t)src[i];
			}
			else if (bitDepth == 24)
			{
				int32_t *dst24 = smpBuf24 + samplesRead;
				for (uint32_t i = 0; i < blockSize; i++)
					dst24[i] = (int32_t)src[i];
			}
		}

		samplesRead += blockSize;
	}

	if (doAbort)
		return FLAC__STREAM_DECODER_WRITE_STATUS_ABORT;

	return FLAC__STREAM_DECODER_WRITE_STATUS_CONTINUE;

	(void)client_data;
	(void)decoder;
}

static void error_callback(const FLAC__StreamDecoder *decoder, FLAC__StreamDecoderErrorStatus status, void *client_data)
{
	(void)status;
	(void)decoder;
	(void)client_data;
}

#else

bool loadFLACSample(FILE *f, int32_t filesize, moduleSample_t *s)
{
	displayErrorMsg("NO FLAC SUPPORT !");
	return false;

	(void)f;
	(void)filesize;
	(void)s;
}

#endif
