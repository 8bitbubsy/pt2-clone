// for finding memory leaks in debug mode with Visual Studio 
#if defined _DEBUG && defined _MSC_VER
#include <crtdbg.h>
#endif

#include <stdint.h>
#include <stdbool.h>
#include <ctype.h>
#include "pt2_textout.h"
#include "pt2_visuals.h"
#include "pt2_helpers.h"
#include "pt2_replayer.h"
#include "pt2_bmp.h"
#include "pt2_mouse.h"
#include "pt2_edit.h"
#include "pt2_config.h"
#include "pt2_diskop.h"
#include "pt2_sampler.h"
#include "pt2_audio.h"
#include "pt2_chordmaker.h"
#include "pt2_textedit.h"

// PATH_MAX is the absolute longest editable string possible
static char oldText[PATH_MAX+2];
static uint32_t oldTextLength;

void renderTextEditCursor(void)
{
	if (!ui.editTextFlag)
		return;

	const int32_t x = textEdit.cursorStartX + (textEdit.cursorBlock * FONT_CHAR_W);
	const int32_t y = textEdit.cursorStartY;
	const int32_t w = 7;
	const int32_t h = 2;

	if (x < 0 || x+w >= SCREEN_W || y < 0 || y+h >= SCREEN_H)
		return;

	fillRect(x, y, w, h, video.palette[PAL_TEXTMARK]);
}

void removeTextEditCursor(void)
{
	if (!ui.editTextFlag)
		return;

	const int32_t x = textEdit.cursorStartX + (textEdit.cursorBlock * FONT_CHAR_W);
	const int32_t y = textEdit.cursorStartY;
	const int32_t w = 7;
	const int32_t h = 2;

	if (x < 0 || x+w >= SCREEN_W || y < 0 || y+h >= SCREEN_H)
		return;

	if (textEdit.object == PTB_PE_PATT || textEdit.object == PTB_PE_PATTNAME)
	{
		// position editor text editing needs different handling

		// rewrite border pixels (below second row of text edit cursor)
		hLine(x, y, w, video.palette[PAL_GENBKG2]);

		ui.updatePosEd = true; // this also erases the first row of the text edit cursor
	}
	else
	{
		// all others
		fillRect(x, y, w, h, video.palette[PAL_GENBKG]);
	}
}

static void moveTextCursorLeft(void)
{
	if (textEdit.cursorBlock > 0)
	{
		removeTextEditCursor();
		textEdit.cursorBlock--;
		renderTextEditCursor();
	}
	else if (textEdit.scrollable)
	{
		if (textEdit.scrollOffset > 0)
		{
			textEdit.scrollOffset--;

			if (textEdit.object == PTB_DO_DATAPATH)
				ui.updateDiskOpPathText = true;
			else if (textEdit.object == PTB_PE_PATTNAME)
				ui.updatePosEd = true;
		}
	}
}

static void moveTextCursorRight(void)
{
	if (textEdit.type == TEXT_EDIT_STRING)
	{
		if (textEdit.cursorBlock < textEdit.numBlocks-1)
		{
			removeTextEditCursor();
			textEdit.cursorBlock++;
			renderTextEditCursor();
		}
		else if (textEdit.scrollable)
		{
			if (textEdit.textPtr <= textEdit.textEndPtr)
			{
				textEdit.scrollOffset++;

				if (textEdit.object == PTB_DO_DATAPATH)
					ui.updateDiskOpPathText = true;
				else if (textEdit.object == PTB_PE_PATTNAME)
					ui.updatePosEd = true;
			}
		}
	}
	else
	{
		// we end up here when entering a number/hex digit

		if (textEdit.cursorBlock < textEdit.numDigits)
			removeTextEditCursor();

		textEdit.cursorBlock++;

		if (textEdit.cursorBlock < textEdit.numDigits)
			renderTextEditCursor();

		// don't clamp now, textEdit.cursorBlock is tested elsewhere
	}
}

void editTextPrevChar(void)
{
	if (textEdit.type != TEXT_EDIT_STRING)
	{
		if (textEdit.cursorBlock > 0)
		{
			removeTextEditCursor();
			textEdit.cursorBlock--;
			renderTextEditCursor();
		}

		return;
	}

	if (editor.mixFlag && textEdit.cursorBlock <= 4) // kludge...
		return;

	if (textEdit.textPtr > textEdit.textStartPtr)
	{
		removeTextEditCursor();

		textEdit.textPtr--;
		moveTextCursorLeft();

		if (editor.mixFlag) // special case for "Mix" input field in Edit. Op.
		{
			if (textEdit.cursorBlock == 12)
			{
				textEdit.textPtr--; moveTextCursorLeft();
				textEdit.textPtr--; moveTextCursorLeft();
				textEdit.textPtr--; moveTextCursorLeft();
				textEdit.textPtr--; moveTextCursorLeft();
			}
			else if (textEdit.cursorBlock == 6)
			{
				textEdit.textPtr--;
				moveTextCursorLeft();
			}
		}

		renderTextEditCursor();
	}

	textEdit.endReached = false;
}

