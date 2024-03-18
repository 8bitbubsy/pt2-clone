#include <stdint.h>
#include <stdbool.h>
#include "pt2_textout.h"
#include "pt2_visuals.h"
#include "pt2_mouse.h"
#include "pt2_structs.h"
#include "pt2_visuals_sync.h"
#include "pt2_keyboard.h"
#include "pt2_diskop.h"
#include "pt2_mod2wav.h"
#include "pt2_pat2smp.h"
#include "pt2_askbox.h"
#include "pt2_posed.h"

#define INIT_BUTTONS(num, abortRetVal, defRetVal) \
d.numButtons = num; \
d.abortReturnValue = abortRetVal; \
d.defaultReturnValue = defRetVal;

#define SET_DIALOG(xx, yy, ww, hh) \
d.x = xx; \
d.y = yy; \
d.w = ww; \
d.h = hh;

#define SET_BUTTON(i, invert, label, key, retVal, xx1, xx2, yy1, yy2) \
strcpy(d.buttons[i].text, label); \
d.buttons[i].repeat = false; \
d.buttons[i].inverted = invert; \
d.buttons[i].callback = NULL; \
d.buttons[i].keyBinding = key; \
d.buttons[i].returnValue = retVal; \
d.buttons[i].clickableWidth = 0; \
d.buttons[i].x1 = xx1; \
d.buttons[i].x2 = xx2; \
d.buttons[i].y1 = yy1; \
d.buttons[i].y2 = yy2;

#define SET_CALLBACK_BUTTON(i, label, func, clickWidth, xx1, xx2, yy1, yy2, doRepeat) \
strcpy(d.buttons[i].text, label); \
d.buttons[i].repeat = true; \
d.buttons[i].inverted = false; \
d.buttons[i].callback = func; \
d.buttons[i].clickableWidth = clickWidth; \
d.buttons[i].x1 = xx1; \
d.buttons[i].x2 = xx2; \
d.buttons[i].y1 = yy1; \
d.buttons[i].y2 = yy2;

typedef struct choice_t
{
	void (*callback)(void);
	bool inverted, repeat;
	int16_t x1, y1, x2, y2, clickableWidth;
	SDL_Keycode keyBinding;
	int16_t returnValue;
	char text[32];
} button_t;

typedef struct dialog_t
{
	bool bigDialog;
	int16_t x, y, w, h, numButtons, defaultReturnValue, abortReturnValue;
	button_t buttons[16];
} dialog_t;

typedef struct askBoxData_t // for thread-safe version of askBox()
{
	volatile bool active;
	uint32_t dialogType, returnValue;
	const char *statusText;
} askBoxData_t;

static int32_t buttonToRepeat = -1;
static askBoxData_t askBoxData;

static void drawDialog(dialog_t *d)
{
	// render dialog
	drawFramework3(d->x, d->y, d->w, d->h);

	// render buttons
	for (int32_t i = 0; i < d->numButtons; i++)
	{
		button_t *b = &d->buttons[i];
		if (b->inverted)
			drawButton2(b->x1, b->y1, (b->x2 - b->x1) + 1, (b->y2 - b->y1) + 1, b->text);
		else
			drawButton1(b->x1, b->y1, (b->x2 - b->x1) + 1, (b->y2 - b->y1) + 1, b->text);
	}
}

void removeAskBox(void)
{
	ui.disableVisualizer = false;
	ui.askBoxShown = false;

	if (ui.diskOpScreenShown)
	{
		renderDiskOpScreen(); // also sets update flags
		updateDiskOp(); // redraw requested updates
	}
	else if (ui.posEdScreenShown)
	{
		renderPosEdScreen(); // also sets update flags
		updatePosEd(); // redraw requested updates
	}
	else if (ui.editOpScreenShown)
	{
		renderEditOpScreen(); // also sets update flags
		updateEditOp(); // redraw requested updates
	}
	else if (ui.aboutScreenShown)
	{
		renderAboutScreen();
	}
	else
	{
		if (ui.visualizerMode == VISUAL_QUADRASCOPE)
			renderQuadrascopeBg();
		else if (ui.visualizerMode == VISUAL_SPECTRUM)
			renderSpectrumAnalyzerBg();

		updateVisualizer(); // will draw one frame of the visualizer in use
	}
}

