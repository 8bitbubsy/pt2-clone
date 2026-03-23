/* 31-sample MOD loader (Amiga, PC, etc.)
**
** Note: Data sanitation is done in the last stage
** of module loading, so you don't need to do that here.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h> // isdigit()
#include <stdint.h>
#include <stdbool.h>
#include "../pt2_header.h"
#include "../pt2_paula.h" // PAULA_VOICES
#include "../pt2_config.h"
#include "../pt2_structs.h"
#include "../pt2_replayer.h"
#include "../pt2_textout.h"
#include "../pt2_visuals.h"

enum // 31-sample .MOD types
{
	FORMAT_UNKNOWN,

	FORMAT_PT,  // ProTracker or compatible
	FORMAT_FLT, // Startrekker (4 channels)
	FORMAT_FT2, // FT2 (or other trackers, multichannel)
	FORMAT_NT,  // NoiseTracker
	FORMAT_HMNT // His Master's NoiseTracker (special one)
};

static int32_t realSampleLengths[MOD_SAMPLES];

static uint8_t getMod31Type(uint8_t *buffer, uint32_t filesize, uint8_t *numChannels); // 0 = not detected
bool detectMod31(uint8_t *buffer, uint32_t filesize);

module_t *loadMod31(uint8_t *buffer, uint32_t filesize)
{
	module_t *m = createEmptyMod();
	if (m == NULL)
	{
		statusOutOfMemory();
		goto loadError;
	}

	uint8_t numChannels;
	uint8_t modFormat = getMod31Type(buffer, filesize, &numChannels);
	if (modFormat == FORMAT_UNKNOWN || numChannels > PAULA_VOICES)
	{
		displayErrorMsg("UNSUPPORTED MOD !");
		goto loadError;
	}

	uint8_t *p = buffer;

	memcpy(m->header.name, p, 20); p += 20;

	// read sample headers
	moduleSample_t *s = m->samples;
	for (int32_t i = 0; i < MOD_SAMPLES; i++, s++)
	{
		memcpy(s->text, p, 22); p += 22;

		realSampleLengths[i] = ((p[0] << 8) | p[1]) * 2; p += 2;
		s->length = (realSampleLengths[i] > config.maxSampleLength) ? config.maxSampleLength : realSampleLengths[i];

		s->fineTune = *p++ & 0xF;
		s->volume = *p++;

		s->loopStart = ((p[0] << 8) | p[1]) * 2; p += 2;
		s->loopLength = ((p[0] << 8) | p[1]) * 2; p += 2;

		// fix for poorly converted STK (< v2.5) -> PT/NT modules
		if (s->loopLength > 2 && s->loopStart+s->loopLength > s->length)
		{
			if ((s->loopStart/2) + s->loopLength <= s->length)
				s->loopStart /= 2;
		}
	}

	m->header.songLength = *p++;
	p++; // skip uninteresting field (127 in PT MOds, "restart pos" in PC MODs)

	if (modFormat == FORMAT_PT && m->header.songLength == 129)
		m->header.songLength = 127; // fixes a specific copy of beatwave.mod (XXX: hackish...)

	if (m->header.songLength == 0 || m->header.songLength > 129)
	{
		displayErrorMsg("NOT A MOD FILE !");
		goto loadError;
	}

	// read orders and count number of patterns
	int32_t numPatterns = 0;
	for (int32_t i = 0; i < 128; i++)
	{
		m->header.patternTable[i] = *p++;
		if (m->header.patternTable[i] > numPatterns)
			numPatterns = m->header.patternTable[i];
	}
	numPatterns++;

	if (numPatterns > MAX_PATTERNS)
	{
		displayErrorMsg("UNSUPPORTED MOD !");
		goto loadError;
	}

	p += 4; // skip magic ID (already handled)

	// load pattern data
	for (int32_t i = 0; i < numPatterns; i++)
	{
		note_t *note = m->patterns[i];
		for (int32_t j = 0; j < MOD_ROWS; j++)
		{
			for (int32_t k = 0; k < numChannels; k++, note++, p += 4)
			{
				note->period = ((p[0] & 0x0F) << 8) | p[1];
				note->sample = ((p[0] & 0xF0) | (p[2] >> 4)) & 31;
				note->command = p[2] & 0x0F;
				note->param = p[3];
			}

			if (numChannels < PAULA_VOICES)
				note += PAULA_VOICES-numChannels;
		}
	}

	if (modFormat != FORMAT_PT) // pattern command conversion for non-PT formats
	{
		for (int32_t i = 0; i < numPatterns; i++)
		{
			note_t *note = m->patterns[i];
			for (int32_t j = 0; j < MOD_ROWS*4; j++, note++)
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

				// remove E8x (Karplus-Strong is only supported for ProTracker .MODs)
				if (note->command == 0xE && (note->param >> 4) == 0x8)
				{
					note->command = 0;
					note->param = 0;
				}

				// remove EFx (Invert Loop is only supported for ProTracker .MODs)
				if (note->command == 0xE && (note->param >> 4) == 0xF)
				{
					note->command = 0;
					note->param = 0;
				}
			}
		}
	}

	// load sample data
	s = m->samples;
	for (int32_t i = 0; i < MOD_SAMPLES; i++, s++)
	{
		int32_t bytesToSkip = 0;
		if (realSampleLengths[i] > config.maxSampleLength)
			bytesToSkip = realSampleLengths[i] - config.maxSampleLength;

		memcpy(&m->sampleData[s->offset], p, s->length); p += s->length;
		if (bytesToSkip > 0)
			p += bytesToSkip;
	}

	m->header.initialTempo = 125;
	return m;

loadError:
	if (m != NULL)
	{
		for (int32_t i = 0; i < MAX_PATTERNS; i++)
		{
			if (m->patterns[i] != NULL)
				free(m->patterns[i]);
		}

		if (m->sampleData != NULL)
			free(m->sampleData);

		free(m);
	}

	return NULL;
}

static uint8_t getMod31Type(uint8_t *buffer, uint32_t filesize, uint8_t *numChannels) // 0 = not detected
{
	const uint8_t *id = &buffer[1080];
#define ID(s) !memcmp(s, id, 4)

	*numChannels = 0;
	if (buffer == NULL || filesize < 1084+1024)
		return FORMAT_UNKNOWN;

	*numChannels = 4;
	if (ID("M.K.") || ID("M!K!") || ID("NSMS") || ID("LARD") || ID("PATT"))
	{
		return FORMAT_PT; // ProTracker (or compatible)
	}
	else if (ID("FLT4"))
	{
		return FORMAT_FLT; // Startrekker (4 channels)
	}
	else if (ID("N.T."))
	{
		return FORMAT_NT; // NoiseTracker
	}
	else if (ID("M&K!") || ID("FEST"))
	{
		return FORMAT_HMNT; // His Master's NoiseTracker
	}
	else if (isdigit(id[0]) && (id[1] == 'C' && id[2] == 'H' && id[3] == 'N'))
	{
		*numChannels = id[0] - '0';
		return FORMAT_FT2; // Fasttracker II 1..9 channels (or other trackers)
	}
	else if (isdigit(id[0]) && isdigit(id[1]) && id[1] == 'C' && id[2] == 'H')
	{
		*numChannels = ((id[0] - '0') * 10) + (id[1] - '0');
		return FORMAT_FT2; // Fasttracker II 10+ channels (or other trackers)
	}

	return FORMAT_UNKNOWN;
}

bool detectMod31(uint8_t *buffer, uint32_t filesize)
{
	uint8_t junk;

	if (buffer == NULL || filesize < 1084+1024)
		return FORMAT_UNKNOWN;

	return getMod31Type(buffer, filesize, &junk) != FORMAT_UNKNOWN;
}