void editTextNextChar(void)
{
	if (textEdit.type != TEXT_EDIT_STRING)
	{
		if (textEdit.cursorBlock < textEdit.numDigits-1)
		{
			removeTextEditCursor();
			textEdit.cursorBlock++;
			renderTextEditCursor();
		}

		return;
	}

	if (editor.mixFlag && textEdit.cursorBlock >= 14) // kludge
		return;

	if (textEdit.textPtr < textEdit.textEndPtr)
	{
		if (*textEdit.textPtr != '\0')
		{
			removeTextEditCursor();

			textEdit.textPtr++;
			moveTextCursorRight();

			if (editor.mixFlag) // special case for "Mix" input field in Edit. Op.
			{
				if (textEdit.cursorBlock == 9)
				{
					textEdit.textPtr++; moveTextCursorRight();
					textEdit.textPtr++; moveTextCursorRight();
					textEdit.textPtr++; moveTextCursorRight();
					textEdit.textPtr++; moveTextCursorRight();
				}
				else if (textEdit.cursorBlock == 6)
				{
					textEdit.textPtr++;
					moveTextCursorRight();
				}
			}

			renderTextEditCursor();
		}
		else
		{
			textEdit.endReached = true;
		}
	}
	else
	{
		textEdit.endReached = true;
	}
}

void handleTextEditing(uint8_t mouseButton) // handle mouse while editing text/numbers
{
	if (!ui.editTextFlag)
		return;

	if (textEdit.type != TEXT_EDIT_STRING)
	{
		if (mouseButton == SDL_BUTTON_RIGHT)
			leaveTextEditMode(EDIT_TEXT_NO_UPDATE);
	}
	else if (mouseButton == SDL_BUTTON_LEFT && !editor.mixFlag) // string type
	{
		// jump to edit character under the mouse pointer

		int32_t tmp32 = mouse.y - textEdit.cursorStartY;
		if (tmp32 <= 2 && tmp32 >= -9)
		{
			assert(textEdit.textStartPtr != NULL);
			const int32_t textLength = (int32_t)strlen(textEdit.textStartPtr);
			int32_t cursorBlock = (mouse.x - textEdit.cursorStartX) / FONT_CHAR_W;
			const int32_t textPos = CLAMP(textEdit.scrollOffset + cursorBlock, 0, textLength);

			cursorBlock = CLAMP(cursorBlock, 0, textEdit.numBlocks-1);
			if (cursorBlock > textLength)
				cursorBlock = textLength;

			removeTextEditCursor();
			textEdit.cursorBlock = (uint16_t)cursorBlock;
			textEdit.textPtr = textEdit.textStartPtr + textPos;
			renderTextEditCursor();
		}
		else
		{
			// if we clicked outside of the vertical edit area, stop edit mode

			if (textEdit.object != PTB_PE_PATTNAME) // special case...
				leaveTextEditMode(EDIT_TEXT_UPDATE);
		}
	}
	else if (mouseButton == SDL_BUTTON_RIGHT)
	{
		if (editor.mixFlag)
		{
			leaveTextEditMode(EDIT_TEXT_UPDATE);
			editor.mixFlag = false;
			ui.updateMixText = true;
		}
		else
		{
			// delete text
			const uint32_t charsToZero = (uint32_t)(textEdit.textEndPtr - textEdit.textStartPtr);
			memset(textEdit.textStartPtr, '\0', charsToZero);

			if (textEdit.object == PTB_DO_DATAPATH)
			{
				/* Don't exit text edit mode if the Disk Op. path was about to
				** be deleted with the right mouse button.
				** Set text cursor to beginning instead.
				*/
				removeTextEditCursor();
				textEdit.cursorBlock = 0;
				textEdit.textPtr = textEdit.textStartPtr;
				textEdit.scrollOffset = 0;
				renderTextEditCursor();

				ui.updateDiskOpPathText = true;
			}
			else
			{
				if (textEdit.object == PTB_SONGNAME)
					ui.updateSongName = true;
				else if (textEdit.object == PTB_SAMPLENAME)
					ui.updateCurrSampleName = true;

				leaveTextEditMode(EDIT_TEXT_UPDATE);
			}
		}
	}
}

void enterTextEditMode(int16_t editObject)
{
	pointerSetMode(POINTER_MODE_MSG1, NO_CARRY);

	textEdit.textPtr = textEdit.textStartPtr;
	textEdit.cursorBlock = 0;
	textEdit.scrollOffset = 0;
	textEdit.type = TEXT_EDIT_STRING;
	textEdit.object = editObject;
	textEdit.endReached = false;
	ui.editTextFlag = true;

	// make backup of old text (for handling 'song modified' flag later)
	oldTextLength = (uint32_t)strlen(textEdit.textPtr);
	memset(oldText, '\0', sizeof (oldText));
	memcpy(oldText, textEdit.textPtr, oldTextLength);
	
	if (editor.mixFlag) // skip to editable text section
	{
		editTextNextChar();
		editTextNextChar();
		editTextNextChar();
		editTextNextChar();
	}

	renderTextEditCursor();
	SDL_StartTextInput();
}

