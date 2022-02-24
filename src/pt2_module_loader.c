// for finding memory leaks in debug mode with Visual Studio 
#if defined _DEBUG && defined _MSC_VER
#include <crtdbg.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
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
#include "pt2_config.h"
#include "pt2_sampler.h"
#include "pt2_textout.h"
#include "pt2_audio.h"
#include "pt2_helpers.h"
#include "pt2_visuals.h"
#include "pt2_unicode.h"
#include "pt2_module_loader.h"
#include "pt2_sample_loader.h"
#include "pt2_config.h"
#include "pt2_sampling.h"
#include "pt2_xpk.h"

typedef struct mem_t
{
	bool _eof;
	uint8_t *_ptr, *_base;
	uint32_t _cnt, _bufsiz;
} MEMFILE;

static bool oldAutoPlay;
static char oldFullPath[(PATH_MAX * 2) + 2];
static int32_t realSampleLengths[MOD_SAMPLES];
static uint32_t oldFullPathLen;

static MEMFILE *mopen(const uint8_t *src, uint32_t length);
static void mclose(MEMFILE **buf);
static int32_t mgetc(MEMFILE *buf);
static size_t mread(void *buffer, size_t size, size_t count, MEMFILE *buf);
static void mseek(MEMFILE *buf, int32_t offset, int32_t whence);
static void mrewind(MEMFILE *buf);
static uint8_t ppdecrunch(uint8_t *src, uint8_t *dst, uint8_t *offsetLens, uint32_t srcLen, uint32_t dstLen, uint8_t skipBits);

void showSongUnsavedAskBox(int8_t askScreenType)
{
	ui.askScreenShown = true;
	ui.askScreenType = askScreenType;

	pointerSetMode(POINTER_MODE_MSG1, NO_CARRY);
	setStatusMessage("SONG IS UNSAVED !", NO_CARRY);
	renderAskDialog();
}

#define IS_ID(s, b) !strncmp(s, b, 4)

