// for finding memory leaks in debug mode with Visual Studio 
#if defined _DEBUG && defined _MSC_VER
#include <crtdbg.h>
#endif

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include "pt2_textout.h"
#include "pt2_sampler.h"
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
	SAMPLETYPE_RAW,
	SAMPLETYPE_IFF,
	SAMPLETYPE_AIFF,
	SAMPLETYPE_WAV,
	SAMPLETYPE_FLAC
};

bool loadRAWSample(FILE *f, uint32_t filesize, moduleSample_t *s);
bool loadIFFSample(FILE *f, uint32_t filesize, moduleSample_t *s);
bool loadAIFFSample(FILE *f, uint32_t filesize, moduleSample_t *s);
bool loadWAVSample(FILE *f, uint32_t filesize, moduleSample_t *s);
bool loadFLACSample(FILE *f, uint32_t filesize, moduleSample_t *s);

static void setSampleTextFromFilename(moduleSample_t *s, char *entryName, const char *ext)
{
	int32_t extLen = (int32_t)strlen(ext);
	int32_t nameLen = (int32_t)strlen(entryName);

	// don't include file extension (if present)
	if (nameLen >= extLen && !_strnicmp(&entryName[nameLen-extLen], ext, extLen))
		nameLen -= extLen;

	// copy over filename to sample name first
	for (int32_t i = 0; i < 21; i++)
		s->text[i] = (i < nameLen) ? (char)entryName[i] : '\0';

	s->text[21] = '\0';
	s->text[22] = '\0';
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

	uint32_t filesize;
	fseek(f, 0, SEEK_END);
	filesize = (uint32_t)ftell(f);
	rewind(f);

	// defaults to RAW if no format was identified
	uint8_t sampleType = SAMPLETYPE_RAW;

	// first, check heades before we eventually load as RAW
	if (filesize > 16)
	{
		uint32_t ID;
		fread(&ID, 4, 1, f);

		if (ID == 0x43614C66) // "fLaC" (XXX: weak detection)
		{
			sampleType = SAMPLETYPE_FLAC;
		}
		else if (ID == 0x46464952) // "RIFF" (WAV)
		{
			fseek(f, 4, SEEK_CUR);
			fread(&ID, 4, 1, f);

			if (ID == 0x45564157) // "WAVE"
				sampleType = SAMPLETYPE_WAV;
		}
		else if (ID == 0x4D524F46) // "FORM" (IFF/AIFF)
		{
			fseek(f, 4, SEEK_CUR);
			fread(&ID, 4, 1, f);

			// check if it's an Amiga IFF sample
			if (ID == 0x58565338 || ID == 0x56533631) // "8SVX" (normal) and "16SV" (FT2 sample)
			{
				sampleType = SAMPLETYPE_IFF;
			}

			// check if it's an AIFF sample
			else if (ID == 0x46464941) // "AIFF"
			{
				sampleType = SAMPLETYPE_AIFF;
			}

			else if (ID == 0x43464941) // "AIFC" (compressed AIFF)
			{
				fclose(f);
				displayErrorMsg("UNSUPPORTED AIFF!");
				return false;
			}
		}
	}

	moduleSample_t *s = &song->samples[editor.currSample];

	// we're not ready to load the sample (error message is shown in the loaders)

	rewind(f);

	bool result = false;
	switch (sampleType)
	{
		case SAMPLETYPE_RAW:
			setSampleTextFromFilename(s, entryName, ".raw");
			result = loadRAWSample(f, filesize, s);
		break;

		case SAMPLETYPE_IFF:
			setSampleTextFromFilename(s, entryName, ".iff");
			result = loadIFFSample(f, filesize, s); 
		break;

		case SAMPLETYPE_AIFF:
			setSampleTextFromFilename(s, entryName, ".aiff");
			result = loadAIFFSample(f, filesize, s);
		break;

		case SAMPLETYPE_WAV:
			setSampleTextFromFilename(s, entryName, ".wav");
			result = loadWAVSample(f, filesize, s);
		break;

		case SAMPLETYPE_FLAC:
			setSampleTextFromFilename(s, entryName, ".flac");
			result = loadFLACSample(f, filesize, s);
		break;

		default: break;
	}

	fclose(f);

	if (result == true)
	{
		// sample load was successful

		if (s->length > config.maxSampleLength)
			s->length = config.maxSampleLength;

		if (s->loopStart+s->loopLength > s->length)
		{
			s->loopStart = 0;
			s->loopLength = 2;
		}

		// zero out rest of sample (if not full length)
		if (s->length < config.maxSampleLength)
			memset(&song->sampleData[s->offset + s->length], 0, config.maxSampleLength - s->length);

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
	}

	return result;
}