void enterNumberEditMode(uint8_t type, int16_t editObject)
{
	pointerSetMode(POINTER_MODE_MSG1, NO_CARRY);

	textEdit.cursorBlock = 0;
	textEdit.type = type;
	textEdit.object = editObject;
	textEdit.endReached = false;
	ui.editTextFlag = true;

	renderTextEditCursor();
	SDL_StartTextInput();
}

static void redrawTextEditObject(void)
{
	switch (textEdit.object)
	{
		default: break;
		case PTB_SONGNAME: ui.updateSongName = true; break;
		case PTB_SAMPLENAME: ui.updateCurrSampleName = true; break;
		case PTB_PE_PATT: ui.updatePosEd = true; break;
		case PTB_PE_PATTNAME: ui.updatePosEd = true; break;
		case PTB_EO_QUANTIZE: ui.updateQuantizeText = true; break;
		case PTB_EO_METRO_1: ui.updateMetro1Text = true; break;
		case PTB_EO_METRO_2: ui.updateMetro2Text = true; break;
		case PTB_EO_FROM_NUM: ui.updateFromText = true; break;
		case PTB_EO_TO_NUM: ui.updateToText = true; break;
		case PTB_EO_MIX: ui.updateMixText = true; break;
		case PTB_EO_POS_NUM: ui.updatePosText = true; break;
		case PTB_EO_MOD_NUM: ui.updateModText = true; break;
		case PTB_EO_VOL_NUM: ui.updateVolText = true; break;
		case PTB_DO_DATAPATH: ui.updateDiskOpPathText = true; break;
		case PTB_POSS: ui.updateSongPos = true; break;
		case PTB_PATTERNS: ui.updateSongPattern = true; break;
		case PTB_LENGTHS: ui.updateSongLength = true; break;
		case PTB_SAMPLES: ui.updateCurrSampleNum = true; break;
		case PTB_SVOLUMES: ui.updateCurrSampleVolume = true; break;
		case PTB_SLENGTHS: ui.updateCurrSampleLength = true; break;
		case PTB_SREPEATS: ui.updateCurrSampleRepeat = true; break;
		case PTB_SREPLENS: ui.updateCurrSampleReplen = true; break;
		case PTB_PATTDATA: ui.updateCurrPattText = true; break;
		case PTB_SA_VOL_FROM_NUM: ui.updateVolFromText = true; break;
		case PTB_SA_VOL_TO_NUM: ui.updateVolToText = true; break;
		case PTB_SA_FIL_LP_CUTOFF: ui.updateLPText = true; break;
		case PTB_SA_FIL_HP_CUTOFF: ui.updateHPText = true; break;
	}
}

