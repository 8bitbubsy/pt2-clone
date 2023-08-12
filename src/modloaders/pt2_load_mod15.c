/* 15-sample Amiga MOD loader (The Ultimate Soundtracker, etc.)
**
** Note: Data sanitation is done in the last stage
** of module loading, so you don't need to do that here.
*/

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include "../pt2_header.h"
#include "../pt2_config.h"
#include "../pt2_structs.h"
#include "../pt2_replayer.h"
#include "../pt2_textout.h"
#include "../pt2_visuals.h"

static int32_t realSampleLengths[15];

module_t *loadMod15(uint8_t *buffer, uint32_t filesize)
{
	(void)filesize;

	module_t *m = createEmptyMod();
	if (m == NULL)
	{
		statusOutOfMemory();
		goto loadError;
	}

	bool veryLateSTKVerFlag = false; // "DFJ SoundTracker III" and later
	bool lateSTKVerFlag = false; // "TJC SoundTracker II" and later

	const uint8_t *p = buffer;
	memcpy(m->header.name, p, 20); p += 20;

	// read sample headers
	moduleSample_t *s = m->samples;
	for (int32_t i = 0; i < 15; i++, s++)
	{
		memcpy(s->text, p, 22); p += 22;

		realSampleLengths[i] = ((p[0] << 8) | p[1]) * 2; p += 2;
		s->length = (realSampleLengths[i] > config.maxSampleLength) ? config.maxSampleLength : realSampleLengths[i];

		/* Only late versions of Ultimate SoundTracker can have samples larger than 9999 bytes.
		** If detected, we know for sure that this is a late STK module.
		*/
		if (s->length > 9999)
			lateSTKVerFlag = true;

		p++; // hi-byte of volume, no finetune in STK/UST modules. Skip it.
		s->volume = *p++;

		// in The Ultimate SoundTracker, sample loop start is in bytes, not words
		s->loopStart = (p[0] << 8) | p[1]; p += 2;
		s->loopLength = ((p[0] << 8) | p[1]) * 2; p += 2;
	}

	m->header.songLength = *p++;

	if (m->header.songLength == 0 || m->header.songLength > 128)
	{
		displayErrorMsg("NOT A MOD FILE !");
		goto loadError;
	}

	uint8_t initTempo = *p++;
	if (initTempo > 220)
	{
		displayErrorMsg("NOT A MOD FILE !");
		goto loadError;
	}

	// 120 is a special case and means 50Hz (125BPM)
	if (initTempo == 0)
		initTempo = 120;

	// jjk55.mod by Jesper Kyd has a bogus STK tempo value that should be ignored (XXX: hackish...)
	if (!strcmp("jjk55", m->header.name))
		initTempo = 120;

	m->header.initialTempo = 125;
	if (initTempo != 120)
	{
		if (initTempo > 220)
			initTempo = 220;

		// convert UST tempo to BPM
		uint16_t ciaPeriod = (240 - initTempo) * 122;

		const double dHz = (double)CIA_PAL_CLK / (ciaPeriod+1); // +1, CIA triggers on underflow

		m->header.initialTempo = (uint16_t)((dHz * (125.0 / 50.0)) + 0.5);
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

	// load pattern data
	for (int32_t i = 0; i < numPatterns; i++)
	{
		note_t *note = m->patterns[i];
		for (int32_t j = 0; j < MOD_ROWS; j++)
		{
			for (int32_t k = 0; k < 4; k++, note++, p += 4)
			{
				note->period = ((p[0] & 0x0F) << 8) | p[1];
				note->sample = (p[0] & 0xF0) | (p[2] >> 4);
				note->command = p[2] & 0x0F;
				note->param = p[3];

				// added sanitation not present in original PT
				if (note->sample > 31)
					note->sample = 0;

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
	}

	// pattern command conversion
	for (int32_t i = 0; i < numPatterns; i++)
	{
		note_t *note = m->patterns[i];
		for (int32_t j = 0; j < MOD_ROWS*4; j++, note++)
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

	// load sample data
	s = m->samples;
	for (int32_t i = 0; i < 15; i++, s++)
	{
		// for Ultimate SoundTracker modules, only the loop area of a looped sample is played.
		if (s->loopStart > 0 && s->loopLength < s->length)
		{
			s->length -= s->loopStart;
			p += s->loopStart;
			s->loopStart = 0;
		}

		int32_t bytesToSkip = 0;
		if (realSampleLengths[i] > config.maxSampleLength)
			bytesToSkip = realSampleLengths[i] - config.maxSampleLength;

		// for Ultimate SoundTracker modules, don't load sample data after loop end
		int32_t loopEnd = s->loopStart + s->loopLength;
		if (loopEnd > 2 && s->length > loopEnd)
		{
			bytesToSkip += s->length-loopEnd;
			s->length = loopEnd;
		}

		memcpy(&m->sampleData[s->offset], p, s->length); p += s->length;
		if (bytesToSkip > 0)
			p += bytesToSkip;
	}

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
