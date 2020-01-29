// for finding memory leaks in debug mode with Visual Studio 
#if defined _DEBUG && defined _MSC_VER
#include <crtdbg.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <ctype.h> // tolower()
#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include "pt2_mouse.h"
#include "pt2_header.h"
#include "pt2_sampler.h"
#include "pt2_textout.h"
#include "pt2_audio.h"
#include "pt2_helpers.h"
#include "pt2_visuals.h"
#include "pt2_unicode.h"
#include "pt2_modloader.h"
#include "pt2_sampleloader.h"
#
typedef struct mem_t
{
	bool _eof;
	uint8_t *_ptr, *_base;
	uint32_t _cnt, _bufsiz;
} mem_t;

static bool oldAutoPlay;
static char oldFullPath[(PATH_MAX * 2) + 2];
static uint32_t oldFullPathLen;
static module_t *tempMod;

extern SDL_Window *window;

static mem_t *mopen(const uint8_t *src, uint32_t length);
static void mclose(mem_t **buf);
static int32_t mgetc(mem_t *buf);
static size_t mread(void *buffer, size_t size, size_t count, mem_t *buf);
static void mseek(mem_t *buf, int32_t offset, int32_t whence);
static uint8_t ppdecrunch(uint8_t *src, uint8_t *dst, uint8_t *offsetLens, uint32_t srcLen, uint32_t dstLen, uint8_t skipBits);

void showSongUnsavedAskBox(int8_t askScreenType)
{
	editor.ui.askScreenShown = true;
	editor.ui.askScreenType = askScreenType;

	pointerSetMode(POINTER_MODE_MSG1, NO_CARRY);
	setStatusMessage("SONG IS UNSAVED !", NO_CARRY);
	renderAskDialog();
}

bool modSave(char *fileName)
{
	int16_t tempPatternCount;
	int32_t i;
	uint32_t tempLoopLength, tempLoopStart, j, k;
	note_t tmp;
	FILE *fmodule;

	tempPatternCount = 0;

	fmodule = fopen(fileName, "wb");
	if (fmodule == NULL)
	{
		displayErrorMsg("FILE I/O ERROR !");
		return false;
	}

	for (i = 0; i < 20; i++)
		fputc(tolower(modEntry->head.moduleTitle[i]), fmodule);

	for (i = 0; i < MOD_SAMPLES; i++)
	{
		for (j = 0; j < 22; j++)
			fputc(tolower(modEntry->samples[i].text[j]), fmodule);

		fputc(modEntry->samples[i].length >> 9, fmodule);
		fputc(modEntry->samples[i].length >> 1, fmodule);
		fputc(modEntry->samples[i].fineTune & 0x0F, fmodule);
		fputc((modEntry->samples[i].volume > 64) ? 64 : modEntry->samples[i].volume, fmodule);

		tempLoopLength = modEntry->samples[i].loopLength;
		if (tempLoopLength < 2)
			tempLoopLength = 2;

		tempLoopStart = modEntry->samples[i].loopStart;
		if (tempLoopLength == 2)
			tempLoopStart = 0;

		fputc(tempLoopStart >> 9, fmodule);
		fputc(tempLoopStart >> 1, fmodule);
		fputc(tempLoopLength >> 9, fmodule);
		fputc(tempLoopLength >> 1, fmodule);
	}

	fputc(modEntry->head.orderCount & 0x00FF, fmodule);
	fputc(0x7F, fmodule); // ProTracker puts 0x7F at this place (restart pos/BPM in other trackers)

	for (i = 0; i < MOD_ORDERS; i++)
		fputc(modEntry->head.order[i] & 0xFF, fmodule);

	tempPatternCount = 0;
	for (i = 0; i < MOD_ORDERS; i++)
	{
		if (tempPatternCount < modEntry->head.order[i])
			tempPatternCount = modEntry->head.order[i];
	}

	if (++tempPatternCount > MAX_PATTERNS)
		  tempPatternCount = MAX_PATTERNS;

	fwrite((tempPatternCount <= 64) ? "M.K." : "M!K!", 1, 4, fmodule);

	for (i = 0; i < tempPatternCount; i++)
	{
		for (j = 0; j < MOD_ROWS; j++)
		{
			for (k = 0; k < AMIGA_VOICES; k++)
			{
				tmp = modEntry->patterns[i][(j * AMIGA_VOICES) + k];

				fputc((tmp.sample & 0xF0) | ((tmp.period >> 8) & 0x0F), fmodule);
				fputc(tmp.period & 0xFF, fmodule);
				fputc(((tmp.sample << 4) & 0xF0) | (tmp.command & 0x0F), fmodule);
				fputc(tmp.param, fmodule);
			}
		}
	}

	for (i = 0; i < MOD_SAMPLES; i++)
	{
		// Amiga ProTracker stuck "BEEP" sample fix
		if (modEntry->samples[i].length >= 2 && modEntry->samples[i].loopStart+modEntry->samples[i].loopLength == 2)
		{
			fputc(0, fmodule);
			fputc(0, fmodule);

			k = modEntry->samples[i].length;
			for (j = 2; j < k; j++)
				fputc(modEntry->sampleData[modEntry->samples[i].offset+j], fmodule);
		}
		else
		{
			fwrite(&modEntry->sampleData[MAX_SAMPLE_LEN * i], 1, modEntry->samples[i].length, fmodule);
		}
	}

	fclose(fmodule);

	displayMsg("MODULE SAVED !");
	setMsgPointer();

	editor.diskop.cached = false;
	if (editor.ui.diskOpScreenShown)
		editor.ui.updateDiskOpFileList = true;

	updateWindowTitle(MOD_NOT_MODIFIED);
	return true;
}