static uint8_t getModType(uint8_t *numChannels, const char *id)
{
	*numChannels = 4;

	if (IS_ID("M.K.", id) || IS_ID("M!K!", id) || IS_ID("NSMS", id) || IS_ID("LARD", id) || IS_ID("PATT", id))
	{
		return FORMAT_MK; // ProTracker (or compatible)
	}
	else if (IS_ID("FLT4", id))
	{
		return FORMAT_FLT; // Startrekker (4 channels)
	}
	else if (IS_ID("N.T.", id))
	{
		return FORMAT_NT; // NoiseTracker
	}
	else if (IS_ID("M&K!", id) || IS_ID("FEST", id))
	{
		return FORMAT_HMNT; // His Master's NoiseTracker
	}
	else if (id[1] == 'C' && id[2] == 'H' && id[3] == 'N')
	{
		*numChannels = id[0] - '0';
		return FORMAT_FT2; // Fasttracker II 1..9 channels (or other trackers)
	}

	return FORMAT_UNKNOWN; // may be The Ultimate Soundtracker (set to FORMAT_STK later)
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

static uint8_t *unpackPPModule(FILE *f, uint32_t *filesize)
{
	uint8_t *modBuffer, ppCrunchData[4], *ppBuffer;
	uint32_t ppPackLen, ppUnpackLen;

	ppPackLen = *filesize;
	if ((ppPackLen & 3) || ppPackLen <= 12)
	{
		displayErrorMsg("POWERPACKER ERROR");
		return NULL;
	}

	ppBuffer = (uint8_t *)malloc(ppPackLen);
	if (ppBuffer == NULL)
	{
		statusOutOfMemory();
		return NULL;
	}

	fseek(f, ppPackLen-4, SEEK_SET);

	ppCrunchData[0] = (uint8_t)fgetc(f);
	ppCrunchData[1] = (uint8_t)fgetc(f);
	ppCrunchData[2] = (uint8_t)fgetc(f);
	ppCrunchData[3] = (uint8_t)fgetc(f);

	ppUnpackLen = (ppCrunchData[0] << 16) | (ppCrunchData[1] << 8) | ppCrunchData[2];

	modBuffer = (uint8_t *)malloc(ppUnpackLen);
	if (modBuffer == NULL)
	{
		free(ppBuffer);
		statusOutOfMemory();
		return NULL;
	}

	rewind(f);
	fread(ppBuffer, 1, ppPackLen, f);
	fclose(f);

	if (!ppdecrunch(ppBuffer+8, modBuffer, ppBuffer+4, ppPackLen-12, ppUnpackLen, ppCrunchData[3]))
	{
		free(ppBuffer);
		displayErrorMsg("POWERPACKER ERROR");
		return NULL;
	}

	free(ppBuffer);
	*filesize = ppUnpackLen;
	return modBuffer;
}

module_t *modLoad(UNICHAR *fileName)
{
	bool mightBeSTK, lateSTKVerFlag, veryLateSTKVerFlag;
	char modID[4], tmpChar;
	int8_t numSamples;
	uint8_t bytes[4], restartPos, modFormat;
	uint8_t *modBuffer, numChannels;
	int32_t i, j, k, loopStart, loopLength, loopOverflowVal, numPatterns;
	uint32_t powerPackerID, filesize;
	FILE *f;
	MEMFILE *m;
	module_t *newMod;
	moduleSample_t *s;
	note_t *note;

	veryLateSTKVerFlag = false; // "DFJ SoundTracker III" and later
	lateSTKVerFlag = false; // "TJC SoundTracker II" and later
	mightBeSTK = false;

	m = NULL;
	f = NULL;
	modBuffer = NULL;

	newMod = (module_t *)calloc(1, sizeof (module_t));
	if (newMod == NULL)
	{
		statusOutOfMemory();
		goto modLoadError;
	}

	f = UNICHAR_FOPEN(fileName, "rb");
	if (f == NULL)
	{
		displayErrorMsg("FILE I/O ERROR !");
		goto modLoadError;
	}

	fseek(f, 0, SEEK_END);
	filesize = ftell(f);
	rewind(f);

	// check if mod is a powerpacker mod
	fread(&powerPackerID, 4, 1, f);
	if (powerPackerID == 0x30325850) // "PX20"
	{
		displayErrorMsg("ENCRYPTED MOD !");
		goto modLoadError;
	}
	else if (powerPackerID == 0x30325050) // "PP20"
	{
		modBuffer = unpackPPModule(f, &filesize);
		if (modBuffer == NULL)
			goto modLoadError; // error msg is set in unpackPPModule()
	}
	else
	{
		if (DetectXPK(f))
		{
			if (!UnpackXPK(f, &filesize, &modBuffer))
			{
				displayErrorMsg("XPK UNPACK ERROR !");
				goto modLoadError;
			}

			if (modBuffer == NULL)
			{
				statusOutOfMemory();
				goto modLoadError;
			}

			fclose(f);
		}
		else
		{
			modBuffer = (uint8_t *)malloc(filesize);

			if (modBuffer == NULL)
			{
				statusOutOfMemory();
				goto modLoadError;
			}

			fseek(f, 0, SEEK_SET);
			fread(modBuffer, 1, filesize, f);
			fclose(f);
		}
	}

	// smallest and biggest possible PT .MOD
	if (filesize < 2108 || filesize > 4195326)
	{
		displayErrorMsg("NOT A MOD FILE !");
		goto modLoadError;
	}

	// Use MEMFILE functions on module buffer (similar to FILE functions)

	m = mopen(modBuffer, filesize);
	if (m == NULL)
	{
		displayErrorMsg("FILE I/O ERROR !");
		goto modLoadError;
	}

	// check magic ID
	memset(modID, 0, 4); // in case mread fails
	mseek(m, 1080, SEEK_SET);
	mread(modID, 1, 4, m);

	modFormat = getModType(&numChannels, modID);
	if (numChannels == 0 || numChannels > AMIGA_VOICES)
	{
		displayErrorMsg("UNSUPPORTED MOD !");
		goto modLoadError;
	}

	if (modFormat == FORMAT_UNKNOWN)
		mightBeSTK = true;

	mrewind(m);
	mread(newMod->header.name, 1, 20, m);
	newMod->header.name[20] = '\0';

	// convert illegal song name characters to space
	for (i = 0; i < 20; i++)
	{
		tmpChar = newMod->header.name[i];
		if ((tmpChar < ' ' || tmpChar > '~') && tmpChar != '\0')
			tmpChar = ' ';

		newMod->header.name[i] = tmpChar;
	}

	fixZeroesInString(newMod->header.name, 20);

	// read sample headers
	s = newMod->samples;
	for (i = 0; i < MOD_SAMPLES; i++, s++)
	{
		if (mightBeSTK && i >= 15) // skip reading sample headers past sample slot 15 in STK/UST modules
		{
			s->loopLength = 2; // this be set though
			continue;
		}

		mread(s->text, 1, 22, m);
		s->text[22] = '\0';

		if (modFormat == FORMAT_HMNT)
		{
			// most of "His Master's Noisetracker" songs have junk sample names, so let's wipe it.
			memset(s->text, 0, 22);
		}
		else
		{
			// convert illegal sample name characters to space
			for (j = 0; j < 22; j++)
			{
				tmpChar = s->text[j];
				if ((tmpChar < ' ' || tmpChar > '~') && tmpChar != '\0')
					tmpChar = ' ';

				s->text[j] = tmpChar;
			}

			fixZeroesInString(s->text, 22);
		}

		realSampleLengths[i] = ((mgetc(m) << 8) | mgetc(m)) * 2;
		s->length = (realSampleLengths[i] > config.maxSampleLength) ? config.maxSampleLength : realSampleLengths[i];

		/* Only late versions of Ultimate SoundTracker could have samples larger than 9999 bytes.
		** If found, we know for sure that this is a late STK module.
		*/
		if (mightBeSTK && s->length > 9999)
			lateSTKVerFlag = true;

		if (modFormat == FORMAT_HMNT) // finetune in "His Master's NoiseTracker" is different
			s->fineTune = (uint8_t)((-mgetc(m) & 0x1F) / 2); // one more bit of precision, + inverted
		else
			s->fineTune = (uint8_t)mgetc(m) & 0xF;

		if (mightBeSTK)
			s->fineTune = 0; // this is high byte of volume in STK/UST (has no finetune), set to zero

		s->volume = (int8_t)mgetc(m);
		if ((uint8_t)s->volume > 64)
			s->volume = 64;

		loopStart = ((mgetc(m) << 8) | mgetc(m)) * 2;
		loopLength = ((mgetc(m) << 8) | mgetc(m)) * 2;

		if (loopLength < 2)
			loopLength = 2; // fixes empty samples in .MODs saved from FT2

		// we don't support samples bigger than 65534 (or 128kB) bytes, disable uncompatible loops
		if (loopStart > config.maxSampleLength || loopStart+loopLength > config.maxSampleLength)
		{
			s->loopStart = 0;
			s->loopLength = 2;
		}
		else
		{
			s->loopStart = loopStart;
			s->loopLength = loopLength;
		}

		// in The Ultimate SoundTracker, sample loop start is in bytes, not words
		if (mightBeSTK)
			s->loopStart /= 2;

		// fix for poorly converted STK (< v2.5) -> PT/NT modules
		if (!mightBeSTK && s->loopLength > 2 && s->loopStart+s->loopLength > s->length)
		{
			if ((s->loopStart/2) + s->loopLength <= s->length)
				s->loopStart /= 2;
		}
	}

	newMod->header.numOrders = (uint8_t)mgetc(m);

	if (modFormat == FORMAT_MK && newMod->header.numOrders == 129)
		newMod->header.numOrders = 127; // fixes a specific copy of beatwave.mod

	if (newMod->header.numOrders > 129)
	{
		displayErrorMsg("NOT A MOD FILE !");
		goto modLoadError;
	}

	if (newMod->header.numOrders == 0)
	{
		displayErrorMsg("NOT A MOD FILE !");
		goto modLoadError;
	}

	restartPos = (uint8_t)mgetc(m);
	if (mightBeSTK && restartPos > 220)
	{
		displayErrorMsg("NOT A MOD FILE !");
		goto modLoadError;
	}

	newMod->header.initialTempo = 125;
	if (mightBeSTK)
	{
		/* If we're still here at this point and the mightBeSTK flag is set,
		** then it's most likely a proper The Ultimate SoundTracker (STK/UST) module.
		*/
		modFormat = FORMAT_STK;

		if (restartPos == 0)
			restartPos = 120;

		// jjk55.mod by Jesper Kyd has a bogus STK tempo value that should be ignored
		if (!strcmp("jjk55", newMod->header.name))
			restartPos = 120;

		// the "restart pos" field in STK is the inital tempo (must be converted to BPM first)
		if (restartPos != 120) // 120 is a special case and means 50Hz (125BPM)
		{
			if (restartPos > 220)
				restartPos = 220;

			// convert UST tempo to BPM
			uint16_t ciaPeriod = (240 - restartPos) * 122;
			double dHz = (double)CIA_PAL_CLK / ciaPeriod;
			int32_t BPM = (int32_t)((dHz * (125.0 / 50.0)) + 0.5);

			newMod->header.initialTempo = (uint16_t)BPM;
		}
	}

	// read orders and count number of patterns
	numPatterns = 0;
	for (i = 0; i < MOD_ORDERS; i++)
	{
		newMod->header.order[i] = (int16_t)mgetc(m);
		if (newMod->header.order[i] > numPatterns)
			numPatterns = newMod->header.order[i];
	}
	numPatterns++;

	if (numPatterns > MAX_PATTERNS)
	{
		displayErrorMsg("UNSUPPORTED MOD !");
		goto modLoadError;
	}

	// skip magic ID (The Ultimate SoundTracker MODs doesn't have it)
	if (modFormat != FORMAT_STK)
		mseek(m, 4, SEEK_CUR);

	// allocate 100 patterns
	for (i = 0; i < MAX_PATTERNS; i++)
	{
		newMod->patterns[i] = (note_t *)calloc(MOD_ROWS * AMIGA_VOICES, sizeof (note_t));
		if (newMod->patterns[i] == NULL)
		{
			statusOutOfMemory();
			goto modLoadError;
		}
	}

	// load pattern data
	for (i = 0; i < numPatterns; i++)
	{
		note = newMod->patterns[i];
		for (j = 0; j < MOD_ROWS; j++)
		{
			for (k = 0; k < numChannels; k++, note++)
			{
				mread(bytes, 1, 4, m);

				note->period = ((bytes[0] & 0x0F) << 8) | bytes[1];
				note->sample = (bytes[0] & 0xF0) | (bytes[2] >> 4);
				note->command = bytes[2] & 0x0F;
				note->param = bytes[3];

				if (modFormat == FORMAT_STK)
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
			}

			if (numChannels < AMIGA_VOICES)
				note += AMIGA_VOICES-numChannels;
		}
	}

	// pattern command conversion for non-PT formats
	if (modFormat == FORMAT_STK || modFormat == FORMAT_FT2 || modFormat == FORMAT_NT || modFormat == FORMAT_HMNT || modFormat == FORMAT_FLT)
	{
		for (i = 0; i < numPatterns; i++)
		{
			note = newMod->patterns[i];
			for (j = 0; j < MOD_ROWS*4; j++, note++)
			{
				if (modFormat == FORMAT_NT || modFormat == FORMAT_HMNT)
				{
					// any Dxx == D00 in NT/HMNT
					if (note->command == 0xD)
						note->param = 0;

					// effect F with param 0x00 does nothing in NT/HMNT
					if (note->command == 0xF && note->param == 0)
						note->command = 0;
				}
				else if (modFormat == FORMAT_FLT) // Startrekker (4 channels)
				{
					if (note->command == 0xE) // remove unsupported "assembly macros" command
					{
						note->command = 0;
						note->param = 0;
					}

					// Startrekker is always in vblank mode, and limits speed to 0x1F
					if (note->command == 0xF && note->param > 0x1F)
						note->param = 0x1F;
				}
				else if (modFormat == FORMAT_STK)
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

						// TODO: This needs more detection and is NOT correct!
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

					// effect F with param 0x00 does nothing in UST/STK (I think?)
					if (note->command == 0xF && note->param == 0)
						note->command = 0;
				}

				// remove sample-trashing effects that were only present in ProTracker

				// remove E8x (Karplus-Strong in ProTracker)
				if (note->command == 0xE && (note->param >> 4) == 0x8)
				{
					note->command = 0;
					note->param = 0;
				}

				// remove EFx (Invert Loop in ProTracker)
				if (note->command == 0xE && (note->param >> 4) == 0xF)
				{
					note->command = 0;
					note->param = 0;
				}
			}
		}
	}

	newMod->sampleData = allocMemForAllSamples();
	if (newMod->sampleData == NULL)
	{
		statusOutOfMemory();
		goto modLoadError;
	}

	// set sample data offsets (sample data = one huge buffer to rule them all)
	for (i = 0; i < MOD_SAMPLES; i++)
		newMod->samples[i].offset = config.maxSampleLength * i;

	// load sample data
	numSamples = (modFormat == FORMAT_STK) ? 15 : 31;
	s = newMod->samples;
	for (i = 0; i < numSamples; i++, s++)
	{
		/* For Ultimate SoundTracker modules, only the loop area of a looped sample is played.
		** Skip loading of eventual data present before loop start.
		*/
		if (modFormat == FORMAT_STK && s->loopStart > 0 && s->loopLength < s->length)
		{
			s->length -= s->loopStart;
			mseek(m, s->loopStart, SEEK_CUR);
			s->loopStart = 0;
		}

		/* We don't support loading samples bigger than 65534 bytes in our PT2 clone,
		** so skip overflown data in .MOD file if present.
		*/
		int32_t bytesToSkip = 0;
		if (realSampleLengths[i] > config.maxSampleLength)
			bytesToSkip = realSampleLengths[i] - config.maxSampleLength;

		// For Ultimate SoundTracker modules, don't load sample data after loop end
		int32_t loopEnd = s->loopStart + s->loopLength;
		if (modFormat == FORMAT_STK && loopEnd > 2 && s->length > loopEnd)
		{
			bytesToSkip += s->length-loopEnd;
			s->length = loopEnd;
		}

		mread(&newMod->sampleData[s->offset], 1, s->length, m);
		if (bytesToSkip > 0)
			mseek(m, bytesToSkip, SEEK_CUR);

		// clear first two bytes of non-looping samples to prevent beep after sample has been played
		if (s->length >= 2 && loopEnd <= 2)
		{
			newMod->sampleData[s->offset+0] = 0;
			newMod->sampleData[s->offset+1] = 0;
		}

		// some modules are broken like this, adjust sample length if possible (this is ok if we have room)
		if (s->length > 0 && s->loopLength > 2 && s->loopStart+s->loopLength > s->length)
		{
			loopOverflowVal = (s->loopStart + s->loopLength) - s->length;
			if (s->length+loopOverflowVal <= config.maxSampleLength)
			{
				s->length += loopOverflowVal; // this is safe, we're allocating 65534 bytes per sample slot
			}
			else
			{
				s->loopStart = 0;
				s->loopLength = 2;
			}
		}
	}

	mclose(&m);
	free(modBuffer);

	for (i = 0; i < AMIGA_VOICES; i++)
		newMod->channels[i].n_chanindex = (int8_t)i;

	return newMod;

modLoadError:
	if (m != NULL)
		mclose(&m);

	if (modBuffer != NULL)
		free(modBuffer);

	if (newMod != NULL)
	{
		for (i = 0; i < MAX_PATTERNS; i++)
		{
			if (newMod->patterns[i] != NULL)
				free(newMod->patterns[i]);
		}

		free(newMod);
	}

	return NULL;
}

