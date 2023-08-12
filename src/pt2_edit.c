// for finding memory leaks in debug mode with Visual Studio 
#if defined _DEBUG && defined _MSC_VER
#include <crtdbg.h>
#endif

#include <stdint.h>
#include <stdbool.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#else
#include <unistd.h>
#endif
#include "pt2_helpers.h"
#include "pt2_textout.h"
#include "pt2_tables.h"
#include "pt2_diskop.h"
#include "pt2_sampler.h"
#include "pt2_visuals.h"
#include "pt2_keyboard.h"
#include "pt2_config.h"
#include "pt2_audio.h"
#include "pt2_chordmaker.h"
#include "pt2_edit.h"
#include "pt2_replayer.h"
#include "pt2_visuals_sync.h"

static const int8_t scancode2NoteLo[52] = // "USB usage page standard" order
{
	 7,  4,  3, 16, -1,  6,  8, 24,
	10, -1, 13, 11,  9, 26, 28, 12,
	17,  1, 19, 23,  5, 14,  2, 21,
	 0, -1, 13, 15, -1, 18, 20, 22,
	-1, 25, 27, -1, -1, -1, -1, -1,
	-1, 30, 29, 31, 30, -1, 15, -1,
	-1, 12, 14, 16
};

static const int8_t scancode2NoteHi[52] = // "USB usage page standard" order
{
	19, 16, 15, 28, -1, 18, 20, -2,
	22, -1, 25, 23, 21, -2, -2, 24,
	29, 13, 31, 35, 17, 26, 14, 33,
	12, -1, 25, 27, -1, 30, 32, 34,
	-1, -2, -2, -1, -1, -1, -1, -1,
	-1, -2, -2, -2, -2, -1, 27, -1,
	-1, 24, 26, 28
};

static uint8_t quantizeCheck(uint8_t row)
{
	assert(song != NULL);
	if (song == NULL)
		return row;

	const uint8_t quantize = (uint8_t)config.quantizeValue;

	editor.didQuantize = false;
	if (editor.currMode == MODE_RECORD)
	{
		if (quantize == 0)
		{
			return row;
		}
		else if (quantize == 1)
		{
			if (song->tick > song->speed>>1)
			{
				row = (row + 1) & 63;
				editor.didQuantize = true;
			}
		}
		else
		{
			uint8_t tempRow = ((((quantize >> 1) + row) & 63) / quantize) * quantize;
			if (tempRow > row)
				editor.didQuantize = true;

			return tempRow;
		}
	}

	return row;
}