static int8_t checkModType(const char *buf)
{
	     if (!strncmp(buf, "M.K.", 4)) return FORMAT_MK;   // ProTracker v1.x, handled as ProTracker v2.x
	else if (!strncmp(buf, "M!K!", 4)) return FORMAT_MK2;  // ProTracker v2.x (if >64 patterns)
	else if (!strncmp(buf, "FLT4", 4)) return FORMAT_FLT4; // StarTrekker (4ch), handled as ProTracker v2.x
	else if (!strncmp(buf, "1CHN", 4)) return FORMAT_1CHN; // handled as 4ch
	else if (!strncmp(buf, "2CHN", 4)) return FORMAT_2CHN; // FastTracker II, handled as 4ch
	else if (!strncmp(buf, "3CHN", 4)) return FORMAT_3CHN; // handled as 4ch
	else if (!strncmp(buf, "4CHN", 4)) return FORMAT_4CHN; // rare type, not sure what tracker it comes from
	else if (!strncmp(buf, "N.T.", 4)) return FORMAT_MK;   // NoiseTracker 1.0, handled as ProTracker v2.x
	else if (!strncmp(buf, "M&K!", 4)) return FORMAT_FEST; // Special NoiseTracker format (used in music disks?)
	else if (!strncmp(buf, "FEST", 4)) return FORMAT_FEST; // Special NoiseTracker format (used in music disks?)
	else if (!strncmp(buf, "NSMS", 4)) return FORMAT_MK;   // OpenMPT Load_mod.cpp: "kingdomofpleasure.mod by bee hunter"
	else if (!strncmp(buf, "LARD", 4)) return FORMAT_MK;   // OpenMPT Load_mod.cpp: "judgement_day_gvine.mod by 4-mat"
	else if (!strncmp(buf, "PATT", 4)) return FORMAT_MK;   // OpenMPT Load_mod.cpp: "ProTracker 3.6"

	return FORMAT_UNKNOWN; // may be The Ultimate SoundTracker, 15 samples
}

// converts zeroes to spaces in a string, up until the last zero found
static void fixZeroesInString(char *str, uint32_t maxLength)
{
	int32_t i;

	for (i = maxLength-1; i >= 0; i--)
	{
		if (str[i] != '\0')
			break;
	}

	// convert zeroes to spaces
	if (i > 0)
	{
		for (int32_t j = 0; j < i; j++)
		{
			if (str[j] == '\0')
				str[j] = ' ';
		}
	}
}