static MEMFILE *mopen(const uint8_t *src, uint32_t length)
{
	MEMFILE *b;

	if (src == NULL || length == 0)
		return NULL;

	b = (MEMFILE *)malloc(sizeof (MEMFILE));
	if (b == NULL)
		return NULL;

	b->_base = (uint8_t *)src;
	b->_ptr = (uint8_t *)src;
	b->_cnt = length;
	b->_bufsiz = length;
	b->_eof = false;

	return b;
}

static void mclose(MEMFILE **buf)
{
	if (*buf != NULL)
	{
		free(*buf);
		*buf = NULL;
	}
}

static int32_t mgetc(MEMFILE *buf)
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

static size_t mread(void *buffer, size_t size, size_t count, MEMFILE *buf)
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

	return pcnt / size;
}

static void mseek(MEMFILE *buf, int32_t offset, int32_t whence)
{
	if (buf == NULL)
		return;

	if (buf->_base)
	{
		switch (whence)
		{
			case SEEK_SET: buf->_ptr = buf->_base + offset; break;
			case SEEK_CUR: buf->_ptr += offset; break;
			case SEEK_END: buf->_ptr = buf->_base + buf->_bufsiz + offset; break;
			default: break;
		}

		buf->_eof = false;
		if (buf->_ptr >= buf->_base+buf->_bufsiz)
		{
			buf->_ptr = buf->_base + buf->_bufsiz;
			buf->_eof = true;
		}

		buf->_cnt = (uint32_t)((buf->_base + buf->_bufsiz) - buf->_ptr);
	}
}