static void jamAndPlaceSample(SDL_Scancode scancode, bool normalMode)
{
	uint8_t chNum = cursor.channel;
	assert(chNum < PAULA_VOICES);

	moduleChannel_t *ch = &song->channels[chNum];
	note_t *note = &song->patterns[song->currPattern][(quantizeCheck(song->currRow) * PAULA_VOICES) + chNum];

	int8_t noteVal = normalMode ? keyToNote(scancode) : pNoteTable[editor.currSample];
	if (noteVal >= 0)
	{
		moduleSample_t *s = &song->samples[editor.currSample];

		int16_t tempPeriod  = periodTable[((s->fineTune & 0xF) * 37) + noteVal];
		uint16_t cleanPeriod = periodTable[noteVal];

		editor.currPlayNote = noteVal;

		// play current sample

		// don't play sample if we quantized to another row (will be played in modplayer instead)
		if (editor.currMode != MODE_RECORD || !editor.didQuantize)
		{
			lockAudio();

			ch->n_samplenum = editor.currSample;
			ch->n_volume = s->volume;
			ch->n_period = tempPeriod;
			ch->n_start = &song->sampleData[s->offset];
			ch->n_length = (uint16_t)((s->loopStart > 0) ? (s->loopStart + s->loopLength) >> 1 : s->length >> 1);
			ch->n_loopstart = &song->sampleData[s->offset + s->loopStart];
			ch->n_replen = (uint16_t)(s->loopLength >> 1);

			if (ch->n_length == 0)
				ch->n_length = 1;

			const uint32_t voiceAddr = 0xDFF0A0 + (chNum * 16);
			paulaWriteWord(voiceAddr + 8, ch->n_volume);
			paulaWriteWord(voiceAddr + 6, ch->n_period);
			paulaWritePtr(voiceAddr + 0, ch->n_start);
			paulaWriteWord(voiceAddr + 4, ch->n_length);

			if (!editor.muted[chNum])
				paulaWriteWord(0xDFF096, 0x8000 | ch->n_dmabit); // voice DMA on
			else
				paulaWriteWord(0xDFF096, ch->n_dmabit); // voice DMA off

			// these take effect after the current DMA cycle is done
			paulaWritePtr(voiceAddr + 0, ch->n_loopstart);
			paulaWriteWord(voiceAddr + 4, ch->n_replen);

			// update tracker visuals

			setVisualsVolume(chNum, ch->n_volume);
			setVisualsPeriod(chNum, ch->n_period);
			setVisualsDataPtr(chNum, ch->n_start);
			setVisualsLength(chNum, ch->n_length);

			if (!editor.muted[chNum])
				setVisualsDMACON(0x8000 | ch->n_dmabit);
			else
				setVisualsDMACON(ch->n_dmabit);

			setVisualsDataPtr(chNum, ch->n_loopstart);
			setVisualsLength(chNum, ch->n_replen);

			unlockAudio();
		}

		// normalMode = normal keys, or else keypad keys (in jam mode)
		if (normalMode || editor.pNoteFlag != 0)
		{
			if (normalMode || editor.pNoteFlag == 2)
			{
				// insert note and sample number
				if (!ui.samplerScreenShown && (editor.currMode == MODE_EDIT || editor.currMode == MODE_RECORD))
				{
					note->sample = editor.sampleZero ? 0 : (editor.currSample + 1);
					note->period = cleanPeriod;

					if (editor.autoInsFlag)
					{
						note->command = editor.effectMacros[editor.autoInsSlot] >> 8;
						note->param = editor.effectMacros[editor.autoInsSlot] & 0xFF;
					}

					if (editor.currMode != MODE_RECORD)
						modSetPos(DONT_SET_ORDER, (song->currRow + editor.editMoveAdd) & 63);

					updateWindowTitle(MOD_IS_MODIFIED);
				}
			}

			if (editor.multiFlag)
				gotoNextMulti();
		}

		// PT quirk: spectrum analyzer is still handled here even if channel is muted
		updateSpectrumAnalyzer(s->volume, tempPeriod);
	}
	else if (noteVal == -2)
	{
		// delete note and sample if illegal note (= -2, -1 = ignore) key was entered

		if (normalMode || editor.pNoteFlag == 2)
		{
			if (!ui.samplerScreenShown && (editor.currMode == MODE_EDIT || editor.currMode == MODE_RECORD))
			{
				note->period = 0;
				note->sample = 0;

				if (editor.currMode != MODE_RECORD)
					modSetPos(DONT_SET_ORDER, (song->currRow + editor.editMoveAdd) & 63);

				updateWindowTitle(MOD_IS_MODIFIED);
			}
		}
	}
}