module_t *modLoad(UNICHAR *fileName)
{
	bool mightBeSTK, lateSTKVerFlag, veryLateSTKVerFlag;
	char modSig[4], tmpChar;
	int8_t numSamples;
	uint8_t ppCrunchData[4], bytes[4], *ppBuffer;
	uint8_t *modBuffer, ch, row, pattern, channels;
	uint16_t ciaPeriod;
	int32_t i, loopStart, loopLength, loopOverflowVal;
	uint32_t j, PP20, ppPackLen, ppUnpackLen;
	FILE *fmodule;
	module_t *newModule;
	moduleSample_t *s;
	note_t *note;
	mem_t *mod;

	/* these flags are kinda dumb and inaccurate, but we
	** don't aim for excellent STK import anyway. */
	veryLateSTKVerFlag = false; // "DFJ SoundTracker III" nad later
	lateSTKVerFlag = false; // "TJC SoundTracker II" and later
	mightBeSTK = false;

	mod = NULL;
	ppBuffer = NULL;
	modBuffer = NULL;
	fmodule = NULL;
	newModule = NULL;

	newModule = (module_t *)calloc(1, sizeof (module_t));
	if (newModule == NULL)
	{
		statusOutOfMemory();
		goto modLoadError;
	}

	fmodule = UNICHAR_FOPEN(fileName, "rb");
	if (fmodule == NULL)
	{
		displayErrorMsg("FILE I/O ERROR !");
		goto modLoadError;
	}

	fseek(fmodule, 0, SEEK_END);
	newModule->head.moduleSize = ftell(fmodule);
	fseek(fmodule, 0, SEEK_SET);

	// check if mod is a powerpacker mod
	fread(&PP20, 4, 1, fmodule);
	if (PP20 == 0x30325850) // "PX20"
	{
		displayErrorMsg("ENCRYPTED PPACK !");
		goto modLoadError;
	}
	else if (PP20 == 0x30325050) // "PP20"
	{
		ppPackLen = newModule->head.moduleSize;
		if (ppPackLen & 3)
		{
			displayErrorMsg("POWERPACKER ERROR");
			goto modLoadError;
		}

		fseek(fmodule, ppPackLen - 4, SEEK_SET);

		ppCrunchData[0] = (uint8_t)fgetc(fmodule);
		ppCrunchData[1] = (uint8_t)fgetc(fmodule);
		ppCrunchData[2] = (uint8_t)fgetc(fmodule);
		ppCrunchData[3] = (uint8_t)fgetc(fmodule);

		ppUnpackLen = (ppCrunchData[0] << 16) | (ppCrunchData[1] << 8) | ppCrunchData[2];

		// smallest and biggest possible .MOD
		if (ppUnpackLen < 2108 || ppUnpackLen > 4195326)
		{
			displayErrorMsg("NOT A MOD FILE !");
			goto modLoadError;
		}

		ppBuffer = (uint8_t *)malloc(ppPackLen);
		if (ppBuffer == NULL)
		{
			statusOutOfMemory();
			goto modLoadError;
		}

		modBuffer = (uint8_t *)malloc(ppUnpackLen);
		if (modBuffer == NULL)
		{
			statusOutOfMemory();
			goto modLoadError;
		}

		fseek(fmodule, 0, SEEK_SET);
		fread(ppBuffer, 1, ppPackLen, fmodule);
		fclose(fmodule);
		ppdecrunch(ppBuffer + 8, modBuffer, ppBuffer + 4, ppPackLen - 12, ppUnpackLen, ppCrunchData[3]);
		free(ppBuffer);
		newModule->head.moduleSize = ppUnpackLen;
	}
	else
	{
		// smallest and biggest possible PT .MOD
		if (newModule->head.moduleSize < 2108 || newModule->head.moduleSize > 4195326)
		{
			displayErrorMsg("NOT A MOD FILE !");
			goto modLoadError;
		}

		modBuffer = (uint8_t *)malloc(newModule->head.moduleSize);
		if (modBuffer == NULL)
		{
			statusOutOfMemory();
			goto modLoadError;
		}

		fseek(fmodule, 0, SEEK_SET);
		fread(modBuffer, 1, newModule->head.moduleSize, fmodule);
		fclose(fmodule);
	}

	mod = mopen(modBuffer, newModule->head.moduleSize);
	if (mod == NULL)
	{
		displayErrorMsg("FILE I/O ERROR !");
		goto modLoadError;
	}

	// check module tag
	mseek(mod, 0x0438, SEEK_SET);
	mread(modSig, 1, 4, mod);

	newModule->head.format = checkModType(modSig);
	if (newModule->head.format == FORMAT_UNKNOWN)
		mightBeSTK = true;

	     if (newModule->head.format == FORMAT_1CHN) channels = 1;
	else if (newModule->head.format == FORMAT_2CHN) channels = 2;
	else if (newModule->head.format == FORMAT_3CHN) channels = 3;
	else channels = 4;

	mseek(mod, 0, SEEK_SET);

	mread(newModule->head.moduleTitle, 1, 20, mod);
	newModule->head.moduleTitle[20] = '\0';

	for (i = 0; i < 20; i++)
	{
		tmpChar = newModule->head.moduleTitle[i];
		if ((tmpChar < ' ' || tmpChar > '~') && tmpChar != '\0')
			tmpChar = ' ';

		newModule->head.moduleTitle[i] = (char)tolower(tmpChar);
	}

	fixZeroesInString(newModule->head.moduleTitle, 20);

	// read sample information
	for (i = 0; i < MOD_SAMPLES; i++)
	{
		s = &newModule->samples[i];

		if (mightBeSTK && i >= 15)
		{
			s->loopLength = 2;
		}
		else
		{
			mread(s->text, 1, 22, mod);
			s->text[22] = '\0';

			for (j = 0; j < 22; j++)
			{
				tmpChar = s->text[j];
				if ((tmpChar < ' ' || tmpChar > '~') && tmpChar != '\0')
					tmpChar = ' ';

				s->text[j] = (char)tolower(tmpChar);
			}

			fixZeroesInString(s->text, 22);

			s->realLength = ((mgetc(mod) << 8) | mgetc(mod)) * 2;
			if (s->realLength > MAX_SAMPLE_LEN)
				s->length = MAX_SAMPLE_LEN;
			else
				s->length = (uint16_t)s->realLength;

			if (s->length > 9999)
				lateSTKVerFlag = true; // Only used if mightBeSTK is set

			if (newModule->head.format == FORMAT_FEST)
				s->fineTune = (uint8_t)((-mgetc(mod) & 0x1F) / 2); // One more bit of precision, + inverted
			else
				s->fineTune = (uint8_t)mgetc(mod) & 0x0F;

			s->volume = (int8_t)mgetc(mod);
			s->volume = CLAMP(s->volume, 0, 64);

			loopStart = ((mgetc(mod) << 8) | mgetc(mod)) * 2;
			loopLength = ((mgetc(mod) << 8) | mgetc(mod)) * 2;

			if (loopLength < 2)
				loopLength = 2; // fixes empty samples in .MODs saved from FT2

			if (loopStart > MAX_SAMPLE_LEN || loopStart+loopLength > MAX_SAMPLE_LEN)
			{
				s->loopStart = 0;
				s->loopLength = 2;
			}
			else
			{
				s->loopStart = (uint16_t)loopStart;
				s->loopLength = (uint16_t)loopLength;
			}

			if (mightBeSTK)
				s->loopStart /= 2;

			// fix for poorly converted STK->PTMOD modules.
			if (!mightBeSTK && s->loopLength > 2 && s->loopStart+s->loopLength > s->length)
			{
				if ((s->loopStart/2) + s->loopLength <= s->length)
					s->loopStart /= 2;
			}

			if (mightBeSTK)
			{
				if (s->loopLength > 2 && s->loopStart < s->length)
				{
					s->tmpLoopStart = s->loopStart; // for sample data reading later on
					s->length -= s->loopStart;
					s->realLength -= s->loopStart;
					s->loopStart = 0;
				}

				// no finetune in STK/UST
				s->fineTune = 0;
			}

			// some modules are broken like this, adjust sample length if possible (this is ok if we have room)
			if (s->length > 0 && s->loopLength > 2 && s->loopStart+s->loopLength > s->length)
			{
				loopOverflowVal = (s->loopStart+s->loopLength) - s->length;
				if (s->length+loopOverflowVal <= MAX_SAMPLE_LEN)
				{
					s->length += loopOverflowVal; // this is safe, we're calloc()'ing 65535*(31+2) bytes
				}
				else
				{
					s->loopStart = 0;
					s->loopLength = 2;
				}
			}
		}
	}

	// STK 2.5 had loopStart in words, not bytes. Convert if late version STK.
	if (mightBeSTK && lateSTKVerFlag)
	{
		for (i = 0; i < 15; i++)
		{
			s = &newModule->samples[i];
			if (s->loopStart > 2)
			{
				s->length -= s->tmpLoopStart;
				s->tmpLoopStart *= 2;
			}
		}
	}

	newModule->head.orderCount = (uint8_t)mgetc(mod);

	// fixes beatwave.mod (129 orders) and other weird MODs
	if (newModule->head.orderCount > 128)
	{
		if (newModule->head.orderCount > 129)
		{
			displayErrorMsg("NOT A MOD FILE !");
			goto modLoadError;
		}

		newModule->head.orderCount = 128;
	}

	if (newModule->head.orderCount == 0)
	{
		displayErrorMsg("NOT A MOD FILE !");
		goto modLoadError;
	}

	newModule->head.restartPos = (uint8_t)mgetc(mod);
	if (mightBeSTK && (newModule->head.restartPos == 0 || newModule->head.restartPos > 220))
	{
		displayErrorMsg("NOT A MOD FILE !");
		goto modLoadError;
	}

	if (mightBeSTK)
	{
		/* If we're still here at this point and the mightBeSTK flag is set,
		** then it's definitely a proper The Ultimate SoundTracker (STK) module. */

		newModule->head.format = FORMAT_STK;

		if (newModule->head.restartPos != 120) // 120 is a special case and means 50Hz (125BPM)
		{
			if (newModule->head.restartPos > 239)
				newModule->head.restartPos = 239;

			// convert UST tempo to BPM

			ciaPeriod = (240 - newModule->head.restartPos) * 122;
			newModule->head.initBPM = (uint16_t)round(((double)CIA_PAL_CLK / ciaPeriod) * (125.0 / 50.0));
		}

		newModule->head.restartPos = 0;
	}

	for (i = 0; i < MOD_ORDERS; i++)
	{
		newModule->head.order[i] = (int16_t)mgetc(mod);
		if (newModule->head.order[i] > newModule->head.patternCount)
			newModule->head.patternCount = newModule->head.order[i];
	}

	if (++newModule->head.patternCount > MAX_PATTERNS)
	{
		displayErrorMsg("UNSUPPORTED MOD !");
		goto modLoadError;
	}

	if (newModule->head.format != FORMAT_STK) // The Ultimate SoundTracker MODs doesn't have this tag
		mseek(mod, 4, SEEK_CUR); // we already read/tested the tag earlier, skip it

	// init 100 patterns and load patternCount of patterns
	for (pattern = 0; pattern < MAX_PATTERNS; pattern++)
	{
		newModule->patterns[pattern] = (note_t *)calloc(MOD_ROWS * AMIGA_VOICES, sizeof (note_t));
		if (newModule->patterns[pattern] == NULL)
		{
			statusOutOfMemory();
			goto modLoadError;
		}
	}

	// load pattern data
	for (pattern = 0; pattern < newModule->head.patternCount; pattern++)
	{
		note = newModule->patterns[pattern];
		for (row = 0; row < MOD_ROWS; row++)
		{
			for (ch = 0; ch < channels; ch++)
			{
				mread(bytes, 1, 4, mod);

				note->period = ((bytes[0] & 0x0F) << 8) | bytes[1];
				note->sample = (bytes[0] & 0xF0) | (bytes[2] >> 4); // don't (!) clamp, the player checks for invalid samples
				note->command = bytes[2] & 0x0F;
				note->param = bytes[3];

				if (mightBeSTK)
				{
					if (note->command == 0xC || note->command == 0xD || note->command == 0xE)
					{
						// "TJC SoundTracker II" and later
						lateSTKVerFlag = true;
					}

					if (note->command == 0xF)
					{
						// "DFJ SoundTracker III" and later
						lateSTKVerFlag = true;
						veryLateSTKVerFlag = true;
					}
				}

				note++;
			}

			if (channels < 4)
				note += AMIGA_VOICES - channels;
		}
	}

	/* TODO: Find out if song is FORMAT_NT through heuristics
	** Only detected for FEST songs for now. */

	// pattern command conversion
	if (mightBeSTK || newModule->head.format == FORMAT_4CHN ||
		newModule->head.format == FORMAT_NT || newModule->head.format == FORMAT_FEST)
	{
		for (pattern = 0; pattern < newModule->head.patternCount; pattern++)
		{
			note = newModule->patterns[pattern];
			for (j = 0; j < MOD_ROWS*4; j++)
			{
				if (newModule->head.format == FORMAT_NT || newModule->head.format == FORMAT_FEST)
				{
					// any Dxx == D00 in N.T./FEST modules
					if (note->command == 0xD)
						note->param = 0;

					// effect F with param 0x00 does nothing in NT
					if (note->command == 0xF && note->param == 0)
						note->command = 0;
				}
				else if (mightBeSTK)
				{
					// convert STK effects to PT effects

					if (!lateSTKVerFlag)
					{
						// old SoundTracker 1.x commands

						if (note->command == 1)
						{
							// arpeggio
							note->command = 0;
						}
						else if (note->command == 2)
						{
							// pitch slide
							if (note->param & 0xF0)
							{
								// pitch slide down
								note->command = 2;
								note->param >>= 4;
							}
							else if (note->param & 0x0F)
							{
								// pitch slide up
								note->command = 1;
							}
						}
					}
					else
					{
						// "DFJ SoundTracker II" or later

						if (note->command == 0xD)
						{
							if (veryLateSTKVerFlag) // "DFJ SoundTracker III" or later
							{
								// pattern break w/ no param (param must be cleared to fix some songs)
								note->param = 0;
							}
							else
							{
								// volume slide
								note->command = 0xA;
							}
						}
					}
				}
				else if (newModule->head.format == FORMAT_4CHN) // 4CHN != PT MOD
				{
					// remove E8x (pan) commands as these are Karplus-Strong in ProTracker
					if (note->command == 0xE && (note->param >> 4) == 0x8)
					{
						note->command = 0;
						note->param = 0;
					}

					// effect F with param 0x00 does nothing in these 4CHN formats
					if (note->command == 0xF && note->param == 0)
					{
						note->command = 0;
						note->param = 0;
					}
				}

				note++;
			}
		}
	}

	// set static sample data pointers (sample data = one huge buffer internally)
	for (i = 0; i < MOD_SAMPLES; i++)
		newModule->samples[i].offset = MAX_SAMPLE_LEN * i;

	// +2 sample slots for overflow safety (Paula and scopes)
	newModule->sampleDataUnaligned = (int8_t *)CALLOC_PAD((MOD_SAMPLES + 2) * MAX_SAMPLE_LEN, 256);
	if (newModule->sampleDataUnaligned == NULL)
	{
		statusOutOfMemory();
		goto modLoadError;
	}

	newModule->sampleData = (int8_t *)ALIGN_PTR(newModule->sampleDataUnaligned, 256);

	// load sample data
	numSamples = (newModule->head.format == FORMAT_STK) ? 15 : 31;
	for (i = 0; i < numSamples; i++)
	{
		s = &newModule->samples[i];
		if (mightBeSTK && (s->loopLength > 2 && s->loopLength < s->length))
			mseek(mod, s->tmpLoopStart, SEEK_CUR);

		mread(&newModule->sampleData[s->offset], 1, s->length, mod);
		if (s->realLength > s->length)
			mseek(mod, s->realLength - s->length, SEEK_CUR);

		// fix beeping samples
		if (s->length >= 2 && s->loopStart+s->loopLength <= 2)
		{
			newModule->sampleData[s->offset+0] = 0;
			newModule->sampleData[s->offset+1] = 0;
		}
	}

	mclose(&mod);
	free(modBuffer);

	for (i = 0; i < AMIGA_VOICES; i++)
		newModule->channels[i].n_chanindex = i;

	return newModule;

modLoadError:
	if (mod != NULL) mclose(&mod);
	if (modBuffer != NULL) free(modBuffer);
	if (ppBuffer != NULL) free(ppBuffer);

	if (newModule != NULL)
	{
		for (i = 0; i < MAX_PATTERNS; i++)
		{
			if (newModule->patterns[i] != NULL)
				free(newModule->patterns[i]);
		}

		free(newModule);
	}

	return NULL;
}

