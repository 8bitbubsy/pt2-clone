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
#include "pt2_helpers.h"
#include "pt2_visuals.h"
#include "pt2_diskop.h"
#include "pt2_edit.h"
#include "pt2_sampler.h"
#include "pt2_audio.h"
#include "pt2_tables.h"
#include "pt2_module_saver.h"
#include "pt2_sample_saver.h"
#include "pt2_config.h"
#include "pt2_sampling.h"
#include "pt2_chordmaker.h"
#include "pt2_askbox.h"
#include "pt2_replayer.h"
#include "pt2_posed.h"
#include "pt2_textedit.h"

#if defined _WIN32 && !defined _DEBUG
extern bool windowsKeyIsDown;
extern HHOOK g_hKeyboardHook;
#endif

static bool handleGeneralModes(SDL_Scancode scancode);
static void movePatCurPrevCh(void);
static void movePatCurNextCh(void);
static void movePatCurRight(void);
static void movePatCurLeft(void);

bool handleTextEditMode(SDL_Scancode scancode);

void readKeyModifiers(void)
{
	const SDL_Keymod modState = SDL_GetModState();

	keyb.leftCtrlPressed = (modState & KMOD_LCTRL)  ? true : false;
	keyb.leftAltPressed = (modState & KMOD_LALT) ? true : false;
	keyb.shiftPressed = (modState & (KMOD_LSHIFT + KMOD_RSHIFT)) ? true : false;

#ifdef __APPLE__
	keyb.leftCommandPressed = (modState & KMOD_LGUI) ? true : false;
#endif

#if defined _WIN32 && !defined _DEBUG
	keyb.leftAmigaPressed = windowsKeyIsDown; // Windows: handled in lowLevelKeyboardProc
#else
	keyb.leftAmigaPressed = (modState & KMOD_LGUI) ? true : false;
#endif
}

#if defined _WIN32 && !defined _DEBUG
/* For taking control over the windows key if the program has focus.
** Warning: Don't do this in debug mode, it will completely ruin the keyboard input
** latency (in the OS in general) when the debugger is breaking.
*/
LRESULT CALLBACK lowLevelKeyboardProc(int32_t nCode, WPARAM wParam, LPARAM lParam)
{
	SDL_Window *window = video.window;

	if (nCode == HC_ACTION && window != NULL)
	{
		switch (wParam)
		{
			case WM_KEYUP:
			case WM_KEYDOWN:
			case WM_SYSKEYUP: // needed to prevent stuck Windows key if used with ALT
			{
				const bool windowHasFocus = SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_FOCUS;
				if (!windowHasFocus)
				{
					windowsKeyIsDown = false;
					break;
				}

				if (((KBDLLHOOKSTRUCT *)lParam)->vkCode != VK_LWIN)
					break;

				windowsKeyIsDown = (wParam == WM_KEYDOWN);
				return 1; // eat keystroke
			}
			break;

			default: break;
		}
	}

	return CallNextHookEx(g_hKeyboardHook, nCode, wParam, lParam);
}
#endif