void handleEditKeys(SDL_Scancode scancode, bool normalMode)
{
	if (ui.editTextFlag)
		return;

	if (ui.samplerScreenShown || (editor.currMode == MODE_IDLE || editor.currMode == MODE_PLAY))
	{
		// at this point it will only jam, not place it
		if (!keyb.leftAltPressed && !keyb.leftAmigaPressed && !keyb.leftCtrlPressed && !keyb.shiftPressed)
			jamAndPlaceSample(scancode, normalMode);

		return;
	}

	// handle modified (ALT/CTRL/SHIFT etc) keys for editing
	if (editor.currMode == MODE_EDIT || editor.currMode == MODE_RECORD)
	{
		if (handleSpecialKeys(scancode))
		{
			if (editor.currMode != MODE_RECORD)
				modSetPos(DONT_SET_ORDER, (song->currRow + editor.editMoveAdd) & 63);

			return;
		}
	}

	// are we editing a note, or other stuff?
	if (cursor.mode != CURSOR_NOTE)
	{
		// if we held down any key modifier at this point, then do nothing
		if (keyb.leftAltPressed || keyb.leftAmigaPressed || keyb.leftCtrlPressed || keyb.shiftPressed)
			return;

		if (editor.currMode == MODE_EDIT || editor.currMode == MODE_RECORD)
		{
			int8_t hexKey, numberKey;

			if (scancode == SDL_SCANCODE_0)
				numberKey = 0;
			else if (scancode >= SDL_SCANCODE_1 && scancode <= SDL_SCANCODE_9)
				numberKey = (int8_t)scancode - (SDL_SCANCODE_1-1);
			else
				numberKey = -1;

			if (scancode >= SDL_SCANCODE_A && scancode <= SDL_SCANCODE_F)
				hexKey = 10 + ((int8_t)scancode - SDL_SCANCODE_A);
			else
				hexKey = -1;

			int8_t key = -1;
			if (numberKey != -1)
			{
				if (key == -1)
					key = 0;

				key += numberKey;
			}

			if (hexKey != -1)
			{
				if (key == -1)
					key = 0;

				key += hexKey;
			}

			note_t *note = &song->patterns[song->currPattern][(song->currRow * PAULA_VOICES) + cursor.channel];
			switch (cursor.mode)
			{
				case CURSOR_SAMPLE1:
				{
					if (key != -1 && key < 2)
					{
						note->sample = (uint8_t)((note->sample & 0x0F) | (key << 4));

						if (editor.currMode != MODE_RECORD)
							modSetPos(DONT_SET_ORDER, (song->currRow + editor.editMoveAdd) & 63);

						updateWindowTitle(MOD_IS_MODIFIED);
					}
				}
				break;

				case CURSOR_SAMPLE2:
				{
					if (key != -1 && key < 16)
					{
						note->sample = (uint8_t)((note->sample & 0xF0) | key);

						if (editor.currMode != MODE_RECORD)
							modSetPos(DONT_SET_ORDER, (song->currRow + editor.editMoveAdd) & 63);

						updateWindowTitle(MOD_IS_MODIFIED);
					}
				}
				break;

				case CURSOR_CMD:
				{
					if (key != -1 && key < 16)
					{
						note->command = (uint8_t)key;

						if (editor.currMode != MODE_RECORD)
							modSetPos(DONT_SET_ORDER, (song->currRow + editor.editMoveAdd) & 63);

						updateWindowTitle(MOD_IS_MODIFIED);
					}
				}
				break;

				case CURSOR_PARAM1:
				{
					if (key != -1 && key < 16)
					{
						note->param = (uint8_t)((note->param & 0x0F) | (key << 4));

						if (editor.currMode != MODE_RECORD)
							modSetPos(DONT_SET_ORDER, (song->currRow + editor.editMoveAdd) & 63);

						updateWindowTitle(MOD_IS_MODIFIED);
					}
				}
				break;

				case CURSOR_PARAM2:
				{
					if (key != -1 && key < 16)
					{
						note->param = (uint8_t)((note->param & 0xF0) | key);

						if (editor.currMode != MODE_RECORD)
							modSetPos(DONT_SET_ORDER, (song->currRow + editor.editMoveAdd) & 63);

						updateWindowTitle(MOD_IS_MODIFIED);
					}
				}
				break;

				default: break;
			}
		}
	}
	else
	{
		if (scancode == SDL_SCANCODE_DELETE)
		{
			if (editor.currMode == MODE_EDIT || editor.currMode == MODE_RECORD)
			{
				note_t *note = &song->patterns[song->currPattern][(song->currRow * PAULA_VOICES) + cursor.channel];

				if (!keyb.leftAltPressed)
				{
					note->sample = 0;
					note->period = 0;
				}

				if (keyb.shiftPressed || keyb.leftAltPressed)
				{
					note->command = 0;
					note->param = 0;
				}

				if (editor.currMode != MODE_RECORD)
					modSetPos(DONT_SET_ORDER, (song->currRow + editor.editMoveAdd) & 63);

				updateWindowTitle(MOD_IS_MODIFIED);
			}
		}
		else
		{
			// if we held down any key modifier at this point, then do nothing
			if (keyb.leftAltPressed || keyb.leftAmigaPressed || keyb.leftCtrlPressed || keyb.shiftPressed)
				return;

			jamAndPlaceSample(scancode, normalMode);
		}
	}
}