bool saveModule(bool checkIfFileExist, bool giveNewFreeFilename)
{
	char fileName[128], tmpBuffer[64];
	uint16_t i;
	struct stat statBuffer;

	memset(fileName, 0, sizeof (fileName));

	if (ptConfig.modDot)
	{
		// extension.filename
		if (*modEntry->head.moduleTitle == '\0')
		{
			strcat(fileName, "mod.untitled");
		}
		else
		{
			strcat(fileName, "mod.");
			for (i = 4; i < 20+4; i++)
			{
				fileName[i] = (char)tolower(modEntry->head.moduleTitle[i-4]);
				if (fileName[i] == '\0') break;
				sanitizeFilenameChar(&fileName[i]);
			}
		}
	}
	else
	{
		// filename.extension
		if (*modEntry->head.moduleTitle == '\0')
		{
			strcat(fileName, "untitled.mod");
		}
		else
		{
			for (i = 0; i < 20; i++)
			{
				fileName[i] = (char)tolower(modEntry->head.moduleTitle[i]);
				if (fileName[i] == '\0') break;
				sanitizeFilenameChar(&fileName[i]);
			}
			strcat(fileName, ".mod");
		}
	}

	if (giveNewFreeFilename && stat(fileName, &statBuffer) == 0)
	{
		for (uint16_t j = 1; j <= 9999; j++)
		{
			memset(fileName, 0, sizeof (fileName));
			if (ptConfig.modDot)
			{
				// extension.filename
				if (*modEntry->head.moduleTitle == '\0')
				{
					sprintf(fileName, "mod.untitled-%d", j);
				}
				else
				{
					for (i = 0; i < 20; i++)
					{
						tmpBuffer[i] = (char)tolower(modEntry->head.moduleTitle[i]);
						if (tmpBuffer[i] == '\0') break;
						sanitizeFilenameChar(&tmpBuffer[i]);
					}
					sprintf(fileName, "mod.%s-%d", tmpBuffer, j);
				}
			}
			else
			{
				// filename.extension
				if (*modEntry->head.moduleTitle == '\0')
				{
					sprintf(fileName, "untitled-%d.mod", j);
				}
				else
				{
					for (i = 0; i < 20; i++)
					{
						tmpBuffer[i] = (char)tolower(modEntry->head.moduleTitle[i]);
						if (tmpBuffer[i] == '\0') break;
						sanitizeFilenameChar(&tmpBuffer[i]);
					}
					sprintf(fileName, "%s-%d.mod", tmpBuffer, j);
				}
			}

			if (stat(fileName, &statBuffer) != 0)
				break;
		}
	}

	if (checkIfFileExist)
	{
		if (stat(fileName, &statBuffer) == 0)
		{
			editor.ui.askScreenShown = true;
			editor.ui.askScreenType = ASK_SAVEMOD_OVERWRITE;
			pointerSetMode(POINTER_MODE_MSG1, NO_CARRY);
			setStatusMessage("OVERWRITE FILE ?", NO_CARRY);
			renderAskDialog();
			return -1;
		}
	}

	if (editor.ui.askScreenShown)
	{
		editor.ui.answerNo = false;
		editor.ui.answerYes = false;
		editor.ui.askScreenShown = false;
	}

	return modSave(fileName);
}