void leaveTextEditMode(bool updateValue)
{
	int8_t tmp8;
	int16_t tmp16;
	int32_t tmp32;

	SDL_StopTextInput();

	// if user updated the disk op path text
	if (ui.diskOpScreenShown && textEdit.object == PTB_DO_DATAPATH)
	{
		UNICHAR *pathU = (UNICHAR *)calloc(PATH_MAX + 2, sizeof (UNICHAR));
		if (pathU != NULL)
		{
#ifdef _WIN32
			MultiByteToWideChar(CP_ACP, MB_PRECOMPOSED, editor.currPath, -1, pathU, PATH_MAX);
#else
			strcpy(pathU, editor.currPath);
#endif
			diskOpSetPath(pathU, DISKOP_CACHE);
			free(pathU);
		}
	}

	if (textEdit.type != TEXT_EDIT_STRING)
	{
		if (textEdit.cursorBlock != textEdit.numDigits)
			removeTextEditCursor();

		redrawTextEditObject();
	}
	else
	{
		removeTextEditCursor();

		// yet another kludge...
		if (textEdit.object == PTB_PE_PATT || textEdit.object == PTB_PE_PATTNAME)
			ui.updatePosEd = true;
	}

	ui.editTextFlag = false;

	if (textEdit.type == TEXT_EDIT_STRING)
	{
		pointerSetPreviousMode();

		// handle song modified state (only for some text edit objects)
		if (textEdit.object != PTB_EO_MIX && textEdit.object != PTB_PE_PATTNAME && textEdit.object != PTB_DO_DATAPATH)
		{
			if (strcmp(textEdit.textStartPtr, oldText) != 0)
				updateWindowTitle(MOD_IS_MODIFIED);
		}
	}
	else
	{
		// set back GUI text pointers and update values (if requested)

		moduleSample_t *s = &song->samples[editor.currSample];
		switch (textEdit.object)
		{
			case PTB_SA_FIL_LP_CUTOFF:
			{
				editor.lpCutOffDisp = &editor.lpCutOff;

				if (updateValue)
				{
					editor.lpCutOff = textEdit.tmpDisp16;
					if (editor.lpCutOff > (uint16_t)(FILTERS_BASE_FREQ/2))
						editor.lpCutOff = (uint16_t)(FILTERS_BASE_FREQ/2);

					ui.updateLPText = true;
				}
			}
			break;

			case PTB_SA_FIL_HP_CUTOFF:
			{
				editor.hpCutOffDisp = &editor.hpCutOff;

				if (updateValue)
				{
					editor.hpCutOff = textEdit.tmpDisp16;
					if (editor.hpCutOff > (uint16_t)(FILTERS_BASE_FREQ/2))
						editor.hpCutOff = (uint16_t)(FILTERS_BASE_FREQ/2);

					ui.updateHPText = true;
				}
			}
			break;

			case PTB_SA_VOL_FROM_NUM:
			{
				editor.vol1Disp = &editor.vol1;

				if (updateValue)
				{
					editor.vol1 = textEdit.tmpDisp16;
					if (editor.vol1 > 200)
						editor.vol1 = 200;

					ui.updateVolFromText = true;
					showVolFromSlider();
				}
			}
			break;

			case PTB_SA_VOL_TO_NUM:
			{
				editor.vol2Disp = &editor.vol2;

				if (updateValue)
				{
					editor.vol2 = textEdit.tmpDisp16;
					if (editor.vol2 > 200)
						editor.vol2 = 200;

					ui.updateVolToText = true;
					showVolToSlider();
				}
			}
			break;

			case PTB_EO_VOL_NUM:
			{
				editor.sampleVolDisp = &editor.sampleVol;

				if (updateValue)
				{
					editor.sampleVol = textEdit.tmpDisp16;
					ui.updateVolText = true;
				}
			}
			break;

			case PTB_EO_POS_NUM:
			{
				editor.samplePosDisp = &editor.samplePos;

				if (updateValue)
				{
					editor.samplePos = textEdit.tmpDisp32;
					if (editor.samplePos > config.maxSampleLength)
						editor.samplePos = config.maxSampleLength;

					if (editor.samplePos > song->samples[editor.currSample].length)
						editor.samplePos = song->samples[editor.currSample].length;

					ui.updatePosText = true;
				}
			}
			break;

			case PTB_EO_QUANTIZE:
			{
				editor.quantizeValueDisp = &config.quantizeValue;

				if (updateValue)
				{
					if (textEdit.tmpDisp16 > 63)
						textEdit.tmpDisp16 = 63;

					config.quantizeValue = textEdit.tmpDisp16;
					ui.updateQuantizeText = true;
				}
			}
			break;

			case PTB_EO_METRO_1: // metronome speed
			{
				editor.metroSpeedDisp = &editor.metroSpeed;

				if (updateValue)
				{
					if (textEdit.tmpDisp16 > 64)
						textEdit.tmpDisp16 = 64;

					editor.metroSpeed = textEdit.tmpDisp16;
					ui.updateMetro1Text = true;
				}
			}
			break;

			case PTB_EO_METRO_2: // metronome channel
			{
				editor.metroChannelDisp = &editor.metroChannel;

				if (updateValue)
				{
					if (textEdit.tmpDisp16 > 4)
						textEdit.tmpDisp16 = 4;

					editor.metroChannel = textEdit.tmpDisp16;
					ui.updateMetro2Text = true;
				}
			}
			break;

			case PTB_EO_FROM_NUM:
			{
				editor.sampleFromDisp = &editor.sampleFrom;

				if (updateValue)
				{
					editor.sampleFrom = textEdit.tmpDisp8;

					// signed check + normal check
					if (editor.sampleFrom < 0x00 || editor.sampleFrom > 0x1F)
						editor.sampleFrom = 0x1F;

					ui.updateFromText = true;
				}
			}
			break;

			case PTB_EO_TO_NUM:
			{
				editor.sampleToDisp = &editor.sampleTo;

				if (updateValue)
				{
					editor.sampleTo = textEdit.tmpDisp8;

					// signed check + normal check
					if (editor.sampleTo < 0x00 || editor.sampleTo > 0x1F)
						editor.sampleTo = 0x1F;

					ui.updateToText = true;
				}
			}
			break;

			case PTB_PE_PATT:
			{
				int16_t posEdPos = song->currPos;
				if (posEdPos > song->header.songLength-1)
					posEdPos = song->header.songLength-1;

				editor.currPosEdPattDisp = &song->header.patternTable[posEdPos];

				if (updateValue)
				{
					if (textEdit.tmpDisp16 > MAX_PATTERNS-1)
						textEdit.tmpDisp16 = MAX_PATTERNS-1;

					song->header.patternTable[posEdPos] = textEdit.tmpDisp16;

					updateWindowTitle(MOD_IS_MODIFIED);

					if (ui.posEdScreenShown)
						ui.updatePosEd = true;

					ui.updateSongPattern = true;
					ui.updateSongSize = true;
				}
			}
			break;

			case PTB_POSS:
			{
				editor.currPosDisp = &song->currPos;

				if (updateValue)
				{
					tmp16 = textEdit.tmpDisp16;
					if (tmp16 > 126)
						tmp16 = 126;

					if (song->currPos != tmp16)
					{
						song->currPos = tmp16;
						editor.currPatternDisp = &song->header.patternTable[song->currPos];

						if (ui.posEdScreenShown)
							ui.updatePosEd = true;

						ui.updateSongPos = true;
						ui.updatePatternData = true;
					}
				}
			}
			break;

			case PTB_PATTERNS:
			{
				editor.currPatternDisp = &song->header.patternTable[song->currPos];

				if (updateValue)
				{
					tmp16 = textEdit.tmpDisp16;
					if (tmp16 > MAX_PATTERNS-1)
						tmp16 = MAX_PATTERNS-1;

					if (song->header.patternTable[song->currPos] != tmp16)
					{
						song->header.patternTable[song->currPos] = tmp16;

						updateWindowTitle(MOD_IS_MODIFIED);

						if (ui.posEdScreenShown)
							ui.updatePosEd = true;

						ui.updateSongPattern = true;
						ui.updateSongSize = true;
					}
				}
			}
			break;

			case PTB_LENGTHS:
			{
				editor.currLengthDisp = &song->header.songLength;

				if (updateValue)
				{
					tmp16 = CLAMP(textEdit.tmpDisp16, 1, 127);

					if (song->header.songLength != tmp16)
					{
						song->header.songLength = tmp16;

						int16_t posEdPos = song->currPos;
						if (posEdPos > song->header.songLength-1)
							posEdPos = song->header.songLength-1;

						editor.currPosEdPattDisp = &song->header.patternTable[posEdPos];

						if (ui.posEdScreenShown)
							ui.updatePosEd = true;

						ui.updateSongLength = true;
						ui.updateSongSize = true;
						updateWindowTitle(MOD_IS_MODIFIED);
					}
				}
			}
			break;

			case PTB_PATTDATA:
			{
				editor.currEditPatternDisp = &song->currPattern;

				if (updateValue)
				{
					if (song->currPattern != textEdit.tmpDisp16)
					{
						setPattern(textEdit.tmpDisp16);
						ui.updatePatternData = true;
						ui.updateCurrPattText = true;
					}
				}
			}
			break;

			case PTB_SAMPLES:
			{
				editor.currSampleDisp = &editor.currSample;

				if (updateValue)
				{
					tmp8 = textEdit.tmpDisp8;
					if (tmp8 < 0x00) // (signed) if >0x7F was entered, clamp to 0x1F
						tmp8 = 0x1F;

					tmp8 = CLAMP(tmp8, 0x01, 0x1F) - 1;

					if (tmp8 != editor.currSample)
					{
						editor.currSample = tmp8;
						updateCurrSample();
					}
				}
			}
			break;

			case PTB_SVOLUMES:
			{
				s->volumeDisp = &s->volume;

				if (updateValue)
				{
					tmp8 = textEdit.tmpDisp8;

					// signed check + normal check
					if (tmp8 < 0x00 || tmp8 > 0x40)
						 tmp8 = 0x40;

					if (s->volume != tmp8)
					{
						s->volume = tmp8;
						ui.updateCurrSampleVolume = true;
						updateWindowTitle(MOD_IS_MODIFIED);
					}
				}
			}
			break;

			case PTB_SLENGTHS:
			{
				s->lengthDisp = &s->length;

				if (updateValue)
				{
					tmp32 = textEdit.tmpDisp32 & ~1; // even'ify
					if (tmp32 > config.maxSampleLength)
						tmp32 = config.maxSampleLength;

					if (s->loopStart+s->loopLength > 2)
					{
						if (tmp32 < s->loopStart+s->loopLength)
							tmp32 = s->loopStart+s->loopLength;
					}

					tmp32 &= ~1;

					if (s->length != tmp32)
					{
						turnOffVoices();
						s->length = tmp32;

						ui.updateCurrSampleLength = true;
						ui.updateSongSize = true;
						updateSamplePos();

						if (ui.samplerScreenShown)
							redrawSample();

						recalcChordLength();
						updateWindowTitle(MOD_IS_MODIFIED);
					}
				}
			}
			break;

			case PTB_SREPEATS:
			{
				s->loopStartDisp = &s->loopStart;

				if (updateValue)
				{
					tmp32 = textEdit.tmpDisp32 & ~1; // even'ify
					if (tmp32 > config.maxSampleLength)
						tmp32 = config.maxSampleLength;

					if (s->length >= s->loopLength)
					{
						if (tmp32+s->loopLength > s->length)
							 tmp32 = s->length - s->loopLength;
					}
					else
					{
						tmp32 = 0;
					}

					tmp32 &= ~1;

					if (s->loopStart != tmp32)
					{
						turnOffVoices();
						s->loopStart = tmp32;
						updatePaulaLoops();

						ui.updateCurrSampleRepeat = true;

						if (ui.editOpScreenShown && ui.editOpScreen == 3)
							ui.updateChordLengthText = true;

						if (ui.samplerScreenShown)
							setLoopSprites();

						updateWindowTitle(MOD_IS_MODIFIED);
					}
				}
			}
			break;

			case PTB_SREPLENS:
			{
				s->loopLengthDisp = &s->loopLength;

				if (updateValue)
				{
					tmp32 = textEdit.tmpDisp32 & ~1; // even'ify
					if (tmp32 > config.maxSampleLength)
						tmp32 = config.maxSampleLength;

					if (s->length >= s->loopStart)
					{
						if (s->loopStart+tmp32 > s->length)
							tmp32 = s->length - s->loopStart;
					}
					else
					{
						tmp32 = 2;
					}

					tmp32 &= ~1;

					if (tmp32 < 2)
						tmp32 = 2;

					if (s->loopLength != tmp32)
					{
						turnOffVoices();
						s->loopLength = tmp32;
						updatePaulaLoops();

						ui.updateCurrSampleReplen = true;
						if (ui.editOpScreenShown && ui.editOpScreen == 3)
							ui.updateChordLengthText = true;

						if (ui.samplerScreenShown)
							setLoopSprites();

						updateWindowTitle(MOD_IS_MODIFIED);
					}
				}
			}
			break;

			default: break;
		}

		pointerSetPreviousMode();
	}

	textEdit.type = 0;
}