bool handleSpecialKeys(SDL_Scancode scancode)
{
	if (!keyb.leftAltPressed)
		return false;

	note_t *patt = song->patterns[song->currPattern];
	note_t *note = &patt[(song->currRow * PAULA_VOICES) + cursor.channel];
	note_t *prevNote = &patt[(((song->currRow - 1) & 63) * PAULA_VOICES) + cursor.channel];

	if (scancode >= SDL_SCANCODE_1 && scancode <= SDL_SCANCODE_0)
	{
		// insert stored effect (buffer[0..8])
		note->command = editor.effectMacros[scancode - SDL_SCANCODE_1] >> 8;
		note->param = editor.effectMacros[scancode - SDL_SCANCODE_1] & 0xFF;

		updateWindowTitle(MOD_IS_MODIFIED);
		return true;
	}

	// copy command+effect from above into current command+effect
	if (scancode == SDL_SCANCODE_BACKSLASH)
	{
		note->command = prevNote->command;
		note->param = prevNote->param;

		updateWindowTitle(MOD_IS_MODIFIED);
		return true;
	}

	// copy command+(effect + 1) from above into current command+effect
	if (scancode == SDL_SCANCODE_EQUALS)
	{
		note->command = prevNote->command;
		note->param = prevNote->param + 1; // wraps 0x00..0xFF

		updateWindowTitle(MOD_IS_MODIFIED);
		return true;
	}

	// copy command+(effect - 1) from above into current command+effect
	if (scancode == SDL_SCANCODE_MINUS)
	{
		note->command = prevNote->command;
		note->param = prevNote->param - 1; // wraps 0x00..0xFF

		updateWindowTitle(MOD_IS_MODIFIED);
		return true;
	}

	return false;
}

void handleSampleJamming(SDL_Scancode scancode) // used for the sampling feature (in SAMPLER)
{
	const int32_t chNum = cursor.channel;

	if (scancode == SDL_SCANCODE_NONUSBACKSLASH)
	{
		turnOffVoices(); // magic "kill all voices" button
		return;
	}

	int8_t noteVal = keyToNote(scancode);
	if (noteVal < 0 || noteVal > 35)
		return;

	moduleSample_t *s = &song->samples[editor.currSample];
	if (s->length <= 1)
		return;

	moduleChannel_t *ch = &song->channels[chNum];

	int8_t *n_start = &song->sampleData[s->offset];
	int8_t vol = 64;
	uint16_t n_length = (uint16_t)(s->length >> 1);
	uint16_t period = periodTable[((s->fineTune & 0xF) * 37) + noteVal];

	lockAudio();

	ch->n_samplenum = editor.currSample; // needed for sample playback/sampling line

	const uint32_t voiceAddr = 0xDFF0A0 + (chNum * 16);
	paulaWriteWord(voiceAddr +  8, vol);
	paulaWriteWord(voiceAddr + 6, period);
	paulaWritePtr(voiceAddr + 0, n_start);
	paulaWriteWord(voiceAddr + 4, n_length);

	if (!editor.muted[chNum])
		paulaWriteWord(0xDFF096, 0x8000 | ch->n_dmabit); // voice DMA on
	else
		paulaWriteWord(0xDFF096, ch->n_dmabit); // voice DMA off

	// these take effect after the current DMA cycle is done
	paulaWritePtr(voiceAddr + 0, NULL); // data
	paulaWriteWord(voiceAddr + 4, 1); // length

	// update tracker visuals

	setVisualsVolume(chNum, vol);
	setVisualsPeriod(chNum, period);
	setVisualsDataPtr(chNum, n_start);
	setVisualsLength(chNum, n_length);

	if (!editor.muted[chNum])
		setVisualsDMACON(0x8000 | ch->n_dmabit);
	else
		setVisualsDMACON(ch->n_dmabit);

	setVisualsDataPtr(chNum, NULL);
	setVisualsLength(chNum, 1);

	unlockAudio();
}