static mem_t *mopen(const uint8_t *src, uint32_t length)
{
	mem_t *b;

	if (src == NULL || length == 0)
		return NULL;

	b = (mem_t *)malloc(sizeof (mem_t));
	if (b == NULL)
		return NULL;

	b->_base = (uint8_t *)src;
	b->_ptr = (uint8_t *)src;
	b->_cnt = length;
	b->_bufsiz = length;
	b->_eof = false;

	return b;
}

static void mclose(mem_t **buf)
{
	if (*buf != NULL)
	{
		free(*buf);
		*buf = NULL;
	}
}

static int32_t mgetc(mem_t *buf)
{
	int32_t b;

	if (buf == NULL || buf->_ptr == NULL || buf->_cnt <= 0)
		return 0;

	b = *buf->_ptr;

	buf->_cnt--;
	buf->_ptr++;

	if (buf->_cnt <= 0)
	{
		buf->_ptr = buf->_base + buf->_bufsiz;
		buf->_cnt = 0;
		buf->_eof = true;
	}

	return (int32_t)b;
}

static size_t mread(void *buffer, size_t size, size_t count, mem_t *buf)
{
	int32_t pcnt;
	size_t wrcnt;

	if (buf == NULL || buf->_ptr == NULL)
		return 0;

	wrcnt = size * count;
	if (size == 0 || buf->_eof)
		return 0;

	pcnt = (buf->_cnt > (uint32_t)wrcnt) ? (uint32_t)wrcnt : buf->_cnt;
	memcpy(buffer, buf->_ptr, pcnt);

	buf->_cnt -= pcnt;
	buf->_ptr += pcnt;

	if (buf->_cnt <= 0)
	{
		buf->_ptr = buf->_base + buf->_bufsiz;
		buf->_cnt = 0;
		buf->_eof = true;
	}

	return pcnt/size;
}