void keyUpHandler(SDL_Scancode scancode)
{
	if (scancode == SDL_SCANCODE_KP_PLUS)
		keyb.keypadEnterPressed = false;

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
	if (scancode == SDL_SCANCODE_CAPSLOCK)
	{
		editor.repeatKeyFlag ^= 1;
		return;
	}

	// kludge: allow certain specific key combos to be repeated with the ctrl key
	const bool nonRepeatAltKeys = keyb.leftAltPressed && scancode != SDL_SCANCODE_DELETE    && scancode != SDL_SCANCODE_RETURN
	                                                  && scancode != SDL_SCANCODE_BACKSPACE && scancode != SDL_SCANCODE_BACKSLASH
	                                                  && scancode != SDL_SCANCODE_EQUALS    && scancode != SDL_SCANCODE_MINUS
	                                                  && scancode <  SDL_SCANCODE_1         && scancode >  SDL_SCANCODE_0;

	// kludge: allow certain specific key combos to be repeated with the alt key
	const bool nonRepeatCtrlKeys = keyb.leftCtrlPressed && scancode != SDL_SCANCODE_DELETE && scancode != SDL_SCANCODE_RETURN
	                                                    && scancode != SDL_SCANCODE_BACKSPACE;

	// these keys should not allow to be repeated in keyrepeat mode (caps lock)
	const bool nonRepeatKeys = keyb.leftAmigaPressed || nonRepeatAltKeys || nonRepeatCtrlKeys
		|| scancode == SDL_SCANCODE_LEFT || scancode == SDL_SCANCODE_RIGHT
		|| scancode == SDL_SCANCODE_UP   || scancode == SDL_SCANCODE_DOWN;
	if (editor.repeatKeyFlag && keyb.repeatKey && scancode == keyb.lastRepKey && nonRepeatKeys)
		return;

	if (scancode == SDL_SCANCODE_KP_PLUS)
		keyb.keypadEnterPressed = true;

	// TOGGLE FULLSCREEN (should always react)
	if (scancode == SDL_SCANCODE_F11 && !keyb.leftAltPressed)
	{
		toggleFullscreen();
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
	if (ui.diskOpScreenShown && keyb.shiftPressed && !ui.editTextFlag)
	{
		if (keycode >= 32 && keycode <= 126)
		{
			handleEntryJumping(keycode);
			return;
		}
	}

	// XXX: This really needs some refactoring, it's messy and not logical

	if (!handleGeneralModes(scancode)) return;
	if (!handleTextEditMode(scancode)) return;
	if (ui.samplerVolBoxShown || ui.samplingBoxShown) return;

	if (ui.samplerFiltersBoxShown)
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

				ui.updateTrackerFlags = true;
			}
		}
		break;

		case SDL_SCANCODE_BACKSLASH:
		{
			if (keyb.leftAltPressed)
			{
				if (handleSpecialKeys(scancode) && editor.currMode != MODE_RECORD)
					modSetPos(DONT_SET_ORDER, (song->currRow + editor.editMoveAdd) & 63);
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

				ui.updateTrackerFlags = true;
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
			if (!ui.askBoxShown)
			{
				editor.playMode = PLAY_MODE_NORMAL;
				modPlay(DONT_SET_PATTERN, song->currPos, DONT_SET_ROW);
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
			if (!ui.askBoxShown)
			{
				editor.playMode = PLAY_MODE_PATTERN;
				modPlay(song->currPattern, DONT_SET_ORDER, DONT_SET_ROW);
				editor.currMode = MODE_PLAY;
				pointerSetMode(POINTER_MODE_PLAY, DO_CARRY);
				statusAllRight();
			}
		}
		break;

		case SDL_SCANCODE_RSHIFT:
		{
			// right shift on Amiga keyb
			if (!ui.samplerScreenShown && !ui.askBoxShown)
			{
				editor.playMode = PLAY_MODE_PATTERN;
				modPlay(song->currPattern, DONT_SET_ORDER, DONT_SET_ROW);
				editor.currMode = MODE_RECORD;
				pointerSetMode(POINTER_MODE_EDIT, DO_CARRY);
				statusAllRight();
			}
		}
		break;

		case SDL_SCANCODE_ESCAPE:
		{
			if (ui.posEdScreenShown)
			{
				ui.posEdScreenShown = false;
				displayMainScreen();
			}
			else if (ui.diskOpScreenShown)
			{
				ui.diskOpScreenShown = false;
				displayMainScreen();
			}
			else if (ui.samplerScreenShown)
			{
				exitFromSam();
			}
			else if (ui.editOpScreenShown)
			{
				ui.editOpScreenShown = false;
				displayMainScreen();
			}
			else
			{
				if (askBox(ASKBOX_YES_NO, "REALLY QUIT ?"))
				{
					ui.throwExit = true;
					return;
				}
			}

			pointerSetPreviousMode();
			setPrevStatusMessage();
		}
		break;

		case SDL_SCANCODE_INSERT:
		{
			if (ui.samplerScreenShown)
			{
				samplerSamPaste();
				return;
			}
		}
		break;

		case SDL_SCANCODE_PAGEUP:
		{
			if (ui.posEdScreenShown)
			{
				posEdPageUp();
			}
			else if (ui.diskOpScreenShown)
			{
				diskop.scrollOffset -= DISKOP_LINES - 1;
				if (diskop.scrollOffset < 0)
					diskop.scrollOffset = 0;

				ui.updateDiskOpFileList = true;
			}
			else
			{
				if (editor.currMode == MODE_IDLE || editor.currMode == MODE_EDIT)
				{
					if (song->currRow == 63)
						modSetPos(DONT_SET_ORDER, song->currRow - 15);
					else if (song->currRow == 15)
						modSetPos(DONT_SET_ORDER, 0); // 15-16 would turn into -1, which is "DON'T SET ROW" flag
					else
						modSetPos(DONT_SET_ORDER, song->currRow - 16);
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
			if (ui.posEdScreenShown)
			{
				posEdPageDown();
			}
			else if (ui.diskOpScreenShown)
			{
				if (diskop.numEntries > DISKOP_LINES)
				{
					diskop.scrollOffset += DISKOP_LINES-1;
					if (diskop.scrollOffset > diskop.numEntries-DISKOP_LINES)
						diskop.scrollOffset = diskop.numEntries-DISKOP_LINES;

					ui.updateDiskOpFileList = true;
				}
			}
			else
			{
				if (editor.currMode == MODE_IDLE || editor.currMode == MODE_EDIT)
					modSetPos(DONT_SET_ORDER, song->currRow + 16);
			}

			if (!keyb.repeatKey)
				keyb.delayCounter = 0;

			keyb.repeatKey = true;
			keyb.delayKey = true;
		}
		break;

		case SDL_SCANCODE_HOME:
		{
			if (ui.posEdScreenShown)
			{
				posEdScrollToTop();
			}
			else if (ui.diskOpScreenShown)
			{
				if (diskop.scrollOffset != 0)
				{
					diskop.scrollOffset = 0;
					ui.updateDiskOpFileList = true;
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
			if (ui.posEdScreenShown)
			{
				posEdScrollToBottom();
			}
			else if (ui.diskOpScreenShown)
			{
				if (diskop.numEntries > DISKOP_LINES)
				{
					diskop.scrollOffset = diskop.numEntries - DISKOP_LINES;
					ui.updateDiskOpFileList = true;
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
			if (ui.samplerScreenShown)
				samplerSamDelete(NO_SAMPLE_CUT);
			else
				handleEditKeys(scancode, EDIT_NORMAL);
		}
		break;

		case SDL_SCANCODE_F12:
		{
			if (keyb.leftCtrlPressed)
			{
				const bool audioWasntLocked = !audio.locked;
				if (audioWasntLocked)
					lockAudio();

				editor.timingMode ^= 1;
				updateReplayerTimingMode();

				if (editor.timingMode == TEMPO_MODE_VBLANK)
				{
					editor.oldTempo = song->currBPM;
					modSetTempo(125, false);
				}
				else
				{
					modSetTempo(editor.oldTempo, false);
				}

				if (audioWasntLocked)
					unlockAudio();

				ui.updateSongTiming = true;
			}
			else if (keyb.shiftPressed)
			{
				toggleAmigaPanMode();
			}
			else
			{
				toggleAmigaFilterModel();
			}
		}
		break;

		case SDL_SCANCODE_RETURN:
		{
			if (keyb.shiftPressed || keyb.leftAltPressed || keyb.leftCtrlPressed)
			{
				saveUndo();
				if (keyb.leftAltPressed && !keyb.leftCtrlPressed)
				{
					if (song->currRow < 63)
					{
						for (int32_t i = 0; i < PAULA_VOICES; i++)
						{
							int32_t j;
							for (j = 62; j >= song->currRow; j--)
							{
								note_t *noteSrc = &song->patterns[song->currPattern][(j * PAULA_VOICES) + i];
								song->patterns[song->currPattern][((j + 1) * PAULA_VOICES) + i] = *noteSrc;
							}

							note_t *noteDst = &song->patterns[song->currPattern][((j + 1) * PAULA_VOICES) + i];

							noteDst->period = 0;
							noteDst->sample = 0;
							noteDst->command = 0;
							noteDst->param = 0;
						}

						song->currRow++;

						updateWindowTitle(MOD_IS_MODIFIED);
						ui.updatePatternData = true;
					}
				}
				else
				{
					if (song->currRow < 63)
					{
						int32_t i;
						for (i = 62; i >= song->currRow; i--)
						{
							note_t *noteSrc = &song->patterns[song->currPattern][((i + 0) * PAULA_VOICES) + cursor.channel];
							note_t *noteDst = &song->patterns[song->currPattern][((i + 1) * PAULA_VOICES) + cursor.channel];

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

						note_t *noteDst = &song->patterns[song->currPattern][((i + 1) * PAULA_VOICES) + cursor.channel];

						if (!keyb.leftCtrlPressed)
						{
							noteDst->period = 0;
							noteDst->sample = 0;
						}

						noteDst->command = 0;
						noteDst->param = 0;

						song->currRow++;

						updateWindowTitle(MOD_IS_MODIFIED);
						ui.updatePatternData = true;
					}
				}
			}
			else
			{
				editor.stepPlayEnabled = true;
				editor.stepPlayBackwards = false;

				editor.stepPlayLastMode = editor.currMode;

				if (config.keepEditModeAfterStepPlay && editor.stepPlayLastMode == MODE_EDIT)
					doStopIt(false);
				else
					doStopIt(true);

				playPattern(song->currRow);

				if (config.keepEditModeAfterStepPlay && editor.stepPlayLastMode == MODE_EDIT)
				{
					pointerSetMode(POINTER_MODE_EDIT, DO_CARRY);
					editor.playMode = PLAY_MODE_NORMAL;
					editor.currMode = MODE_EDIT;
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
				if (!ui.samplerScreenShown)
				{
					modStop();
					editor.currMode = MODE_IDLE;
					pointerSetMode(POINTER_MODE_IDLE, DO_CARRY);
					statusAllRight();
				}
			}
			else
			{
				if (!ui.samplerScreenShown)
				{
					modStop();
					editor.currMode = MODE_EDIT;
					pointerSetMode(POINTER_MODE_EDIT, DO_CARRY);
					statusAllRight();
				}
			}
		}
		break;

		case SDL_SCANCODE_F1: editor.keyOctave = OCTAVE_LOW;  break;
		case SDL_SCANCODE_F2: editor.keyOctave = OCTAVE_HIGH; break;

		case SDL_SCANCODE_F3:
		{
			if (ui.samplerScreenShown)
			{
				samplerSamDelete(SAMPLE_CUT);
			}
			else
			{
				if (keyb.shiftPressed)
				{
					// cut channel and put in buffer
					saveUndo();

					note_t *noteDst = editor.trackBuffer;
					for (int32_t i = 0; i < MOD_ROWS; i++)
					{
						note_t *noteSrc = &song->patterns[song->currPattern][(i * PAULA_VOICES) + cursor.channel];
						*noteDst++ = *noteSrc;

						noteSrc->period = 0;
						noteSrc->sample = 0;
						noteSrc->command = 0;
						noteSrc->param = 0;
					}

					updateWindowTitle(MOD_IS_MODIFIED);
					ui.updatePatternData = true;
				}
				else if (keyb.leftAltPressed)
				{
					// cut pattern and put in buffer
					saveUndo();

					memcpy(editor.patternBuffer, song->patterns[song->currPattern],
						sizeof (note_t) * (PAULA_VOICES * MOD_ROWS));

					memset(song->patterns[song->currPattern], 0,
						sizeof (note_t) * (PAULA_VOICES * MOD_ROWS));

					updateWindowTitle(MOD_IS_MODIFIED);
					ui.updatePatternData = true;
				}
				else if (keyb.leftCtrlPressed)
				{
					// cut channel commands and put in buffer
					saveUndo();

					note_t *noteDst = editor.cmdsBuffer;
					for (int32_t i = 0; i < MOD_ROWS; i++)
					{
						note_t *noteSrc = &song->patterns[song->currPattern][(i * PAULA_VOICES) + cursor.channel];
						*noteDst++ = *noteSrc;

						noteSrc->command = 0;
						noteSrc->param = 0;
					}

					updateWindowTitle(MOD_IS_MODIFIED);
					ui.updatePatternData = true;
				}
			}
		}
		break;

		case SDL_SCANCODE_F4:
		{
			if (ui.samplerScreenShown)
			{
				samplerSamCopy();
			}
			else
			{
				if (keyb.shiftPressed)
				{
					// copy channel to buffer

					note_t *noteDst = editor.trackBuffer;
					for (int32_t i = 0; i < MOD_ROWS; i++)
						*noteDst++ = song->patterns[song->currPattern][(i * PAULA_VOICES) + cursor.channel];
				}
				else if (keyb.leftAltPressed)
				{
					// copy pattern to buffer

					memcpy(editor.patternBuffer, song->patterns[song->currPattern],
						sizeof (note_t) * (PAULA_VOICES * MOD_ROWS));
				}
				else if (keyb.leftCtrlPressed)
				{
					// copy channel commands to buffer

					note_t *noteDst = editor.cmdsBuffer;
					for (int32_t i = 0; i < MOD_ROWS; i++)
					{
						note_t *noteSrc = &song->patterns[song->currPattern][(i * PAULA_VOICES) + cursor.channel];
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
			if (ui.samplerScreenShown)
			{
				samplerSamPaste();
			}
			else
			{
				if (keyb.shiftPressed)
				{
					// paste channel buffer to channel
					saveUndo();

					note_t *noteSrc = editor.trackBuffer;
					for (int32_t i = 0; i < MOD_ROWS; i++)
						song->patterns[song->currPattern][(i * PAULA_VOICES) + cursor.channel] = *noteSrc++;

					updateWindowTitle(MOD_IS_MODIFIED);
					ui.updatePatternData = true;
				}
				else if (keyb.leftAltPressed)
				{
					// paste pattern buffer to pattern
					saveUndo();

					memcpy(song->patterns[song->currPattern],
						editor.patternBuffer, sizeof (note_t) * (PAULA_VOICES * MOD_ROWS));

					updateWindowTitle(MOD_IS_MODIFIED);
					ui.updatePatternData = true;
				}
				else if (keyb.leftCtrlPressed)
				{
					// paste channel commands buffer to channel
					saveUndo();

					note_t *noteSrc = editor.cmdsBuffer;
					for (int32_t i = 0; i < MOD_ROWS; i++)
					{
						note_t *noteDst = &song->patterns[song->currPattern][(i * PAULA_VOICES) + cursor.channel];
						noteDst->command = noteSrc->command;
						noteDst->param = noteSrc->param;

						noteSrc++;
					}

					updateWindowTitle(MOD_IS_MODIFIED);
					ui.updatePatternData = true;
				}
			}
		}
		break;

		case SDL_SCANCODE_F6:
		{
			if (keyb.shiftPressed)
			{
				editor.f6Pos = song->currRow;
				displayMsg("POSITION SET");
			}
			else
			{
				if (keyb.leftAltPressed)
				{
					editor.playMode = PLAY_MODE_PATTERN;
					modPlay(song->currPattern, DONT_SET_ORDER, editor.f6Pos);

					editor.currMode = MODE_PLAY;
					pointerSetMode(POINTER_MODE_PLAY, DO_CARRY);
					statusAllRight();
				}
				else if (keyb.leftCtrlPressed)
				{
					if (!ui.samplerScreenShown)
					{
						editor.playMode = PLAY_MODE_PATTERN;
						modPlay(song->currPattern, DONT_SET_ORDER, editor.f6Pos);

						editor.currMode = MODE_RECORD;
						pointerSetMode(POINTER_MODE_EDIT, DO_CARRY);
						statusAllRight();
					}
				}
				else if (keyb.leftAmigaPressed)
				{
					editor.playMode = PLAY_MODE_NORMAL;
					modPlay(DONT_SET_PATTERN, song->currPos, editor.f6Pos);

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
				editor.f7Pos = song->currRow;
				displayMsg("POSITION SET");
			}
			else
			{
				if (keyb.leftAltPressed)
				{
					editor.playMode = PLAY_MODE_PATTERN;
					modPlay(song->currPattern, DONT_SET_ORDER, editor.f7Pos);

					editor.currMode = MODE_PLAY;
					pointerSetMode(POINTER_MODE_PLAY, DO_CARRY);
					statusAllRight();
				}
				else if (keyb.leftCtrlPressed)
				{
					if (!ui.samplerScreenShown)
					{
						editor.playMode = PLAY_MODE_PATTERN;
						modPlay(song->currPattern, DONT_SET_ORDER, editor.f7Pos);

						editor.currMode = MODE_RECORD;
						pointerSetMode(POINTER_MODE_EDIT, DO_CARRY);
						statusAllRight();
					}
				}
				else if (keyb.leftAmigaPressed)
				{
					editor.playMode = PLAY_MODE_NORMAL;
					modPlay(DONT_SET_PATTERN, song->currPos, editor.f7Pos);

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
				editor.f8Pos = song->currRow;
				displayMsg("POSITION SET");
			}
			else
			{
				if (keyb.leftAltPressed)
				{
					editor.playMode = PLAY_MODE_PATTERN;
					modPlay(song->currPattern, DONT_SET_ORDER, editor.f8Pos);

					editor.currMode = MODE_PLAY;
					pointerSetMode(POINTER_MODE_PLAY, DO_CARRY);
					statusAllRight();
				}
				else if (keyb.leftCtrlPressed)
				{
					if (!ui.samplerScreenShown)
					{
						editor.playMode = PLAY_MODE_PATTERN;
						modPlay(song->currPattern, DONT_SET_ORDER, editor.f8Pos);

						editor.currMode = MODE_RECORD;
						pointerSetMode(POINTER_MODE_EDIT, DO_CARRY);
						statusAllRight();
					}
				}
				else if (keyb.leftAmigaPressed)
				{
					editor.playMode = PLAY_MODE_NORMAL;
					modPlay(DONT_SET_PATTERN, song->currPos, editor.f8Pos);

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
				editor.f9Pos = song->currRow;
				displayMsg("POSITION SET");
			}
			else
			{
				if (keyb.leftAltPressed)
				{
					editor.playMode = PLAY_MODE_PATTERN;
					modPlay(song->currPattern, DONT_SET_ORDER, editor.f9Pos);

					editor.currMode = MODE_PLAY;
					pointerSetMode(POINTER_MODE_PLAY, DO_CARRY);
					statusAllRight();
				}
				else if (keyb.leftCtrlPressed)
				{
					if (!ui.samplerScreenShown)
					{
						editor.playMode = PLAY_MODE_PATTERN;
						modPlay(song->currPattern, DONT_SET_ORDER, editor.f9Pos);

						editor.currMode = MODE_RECORD;
						pointerSetMode(POINTER_MODE_EDIT, DO_CARRY);
						statusAllRight();
					}
				}
				else if (keyb.leftAmigaPressed)
				{
					editor.playMode = PLAY_MODE_NORMAL;
					modPlay(DONT_SET_PATTERN, song->currPos, editor.f9Pos);

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
				editor.f10Pos = song->currRow;
				displayMsg("POSITION SET");
			}
			else
			{
				if (keyb.leftAltPressed)
				{
					editor.playMode = PLAY_MODE_PATTERN;
					modPlay(song->currPattern, DONT_SET_ORDER, editor.f10Pos);

					editor.currMode = MODE_PLAY;
					pointerSetMode(POINTER_MODE_PLAY, DO_CARRY);
					statusAllRight();
				}
				else if (keyb.leftCtrlPressed)
				{
					if (!ui.samplerScreenShown)
					{
						editor.playMode = PLAY_MODE_PATTERN;
						modPlay(song->currPattern, DONT_SET_ORDER, editor.f10Pos);

						editor.currMode = MODE_RECORD;
						pointerSetMode(POINTER_MODE_EDIT, DO_CARRY);
						statusAllRight();
					}
				}
				else if (keyb.leftAmigaPressed)
				{
					editor.playMode = PLAY_MODE_NORMAL;
					modPlay(DONT_SET_PATTERN, song->currPos, editor.f10Pos);

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
				ui.updateTrackerFlags = true;
			}
			else if (keyb.shiftPressed)
			{
				note_t *noteSrc = &song->patterns[song->currPattern][(song->currRow * PAULA_VOICES) + cursor.channel];
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
				ui.updateTrackerFlags = true;
			}
			else if (keyb.shiftPressed)
			{
				note_t *noteSrc = &song->patterns[song->currPattern][(song->currRow * PAULA_VOICES) + cursor.channel];
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
				ui.updateTrackerFlags = true;
			}
			else if (keyb.shiftPressed)
			{
				note_t *noteSrc = &song->patterns[song->currPattern][(song->currRow * PAULA_VOICES) + cursor.channel];
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
				ui.updateTrackerFlags = true;
			}
			else if (keyb.shiftPressed)
			{
				note_t *noteSrc = &song->patterns[song->currPattern][(song->currRow * PAULA_VOICES) + cursor.channel];
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
				ui.updateTrackerFlags = true;
			}
			else if (keyb.shiftPressed)
			{
				note_t *noteSrc = &song->patterns[song->currPattern][(song->currRow * PAULA_VOICES) + cursor.channel];
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
				ui.updateTrackerFlags = true;
			}
			else if (keyb.shiftPressed)
			{
				note_t *noteSrc = &song->patterns[song->currPattern][(song->currRow * PAULA_VOICES) + cursor.channel];
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
				ui.updateTrackerFlags = true;
			}
			else if (keyb.shiftPressed)
			{
				note_t *noteSrc = &song->patterns[song->currPattern][(song->currRow * PAULA_VOICES) + cursor.channel];
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
				ui.updateTrackerFlags = true;
			}
			else if (keyb.shiftPressed)
			{
				note_t *noteSrc = &song->patterns[song->currPattern][(song->currRow * PAULA_VOICES) + cursor.channel];
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
				ui.updateTrackerFlags = true;
			}
			else if (keyb.shiftPressed)
			{
				note_t *noteSrc = &song->patterns[song->currPattern][(song->currRow * PAULA_VOICES) + cursor.channel];
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
				ui.updateTrackerFlags = true;
			}
			else if (keyb.shiftPressed)
			{
				note_t *noteSrc = &song->patterns[song->currPattern][(song->currRow * PAULA_VOICES) + cursor.channel];
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
			if (editor.hiLowInstr >= 0x10)
			{
				editor.sampleZero = false;
				editor.currSample = 0x10-1;
			}
			else
			{
				editor.sampleZero = true;
				editor.currSample = 0x00;
			}

			updateCurrSample();
		}
		break;

		case SDL_SCANCODE_KP_1:
		{
			editor.sampleZero = false;
			editor.currSample = editor.hiLowInstr + 12;

			updateCurrSample();
			if (keyb.leftAltPressed && editor.pNoteFlag > 0)
			{
				ui.changingDrumPadNote = true;
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
			editor.currSample = editor.hiLowInstr + 13;

			updateCurrSample();
			if (keyb.leftAltPressed && editor.pNoteFlag > 0)
			{
				ui.changingDrumPadNote = true;
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
			editor.currSample = editor.hiLowInstr + 14;

			updateCurrSample();
			if (keyb.leftAltPressed && editor.pNoteFlag > 0)
			{
				ui.changingDrumPadNote = true;
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
			editor.currSample = editor.hiLowInstr + 8;

			updateCurrSample();
			if (keyb.leftAltPressed && editor.pNoteFlag > 0)
			{
				ui.changingDrumPadNote = true;
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
			editor.currSample = editor.hiLowInstr + 9;

			updateCurrSample();
			if (keyb.leftAltPressed && editor.pNoteFlag > 0)
			{
				ui.changingDrumPadNote = true;
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
			editor.currSample = editor.hiLowInstr + 10;

			updateCurrSample();
			if (keyb.leftAltPressed && editor.pNoteFlag > 0)
			{
				ui.changingDrumPadNote = true;
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
			editor.currSample = editor.hiLowInstr + 4;

			updateCurrSample();
			if (keyb.leftAltPressed && editor.pNoteFlag > 0)
			{
				ui.changingDrumPadNote = true;
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
			editor.currSample = editor.hiLowInstr + 5;

			updateCurrSample();
			if (keyb.leftAltPressed && editor.pNoteFlag > 0)
			{
				ui.changingDrumPadNote = true;
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
			editor.currSample = editor.hiLowInstr + 6;

			updateCurrSample();
			if (keyb.leftAltPressed && editor.pNoteFlag > 0)
			{
				ui.changingDrumPadNote = true;
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
			editor.hiLowInstr ^= 0x10;

			if (editor.sampleZero)
			{
				editor.currSample = 15;
				editor.sampleZero = false;
			}
			else
			{
				editor.currSample ^= 0x10;
			}

			if (editor.currSample == 31) // kludge if sample was 15 (0010 in UI) before key press
			{
				editor.currSample = 15;
				editor.sampleZero ^= 1;
			}

			updateCurrSample();
			if (keyb.leftAltPressed && editor.pNoteFlag > 0)
			{
				ui.changingDrumPadNote = true;
				setStatusMessage("SELECT NOTE", NO_CARRY);
				pointerSetMode(POINTER_MODE_MSG1, NO_CARRY);
				break;
			}

			if (editor.pNoteFlag > 0)
				handleEditKeys(scancode, EDIT_SPECIAL);
		}
		break;

		case SDL_SCANCODE_KP_PLUS:
		{
			editor.sampleZero = false;

			// the Amiga numpad has one more key, so we need to use this key for two sample numbers...
			if (editor.keypadToggle8CFlag)
				editor.currSample = editor.hiLowInstr + (0x0C - 1);
			else
				editor.currSample = editor.hiLowInstr + (0x08 - 1);

			updateCurrSample();
			if (keyb.leftAltPressed && editor.pNoteFlag > 0)
				displayErrorMsg("INVALID PAD KEY !");

			editor.keypadToggle8CFlag ^= 1;
		}
		break;

		case SDL_SCANCODE_KP_MINUS:
		{
			editor.sampleZero = false;
			editor.currSample = editor.hiLowInstr + 3;

			updateCurrSample();
			if (keyb.leftAltPressed && editor.pNoteFlag > 0)
			{
				ui.changingDrumPadNote = true;
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
			editor.currSample = editor.hiLowInstr + 2;

			updateCurrSample();
			if (keyb.leftAltPressed && editor.pNoteFlag > 0)
			{
				ui.changingDrumPadNote = true;
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
			editor.currSample = editor.hiLowInstr + 1;

			updateCurrSample();
			if (keyb.leftAltPressed && editor.pNoteFlag > 0)
			{
				ui.changingDrumPadNote = true;
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
			editor.currSample = editor.hiLowInstr + 0;

			updateCurrSample();
			if (keyb.leftAltPressed && editor.pNoteFlag > 0)
			{
				ui.changingDrumPadNote = true;

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
			if (askBox(ASKBOX_YES_NO, "KILL SAMPLE ?"))
				killSample();
		}
		break;

		case SDL_SCANCODE_DOWN:
		{
			keyb.delayKey = false;
			keyb.repeatKey = false;

			if (ui.diskOpScreenShown)
			{
				if (diskop.numEntries > DISKOP_LINES)
				{
					diskop.scrollOffset++;
					if (mouse.rightButtonPressed) // PT quirk: right mouse button speeds up scrolling even on keyb UP/DOWN
						diskop.scrollOffset += 3;

					if (diskop.scrollOffset > diskop.numEntries-DISKOP_LINES)
						diskop.scrollOffset = diskop.numEntries-DISKOP_LINES;

					ui.updateDiskOpFileList = true;
				}

				if (!keyb.repeatKey)
					keyb.delayCounter = 0;

				keyb.repeatKey = true;
				keyb.delayKey = false;
			}
			else if (ui.posEdScreenShown)
			{
				posEdScrollDown();

				if (!keyb.repeatKey)
					keyb.delayCounter = 0;

				keyb.repeatKey = true;
				keyb.delayKey = true;
			}
			else if (!ui.samplerScreenShown)
			{
				if (editor.currMode != MODE_PLAY && editor.currMode != MODE_RECORD)
					modSetPos(DONT_SET_ORDER, (song->currRow + 1) & 63);

				keyb.repeatKey = true;
			}
		}
		break;

		case SDL_SCANCODE_UP:
		{
			keyb.delayKey  = false;
			keyb.repeatKey = false;

			if (ui.diskOpScreenShown)
			{
				diskop.scrollOffset--;
				if (mouse.rightButtonPressed) // PT quirk: right mouse button speeds up scrolling even on keyb UP/DOWN
					diskop.scrollOffset -= 3;

				if (diskop.scrollOffset < 0)
					diskop.scrollOffset = 0;

				ui.updateDiskOpFileList = true;

				if (!keyb.repeatKey)
					keyb.delayCounter = 0;

				keyb.repeatKey = true;
				keyb.delayKey = false;
			}
			else if (ui.posEdScreenShown)
			{
				posEdScrollUp();

				if (!keyb.repeatKey)
					keyb.delayCounter = 0;

				keyb.repeatKey = true;
				keyb.delayKey = true;
			}
			else if (!ui.samplerScreenShown)
			{
				if ((editor.currMode != MODE_PLAY) && (editor.currMode != MODE_RECORD))
					modSetPos(DONT_SET_ORDER, (song->currRow - 1) & 63);

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
				if (song->currPos > 0)
				{
					modSetPos(song->currPos - 1, DONT_SET_ROW);
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
				if (song->currPos < 126)
				{
					modSetPos(song->currPos + 1, DONT_SET_ROW);
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
				if (ui.samplerScreenShown)
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

						editor.muted[cursor.channel] = false;
						renderMuteButtons();
						break;
					}

					editor.muted[cursor.channel] ^= 1;
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
				if (ui.introTextShown)
				{
					ui.introTextShown = false;
					statusAllRight();
				}

				if (editor.blockMarkFlag)
				{
					editor.blockMarkFlag = false;
				}
				else
				{
					editor.blockMarkFlag = true;
					editor.blockFromPos = song->currRow;
					editor.blockToPos = song->currRow;
				}

				ui.updateStatusText = true;
			}
			else if (keyb.leftAltPressed)
			{
				moduleSample_t *s = &song->samples[editor.currSample];
				if (s->length == 0)
				{
					statusSampleIsEmpty();
					break;
				}

				boostSample(editor.currSample, true);

				if (ui.samplerScreenShown)
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
				if (ui.samplerScreenShown)
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

				for (int32_t i = 0; i < MOD_ROWS; i++)
					editor.blockBuffer[i] = song->patterns[song->currPattern][(i * PAULA_VOICES) + cursor.channel];

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
					if (!ui.posEdScreenShown)
					{
						editor.blockMarkFlag = false;

						ui.diskOpScreenShown ^= 1;
						if (!ui.diskOpScreenShown)
						{
							pointerSetPreviousMode();
							setPrevStatusMessage();
							displayMainScreen();
						}
						else
						{
							ui.diskOpScreenShown = true;
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
				if (!ui.diskOpScreenShown && !ui.posEdScreenShown)
				{
					if (ui.editOpScreenShown)
						ui.editOpScreen = (ui.editOpScreen + 1) % 3;
					else
						ui.editOpScreenShown = true;

					renderEditOpScreen();
				}
			}
			else if (keyb.leftCtrlPressed)
			{
				saveUndo();

				int32_t j = song->currRow + 1;
				while (j < MOD_ROWS)
				{
					int32_t i;
					for (i = 62; i >= j; i--)
					{
						note_t *noteSrc = &song->patterns[song->currPattern][(i * PAULA_VOICES) + cursor.channel];
						song->patterns[song->currPattern][((i + 1) * PAULA_VOICES) + cursor.channel] = *noteSrc;
					}

					note_t *noteDst = &song->patterns[song->currPattern][((i + 1) * PAULA_VOICES) + cursor.channel];
					noteDst->period = 0;
					noteDst->sample = 0;
					noteDst->command = 0;
					noteDst->param = 0;

					j += 2;
				}

				updateWindowTitle(MOD_IS_MODIFIED);
				ui.updatePatternData = true;
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
				toggleFullscreen();
			}
			else
#endif
			if (keyb.leftAmigaPressed)
			{
				pattOctaUp(TRANSPOSE_ALL);
			}
			else if (keyb.leftCtrlPressed)
			{
				if (keyb.shiftPressed)
				{
					resetFPSCounter();

					video.debug ^= 1;
					if (!video.debug)
						displayMainScreen();
				}
				else
				{
					toggleLEDFilter();

					if (audio.ledFilterEnabled)
						displayMsg("LED FILTER ON");
					else
						displayMsg("LED FILTER OFF");
				}
			}
			else if (keyb.leftAltPressed)
			{
				moduleSample_t *s = &song->samples[editor.currSample];
				if (s->length == 0)
				{
					statusSampleIsEmpty();
					break;
				}

				filterSample(editor.currSample, true);
				if (ui.samplerScreenShown)
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
			if (keyb.leftAltPressed) // toggle record mode (PT clone and PT2.3E only)
			{
				editor.recordMode ^= 1;
				if (editor.recordMode == 0)
					displayMsg("REC MODE: PATT");
				else
					displayMsg("REC MODE: SONG");

				if (ui.editOpScreenShown && ui.editOpScreen == 1)
					ui.updateRecordText = true;
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

				if (song->currRow < 63)
				{
					for (int32_t i = 0; i <= editor.buffToPos-editor.buffFromPos; i++)
					{
						for (int32_t j = 62; j >= song->currRow; j--)
						{
							note_t *noteSrc = &song->patterns[song->currPattern][(j * PAULA_VOICES) + cursor.channel];
							song->patterns[song->currPattern][((j + 1) * PAULA_VOICES) + cursor.channel] = *noteSrc;
						}
					}
				}

				saveUndo();

				int32_t i;
				for (i = 0; i <= editor.buffToPos-editor.buffFromPos; i++)
				{
					if (song->currRow+i > 63)
						break;

					song->patterns[song->currPattern][((song->currRow + i) * PAULA_VOICES) + cursor.channel]
						= editor.blockBuffer[editor.buffFromPos + i];
				}

				if (!keyb.shiftPressed)
				{
					song->currRow += i & 0xFF;
					if (song->currRow > 63)
						song->currRow = 0;
				}

				updateWindowTitle(MOD_IS_MODIFIED);
				ui.updatePatternData = true;
			}
			else if (keyb.leftAltPressed)
			{
				editor.autoInsFlag ^= 1;
				ui.updateTrackerFlags = true;
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

				int32_t i = editor.buffFromPos;
				int32_t j = song->currRow;
				note_t *patt = song->patterns[song->currPattern];

				while (true)
				{
					note_t *noteDst = &patt[(j * PAULA_VOICES) + cursor.channel];

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
					song->currRow += (editor.buffToPos-editor.buffFromPos) + 1;
					if (song->currRow > 63)
						song->currRow = 0;
				}

				updateWindowTitle(MOD_IS_MODIFIED);
				ui.updatePatternData = true;
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

				updateWindowTitle(MOD_IS_MODIFIED);
				ui.updatePatternData = true;
			}
			else if (keyb.leftCtrlPressed)
			{
				saveUndo();

				int32_t i = song->currRow;
				if (keyb.shiftPressed)
				{
					// kill to start
					while (i >= 0)
					{
						note_t *noteDst = &song->patterns[song->currPattern][(i * PAULA_VOICES) + cursor.channel];
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
						note_t *noteDst = &song->patterns[song->currPattern][(i * PAULA_VOICES) + cursor.channel];
						noteDst->period = 0;
						noteDst->sample = 0;
						noteDst->command = 0;
						noteDst->param = 0;

						i++;
					}
				}

				updateWindowTitle(MOD_IS_MODIFIED);
				ui.updatePatternData = true;
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
				ui.updateTrackerFlags = true;
				ui.updateKeysText = true;
			}
			else if (keyb.leftAltPressed)
			{
				if (keyb.shiftPressed)
					editor.metroChannel = cursor.channel + 1;
				else
					editor.metroFlag ^= 1;

				ui.updateTrackerFlags = true;
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
				song->currRow = editor.blockToPos;
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

				int32_t j = song->currRow + 1;
				while (j < MOD_ROWS)
				{
					for (int32_t i = j; i < MOD_ROWS-1; i++)
					{
						note_t *noteSrc = &song->patterns[song->currPattern][((i + 1) * PAULA_VOICES) + cursor.channel];
						song->patterns[song->currPattern][(i * PAULA_VOICES) + cursor.channel] = *noteSrc;
					}

					// clear newly made row on very bottom
					note_t *noteDst = &song->patterns[song->currPattern][(63 * PAULA_VOICES) + cursor.channel];
					noteDst->period = 0;
					noteDst->sample = 0;
					noteDst->command = 0;
					noteDst->param = 0;

					j++;
				}

				updateWindowTitle(MOD_IS_MODIFIED);
				ui.updatePatternData = true;
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

				int32_t i = editor.buffFromPos;
				int32_t j = song->currRow;
				note_t *patt = song->patterns[song->currPattern];

				while (true)
				{
					note_t *noteDst = &patt[(j * PAULA_VOICES) + cursor.channel];
					*noteDst = editor.blockBuffer[i];

					if (i == editor.buffToPos || i == 63 || j == 63)
						break;

					i++;
					j++;
				}

				if (!keyb.shiftPressed)
				{
					song->currRow += (editor.buffToPos-editor.buffFromPos) + 1;
					if (song->currRow > 63)
						song->currRow = 0;
				}

				updateWindowTitle(MOD_IS_MODIFIED);
				ui.updatePatternData = true;
			}
			else if (keyb.leftAltPressed)
			{
				if (!ui.diskOpScreenShown)
					posEdToggle();
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
#ifdef __APPLE__
				/* On Mac, command+Q sends a quit signal to the program.
				** However, command+Q is also one of the transpose keys in this ProTracker port.
				** Ignore the signal if command+Q was pressed.
				*/
				editor.macCmdQIssued = true;
#endif
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
				if (askBox(ASKBOX_YES_NO, "REALLY QUIT ?"))
					ui.throwExit = true;
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
				if (askBox(ASKBOX_YES_NO, "RESAMPLE?"))
					samplerResample();
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
				if (diskop.mode == DISKOP_MODE_SMP)
					UNICHAR_CHDIR(editor.modulesPathU);

				saveModule(DONT_CHECK_IF_FILE_EXIST, DONT_GIVE_NEW_FILENAME);

				// set current dir to samples path
				if (diskop.mode == DISKOP_MODE_SMP)
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
				if (ui.samplerScreenShown)
				{
					samplerSamPaste();
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

				int32_t i = editor.buffFromPos;
				int32_t j = song->currRow;
				note_t *patt = song->patterns[song->currPattern];

				while (true)
				{
					note_t *noteDst = &patt[(j * PAULA_VOICES) + cursor.channel];
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
					song->currRow += (editor.buffToPos-editor.buffFromPos) + 1;
					if (song->currRow > 63)
						song->currRow = 0;
				}

				updateWindowTitle(MOD_IS_MODIFIED);
				ui.updatePatternData = true;
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
				if (ui.samplerScreenShown)
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

				for (int32_t i = 0; i < MOD_ROWS; i++)
					editor.blockBuffer[i] = song->patterns[song->currPattern][(i * PAULA_VOICES) + cursor.channel];

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

				for (int32_t i = editor.buffFromPos; i <= editor.buffToPos; i++)
				{
					note_t *noteDst = &song->patterns[song->currPattern][(i * PAULA_VOICES) + cursor.channel];
					noteDst->period = 0;
					noteDst->sample = 0;
					noteDst->command = 0;
					noteDst->param = 0;
				}

				statusAllRight();
				updateWindowTitle(MOD_IS_MODIFIED);
				ui.updatePatternData = true;
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
			uint8_t blockFrom, blockTo;

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
					note_t *noteDst = &song->patterns[song->currPattern][(blockFrom * PAULA_VOICES) + cursor.channel];
					note_t *noteSrc = &song->patterns[song->currPattern][(blockTo * PAULA_VOICES) + cursor.channel];

					note_t noteTmp = *noteDst;
					*noteDst = *noteSrc;
					*noteSrc = noteTmp;

					blockFrom += 1;
					blockTo -= 1;
				}

				statusAllRight();
				updateWindowTitle(MOD_IS_MODIFIED);
				ui.updatePatternData = true;
			}
			else if (keyb.leftAltPressed)
			{
				if (askBox(ASKBOX_YES_NO, "SAVE ALL SAMPLES?"))
				{
					int8_t oldSample = editor.currSample;
					for (int32_t i = 0; i < MOD_SAMPLES; i++)
					{
						editor.currSample = (int8_t)i;
						if (song->samples[i].length > 2)
							saveSample(DONT_CHECK_IF_FILE_EXIST, GIVE_NEW_FILENAME);
					}
					editor.currSample = oldSample;

					displayMsg("SAMPLES SAVED !");
					setMsgPointer();
				}
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
				if (ui.samplerScreenShown)
				{
					if (askBox(ASKBOX_YES_NO, "RESTORE SAMPLE?"))
						redoSampleData(editor.currSample);
				}
				else
				{
					modSetTempo(125, true);
					modSetSpeed(6);

					moduleChannel_t *ch = song->channels;
					for (int32_t i = 0; i < PAULA_VOICES; i++, ch++)
					{
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

static void movePatCurPrevCh(void)
{
	int8_t pos = ((cursor.pos + 5) / 6) - 1;

	cursor.pos = (pos < 0) ? (3 * 6) : (pos * 6);
	cursor.mode = CURSOR_NOTE;

	     if (cursor.pos <  6) cursor.channel = 0;
	else if (cursor.pos < 12) cursor.channel = 1;
	else if (cursor.pos < 18) cursor.channel = 2;
	else if (cursor.pos < 24) cursor.channel = 3;

	updateCursorPos();
}

static void movePatCurNextCh(void)
{
	int8_t pos = (cursor.pos / 6) + 1;

	cursor.pos = (pos == 4) ? 0 : (pos * 6);
	cursor.mode = CURSOR_NOTE;

	     if (cursor.pos <  6) cursor.channel = 0;
	else if (cursor.pos < 12) cursor.channel = 1;
	else if (cursor.pos < 18) cursor.channel = 2;
	else if (cursor.pos < 24) cursor.channel = 3;

	updateCursorPos();
}

static void movePatCurRight(void)
{
	cursor.pos = (cursor.pos == 23) ? 0 : (cursor.pos + 1);

	     if (cursor.pos <  6) cursor.channel = 0;
	else if (cursor.pos < 12) cursor.channel = 1;
	else if (cursor.pos < 18) cursor.channel = 2;
	else if (cursor.pos < 24) cursor.channel = 3;

	cursor.mode = cursor.pos % 6;
	updateCursorPos();
}

static void movePatCurLeft(void)
{
	cursor.pos = (cursor.pos == 0) ? 23 : (cursor.pos - 1);

	     if (cursor.pos <  6) cursor.channel = 0;
	else if (cursor.pos < 12) cursor.channel = 1;
	else if (cursor.pos < 18) cursor.channel = 2;
	else if (cursor.pos < 24) cursor.channel = 3;

	cursor.mode = cursor.pos % 6;
	updateCursorPos();
}

void handleKeyRepeat(SDL_Scancode scancode)
{
	if (!keyb.repeatKey || ui.askBoxShown)
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

				if (ui.posEdScreenShown)
				{
					posEdPageUp();
				}
				else if (ui.diskOpScreenShown)
				{
					if (ui.diskOpScreenShown)
					{
						diskop.scrollOffset -= DISKOP_LINES-1;
						if (diskop.scrollOffset < 0)
							diskop.scrollOffset = 0;

						ui.updateDiskOpFileList = true;
					}
				}
				else if (editor.currMode == MODE_IDLE || editor.currMode == MODE_EDIT)
				{
					if (song->currRow == 63)
						modSetPos(DONT_SET_ORDER, song->currRow - 15);
					else if (song->currRow == 15)
						modSetPos(DONT_SET_ORDER, 0); // 15-16 would turn into -1, which is "DON'T SET ROW" flag
					else
						modSetPos(DONT_SET_ORDER, song->currRow - 16);
				}
			}
		}
		break;

		case SDL_SCANCODE_PAGEDOWN:
		{
			if (keyb.repeatCounter >= 3)
			{
				keyb.repeatCounter = 0;

				if (ui.posEdScreenShown)
				{
					posEdPageDown();
				}
				else if (ui.diskOpScreenShown)
				{
					if (diskop.numEntries > DISKOP_LINES)
					{
						diskop.scrollOffset += DISKOP_LINES-1;
						if (diskop.scrollOffset > diskop.numEntries-DISKOP_LINES)
							diskop.scrollOffset = diskop.numEntries-DISKOP_LINES;

						ui.updateDiskOpFileList = true;
					}
				}
				else if (editor.currMode == MODE_IDLE || editor.currMode == MODE_EDIT)
				{
					modSetPos(DONT_SET_ORDER, song->currRow + 16);
				}
			}
		}
		break;

		case SDL_SCANCODE_LEFT:
		{
			if (ui.editTextFlag)
			{
				if (keyb.delayCounter >= KEYB_REPEAT_DELAY)
				{
					if (keyb.repeatCounter >= 3)
					{
						keyb.repeatCounter = 0;
						editTextPrevChar();
					}
				}
				else
				{
					keyb.delayCounter++;
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
						if (song->currPos > 0)
							modSetPos(song->currPos - 1, DONT_SET_ROW);
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
			if (ui.editTextFlag)
			{
				if (keyb.delayCounter >= KEYB_REPEAT_DELAY)
				{
					if (keyb.repeatCounter >= 3)
					{
						keyb.repeatCounter = 0;
						editTextNextChar();
					}
				}
				else
				{
					keyb.delayCounter++;
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
						if (song->currPos < 126)
							modSetPos(song->currPos + 1, DONT_SET_ROW);
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
			if (ui.diskOpScreenShown)
			{
				if (keyb.repeatCounter >= 1)
				{
					keyb.repeatCounter = 0;

					diskop.scrollOffset--;
					if (mouse.rightButtonPressed) // PT quirk: right mouse button speeds up scrolling even on keyb UP/DOWN
						diskop.scrollOffset -= 3;

					if (diskop.scrollOffset < 0)
						diskop.scrollOffset = 0;

					ui.updateDiskOpFileList = true;
				}
			}
			else if (ui.posEdScreenShown)
			{
				if (keyb.repeatCounter >= 3)
				{
					keyb.repeatCounter = 0;
					posEdScrollUp();
				}
			}
			else if (!ui.samplerScreenShown)
			{
				if (editor.currMode != MODE_PLAY && editor.currMode != MODE_RECORD)
				{
					uint8_t repeatNum = 6;
					if (keyb.leftAltPressed)
						repeatNum = 1;
					else if (keyb.shiftPressed)
						repeatNum = 3;

					if (keyb.repeatCounter >= repeatNum)
					{
						keyb.repeatCounter = 0;
						modSetPos(DONT_SET_ORDER, (song->currRow - 1) & 63);
					}
				}
			}
		}
		break;

		case SDL_SCANCODE_DOWN:
		{
			if (ui.diskOpScreenShown)
			{
				if (keyb.repeatCounter >= 1)
				{
					keyb.repeatCounter = 0;

					if (diskop.numEntries > DISKOP_LINES)
					{
						diskop.scrollOffset++;
						if (mouse.rightButtonPressed) // PT quirk: right mouse button speeds up scrolling even on keyb UP/DOWN
							diskop.scrollOffset += 3;

						if (diskop.scrollOffset > diskop.numEntries-DISKOP_LINES)
							diskop.scrollOffset = diskop.numEntries-DISKOP_LINES;

						ui.updateDiskOpFileList = true;
					}
				}
			}
			else if (ui.posEdScreenShown)
			{
				if (keyb.repeatCounter >= 3)
				{
					keyb.repeatCounter = 0;
					posEdScrollDown();
				}
			}
			else if (!ui.samplerScreenShown)
			{
				if (editor.currMode != MODE_PLAY && editor.currMode != MODE_RECORD)
				{
					uint8_t repeatNum = 6;
					if (keyb.leftAltPressed)
						repeatNum = 1;
					else if (keyb.shiftPressed)
						repeatNum = 3;

					if (keyb.repeatCounter >= repeatNum)
					{
						keyb.repeatCounter = 0;
						modSetPos(DONT_SET_ORDER, (song->currRow + 1) & 63);
					}
				}
			}
		}
		break;

		case SDL_SCANCODE_BACKSPACE:
		{
			if (ui.editTextFlag)
			{
				// only repeat backspace while editing texts

				if (keyb.delayCounter >= KEYB_REPEAT_DELAY)
				{
					if (keyb.repeatCounter >= 3)
					{
						keyb.repeatCounter = 0;
						keyDownHandler(scancode, 0);
					}
				}
				else
				{
					keyb.delayCounter++;
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

	keyb.repeatFrac += video.amigaVblankDelta;  // 0.52 fixed-point
	if (keyb.repeatFrac > 1ULL<<52)
	{
		keyb.repeatFrac &= (1ULL<<52)-1;
		keyb.repeatCounter++;
	}
}

static void swapChannel(uint8_t srcCh, uint8_t dstCh)
{
	if (srcCh == dstCh)
		return;

	for (int32_t i = 0; i < MOD_ROWS; i++)
	{
		note_t *noteSrc = &song->patterns[song->currPattern][(i * PAULA_VOICES) + dstCh];
		note_t noteTmp = song->patterns[song->currPattern][(i * PAULA_VOICES) + srcCh];

		song->patterns[song->currPattern][(i * PAULA_VOICES) + srcCh] = *noteSrc;
		*noteSrc = noteTmp;
	}

	updateWindowTitle(MOD_IS_MODIFIED);
	ui.updatePatternData = true;
}

static bool handleGeneralModes(SDL_Scancode scancode)
{
	// if MOD2WAV is ongoing, only check for ESC key
	if (editor.mod2WavOngoing)
	{
		if (scancode == SDL_SCANCODE_ESCAPE)
			editor.abortMod2Wav = true;

		return false; // don't handle other keys
	}

	// SAMPLER SCREEN (volume box)
	if (ui.samplerVolBoxShown && !ui.editTextFlag && scancode == SDL_SCANCODE_ESCAPE)
	{
		ui.samplerVolBoxShown = false;
		removeSamplerVolBox();
		return false;
	}

	// SAMPLER SCREEN (filters box)
	if (ui.samplerFiltersBoxShown && !ui.editTextFlag && scancode == SDL_SCANCODE_ESCAPE)
	{
		ui.samplerFiltersBoxShown = false;
		removeSamplerFiltersBox();
		return false;
	}

	// SAMPLER SCREEN (sampling box)
	if (ui.samplingBoxShown)
	{
		if (audio.isSampling)
		{
			stopSampling();
			return false;
		}

		if (scancode == SDL_SCANCODE_F1)
			editor.keyOctave = OCTAVE_LOW;
		else if (scancode == SDL_SCANCODE_F2)
			editor.keyOctave = OCTAVE_HIGH;

		if (ui.changingSamplingNote)
		{
			if (scancode == SDL_SCANCODE_ESCAPE)
			{
				ui.changingSamplingNote = false;
				setPrevStatusMessage();
				pointerSetPreviousMode();
			}

			int8_t rawKey = keyToNote(scancode);
			if (rawKey >= 0)
			{
				ui.changingSamplingNote = false;

				setSamplingNote(rawKey);

				setPrevStatusMessage();
				pointerSetPreviousMode();
			}

			return false;
		}
		else
		{
			if (keyb.leftCtrlPressed)
			{
				if (scancode == SDL_SCANCODE_LEFT)
					samplingSampleNumDown();
				else if (scancode == SDL_SCANCODE_RIGHT)
					samplingSampleNumUp();
			}
			else
			{
				if (scancode == SDL_SCANCODE_SPACE)
					turnOffVoices();
				else
					handleSampleJamming(scancode);
			}
		}

		if (!ui.editTextFlag && scancode == SDL_SCANCODE_ESCAPE)
		{
			ui.samplingBoxShown = false;
			removeSamplingBox();
		}

		return false;
	}

	// EDIT OP. SCREEN #3
	if (editor.mixFlag && scancode == SDL_SCANCODE_ESCAPE)
	{
		leaveTextEditMode(EDIT_TEXT_UPDATE);
		editor.mixFlag = false;
		ui.updateMixText = true;
		return false;
	}

	// EDIT OP. SCREEN #4
	if (ui.changingChordNote)
	{
		if (scancode == SDL_SCANCODE_ESCAPE)
		{
			ui.changingChordNote = false;
			setPrevStatusMessage();
			pointerSetPreviousMode();
			return false;
		}

		if (scancode == SDL_SCANCODE_F1)
			editor.keyOctave = OCTAVE_LOW;
		else if (scancode == SDL_SCANCODE_F2)
			editor.keyOctave = OCTAVE_HIGH;

		int8_t rawKey = keyToNote(scancode);
		if (rawKey >= 0)
		{
			if (ui.changingChordNote == 1)
			{
				editor.note1 = rawKey;
				ui.updateChordNote1Text = true;
			}
			else if (ui.changingChordNote == 2)
			{
				editor.note2 = rawKey;
				ui.updateChordNote2Text = true;
			}
			else if (ui.changingChordNote == 3)
			{
				editor.note3 = rawKey;
				ui.updateChordNote3Text = true;
			}
			else if (ui.changingChordNote == 4)
			{
				editor.note4 = rawKey;
				ui.updateChordNote4Text = true;
			}

			ui.changingChordNote = false;
			recalcChordLength();

			setPrevStatusMessage();
			pointerSetPreviousMode();
		}

		return false;
	}

	// CHANGE DRUMPAD NOTE
	if (ui.changingDrumPadNote)
	{
		if (scancode == SDL_SCANCODE_ESCAPE)
		{
			ui.changingDrumPadNote = false;
			setPrevStatusMessage();
			pointerSetPreviousMode();
			return false;
		}

		if (scancode == SDL_SCANCODE_F1)
			editor.keyOctave = OCTAVE_LOW;
		else if (scancode == SDL_SCANCODE_F2)
			editor.keyOctave = OCTAVE_HIGH;

		int8_t rawKey = keyToNote(scancode);
		if (rawKey >= 0)
		{
			pNoteTable[editor.currSample] = rawKey;
			ui.changingDrumPadNote = false;
			setPrevStatusMessage();
			pointerSetPreviousMode();
		}

		return false;
	}

	// SAMPLER SCREEN
	if (ui.changingSmpResample)
	{
		if (scancode == SDL_SCANCODE_ESCAPE)
		{
			ui.changingSmpResample = false;
			ui.updateResampleNote = true;
			setPrevStatusMessage();
			pointerSetPreviousMode();
			return false;
		}

		if (scancode == SDL_SCANCODE_F1)
			editor.keyOctave = OCTAVE_LOW;
		else if (scancode == SDL_SCANCODE_F2)
			editor.keyOctave = OCTAVE_HIGH;

		int8_t rawKey = keyToNote(scancode);
		if (rawKey >= 0)
		{
			editor.resampleNote = rawKey;
			ui.changingSmpResample = false;
			ui.updateResampleNote = true;
			setPrevStatusMessage();
			pointerSetPreviousMode();
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
				swapChannel(0, cursor.channel);
				editor.swapChannelFlag = false;

				pointerSetPreviousMode();
				setPrevStatusMessage();
			}
			break;

			case SDL_SCANCODE_2:
			{
				swapChannel(1, cursor.channel);
				editor.swapChannelFlag = false;

				pointerSetPreviousMode();
				setPrevStatusMessage();
			}
			break;

			case SDL_SCANCODE_3:
			{
				swapChannel(2, cursor.channel);
				editor.swapChannelFlag = false;

				pointerSetPreviousMode();
				setPrevStatusMessage();
			}
			break;

			case SDL_SCANCODE_4:
			{
				swapChannel(3, cursor.channel);
				editor.swapChannelFlag = false;

				pointerSetPreviousMode();
				setPrevStatusMessage();
			}
			break;

			default: break;
		}

		return false;
	}

	return true;
}