void saveUndo(void)
{
	memcpy(editor.undoBuffer, song->patterns[song->currPattern], sizeof (note_t) * (PAULA_VOICES * MOD_ROWS));
}

void undoLastChange(void)
{
	for (uint16_t i = 0; i < MOD_ROWS * PAULA_VOICES; i++)
	{
		note_t data = editor.undoBuffer[i];
		editor.undoBuffer[i] = song->patterns[song->currPattern][i];
		song->patterns[song->currPattern][i] = data;
	}

	updateWindowTitle(MOD_IS_MODIFIED);
	ui.updatePatternData = true;
}

void copySampleTrack(void)
{
	if (editor.trackPattFlag == 2)
	{
		// copy from one sample slot to another

		// never attempt to swap if from and/or to is 0
		if (editor.sampleFrom == 0 || editor.sampleTo == 0)
		{
			displayErrorMsg("FROM/TO = 0 !");
			return;
		}

		moduleSample_t *smpTo = &song->samples[editor.sampleTo - 1];
		moduleSample_t *smpFrom = &song->samples[editor.sampleFrom - 1];

		turnOffVoices();

		// copy
		uint32_t tmpOffset = smpTo->offset;
		*smpTo = *smpFrom;
		smpTo->offset = tmpOffset;

		// update the copied sample's GUI text pointers
		smpTo->volumeDisp = &smpTo->volume;
		smpTo->lengthDisp = &smpTo->length;
		smpTo->loopStartDisp = &smpTo->loopStart;
		smpTo->loopLengthDisp = &smpTo->loopLength;

		// copy sample data
		memcpy(&song->sampleData[smpTo->offset], &song->sampleData[smpFrom->offset], config.maxSampleLength);

		updateCurrSample();
		ui.updateSongSize = true;
	}
	else
	{
		// copy sample number in track/pattern
		if (editor.trackPattFlag == 0)
		{
			for (int32_t i = 0; i < MOD_ROWS; i++)
			{
				note_t *noteSrc = &song->patterns[song->currPattern][(i * PAULA_VOICES) + cursor.channel];
				if (noteSrc->sample == editor.sampleFrom)
					noteSrc->sample = editor.sampleTo;
			}
		}
		else
		{
			for (int32_t i = 0; i < PAULA_VOICES; i++)
			{
				for (int32_t j = 0; j < MOD_ROWS; j++)
				{
					note_t *noteSrc = &song->patterns[song->currPattern][(j * PAULA_VOICES) + i];
					if (noteSrc->sample == editor.sampleFrom)
						noteSrc->sample = editor.sampleTo;
				}
			}
		}

		ui.updatePatternData = true;
	}

	editor.samplePos = 0;
	updateSamplePos();

	updateWindowTitle(MOD_IS_MODIFIED);
}