static void mseek(mem_t *buf, int32_t offset, int32_t whence)
{
	if (buf == NULL)
		return;

	if (buf->_base)
	{
		switch (whence)
		{
			case SEEK_SET: buf->_ptr  = buf->_base + offset; break;
			case SEEK_CUR: buf->_ptr += offset; break;
			case SEEK_END: buf->_ptr  = buf->_base + buf->_bufsiz + offset; break;
			default: break;
		}

		buf->_eof = false;
		if (buf->_ptr >= buf->_base+buf->_bufsiz)
		{
			buf->_ptr = buf->_base+buf->_bufsiz;
			buf->_eof = true;
		}

		buf->_cnt = (buf->_base+buf->_bufsiz) - buf->_ptr;
	}
}

/* Code taken from Heikki Orsila's amigadepack. Seems to have no license,
** so I'll assume it fits into wtfpl (wtfpl.net). Heikki should contact me
** if it shall not.
** Modified by 8bitbubsy */

#define PP_READ_BITS(nbits, var)            \
  bitCnt = (nbits);                         \
  while (bitsLeft < bitCnt) {               \
	if (bufSrc < src) return false;         \
	bitBuffer |= (*--bufSrc << bitsLeft);   \
	bitsLeft += 8;                          \
  }                                         \
  (var) = 0;                                \
  bitsLeft -= bitCnt;                       \
  while (bitCnt--) {                        \
	(var) = ((var) << 1) | (bitBuffer & 1); \
	bitBuffer >>= 1;                        \
  }                                         \

static uint8_t ppdecrunch(uint8_t *src, uint8_t *dst, uint8_t *offsetLens, uint32_t srcLen, uint32_t dstLen, uint8_t skipBits)
{
	uint8_t *bufSrc, *dstEnd, *out, bitsLeft, bitCnt;
	uint32_t x, todo, offBits, offset, written, bitBuffer;

	if (src == NULL || dst == NULL || offsetLens == NULL)
		return false;

	bitsLeft = 0;
	bitBuffer = 0;
	written = 0;
	bufSrc = src + srcLen;
	out = dst + dstLen;
	dstEnd = out;

	PP_READ_BITS(skipBits, x);
	while (written < dstLen)
	{
		PP_READ_BITS(1, x);
		if (x == 0)
		{
			todo = 1;

			do
			{
				PP_READ_BITS(2, x);
				todo += x;
			}
			while (x == 3);

			while (todo--)
			{
				PP_READ_BITS(8, x);

				if (out <= dst)
					return false;

				*--out = (uint8_t)x;
				written++;
			}

			if (written == dstLen)
				break;
		}

		PP_READ_BITS(2, x);

		offBits = offsetLens[x];
		todo = x + 2;

		if (x == 3)
		{
			PP_READ_BITS(1, x);
			if (x == 0) offBits = 7;

			PP_READ_BITS((uint8_t)offBits, offset);
			do
			{
				PP_READ_BITS(3, x);
				todo += x;
			}
			while (x == 7);
		}
		else
		{
			PP_READ_BITS((uint8_t)offBits, offset);
		}

		if (out+offset >= dstEnd)
			return false;

		while (todo--)
		{
			x = out[offset];

			if (out <= dst)
				return false;

			*--out = (uint8_t)x;
			written++;
		}
	}

	return true;
}

