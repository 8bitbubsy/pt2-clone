// for finding memory leaks in debug mode with Visual Studio 
#if defined _DEBUG && defined _MSC_VER
#include <crtdbg.h>
#endif

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <ctype.h> // tolower()
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif
#include "pt2_textout.h"
#include "pt2_header.h"
#include "pt2_helpers.h"
#include "pt2_visuals.h"
#include "pt2_palette.h"
#include "pt2_diskop.h"
#include "pt2_edit.h"
#include "pt2_sampler.h"
#include "pt2_audio.h"
#include "pt2_keyboard.h"
#include "pt2_tables.h"
#include "pt2_modloader.h"
#include "pt2_mouse.h"
#include "pt2_unicode.h"

#ifdef _WIN32
extern bool windowsKeyIsDown;
extern HHOOK g_hKeyboardHook;
#endif

void movePatCurPrevCh(void);
void movePatCurNextCh(void);
void movePatCurRight(void);
void movePatCurLeft(void);

static bool handleGeneralModes(SDL_Keycode keycode, SDL_Scancode scancode);
bool handleTextEditMode(SDL_Scancode scancode);

void sampleUpButton(void); // pt_mouse.c
void sampleDownButton(void); // pt_mouse.c

void gotoNextMulti(void)
{
	editor.cursor.channel = (editor.multiModeNext[editor.cursor.channel] - 1) & 3;
	editor.cursor.pos = editor.cursor.channel * 6;
	updateCursorPos();
}

void readKeyModifiers(void)
{
	uint32_t modState;

	modState = SDL_GetModState();

	keyb.leftCtrlPressed = (modState & KMOD_LCTRL)  ? true : false;
	keyb.leftAltPressed = (modState & KMOD_LALT) ? true : false;
	keyb.shiftPressed = (modState & (KMOD_LSHIFT + KMOD_RSHIFT)) ? true : false;
#ifdef __APPLE__
	keyb.leftCommandPressed = (modState & KMOD_LGUI) ? true : false;
#endif
#ifndef _WIN32 // MS Windows: handled in lowLevelKeyboardProc
	keyb.leftAmigaPressed = (modState & KMOD_LGUI) ? true : false;
#endif
}

#ifdef _WIN32
// for taking control over windows key and numlock on keyboard if app has focus
LRESULT CALLBACK lowLevelKeyboardProc(int32_t nCode, WPARAM wParam, LPARAM lParam)
{
	bool bEatKeystroke;
	KBDLLHOOKSTRUCT *p;
	SDL_Event inputEvent;

	if (nCode < 0 || nCode != HC_ACTION) // do not process message
		return CallNextHookEx(g_hKeyboardHook, nCode, wParam, lParam);

	bEatKeystroke = false;
	p = (KBDLLHOOKSTRUCT *)lParam;

	switch (wParam)
	{
		case WM_KEYUP:
		case WM_KEYDOWN:
		{
			bEatKeystroke = (SDL_GetWindowFlags(video.window) & SDL_WINDOW_INPUT_FOCUS) && (p->vkCode == VK_LWIN || p->vkCode == VK_NUMLOCK);

			if (bEatKeystroke)
			{
				if (wParam == WM_KEYDOWN)
				{
					if (p->vkCode == VK_NUMLOCK)
					{
						memset(&inputEvent, 0, sizeof (SDL_Event));
						inputEvent.type = SDL_KEYDOWN;
						inputEvent.key.type = SDL_KEYDOWN;
						inputEvent.key.state = 1;
						inputEvent.key.keysym.scancode = (SDL_Scancode)69;
						inputEvent.key.keysym.mod = KMOD_NUM;
						inputEvent.key.keysym.scancode = SDL_SCANCODE_NUMLOCKCLEAR;

						SDL_PushEvent(&inputEvent);
					}
					else if (!windowsKeyIsDown)
					{
						windowsKeyIsDown = true;
						keyb.leftAmigaPressed = true;

						memset(&inputEvent, 0, sizeof (SDL_Event));
						inputEvent.type = SDL_KEYDOWN;
						inputEvent.key.type = SDL_KEYDOWN;
						inputEvent.key.state = 1;
						inputEvent.key.keysym.scancode = (SDL_Scancode)91;
						inputEvent.key.keysym.scancode = SDL_SCANCODE_LGUI;

						SDL_PushEvent(&inputEvent);
					}
				}
				else if (wParam == WM_KEYUP)
				{
					if (p->vkCode == VK_NUMLOCK)
					{
						memset(&inputEvent, 0, sizeof (SDL_Event));
						inputEvent.type = SDL_KEYUP;
						inputEvent.key.type = SDL_KEYUP;
						inputEvent.key.keysym.scancode = (SDL_Scancode)69;
						inputEvent.key.keysym.scancode = SDL_SCANCODE_NUMLOCKCLEAR;

						SDL_PushEvent(&inputEvent);
					}
					else
					{
						windowsKeyIsDown = false;
						keyb.leftAmigaPressed = false;

						memset(&inputEvent, 0, sizeof (SDL_Event));
						inputEvent.type = SDL_KEYUP;
						inputEvent.key.type = SDL_KEYUP;
						inputEvent.key.keysym.scancode = (SDL_Scancode)91;
						inputEvent.key.keysym.scancode = SDL_SCANCODE_LGUI;

						SDL_PushEvent(&inputEvent);
					}
				}
			}
			break;
		}

		default: break;
	}

	return bEatKeystroke ? true : CallNextHookEx(g_hKeyboardHook, nCode, wParam, lParam);
}
#endif

// these four functions are for the text edit cursor
void textMarkerMoveLeft(void)
{
	if (editor.ui.dstPos > 0)
	{
		removeTextEditMarker();
		editor.ui.dstPos--;
		editor.ui.lineCurX -= FONT_CHAR_W;
		renderTextEditMarker();
	}
	else
	{
		if (editor.ui.dstOffset != NULL)
		{
			(*editor.ui.dstOffset)--;
			if (editor.ui.editObject == PTB_DO_DATAPATH)
				editor.ui.updateDiskOpPathText = true;
		}
	}
}

void textMarkerMoveRight(void)
{
	if (editor.ui.editTextType == TEXT_EDIT_STRING)
	{
		if (editor.ui.dstPos < editor.ui.textLength-1)
		{
			removeTextEditMarker();
			editor.ui.dstPos++;
			editor.ui.lineCurX += FONT_CHAR_W;
			renderTextEditMarker();
		}
		else
		{
			if (editor.ui.dstOffset != NULL)
			{
				(*editor.ui.dstOffset)++;
				if (editor.ui.editObject == PTB_DO_DATAPATH)
					editor.ui.updateDiskOpPathText = true;
			}
		}
	}
	else
	{
		// we end up here when entering a number/hex digit

		if (editor.ui.dstPos < editor.ui.numLen)
			removeTextEditMarker();

		editor.ui.dstPos++;
		editor.ui.lineCurX += FONT_CHAR_W;

		if (editor.ui.dstPos < editor.ui.numLen)
			renderTextEditMarker();

		// don't clamp, dstPos is tested elsewhere to check if done editing a number
	}
}

void textCharPrevious(void)
{
	if (editor.ui.editTextType != TEXT_EDIT_STRING)
	{
		if (editor.ui.dstPos > 0)
		{
			removeTextEditMarker();
			editor.ui.dstPos--;
			editor.ui.lineCurX -= FONT_CHAR_W;
			renderTextEditMarker();
		}

		return;
	}

	if (editor.mixFlag && editor.ui.dstPos <= 4)
		return;

	if (editor.ui.editPos > editor.ui.showTextPtr)
	{
		removeTextEditMarker();

		editor.ui.editPos--;
		textMarkerMoveLeft();

		if (editor.mixFlag) // special mode for mix window
		{
			if (editor.ui.dstPos == 12)
			{
				for (uint8_t i = 0; i < 4; i++)
				{
					editor.ui.editPos--;
					textMarkerMoveLeft();
				}
			}
			else if (editor.ui.dstPos == 6)
			{
				editor.ui.editPos--;
				textMarkerMoveLeft();
			}
		}

		renderTextEditMarker();
	}

	editor.ui.dstOffsetEnd = false;
}

void textCharNext(void)
{
	if (editor.ui.editTextType != TEXT_EDIT_STRING)
	{
		if (editor.ui.dstPos < editor.ui.numLen-1)
		{
			removeTextEditMarker();
			editor.ui.dstPos++;
			editor.ui.lineCurX += FONT_CHAR_W;
			renderTextEditMarker();
		}

		return;
	}

	if (editor.mixFlag && editor.ui.dstPos >= 14)
		return;

	if (editor.ui.editPos < editor.ui.textEndPtr)
	{
		if (*editor.ui.editPos != '\0')
		{
			removeTextEditMarker();

			editor.ui.editPos++;
			textMarkerMoveRight();

			if (editor.mixFlag) // special mode for mix window
			{
				if (editor.ui.dstPos == 9)
				{
					for (uint8_t i = 0; i < 4; i++)
					{
						editor.ui.editPos++;
						textMarkerMoveRight();
					}
				}
				else if (editor.ui.dstPos == 6)
				{
					editor.ui.editPos++;
					textMarkerMoveRight();
				}
			}

			renderTextEditMarker();
		}
		else
		{
			editor.ui.dstOffsetEnd = true;
		}
	}
	else
	{
		editor.ui.dstOffsetEnd = true;
	}
}
// --------------------------------

void keyUpHandler(SDL_Scancode scancode, SDL_Keycode keycode)
{
	(void)keycode;

	if (scancode == SDL_SCANCODE_KP_PLUS)
	{
		keyb.keypadEnterPressed = false;
	}

	if (scancode == keyb.lastRepKey)
		keyb.lastRepKey = SDL_SCANCODE_UNKNOWN;

	switch (scancode)
	{
		// modifiers shouldn't reset keyb repeat/delay flags & counters
		case SDL_SCANCODE_LCTRL:
		case SDL_SCANCODE_RCTRL:
		case SDL_SCANCODE_LSHIFT:
		case SDL_SCANCODE_RSHIFT:
		case SDL_SCANCODE_LALT:
		case SDL_SCANCODE_RALT:
		case SDL_SCANCODE_LGUI:
		case SDL_SCANCODE_RGUI:
		case SDL_SCANCODE_MENU:
		case SDL_SCANCODE_MODE:
		case SDL_SCANCODE_CAPSLOCK:
		break;

		default:
		{
			keyb.repeatKey = false;
			keyb.delayKey = false;
			keyb.repeatFrac = 0;
			keyb.delayCounter = 0;
		}
		break;
	}
}

static void incMulti(uint8_t slot)
{
	char str[32];

	assert(slot < 4);
	if (editor.multiModeNext[slot] == 4)
		editor.multiModeNext[slot] = 1;
	else
		editor.multiModeNext[slot]++;

	sprintf(str, "MULTI=%d-%d-%d-%d", editor.multiModeNext[0], editor.multiModeNext[1],
		editor.multiModeNext[2], editor.multiModeNext[3]);
	displayMsg(str);
}