void handleTextEditInputChar(char textChar)
{
	// we only want certain keys
	if (textChar < ' ' || textChar > '~')
		return;

	// A..Z -> a..z
	if (textChar >= 'A' && textChar <= 'Z')
		textChar = (char)tolower(textChar);

	if (textEdit.type == TEXT_EDIT_STRING)
	{
		if (textEdit.textPtr < textEdit.textEndPtr)
		{
			if (!editor.mixFlag)
			{
				char *readTmp = textEdit.textEndPtr;
				while (readTmp > textEdit.textPtr)
				{
					int8_t readTmpPrev = *--readTmp;
					*(readTmp + 1) = readTmpPrev;
				}

				*textEdit.textEndPtr = '\0';
				*textEdit.textPtr++ = textChar;

				moveTextCursorRight();
			}
			else if ((textChar >= '0' && textChar <= '9') || (textChar >= 'a' && textChar <= 'f'))
			{
				if (textEdit.cursorBlock == 14) // hack for sample mix text
				{
					*textEdit.textPtr = textChar;
				}
				else
				{
					*textEdit.textPtr++ = textChar;
					moveTextCursorRight();

					// hack for sample mix text
					if (textEdit.cursorBlock == 9) 
					{
						for (int32_t i = 0; i < 4; i++)
						{
							textEdit.textPtr++;
							moveTextCursorRight();
						}
					}
					else if (textEdit.cursorBlock == 6)
					{
						textEdit.textPtr++;
						moveTextCursorRight();
					}
				}
			}
		}
	}
	else
	{
		if (textEdit.type == TEXT_EDIT_DECIMAL)
		{
			if (textChar >= '0' && textChar <= '9')
			{
				uint8_t digit1, digit2, digit3, digit4;
				uint32_t number;

				textChar -= '0';

				if (textEdit.numDigits == 4)
				{
					number = *textEdit.numPtr16;
					digit4 = number % 10; number /= 10;
					digit3 = number % 10; number /= 10;
					digit2 = number % 10; number /= 10;
					digit1 = (uint8_t)number;

					     if (textEdit.cursorBlock == 0) *textEdit.numPtr16 = (textChar * 1000) + (digit2 * 100) + (digit3 * 10) + digit4;
					else if (textEdit.cursorBlock == 1) *textEdit.numPtr16 = (digit1 * 1000) + (textChar * 100) + (digit3 * 10) + digit4;
					else if (textEdit.cursorBlock == 2) *textEdit.numPtr16 = (digit1 * 1000) + (digit2 * 100) + (textChar * 10) + digit4;
					else if (textEdit.cursorBlock == 3) *textEdit.numPtr16 = (digit1 * 1000) + (digit2 * 100) + (digit3 * 10) + textChar;
				}
				else if (textEdit.numDigits == 3)
				{
					number = *textEdit.numPtr16;
					digit3 = number % 10; number /= 10;
					digit2 = number % 10; number /= 10;
					digit1 = (uint8_t)number;

					     if (textEdit.cursorBlock == 0) *textEdit.numPtr16 = (textChar * 100) + (digit2 * 10) + digit3;
					else if (textEdit.cursorBlock == 1) *textEdit.numPtr16 = (digit1 * 100) + (textChar * 10) + digit3;
					else if (textEdit.cursorBlock == 2) *textEdit.numPtr16 = (digit1 * 100) + (digit2 * 10) + textChar;
				}
				else if (textEdit.numDigits == 2)
				{
					number = *textEdit.numPtr16;
					digit2 = number % 10; number /= 10;
					digit1 = (uint8_t)number;

					     if (textEdit.cursorBlock == 0) *textEdit.numPtr16 = (textChar * 10) + digit2;
					else if (textEdit.cursorBlock == 1) *textEdit.numPtr16 = (digit1 * 10) + textChar;
				}

				moveTextCursorRight();
				if (textEdit.cursorBlock >= textEdit.numDigits)
					leaveTextEditMode(EDIT_TEXT_UPDATE);
			}
		}
		else
		{
			if ((textChar >= '0' && textChar <= '9') || (textChar >= 'a' && textChar <= 'f'))
			{
				if (textChar <= '9')
					textChar -= '0';
				else if (textChar <= 'f')
					textChar -= 'a'-10;

				if (textEdit.numBits == 17)
				{
					*textEdit.numPtr32 &= ~(0xF0000 >> (textEdit.cursorBlock << 2));
					*textEdit.numPtr32 |= textChar << (16 - (textEdit.cursorBlock << 2));
				}
				else if (textEdit.numBits == 16)
				{
					if (textEdit.force32BitNumPtr)
					{
						*textEdit.numPtr32 &= ~(0xF000 >> (textEdit.cursorBlock << 2));
						*textEdit.numPtr32 |= textChar << (12 - (textEdit.cursorBlock << 2));
					}
					else
					{
						*textEdit.numPtr16 &= ~(0xF000 >> (textEdit.cursorBlock << 2));
						*textEdit.numPtr16 |= textChar << (12 - (textEdit.cursorBlock << 2));
					}
				}
				else if (textEdit.numBits == 8)
				{
					*textEdit.numPtr8 &= ~(0xF0 >> (textEdit.cursorBlock << 2));
					*textEdit.numPtr8 |= textChar << (4 - (textEdit.cursorBlock << 2));
				}

				moveTextCursorRight();
				if (textEdit.cursorBlock >= textEdit.numDigits)
					leaveTextEditMode(EDIT_TEXT_UPDATE);
			}
		}
	}

	redrawTextEditObject();

	if (!keyb.repeatKey)
		keyb.delayCounter = 0;

	keyb.repeatKey = true;
	keyb.delayKey = true;
}