void handleThreadedAskBox(void) // rain from main input/video loop
{
	if (askBoxData.active)
	{
		askBoxData.returnValue = askBox(askBoxData.dialogType, askBoxData.statusText);
		askBoxData.active = false;
	}
}

uint32_t askBoxThreadSafe(uint32_t dialogType, const char *statusText)
{
	if (!editor.mainLoopOngoing)
		return 0; // main loop was not even started yet, bail out.

	// block multiple calls before they are completed (for safety)
	while (askBoxData.active)
		SDL_Delay(1000 / VBLANK_HZ); // accuracy is not important here

	askBoxData.dialogType = dialogType;
	askBoxData.statusText = statusText;
	askBoxData.active = true;

	while (askBoxData.active)
		SDL_Delay(1000 / VBLANK_HZ); // accuracy is not important here

	return askBoxData.returnValue;
}

uint32_t askBox(uint32_t dialogType, const char *statusText)
{
	uint32_t returnValue = 0;
	dialog_t d;

	assert(dialogType < ASKBOX_NUM);
	if (dialogType >= ASKBOX_NUM)
		return ASKBOX_NO;

	editor.errorMsgActive = false;
	editor.errorMsgBlock = false;
	editor.errorMsgCounter = 0;

	editor.blockMarkFlag = false;
	ui.introTextShown = false; // kludge :-)
	ui.disableVisualizer = true;
	ui.askBoxShown = true;

	SDL_EventState(SDL_DROPFILE, SDL_DISABLE);

	// setup dialog based on type
	switch (dialogType)
	{
		default:
		case ASKBOX_YES_NO:
		{
			// input: x, y, width, height
			SET_DIALOG(160, 51, 104, 39);

			// input: number of buttons, abort return-value (esc / right mouse button), default return-value (enter/return)
			INIT_BUTTONS(2, ASKBOX_NO, ASKBOX_YES);

			// input: button number, inverted type, keybinding, return-value, x1, x2, y1, y2
			SET_BUTTON(0, true, "YES", SDLK_y, ASKBOX_YES, 171, 196, 71, 81);
			SET_BUTTON(1, true, "NO",  SDLK_n, ASKBOX_NO,  234, 252, 71, 81);

			drawDialog(&d);

			textOut2(166, 59, "ARE YOU SURE?");
		}
		break;

		case ASKBOX_CLEAR:
		{
			SET_DIALOG(160, 51, 104, 39);
			INIT_BUTTONS(4, ASKBOX_CLEAR_CANCEL, ASKBOX_CLEAR_CANCEL);
			SET_BUTTON(0, true, "SONG",    SDLK_o, ASKBOX_CLEAR_SONG,    166, 198, 57, 67);
			SET_BUTTON(1, true, "SAMPLES", SDLK_s, ASKBOX_CLEAR_SAMPLES, 204, 257, 57, 67);
			SET_BUTTON(2, true, "ALL",     SDLK_a, ASKBOX_CLEAR_ALL,     166, 198, 73, 83);
			SET_BUTTON(3, true, "CANCEL",  SDLK_c, ASKBOX_CLEAR_CANCEL,  204, 257, 73, 83);
			drawDialog(&d);
		}
		break;

		case ASKBOX_PAT2SMP:
		{
			SET_DIALOG(120, 44, 200, 55);
			INIT_BUTTONS(10, 0, 1);
	
			SET_BUTTON(0, false, "RENDER TO SAMPLE", SDLK_r, 1, 124, 246, 84, 94);
			SET_BUTTON(1, false, "EXIT",             SDLK_e, 0, 258, 315, 84, 94);

			// input: button number, text, keybinding, return-value, x1, x2, y1, y2, repeat button
			SET_CALLBACK_BUTTON(2, ARROW_UP_STR,   pat2SmpNoteUp,       0, 191, 201, 48, 58, true);
			SET_CALLBACK_BUTTON(3, ARROW_DOWN_STR, pat2SmpNoteDown,     0, 202, 212, 48, 58, true);
			SET_CALLBACK_BUTTON(4, ARROW_UP_STR,   pat2SmpStartRowUp,   0, 294, 304, 48, 58, true);
			SET_CALLBACK_BUTTON(5, ARROW_DOWN_STR, pat2SmpStartRowDown, 0, 305, 315, 48, 58, true);
			SET_CALLBACK_BUTTON(6, ARROW_UP_STR,   pat2SmpFinetuneUp,   0, 191, 201, 59, 69, true);
			SET_CALLBACK_BUTTON(7, ARROW_DOWN_STR, pat2SmpFinetuneDown, 0, 202, 212, 59, 69, true);
			SET_CALLBACK_BUTTON(8, ARROW_UP_STR,   pat2SmpRowsUp,       0, 294, 304, 59, 69, true);
			SET_CALLBACK_BUTTON(9, ARROW_DOWN_STR, pat2SmpRowsDown,     0, 305, 315, 59, 69, true);

			drawDialog(&d);

			drawFramework1(163, 48, 28, 11);
			drawFramework1(274, 48, 20, 11);
			drawFramework1(163, 59, 28, 11);
			drawFramework1(274, 59, 20, 11);

			pat2SmpDrawNote();
			pat2SmpDrawStartRow();
			pat2SmpDrawRows();
			pat2SmpDrawFinetune();
			pat2SmpCalculateFreq();

			textOut2(127, 50, "NOTE");
			textOut2(217, 50, "ROW BEG.");
			textOut2(124, 61, "FTUNE");
			textOut2(223, 61, "# ROWS");
			textOut2(124, 74, "FREQ.");
		}
		break;

		case ASKBOX_DOWNSAMPLE: // sample loader (AIFF/WAV w/ rate >22kHz)
		{
			SET_DIALOG(120, 44, 200, 55);
			INIT_BUTTONS(2, ASKBOX_NO, ASKBOX_YES);
			SET_BUTTON(0, true, "YES", SDLK_y, ASKBOX_YES, 179, 204, 83, 93);
			SET_BUTTON(1, true, "NO",  SDLK_n, ASKBOX_NO,  242, 260, 83, 93);
			drawDialog(&d);

			textOut2(133, 49, "THE SAMPLE'S FREQUENCY IS");
			textOut2(154, 57, "HIGH (ABOVE 22KHZ).");
			textOut2(133, 65, "DO YOU WANT TO DOWNSAMPLE");
			textOut2(156, 73, "BEFORE LOADING IT?");
		}
		break;

		case ASKBOX_MOD2WAV:
		{
			SET_DIALOG(138, 44, 165, 55);
			INIT_BUTTONS(7, ASKBOX_NO, ASKBOX_YES);

			SET_BUTTON(0, false, "RENDER TO WAV", SDLK_r, ASKBOX_YES, 142, 246, 84, 94);
			SET_BUTTON(1, false, "EXIT",          SDLK_e, ASKBOX_NO,  257, 298, 84, 94);

			SET_CALLBACK_BUTTON(2, " ",            toggleMod2WavFadeout, 152, 142, 151, 48, 56, false);
			SET_CALLBACK_BUTTON(3, ARROW_UP_STR,   mod2WavFadeoutUp,       0, 277, 287, 59, 69, true);
			SET_CALLBACK_BUTTON(4, ARROW_DOWN_STR, mod2WavFadeoutDown,     0, 288, 298, 59, 69, true);
			SET_CALLBACK_BUTTON(5, ARROW_UP_STR,   mod2WavLoopCountUp,     0, 277, 287, 70, 80, true);
			SET_CALLBACK_BUTTON(6, ARROW_DOWN_STR, mod2WavLoopCountDown,   0, 288, 298, 70, 80, true);

			drawDialog(&d);

			drawFramework1(257, 59, 20, 11);
			drawFramework1(257, 70, 20, 11);

			textOut2(154, 50, "FADE OUT END OF SONG");
			textOut2(142, 62, "FADEOUT SECONDS");
			textOut2(142, 73, "SONG LOOP COUNT");

			mod2WavDrawFadeoutToggle();
			mod2WavDrawFadeoutSeconds();
			mod2WavDrawLoopCount();
		}
	}

	pointerSetMode(POINTER_MODE_MSG1, NO_CARRY);
	if (statusText != NULL)
		setStatusMessage(statusText, NO_CARRY);

	mouse.leftButtonPressed = false;
	mouse.buttonWaiting = false;
	mouse.buttonWaitCounter = 0;
	mouse.repeatCounter = 0;

	// main loop here
	// XXX: if you change anything in the main rendering loop, make sure it goes in here too (if needed)

	bool loopRunning = true;
	while (loopRunning)
	{
		SDL_Event event;

		beginFPSCounter();
		sinkVisualizerBars();
		updateChannelSyncBuffer();
		readMouseXY();

		while (SDL_PollEvent(&event))
		{
			if (event.type == SDL_WINDOWEVENT)
			{
				if (event.window.event == SDL_WINDOWEVENT_HIDDEN)
					video.windowHidden = true;
				else if (event.window.event == SDL_WINDOWEVENT_SHOWN)
					video.windowHidden = false;

				// reset vblank end time if we minimize window
				if (event.window.event == SDL_WINDOWEVENT_MINIMIZED || event.window.event == SDL_WINDOWEVENT_FOCUS_LOST)
					hpc_ResetCounters(&video.vblankHpc);
			}
			else if (event.type == SDL_KEYDOWN)
			{
				SDL_Keycode key = event.key.keysym.sym;

				// abort (escape key)
				if (key == SDLK_ESCAPE)
				{
					returnValue = d.abortReturnValue;

					loopRunning = false;
					break;
				}

				// default action (return/enter key)
				if (key == SDLK_RETURN || key == SDLK_KP_ENTER)
				{
					returnValue = d.defaultReturnValue;

					loopRunning = false;
					break;
				}

				// test keybindings
				for (int32_t i = 0; i < d.numButtons; i++)
				{
					button_t *b = &d.buttons[i];
					if (b->callback == NULL && key == b->keyBinding)
					{
						returnValue = b->returnValue;

						loopRunning = false;
						break;
					}
				}
			}
			else if (event.type == SDL_MOUSEBUTTONDOWN)
			{
				if (event.button.button == SDL_BUTTON_LEFT)
				{
					mouse.leftButtonPressed = true;
					mouse.buttonWaiting = true;
					mouse.repeatCounter = 0;
				}

				// abort (right mouse button)
				if (event.button.button == SDL_BUTTON_RIGHT)
				{
					returnValue = d.abortReturnValue;

					loopRunning = false;
					break;
				}

				// test buttons
				if (event.button.button == SDL_BUTTON_LEFT)
				{
					for (int32_t i = 0; i < d.numButtons; i++)
					{
						button_t *b = &d.buttons[i];

						const int32_t x2 = (b->clickableWidth > 0) ? (b->x1 + b->clickableWidth) : b->x2;
						if (mouse.x >= b->x1 && mouse.x <= x2 && mouse.y >= b->y1 && mouse.y <= b->y2)
						{
							if (b->callback != NULL)
							{
								b->callback();
								if (b->repeat)
								{
									buttonToRepeat = i;
								}
							}
							else
							{
								returnValue = b->returnValue;

								loopRunning = false;
								break;
							}
						}
					}
				}
			}
			else if (event.type == SDL_MOUSEBUTTONUP)
			{
#if defined __APPLE__ && defined __aarch64__
				armMacGhostMouseCursorFix();
#endif
				mouse.buttonWaitCounter = 0;
				mouse.buttonWaiting = false;
				buttonToRepeat = -1;

				if (event.button.button == SDL_BUTTON_LEFT)
					mouse.leftButtonPressed = false;
			}
#if defined __APPLE__ && defined __aarch64__
			else if (event.type == SDL_MOUSEMOTION)
			{
				armMacGhostMouseCursorFix();
			}
#endif
		}

		if (!mouse.buttonWaiting && buttonToRepeat > -1)
		{
			button_t *b = &d.buttons[buttonToRepeat];

			if (++mouse.repeatCounter >= 3)
			{
				const int32_t x2 = (b->clickableWidth > 0) ? (b->x1 + b->clickableWidth) : b->x2;
				if (mouse.x >= b->x1 && mouse.x <= x2 && mouse.y >= b->y1 && mouse.y <= b->y2)
				{
					if (b->callback != NULL)
						b->callback();
				}

				mouse.repeatCounter = 0;
			}
		}

		updateMouseCounters();
		renderFrame2();
		flipFrame();
		endFPSCounter();
	}

	mouse.leftButtonPressed = false;
	mouse.buttonWaiting = false;
	mouse.buttonWaitCounter = 0;
	mouse.repeatCounter = 0;

	removeAskBox();
	pointerSetPreviousMode();
	setPrevStatusMessage();

	SDL_EventState(SDL_DROPFILE, SDL_ENABLE);
	return returnValue;
}