void keyDownHandler(SDL_Scancode scancode, SDL_Keycode keycode)
{
	uint8_t blockFrom, blockTo;
	int16_t i, j;
	note_t *patt, *noteSrc, *noteDst, noteTmp;
	moduleSample_t *s;
	moduleChannel_t *ch;

	if (scancode == SDL_SCANCODE_CAPSLOCK)
	{
		editor.repeatKeyFlag ^= 1;
		return;
	}

	// kludge to allow certain repeat-keys to use custom repeat/delay values
	if (editor.repeatKeyFlag && keyb.repeatKey && scancode == keyb.lastRepKey &&
	    (keyb.leftAltPressed || keyb.leftAmigaPressed || keyb.leftCtrlPressed))
	{
		return;
	}

	if (scancode == SDL_SCANCODE_KP_PLUS)
		keyb.keypadEnterPressed = true;

	// TOGGLE FULLSCREEN (should always react)
	if (scancode == SDL_SCANCODE_F11 && !keyb.leftAltPressed)
	{
		toggleFullScreen();

		// prevent fullscreen toggle from firing twice on certain SDL2 Linux ports
#ifdef __unix__
		SDL_Delay(100);
#endif
		return;
	}

	// don't handle input if an error message wait is active or if an unknown key is passed
	if ((editor.errorMsgActive && editor.errorMsgBlock) || scancode == SDL_SCANCODE_UNKNOWN)
		return;

	// if no ALT/SHIFT/CTRL/AMIGA, update last key for repeat routine
	if (scancode != SDL_SCANCODE_LALT     && scancode != SDL_SCANCODE_RALT   &&
		scancode != SDL_SCANCODE_LCTRL    && scancode != SDL_SCANCODE_RCTRL  &&
		scancode != SDL_SCANCODE_LSHIFT   && scancode != SDL_SCANCODE_RSHIFT &&
		scancode != SDL_SCANCODE_LGUI     && scancode != SDL_SCANCODE_RGUI   &&
		scancode != SDL_SCANCODE_MENU     && scancode != SDL_SCANCODE_MODE   &&
		scancode != SDL_SCANCODE_CAPSLOCK && scancode != SDL_SCANCODE_ESCAPE)
	{
		if (editor.repeatKeyFlag)
		{
			// if Repeat Flag, repeat all keys
			if (!keyb.repeatKey)
			{
				keyb.repeatCounter = 0;
				keyb.repeatFrac = 0;
			}

			keyb.repeatKey = true;
			keyb.delayKey = true;
		}

		keyb.repeatCounter = 0;
		keyb.repeatFrac = 0;
		keyb.lastRepKey = scancode;
	}

	// ENTRY JUMPING IN DISK OP. FILELIST
	if (editor.ui.diskOpScreenShown && keyb.shiftPressed && !editor.ui.editTextFlag)
	{
		if (keycode >= 32 && keycode <= 126)
		{
			handleEntryJumping(keycode);
			return;
		}
	}

	if (!handleGeneralModes(keycode, scancode)) return;
	if (!handleTextEditMode(scancode)) return;
	if (editor.ui.samplerVolBoxShown) return;

	if (editor.ui.samplerFiltersBoxShown)
	{
		handleEditKeys(scancode, EDIT_NORMAL);
		return;
	}

	// GENERAL KEYS
	switch (scancode)
	{
		case SDL_SCANCODE_NONUSBACKSLASH: turnOffVoices(); break; // magic "kill all voices" button

		case SDL_SCANCODE_APOSTROPHE:
		{
			if (editor.autoInsFlag)
			{
				if (keyb.shiftPressed)
					editor.autoInsSlot -= 4;
				else
					editor.autoInsSlot--;

				if (editor.autoInsSlot < 0)
					editor.autoInsSlot = 0;

				editor.ui.updateTrackerFlags = true;
			}
		}
		break;

		case SDL_SCANCODE_BACKSLASH:
		{
			if (keyb.leftAltPressed)
			{
				if (handleSpecialKeys(scancode) && editor.currMode != MODE_RECORD)
					modSetPos(DONT_SET_ORDER, (modEntry->currRow + editor.editMoveAdd) & 0x3F);
			}
			else
			{
				if (editor.autoInsFlag)
				{
					if (keyb.shiftPressed)
						editor.autoInsSlot += 4;
					else
						editor.autoInsSlot++;

					if (editor.autoInsSlot > 9)
						editor.autoInsSlot = 9;
				}
				else
				{
					editor.pNoteFlag = (editor.pNoteFlag + 1) % 3;
				}

				editor.ui.updateTrackerFlags = true;
			}
		}
		break;

#ifdef __APPLE__
		case SDL_SCANCODE_RGUI:
#else
		case SDL_SCANCODE_RALT:
#endif
		{
			// right Amiga key on Amiga keyb
			if (!editor.ui.askScreenShown)
			{
				editor.playMode = PLAY_MODE_NORMAL;
				modPlay(DONT_SET_PATTERN, modEntry->currOrder, DONT_SET_ROW);
				editor.currMode = MODE_PLAY;
				pointerSetMode(POINTER_MODE_PLAY, DO_CARRY);
				statusAllRight();
			}
		}
		break;

#ifdef __APPLE__
		case SDL_SCANCODE_RALT:
#else
		case SDL_SCANCODE_RCTRL:
#endif
		{
			// right alt on Amiga keyb
			if (!editor.ui.askScreenShown)
			{
				editor.playMode = PLAY_MODE_PATTERN;
				modPlay(modEntry->currPattern, DONT_SET_ORDER, DONT_SET_ROW);
				editor.currMode = MODE_PLAY;
				pointerSetMode(POINTER_MODE_PLAY, DO_CARRY);
				statusAllRight();
			}
		}
		break;

		case SDL_SCANCODE_RSHIFT:
		{
			// right shift on Amiga keyb
			if (!editor.ui.samplerScreenShown && !editor.ui.askScreenShown)
			{
				editor.playMode = PLAY_MODE_PATTERN;
				modPlay(modEntry->currPattern, DONT_SET_ORDER, DONT_SET_ROW);
				editor.currMode = MODE_RECORD;
				pointerSetMode(POINTER_MODE_EDIT, DO_CARRY);
				statusAllRight();
			}
		}
		break;

		case SDL_SCANCODE_ESCAPE:
		{
			if (editor.ui.posEdScreenShown)
			{
				editor.ui.posEdScreenShown = false;
				displayMainScreen();
			}
			else if (editor.ui.diskOpScreenShown)
			{
				editor.ui.diskOpScreenShown = false;
				displayMainScreen();
			}
			else if (editor.ui.samplerScreenShown)
			{
				exitFromSam();
			}
			else if (editor.ui.editOpScreenShown)
			{
				editor.ui.editOpScreenShown = false;
				displayMainScreen();
			}
			else
			{
				editor.ui.askScreenShown = true;
				editor.ui.askScreenType = ASK_QUIT;

				pointerSetMode(POINTER_MODE_MSG1, NO_CARRY);
				setStatusMessage("REALLY QUIT ?", NO_CARRY);
				renderAskDialog();
				return;
			}

			pointerSetPreviousMode();
			setPrevStatusMessage();
		}
		break;

		case SDL_SCANCODE_INSERT:
		{
			if (editor.ui.samplerScreenShown)
			{
				samplerSamPaste();
				return;
			}
		}
		break;

		case SDL_SCANCODE_PAGEUP:
		{
			if (editor.ui.posEdScreenShown)
			{
				if (modEntry->currOrder > 0)
				{
					if (modEntry->currOrder-(POSED_LIST_SIZE-1) > 0)
						modSetPos(modEntry->currOrder-(POSED_LIST_SIZE-1), DONT_SET_ROW);
					else
						modSetPos(0, DONT_SET_ROW);
				}
			}
			else if (editor.ui.diskOpScreenShown)
			{
				editor.diskop.scrollOffset -= DISKOP_LINES - 1;
				if (editor.diskop.scrollOffset < 0)
					editor.diskop.scrollOffset = 0;

				editor.ui.updateDiskOpFileList = true;
			}
			else
			{
				if (editor.currMode == MODE_IDLE || editor.currMode == MODE_EDIT)
				{
					if (modEntry->currRow == 63)
						modSetPos(DONT_SET_ORDER, modEntry->currRow - 15);
					else if (modEntry->currRow == 15)
						modSetPos(DONT_SET_ORDER, 0); // 15-16 would turn into -1, which is "DON'T SET ROW" flag
					else
						modSetPos(DONT_SET_ORDER, modEntry->currRow - 16);
				}
			}

			if (!keyb.repeatKey)
				keyb.delayCounter = 0;

			keyb.repeatKey = true;
			keyb.delayKey = true;
		}
		break;

		case SDL_SCANCODE_PAGEDOWN:
		{
			if (editor.ui.posEdScreenShown)
			{
				if (modEntry->currOrder != modEntry->head.orderCount-1)
				{
					if (modEntry->currOrder+(POSED_LIST_SIZE-1) <= modEntry->head.orderCount-1)
						modSetPos(modEntry->currOrder+(POSED_LIST_SIZE-1), DONT_SET_ROW);
					else
						modSetPos(modEntry->head.orderCount - 1, DONT_SET_ROW);
				}
			}
			else if (editor.ui.diskOpScreenShown)
			{
				if (editor.diskop.numEntries > DISKOP_LINES)
				{
					editor.diskop.scrollOffset += DISKOP_LINES-1;
					if (editor.diskop.scrollOffset > editor.diskop.numEntries-DISKOP_LINES)
						editor.diskop.scrollOffset = editor.diskop.numEntries-DISKOP_LINES;

					editor.ui.updateDiskOpFileList = true;
				}
			}
			else
			{
				if (editor.currMode == MODE_IDLE || editor.currMode == MODE_EDIT)
					modSetPos(DONT_SET_ORDER, modEntry->currRow + 16);
			}

			if (!keyb.repeatKey)
				keyb.delayCounter = 0;

			keyb.repeatKey = true;
			keyb.delayKey = true;
		}
		break;

		case SDL_SCANCODE_HOME:
		{
			if (editor.ui.posEdScreenShown)
			{
				if (modEntry->currOrder > 0)
					modSetPos(0, DONT_SET_ROW);
			}
			else if (editor.ui.diskOpScreenShown)
			{
				if (editor.diskop.scrollOffset != 0)
				{
					editor.diskop.scrollOffset = 0;
					editor.ui.updateDiskOpFileList = true;
				}
			}
			else
			{
				if (editor.currMode == MODE_IDLE || editor.currMode == MODE_EDIT)
					modSetPos(DONT_SET_ORDER, 0);
			}
		}
		break;

		case SDL_SCANCODE_END:
		{
			if (editor.ui.posEdScreenShown)
			{
				modSetPos(modEntry->head.orderCount - 1, DONT_SET_ROW);
			}
			else if (editor.ui.diskOpScreenShown)
			{
				if (editor.diskop.numEntries > DISKOP_LINES)
				{
					editor.diskop.scrollOffset = editor.diskop.numEntries - DISKOP_LINES;
					editor.ui.updateDiskOpFileList = true;
				}
			}
			else
			{
				if (editor.currMode == MODE_IDLE || editor.currMode == MODE_EDIT)
					modSetPos(DONT_SET_ORDER, 63);
			}
		}
		break;

		case SDL_SCANCODE_DELETE:
		{
			if (editor.ui.samplerScreenShown)
				samplerSamDelete(NO_SAMPLE_CUT);
			else
				handleEditKeys(scancode, EDIT_NORMAL);
		}
		break;

		case SDL_SCANCODE_F12:
		{
			if (keyb.leftCtrlPressed)
			{
				editor.timingMode ^= 1;
				if (editor.timingMode == TEMPO_MODE_VBLANK)
				{
					editor.oldTempo = modEntry->currBPM;
					modSetTempo(125);
				}
				else
				{
					modSetTempo(editor.oldTempo);
				}

				editor.ui.updateSongTiming = true;
			}
			else if (keyb.shiftPressed)
			{
				toggleAmigaPanMode();
			}
			else
			{
				toggleA500Filters();
			}
		}
		break;

		case SDL_SCANCODE_RETURN:
		{
			if (editor.ui.askScreenShown)
			{
				editor.ui.answerNo = false;
				editor.ui.answerYes = true;
				editor.ui.askScreenShown = false;

				handleAskYes();
			}
			else
			{
				if (keyb.shiftPressed || keyb.leftAltPressed || keyb.leftCtrlPressed)
				{
					saveUndo();
					if (keyb.leftAltPressed && !keyb.leftCtrlPressed)
					{
						if (modEntry->currRow < 63)
						{
							for (i = 0; i < AMIGA_VOICES; i++)
							{
								for (j = 62; j >= modEntry->currRow; j--)
								{
									noteSrc = &modEntry->patterns[modEntry->currPattern][(j * AMIGA_VOICES) + i];
									modEntry->patterns[modEntry->currPattern][((j + 1) * AMIGA_VOICES) + i] = *noteSrc;
								}

								noteDst = &modEntry->patterns[modEntry->currPattern][((j + 1) * AMIGA_VOICES) + i];

								noteDst->period = 0;
								noteDst->sample = 0;
								noteDst->command = 0;
								noteDst->param = 0;
							}

							modEntry->currRow++;

							updateWindowTitle(MOD_IS_MODIFIED);
							editor.ui.updatePatternData = true;
						}
					}
					else
					{
						if (modEntry->currRow < 63)
						{
							for (i = 62; i >= modEntry->currRow; i--)
							{
								noteSrc = &modEntry->patterns[modEntry->currPattern][((i + 0) * AMIGA_VOICES) + editor.cursor.channel];
								noteDst = &modEntry->patterns[modEntry->currPattern][((i + 1) * AMIGA_VOICES) + editor.cursor.channel];

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

							noteDst = &modEntry->patterns[modEntry->currPattern][((i + 1) * AMIGA_VOICES) + editor.cursor.channel];

							if (!keyb.leftCtrlPressed)
							{
								noteDst->period = 0;
								noteDst->sample = 0;
							}

							noteDst->command = 0;
							noteDst->param = 0;

							modEntry->currRow++;

							updateWindowTitle(MOD_IS_MODIFIED);
							editor.ui.updatePatternData = true;
						}
					}
				}
				else
				{
					editor.stepPlayEnabled = true;
					editor.stepPlayBackwards = false;

					doStopIt();
					playPattern(modEntry->currRow);
				}
			}
		}
		break;

		// toggle between IDLE and EDIT (IDLE if PLAY)
		case SDL_SCANCODE_SPACE:
		{
			if (editor.currMode == MODE_PLAY)
			{
				modStop();
				editor.currMode = MODE_IDLE;
				pointerSetMode(POINTER_MODE_IDLE, DO_CARRY);
				statusAllRight();
			}
			else if (editor.currMode == MODE_EDIT || editor.currMode == MODE_RECORD)
			{
				if (!editor.ui.samplerScreenShown)
				{
					modStop();
					editor.currMode = MODE_IDLE;
					pointerSetMode(POINTER_MODE_IDLE, DO_CARRY);
					statusAllRight();
				}
			}
			else if (!editor.ui.samplerScreenShown)
			{
				modStop();
				editor.currMode = MODE_EDIT;
				pointerSetMode(POINTER_MODE_EDIT, DO_CARRY);
				statusAllRight();
			}
		}
		break;

		case SDL_SCANCODE_F1: editor.keyOctave = OCTAVE_LOW;  break;
		case SDL_SCANCODE_F2: editor.keyOctave = OCTAVE_HIGH; break;

		case SDL_SCANCODE_F3:
		{
			if (editor.ui.samplerScreenShown)
			{
				samplerSamDelete(SAMPLE_CUT);
			}
			else
			{
				if (keyb.shiftPressed)
				{
					// cut channel and put in buffer
					saveUndo();

					noteDst = editor.trackBuffer;
					for (i = 0; i < MOD_ROWS; i++)
					{
						noteSrc = &modEntry->patterns[modEntry->currPattern][(i * AMIGA_VOICES) + editor.cursor.channel];
						*noteDst++ = *noteSrc;

						noteSrc->period = 0;
						noteSrc->sample = 0;
						noteSrc->command = 0;
						noteSrc->param = 0;
					}

					updateWindowTitle(MOD_IS_MODIFIED);
					editor.ui.updatePatternData = true;
				}
				else if (keyb.leftAltPressed)
				{
					// cut pattern and put in buffer
					saveUndo();

					memcpy(editor.patternBuffer, modEntry->patterns[modEntry->currPattern],
						sizeof (note_t) * (AMIGA_VOICES * MOD_ROWS));

					memset(modEntry->patterns[modEntry->currPattern], 0,
						sizeof (note_t) * (AMIGA_VOICES * MOD_ROWS));

					updateWindowTitle(MOD_IS_MODIFIED);
					editor.ui.updatePatternData = true;
				}
				else if (keyb.leftCtrlPressed)
				{
					// cut channel commands and put in buffer
					saveUndo();

					noteDst = editor.cmdsBuffer;
					for (i = 0; i < MOD_ROWS; i++)
					{
						noteSrc = &modEntry->patterns[modEntry->currPattern][(i * AMIGA_VOICES) + editor.cursor.channel];
						*noteDst++ = *noteSrc;

						noteSrc->command = 0;
						noteSrc->param = 0;
					}

					updateWindowTitle(MOD_IS_MODIFIED);
					editor.ui.updatePatternData = true;
				}
			}
		}
		break;

		case SDL_SCANCODE_F4:
		{
			if (editor.ui.samplerScreenShown)
			{
				samplerSamCopy();
			}
			else
			{
				if (keyb.shiftPressed)
				{
					// copy channel to buffer

					noteDst = editor.trackBuffer;
					for (i = 0; i < MOD_ROWS; i++)
						*noteDst++ = modEntry->patterns[modEntry->currPattern][(i * AMIGA_VOICES) + editor.cursor.channel];
				}
				else if (keyb.leftAltPressed)
				{
					// copy pattern to buffer

					memcpy(editor.patternBuffer, modEntry->patterns[modEntry->currPattern],
						sizeof (note_t) * (AMIGA_VOICES * MOD_ROWS));
				}
				else if (keyb.leftCtrlPressed)
				{
					// copy channel commands to buffer

					noteDst = editor.cmdsBuffer;
					for (i = 0; i < MOD_ROWS; i++)
					{
						noteSrc = &modEntry->patterns[modEntry->currPattern][(i * AMIGA_VOICES) + editor.cursor.channel];
						noteDst->command = noteSrc->command;
						noteDst->param = noteSrc->param;

						noteDst++;
					}
				}
			}
		}
		break;

		case SDL_SCANCODE_F5:
		{
			if (editor.ui.samplerScreenShown)
			{
				samplerSamPaste();
			}
			else
			{
				if (keyb.shiftPressed)
				{
					// paste channel buffer to channel
					saveUndo();

					noteSrc = editor.trackBuffer;
					for (i = 0; i < MOD_ROWS; i++)
						modEntry->patterns[modEntry->currPattern][(i * AMIGA_VOICES) + editor.cursor.channel] = *noteSrc++;

					updateWindowTitle(MOD_IS_MODIFIED);
					editor.ui.updatePatternData = true;
				}
				else if (keyb.leftAltPressed)
				{
					// paste pattern buffer to pattern
					saveUndo();

					memcpy(modEntry->patterns[modEntry->currPattern],
						editor.patternBuffer, sizeof (note_t) * (AMIGA_VOICES * MOD_ROWS));

					updateWindowTitle(MOD_IS_MODIFIED);
					editor.ui.updatePatternData = true;
				}
				else if (keyb.leftCtrlPressed)
				{
					// paste channel commands buffer to channel
					saveUndo();

					noteSrc = editor.cmdsBuffer;
					for (i = 0; i < MOD_ROWS; i++)
					{
						noteDst = &modEntry->patterns[modEntry->currPattern][(i * AMIGA_VOICES) + editor.cursor.channel];
						noteDst->command = noteSrc->command;
						noteDst->param = noteSrc->param;

						noteSrc++;
					}

					updateWindowTitle(MOD_IS_MODIFIED);
					editor.ui.updatePatternData = true;
				}
			}
		}
		break;

		case SDL_SCANCODE_F6:
		{
			if (keyb.shiftPressed)
			{
				editor.f6Pos = modEntry->currRow;
				displayMsg("POSITION SET");
			}
			else
			{
				if (keyb.leftAltPressed)
				{
					editor.playMode = PLAY_MODE_PATTERN;
					modPlay(modEntry->currPattern, DONT_SET_ORDER, editor.f6Pos);

					editor.currMode = MODE_PLAY;
					pointerSetMode(POINTER_MODE_PLAY, DO_CARRY);
					statusAllRight();
				}
				else if (keyb.leftCtrlPressed)
				{
					if (!editor.ui.samplerScreenShown)
					{
						editor.playMode = PLAY_MODE_PATTERN;
						modPlay(modEntry->currPattern, DONT_SET_ORDER, editor.f6Pos);

						editor.currMode = MODE_RECORD;
						pointerSetMode(POINTER_MODE_EDIT, DO_CARRY);
						statusAllRight();
					}
				}
				else if (keyb.leftAmigaPressed)
				{
					editor.playMode = PLAY_MODE_NORMAL;
					modPlay(DONT_SET_PATTERN, modEntry->currOrder, editor.f6Pos);

					editor.currMode = MODE_PLAY;
					pointerSetMode(POINTER_MODE_PLAY, DO_CARRY);
					statusAllRight();
				}
				else if (editor.currMode != MODE_PLAY && editor.currMode != MODE_RECORD)
				{
					modSetPos(DONT_SET_ORDER, editor.f6Pos);
				}
			}
		}
		break;

		case SDL_SCANCODE_F7:
		{
			if (keyb.shiftPressed)
			{
				editor.f7Pos = modEntry->currRow;
				displayMsg("POSITION SET");
			}
			else
			{
				if (keyb.leftAltPressed)
				{
					editor.playMode = PLAY_MODE_PATTERN;
					modPlay(modEntry->currPattern, DONT_SET_ORDER, editor.f7Pos);

					editor.currMode = MODE_PLAY;
					pointerSetMode(POINTER_MODE_PLAY, DO_CARRY);
					statusAllRight();
				}
				else if (keyb.leftCtrlPressed)
				{
					if (!editor.ui.samplerScreenShown)
					{
						editor.playMode = PLAY_MODE_PATTERN;
						modPlay(modEntry->currPattern, DONT_SET_ORDER, editor.f7Pos);

						editor.currMode = MODE_RECORD;
						pointerSetMode(POINTER_MODE_EDIT, DO_CARRY);
						statusAllRight();
					}
				}
				else if (keyb.leftAmigaPressed)
				{
					editor.playMode = PLAY_MODE_NORMAL;
					modPlay(DONT_SET_PATTERN, modEntry->currOrder, editor.f7Pos);

					editor.currMode = MODE_PLAY;
					pointerSetMode(POINTER_MODE_PLAY, DO_CARRY);
					statusAllRight();
				}
				else if (editor.currMode != MODE_PLAY && editor.currMode != MODE_RECORD)
				{
					modSetPos(DONT_SET_ORDER, editor.f7Pos);
				}
			}
		}
		break;

		case SDL_SCANCODE_F8:
		{
			if (keyb.shiftPressed)
			{
				editor.f8Pos = modEntry->currRow;
				displayMsg("POSITION SET");
			}
			else
			{
				if (keyb.leftAltPressed)
				{
					editor.playMode = PLAY_MODE_PATTERN;
					modPlay(modEntry->currPattern, DONT_SET_ORDER, editor.f8Pos);

					editor.currMode = MODE_PLAY;
					pointerSetMode(POINTER_MODE_PLAY, DO_CARRY);
					statusAllRight();
				}
				else if (keyb.leftCtrlPressed)
				{
					if (!editor.ui.samplerScreenShown)
					{
						editor.playMode = PLAY_MODE_PATTERN;
						modPlay(modEntry->currPattern, DONT_SET_ORDER, editor.f8Pos);

						editor.currMode = MODE_RECORD;
						pointerSetMode(POINTER_MODE_EDIT, DO_CARRY);
						statusAllRight();
					}
				}
				else if (keyb.leftAmigaPressed)
				{
					editor.playMode = PLAY_MODE_NORMAL;
					modPlay(DONT_SET_PATTERN, modEntry->currOrder, editor.f8Pos);

					editor.currMode = MODE_PLAY;
					pointerSetMode(POINTER_MODE_PLAY, DO_CARRY);
					statusAllRight();
				}
				else if (editor.currMode != MODE_PLAY && editor.currMode != MODE_RECORD)
				{
					modSetPos(DONT_SET_ORDER, editor.f8Pos);
				}
			}
		}
		break;

		case SDL_SCANCODE_F9:
		{
			if (keyb.shiftPressed)
			{
				editor.f9Pos = modEntry->currRow;
				displayMsg("POSITION SET");
			}
			else
			{
				if (keyb.leftAltPressed)
				{
					editor.playMode = PLAY_MODE_PATTERN;
					modPlay(modEntry->currPattern, DONT_SET_ORDER, editor.f9Pos);

					editor.currMode = MODE_PLAY;
					pointerSetMode(POINTER_MODE_PLAY, DO_CARRY);
					statusAllRight();
				}
				else if (keyb.leftCtrlPressed)
				{
					if (!editor.ui.samplerScreenShown)
					{
						editor.playMode = PLAY_MODE_PATTERN;
						modPlay(modEntry->currPattern, DONT_SET_ORDER, editor.f9Pos);

						editor.currMode = MODE_RECORD;
						pointerSetMode(POINTER_MODE_EDIT, DO_CARRY);
						statusAllRight();
					}
				}
				else if (keyb.leftAmigaPressed)
				{
					editor.playMode = PLAY_MODE_NORMAL;
					modPlay(DONT_SET_PATTERN, modEntry->currOrder, editor.f9Pos);

					editor.currMode = MODE_PLAY;
					pointerSetMode(POINTER_MODE_PLAY, DO_CARRY);
					statusAllRight();
				}
				else if (editor.currMode != MODE_PLAY && editor.currMode != MODE_RECORD)
				{
					modSetPos(DONT_SET_ORDER, editor.f9Pos);
				}
			}
		}
		break;

		case SDL_SCANCODE_F10:
		{
			if (keyb.shiftPressed)
			{
				editor.f10Pos = modEntry->currRow;
				displayMsg("POSITION SET");
			}
			else
			{
				if (keyb.leftAltPressed)
				{
					editor.playMode = PLAY_MODE_PATTERN;
					modPlay(modEntry->currPattern, DONT_SET_ORDER, editor.f10Pos);

					editor.currMode = MODE_PLAY;
					pointerSetMode(POINTER_MODE_PLAY, DO_CARRY);
					statusAllRight();
				}
				else if (keyb.leftCtrlPressed)
				{
					if (!editor.ui.samplerScreenShown)
					{
						editor.playMode = PLAY_MODE_PATTERN;
						modPlay(modEntry->currPattern, DONT_SET_ORDER, editor.f10Pos);

						editor.currMode = MODE_RECORD;
						pointerSetMode(POINTER_MODE_EDIT, DO_CARRY);
						statusAllRight();
					}
				}
				else if (keyb.leftAmigaPressed)
				{
					editor.playMode = PLAY_MODE_NORMAL;
					modPlay(DONT_SET_PATTERN, modEntry->currOrder, editor.f10Pos);

					editor.currMode = MODE_PLAY;
					pointerSetMode(POINTER_MODE_PLAY, DO_CARRY);
					statusAllRight();
				}
				else if (editor.currMode != MODE_PLAY && editor.currMode != MODE_RECORD)
				{
					modSetPos(DONT_SET_ORDER, editor.f10Pos);
				}
			}
		}
		break;

		case SDL_SCANCODE_F11:
		{
			if (keyb.leftAltPressed)
			{
				config.realVuMeters ^= 1;
				displayMsg(config.realVuMeters ? "VU-METERS: REAL" : "VU-METERS: FAKE");
			}
		}
		break;

		case SDL_SCANCODE_TAB:
		{
			if (keyb.shiftPressed)
				movePatCurPrevCh();
			else
				movePatCurNextCh();
		}
		break;

		case SDL_SCANCODE_0:
		{
			if (keyb.leftCtrlPressed)
			{
				editor.editMoveAdd = 0;
				displayMsg("EDITSKIP = 0");
				editor.ui.updateTrackerFlags = true;
			}
			else if (keyb.shiftPressed)
			{
				noteSrc = &modEntry->patterns[modEntry->currPattern][(modEntry->currRow * AMIGA_VOICES) + editor.cursor.channel];
				editor.effectMacros[9] = (noteSrc->command << 8) | noteSrc->param;
				displayMsg("COMMAND STORED!");
			}
			else
			{
				handleEditKeys(scancode, EDIT_NORMAL);
			}
		}
		break;

		case SDL_SCANCODE_1:
		{
			if (keyb.leftAmigaPressed)
			{
				trackNoteUp(TRANSPOSE_ALL, 0, MOD_ROWS - 1);
			}
			else if (keyb.leftCtrlPressed)
			{
				editor.editMoveAdd = 1;
				displayMsg("EDITSKIP = 1");
				editor.ui.updateTrackerFlags = true;
			}
			else if (keyb.shiftPressed)
			{
				noteSrc = &modEntry->patterns[modEntry->currPattern][(modEntry->currRow * AMIGA_VOICES) + editor.cursor.channel];
				editor.effectMacros[0] = (noteSrc->command << 8) | noteSrc->param;
				displayMsg("COMMAND STORED!");
			}
			else if (editor.currMode == MODE_IDLE && keyb.leftAltPressed)
			{
				incMulti(0);
			}
			else
			{
				handleEditKeys(scancode, EDIT_NORMAL);
			}
		}
		break;

		case SDL_SCANCODE_2:
		{
			if (keyb.leftAmigaPressed)
			{
				pattNoteUp(TRANSPOSE_ALL);
			}
			else if (keyb.leftCtrlPressed)
			{
				editor.editMoveAdd = 2;
				displayMsg("EDITSKIP = 2");
				editor.ui.updateTrackerFlags = true;
			}
			else if (keyb.shiftPressed)
			{
				noteSrc = &modEntry->patterns[modEntry->currPattern][(modEntry->currRow * AMIGA_VOICES) + editor.cursor.channel];
				editor.effectMacros[1] = (noteSrc->command << 8) | noteSrc->param;
				displayMsg("COMMAND STORED!");
			}
			else if (editor.currMode == MODE_IDLE && keyb.leftAltPressed)
			{
				incMulti(1);
			}
			else
			{
				handleEditKeys(scancode, EDIT_NORMAL);
			}
		}
		break;

		case SDL_SCANCODE_3:
		{
			if (keyb.leftAmigaPressed)
			{
				trackNoteUp(TRANSPOSE_ALL, 0, MOD_ROWS - 1);
			}
			else if (keyb.leftCtrlPressed)
			{
				editor.editMoveAdd = 3;
				displayMsg("EDITSKIP = 3");
				editor.ui.updateTrackerFlags = true;
			}
			else if (keyb.shiftPressed)
			{
				noteSrc = &modEntry->patterns[modEntry->currPattern][(modEntry->currRow * AMIGA_VOICES) + editor.cursor.channel];
				editor.effectMacros[2] = (noteSrc->command << 8) | noteSrc->param;
				displayMsg("COMMAND STORED!");
			}
			else if (editor.currMode == MODE_IDLE && keyb.leftAltPressed)
			{
				incMulti(2);
			}
			else
			{
				handleEditKeys(scancode, EDIT_NORMAL);
			}
		}
		break;

		case SDL_SCANCODE_4:
		{
			if (keyb.leftAmigaPressed)
			{
				pattNoteUp(TRANSPOSE_ALL);
			}
			else if (keyb.leftCtrlPressed)
			{
				editor.editMoveAdd = 4;
				displayMsg("EDITSKIP = 4");
				editor.ui.updateTrackerFlags = true;
			}
			else if (keyb.shiftPressed)
			{
				noteSrc = &modEntry->patterns[modEntry->currPattern][(modEntry->currRow * AMIGA_VOICES) + editor.cursor.channel];
				editor.effectMacros[3] = (noteSrc->command << 8) | noteSrc->param;
				displayMsg("COMMAND STORED!");
			}
			else if (editor.currMode == MODE_IDLE && keyb.leftAltPressed)
			{
				incMulti(3);
			}
			else
			{
				handleEditKeys(scancode, EDIT_NORMAL);
			}
		}
		break;

		case SDL_SCANCODE_5:
		{
			if (keyb.leftCtrlPressed)
			{
				editor.editMoveAdd = 5;
				displayMsg("EDITSKIP = 5");
				editor.ui.updateTrackerFlags = true;
			}
			else if (keyb.shiftPressed)
			{
				noteSrc = &modEntry->patterns[modEntry->currPattern][(modEntry->currRow * AMIGA_VOICES) + editor.cursor.channel];
				editor.effectMacros[4] = (noteSrc->command << 8) | noteSrc->param;
				displayMsg("COMMAND STORED!");
			}
			else
			{
				handleEditKeys(scancode, EDIT_NORMAL);
			}
		}
		break;

		case SDL_SCANCODE_6:
		{
			if (keyb.leftCtrlPressed)
			{
				editor.editMoveAdd = 6;
				displayMsg("EDITSKIP = 6");
				editor.ui.updateTrackerFlags = true;
			}
			else if (keyb.shiftPressed)
			{
				noteSrc = &modEntry->patterns[modEntry->currPattern][(modEntry->currRow * AMIGA_VOICES) + editor.cursor.channel];
				editor.effectMacros[5] = (noteSrc->command << 8) | noteSrc->param;
				displayMsg("COMMAND STORED!");
			}
			else
			{
				handleEditKeys(scancode, EDIT_NORMAL);
			}
		}
		break;

		case SDL_SCANCODE_7:
		{
			if (keyb.leftCtrlPressed)
			{
				editor.editMoveAdd = 7;
				displayMsg("EDITSKIP = 7");
				editor.ui.updateTrackerFlags = true;
			}
			else if (keyb.shiftPressed)
			{
				noteSrc = &modEntry->patterns[modEntry->currPattern][(modEntry->currRow * AMIGA_VOICES) + editor.cursor.channel];
				editor.effectMacros[6] = (noteSrc->command << 8) | noteSrc->param;
				displayMsg("COMMAND STORED!");
			}
			else
			{
				handleEditKeys(scancode, EDIT_NORMAL);
			}
		}
		break;

		case SDL_SCANCODE_8:
		{
			if (keyb.leftCtrlPressed)
			{
				editor.editMoveAdd = 8;
				displayMsg("EDITSKIP = 8");
				editor.ui.updateTrackerFlags = true;
			}
			else if (keyb.shiftPressed)
			{
				noteSrc = &modEntry->patterns[modEntry->currPattern][(modEntry->currRow * AMIGA_VOICES) + editor.cursor.channel];
				editor.effectMacros[7] = (noteSrc->command << 8) | noteSrc->param;
				displayMsg("COMMAND STORED!");
			}
			else
			{
				handleEditKeys(scancode, EDIT_NORMAL);
			}
		}
		break;

		case SDL_SCANCODE_9:
		{
			if (keyb.leftCtrlPressed)
			{
				editor.editMoveAdd = 9;
				displayMsg("EDITSKIP = 9");
				editor.ui.updateTrackerFlags = true;
			}
			else if (keyb.shiftPressed)
			{
				noteSrc = &modEntry->patterns[modEntry->currPattern][(modEntry->currRow * AMIGA_VOICES) + editor.cursor.channel];
				editor.effectMacros[8] = (noteSrc->command << 8) | noteSrc->param;
				displayMsg("COMMAND STORED!");
			}
			else
			{
				handleEditKeys(scancode, EDIT_NORMAL);
			}
		}
		break;

		case SDL_SCANCODE_KP_0:
		{
			editor.sampleZero = true;
			updateCurrSample();
		}
		break;

		case SDL_SCANCODE_KP_1:
		{
			editor.sampleZero = false;
			editor.currSample = editor.keypadSampleOffset + 12;

			updateCurrSample();
			if (keyb.leftAltPressed && editor.pNoteFlag > 0)
			{
				editor.ui.changingDrumPadNote = true;
				setStatusMessage("SELECT NOTE", NO_CARRY);
				pointerSetMode(POINTER_MODE_MSG1, NO_CARRY);
				break;
			}

			if (editor.pNoteFlag > 0)
				handleEditKeys(scancode, EDIT_SPECIAL);
		}
		break;

		case SDL_SCANCODE_KP_2:
		{
			editor.sampleZero = false;
			editor.currSample = editor.keypadSampleOffset + 13;

			updateCurrSample();
			if (keyb.leftAltPressed && editor.pNoteFlag > 0)
			{
				editor.ui.changingDrumPadNote = true;
				setStatusMessage("SELECT NOTE", NO_CARRY);
				pointerSetMode(POINTER_MODE_MSG1, NO_CARRY);
				break;
			}

			if (editor.pNoteFlag > 0)
				handleEditKeys(scancode, EDIT_SPECIAL);
		}
		break;

		case SDL_SCANCODE_KP_3:
		{
			editor.sampleZero = false;
			editor.currSample = editor.keypadSampleOffset + 14;

			updateCurrSample();
			if (keyb.leftAltPressed && editor.pNoteFlag > 0)
			{
				editor.ui.changingDrumPadNote = true;
				setStatusMessage("SELECT NOTE", NO_CARRY);
				pointerSetMode(POINTER_MODE_MSG1, NO_CARRY);
				break;
			}

			if (editor.pNoteFlag > 0)
				handleEditKeys(scancode, EDIT_SPECIAL);
		}
		break;

		case SDL_SCANCODE_KP_4:
		{
			editor.sampleZero = false;
			editor.currSample = editor.keypadSampleOffset + 8;

			updateCurrSample();
			if (keyb.leftAltPressed && editor.pNoteFlag > 0)
			{
				editor.ui.changingDrumPadNote = true;
				setStatusMessage("SELECT NOTE", NO_CARRY);
				pointerSetMode(POINTER_MODE_MSG1, NO_CARRY);
				break;
			}

			if (editor.pNoteFlag > 0)
				handleEditKeys(scancode, EDIT_SPECIAL);
		}
		break;

		case SDL_SCANCODE_KP_5:
		{
			editor.sampleZero = false;
			editor.currSample = editor.keypadSampleOffset + 9;

			updateCurrSample();
			if (keyb.leftAltPressed && editor.pNoteFlag > 0)
			{
				editor.ui.changingDrumPadNote = true;
				setStatusMessage("SELECT NOTE", NO_CARRY);
				pointerSetMode(POINTER_MODE_MSG1, NO_CARRY);
				break;
			}

			if (editor.pNoteFlag > 0)
				handleEditKeys(scancode, EDIT_SPECIAL);
		}
		break;

		case SDL_SCANCODE_KP_6:
		{
			editor.sampleZero = false;
			editor.currSample = editor.keypadSampleOffset + 10;

			updateCurrSample();
			if (keyb.leftAltPressed && editor.pNoteFlag > 0)
			{
				editor.ui.changingDrumPadNote = true;
				setStatusMessage("SELECT NOTE", NO_CARRY);
				pointerSetMode(POINTER_MODE_MSG1, NO_CARRY);
				break;
			}

			if (editor.pNoteFlag > 0)
				handleEditKeys(scancode, EDIT_SPECIAL);
		}
		break;

		case SDL_SCANCODE_KP_7:
		{
			editor.sampleZero = false;
			editor.currSample = editor.keypadSampleOffset + 4;

			updateCurrSample();
			if (keyb.leftAltPressed && editor.pNoteFlag > 0)
			{
				editor.ui.changingDrumPadNote = true;
				setStatusMessage("SELECT NOTE", NO_CARRY);
				pointerSetMode(POINTER_MODE_MSG1, NO_CARRY);
				break;
			}

			if (editor.pNoteFlag > 0)
				handleEditKeys(scancode, EDIT_SPECIAL);
		}
		break;

		case SDL_SCANCODE_KP_8:
		{
			editor.sampleZero = false;
			editor.currSample = editor.keypadSampleOffset + 5;

			updateCurrSample();
			if (keyb.leftAltPressed && editor.pNoteFlag > 0)
			{
				editor.ui.changingDrumPadNote = true;
				setStatusMessage("SELECT NOTE", NO_CARRY);
				pointerSetMode(POINTER_MODE_MSG1, NO_CARRY);
				break;
			}

			if (editor.pNoteFlag > 0)
				handleEditKeys(scancode, EDIT_SPECIAL);
		}
		break;

		case SDL_SCANCODE_KP_9:
		{
			editor.sampleZero = false;
			editor.currSample = editor.keypadSampleOffset + 6;

			updateCurrSample();
			if (keyb.leftAltPressed && editor.pNoteFlag > 0)
			{
				editor.ui.changingDrumPadNote = true;
				setStatusMessage("SELECT NOTE", NO_CARRY);
				pointerSetMode(POINTER_MODE_MSG1, NO_CARRY);
				break;
			}

			if (editor.pNoteFlag > 0)
				handleEditKeys(scancode, EDIT_SPECIAL);
		}
		break;

		case SDL_SCANCODE_KP_ENTER:
		{
			if (editor.ui.askScreenShown)
			{
				editor.ui.answerNo = false;
				editor.ui.answerYes = true;
				editor.ui.askScreenShown = false;
				handleAskYes();
			}
			else
			{
				editor.sampleZero = false;

				editor.currSample++;
				if (editor.currSample >= 0x10)
				{
					editor.keypadSampleOffset = 0x00;

					editor.currSample -= 0x10;
					if (editor.currSample < 0x01)
						editor.currSample = 0x01;
				}
				else
				{
					editor.currSample += 0x10;
					editor.keypadSampleOffset = 0x10;
				}
				editor.currSample--;

				updateCurrSample();
				if (keyb.leftAltPressed && editor.pNoteFlag > 0)
				{
					editor.ui.changingDrumPadNote = true;
					setStatusMessage("SELECT NOTE", NO_CARRY);
					pointerSetMode(POINTER_MODE_MSG1, NO_CARRY);
					break;
				}

				if (editor.pNoteFlag > 0)
					handleEditKeys(scancode, EDIT_SPECIAL);
			}
		}
		break;

		case SDL_SCANCODE_KP_PLUS:
		{
			editor.sampleZero = false;

			// the Amiga numpad has one more key, so we need to use this key for two sample numbers...
			if (editor.keypadToggle8CFlag)
				editor.currSample = editor.keypadSampleOffset + (0x0C - 1);
			else
				editor.currSample = editor.keypadSampleOffset + (0x08 - 1);

			updateCurrSample();
			if (keyb.leftAltPressed && editor.pNoteFlag > 0)
				displayErrorMsg("INVALID PAD KEY !");

			editor.keypadToggle8CFlag ^= 1;
		}
		break;

		case SDL_SCANCODE_KP_MINUS:
		{
			editor.sampleZero = false;
			editor.currSample = editor.keypadSampleOffset + 3;

			updateCurrSample();
			if (keyb.leftAltPressed && editor.pNoteFlag > 0)
			{
				editor.ui.changingDrumPadNote = true;
				setStatusMessage("SELECT NOTE", NO_CARRY);
				pointerSetMode(POINTER_MODE_MSG1, NO_CARRY);
				break;
			}

			if (editor.pNoteFlag > 0)
				handleEditKeys(scancode, EDIT_SPECIAL);
		}
		break;

		case SDL_SCANCODE_KP_MULTIPLY:
		{
			editor.sampleZero = false;
			editor.currSample = editor.keypadSampleOffset + 2;

			updateCurrSample();
			if (keyb.leftAltPressed && editor.pNoteFlag > 0)
			{
				editor.ui.changingDrumPadNote = true;
				setStatusMessage("SELECT NOTE", NO_CARRY);
				pointerSetMode(POINTER_MODE_MSG1, NO_CARRY);
				break;
			}

			if (editor.pNoteFlag > 0)
				handleEditKeys(scancode, EDIT_SPECIAL);
		}
		break;

		case SDL_SCANCODE_KP_DIVIDE:
		{
			editor.sampleZero = false;
			editor.currSample = editor.keypadSampleOffset + 1;

			updateCurrSample();
			if (keyb.leftAltPressed && editor.pNoteFlag > 0)
			{
				editor.ui.changingDrumPadNote = true;
				setStatusMessage("SELECT NOTE", NO_CARRY);
				pointerSetMode(POINTER_MODE_MSG1, NO_CARRY);
				break;
			}

			if (editor.pNoteFlag > 0)
				handleEditKeys(scancode, EDIT_SPECIAL);
		}
		break;

		case SDL_SCANCODE_NUMLOCKCLEAR:
		{
			editor.sampleZero = false;
			editor.currSample = editor.keypadSampleOffset + 0;

			updateCurrSample();
			if (keyb.leftAltPressed && editor.pNoteFlag > 0)
			{
				editor.ui.changingDrumPadNote = true;

				setStatusMessage("SELECT NOTE", NO_CARRY);
				pointerSetMode(POINTER_MODE_MSG1, NO_CARRY);

				break;
			}

			if (editor.pNoteFlag > 0)
				handleEditKeys(scancode, EDIT_SPECIAL);
		}
		break;

		case SDL_SCANCODE_KP_PERIOD:
		{
			editor.ui.askScreenShown = true;
			editor.ui.askScreenType = ASK_KILL_SAMPLE;
			pointerSetMode(POINTER_MODE_MSG1, NO_CARRY);
			setStatusMessage("KILL SAMPLE ?", NO_CARRY);
			renderAskDialog();
		}
		break;

		case SDL_SCANCODE_DOWN:
		{
			keyb.delayKey = false;
			keyb.repeatKey = false;

			if (editor.ui.diskOpScreenShown)
			{
				if (editor.diskop.numEntries > DISKOP_LINES)
				{
					editor.diskop.scrollOffset++;
					if (mouse.rightButtonPressed) // PT quirk: right mouse button speeds up scrolling even on keyb UP/DOWN
						editor.diskop.scrollOffset += 3;

					if (editor.diskop.scrollOffset > editor.diskop.numEntries-DISKOP_LINES)
						editor.diskop.scrollOffset = editor.diskop.numEntries-DISKOP_LINES;

					editor.ui.updateDiskOpFileList = true;
				}

				if (!keyb.repeatKey)
					keyb.delayCounter = 0;

				keyb.repeatKey = true;
				keyb.delayKey = false;
			}
			else if (editor.ui.posEdScreenShown)
			{
				if (modEntry->currOrder != modEntry->head.orderCount-1)
				{
					if (++modEntry->currOrder > modEntry->head.orderCount-1)
						modEntry->currOrder = modEntry->head.orderCount-1;

					modSetPos(modEntry->currOrder, DONT_SET_ROW);
					editor.ui.updatePosEd = true;
				}

				if (!keyb.repeatKey)
					keyb.delayCounter = 0;

				keyb.repeatKey = true;
				keyb.delayKey = true;
			}
			else if (!editor.ui.samplerScreenShown)
			{
				if (editor.currMode != MODE_PLAY && editor.currMode != MODE_RECORD)
					modSetPos(DONT_SET_ORDER, (modEntry->currRow + 1) & 0x3F);

				keyb.repeatKey = true;
			}
		}
		break;

		case SDL_SCANCODE_UP:
		{
			keyb.delayKey  = false;
			keyb.repeatKey = false;

			if (editor.ui.diskOpScreenShown)
			{
				editor.diskop.scrollOffset--;
				if (mouse.rightButtonPressed) // PT quirk: right mouse button speeds up scrolling even on keyb UP/DOWN
					editor.diskop.scrollOffset -= 3;

				if (editor.diskop.scrollOffset < 0)
					editor.diskop.scrollOffset = 0;

				editor.ui.updateDiskOpFileList = true;

				if (!keyb.repeatKey)
					keyb.delayCounter = 0;

				keyb.repeatKey = true;
				keyb.delayKey = false;
			}
			else if (editor.ui.posEdScreenShown)
			{
				if (modEntry->currOrder > 0)
				{
					modSetPos(modEntry->currOrder - 1, DONT_SET_ROW);
					editor.ui.updatePosEd = true;
				}

				if (!keyb.repeatKey)
					keyb.delayCounter = 0;

				keyb.repeatKey = true;
				keyb.delayKey = true;
			}
			else if (!editor.ui.samplerScreenShown)
			{
				if ((editor.currMode != MODE_PLAY) && (editor.currMode != MODE_RECORD))
					modSetPos(DONT_SET_ORDER, (modEntry->currRow - 1) & 0x3F);

				keyb.repeatKey = true;
			}
		}
		break;

		case SDL_SCANCODE_LEFT:
		{
			keyb.delayKey = false;
			keyb.repeatKey = false;

			if (keyb.leftCtrlPressed)
			{
				sampleDownButton();
				if (editor.repeatKeyFlag)
				{
					keyb.delayKey = true;
					keyb.repeatKey = true;
				}
			}
			else if (keyb.shiftPressed)
			{
				if (modEntry->currOrder > 0)
				{
					modSetPos(modEntry->currOrder - 1, DONT_SET_ROW);
					if (editor.repeatKeyFlag)
					{
						keyb.delayKey = true;
						keyb.repeatKey = true;
					}
				}
			}
			else if (keyb.leftAltPressed)
			{
				decPatt();
				if (editor.repeatKeyFlag)
				{
					keyb.delayKey = true;
					keyb.repeatKey = true;
				}
			}
			else
			{
				movePatCurLeft();
				keyb.repeatKey = true;
			}
		}
		break;

		case SDL_SCANCODE_RIGHT:
		{
			keyb.delayKey = false;
			keyb.repeatKey = false;

			if (keyb.leftCtrlPressed)
			{
				sampleUpButton();
				if (editor.repeatKeyFlag)
				{
					keyb.delayKey = true;
					keyb.repeatKey = true;
				}
			}
			else if (keyb.shiftPressed)
			{
				if (modEntry->currOrder < 126)
				{
					modSetPos(modEntry->currOrder + 1, DONT_SET_ROW);
					if (editor.repeatKeyFlag)
					{
						keyb.delayKey = true;
						keyb.repeatKey = true;
					}
				}
			}
			else if (keyb.leftAltPressed)
			{
				incPatt();
				if (editor.repeatKeyFlag)
				{
					keyb.delayKey = true;
					keyb.repeatKey = true;
				}
			}
			else
			{
				movePatCurRight();
				keyb.repeatKey = true;
			}
		}
		break;

		case SDL_SCANCODE_A:
		{
			if (keyb.leftAmigaPressed)
			{
				trackOctaUp(TRANSPOSE_ALL, 0, MOD_ROWS - 1);
			}
			else if (keyb.leftCtrlPressed)
			{
				if (editor.ui.samplerScreenShown)
				{
					samplerRangeAll();
				}
				else
				{
					if (keyb.shiftPressed)
					{
						editor.muted[0] = true;
						editor.muted[1] = true;
						editor.muted[2] = true;
						editor.muted[3] = true;

						editor.muted[editor.cursor.channel] = false;
						renderMuteButtons();
						break;
					}

					editor.muted[editor.cursor.channel] ^= 1;
					renderMuteButtons();
				}
			}
			else
			{
				handleEditKeys(scancode, EDIT_NORMAL);
			}
		}
		break;

		case SDL_SCANCODE_B:
		{
			if (keyb.leftCtrlPressed)
			{
				// CTRL+B doesn't change the status message back, so do this:
				if (editor.ui.introScreenShown)
				{
					editor.ui.introScreenShown = false;
					statusAllRight();
				}

				if (editor.blockMarkFlag)
				{
					editor.blockMarkFlag = false;
				}
				else
				{
					editor.blockMarkFlag = true;
					editor.blockFromPos = modEntry->currRow;
					editor.blockToPos = modEntry->currRow;
				}

				editor.ui.updateStatusText = true;
			}
			else if (keyb.leftAltPressed)
			{
				s = &modEntry->samples[editor.currSample];
				if (s->length == 0)
				{
					displayErrorMsg("SAMPLE IS EMPTY");
					break;
				}

				boostSample(editor.currSample, true);

				if (editor.ui.samplerScreenShown)
					displaySample();
			}
			else
			{
				handleEditKeys(scancode, EDIT_NORMAL);
			}
		}
		break;

		case SDL_SCANCODE_C:
		{
			if (keyb.leftAmigaPressed)
			{
				trackOctaDown(TRANSPOSE_ALL, 0, MOD_ROWS - 1);
			}
			else if (keyb.leftCtrlPressed)
			{
				if (editor.ui.samplerScreenShown)
				{
					samplerSamCopy();
					return;
				}

				if (!editor.blockMarkFlag)
				{
					displayErrorMsg("NO BLOCK MARKED !");
					return;
				}

				editor.blockMarkFlag = false;
				editor.blockBufferFlag = true;

				for (i = 0; i < MOD_ROWS; i++)
					editor.blockBuffer[i] = modEntry->patterns[modEntry->currPattern][(i * AMIGA_VOICES) + editor.cursor.channel];

				if (editor.blockFromPos > editor.blockToPos)
				{
					editor.buffFromPos = editor.blockToPos;
					editor.buffToPos = editor.blockFromPos;
				}
				else
				{
					editor.buffFromPos = editor.blockFromPos;
					editor.buffToPos = editor.blockToPos;
				}

				statusAllRight();
			}
			else
			{
				if (keyb.leftAltPressed)
				{
					editor.muted[2] ^= 1; // toggle channel 3
					renderMuteButtons();
				}
				else
				{
					handleEditKeys(scancode, EDIT_NORMAL);
				}
			}
		}
		break;

		case SDL_SCANCODE_D:
		{
			if (keyb.leftAmigaPressed)
			{
				trackOctaUp(TRANSPOSE_ALL, 0, MOD_ROWS - 1);
			}
			else if (keyb.leftCtrlPressed)
			{
				saveUndo();
			}
			else
			{
				if (keyb.leftAltPressed)
				{
					if (!editor.ui.posEdScreenShown)
					{
						editor.blockMarkFlag = false;

						editor.ui.diskOpScreenShown ^= 1;
						if (!editor.ui.diskOpScreenShown)
						{
							pointerSetPreviousMode();
							setPrevStatusMessage();
							displayMainScreen();
						}
						else
						{
							editor.ui.diskOpScreenShown = true;
							renderDiskOpScreen();
						}
					}
				}
				else
				{
					handleEditKeys(scancode, EDIT_NORMAL);
				}
			}
		}
		break;

		case SDL_SCANCODE_E:
		{
			if (keyb.leftAmigaPressed)
			{
				trackNoteDown(TRANSPOSE_ALL, 0, MOD_ROWS - 1);
			}
			else if (keyb.leftAltPressed)
			{
				if (!editor.ui.diskOpScreenShown && !editor.ui.posEdScreenShown)
				{
					if (editor.ui.editOpScreenShown)
						editor.ui.editOpScreen = (editor.ui.editOpScreen + 1) % 3;
					else
						editor.ui.editOpScreenShown = true;

					renderEditOpScreen();
				}
			}
			else if (keyb.leftCtrlPressed)
			{
				saveUndo();

				j = modEntry->currRow + 1;
				while (j < MOD_ROWS)
				{
					for (i = 62; i >= j; i--)
					{
						noteSrc = &modEntry->patterns[modEntry->currPattern][(i * AMIGA_VOICES) + editor.cursor.channel];
						modEntry->patterns[modEntry->currPattern][((i + 1) * AMIGA_VOICES) + editor.cursor.channel] = *noteSrc;
					}

					noteDst = &modEntry->patterns[modEntry->currPattern][((i + 1) * AMIGA_VOICES) + editor.cursor.channel];
					noteDst->period = 0;
					noteDst->sample = 0;
					noteDst->command = 0;
					noteDst->param = 0;

					j += 2;
				}

				updateWindowTitle(MOD_IS_MODIFIED);
				editor.ui.updatePatternData = true;
			}
			else
			{
				handleEditKeys(scancode, EDIT_NORMAL);
			}
		}
		break;

		case SDL_SCANCODE_F:
		{
#ifdef __APPLE__
			if (keyb.leftCommandPressed && keyb.leftCtrlPressed)
			{
				toggleFullScreen();
			}
			else
#endif
			if (keyb.leftAmigaPressed)
			{
				pattOctaUp(TRANSPOSE_ALL);
			}
			else if (keyb.leftCtrlPressed)
			{
				toggleLEDFilter();

				if (editor.useLEDFilter)
					displayMsg("LED FILTER ON");
				else
					displayMsg("LED FILTER OFF");
			}
			else if (keyb.leftAltPressed)
			{
				s = &modEntry->samples[editor.currSample];
				if (s->length == 0)
				{
					displayErrorMsg("SAMPLE IS EMPTY");
					break;
				}

				filterSample(editor.currSample, true);
				if (editor.ui.samplerScreenShown)
					displaySample();
			}
			else
			{
				handleEditKeys(scancode, EDIT_NORMAL);
			}
		}
		break;

		case SDL_SCANCODE_G:
		{
			if (keyb.leftCtrlPressed)
			{
				editor.ui.askScreenShown = true;
				editor.ui.askScreenType = ASK_BOOST_ALL_SAMPLES;
				pointerSetMode(POINTER_MODE_MSG1, NO_CARRY);
				setStatusMessage("BOOST ALL SAMPLES", NO_CARRY);
				renderAskDialog();
			}
			else if (keyb.leftAltPressed) // toggle record mode (PT clone and PT2.3E only)
			{
				editor.recordMode ^= 1;
				if (editor.recordMode == 0)
					displayMsg("REC MODE: PATT");
				else
					displayMsg("REC MODE: SONG");

				if (editor.ui.editOpScreenShown && editor.ui.editOpScreen == 1)
					editor.ui.updateRecordText = true;
			}
			else
			{
				handleEditKeys(scancode, EDIT_NORMAL);
			}
		}
		break;

		case SDL_SCANCODE_H:
		{
			if (keyb.leftCtrlPressed)
			{
				if (!editor.blockMarkFlag)
				{
					displayErrorMsg("NO BLOCK MARKED !");
					return;
				}

				trackNoteUp(TRANSPOSE_ALL, editor.blockFromPos, editor.blockToPos);
			}
			else
			{
				handleEditKeys(scancode, EDIT_NORMAL);
			}
		}
		break;

		case SDL_SCANCODE_I:
		{
			if (keyb.leftCtrlPressed)
			{
				if (!editor.blockBufferFlag)
				{
					displayErrorMsg("BUFFER IS EMPTY !");
					return;
				}

				if (modEntry->currRow < 63)
				{
					for (i = 0; i <= editor.buffToPos-editor.buffFromPos; i++)
					{
						for (j = 62; j >= modEntry->currRow; j--)
						{
							noteSrc = &modEntry->patterns[modEntry->currPattern][(j * AMIGA_VOICES) + editor.cursor.channel];
							modEntry->patterns[modEntry->currPattern][((j + 1) * AMIGA_VOICES) + editor.cursor.channel] = *noteSrc;
						}
					}
				}

				saveUndo();
				for (i = 0; i <= editor.buffToPos-editor.buffFromPos; i++)
				{
					if (modEntry->currRow+i > 63)
						break;

					modEntry->patterns[modEntry->currPattern][((modEntry->currRow + i) * AMIGA_VOICES) + editor.cursor.channel]
						= editor.blockBuffer[editor.buffFromPos + i];
				}

				if (!keyb.shiftPressed)
				{
					modEntry->currRow += i & 0xFF;
					if (modEntry->currRow > 63)
						modEntry->currRow = 0;
				}

				updateWindowTitle(MOD_IS_MODIFIED);
				editor.ui.updatePatternData = true;
			}
			else if (keyb.leftAltPressed)
			{
				editor.autoInsFlag ^= 1;
				editor.ui.updateTrackerFlags = true;
			}
			else
			{
				handleEditKeys(scancode, EDIT_NORMAL);
			}
		}
		break;

		case SDL_SCANCODE_J:
		{
			if (keyb.leftCtrlPressed)
			{
				if (!editor.blockBufferFlag)
				{
					displayErrorMsg("BUFFER IS EMPTY !");
					return;
				}

				saveUndo();

				i = editor.buffFromPos;
				j = modEntry->currRow;
				patt = modEntry->patterns[modEntry->currPattern];
				while (true)
				{
					noteDst = &patt[(j * AMIGA_VOICES) + editor.cursor.channel];

					if (editor.blockBuffer[i].period == 0 && editor.blockBuffer[i].sample == 0)
					{
						noteDst->command = editor.blockBuffer[i].command;
						noteDst->param = editor.blockBuffer[i].param;
					}
					else
					{
						*noteDst = editor.blockBuffer[i];
					}

					if (i == editor.buffToPos || i == 63 || j == 63)
						break;

					i++;
					j++;
				}

				if (!keyb.shiftPressed)
				{
					modEntry->currRow += (editor.buffToPos-editor.buffFromPos) + 1;
					if (modEntry->currRow > 63)
						modEntry->currRow = 0;
				}

				updateWindowTitle(MOD_IS_MODIFIED);
				editor.ui.updatePatternData = true;
			}
			else
			{
				handleEditKeys(scancode, EDIT_NORMAL);
			}
		}
		break;

		case SDL_SCANCODE_K:
		{
			if (keyb.leftAltPressed)
			{
				for (i = 0; i < MOD_ROWS; i++)
				{
					noteSrc = &modEntry->patterns[modEntry->currPattern][(i * AMIGA_VOICES) + editor.cursor.channel];
					if (noteSrc->sample == editor.currSample+1)
					{
						noteSrc->period = 0;
						noteSrc->sample = 0;
						noteSrc->command = 0;
						noteSrc->param = 0;
					}
				}

				updateWindowTitle(MOD_IS_MODIFIED);
				editor.ui.updatePatternData = true;
			}
			else if (keyb.leftCtrlPressed)
			{
				saveUndo();

				i = modEntry->currRow;
				if (keyb.shiftPressed)
				{
					// kill to start
					while (i >= 0)
					{
						noteDst = &modEntry->patterns[modEntry->currPattern][(i * AMIGA_VOICES) + editor.cursor.channel];
						noteDst->period = 0;
						noteDst->sample = 0;
						noteDst->command = 0;
						noteDst->param = 0;

						i--;
					}
				}
				else
				{
					// kill to end
					while (i < MOD_ROWS)
					{
						noteDst = &modEntry->patterns[modEntry->currPattern][(i * AMIGA_VOICES) + editor.cursor.channel];
						noteDst->period = 0;
						noteDst->sample = 0;
						noteDst->command = 0;
						noteDst->param = 0;

						i++;
					}
				}

				updateWindowTitle(MOD_IS_MODIFIED);
				editor.ui.updatePatternData = true;
			}
			else
			{
				handleEditKeys(scancode, EDIT_NORMAL);
			}
		}
		break;

		case SDL_SCANCODE_L:
		{
			if (keyb.leftCtrlPressed)
			{
				if (!editor.blockMarkFlag)
				{
					displayErrorMsg("NO BLOCK MARKED !");
					return;
				}

				trackNoteDown(TRANSPOSE_ALL, editor.blockFromPos, editor.blockToPos);
			}
			else
			{
				handleEditKeys(scancode, EDIT_NORMAL);
			}
		}
		break;

		case SDL_SCANCODE_M:
		{
			if (keyb.leftCtrlPressed)
			{
				editor.multiFlag ^= 1;
				editor.ui.updateTrackerFlags = true;
				editor.ui.updateKeysText = true;
			}
			else if (keyb.leftAltPressed)
			{
				if (keyb.shiftPressed)
					editor.metroChannel = editor.cursor.channel + 1;
				else
					editor.metroFlag ^= 1;

				editor.ui.updateTrackerFlags = true;
			}
			else
			{
				handleEditKeys(scancode, EDIT_NORMAL);
			}
		}
		break;

		case SDL_SCANCODE_N:
		{
			if (keyb.leftCtrlPressed)
			{
				editor.blockMarkFlag = true;
				modEntry->currRow = editor.blockToPos;
			}
			else
			{
				handleEditKeys(scancode, EDIT_NORMAL);
			}
		}
		break;

		case SDL_SCANCODE_O:
		{
			if (keyb.leftCtrlPressed)
			{
				// fun fact: this function is broken in PT but I fixed it in my clone

				saveUndo();

				j = modEntry->currRow + 1;
				while (j < MOD_ROWS)
				{
					for (i = j; i < MOD_ROWS-1; i++)
					{
						noteSrc = &modEntry->patterns[modEntry->currPattern][((i + 1) * AMIGA_VOICES) + editor.cursor.channel];
						modEntry->patterns[modEntry->currPattern][(i * AMIGA_VOICES) + editor.cursor.channel] = *noteSrc;
					}

					// clear newly made row on very bottom
					noteDst = &modEntry->patterns[modEntry->currPattern][(63 * AMIGA_VOICES) + editor.cursor.channel];
					noteDst->period = 0;
					noteDst->sample = 0;
					noteDst->command = 0;
					noteDst->param = 0;

					j++;
				}

				updateWindowTitle(MOD_IS_MODIFIED);
				editor.ui.updatePatternData = true;
			}
			else
			{
				handleEditKeys(scancode, EDIT_NORMAL);
			}
		}
		break;

		case SDL_SCANCODE_P:
		{
			if (keyb.leftCtrlPressed)
			{
				if (!editor.blockBufferFlag)
				{
					displayErrorMsg("BUFFER IS EMPTY !");
					return;
				}

				saveUndo();

				i = editor.buffFromPos;
				j = modEntry->currRow;
				patt = modEntry->patterns[modEntry->currPattern];
				while (true)
				{
					noteDst = &patt[(j * AMIGA_VOICES) + editor.cursor.channel];
					*noteDst = editor.blockBuffer[i];

					if (i == editor.buffToPos || i == 63 || j == 63)
						break;

					i++;
					j++;
				}

				if (!keyb.shiftPressed)
				{
					modEntry->currRow += (editor.buffToPos-editor.buffFromPos) + 1;
					if (modEntry->currRow > 63)
						modEntry->currRow = 0;
				}

				updateWindowTitle(MOD_IS_MODIFIED);
				editor.ui.updatePatternData = true;
			}
			else if (keyb.leftAltPressed)
			{
				if (!editor.ui.diskOpScreenShown)
				{
					editor.ui.posEdScreenShown ^= 1;
					if (editor.ui.posEdScreenShown)
					{
						renderPosEdScreen();
						editor.ui.updatePosEd = true;
					}
					else
					{
						displayMainScreen();
					}
				}
			}
			else
			{
				handleEditKeys(scancode, EDIT_NORMAL);
			}
		}
		break;

		case SDL_SCANCODE_Q:
		{
			if (keyb.leftAmigaPressed)
			{
				trackNoteDown(TRANSPOSE_ALL, 0, MOD_ROWS - 1);
			}
			else if (keyb.leftCtrlPressed)
			{
				editor.muted[0] = false;
				editor.muted[1] = false;
				editor.muted[2] = false;
				editor.muted[3] = false;
				renderMuteButtons();
			}
			else if (keyb.leftAltPressed)
			{
				editor.ui.askScreenShown = true;
				editor.ui.askScreenType = ASK_QUIT;

				pointerSetMode(POINTER_MODE_MSG1, NO_CARRY);
				setStatusMessage("REALLY QUIT ?", NO_CARRY);
				renderAskDialog();
			}
			else
			{
				handleEditKeys(scancode, EDIT_NORMAL);
			}
		}
		break;

		case SDL_SCANCODE_R:
		{
			if (keyb.leftAmigaPressed)
			{
				pattNoteDown(TRANSPOSE_ALL);
			}
			else if (keyb.leftCtrlPressed)
			{
				editor.f6Pos = 0;
				editor.f7Pos = 16;
				editor.f8Pos = 32;
				editor.f9Pos = 48;
				editor.f10Pos = 63;

				displayMsg("POS RESTORED !");
			}
			else if (keyb.leftAltPressed)
			{
				editor.ui.askScreenShown = true;
				editor.ui.askScreenType = ASK_RESAMPLE;
				pointerSetMode(POINTER_MODE_MSG1, NO_CARRY);
				setStatusMessage("RESAMPLE?", NO_CARRY);
				renderAskDialog();
			}
			else
			{
				handleEditKeys(scancode, EDIT_NORMAL);
			}
		}
		break;

		case SDL_SCANCODE_S:
		{
			if (keyb.leftCtrlPressed)
			{
				// if we're in sample load/save mode, set current dir to modules path
				if (editor.diskop.mode == DISKOP_MODE_SMP)
					UNICHAR_CHDIR(editor.modulesPathU);

				saveModule(DONT_CHECK_IF_FILE_EXIST, DONT_GIVE_NEW_FILENAME);

				// set current dir to samples path
				if (editor.diskop.mode == DISKOP_MODE_SMP)
					UNICHAR_CHDIR(editor.samplesPathU);
			}
			else if (keyb.leftAmigaPressed)
			{
				pattOctaUp(TRANSPOSE_ALL);
			}
			else if (keyb.leftAltPressed)
			{
				samplerScreen();
			}
			else
			{
				handleEditKeys(scancode, EDIT_NORMAL);
			}
		}
		break;

		case SDL_SCANCODE_T:
		{
			if (keyb.leftCtrlPressed)
			{
				editor.swapChannelFlag = true;
				pointerSetMode(POINTER_MODE_MSG1, NO_CARRY);
				setStatusMessage("SWAP (1/2/3/4) ?", NO_CARRY);
			}
			else if (keyb.leftAltPressed)
			{
				toggleTuningTone();
			}
			else
			{
				handleEditKeys(scancode, EDIT_NORMAL);
			}
		}
		break;

		case SDL_SCANCODE_U:
		{
			if (keyb.leftCtrlPressed)
				undoLastChange();
			else
				handleEditKeys(scancode, EDIT_NORMAL);
		}
		break;

		case SDL_SCANCODE_V:
		{
			if (keyb.leftAmigaPressed)
			{
				pattOctaDown(TRANSPOSE_ALL);
			}
			else if (keyb.leftCtrlPressed)
			{
				if (editor.ui.samplerScreenShown)
				{
					samplerSamPaste();
				}
				else
				{
					editor.ui.askScreenShown = true;
					editor.ui.askScreenType = ASK_FILTER_ALL_SAMPLES;

					pointerSetMode(POINTER_MODE_MSG1, NO_CARRY);
					setStatusMessage("FILTER ALL SAMPLS", NO_CARRY);
					renderAskDialog();
				}
			}
			else if (keyb.leftAltPressed)
			{
				editor.muted[3] ^= 1; // toggle channel 4
				renderMuteButtons();
			}
			else
			{
				handleEditKeys(scancode, EDIT_NORMAL);
			}
		}
		break;

		case SDL_SCANCODE_W:
		{
			if (keyb.leftAmigaPressed)
			{
				pattNoteDown(TRANSPOSE_ALL);
			}
			else if (keyb.leftCtrlPressed)
			{
				// Polyphonize Block
				if (!editor.blockBufferFlag)
				{
					displayErrorMsg("BUFFER IS EMPTY !");
					return;
				}

				saveUndo();

				i = editor.buffFromPos;
				j = modEntry->currRow;
				patt = modEntry->patterns[modEntry->currPattern];
				while (true)
				{
					noteDst = &patt[(j * AMIGA_VOICES) + editor.cursor.channel];
					if (editor.blockBuffer[i].period == 0 && editor.blockBuffer[i].sample == 0)
					{
						noteDst->command = editor.blockBuffer[i].command;
						noteDst->param = editor.blockBuffer[i].param;
					}
					else
					{
						*noteDst = editor.blockBuffer[i];
					}

					if (i == editor.buffToPos || i == 63 || j == 63)
						break;

					i++;
					j++;
					gotoNextMulti();
				}

				if (!keyb.shiftPressed)
				{
					modEntry->currRow += (editor.buffToPos-editor.buffFromPos) + 1;
					if (modEntry->currRow > 63)
						modEntry->currRow = 0;
				}

				updateWindowTitle(MOD_IS_MODIFIED);
				editor.ui.updatePatternData = true;
			}
			else
			{
				handleEditKeys(scancode, EDIT_NORMAL);
			}
		}
		break;

		case SDL_SCANCODE_X:
		{
			if (keyb.leftAmigaPressed)
			{
				pattOctaDown(TRANSPOSE_ALL);
			}
			else if (keyb.leftCtrlPressed)
			{
				if (editor.ui.samplerScreenShown)
				{
					samplerSamDelete(SAMPLE_CUT);
					return;
				}

				if (!editor.blockMarkFlag)
				{
					displayErrorMsg("NO BLOCK MARKED !");
					return;
				}

				editor.blockMarkFlag = false;
				saveUndo();
				editor.blockBufferFlag = true;

				for (i = 0; i < MOD_ROWS; i++)
					editor.blockBuffer[i] = modEntry->patterns[modEntry->currPattern][(i * AMIGA_VOICES) + editor.cursor.channel];

				if (editor.blockFromPos > editor.blockToPos)
				{
					editor.buffFromPos = editor.blockToPos;
					editor.buffToPos = editor.blockFromPos;
				}
				else
				{
					editor.buffFromPos = editor.blockFromPos;
					editor.buffToPos = editor.blockToPos;
				}

				for (i = editor.buffFromPos; i <= editor.buffToPos; i++)
				{
					noteDst = &modEntry->patterns[modEntry->currPattern][(i * AMIGA_VOICES) + editor.cursor.channel];
					noteDst->period = 0;
					noteDst->sample = 0;
					noteDst->command = 0;
					noteDst->param = 0;
				}

				statusAllRight();
				updateWindowTitle(MOD_IS_MODIFIED);
				editor.ui.updatePatternData = true;
			}
			else
			{
				if (keyb.leftAltPressed)
				{
					editor.muted[1] ^= 1; // toggle channel 2
					renderMuteButtons();
				}
				else
				{
					handleEditKeys(scancode, EDIT_NORMAL);
				}
			}
		}
		break;

		case SDL_SCANCODE_Y:
		{
			if (keyb.leftCtrlPressed)
			{
				if (!editor.blockMarkFlag)
				{
					displayErrorMsg("NO BLOCK MARKED !");
					return;
				}

				editor.blockMarkFlag = false;

				saveUndo();

				if (editor.blockFromPos >= editor.blockToPos)
				{
					blockFrom = editor.blockToPos;
					blockTo = editor.blockFromPos;
				}
				else
				{
					blockFrom = editor.blockFromPos;
					blockTo = editor.blockToPos;
				}

				while (blockFrom < blockTo)
				{
					noteDst = &modEntry->patterns[modEntry->currPattern][(blockFrom * AMIGA_VOICES) + editor.cursor.channel];
					noteSrc = &modEntry->patterns[modEntry->currPattern][(blockTo * AMIGA_VOICES) + editor.cursor.channel];

					noteTmp = *noteDst;
					*noteDst = *noteSrc;
					*noteSrc = noteTmp;

					blockFrom += 1;
					blockTo -= 1;
				}

				statusAllRight();
				updateWindowTitle(MOD_IS_MODIFIED);
				editor.ui.updatePatternData = true;
			}
			else if (keyb.leftAltPressed)
			{
				editor.ui.askScreenShown = true;
				editor.ui.askScreenType = ASK_SAVE_ALL_SAMPLES;
				pointerSetMode(POINTER_MODE_MSG1, NO_CARRY);
				setStatusMessage("SAVE ALL SAMPLES?", NO_CARRY);
				renderAskDialog();
			}
			else
			{
				handleEditKeys(scancode, EDIT_NORMAL);
			}
		}
		break;

		case SDL_SCANCODE_Z:
		{
			if (keyb.leftAmigaPressed)
			{
				trackOctaDown(TRANSPOSE_ALL, 0, MOD_ROWS - 1);
			}
			else if (keyb.leftCtrlPressed)
			{
				if (editor.ui.samplerScreenShown)
				{
					editor.ui.askScreenShown = true;
					editor.ui.askScreenType = ASK_RESTORE_SAMPLE;

					pointerSetMode(POINTER_MODE_MSG1, NO_CARRY);
					setStatusMessage("RESTORE SAMPLE ?", NO_CARRY);
					renderAskDialog();
				}
				else
				{
					modSetTempo(125);
					modSetSpeed(6);

					for (i = 0; i < AMIGA_VOICES; i++)
					{
						ch = &modEntry->channels[i];
						ch->n_wavecontrol = 0;
						ch->n_glissfunk = 0;
						ch->n_finetune = 0;
						ch->n_loopcount = 0;
					}

					displayMsg("EFX RESTORED !");
				}
			}
			else if (keyb.leftAltPressed)
			{
				editor.muted[0] ^= 1; // toggle channel 1
				renderMuteButtons();
			}
			else
			{
				handleEditKeys(scancode, EDIT_NORMAL);
			}
		}
		break;

		default:
			handleEditKeys(scancode, EDIT_NORMAL);
		break;
	}
}

void movePatCurPrevCh(void)
{
	int8_t pos = ((editor.cursor.pos + 5) / 6) - 1;

	editor.cursor.pos = (pos < 0) ? (3 * 6) : (pos * 6);
	editor.cursor.mode = CURSOR_NOTE;

	     if (editor.cursor.pos <  6) editor.cursor.channel = 0;
	else if (editor.cursor.pos < 12) editor.cursor.channel = 1;
	else if (editor.cursor.pos < 18) editor.cursor.channel = 2;
	else if (editor.cursor.pos < 24) editor.cursor.channel = 3;

	updateCursorPos();
}

void movePatCurNextCh(void)
{
	int8_t pos = (editor.cursor.pos / 6) + 1;

	editor.cursor.pos = (pos == 4) ? 0 : (pos * 6);
	editor.cursor.mode = CURSOR_NOTE;

	     if (editor.cursor.pos <  6) editor.cursor.channel = 0;
	else if (editor.cursor.pos < 12) editor.cursor.channel = 1;
	else if (editor.cursor.pos < 18) editor.cursor.channel = 2;
	else if (editor.cursor.pos < 24) editor.cursor.channel = 3;

	updateCursorPos();
}

void movePatCurRight(void)
{
	editor.cursor.pos = (editor.cursor.pos == 23) ? 0 : (editor.cursor.pos + 1);

	     if (editor.cursor.pos <  6) editor.cursor.channel = 0;
	else if (editor.cursor.pos < 12) editor.cursor.channel = 1;
	else if (editor.cursor.pos < 18) editor.cursor.channel = 2;
	else if (editor.cursor.pos < 24) editor.cursor.channel = 3;

	editor.cursor.mode = editor.cursor.pos % 6;
	updateCursorPos();
}

void movePatCurLeft(void)
{
	editor.cursor.pos = (editor.cursor.pos == 0) ? 23 : (editor.cursor.pos - 1);

	     if (editor.cursor.pos <  6) editor.cursor.channel = 0;
	else if (editor.cursor.pos < 12) editor.cursor.channel = 1;
	else if (editor.cursor.pos < 18) editor.cursor.channel = 2;
	else if (editor.cursor.pos < 24) editor.cursor.channel = 3;

	editor.cursor.mode = editor.cursor.pos % 6;
	updateCursorPos();
}

void handleKeyRepeat(SDL_Scancode scancode)
{
	uint8_t repeatNum;

	if (!keyb.repeatKey || (editor.ui.clearScreenShown || editor.ui.askScreenShown))
	{
		keyb.repeatFrac = 0;
		keyb.repeatCounter = 0;
		return;
	}

	if (keyb.delayKey && keyb.delayCounter < KEYB_REPEAT_DELAY)
	{
		keyb.delayCounter++;
		return;
	}

	switch (scancode) // only some buttons have repeat
	{
		case SDL_SCANCODE_PAGEUP:
		{
			if (keyb.repeatCounter >= 3)
			{
				keyb.repeatCounter = 0;

				if (editor.ui.posEdScreenShown)
				{
					if (modEntry->currOrder-(POSED_LIST_SIZE-1) > 0)
						modSetPos(modEntry->currOrder-(POSED_LIST_SIZE-1), DONT_SET_ROW);
					else
						modSetPos(0, DONT_SET_ROW);
				}
				else if (editor.ui.diskOpScreenShown)
				{
					if (editor.ui.diskOpScreenShown)
					{
						editor.diskop.scrollOffset -= DISKOP_LINES-1;
						if (editor.diskop.scrollOffset < 0)
							editor.diskop.scrollOffset = 0;

						editor.ui.updateDiskOpFileList = true;
					}
				}
				else if (editor.currMode == MODE_IDLE || editor.currMode == MODE_EDIT)
				{
					if (modEntry->currRow == 63)
						modSetPos(DONT_SET_ORDER, modEntry->currRow - 15);
					else if (modEntry->currRow == 15)
						modSetPos(DONT_SET_ORDER, 0); // 15-16 would turn into -1, which is "DON'T SET ROW" flag
					else
						modSetPos(DONT_SET_ORDER, modEntry->currRow - 16);
				}
			}
		}
		break;

		case SDL_SCANCODE_PAGEDOWN:
		{
			if (keyb.repeatCounter >= 3)
			{
				keyb.repeatCounter = 0;

				if (editor.ui.posEdScreenShown)
				{
					if (modEntry->currOrder+(POSED_LIST_SIZE-1) <= modEntry->head.orderCount-1)
						modSetPos(modEntry->currOrder+(POSED_LIST_SIZE-1), DONT_SET_ROW);
					else
						modSetPos(modEntry->head.orderCount - 1, DONT_SET_ROW);
				}
				else if (editor.ui.diskOpScreenShown)
				{
					if (editor.diskop.numEntries > DISKOP_LINES)
					{
						editor.diskop.scrollOffset += DISKOP_LINES-1;
						if (editor.diskop.scrollOffset > editor.diskop.numEntries-DISKOP_LINES)
							editor.diskop.scrollOffset = editor.diskop.numEntries-DISKOP_LINES;

						editor.ui.updateDiskOpFileList = true;
					}
				}
				else if (editor.currMode == MODE_IDLE || editor.currMode == MODE_EDIT)
				{
					modSetPos(DONT_SET_ORDER, modEntry->currRow + 16);
				}
			}
		}
		break;

		case SDL_SCANCODE_LEFT:
		{
			if (editor.ui.editTextFlag)
			{
				if (keyb.repeatCounter >= 4)
				{
					keyb.repeatCounter = 0;
					textCharPrevious();
				}
			}
			else
			{
				if (keyb.leftCtrlPressed)
				{
					if (keyb.repeatCounter >= 6)
					{
						keyb.repeatCounter = 0;
						sampleDownButton();
					}
				}
				else if (keyb.shiftPressed)
				{
					if (keyb.repeatCounter >= 6)
					{
						keyb.repeatCounter = 0;
						if (modEntry->currOrder > 0)
							modSetPos(modEntry->currOrder - 1, DONT_SET_ROW);
					}
				}
				else if (keyb.leftAltPressed)
				{
					if (keyb.repeatCounter >= 4)
					{
						keyb.repeatCounter = 0;
						decPatt();
					}
				}
				else
				{
					if (keyb.repeatCounter >= 6)
					{
						keyb.repeatCounter = 0;
						if (!keyb.shiftPressed && !keyb.leftAltPressed && !keyb.leftCtrlPressed)
							movePatCurLeft();
					}
				}
			}
		}
		break;

		case SDL_SCANCODE_RIGHT:
		{
			if (editor.ui.editTextFlag)
			{
				if (keyb.repeatCounter >= 4)
				{
					keyb.repeatCounter = 0;
					textCharNext();
				}
			}
			else
			{
				if (keyb.leftCtrlPressed)
				{
					if (keyb.repeatCounter >= 6)
					{
						keyb.repeatCounter = 0;
						sampleUpButton();
					}
				}
				else if (keyb.shiftPressed)
				{
					if (keyb.repeatCounter >= 6)
					{
						keyb.repeatCounter = 0;
						if (modEntry->currOrder < 126)
							modSetPos(modEntry->currOrder + 1, DONT_SET_ROW);
					}
				}
				else if (keyb.leftAltPressed)
				{
					if (keyb.repeatCounter >= 4)
					{
						keyb.repeatCounter = 0;
						incPatt();
					}
				}
				else
				{
					if (keyb.repeatCounter >= 6)
					{
						keyb.repeatCounter = 0;
						if (!keyb.shiftPressed && !keyb.leftAltPressed && !keyb.leftCtrlPressed)
							movePatCurRight();
					}
				}
			}
		}
		break;

		case SDL_SCANCODE_UP:
		{
			if (editor.ui.diskOpScreenShown)
			{
				if (keyb.repeatCounter >= 1)
				{
					keyb.repeatCounter = 0;

					editor.diskop.scrollOffset--;
					if (mouse.rightButtonPressed) // PT quirk: right mouse button speeds up scrolling even on keyb UP/DOWN
						editor.diskop.scrollOffset -= 3;

					if (editor.diskop.scrollOffset < 0)
						editor.diskop.scrollOffset = 0;

					editor.ui.updateDiskOpFileList = true;
				}
			}
			else if (editor.ui.posEdScreenShown)
			{
				if (keyb.repeatCounter >= 3)
				{
					keyb.repeatCounter = 0;
					if (modEntry->currOrder > 0)
					{
						modSetPos(modEntry->currOrder - 1, DONT_SET_ROW);
						editor.ui.updatePosEd = true;
					}
				}
			}
			else if (!editor.ui.samplerScreenShown)
			{
				if (editor.currMode != MODE_PLAY && editor.currMode != MODE_RECORD)
				{
					repeatNum = 6;
					if (keyb.leftAltPressed)
						repeatNum = 1;
					else if (keyb.shiftPressed)
						repeatNum = 3;

					if (keyb.repeatCounter >= repeatNum)
					{
						keyb.repeatCounter = 0;
						modSetPos(DONT_SET_ORDER, (modEntry->currRow - 1) & 0x3F);
					}
				}
			}
		}
		break;

		case SDL_SCANCODE_DOWN:
		{
			if (editor.ui.diskOpScreenShown)
			{
				if (keyb.repeatCounter >= 1)
				{
					keyb.repeatCounter = 0;

					if (editor.diskop.numEntries > DISKOP_LINES)
					{
						editor.diskop.scrollOffset++;
						if (mouse.rightButtonPressed) // PT quirk: right mouse button speeds up scrolling even on keyb UP/DOWN
							editor.diskop.scrollOffset += 3;

						if (editor.diskop.scrollOffset > editor.diskop.numEntries-DISKOP_LINES)
							editor.diskop.scrollOffset = editor.diskop.numEntries-DISKOP_LINES;

						editor.ui.updateDiskOpFileList = true;
					}
				}
			}
			else if (editor.ui.posEdScreenShown)
			{
				if (keyb.repeatCounter >= 3)
				{
					keyb.repeatCounter = 0;

					if (modEntry->currOrder != modEntry->head.orderCount-1)
					{
						if (++modEntry->currOrder > modEntry->head.orderCount-1)
							modEntry->currOrder = modEntry->head.orderCount-1;

						modSetPos(modEntry->currOrder, DONT_SET_ROW);
						editor.ui.updatePosEd = true;
					}
				}
			}
			else if (!editor.ui.samplerScreenShown)
			{
				if (editor.currMode != MODE_PLAY && editor.currMode != MODE_RECORD)
				{
					repeatNum = 6;
					if (keyb.leftAltPressed)
						repeatNum = 1;
					else if (keyb.shiftPressed)
						repeatNum = 3;

					if (keyb.repeatCounter >= repeatNum)
					{
						keyb.repeatCounter = 0;
						modSetPos(DONT_SET_ORDER, (modEntry->currRow + 1) & 0x3F);
					}
				}
			}
		}
		break;

		case SDL_SCANCODE_BACKSPACE:
		{
			if (editor.ui.editTextFlag)
			{
				// only repeat backspace while editing texts
				if (keyb.repeatCounter >= 3)
				{
					keyb.repeatCounter = 0;
					keyDownHandler(scancode, 0);
				}
			}
		}
		break;

		case SDL_SCANCODE_KP_ENTER:
		case SDL_SCANCODE_RETURN:
			break; // do NOT repeat enter!

		default:
		{
			if (keyb.repeatCounter >= 3)
			{
				keyb.repeatCounter = 0;
				keyDownHandler(scancode, 0);
			}
		}
		break;
	}

	// repeat keys at 50Hz rate

	const uint64_t keyRepeatDelta = ((uint64_t)AMIGA_PAL_VBLANK_HZ << 32) / VBLANK_HZ;

	keyb.repeatFrac += keyRepeatDelta; // 32.32 fixed-point counter
	if (keyb.repeatFrac > 0xFFFFFFFF)
	{
		keyb.repeatFrac &= 0xFFFFFFFF;
		keyb.repeatCounter++;
	}
}

bool handleGeneralModes(SDL_Keycode keycode, SDL_Scancode scancode)
{
	int8_t rawKey;
	int16_t i;
	note_t *noteSrc, noteTmp;

	// SAMPLER SCREEN (volume box)
	if (editor.ui.samplerVolBoxShown && !editor.ui.editTextFlag && scancode == SDL_SCANCODE_ESCAPE)
	{
		editor.ui.samplerVolBoxShown = false;
		removeSamplerVolBox();
		return false;
	}

	// SAMPLER SCREEN (filters box)
	if (editor.ui.samplerFiltersBoxShown && !editor.ui.editTextFlag && scancode == SDL_SCANCODE_ESCAPE)
	{
		editor.ui.samplerFiltersBoxShown = false;
		removeSamplerFiltersBox();
		return false;
	}

	// EDIT OP. SCREEN #3
	if (editor.mixFlag && scancode == SDL_SCANCODE_ESCAPE)
	{
		exitGetTextLine(EDIT_TEXT_UPDATE);
		editor.mixFlag = false;
		editor.ui.updateMixText = true;
		return false;
	}

	// EDIT OP. SCREEN #4
	if (editor.ui.changingChordNote)
	{
		if (scancode == SDL_SCANCODE_ESCAPE)
		{
			editor.ui.changingChordNote = false;
			setPrevStatusMessage();
			pointerSetPreviousMode();
			return false;
		}

		if (scancode == SDL_SCANCODE_F1)
			editor.keyOctave = OCTAVE_LOW;
		else if (scancode == SDL_SCANCODE_F2)
			editor.keyOctave = OCTAVE_HIGH;

		rawKey = keyToNote(scancode);
		if (rawKey >= 0)
		{
			if (editor.ui.changingChordNote == 1)
			{
				editor.note1 = rawKey;
				editor.ui.updateNote1Text = true;
			}
			else if (editor.ui.changingChordNote == 2)
			{
				editor.note2 = rawKey;
				editor.ui.updateNote2Text = true;
			}
			else if (editor.ui.changingChordNote == 3)
			{
				editor.note3 = rawKey;
				editor.ui.updateNote3Text = true;
			}
			else if (editor.ui.changingChordNote == 4)
			{
				editor.note4 = rawKey;
				editor.ui.updateNote4Text = true;
			}

			editor.ui.changingChordNote = false;
			recalcChordLength();

			setPrevStatusMessage();
			pointerSetPreviousMode();
		}

		return false;
	}

	// CHANGE DRUMPAD NOTE
	if (editor.ui.changingDrumPadNote)
	{
		if (scancode == SDL_SCANCODE_ESCAPE)
		{
			editor.ui.changingDrumPadNote = false;
			setPrevStatusMessage();
			pointerSetPreviousMode();
			return false;
		}

		if (scancode == SDL_SCANCODE_F1)
			editor.keyOctave = OCTAVE_LOW;
		else if (scancode == SDL_SCANCODE_F2)
			editor.keyOctave = OCTAVE_HIGH;

		rawKey = keyToNote(scancode);
		if (rawKey >= 0)
		{
			pNoteTable[editor.currSample] = rawKey;
			editor.ui.changingDrumPadNote = false;
			setPrevStatusMessage();
			pointerSetPreviousMode();
		}

		return false;
	}

	// SAMPLER SCREEN
	if (editor.ui.changingSmpResample)
	{
		if (scancode == SDL_SCANCODE_ESCAPE)
		{
			editor.ui.changingSmpResample = false;
			editor.ui.updateResampleNote = true;
			setPrevStatusMessage();
			pointerSetPreviousMode();
			return false;
		}

		if (scancode == SDL_SCANCODE_F1)
			editor.keyOctave = OCTAVE_LOW;
		else if (scancode == SDL_SCANCODE_F2)
			editor.keyOctave = OCTAVE_HIGH;

		rawKey = keyToNote(scancode);
		if (rawKey >= 0)
		{
			editor.resampleNote = rawKey;
			editor.ui.changingSmpResample = false;
			editor.ui.updateResampleNote = true;
			setPrevStatusMessage();
			pointerSetPreviousMode();
		}

		return false;
	}

	// DISK OP. SCREEN
	if (editor.diskop.isFilling)
	{
		if (editor.ui.askScreenShown && editor.ui.askScreenType == ASK_QUIT)
		{
			if (keycode == SDLK_y)
			{
				editor.ui.askScreenShown = false;
				editor.ui.answerNo = false;
				editor.ui.answerYes = true;
				handleAskYes();
			}
			else if (keycode == SDLK_n)
			{
				editor.ui.askScreenShown = false;
				editor.ui.answerNo = true;
				editor.ui.answerYes = false;
				handleAskNo();
			}
		}

		return false;
	}

	// if MOD2WAV is ongoing, only react to ESC and Y/N on exit ask dialog
	if (editor.isWAVRendering)
	{
		if (editor.ui.askScreenShown && editor.ui.askScreenType == ASK_QUIT)
		{
			if (keycode == SDLK_y)
			{
				editor.isWAVRendering = false;
				SDL_WaitThread(editor.mod2WavThread, NULL);

				editor.ui.askScreenShown = false;
				editor.ui.answerNo = false;
				editor.ui.answerYes = true;

				handleAskYes();
			}
			else if (keycode == SDLK_n)
			{
				editor.ui.askScreenShown = false;
				editor.ui.answerNo = true;
				editor.ui.answerYes = false;

				handleAskNo();

				pointerSetMode(POINTER_MODE_MSG2, NO_CARRY);
				setStatusMessage("RENDERING MOD...", NO_CARRY);
			}
		}
		else if (scancode == SDL_SCANCODE_ESCAPE)
		{
			editor.abortMod2Wav = true;
		}

		return false;
	}

	// SWAP CHANNEL (CTRL+T)
	if (editor.swapChannelFlag)
	{
		switch (scancode)
		{
			case SDL_SCANCODE_ESCAPE:
			{
				editor.swapChannelFlag = false;
				pointerSetPreviousMode();
				setPrevStatusMessage();
			}
			break;

			case SDL_SCANCODE_1:
			{
				for (i = 0; i < MOD_ROWS; i++)
				{
					noteSrc = &modEntry->patterns[modEntry->currPattern][(i * AMIGA_VOICES) + editor.cursor.channel];
					noteTmp = modEntry->patterns[modEntry->currPattern][i * AMIGA_VOICES];

					modEntry->patterns[modEntry->currPattern][i * AMIGA_VOICES] = *noteSrc;
					*noteSrc = noteTmp;
				}

				editor.swapChannelFlag = false;

				pointerSetPreviousMode();
				setPrevStatusMessage();
			}
			break;

			case SDL_SCANCODE_2:
			{
				for (i = 0; i < MOD_ROWS; i++)
				{
					noteSrc = &modEntry->patterns[modEntry->currPattern][(i * AMIGA_VOICES) + editor.cursor.channel];
					noteTmp = modEntry->patterns[modEntry->currPattern][(i * AMIGA_VOICES) + 1];

					modEntry->patterns[modEntry->currPattern][(i * AMIGA_VOICES) + 1] = *noteSrc;
					*noteSrc = noteTmp;
				}

				editor.swapChannelFlag = false;

				pointerSetPreviousMode();
				setPrevStatusMessage();
			}
			break;

			case SDL_SCANCODE_3:
			{
				for (i = 0; i < MOD_ROWS; i++)
				{
					noteSrc = &modEntry->patterns[modEntry->currPattern][(i * AMIGA_VOICES) + editor.cursor.channel];
					noteTmp = modEntry->patterns[modEntry->currPattern][(i * AMIGA_VOICES) + 2];

					modEntry->patterns[modEntry->currPattern][(i * AMIGA_VOICES) + 2] = *noteSrc;
					*noteSrc = noteTmp;
				}

				editor.swapChannelFlag = false;

				pointerSetPreviousMode();
				setPrevStatusMessage();
			}
			break;

			case SDL_SCANCODE_4:
			{
				for (i = 0; i < MOD_ROWS; i++)
				{
					noteSrc = &modEntry->patterns[modEntry->currPattern][(i * AMIGA_VOICES) + editor.cursor.channel];
					noteTmp = modEntry->patterns[modEntry->currPattern][(i * AMIGA_VOICES) + 3];

					modEntry->patterns[modEntry->currPattern][(i * AMIGA_VOICES) + 3] = *noteSrc;
					*noteSrc = noteTmp;
				}

				editor.swapChannelFlag = false;

				pointerSetPreviousMode();
				setPrevStatusMessage();
			}
			break;

			default: break;
		}

		return false;
	}

	// YES/NO ASK DIALOG
	if (editor.ui.askScreenShown)
	{
		if (editor.ui.pat2SmpDialogShown)
		{
			// PAT2SMP specific ask dialog
			switch (keycode)
			{
				case SDLK_KP_ENTER:
				case SDLK_RETURN:
				case SDLK_h:
				{
					editor.ui.askScreenShown = false;
					editor.ui.answerNo = true;
					editor.ui.answerYes = false;
					editor.pat2SmpHQ = true;
					handleAskYes();
				}
				break;

				case SDLK_l:
				{
					editor.ui.askScreenShown = false;
					editor.ui.answerNo = false;
					editor.ui.answerYes = true;
					editor.pat2SmpHQ = false;
					handleAskYes();
					// pointer/status is updated by the 'yes handler'
				}
				break;

				case SDLK_ESCAPE:
				case SDLK_a:
				case SDLK_n:
				{
					editor.ui.askScreenShown = false;
					editor.ui.answerNo = true;
					editor.ui.answerYes = false;
					handleAskNo();
				}
				break;

				default: break;
			}
		}
		else
		{
			// normal yes/no dialog
			switch (keycode)
			{
				case SDLK_ESCAPE:
				case SDLK_n:
				{
					editor.ui.askScreenShown = false;
					editor.ui.answerNo = true;
					editor.ui.answerYes = false;
					handleAskNo();
				}
				break;

				case SDLK_KP_ENTER:
				case SDLK_RETURN:
				case SDLK_y:
				{
					editor.ui.askScreenShown = false;
					editor.ui.answerNo = false;
					editor.ui.answerYes = true;
					handleAskYes();
					// pointer/status is updated by the 'yes handler'
				}
				break;

				default: break;
			}
		}

		return false;
	}

	// CLEAR SCREEN DIALOG
	if (editor.ui.clearScreenShown)
	{
		switch (keycode)
		{
			case SDLK_s:
			{
				editor.ui.clearScreenShown = false;
				removeClearScreen();

				modStop();
				clearSamples();

				editor.playMode = PLAY_MODE_NORMAL;
				editor.currMode = MODE_IDLE;

				pointerSetPreviousMode();
				setPrevStatusMessage();
			}
			break;

			case SDLK_o:
			{
				editor.ui.clearScreenShown = false;
				removeClearScreen();

				modStop();
				clearSong();

				editor.playMode = PLAY_MODE_NORMAL;
				editor.currMode = MODE_IDLE;

				pointerSetPreviousMode();
				setPrevStatusMessage();
			}
			break;

			case SDLK_a:
			{
				editor.ui.clearScreenShown = false;
				removeClearScreen();

				modStop();
				clearAll();

				editor.playMode = PLAY_MODE_NORMAL;
				editor.currMode = MODE_IDLE;

				pointerSetPreviousMode();
				setPrevStatusMessage();
			}
			break;

			case SDLK_c:
			case SDLK_ESCAPE:
			{
				editor.ui.clearScreenShown = false;
				removeClearScreen();

				editor.currMode = MODE_IDLE;

				pointerSetPreviousMode();
				setPrevStatusMessage();

				editor.errorMsgActive = true;
				editor.errorMsgBlock = true;
				editor.errorMsgCounter = 0;

				setErrPointer();
			}
			break;

			default: break;
		}

		return false;
	}

	return true;
}

void handleTextEditInputChar(char textChar)
{
	char *readTmp;
	int8_t readTmpPrev;
	uint8_t digit1, digit2, digit3, digit4;
	uint32_t i, number;

	// we only want certain keys
	if (textChar < ' ' || textChar > '~')
		return;

	// a..z -> A..Z
	if (textChar >= 'a' && textChar <= 'z')
		textChar = toupper(textChar);

	if (editor.ui.editTextType == TEXT_EDIT_STRING)
	{
		if (editor.ui.editPos < editor.ui.textEndPtr)
		{
			if (!editor.mixFlag)
			{
				readTmp = editor.ui.textEndPtr;
				while (readTmp > editor.ui.editPos)
				{
					readTmpPrev = *--readTmp;
					*(readTmp + 1) = readTmpPrev;
				}

				*editor.ui.textEndPtr = '\0';
				*editor.ui.editPos++ = textChar;

				textMarkerMoveRight();
			}
			else if ((textChar >= '0' && textChar <= '9') || (textChar >= 'A' && textChar <= 'F'))
			{
				if (editor.ui.dstPos == 14) // hack for sample mix text
				{
					*editor.ui.editPos = textChar;
				}
				else
				{
					*editor.ui.editPos++ = textChar;
					textMarkerMoveRight();

					if (editor.ui.dstPos == 9) // hack for sample mix text
					{
						for (i = 0; i < 4; i++)
						{
							editor.ui.editPos++;
							textMarkerMoveRight();
						}
					}
					else if (editor.ui.dstPos == 6) // hack for sample mix text
					{
						editor.ui.editPos++;
						textMarkerMoveRight();
					}
				}
			}
		}
	}
	else
	{
		if (editor.ui.editTextType == TEXT_EDIT_DECIMAL)
		{
			if (textChar >= '0' && textChar <= '9')
			{
				textChar -= '0';

				if (editor.ui.numLen == 4)
				{
					number = *editor.ui.numPtr16;
					digit4 = number % 10; number /= 10;
					digit3 = number % 10; number /= 10;
					digit2 = number % 10; number /= 10;
					digit1 = (uint8_t)number;

					     if (editor.ui.dstPos == 0) *editor.ui.numPtr16 = (textChar * 1000) + (digit2 * 100) + (digit3 * 10) + digit4;
					else if (editor.ui.dstPos == 1) *editor.ui.numPtr16 = (digit1 * 1000) + (textChar * 100) + (digit3 * 10) + digit4;
					else if (editor.ui.dstPos == 2) *editor.ui.numPtr16 = (digit1 * 1000) + (digit2 * 100) + (textChar * 10) + digit4;
					else if (editor.ui.dstPos == 3) *editor.ui.numPtr16 = (digit1 * 1000) + (digit2 * 100) + (digit3 * 10) + textChar;
				}
				else if (editor.ui.numLen == 3)
				{
					number = *editor.ui.numPtr16;
					digit3 = number % 10; number /= 10;
					digit2 = number % 10; number /= 10;
					digit1 = (uint8_t)number;

					     if (editor.ui.dstPos == 0) *editor.ui.numPtr16 = (textChar * 100) + (digit2 * 10) + digit3;
					else if (editor.ui.dstPos == 1) *editor.ui.numPtr16 = (digit1 * 100) + (textChar * 10) + digit3;
					else if (editor.ui.dstPos == 2) *editor.ui.numPtr16 = (digit1 * 100) + (digit2 * 10) + textChar;
				}
				else if (editor.ui.numLen == 2)
				{
					number = *editor.ui.numPtr16;
					digit2 = number % 10; number /= 10;
					digit1 = (uint8_t)number;

					     if (editor.ui.dstPos == 0) *editor.ui.numPtr16 = (textChar * 10) + digit2;
					else if (editor.ui.dstPos == 1) *editor.ui.numPtr16 = (digit1 * 10) + textChar;
				}

				textMarkerMoveRight();
				if (editor.ui.dstPos >= editor.ui.numLen)
					exitGetTextLine(EDIT_TEXT_UPDATE);
			}
		}
		else
		{
			if ((textChar >= '0' && textChar <= '9') || (textChar >= 'A' && textChar <= 'F'))
			{
				if (textChar <= '9')
					textChar -= '0';
				else if (textChar <= 'F')
					textChar -= 'A'-10;

				if (editor.ui.numBits == 16)
				{
					*editor.ui.numPtr16 &= ~(0xF000 >> (editor.ui.dstPos << 2));
					*editor.ui.numPtr16 |= (textChar << (12 - (editor.ui.dstPos << 2)));
				}
				else if (editor.ui.numBits == 8)
				{
					*editor.ui.numPtr8 &= ~(0xF0 >> (editor.ui.dstPos << 2));
					*editor.ui.numPtr8 |= (textChar << (4 - (editor.ui.dstPos << 2)));
				}

				textMarkerMoveRight();
				if (editor.ui.dstPos >= editor.ui.numLen)
					exitGetTextLine(EDIT_TEXT_UPDATE);
			}
		}
	}

	updateTextObject(editor.ui.editObject);

	if (!keyb.repeatKey)
		keyb.delayCounter = 0;

	keyb.repeatKey = true;
	keyb.delayKey = true;
}

bool handleTextEditMode(SDL_Scancode scancode)
{
	char *readTmp;
	int8_t readTmpNext;
	int16_t i, j;
	note_t *noteSrc, *noteDst;

	switch (scancode)
	{
		case SDL_SCANCODE_ESCAPE:
		{
			editor.blockMarkFlag = false;
			if (editor.ui.editTextFlag)
			{
				exitGetTextLine(EDIT_TEXT_NO_UPDATE);
				return false;
			}
		}
		break;

		case SDL_SCANCODE_HOME:
		{
			if (editor.ui.editTextFlag && !editor.mixFlag)
			{
				while (editor.ui.editPos > editor.ui.showTextPtr)
					textCharPrevious();
			}
		}
		break;

		case SDL_SCANCODE_END:
		{
			if (editor.ui.editTextFlag && !editor.mixFlag)
			{
				if (editor.ui.editTextType != TEXT_EDIT_STRING)
					break;

				while (!editor.ui.dstOffsetEnd)
					textCharNext();
			}
		}
		break;

		case SDL_SCANCODE_LEFT:
		{
			if (editor.ui.editTextFlag)
			{
				textCharPrevious();
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
			if (editor.ui.editTextFlag)
			{
				textCharNext();
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
			if (editor.ui.editTextFlag)
			{
				if (editor.mixFlag || editor.ui.editTextType != TEXT_EDIT_STRING)
					break;

				readTmp = editor.ui.editPos;
				while (readTmp < editor.ui.textEndPtr)
				{
					readTmpNext = *(readTmp + 1);
					*readTmp++ = readTmpNext;
				}

				// kludge to prevent cloning last character if the song/sample name has one character too much
				if (editor.ui.editObject == PTB_SONGNAME || editor.ui.editObject == PTB_SAMPLENAME)
					 *editor.ui.textEndPtr = '\0';

				if (!keyb.repeatKey)
					keyb.delayCounter = 0;

				keyb.repeatKey = true;
				keyb.delayKey = false;

				updateTextObject(editor.ui.editObject);
			}
		}
		break;

		case SDL_SCANCODE_BACKSPACE:
		{
			if (editor.ui.editTextFlag)
			{
				if (editor.mixFlag || editor.ui.editTextType != TEXT_EDIT_STRING)
					break;

				if (editor.ui.editPos > editor.ui.dstPtr)
				{
					editor.ui.editPos--;

					readTmp = editor.ui.editPos;
					while (readTmp < editor.ui.textEndPtr)
					{
						readTmpNext = *(readTmp + 1);
						*readTmp++ = readTmpNext;
					}

					// kludge to prevent cloning last character if the song/sample name has one character too much
					if (editor.ui.editObject == PTB_SONGNAME || editor.ui.editObject == PTB_SAMPLENAME)
						*editor.ui.textEndPtr = '\0';

					textMarkerMoveLeft();
					updateTextObject(editor.ui.editObject);
				}

				if (!keyb.repeatKey)
					keyb.delayCounter = 0;

				keyb.repeatKey = true;
				keyb.delayKey = false;
			}
			else
			{
				if (editor.ui.diskOpScreenShown)
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
						if (modEntry->currRow > 0)
						{
							for (i = 0; i < AMIGA_VOICES; i++)
							{
								for (j = (modEntry->currRow - 1); j < MOD_ROWS; j++)
								{
									noteSrc = &modEntry->patterns[modEntry->currPattern][((j + 1) * AMIGA_VOICES) + i];
									modEntry->patterns[modEntry->currPattern][(j * AMIGA_VOICES) + i] = *noteSrc;
								}

								// clear newly made row on very bottom
								noteDst = &modEntry->patterns[modEntry->currPattern][(63 * AMIGA_VOICES) + i];
								noteDst->period = 0;
								noteDst->sample = 0;
								noteDst->command = 0;
								noteDst->param = 0;
							}

							modEntry->currRow--;
							editor.ui.updatePatternData = true;
						}
					}
					else
					{
						if (modEntry->currRow > 0)
						{
							for (i = modEntry->currRow-1; i < MOD_ROWS-1; i++)
							{
								noteSrc = &modEntry->patterns[modEntry->currPattern][((i + 1) * AMIGA_VOICES) + editor.cursor.channel];
								noteDst = &modEntry->patterns[modEntry->currPattern][(i * AMIGA_VOICES) + editor.cursor.channel];

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
							noteDst = &modEntry->patterns[modEntry->currPattern][(63 * AMIGA_VOICES) + editor.cursor.channel];
							noteDst->period = 0;
							noteDst->sample = 0;
							noteDst->command = 0;
							noteDst->param = 0;

							modEntry->currRow--;
							editor.ui.updatePatternData = true;
						}
					}
				}
				else
				{
					editor.stepPlayEnabled = true;
					editor.stepPlayBackwards = true;

					doStopIt();
					playPattern((modEntry->currRow - 1) & 0x3F);
				}
			}
		}
		break;

		default: break;
	}

	if (editor.ui.editTextFlag)
	{
		if (scancode == SDL_SCANCODE_RETURN || scancode == SDL_SCANCODE_KP_ENTER)
		{
			// dirty hack
			if (editor.ui.editObject == PTB_SAMPLES)
				editor.ui.tmpDisp8++;

			exitGetTextLine(EDIT_TEXT_UPDATE);

			if (editor.mixFlag)
			{
				editor.mixFlag = false;
				editor.ui.updateMixText = true;
				doMix();
			}
		}

		return false; // don't continue further key handling
	}

	return true; // continue further key handling (we're not editing text)
}