void exchSampleTrack(void)
{
	if (editor.trackPattFlag == 2)
	{
		// exchange sample slots

		// never attempt to swap if from and/or to is 0
		if (editor.sampleFrom == 0 || editor.sampleTo == 0)
		{
			displayErrorMsg("FROM/TO = 0 !");
			return;
		}

		moduleSample_t *smpTo = &song->samples[editor.sampleTo-1];
		moduleSample_t *smpFrom = &song->samples[editor.sampleFrom-1];

		turnOffVoices();

		// swap offsets first so that the next swap will leave offsets intact
		uint32_t tmpOffset = smpFrom->offset;
		smpFrom->offset = smpTo->offset;
		smpTo->offset = tmpOffset;

		// swap sample (now offsets are left as before)
		moduleSample_t smpTmp = *smpFrom;
		*smpFrom = *smpTo;
		*smpTo = smpTmp;

		// update the swapped sample's GUI text pointers
		smpFrom->volumeDisp = &smpFrom->volume;
		smpFrom->lengthDisp = &smpFrom->length;
		smpFrom->loopStartDisp = &smpFrom->loopStart;
		smpFrom->loopLengthDisp = &smpFrom->loopLength;
		smpTo->volumeDisp = &smpTo->volume;
		smpTo->lengthDisp = &smpTo->length;
		smpTo->loopStartDisp = &smpTo->loopStart;
		smpTo->loopLengthDisp = &smpTo->loopLength;

		// swap sample data
		for (int32_t i = 0; i < config.maxSampleLength; i++)
		{
			int8_t smp = song->sampleData[smpFrom->offset+i];
			song->sampleData[smpFrom->offset+i] = song->sampleData[smpTo->offset+i];
			song->sampleData[smpTo->offset+i] = smp;
		}

		editor.sampleZero = false;

		updateCurrSample();
	}
	else
	{
		// exchange sample number in track/pattern
		if (editor.trackPattFlag == 0)
		{
			for (int32_t i = 0; i < MOD_ROWS; i++)
			{
				note_t *noteSrc = &song->patterns[song->currPattern][(i * PAULA_VOICES) + cursor.channel];

				     if (noteSrc->sample == editor.sampleFrom) noteSrc->sample = editor.sampleTo;
				else if (noteSrc->sample == editor.sampleTo) noteSrc->sample = editor.sampleFrom;
			}
		}
		else
		{
			for (int32_t i = 0; i < PAULA_VOICES; i++)
			{
				for (int32_t j = 0; j < MOD_ROWS; j++)
				{
					note_t *noteSrc = &song->patterns[song->currPattern][(j * PAULA_VOICES) + i];

					     if (noteSrc->sample == editor.sampleFrom) noteSrc->sample = editor.sampleTo;
					else if (noteSrc->sample == editor.sampleTo) noteSrc->sample = editor.sampleFrom;
				}
			}
		}

		ui.updatePatternData = true;
	}

	editor.samplePos = 0;
	updateSamplePos();

	updateWindowTitle(MOD_IS_MODIFIED);
}

void delSampleTrack(void)
{
	saveUndo();

	if (editor.trackPattFlag == 0)
	{
		for (int32_t i = 0; i < MOD_ROWS; i++)
		{
			note_t *noteSrc = &song->patterns[song->currPattern][(i * PAULA_VOICES) + cursor.channel];
			if (noteSrc->sample == editor.currSample+1)
			{
				noteSrc->period = 0;
				noteSrc->sample = 0;
				noteSrc->command = 0;
				noteSrc->param = 0;
			}
		}
	}
	else
	{
		for (int32_t i = 0; i < PAULA_VOICES; i++)
		{
			for (int32_t j = 0; j < MOD_ROWS; j++)
			{
				note_t *noteSrc = &song->patterns[song->currPattern][(j * PAULA_VOICES) + i];
				if (noteSrc->sample == editor.currSample+1)
				{
					noteSrc->period = 0;
					noteSrc->sample = 0;
					noteSrc->command = 0;
					noteSrc->param = 0;
				}
			}
		}
	}

	updateWindowTitle(MOD_IS_MODIFIED);
	ui.updatePatternData = true;
}

void trackNoteUp(bool sampleAllFlag, uint8_t from, uint8_t to)
{
	if (from > to)
	{
		uint8_t old = from;
		from = to;
		to = old;
	}

	saveUndo();
	for (int32_t i = from; i <= to; i++)
	{
		note_t *noteSrc = &song->patterns[song->currPattern][(i * PAULA_VOICES) + cursor.channel];

		if (!sampleAllFlag && noteSrc->sample != editor.currSample+1)
			continue;

		if (noteSrc->period)
		{
			// period -> note
			int32_t j;
			for (j = 0; j < 36; j++)
			{
				if (noteSrc->period >= periodTable[j])
					break;
			}

			bool noteDeleted = false;
			if (++j > 35)
			{
				j = 35;

				if (config.transDel)
				{
					noteSrc->period = 0;
					noteSrc->sample = 0;

					noteDeleted = true;
				}
			}

			if (!noteDeleted)
				noteSrc->period = periodTable[j];
		}
	}

	updateWindowTitle(MOD_IS_MODIFIED);
	ui.updatePatternData = true;
}