static void mrewind(MEMFILE *buf)
{
	mseek(buf, 0, SEEK_SET);
}

/* PowerPacker unpack code taken from Heikki Orsila's amigadepack. Seems to have no license,
** so I'll assume it fits into BSD 3-Clause. If not, feel free to contact me at my email
** address found at the bottom of 16-bits.org.
**
** Modified by 8bitbubsy (me).
*/

#define PP_READ_BITS(nbits, var) \
	bitCnt = (nbits); \
	while (bitsLeft < bitCnt) \
	{ \
		if (bufSrc < src) \
			return false; \
		bitBuffer |= ((*--bufSrc) << bitsLeft); \
		bitsLeft += 8; \
	} \
	(var) = 0; \
	bitsLeft -= bitCnt; \
	while (bitCnt--) \
	{ \
		(var) = ((var) << 1) | (bitBuffer & 1); \
		bitBuffer >>= 1; \
	} \

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

void setupLoadedMod(void)
{
	int8_t i;

	// setup GUI text pointers
	for (i = 0; i < MOD_SAMPLES; i++)
	{
		song->samples[i].volumeDisp = &song->samples[i].volume;
		song->samples[i].lengthDisp = &song->samples[i].length;
		song->samples[i].loopStartDisp = &song->samples[i].loopStart;
		song->samples[i].loopLengthDisp = &song->samples[i].loopLength;

		fillSampleRedoBuffer(i);
	}

	modSetPos(0, 0);
	modSetPattern(0); // set pattern to 00 instead of first order's pattern

	editor.currEditPatternDisp = &song->currPattern;
	editor.currPosDisp = &song->currOrder;
	editor.currPatternDisp = &song->header.order[0];
	editor.currPosEdPattDisp = &song->header.order[0];
	editor.currLengthDisp = &song->header.numOrders;

	// calculate MOD size
	ui.updateSongSize = true;

	editor.muted[0] = false;
	editor.muted[1] = false;
	editor.muted[2] = false;
	editor.muted[3] = false;

	editor.editMoveAdd = 1;
	editor.currSample = 0;
	editor.musicTime64 = 0;
	editor.modLoaded = true;
	editor.blockMarkFlag = false;
	editor.sampleZero = false;
	editor.hiLowInstr = 0;

	setLEDFilter(false, false); // real PT doesn't do this, but that's insane

	updateWindowTitle(MOD_NOT_MODIFIED);

	editor.timingMode = TEMPO_MODE_CIA;
	updateReplayerTimingMode();

	modSetSpeed(6);
	modSetTempo(song->header.initialTempo, false); // 125 for normal MODs, custom value for certain STK/UST MODs

	updateCurrSample();
	editor.samplePos = 0;
	updateSamplePos();
}