void setupNewMod(void)
{
	int8_t i;

	// setup GUI text pointers
	for (i = 0; i < MOD_SAMPLES; i++)
	{
		modEntry->samples[i].volumeDisp = &modEntry->samples[i].volume;
		modEntry->samples[i].lengthDisp = &modEntry->samples[i].length;
		modEntry->samples[i].loopStartDisp = &modEntry->samples[i].loopStart;
		modEntry->samples[i].loopLengthDisp = &modEntry->samples[i].loopLength;

		fillSampleRedoBuffer(i);
	}

	modSetPos(0, 0);
	modSetPattern(0); // set pattern to 00 instead of first order's pattern

	editor.currEditPatternDisp = &modEntry->currPattern;
	editor.currPosDisp = &modEntry->currOrder;
	editor.currPatternDisp = &modEntry->head.order[0];
	editor.currPosEdPattDisp = &modEntry->head.order[0];
	editor.currLengthDisp = &modEntry->head.orderCount;

	// calculate MOD size
	editor.ui.updateSongSize = true;

	editor.muted[0] = false;
	editor.muted[1] = false;
	editor.muted[2] = false;
	editor.muted[3] = false;

	editor.editMoveAdd = 1;
	editor.currSample = 0;
	editor.musicTime = 0;
	editor.modLoaded = true;
	editor.blockMarkFlag = false;
	editor.sampleZero = false;
	editor.keypadSampleOffset = 0;

	setLEDFilter(false); // real PT doesn't do this, but that's insane

	updateWindowTitle(MOD_NOT_MODIFIED);

	modSetSpeed(6);

	if (modEntry->head.initBPM > 0)
		modSetTempo(modEntry->head.initBPM);
	else
		modSetTempo(125);

	updateCurrSample();
	editor.samplePos = 0;
	updateSamplePos();
}

void loadModFromArg(char *arg)
{
	uint32_t filenameLen;
	UNICHAR *filenameU;

	editor.ui.introScreenShown = false;
	statusAllRight();

	filenameLen = (uint32_t)strlen(arg);

	filenameU = (UNICHAR *)calloc((filenameLen + 2), sizeof (UNICHAR));
	if (filenameU == NULL)
	{
		statusOutOfMemory();
		return;
	}

#ifdef _WIN32
	MultiByteToWideChar(CP_UTF8, 0, arg, -1, filenameU, filenameLen);
#else
	strcpy(filenameU, arg);
#endif

	tempMod = modLoad(filenameU);
	if (tempMod != NULL)
	{
		modEntry->moduleLoaded = false;
		modFree();
		modEntry = tempMod;
		setupNewMod();
		modEntry->moduleLoaded = true;
	}
	else
	{
		editor.errorMsgActive = true;
		editor.errorMsgBlock = true;
		editor.errorMsgCounter = 0;

		// status/error message is set in the mod loader
		setErrPointer();
	}

	free(filenameU);
}

static bool testExtension(char *ext, uint8_t extLen, char *fullPath)
{
	// checks for EXT.filename and filename.EXT
	char *fileName, begStr[8], endStr[8];
	uint32_t fileNameLen;

	extLen++; // add one to length (dot)

	fileName = strrchr(fullPath, DIR_DELIMITER);
	if (fileName != NULL)
		fileName++;
	else
		fileName = fullPath;

	fileNameLen = (uint32_t)strlen(fileName);
	if (fileNameLen >= extLen)
	{
		sprintf(begStr, "%s.", ext);
		if (!_strnicmp(begStr, fileName, extLen))
			return true;

		sprintf(endStr, ".%s", ext);
		if (!_strnicmp(endStr, fileName + (fileNameLen - extLen), extLen))
			return true;
	}

	return false;
}