void trackNoteDown(bool sampleAllFlag, uint8_t from, uint8_t to)
{
	if (from > to)
	{
		uint8_t old = from;
		from = to;
		to = old;
	}

	saveUndo();
	for (int32_t i = from; i <= to; i++)
	{
		note_t *noteSrc = &song->patterns[song->currPattern][(i * PAULA_VOICES) + cursor.channel];

		if (!sampleAllFlag && noteSrc->sample != editor.currSample+1)
			continue;

		if (noteSrc->period)
		{
			// period -> note
			int32_t j;
			for (j = 0; j < 36; j++)
			{
				if (noteSrc->period >= periodTable[j])
					break;
			}

			bool noteDeleted = false;
			if (--j < 0)
			{
				j = 0;

				if (config.transDel)
				{
					noteSrc->period = 0;
					noteSrc->sample = 0;

					noteDeleted = true;
				}
			}

			if (!noteDeleted)
				noteSrc->period = periodTable[j];
		}
	}

	updateWindowTitle(MOD_IS_MODIFIED);
	ui.updatePatternData = true;
}

void trackOctaUp(bool sampleAllFlag, uint8_t from, uint8_t to)
{
	if (from > to)
	{
		uint8_t old = from;
		from = to;
		to = old;
	}

	bool noteChanged = false;

	saveUndo();
	for (int32_t i = from; i <= to; i++)
	{
		note_t *noteSrc = &song->patterns[song->currPattern][(i * PAULA_VOICES) + cursor.channel];

		if (!sampleAllFlag && noteSrc->sample != editor.currSample+1)
			continue;

		if (noteSrc->period)
		{
			uint16_t oldPeriod = noteSrc->period;

			// period -> note
			int32_t j;
			for (j = 0; j < 36; j++)
			{
				if (noteSrc->period >= periodTable[j])
					break;
			}

			bool noteDeleted = false;
			if (j+12 > 35 && config.transDel)
			{
				noteSrc->period = 0;
				noteSrc->sample = 0;

				noteDeleted = true;
			}

			if (j <= 23)
				j += 12;

			if (!noteDeleted)
				noteSrc->period = periodTable[j];

			if (noteSrc->period != oldPeriod)
				noteChanged = true;
		}
	}

	if (noteChanged)
	{
		updateWindowTitle(MOD_IS_MODIFIED);
		ui.updatePatternData = true;
	}
}

void trackOctaDown(bool sampleAllFlag, uint8_t from, uint8_t to)
{
	if (from > to)
	{
		uint8_t old = from;
		from = to;
		to = old;
	}

	saveUndo();
	for (int32_t i = from; i <= to; i++)
	{
		note_t *noteSrc = &song->patterns[song->currPattern][(i * PAULA_VOICES) + cursor.channel];

		if (!sampleAllFlag && noteSrc->sample != editor.currSample+1)
			continue;

		if (noteSrc->period)
		{
			// period -> note
			int32_t j;
			for (j = 0; j < 36; j++)
			{
				if (noteSrc->period >= periodTable[j])
					break;
			}

			bool noteDeleted = false;
			if (j-12 < 0 && config.transDel)
			{
				noteSrc->period = 0;
				noteSrc->sample = 0;

				noteDeleted = true;
			}

			if (j >= 12)
				j -= 12;

			if (!noteDeleted)
				noteSrc->period = periodTable[j];
		}
	}

	updateWindowTitle(MOD_IS_MODIFIED);
	ui.updatePatternData = true;
}

void pattNoteUp(bool sampleAllFlag)
{
	saveUndo();
	for (int32_t i = 0; i < PAULA_VOICES; i++)
	{
		for (int32_t j = 0; j < MOD_ROWS; j++)
		{
			note_t *noteSrc = &song->patterns[song->currPattern][(j * PAULA_VOICES) + i];

			if (!sampleAllFlag && noteSrc->sample != editor.currSample+1)
				continue;

			if (noteSrc->period)
			{
				// period -> note
				int32_t k;
				for (k = 0; k < 36; k++)
				{
					if (noteSrc->period >= periodTable[k])
						break;
				}

				bool noteDeleted = false;
				if (++k > 35)
				{
					k = 35;

					if (config.transDel)
					{
						noteSrc->period = 0;
						noteSrc->sample = 0;

						noteDeleted = true;
					}
				}

				if (!noteDeleted)
					noteSrc->period = periodTable[k];
			}
		}
	}

	updateWindowTitle(MOD_IS_MODIFIED);
	ui.updatePatternData = true;
}