void loadModFromArg(char *arg)
{
	uint32_t filenameLen;
	UNICHAR *filenameU;

	ui.introScreenShown = false;
	statusAllRight();

	filenameLen = (uint32_t)strlen(arg);

	filenameU = (UNICHAR *)calloc(filenameLen + 2, sizeof (UNICHAR));
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

	module_t *newSong = modLoad(filenameU);
	if (newSong != NULL)
	{
		song->loaded = false;
		modFree();
		song = newSong;
		setupLoadedMod();
		song->loaded = true;
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
	if (ui.pointerMode == POINTER_MODE_MSG1 || diskop.isFilling ||
		editor.isWAVRendering || editor.isSMPRendering ||
		ui.samplerFiltersBoxShown || ui.samplerVolBoxShown || ui.samplingBoxShown)
	{
		return;
	}

	ansiName = (char *)calloc(fullPathLen + 10, sizeof (char));
	if (ansiName == NULL)
	{
		statusOutOfMemory();
		return;
	}

	fullPathU = (UNICHAR *)calloc(fullPathLen + 2, sizeof (UNICHAR));
	if (fullPathU == NULL)
	{
		free(ansiName);
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
		if (songModifiedCheck && song->modified)
		{
			free(ansiName);
			free(fullPathU);

			memcpy(oldFullPath, fullPath, fullPathLen);
			oldFullPath[fullPathLen+0] = 0;
			oldFullPath[fullPathLen+1] = 0;

			oldFullPathLen = fullPathLen;
			oldAutoPlay = autoPlay;

			// de-minimize window and set focus so that the user sees the message box
			SDL_RestoreWindow(video.window);
			SDL_RaiseWindow(video.window);

			showSongUnsavedAskBox(ASK_DISCARD_SONG_DRAGNDROP);
			return;
		}

		module_t *newSong = modLoad(fullPathU);
		if (newSong != NULL)
		{
			oldMode = editor.currMode;
			oldPlayMode = editor.playMode;

			modStop();
			modFree();

			song = newSong;
			setupLoadedMod();
			song->loaded = true;

			statusAllRight();

			if (autoPlay)
			{
				editor.playMode = PLAY_MODE_NORMAL;
				editor.currMode = MODE_PLAY;

				// start normal playback
				modPlay(DONT_SET_PATTERN, 0, 0);

				pointerSetMode(POINTER_MODE_PLAY, DO_CARRY);
			}
			else if (oldMode == MODE_PLAY || oldMode == MODE_RECORD)
			{
				// use last mode
				editor.playMode = oldPlayMode;
				if (oldPlayMode == PLAY_MODE_PATTERN || oldMode == MODE_RECORD)
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

module_t *createEmptyMod(void)
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

	newMod->sampleData = allocMemForAllSamples();
	if (newMod->sampleData == NULL)
		goto oom;

	newMod->header.numOrders = 1;

	moduleSample_t *s = newMod->samples;
	for (i = 0; i < MOD_SAMPLES; i++, s++)
	{
		s->offset = config.maxSampleLength * i;
		s->loopLength = 2;

		// setup GUI text pointers
		s->volumeDisp = &s->volume;
		s->lengthDisp = &s->length;
		s->loopStartDisp = &s->loopStart;
		s->loopLengthDisp = &s->loopLength;
	}

	for (i = 0; i < AMIGA_VOICES; i++)
		newMod->channels[i].n_chanindex = i;

	// setup GUI text pointers
	editor.currEditPatternDisp = &newMod->currPattern;
	editor.currPosDisp = &newMod->currOrder;
	editor.currPatternDisp = &newMod->header.order[0];
	editor.currPosEdPattDisp = &newMod->header.order[0];
	editor.currLengthDisp = &newMod->header.numOrders;

	ui.updateSongSize = true;
	return newMod;

oom:
	showErrorMsgBox("Out of memory!");
	return NULL;
}