void loadDroppedFile(char *fullPath, uint32_t fullPathLen, bool autoPlay, bool songModifiedCheck)
{
	bool isMod;
	char *fileName, *ansiName;
	uint8_t oldMode, oldPlayMode;
	UNICHAR *fullPathU;

	// don't allow drag n' drop if the tracker is busy
	if (editor.ui.pointerMode == POINTER_MODE_MSG1 ||
		editor.diskop.isFilling || editor.isWAVRendering ||
		editor.ui.samplerFiltersBoxShown || editor.ui.samplerVolBoxShown)
	{
		return;
	}

	ansiName = (char *)calloc(fullPathLen + 10, sizeof (char));
	if (ansiName == NULL)
	{
		statusOutOfMemory();
		return;
	}

	fullPathU = (UNICHAR *)calloc((fullPathLen + 2), sizeof (UNICHAR));
	if (fullPathU == NULL)
	{
		statusOutOfMemory();
		return;
	}

#ifdef _WIN32
	MultiByteToWideChar(CP_UTF8, 0, fullPath, -1, fullPathU, fullPathLen);
#else
	strcpy(fullPathU, fullPath);
#endif

	unicharToAnsi(ansiName, fullPathU, fullPathLen);

	// make a new pointer point to filename (strip path)
	fileName = strrchr(ansiName, DIR_DELIMITER);
	if (fileName != NULL)
		fileName++;
	else
		fileName = ansiName;

	// check if the file extension is a module (FIXME: check module by content instead..?)
	isMod = false;
	     if (testExtension("MOD", 3, fileName)) isMod = true;
	else if (testExtension("M15", 3, fileName)) isMod = true;
	else if (testExtension("STK", 3, fileName)) isMod = true;
	else if (testExtension("NST", 3, fileName)) isMod = true;
	else if (testExtension("UST", 3, fileName)) isMod = true;
	else if (testExtension("PP",  2, fileName)) isMod = true;
	else if (testExtension("NT",  2, fileName)) isMod = true;

	if (isMod)
	{
		if (songModifiedCheck && modEntry->modified)
		{
			free(ansiName);
			free(fullPathU);

			memcpy(oldFullPath, fullPath, fullPathLen);
			oldFullPath[fullPathLen+0] = 0;
			oldFullPath[fullPathLen+1] = 0;

			oldFullPathLen = fullPathLen;
			oldAutoPlay = autoPlay;

			// de-minimize window and set focus so that the user sees the message box
			SDL_RestoreWindow(window);
			SDL_RaiseWindow(window);

			showSongUnsavedAskBox(ASK_DISCARD_SONG_DRAGNDROP);
			return;
		}

		tempMod = modLoad(fullPathU);
		if (tempMod != NULL)
		{
			oldMode = editor.currMode;
			oldPlayMode = editor.playMode;

			modStop();
			modFree();

			modEntry = tempMod;
			setupNewMod();
			modEntry->moduleLoaded = true;

			statusAllRight();

			if (autoPlay)
			{
				// start normal playback
				editor.playMode = PLAY_MODE_NORMAL;
				modPlay(DONT_SET_PATTERN, 0, 0);
				editor.currMode = MODE_PLAY;
				pointerSetMode(POINTER_MODE_PLAY, DO_CARRY);
			}
			else if ((oldMode == MODE_PLAY) || (oldMode == MODE_RECORD))
			{
				// use last mode
				editor.playMode = oldPlayMode;
				if ((oldPlayMode == PLAY_MODE_PATTERN) || (oldMode == MODE_RECORD))
					modPlay(0, 0, 0);
				else
					modPlay(DONT_SET_PATTERN, 0, 0);
				editor.currMode = oldMode;

				if (oldMode == MODE_RECORD)
					pointerSetMode(POINTER_MODE_RECORD, DO_CARRY);
				else
					pointerSetMode(POINTER_MODE_PLAY, DO_CARRY);
			}
			else
			{
				// stop playback
				editor.playMode = PLAY_MODE_NORMAL;
				editor.currMode = MODE_IDLE;
				editor.songPlaying = false;
				pointerSetMode(POINTER_MODE_IDLE, DO_CARRY);
			}

			displayMainScreen();
		}
		else
		{
			editor.errorMsgActive = true;
			editor.errorMsgBlock = true;
			editor.errorMsgCounter = 0;
			setErrPointer(); // status/error message is set in the mod loader
		}
	}
	else
	{
		loadSample(fullPathU, fileName);
	}

	free(ansiName);
	free(fullPathU);
}

void loadDroppedFile2(void)
{
	loadDroppedFile(oldFullPath, oldFullPathLen, oldAutoPlay, false);
}

module_t *createNewMod(void)
{
	uint8_t i;
	module_t *newMod;

	newMod = (module_t *)calloc(1, sizeof (module_t));
	if (newMod == NULL)
		goto oom;

	for (i = 0; i < MAX_PATTERNS; i++)
	{
		newMod->patterns[i] = (note_t *)calloc(1, MOD_ROWS * sizeof (note_t) * AMIGA_VOICES);
		if (newMod->patterns[i] == NULL)
			goto oom;
	}

	// +2 sample slots for overflow safety (Paula and scopes)
	newMod->sampleDataUnaligned = (int8_t *)CALLOC_PAD((MOD_SAMPLES + 2) * MAX_SAMPLE_LEN, 256);
	if (newMod->sampleDataUnaligned == NULL)
		goto oom;

	newMod->sampleData = (int8_t *)ALIGN_PTR(newMod->sampleDataUnaligned, 256);

	newMod->head.orderCount = 1;
	newMod->head.patternCount = 1;

	for (i = 0; i < MOD_SAMPLES; i++)
	{
		newMod->samples[i].offset = MAX_SAMPLE_LEN * i;
		newMod->samples[i].loopLength = 2;

		// setup GUI text pointers
		newMod->samples[i].volumeDisp = &newMod->samples[i].volume;
		newMod->samples[i].lengthDisp = &newMod->samples[i].length;
		newMod->samples[i].loopStartDisp = &newMod->samples[i].loopStart;
		newMod->samples[i].loopLengthDisp = &newMod->samples[i].loopLength;
	}

	for (i = 0; i < AMIGA_VOICES; i++)
		newMod->channels[i].n_chanindex = i;

	// setup GUI text pointers
	editor.currEditPatternDisp = &newMod->currPattern;
	editor.currPosDisp = &newMod->currOrder;
	editor.currPatternDisp = &newMod->head.order[0];
	editor.currPosEdPattDisp = &newMod->head.order[0];
	editor.currLengthDisp = &newMod->head.orderCount;

	editor.ui.updateSongSize = true;
	return newMod;

oom:
	showErrorMsgBox("Out of memory!");
	return NULL;
}