void pattNoteDown(bool sampleAllFlag)
{
	saveUndo();
	for (int32_t i = 0; i < PAULA_VOICES; i++)
	{
		for (int32_t j = 0; j < MOD_ROWS; j++)
		{
			note_t *noteSrc = &song->patterns[song->currPattern][(j * PAULA_VOICES) + i];

			if (!sampleAllFlag && noteSrc->sample != editor.currSample+1)
				continue;

			if (noteSrc->period)
			{
				// period -> note
				int32_t k;
				for (k = 0; k < 36; k++)
				{
					if (noteSrc->period >= periodTable[k])
						break;
				}

				bool noteDeleted = false;
				if (--k < 0)
				{
					k = 0;

					if (config.transDel)
					{
						noteSrc->period = 0;
						noteSrc->sample = 0;

						noteDeleted = true;
					}
				}

				if (!noteDeleted)
					noteSrc->period = periodTable[k];
			}
		}
	}

	updateWindowTitle(MOD_IS_MODIFIED);
	ui.updatePatternData = true;
}

void pattOctaUp(bool sampleAllFlag)
{
	saveUndo();
	for (int32_t i = 0; i < PAULA_VOICES; i++)
	{
		for (int32_t j = 0; j < MOD_ROWS; j++)
		{
			note_t *noteSrc = &song->patterns[song->currPattern][(j * PAULA_VOICES) + i];

			if (!sampleAllFlag && noteSrc->sample != editor.currSample+1)
				continue;

			if (noteSrc->period)
			{
				// period -> note
				int32_t k;
				for (k = 0; k < 36; k++)
				{
					if (noteSrc->period >= periodTable[k])
						break;
				}

				bool noteDeleted = false;
				if (k+12 > 35 && config.transDel)
				{
					noteSrc->period = 0;
					noteSrc->sample = 0;

					noteDeleted = true;
				}

				if (k <= 23)
					k += 12;

				if (!noteDeleted)
					noteSrc->period = periodTable[k];
			}
		}
	}

	updateWindowTitle(MOD_IS_MODIFIED);
	ui.updatePatternData = true;
}

void pattOctaDown(bool sampleAllFlag)
{
	saveUndo();
	for (int32_t i = 0; i < PAULA_VOICES; i++)
	{
		for (int32_t j = 0; j < MOD_ROWS; j++)
		{
			note_t *noteSrc = &song->patterns[song->currPattern][(j * PAULA_VOICES) + i];

			if (!sampleAllFlag && noteSrc->sample != editor.currSample+1)
				continue;

			if (noteSrc->period)
			{
				// period -> note
				int32_t k;
				for (k = 0; k < 36; k++)
				{
					if (noteSrc->period >= periodTable[k])
						break;
				}

				bool noteDeleted = false;
				if (k-12 < 0 && config.transDel)
				{
					noteSrc->period = 0;
					noteSrc->sample = 0;

					noteDeleted = true;
				}

				if (k >= 12)
					k -= 12;

				if (!noteDeleted)
					noteSrc->period = periodTable[k];
			}
		}
	}

	updateWindowTitle(MOD_IS_MODIFIED);
	ui.updatePatternData = true;
}

int8_t keyToNote(SDL_Scancode scancode)
{
	int8_t note;

	if (scancode < SDL_SCANCODE_B || scancode > SDL_SCANCODE_SLASH)
		return -1; // not a note key

	int32_t lookUpKey = (int32_t)scancode - SDL_SCANCODE_B;
	if (lookUpKey < 0 || lookUpKey >= 52)
		return -1; // just in case

	if (editor.keyOctave == OCTAVE_LOW)
		note = scancode2NoteLo[lookUpKey];
	else
		note = scancode2NoteHi[lookUpKey];

	return note;
}