bool handleTextEditMode(SDL_Scancode scancode)
{
	switch (scancode)
	{
		case SDL_SCANCODE_ESCAPE:
		{
			editor.blockMarkFlag = false;
			if (ui.editTextFlag)
			{
				leaveTextEditMode(EDIT_TEXT_NO_UPDATE);
				return false;
			}
		}
		break;

		case SDL_SCANCODE_HOME:
		{
			if (ui.editTextFlag && !editor.mixFlag && textEdit.type == TEXT_EDIT_STRING)
			{
				while (textEdit.textPtr > textEdit.textStartPtr)
					editTextPrevChar();
			}
		}
		break;

		case SDL_SCANCODE_END:
		{
			if (ui.editTextFlag && !editor.mixFlag && textEdit.type == TEXT_EDIT_STRING)
			{
				while (!textEdit.endReached)
					editTextNextChar();
			}
		}
		break;

		case SDL_SCANCODE_LEFT:
		{
			if (ui.editTextFlag)
			{
				editTextPrevChar();
				if (!keyb.repeatKey)
					keyb.delayCounter = 0;

				keyb.repeatKey = true;
				keyb.delayKey = false;
			}
			else
			{
				keyb.delayKey = false;
				keyb.repeatKey = true;
			}
		}
		break;

		case SDL_SCANCODE_RIGHT:
		{
			if (ui.editTextFlag)
			{
				editTextNextChar();
				if (!keyb.repeatKey)
					keyb.delayCounter = 0;

				keyb.repeatKey = true;
				keyb.delayKey = false;
			}
			else
			{
				keyb.delayKey = false;
				keyb.repeatKey = true;
			}
		}
		break;

		case SDL_SCANCODE_DELETE:
		{
			if (ui.editTextFlag)
			{
				if (editor.mixFlag || textEdit.type != TEXT_EDIT_STRING)
					break;

				char *readTmp = textEdit.textPtr;
				while (readTmp < textEdit.textEndPtr)
				{
					int8_t readTmpNext = *(readTmp + 1);
					*readTmp++ = readTmpNext;
				}

				// kludge to prevent cloning last character if the song/sample name has one character too much
				if (textEdit.object == PTB_SONGNAME || textEdit.object == PTB_SAMPLENAME || textEdit.object == PTB_PE_PATTNAME)
					*textEdit.textEndPtr = '\0';

				if (!keyb.repeatKey)
					keyb.delayCounter = 0;

				keyb.repeatKey = true;
				keyb.delayKey = true;

				redrawTextEditObject();
			}
		}
		break;

		case SDL_SCANCODE_BACKSPACE:
		{
			if (ui.editTextFlag)
			{
				if (editor.mixFlag || textEdit.type != TEXT_EDIT_STRING)
					break;

				if (textEdit.textPtr > textEdit.textStartPtr)
				{
					textEdit.textPtr--;

					char *readTmp = textEdit.textPtr;
					while (readTmp < textEdit.textEndPtr)
					{
						int8_t readTmpNext = *(readTmp + 1);
						*readTmp++ = readTmpNext;
					}

					// kludge to prevent cloning last character if the song/sample name has one character too much
					if (textEdit.object == PTB_SONGNAME || textEdit.object == PTB_SAMPLENAME || textEdit.object == PTB_PE_PATTNAME)
						*textEdit.textEndPtr = '\0';

					moveTextCursorLeft();
					redrawTextEditObject();
				}

				if (!keyb.repeatKey)
					keyb.delayCounter = 0;

				keyb.repeatKey = true;
				keyb.delayKey = false;
			}
			else
			{
				if (ui.diskOpScreenShown)
				{
#ifdef _WIN32
					diskOpSetPath(L"..", DISKOP_CACHE);
#else
					diskOpSetPath("..", DISKOP_CACHE);
#endif
				}
				else if (keyb.shiftPressed || keyb.leftAltPressed || keyb.leftCtrlPressed)
				{
					saveUndo();
					if (keyb.leftAltPressed && !keyb.leftCtrlPressed)
					{
						if (song->currRow > 0)
						{
							for (int32_t i = 0; i < PAULA_VOICES; i++)
							{
								for (int32_t j = (song->currRow - 1); j < MOD_ROWS; j++)
								{
									note_t *noteSrc = &song->patterns[song->currPattern][((j + 1) * PAULA_VOICES) + i];
									song->patterns[song->currPattern][(j * PAULA_VOICES) + i] = *noteSrc;
								}

								// clear newly made row on very bottom
								note_t *noteDst = &song->patterns[song->currPattern][(63 * PAULA_VOICES) + i];
								noteDst->period = 0;
								noteDst->sample = 0;
								noteDst->command = 0;
								noteDst->param = 0;
							}

							song->currRow--;
							ui.updatePatternData = true;
						}
					}
					else
					{
						if (song->currRow > 0)
						{
							for (int32_t i = song->currRow-1; i < MOD_ROWS-1; i++)
							{
								note_t *noteSrc = &song->patterns[song->currPattern][((i + 1) * PAULA_VOICES) + cursor.channel];
								note_t *noteDst = &song->patterns[song->currPattern][(i * PAULA_VOICES) + cursor.channel];

								if (keyb.leftCtrlPressed)
								{
									noteDst->command = noteSrc->command;
									noteDst->param = noteSrc->param;
								}
								else
								{
									*noteDst = *noteSrc;
								}
							}

							// clear newly made row on very bottom
							note_t *noteDst = &song->patterns[song->currPattern][(63 * PAULA_VOICES) + cursor.channel];
							noteDst->period = 0;
							noteDst->sample = 0;
							noteDst->command = 0;
							noteDst->param = 0;

							song->currRow--;
							ui.updatePatternData = true;
						}
					}
				}
				else
				{
					editor.stepPlayEnabled = true;
					editor.stepPlayBackwards = true;

					editor.stepPlayLastMode = editor.currMode;

					if (config.keepEditModeAfterStepPlay && editor.stepPlayLastMode == MODE_EDIT)
						doStopIt(false);
					else
						doStopIt(true);

					playPattern((song->currRow - 1) & 63);

					if (config.keepEditModeAfterStepPlay && editor.stepPlayLastMode == MODE_EDIT)
					{
						pointerSetMode(POINTER_MODE_EDIT, DO_CARRY);
						editor.playMode = PLAY_MODE_NORMAL;
						editor.currMode = MODE_EDIT;
					}
				}
			}
		}
		break;

		default: break;
	}

	if (ui.editTextFlag)
	{
		if (scancode == SDL_SCANCODE_RETURN || scancode == SDL_SCANCODE_KP_ENTER)
		{
			// dirty hack
			if (textEdit.object == PTB_SAMPLES)
				textEdit.tmpDisp8++;

			leaveTextEditMode(EDIT_TEXT_UPDATE);

			if (editor.mixFlag)
			{
				editor.mixFlag = false;
				ui.updateMixText = true;
				doMix();
			}
		}

		return false; // don't continue further key handling
	}

	return true; // continue further key handling (we're not editing text)
}
