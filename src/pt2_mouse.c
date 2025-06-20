// for finding memory leaks in debug mode with Visual Studio 
#if defined _DEBUG && defined _MSC_VER
#include <crtdbg.h>
#endif

#include <ctype.h>
#include <math.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#ifndef _WIN32
#include <unistd.h>
#endif
#include <stdio.h>
#include "pt2_helpers.h"
#include "pt2_diskop.h"
#include "pt2_sampler.h"
#include "pt2_module_saver.h"
#include "pt2_edit.h"
#include "pt2_sample_saver.h"
#include "pt2_visuals.h"
#include "pt2_tables.h"
#include "pt2_audio.h"
#include "pt2_textout.h"
#include "pt2_keyboard.h"
#include "pt2_config.h"
#include "pt2_bmp.h"
#include "pt2_sampling.h"
#include "pt2_chordmaker.h"
#include "pt2_pat2smp.h"
#include "pt2_mod2wav.h"
#include "pt2_askbox.h"
#include "pt2_replayer.h"
#include "pt2_posed.h"
#include "pt2_textedit.h"

SDL_Cursor *cursors[NUM_CURSORS]; // globalized

static int32_t checkGUIButtons(void);
static bool handleRightMouseButton(void);
static bool handleLeftMouseButton(void);
static bool handleGUIButtons(int32_t button);
static void handleRepeatedGUIButtons(void);
static void handleRepeatedSamplerFilterButtons(void);

#if defined __APPLE__ && defined __aarch64__
void armMacGhostMouseCursorFix(void)
{
	/* M E G A K L U D G E:
	** The mouse cursor can sometimes change back to OS stock
	** (or show both stock and custom mouse) on Macs with a notch
	** (ARM based) in fullscreen mode. Weird, right?!
	**
	** XXX: Can this cause stuttering or performance issues?
	*/
	if (video.fullscreen)
		SDL_SetCursor(NULL); // forces redraw
}
#endif

void sampleUpButton(void)
{
	if (editor.sampleZero)
	{
		editor.sampleZero = false;
		editor.currSample = 0;
	}
	else if (editor.currSample < 30)
	{
		editor.currSample++;
	}

	updateCurrSample();
}

void sampleDownButton(void)
{
	if (!editor.sampleZero && editor.currSample > 0)
	{
		editor.currSample--;
		updateCurrSample();
	}
}

static void pointerSetColor(uint8_t cursorColorIndex)
{
	assert(cursorColorIndex <= 5);

	video.palette[PAL_MOUSE_1] = cursorColors[cursorColorIndex][0];
	video.palette[PAL_MOUSE_2] = cursorColors[cursorColorIndex][1];
	video.palette[PAL_MOUSE_3] = cursorColors[cursorColorIndex][2];

	if (config.hwMouse)
		setSystemCursor(cursors[cursorColorIndex]);
}

void updatePointerColor(void)
{
	switch (ui.pointerMode)
	{
		case POINTER_MODE_IDLE:   pointerSetColor(POINTER_GRAY);   break;
		case POINTER_MODE_PLAY:   pointerSetColor(POINTER_YELLOW); break;
		case POINTER_MODE_EDIT:   pointerSetColor(POINTER_BLUE);   break;
		case POINTER_MODE_RECORD: pointerSetColor(POINTER_BLUE);   break;
		case POINTER_MODE_MSG1:   pointerSetColor(POINTER_PURPLE); break;
		case POINTER_MODE_MSG2:   pointerSetColor(POINTER_GREEN);  break;
		default: break;
	}
}

void pointerSetMode(uint8_t pointerMode, bool carry)
{
	assert(pointerMode <= 5);

	ui.pointerMode = pointerMode;
	if (carry)
		ui.previousPointerMode = ui.pointerMode;

	updatePointerColor();
}

void pointerSetModeThreadSafe(uint8_t pointerMode, bool carry)
{
	assert(pointerMode <= 5);

	ui.pointerMode = pointerMode;
	if (carry)
		ui.previousPointerMode = ui.pointerMode;

	// in hardware mouse mode, the pointer color can only be changed from the main thread
	if (config.hwMouse)
		mouse.updatePointerColorFlag = true;
	else
		updatePointerColor();
}

void pointerSetPreviousMode(void)
{
	if (ui.editTextFlag || ui.askBoxShown)
		pointerSetMode(POINTER_MODE_MSG1, NO_CARRY);
	else
		pointerSetMode(ui.previousPointerMode, NO_CARRY);
}

void setMsgPointer(void)
{
	pointerSetMode(POINTER_MODE_MSG2, false);
}

void setErrPointer(void)
{
	pointerSetColor(POINTER_RED);
}

bool setSystemCursor(SDL_Cursor *cur)
{
	if (cur == NULL)
	{
		SDL_SetCursor(SDL_GetDefaultCursor());
		return false;
	}

	SDL_SetCursor(cur);
	return true;
}

void freeMouseCursors(void)
{
	SDL_SetCursor(SDL_GetDefaultCursor());
	for (uint32_t i = 0; i < NUM_CURSORS; i++)
	{
		if (cursors[i] != NULL)
		{
			SDL_FreeCursor(cursors[i]);
			cursors[i] = NULL;
		}
	}
}

bool createMouseCursors(void) // creates scaled SDL surfaces for current mouse pointer shape
{
	freeMouseCursors();

	uint32_t scaleFactor = video.mouseCursorUpscaleFactor;
	if (scaleFactor > 8) // just in case
		scaleFactor = 8;

	for (uint32_t i = 0; i < NUM_CURSORS; i++)
	{
		SDL_Surface *surface = SDL_CreateRGBSurface(0, POINTER_W*scaleFactor, POINTER_H*scaleFactor, 32, 0, 0, 0, 0);
		if (surface == NULL)
		{
			freeMouseCursors();
			config.hwMouse = false; // enable software mouse
			return false;
		}

		uint32_t color1 = cursorColors[i][0];
		uint32_t color2 = cursorColors[i][1];
		uint32_t color3 = cursorColors[i][2];
		uint32_t colorkey = 0x12345678;

		color1 = SDL_MapRGB(surface->format, R24(color1), G24(color1), B24(color1));
		color2 = SDL_MapRGB(surface->format, R24(color2), G24(color2), B24(color2));
		color3 = SDL_MapRGB(surface->format, R24(color3), G24(color3), B24(color3));
		colorkey = SDL_MapRGB(surface->format, R24(colorkey), G24(colorkey), B24(colorkey));

		SDL_SetSurfaceBlendMode(surface, SDL_BLENDMODE_NONE);
		SDL_SetColorKey(surface, SDL_TRUE, colorkey);
		SDL_SetSurfaceRLE(surface, SDL_TRUE);

		const uint8_t *srcPixels8 = mousePointerBMP;
		SDL_LockSurface(surface);

		uint32_t *dstPixels32 = (uint32_t *)surface->pixels;
		for (int32_t k = 0; k < surface->w*surface->h; k++) // fill surface with colorkey pixels
			dstPixels32[k] = colorkey;

		// blit upscaled cursor to surface
		for (uint32_t y = 0; y < POINTER_H; y++)
		{
			uint32_t *outX = &dstPixels32[(y * scaleFactor) * surface->w];
			for (uint32_t yScale = 0; yScale < scaleFactor; yScale++)
			{
				for (uint32_t x = 0; x < POINTER_W; x++)
				{
					uint8_t srcPix = srcPixels8[(y * POINTER_W) + x];
					if (srcPix != PAL_COLORKEY)
					{
						uint32_t pixel = colorkey; // make compiler happy

						     if (srcPix == PAL_MOUSE_1) pixel = color1;
						else if (srcPix == PAL_MOUSE_2) pixel = color2;
						else if (srcPix == PAL_MOUSE_3) pixel = color3;

						for (uint32_t xScale = 0; xScale < scaleFactor; xScale++)
							outX[xScale] = pixel;
					}

					outX += scaleFactor;
				}
			}
		}

		SDL_UnlockSurface(surface);

		cursors[i] = SDL_CreateColorCursor(surface, 0, 0);
		if (cursors[i] == NULL)
		{
			SDL_FreeSurface(surface);
			freeMouseCursors();
			config.hwMouse = false; // enable software mouse
			return false;
		}

		SDL_FreeSurface(surface);
	}

	pointerSetPreviousMode(); // this sets the appropriate the hardware cursor
	return true;
}

void updateMouseScaling(void)
{
	if (video.renderW > 0) video.dMouseXMul = (double)SCREEN_W / video.renderW;
	if (video.renderH > 0) video.dMouseYMul = (double)SCREEN_H / video.renderH;
}

void readMouseXY(void)
{
	int32_t mx, my, windowX, windowY;

	if (mouse.updatePointerColorFlag) // used when changing pointer color from other threads
	{
		mouse.updatePointerColorFlag = false;
		updatePointerColor();
	}

	if (mouse.setPosFlag)
	{
		if (!video.windowHidden)
			SDL_WarpMouseInWindow(video.window, mouse.setPosX, mouse.setPosY);

		mouse.setPosFlag = false;
		return;
	}

	if (video.fullscreen)
	{
		mouse.buttonState = SDL_GetMouseState(&mx, &my);

		mouse.absX = mx;
		mouse.absY = my;
	}
	else
	{
		mouse.buttonState = SDL_GetGlobalMouseState(&mx, &my);

		mouse.absX = mx;
		mouse.absY = my;

		// convert desktop coords to window coords
		SDL_GetWindowPosition(video.window, &windowX, &windowY);

		mx -= windowX;
		my -= windowY;
	}

	mouse.rawX = mx;
	mouse.rawY = my;

	if (video.fullscreen)
	{
		// if software mouse is enabled, warp mouse inside render space
		if (!config.hwMouse)
		{
			bool warpMouse = false;

			if (mx < video.renderX)
			{
				mx = video.renderX;
				warpMouse = true;
			}
			else if (mx >= video.renderX+video.renderW)
			{
				mx = (video.renderX + video.renderW) - 1;
				warpMouse = true;
			}

			if (my < video.renderY)
			{
				my = video.renderY;
				warpMouse = true;
			}
			else if (my >= video.renderY+video.renderH)
			{
				my = (video.renderY + video.renderH) - 1;
				warpMouse = true;
			}

			if (warpMouse)
				SDL_WarpMouseInWindow(video.window, mx, my);
		}

		// convert fullscreen coords to window (centered image) coords
		mx -= video.renderX;
		my -= video.renderY;
	}

	// kludge: this can happen and prevent buttons from pressing on the very first row of pixels
	if (mx == -1) mx = 0;
	if (my == -1) my = 0;

	// multiply coords by video upscaling factors
	mouse.x = (int32_t)floor(mx * video.dMouseXMul);
	mouse.y = (int32_t)floor(my * video.dMouseYMul);

	if (config.hwMouse)
	{
		// hardware mouse mode (OS)
		hideSprite(SPRITE_MOUSE_POINTER);
	}
	else
	{
		// software mouse mode (PT mouse)
		setSpritePos(SPRITE_MOUSE_POINTER, mouse.x, mouse.y);
	}
}

void mouseButtonUpHandler(uint8_t mouseButton)
{
	mouse.buttonWaitCounter = 0;
	mouse.buttonWaiting = false;

	if (mouseButton == SDL_BUTTON_LEFT)
	{
		mouse.leftButtonPressed = false;

		ui.forceSampleDrag = false;
		ui.forceVolDrag = false;
		ui.leftLoopPinMoving = false;
		ui.rightLoopPinMoving = false;
		ui.sampleMarkingPos = -1;

		switch (mouse.lastGUIButton)
		{
			case PTB_SLENGTHU:
			case PTB_SLENGTHD:
			{
				if (ui.samplerScreenShown)
					redrawSample();

				recalcChordLength();
				updateSamplePos();

				ui.updateSongSize = true;
			}
			break;

			case PTB_LENGTHU:
			case PTB_LENGTHD:
			case PTB_PATTERNU:
			case PTB_PATTERND:
			{
				ui.updateSongSize = true;
				if (ui.posEdScreenShown)
					ui.updatePosEd = true;
			}
			break;

			default:
				break;
		}

		mouse.lastGUIButton = -1;
		mouse.lastSmpFilterButton = -1;
		mouse.lastSamplingButton = -1;
	}

	if (mouseButton == SDL_BUTTON_RIGHT)
	{
		mouse.rightButtonPressed = false;
		ui.forceSampleEdit = false;
	}
}

void mouseButtonDownHandler(uint8_t mouseButton)
{
	if (mouseButton == SDL_BUTTON_LEFT)
	{
		mouse.leftButtonPressed = true;
		mouse.buttonWaiting = true;
	}

	if (mouseButton == SDL_BUTTON_RIGHT)
		mouse.rightButtonPressed = true;

	// when red mouse pointer (error), block further input for a while
	if (editor.errorMsgActive && editor.errorMsgBlock)
		return;

	if (handleRightMouseButton() || handleLeftMouseButton())
		return;

	handleTextEditing(mouseButton);
}

void handleGUIButtonRepeat(void)
{
	if (!mouse.leftButtonPressed)
	{
		// left mouse button released, stop repeating buttons
		mouse.repeatCounter = 0;
		return;
	}

	if (ui.samplerFiltersBoxShown)
	{
		handleRepeatedSamplerFilterButtons();
		return;
	}

	if (ui.samplingBoxShown)
	{
		handleRepeatedSamplingButtons();
		return;
	}

	if (mouse.lastGUIButton != checkGUIButtons()) // FIXME: This can potentially do a ton of iterations, bad design!
	{
		// only repeat the button that was first clicked (e.g. if you hold and move mouse to another button)
		mouse.repeatCounter = 0;
		return;
	}

	handleRepeatedGUIButtons();
	mouse.repeatCounter++;
}

static void edNote1UpButton(void)
{
	if (mouse.rightButtonPressed)
		editor.note1 += 12;
	else
		editor.note1++;

	if (editor.note1 > 36)
		editor.note1 = 36;

	ui.updateChordNote1Text = true;
	recalcChordLength();
}

static void edNote1DownButton(void)
{
	if (mouse.rightButtonPressed)
		editor.note1 -= 12;
	else
		editor.note1--;

	if (editor.note1 < 0)
		editor.note1 = 0;

	ui.updateChordNote1Text = true;
	recalcChordLength();
}

static void edNote2UpButton(void)
{
	if (mouse.rightButtonPressed)
		editor.note2 += 12;
	else
		editor.note2++;

	if (editor.note2 > 36)
		editor.note2 = 36;

	ui.updateChordNote2Text = true;
	recalcChordLength();
}

static void edNote2DownButton(void)
{
	if (mouse.rightButtonPressed)
		editor.note2 -= 12;
	else
		editor.note2--;

	if (editor.note2 < 0)
		editor.note2 = 0;

	ui.updateChordNote2Text = true;
	recalcChordLength();
}

static void edNote3UpButton(void)
{
	if (mouse.rightButtonPressed)
		editor.note3 += 12;
	else
		editor.note3++;

	if (editor.note3 > 36)
		editor.note3 = 36;

	ui.updateChordNote3Text = true;
	recalcChordLength();
}

static void edNote3DownButton(void)
{
	if (mouse.rightButtonPressed)
		editor.note3 -= 12;
	else
		editor.note3--;

	if (editor.note3 < 0)
		editor.note3 = 0;

	ui.updateChordNote3Text = true;
	recalcChordLength();
}

static void edNote4UpButton(void)
{
	if (mouse.rightButtonPressed)
		editor.note4 += 12;
	else
		editor.note4++;

	if (editor.note4 > 36)
		editor.note4 = 36;

	ui.updateChordNote4Text = true;
	recalcChordLength();
}

static void edNote4DownButton(void)
{
	if (mouse.rightButtonPressed)
		editor.note4 -= 12;
	else
		editor.note4--;

	if (editor.note4 < 0)
		editor.note4 = 0;

	ui.updateChordNote4Text = true;
	recalcChordLength();
}

static void edPosUpButton(bool fast)
{
	if (mouse.rightButtonPressed)
	{
		if (fast)
		{
			if (editor.samplePos <= config.maxSampleLength-64)
				editor.samplePos += 64;
			else
				editor.samplePos = config.maxSampleLength;
		}
		else
		{
			if (editor.samplePos <= config.maxSampleLength-16)
				editor.samplePos += 16;
			else
				editor.samplePos = config.maxSampleLength;
		}
	}
	else
	{
		if (fast)
		{
			if (editor.samplePos <= config.maxSampleLength-64)
				editor.samplePos += 64;
			else
				editor.samplePos = config.maxSampleLength;
		}
		else
		{
			if (editor.samplePos < config.maxSampleLength)
				editor.samplePos++;
			else
				editor.samplePos = config.maxSampleLength;
		}
	}

	if (editor.samplePos > song->samples[editor.currSample].length)
		editor.samplePos = song->samples[editor.currSample].length;

	ui.updatePosText = true;
}

static void edPosDownButton(bool fast)
{
	if (mouse.rightButtonPressed)
	{
		if (fast)
		{
			if (editor.samplePos > 64)
				editor.samplePos -= 64;
			else
				editor.samplePos = 0;
		}
		else
		{
			if (editor.samplePos > 16)
				editor.samplePos -= 16;
			else
				editor.samplePos = 0;
		}
	}
	else
	{
		if (fast)
		{
			if (editor.samplePos > 64)
				editor.samplePos -= 64;
			else
				editor.samplePos = 0;
		}
		else
		{
			if (editor.samplePos > 0)
				editor.samplePos--;
			else
				editor.samplePos = 0;
		}
	}

	ui.updatePosText = true;
}

static void edModUpButton(void)
{
	if (mouse.rightButtonPressed)
		editor.modulateSpeed += 10;
	else
		editor.modulateSpeed++;

	if (editor.modulateSpeed > 127)
		editor.modulateSpeed = 127;

	ui.updateModText = true;
}

static void edModDownButton(void)
{
	if (mouse.rightButtonPressed)
	{
		editor.modulateSpeed -= 10;
	}
	else
	{
		editor.modulateSpeed--;
	}

	if (editor.modulateSpeed < -128)
		editor.modulateSpeed = -128;

	ui.updateModText = true;
}

static void edVolUpButton(void)
{
	if (mouse.rightButtonPressed)
	{
		if (editor.sampleVol <= 999-10)
			editor.sampleVol += 10;
		else
			editor.sampleVol = 999;
	}
	else
	{
		if (editor.sampleVol < 999)
			editor.sampleVol++;
	}

	ui.updateVolText = true;
}

static void edVolDownButton(void)
{
	if (mouse.rightButtonPressed)
	{
		if (editor.sampleVol >= 10)
			editor.sampleVol -= 10;
		else
			editor.sampleVol = 0;
	}
	else
	{
		if (editor.sampleVol > 0)
			editor.sampleVol--;
	}

	ui.updateVolText = true;
}

static void sampleFineTuneUpButton(void)
{
	if (editor.sampleZero)
		return;

	int8_t finetune = song->samples[editor.currSample].fineTune & 0xF;
	if (finetune != 7)
		song->samples[editor.currSample].fineTune = (finetune + 1) & 0xF;

	if (mouse.rightButtonPressed)
		song->samples[editor.currSample].fineTune = 0;

	recalcChordLength();
	ui.updateCurrSampleFineTune = true;
}

static void sampleFineTuneDownButton(void)
{
	if (editor.sampleZero)
		return;

	int8_t finetune = song->samples[editor.currSample].fineTune & 0xF;
	if (finetune != 8)
		song->samples[editor.currSample].fineTune = (finetune - 1) & 0xF;

	if (mouse.rightButtonPressed)
		song->samples[editor.currSample].fineTune = 0;

	recalcChordLength();
	ui.updateCurrSampleFineTune = true;
}

static void sampleVolumeUpButton(void)
{
	if (editor.sampleZero)
		return;

	int8_t val = song->samples[editor.currSample].volume;

	if (mouse.rightButtonPressed)
		val += 16;
	else
		val++;

	if (val > 64)
		val = 64;

	song->samples[editor.currSample].volume = (uint8_t)val;
	ui.updateCurrSampleVolume = true;
}

static void sampleVolumeDownButton(void)
{
	if (editor.sampleZero)
		return;

	int8_t val = song->samples[editor.currSample].volume;

	if (mouse.rightButtonPressed)
		val -= 16;
	else
		val--;

	if (val < 0)
		val = 0;

	song->samples[editor.currSample].volume = (uint8_t)val;
	ui.updateCurrSampleVolume = true;
}

static void sampleLengthUpButton(bool fast)
{
	if (editor.sampleZero || song->samples[editor.currSample].length == config.maxSampleLength)
		return;

	int32_t val = song->samples[editor.currSample].length;
	if (mouse.rightButtonPressed)
	{
		if (fast)
			val += 64;
		else
			val += 16;
	}
	else
	{
		if (fast)
			val += 10;
		else
			val += 2;
	}

	if (val > config.maxSampleLength)
		val = config.maxSampleLength;

	song->samples[editor.currSample].length = val;
	ui.updateCurrSampleLength = true;
}

static void sampleLengthDownButton(bool fast)
{
	if (editor.sampleZero)
		return;

	moduleSample_t *s = &song->samples[editor.currSample];
	if (s->loopStart+s->loopLength > 2)
	{
		if (s->length == s->loopStart+s->loopLength)
			return;
	}
	else
	{
		if (s->length == 0)
			return;
	}

	int32_t val = song->samples[editor.currSample].length;
	if (mouse.rightButtonPressed)
	{
		if (fast)
			val -= 64;
		else
			val -= 16;
	}
	else
	{
		if (fast)
			val -= 10;
		else
			val -= 2;
	}

	if (val < 0)
		val = 0;

	if (s->loopStart+s->loopLength > 2)
	{
		if (val < s->loopStart+s->loopLength)
			val = s->loopStart+s->loopLength;
	}

	s->length = val;

	ui.updateCurrSampleLength = true;
}

static void sampleRepeatUpButton(bool fast)
{
	if (editor.sampleZero)
		return;

	int32_t val = song->samples[editor.currSample].loopStart;
	int32_t loopLen = song->samples[editor.currSample].loopLength;
	int32_t len = song->samples[editor.currSample].length;

	if (len == 0)
	{
		song->samples[editor.currSample].loopStart = 0;
		return;
	}

	if (mouse.rightButtonPressed)
	{
		if (fast)
			val += 64;
		else
			val += 16;
	}
	else
	{
		if (fast)
			val += 10;
		else
			val += 2;
	}

	if (val > len-loopLen)
		val = len-loopLen;

	song->samples[editor.currSample].loopStart = val;
	ui.updateCurrSampleRepeat = true;

	updatePaulaLoops();

	if (ui.samplerScreenShown)
		setLoopSprites();

	if (ui.editOpScreenShown && ui.editOpScreen == 3) // sample chord editor
		ui.updateChordLengthText = true;
}

static void sampleRepeatDownButton(bool fast)
{
	if (editor.sampleZero)
		return;

	int32_t val = song->samples[editor.currSample].loopStart;
	int32_t len = song->samples[editor.currSample].length;

	if (len == 0)
	{
		song->samples[editor.currSample].loopStart = 0;
		return;
	}

	if (mouse.rightButtonPressed)
	{
		if (fast)
			val -= 64;
		else
			val -= 16;
	}
	else
	{
		if (fast)
			val -= 10;
		else
			val -= 2;
	}

	if (val < 0)
		val = 0;

	song->samples[editor.currSample].loopStart = val;
	ui.updateCurrSampleRepeat = true;

	updatePaulaLoops();

	if (ui.samplerScreenShown)
		setLoopSprites();

	if (ui.editOpScreenShown && ui.editOpScreen == 3) // sample chord editor
		ui.updateChordLengthText = true;
}

static void sampleRepeatLengthUpButton(bool fast)
{
	if (editor.sampleZero)
		return;

	int32_t val = song->samples[editor.currSample].loopLength;
	int32_t loopStart = song->samples[editor.currSample].loopStart;
	int32_t len = song->samples[editor.currSample].length;

	if (len == 0)
	{
		song->samples[editor.currSample].loopLength = 2;
		return;
	}

	if (mouse.rightButtonPressed)
	{
		if (fast)
			val += 64;
		else
			val += 16;
	}
	else
	{
		if (fast)
			val += 10;
		else
			val += 2;
	}

	if (val > len-loopStart)
		val = len-loopStart;

	song->samples[editor.currSample].loopLength = val;
	ui.updateCurrSampleReplen = true;

	updatePaulaLoops();

	if (ui.samplerScreenShown)
		setLoopSprites();

	if (ui.editOpScreenShown && ui.editOpScreen == 3) // sample chord editor
		ui.updateChordLengthText = true;
}

static void sampleRepeatLengthDownButton(bool fast)
{
	if (editor.sampleZero)
		return;

	int32_t val = song->samples[editor.currSample].loopLength;
	int32_t len = song->samples[editor.currSample].length;

	if (len == 0)
	{
		song->samples[editor.currSample].loopLength = 2;
		return;
	}

	if (mouse.rightButtonPressed)
	{
		if (fast)
			val -= 64;
		else
			val -= 16;
	}
	else
	{
		if (fast)
			val -= 10;
		else
			val -= 2;
	}

	if (val < 2)
		val = 2;

	song->samples[editor.currSample].loopLength = val;
	ui.updateCurrSampleReplen = true;

	updatePaulaLoops();

	if (ui.samplerScreenShown)
		setLoopSprites();

	if (ui.editOpScreenShown && ui.editOpScreen == 3) // sample chord editor
		ui.updateChordLengthText = true;
}

static void tempoUpButton(void)
{
	if (editor.timingMode == TEMPO_MODE_VBLANK)
		return;

	int32_t val = song->currBPM;
	if (mouse.rightButtonPressed)
		val += 10;
	else
		val++;

	if (val > 255)
		val = 255;

	song->currBPM = val;
	modSetTempo(song->currBPM, true);
	ui.updateSongBPM = true;
}

static void tempoDownButton(void)
{
	if (editor.timingMode == TEMPO_MODE_VBLANK)
		return;

	int32_t val = song->currBPM;
	if (mouse.rightButtonPressed)
		val -= 10;
	else
		val--;

	if (val < 32)
		val = 32;

	song->currBPM = val;
	modSetTempo(song->currBPM, true);
	ui.updateSongBPM = true;
}

static void songLengthUpButton(void)
{
	int16_t val = song->header.songLength;
	if (mouse.rightButtonPressed)
		val += 10;
	else
		val++;

	if (val > 128)
		val = 128;

	song->header.songLength = (uint8_t)val;

	val = song->currPos;
	if (val > song->header.songLength-1)
		val = song->header.songLength-1;

	editor.currPosEdPattDisp = &song->header.patternTable[val];
	ui.updateSongLength = true;
}

static void songLengthDownButton(void)
{
	int16_t val = song->header.songLength;

	if (mouse.rightButtonPressed)
		val -= 10;
	else
		val--;

	if (val < 1)
		val = 1;

	song->header.songLength = (uint8_t)val;

	val = song->currPos;
	if (val > song->header.songLength-1)
		val = song->header.songLength-1;

	editor.currPosEdPattDisp = &song->header.patternTable[val];
	ui.updateSongLength = true;
}

static void patternUpButton(void)
{
	int16_t val = song->header.patternTable[song->currPos];

	if (mouse.rightButtonPressed)
		val += 10;
	else
		val++;

	if (val > MAX_PATTERNS-1)
		val = MAX_PATTERNS-1;

	song->header.patternTable[song->currPos] = (uint8_t)val;

	if (ui.posEdScreenShown)
		ui.updatePosEd = true;

	ui.updateSongPattern = true;
}

static void patternDownButton(void)
{
	int16_t val = song->header.patternTable[song->currPos];

	if (mouse.rightButtonPressed)
		val -= 10;
	else
		val--;

	if (val < 0)
		val = 0;

	song->header.patternTable[song->currPos] = (uint8_t)val;

	if (ui.posEdScreenShown)
		ui.updatePosEd = true;

	ui.updateSongPattern = true;
}

static void positionUpButton(void)
{
	int16_t val = song->currPos;

	if (mouse.rightButtonPressed)
		val += 10;
	else
		val++;

	if (val > 127)
		val = 127;

	modSetPos(val, DONT_SET_ROW);
}

static void positionDownButton(void)
{
	int16_t val = song->currPos;

	if (mouse.rightButtonPressed)
		val -= 10;
	else
		val--;

	if (val < 0)
		val = 0;

	modSetPos(val, DONT_SET_ROW);
}

static void handleSamplerVolumeBox(void)
{
	if (mouse.rightButtonPressed)
	{
		if (ui.editTextFlag)
		{
			leaveTextEditMode(EDIT_TEXT_NO_UPDATE);
		}
		else
		{
			ui.samplerVolBoxShown = false;
			removeSamplerVolBox();
		}

		return;
	}

	if (ui.editTextFlag)
		return;

	// check buttons
	if (mouse.leftButtonPressed)
	{
		// MAIN SCREEN STOP
		if (!ui.diskOpScreenShown && !ui.posEdScreenShown)
		{
			if (mouse.x >= 182 && mouse.x <= 243 && mouse.y >= 0 && mouse.y <= 10)
			{
				modStop();
				return;
			}
		}

		if (mouse.x >= 32 && mouse.x <= 95)
		{
			// SAMPLER SCREEN PLAY WAVEFORM
			if (mouse.y >= 211 && mouse.y <= 221)
			{
				samplerPlayWaveform();
				return;
			}

			// SAMPLER SCREEN PLAY DISPLAY
			else if (mouse.y >= 222 && mouse.y <= 232)
			{
				samplerPlayDisplay();
				return;
			}

			// SAMPLER SCREEN PLAY RANGE
			else if (mouse.y >= 233 && mouse.y <= 243)
			{
				samplerPlayRange();
				return;
			}
		}

		// SAMPLER SCREEN STOP
		if (mouse.x >= 0 && mouse.x <= 31 && mouse.y >= 222 && mouse.y <= 243)
		{
			turnOffVoices();
			return;
		}

		// VOLUME button (toggle)
		if (mouse.x >= 96 && mouse.x <= 135 && mouse.y >= 244 && mouse.y <= 254)
		{
			ui.samplerVolBoxShown = false;
			removeSamplerVolBox();
			return;
		}

		// DRAG BOXES
		if (mouse.x >= 72 && mouse.x <= 173 && mouse.y >= 154 && mouse.y <= 175)
		{
			volBoxBarPressed(MOUSE_BUTTON_NOT_HELD);
			return;
		}

		if (mouse.x >= 174 && mouse.x <= 207)
		{
			// FROM NUM
			if (mouse.y >= 154 && mouse.y <= 164)
			{
				textEdit.tmpDisp16 = editor.vol1;
				editor.vol1Disp = &textEdit.tmpDisp16;
				textEdit.numPtr16 = &textEdit.tmpDisp16;
				textEdit.numDigits = 3;
				textEdit.cursorStartX = 176;
				textEdit.cursorStartY = 162;
				enterNumberEditMode(TEXT_EDIT_DECIMAL, PTB_SA_VOL_FROM_NUM);
				return;
			}

			// TO NUM
			else if (mouse.y >= 165 && mouse.y <= 175)
			{
				textEdit.tmpDisp16 = editor.vol2;
				editor.vol2Disp = &textEdit.tmpDisp16;
				textEdit.numPtr16 = &textEdit.tmpDisp16;
				textEdit.numDigits = 3;
				textEdit.cursorStartX = 176;
				textEdit.cursorStartY = 173;
				enterNumberEditMode(TEXT_EDIT_DECIMAL, PTB_SA_VOL_TO_NUM);
				return;
			}
		}

		if (mouse.y >= 176 && mouse.y <= 186)
		{
			// NORMALIZE
			if (mouse.x >= 101 && mouse.x <= 143)
			{
				if (editor.sampleZero)
				{
					statusNotSampleZero();
					return;
				}

				moduleSample_t *s = &song->samples[editor.currSample];
				if (s->length == 0)
				{
					statusSampleIsEmpty();
					return;
				}

				int8_t *sampleData = &song->sampleData[s->offset];

				int32_t from = 0;
				int32_t to = s->length;

				if (editor.markStartOfs != -1)
				{
					from = editor.markStartOfs;
					to = editor.markEndOfs;

					if (to > s->length)
						to = s->length;

					if (from == to || from >= s->length || to < from)
					{
						from = 0;
						to = s->length;
					}
				}

				int32_t hi = 0;
				for (int32_t i = from; i < to; i++)
				{
					const int32_t sample = ABS(sampleData[i]);
					if (sample > hi)
						hi = sample;
				}

				if (hi <= 0 || hi > 127)
				{
					editor.vol1 = 100;
					editor.vol2 = 100;
				}
				else if (hi < 64)
				{
					editor.vol1 = 200;
					editor.vol2 = 200;
				}
				else
				{
					editor.vol1 = (uint16_t)((100 * 127) / hi);
					editor.vol2 = (uint16_t)((100 * 127) / hi);
				}

				ui.updateVolFromText = true;
				ui.updateVolToText = true;

				showVolFromSlider();
				showVolToSlider();
				return;
			}

			// RAMP DOWN
			else if (mouse.x >= 144 && mouse.x <= 153)
			{
				editor.vol1 = 100;
				editor.vol2 = 0;
				ui.updateVolFromText = true;
				ui.updateVolToText = true;
				showVolFromSlider();
				showVolToSlider();
				return;
			}

			// RAMP UP
			else if (mouse.x >= 154 && mouse.x <= 163)
			{
				editor.vol1 = 0;
				editor.vol2 = 100;
				ui.updateVolFromText = true;
				ui.updateVolToText = true;
				showVolFromSlider();
				showVolToSlider();
				return;
			}

			// RAMP UNITY
			else if (mouse.x >= 164 && mouse.x <= 173)
			{
				editor.vol1 = 100;
				editor.vol2 = 100;
				ui.updateVolFromText = true;
				ui.updateVolToText = true;
				showVolFromSlider();
				showVolToSlider();
				return;
			}

			// CANCEL
			else if (mouse.x >= 174 && mouse.x <= 207)
			{
				ui.samplerVolBoxShown = false;
				removeSamplerVolBox();
				return;
			}

			// RAMP
			else if (mouse.x >= 72 && mouse.x <= 100)
			{
				if (editor.sampleZero)
				{
					statusNotSampleZero();
					return;
				}

				moduleSample_t *s = &song->samples[editor.currSample];
				if (s->length == 0)
				{
					statusSampleIsEmpty();
					return;
				}

				if (editor.vol1 == 100 && editor.vol2 == 100)
				{
					ui.samplerVolBoxShown = false;
					removeSamplerVolBox();
					return;
				}

				int8_t *sampleData = &song->sampleData[s->offset];

				int32_t from = 0;
				int32_t to = s->length;

				if (editor.markStartOfs != -1)
				{
					from = editor.markStartOfs;
					to = editor.markEndOfs;

					if (to > s->length)
						to = s->length;

					if (from == to || from >= s->length || to < from)
					{
						from = 0;
						to = s->length;
					}
				}

				const int32_t markLength = to - from;

				double dToFrac = 0.0;
				double dFromFrac = editor.vol1;

				double dToDelta = (double)editor.vol2 / markLength;
				double dFromDelta = (double)editor.vol1 / markLength;

				for (int32_t i = from; i < to; i++)
				{
					double dSmp = ((dToFrac + dFromFrac) * sampleData[i]) * (1.0 / 100.0);
					int32_t smp32 = (int32_t)dSmp;
					CLAMP8(smp32);

					sampleData[i] = (int8_t)smp32;

					dToFrac += dToDelta;
					dFromFrac -= dFromDelta;
				}

				fixSampleBeep(s);

				ui.samplerVolBoxShown = false;
				removeSamplerVolBox();

				updateWindowTitle(MOD_IS_MODIFIED);
			}
		}
	}
}

static void handleRepeatedSamplerFilterButtons(void)
{
	if (!mouse.leftButtonPressed || mouse.lastSmpFilterButton == -1)
		return;

	switch (mouse.lastSmpFilterButton)
	{
		case 0: // low-pass cutoff up
		{
			if (mouse.rightButtonPressed)
			{
				if (editor.lpCutOff <= 0xFFFF-50)
					editor.lpCutOff += 50;
				else
					editor.lpCutOff = 0xFFFF;
			}
			else
			{
				if (editor.lpCutOff < 0xFFFF)
					editor.lpCutOff++;
				else
					editor.lpCutOff = 0xFFFF;
			}

			if (editor.lpCutOff > (uint16_t)(FILTERS_BASE_FREQ/2))
				editor.lpCutOff = (uint16_t)(FILTERS_BASE_FREQ/2);

			ui.updateLPText = true;
		}
		break;

		case 1: // low-pass cutoff down
		{
			if (mouse.rightButtonPressed)
			{
				if (editor.lpCutOff >= 50)
					editor.lpCutOff -= 50;
				else
					editor.lpCutOff = 0;
			}
			else
			{
				if (editor.lpCutOff > 0)
					editor.lpCutOff--;
				else
					editor.lpCutOff = 0;
			}

			ui.updateLPText = true;
		}
		break;

		case 2: // high-pass cutoff up
		{
			if (mouse.rightButtonPressed)
			{
				if (editor.hpCutOff <= 0xFFFF-50)
					editor.hpCutOff += 50;
				else
					editor.hpCutOff = 0xFFFF;
			}
			else
			{
				if (editor.hpCutOff < 0xFFFF)
					editor.hpCutOff++;
				else
					editor.hpCutOff = 0xFFFF;
			}

			if (editor.hpCutOff > (uint16_t)(FILTERS_BASE_FREQ/2))
				editor.hpCutOff = (uint16_t)(FILTERS_BASE_FREQ/2);

			ui.updateHPText = true;
		}
		break;

		case 3: // high-pass cutoff down
		{
			if (mouse.rightButtonPressed)
			{
				if (editor.hpCutOff >= 50)
					editor.hpCutOff -= 50;
				else
					editor.hpCutOff = 0;
			}
			else
			{
				if (editor.hpCutOff > 0)
					editor.hpCutOff--;
				else
					editor.hpCutOff = 0;
			}

			ui.updateHPText = true;
		}
		break;

		default: break;
	}
}

static void handleSamplerFiltersBox(void)
{
	if (mouse.rightButtonPressed && ui.editTextFlag)
	{
		leaveTextEditMode(EDIT_TEXT_NO_UPDATE);
		return;
	}

	if (ui.editTextFlag || mouse.lastSmpFilterButton > -1 || !mouse.leftButtonPressed)
		return;

	// FILTERS button (toggle)
	if (mouse.x >= 211 && mouse.x <= 245 && mouse.y >= 244 && mouse.y <= 254)
	{
		ui.samplerFiltersBoxShown = false;
		removeSamplerFiltersBox();
		return;
	}

	// MAIN SCREEN STOP
	if (!ui.diskOpScreenShown && !ui.posEdScreenShown)
	{
		if (mouse.x >= 182 && mouse.x <= 243 && mouse.y >= 0 && mouse.y <= 10)
		{
			modStop();
			return;
		}
	}

	if (mouse.x >= 32 && mouse.x <= 95)
	{
		// SAMPLER SCREEN PLAY WAVEFORM
		if (mouse.y >= 211 && mouse.y <= 221)
		{
			samplerPlayWaveform();
			return;
		}

		// SAMPLER SCREEN PLAY DISPLAY
		else if (mouse.y >= 222 && mouse.y <= 232)
		{
			samplerPlayDisplay();
			return;
		}

		// SAMPLER SCREEN PLAY RANGE
		else if (mouse.y >= 233 && mouse.y <= 243)
		{
			samplerPlayRange();
			return;
		}
	}

	// SAMPLER SCREEN STOP
	if (mouse.x >= 0 && mouse.x <= 31 && mouse.y >= 222 && mouse.y <= 243)
	{
		turnOffVoices();
		return;
	}

	// UNDO
	if (mouse.x >= 65 && mouse.x <= 75 && mouse.y >= 154 && mouse.y <= 184)
	{
		if (editor.sampleZero)
		{
			statusNotSampleZero();
			return;
		}

		moduleSample_t *s = &song->samples[editor.currSample];
		if (s->length == 0)
		{
			statusSampleIsEmpty();
		}
		else
		{
			memcpy(&song->sampleData[s->offset], sampler.sampleUndoCopy, config.maxSampleLength);
			redrawSample();
			updateWindowTitle(MOD_IS_MODIFIED);
			renderSamplerFiltersBox();
		}

		return;
	}

	if (mouse.y >= 154 && mouse.y <= 164)
	{
		// DO LOW-PASS FILTER
		if (mouse.x >= 76 && mouse.x <= 165)
		{
			lowPassSample(editor.lpCutOff);
			renderSamplerFiltersBox();
			return;
		}

		// LOW-PASS CUTOFF
		else if (mouse.x >= 166 && mouse.x <= 217)
		{
			if (mouse.rightButtonPressed)
			{
				editor.lpCutOff = 0;
				ui.updateLPText = true;
			}
			else
			{
				textEdit.tmpDisp16 = editor.lpCutOff;
				editor.lpCutOffDisp = &textEdit.tmpDisp16;
				textEdit.numPtr16 = &textEdit.tmpDisp16;
				textEdit.numDigits = 4;
				textEdit.cursorStartX = 168;
				textEdit.cursorStartY = 162;
				enterNumberEditMode(TEXT_EDIT_DECIMAL, PTB_SA_FIL_LP_CUTOFF);
			}

			return;
		}

		// LOW-PASS CUTOFF UP
		else if (mouse.x >= 218 && mouse.x <= 228)
		{
			mouse.lastSmpFilterButton = 0;
			if (mouse.rightButtonPressed)
			{
				if (editor.lpCutOff <= 0xFFFF-100)
					editor.lpCutOff += 100;
				else
					editor.lpCutOff = 0xFFFF;
			}
			else
			{
				if (editor.lpCutOff < 0xFFFF)
					editor.lpCutOff++;
				else
					editor.lpCutOff = 0xFFFF;
			}

			if (editor.lpCutOff > (uint16_t)(FILTERS_BASE_FREQ/2))
				editor.lpCutOff = (uint16_t)(FILTERS_BASE_FREQ/2);

			ui.updateLPText = true;
			return;
		}

		// LOW-PASS CUTOFF DOWN
		else if (mouse.x >= 229 && mouse.x <= 239)
		{
			mouse.lastSmpFilterButton = 1;
			if (mouse.rightButtonPressed)
			{
				if (editor.lpCutOff >= 100)
					editor.lpCutOff -= 100;
				else
					editor.lpCutOff = 0;
			}
			else
			{
				if (editor.lpCutOff > 0)
					editor.lpCutOff--;
				else
					editor.lpCutOff = 0;
			}

			ui.updateLPText = true;
			return;
		}
	}

	if (mouse.y >= 164 && mouse.y <= 174)
	{
		// DO HIGH-PASS FILTER
		if (mouse.x >= 76 && mouse.x <= 165)
		{
			highPassSample(editor.hpCutOff);
			renderSamplerFiltersBox();
			return;
		}

		// HIGH-PASS CUTOFF
		else if (mouse.x >= 166 && mouse.x <= 217)
		{
			if (mouse.rightButtonPressed)
			{
				editor.hpCutOff = 0;
				ui.updateHPText = true;
			}
			else
			{
				textEdit.tmpDisp16 = editor.hpCutOff;
				editor.hpCutOffDisp = &textEdit.tmpDisp16;
				textEdit.numPtr16 = &textEdit.tmpDisp16;
				textEdit.numDigits = 4;
				textEdit.cursorStartX = 168;
				textEdit.cursorStartY = 173;
				enterNumberEditMode(TEXT_EDIT_DECIMAL, PTB_SA_FIL_HP_CUTOFF);
			}

			return;
		}

		// HIGH-PASS CUTOFF UP
		else if (mouse.x >= 218 && mouse.x <= 228)
		{
			mouse.lastSmpFilterButton = 2;
			if (mouse.rightButtonPressed)
			{
				if (editor.hpCutOff <= 0xFFFF-100)
					editor.hpCutOff += 100;
				else
					editor.hpCutOff = 0xFFFF;
			}
			else
			{
				if (editor.hpCutOff < 0xFFFF)
					editor.hpCutOff++;
				else
					editor.hpCutOff = 0xFFFF;
			}

			if (editor.hpCutOff > (uint16_t)(FILTERS_BASE_FREQ/2))
				editor.hpCutOff = (uint16_t)(FILTERS_BASE_FREQ/2);

			ui.updateHPText = true;
			return;
		}

		// HIGH-PASS CUTOFF DOWN
		else if (mouse.x >= 229 && mouse.x <= 239)
		{
			mouse.lastSmpFilterButton = 3;
			if (mouse.rightButtonPressed)
			{
				if (editor.hpCutOff >= 100)
					editor.hpCutOff -= 100;
				else
					editor.hpCutOff = 0;
			}
			else
			{
				if (editor.hpCutOff > 0)
					editor.hpCutOff--;
				else
					editor.hpCutOff = 0;
			}

			ui.updateHPText = true;
			return;
		}
	}

	// NORMALIZE SAMPLE FLAG
	if (mouse.x >= 76 && mouse.x <= 239 && mouse.y >= 174 && mouse.y <= 186)
	{
		editor.normalizeFiltersFlag ^= 1;
		ui.updateNormFlag = true;
		return;
	}

	// EXIT
	if (mouse.x >= 240 && mouse.x <= 250 && mouse.y >= 154 && mouse.y <= 186)
	{
		ui.samplerFiltersBoxShown = false;
		removeSamplerFiltersBox();
	}
}

static bool withinButtonRect(const guiButton_t *b)
{
	if (mouse.x >= b->x1 && mouse.x <= b->x2 && mouse.y >= b->y1 && mouse.y <= b->y2)
		return true;

	return false;
}

#define TEST_BUTTONS(bStruct, bNum) \
for (uint32_t i = 0; i < bNum; i++) \
	if (withinButtonRect(&bStruct[i])) \
		return bStruct[i].b; \

static int32_t checkGUIButtons(void)
{
	// QUIT (xy 0,0) works on all screens except for ask/clear screen
	if (mouse.x == 0 && mouse.y == 0)
		return PTB_QUIT;

	// top screen buttons
	if (ui.diskOpScreenShown)
	{
		TEST_BUTTONS(bDiskOp, DISKOP_BUTTONS);
	}
	else
	{
		if (ui.posEdScreenShown)
		{
			TEST_BUTTONS(bPosEd, POSED_BUTTONS);
		}
		else if (ui.editOpScreenShown)
		{
			switch (ui.editOpScreen)
			{
				default:
				case 0: TEST_BUTTONS(bEditOp1, EDITOP1_BUTTONS); break;
				case 1: TEST_BUTTONS(bEditOp2, EDITOP2_BUTTONS); break;
				case 2: TEST_BUTTONS(bEditOp3, EDITOP3_BUTTONS); break;
				case 3: TEST_BUTTONS(bEditOp4, EDITOP4_BUTTONS); break;
			}
		}

		TEST_BUTTONS(bTopScreen, TOPSCREEN_BUTTONS);
	}

	// middle buttons (always present)
	TEST_BUTTONS(bMidScreen, MIDSCREEN_BUTTONS);

	// bottom screen buttons
	if (ui.samplerScreenShown)
	{
		TEST_BUTTONS(bSampler, SAMPLER_BUTTONS);
	}
	else
	{
		TEST_BUTTONS(bBotScreen, BOTSCREEN_BUTTONS);
	}

	return -1;
}

void mouseWheelUpHandler(void)
{
	if (ui.editTextFlag || ui.askBoxShown || editor.swapChannelFlag ||
		ui.samplingBoxShown || ui.samplerVolBoxShown || ui.samplerFiltersBoxShown)
		return;

	if (mouse.y < 121)
	{
		// upper part of screen
		if (ui.diskOpScreenShown)
		{
			if (diskop.scrollOffset > 0)
			{
				diskop.scrollOffset--;
				ui.updateDiskOpFileList = true;
			}
		}
		else if (ui.posEdScreenShown)
		{
			posEdScrollUp();
		}
	}
	else if (ui.samplerScreenShown) // lower part of screen
	{
		samplerZoomInMouseWheel();
	}
	else if (!editor.songPlaying && song->currRow > 0)
	{
		modSetPos(DONT_SET_ORDER, song->currRow - 1); // pattern data
	}
}

void mouseWheelDownHandler(void)
{
	if (ui.editTextFlag || ui.askBoxShown || editor.swapChannelFlag ||
		ui.samplingBoxShown || ui.samplerVolBoxShown || ui.samplerFiltersBoxShown)
		return;

	if (mouse.y < 121)
	{
		// upper part of screen
		if (ui.diskOpScreenShown)
		{
			if (diskop.numEntries > DISKOP_LINES && diskop.scrollOffset < diskop.numEntries-DISKOP_LINES)
			{
				diskop.scrollOffset++;
				ui.updateDiskOpFileList = true;
			}
		}
		else if (ui.posEdScreenShown)
		{
			posEdScrollDown();
		}
	}
	else if (ui.samplerScreenShown) // lower part of screen
	{
		if (!ui.samplerVolBoxShown && !ui.samplerFiltersBoxShown)
			samplerZoomOutMouseWheel();
	}
	else if (!editor.songPlaying && song->currRow < MOD_ROWS)
	{
		modSetPos(DONT_SET_ORDER, song->currRow + 1); // pattern data
	}
}

static bool handleRightMouseButton(void)
{
	if (!mouse.rightButtonPressed)
		return false;

	// exit sample swap mode with right mouse button (if present)
	if (editor.swapChannelFlag)
	{
		editor.swapChannelFlag = false;
		pointerSetPreviousMode();
		setPrevStatusMessage();
		return true;
	}

	// toggle channel muting with right mouse button
	if (ui.visualizerMode == VISUAL_QUADRASCOPE && mouse.y >= 55 && mouse.y <= 87)
	{
		if (!ui.posEdScreenShown && !ui.editOpScreenShown && !ui.diskOpScreenShown &&
			!ui.aboutScreenShown && !ui.samplerVolBoxShown &&
			!ui.samplerFiltersBoxShown && !ui.samplingBoxShown &&
			!editor.mod2WavOngoing)
		{
			     if (mouse.x > 127 && mouse.x <= 167) editor.muted[0] ^= 1;
			else if (mouse.x > 175 && mouse.x <= 215) editor.muted[1] ^= 1;
			else if (mouse.x > 223 && mouse.x <= 263) editor.muted[2] ^= 1;
			else if (mouse.x > 271 && mouse.x <= 311) editor.muted[3] ^= 1;

			renderMuteButtons();
		}
	}

	// sample hand drawing
	if (mouse.y >= 138 && mouse.y <= 201 && ui.samplerScreenShown &&
		!ui.samplerVolBoxShown && !ui.samplerFiltersBoxShown && !ui.samplingBoxShown)
	{
		samplerEditSample(false);
	}

	return false;
}

static bool handleLeftMouseButton(void)
{
	if (editor.swapChannelFlag || ui.editTextFlag)
		return false;

	// handle volume toolbox in sampler screen
	if (ui.samplerVolBoxShown)
	{
		handleSamplerVolumeBox();
		return true;
	}

	// handle filters toolbox in sampler screen
	else if (ui.samplerFiltersBoxShown)
	{
		handleSamplerFiltersBox();
		return true;
	}

	// handle sampling toolbox in sampler screen
	else if (ui.samplingBoxShown)
	{
		handleSamplingBox();
		return true;
	}

	// cancel note input gadgets with left/right mouse button
	if (ui.changingSmpResample || ui.changingChordNote || ui.changingDrumPadNote || ui.changingSamplingNote)
	{
		if (mouse.leftButtonPressed || mouse.rightButtonPressed)
		{
			ui.changingSmpResample = false;
			ui.changingSamplingNote = false;
			ui.changingChordNote = false;
			ui.changingDrumPadNote = false;

			ui.updateResampleNote = true;
			ui.updateChordNote1Text = true;
			ui.updateChordNote2Text = true;
			ui.updateChordNote3Text = true;
			ui.updateChordNote4Text = true;

			setPrevStatusMessage();
			pointerSetPreviousMode();
		}

		return true;
	}

	if (!mouse.leftButtonPressed)
		return false;

	// if MOD2WAV is ongoing, only check CANCEL button
	if (editor.mod2WavOngoing)
	{
		if (mouse.x >= MOD2WAV_CANCEL_BTN_X1 && mouse.x <= MOD2WAV_CANCEL_BTN_X2 &&
			mouse.y >= MOD2WAV_CANCEL_BTN_Y1 && mouse.y <= MOD2WAV_CANCEL_BTN_Y2)
		{
			editor.abortMod2Wav = true;
		}

		return true; // don't handle other buttons
	}

	// if in fullscreen mode and the image isn't filling the whole screen, handle top left corner as quit
	if (video.fullscreen && (video.renderX > 0 || video.renderY > 0) && (mouse.rawX == 0 && mouse.rawY == 0))
		return handleGUIButtons(PTB_QUIT);

	int32_t guiButton = checkGUIButtons();
	if (guiButton == -1)
		return false;

	return handleGUIButtons(guiButton);
}

void updateMouseCounters(void)
{
	if (mouse.buttonWaiting)
	{
		if (++mouse.buttonWaitCounter > VBLANK_HZ/4) // quarter of a second
		{
			mouse.buttonWaitCounter = 0;
			mouse.buttonWaiting = false;
		}
	}

	if (editor.errorMsgActive)
	{
		if (++editor.errorMsgCounter >= VBLANK_HZ) // 1 second
		{
			editor.errorMsgCounter = 0;

			// don't reset status text/mouse color during certain modes
			if (!ui.askBoxShown         && !ui.changingChordNote   && !ui.changingDrumPadNote &&
				!ui.changingSmpResample && !editor.swapChannelFlag && !ui.changingSamplingNote)
			{
				pointerSetPreviousMode();
				setPrevStatusMessage();
			}

			editor.errorMsgActive = false;
			editor.errorMsgBlock = false;

			if (ui.diskOpScreenShown)
				diskOpShowSelectText();
		}
	}
}

static bool handleGUIButtons(int32_t button) // are you prepared to enter the jungle?
{
	textEdit.force32BitNumPtr = false;

	switch (button)
	{
		case PTB_DUMMY: return false; // for gaps / empty spaces / dummies

		case PTB_PAT2SMP:
		{
			if (askBox(ASKBOX_PAT2SMP, "PLEASE SELECT"))
				pat2SmpRender();
		}
		break;

		// Edit Op. All Screens
		case PTB_EO_TITLEBAR:
		{
			     if (ui.editOpScreen == 0) editor.sampleAllFlag ^= 1;
			else if (ui.editOpScreen == 1) editor.trackPattFlag = (editor.trackPattFlag + 1) % 3;
			else if (ui.editOpScreen == 2) editor.halveSampleFlag ^= 1;
			else if (ui.editOpScreen == 3) editor.newOldFlag ^= 1;

			renderEditOpMode();
		}
		break;

		case PTB_EO_1:
		{
			ui.editOpScreen = 0;
			renderEditOpScreen();
		}
		break;

		case PTB_EO_2:
		{
			ui.editOpScreen = 1;
			renderEditOpScreen();
		}
		break;

		case PTB_EO_3:
		{
			ui.editOpScreen = 2;
			renderEditOpScreen();
		}
		break;

		case PTB_EO_EXIT:
		{
			ui.aboutScreenShown = false;
			ui.editOpScreenShown = false;
			displayMainScreen();
		}
		break;
		// ----------------------------------------------------------

		// Edit Op. Screen #1
		case PTB_EO_TRACK_NOTE_UP: trackNoteUp(editor.sampleAllFlag, 0, MOD_ROWS - 1); break;
		case PTB_EO_TRACK_NOTE_DOWN: trackNoteDown(editor.sampleAllFlag, 0, MOD_ROWS - 1); break;
		case PTB_EO_TRACK_OCTA_UP: trackOctaUp(editor.sampleAllFlag, 0, MOD_ROWS - 1); break;
		case PTB_EO_TRACK_OCTA_DOWN: trackOctaDown(editor.sampleAllFlag, 0, MOD_ROWS - 1); break;
		case PTB_EO_PATT_NOTE_UP: pattNoteUp(editor.sampleAllFlag); break;
		case PTB_EO_PATT_NOTE_DOWN: pattNoteDown(editor.sampleAllFlag); break;
		case PTB_EO_PATT_OCTA_UP: pattOctaUp(editor.sampleAllFlag); break;
		case PTB_EO_PATT_OCTA_DOWN: pattOctaDown(editor.sampleAllFlag); break;
		// ----------------------------------------------------------

		// Edit Op. Screen #2
		case PTB_EO_RECORD:
		{
			editor.recordMode ^= 1;
			ui.updateRecordText = true;
		}
		break;

		case PTB_EO_DELETE: delSampleTrack(); break;
		case PTB_EO_EXCHGE: exchSampleTrack(); break;
		case PTB_EO_COPY: copySampleTrack(); break;

		case PTB_EO_FROM:
		{
			editor.sampleFrom = editor.currSample + 1;
			ui.updateFromText = true;
		}
		break;

		case PTB_EO_TO:
		{
			editor.sampleTo = editor.currSample + 1;
			ui.updateToText = true;
		}
		break;

		case PTB_EO_KILL:
		{
			if (askBox(ASKBOX_YES_NO, "KILL SAMPLE ?"))
				killSample();
		}
		break;

		case PTB_EO_QUANTIZE:
		{
			textEdit.tmpDisp16 = config.quantizeValue;
			editor.quantizeValueDisp = &textEdit.tmpDisp16;
			textEdit.numPtr16 = &textEdit.tmpDisp16;
			textEdit.numDigits = 2;
			textEdit.cursorStartX = 192;
			textEdit.cursorStartY = 74;
			enterNumberEditMode(TEXT_EDIT_DECIMAL, PTB_EO_QUANTIZE);
		}
		break;

		case PTB_EO_METRO_1: // metronome speed
		{
			textEdit.tmpDisp16 = editor.metroSpeed;
			editor.metroSpeedDisp = &textEdit.tmpDisp16;
			textEdit.numPtr16 = &textEdit.tmpDisp16;
			textEdit.numDigits = 2;
			textEdit.cursorStartX = 168;
			textEdit.cursorStartY = 85;
			enterNumberEditMode(TEXT_EDIT_DECIMAL, PTB_EO_METRO_1);
		}
		break;

		case PTB_EO_METRO_2: // metronome channel
		{
			textEdit.tmpDisp16 = editor.metroChannel;
			editor.metroChannelDisp = &textEdit.tmpDisp16;
			textEdit.numPtr16 = &textEdit.tmpDisp16;
			textEdit.numDigits = 2;
			textEdit.cursorStartX = 192;
			textEdit.cursorStartY = 85;
			enterNumberEditMode(TEXT_EDIT_DECIMAL, PTB_EO_METRO_2);
		}
		break;

		case PTB_EO_FROM_NUM:
		{
			textEdit.tmpDisp8 = editor.sampleFrom;
			editor.sampleFromDisp = &textEdit.tmpDisp8;
			textEdit.numPtr8 = &textEdit.tmpDisp8;
			textEdit.numDigits = 2;
			textEdit.numBits = 8;
			textEdit.cursorStartX = 264;
			textEdit.cursorStartY = 85;
			enterNumberEditMode(TEXT_EDIT_HEX, PTB_EO_FROM_NUM);
		}
		break;

		case PTB_EO_TO_NUM:
		{
			textEdit.tmpDisp8 = editor.sampleTo;
			editor.sampleToDisp = &textEdit.tmpDisp8;
			textEdit.numPtr8 = &textEdit.tmpDisp8;
			textEdit.numDigits = 2;
			textEdit.numBits = 8;
			textEdit.cursorStartX = 264;
			textEdit.cursorStartY = 96;
			enterNumberEditMode(TEXT_EDIT_HEX, PTB_EO_TO_NUM);
		}
		break;

		case PTB_EO_FROM_UP:
		{
			if (editor.sampleFrom < 0x1F)
			{
				editor.sampleFrom++;
				ui.updateFromText = true;
			}
		}
		break;

		case PTB_EO_FROM_DOWN:
		{
			if (editor.sampleFrom > 0x00)
			{
				editor.sampleFrom--;
				ui.updateFromText = true;
			}
		}
		break;

		case PTB_EO_TO_UP:
		{
			if (editor.sampleTo < 0x1F)
			{
				editor.sampleTo++;
				ui.updateToText = true;
			}
		}
		break;

		case PTB_EO_TO_DOWN:
		{
			if (editor.sampleTo > 0x00)
			{
				editor.sampleTo--;
				ui.updateToText = true;
			}
		}
		break;

		case PTB_EO_KEYS:
		{
			editor.multiFlag ^= 1;
			ui.updateTrackerFlags = true;
			ui.updateKeysText = true;
		}
		break;
		// ----------------------------------------------------------

		// Edit Op. Screen #3

		case PTB_EO_MIX:
		{
			if (!mouse.rightButtonPressed)
			{
				editor.mixFlag = true;
				ui.updateMixText = true;

				textEdit.textStartPtr = editor.mixText;
				textEdit.textEndPtr = editor.mixText + 15;
				textEdit.numBlocks = 16;
				textEdit.cursorStartX = 128;
				textEdit.cursorStartY = 52;
				textEdit.scrollable = false;
				enterTextEditMode(PTB_EO_MIX);
			}
			else // right mouse button = special, mix sample with itself (w/ modulation)
			{
				if (editor.sampleZero)
				{
					statusNotSampleZero();
					break;
				}

				moduleSample_t *s = &song->samples[editor.currSample];
				if (s->length == 0)
				{
					statusSampleIsEmpty();
					break;
				}

				if (editor.samplePos >= s->length)
				{
					displayErrorMsg("INVALID POS !");
					break;
				}

				int8_t *sampleData = &song->sampleData[s->offset];

				// make copy of sample data
				int8_t *sampleCopy = (int8_t *)malloc(s->length);
				if (sampleCopy == NULL)
				{
					statusOutOfMemory();
					return true;
				}

				memcpy(sampleCopy, sampleData, s->length);

				int8_t *mixPtr = sampleData + editor.samplePos;
				const int32_t mixLength = s->length - editor.samplePos;

				if (editor.modulateSpeed == 0) // no modulation
				{
					for (int32_t j = 0; j < mixLength; j++)
					{
						int16_t tmp16 = mixPtr[j] + sampleCopy[j];
						if (editor.halveSampleFlag)
							tmp16 >>= 1;

						CLAMP8(tmp16);
						mixPtr[j] = (int8_t)tmp16;
					}
				}
				else // modulation
				{
					int32_t modTableOffset = 0;
					uint32_t modOffset = 0; // 21.11fp

					for (int32_t j = 0; j < mixLength; j++)
					{
						const int32_t sampleReadPos = modOffset >> 11;
						int16_t tmp16 = mixPtr[j] + sampleCopy[MIN(sampleReadPos, s->length-1)];
						if (editor.halveSampleFlag)
							tmp16 >>= 1;

						CLAMP8(tmp16);
						mixPtr[j] = (int8_t)tmp16;

						modTableOffset += editor.modulateSpeed;
						modOffset += modulationTable[(modTableOffset >> 12) & 63];
					}
				}

				free(sampleCopy);

				fixSampleBeep(s);
				if (ui.samplerScreenShown)
					displaySample();

				updateWindowTitle(MOD_IS_MODIFIED);
			}
		}
		break;

		case PTB_EO_ECHO:
		{
			if (editor.sampleZero)
			{
				statusNotSampleZero();
				break;
			}

			moduleSample_t *s = &song->samples[editor.currSample];
			if (s->length == 0)
			{
				statusSampleIsEmpty();
				break;
			}

			if (editor.samplePos == 0)
			{
				displayErrorMsg("SET SAMPLE POS !");
				break;
			}

			if (editor.samplePos >= s->length)
			{
				displayErrorMsg("INVALID POS !");
				break;
			}

			int8_t *sampleData = &song->sampleData[s->offset];

			if (editor.modulateSpeed == 0) // no modulation
			{
				for (int32_t j = 0; j < s->length; j++)
				{
					int16_t tmp16 = (sampleData[editor.samplePos+j] + sampleData[j]) >> 1;
					CLAMP8(tmp16);

					sampleData[editor.samplePos+j] = (int8_t)tmp16;
				}
			}
			else // modulation
			{
				int32_t modTableOffset = 0;
				uint32_t modOffset = 0; // 21.11fp

				for (int32_t j = 0; j < s->length; j++)
				{
					const int32_t sampleReadPos = modOffset >> 11;
					int16_t tmp16 = (sampleData[editor.samplePos+j] + sampleData[MIN(sampleReadPos, s->length-1)]) >> 1;
					CLAMP8(tmp16);

					sampleData[editor.samplePos+j] = (int8_t)tmp16;

					modTableOffset += editor.modulateSpeed;
					modOffset += modulationTable[(modTableOffset >> 12) & 63];
				}
			}

			if (!editor.halveSampleFlag)
			{
				for (int32_t j = 0; j < s->length; j++)
				{
					int16_t tmp16 = (int16_t)sampleData[j] * 2;
					CLAMP8(tmp16);
					sampleData[j] = (int8_t)tmp16;
				}
			}

			fixSampleBeep(s);
			if (ui.samplerScreenShown)
				displaySample();

			updateWindowTitle(MOD_IS_MODIFIED);
		}
		break;

		case PTB_EO_POS_NUM:
		{
			if (config.maxSampleLength == 65534 && mouse.x < 244) // yuck!
				break;

			if (mouse.rightButtonPressed)
			{
				editor.samplePos = 0;
				ui.updatePosText = true;
			}
			else
			{
				textEdit.force32BitNumPtr = true;

				textEdit.tmpDisp32 = editor.samplePos;
				editor.samplePosDisp = &textEdit.tmpDisp32;
				textEdit.numPtr32 = &textEdit.tmpDisp32;

				if (config.maxSampleLength == 65534)
				{
					textEdit.numDigits = 4;
					textEdit.numBits = 16;
					textEdit.cursorStartX = 248;
					textEdit.cursorStartY = 63;
				}
				else
				{
					textEdit.numDigits = 5;
					textEdit.numBits = 17;
					textEdit.cursorStartX = 240;
					textEdit.cursorStartY = 63;
				}

				enterNumberEditMode(TEXT_EDIT_HEX, PTB_EO_POS_NUM);
			}
		}
		break;

		case PTB_EO_POS_UP: edPosUpButton(INCREMENT_SLOW); break;
		case PTB_EO_POS_DOWN: edPosDownButton(INCREMENT_SLOW); break;

		case PTB_EO_BOOST: // this is actually treble increase
		{
			if (editor.sampleZero)
			{
				statusNotSampleZero();
				break;
			}

			moduleSample_t *s = &song->samples[editor.currSample];
			if (s->length == 0)
			{
				statusSampleIsEmpty();
				break;
			}

			boostSample(editor.currSample, false);
			if (ui.samplerScreenShown)
				displaySample();

			updateWindowTitle(MOD_IS_MODIFIED);
		}
		break;

		case PTB_EO_FILTER: // this is actually treble decrease
		{
			if (editor.sampleZero)
			{
				statusNotSampleZero();
				break;
			}

			moduleSample_t *s = &song->samples[editor.currSample];
			if (s->length == 0)
			{
				statusSampleIsEmpty();
				break;
			}

			filterSample(editor.currSample, false);
			if (ui.samplerScreenShown)
				displaySample();

			updateWindowTitle(MOD_IS_MODIFIED);
		}
		break;

		case PTB_EO_MOD_NUM:
		{
			if (mouse.rightButtonPressed)
			{
				editor.modulateSpeed = 0;
				ui.updateModText = true;
			}
		}
		break;

		case PTB_EO_MOD: // modulate sample data
		{
			if (editor.sampleZero)
			{
				statusNotSampleZero();
				break;
			}

			moduleSample_t *s = &song->samples[editor.currSample];
			if (s->length == 0)
			{
				statusSampleIsEmpty();
				break;
			}

			if (editor.modulateSpeed == 0)
			{
				displayErrorMsg("SET MOD. SPEED !");
				break;
			}

			int8_t *sampleData = &song->sampleData[s->offset];

			// make copy of sample data
			int8_t *sampleCopy = (int8_t *)malloc(s->length);
			if (sampleCopy == NULL)
			{
				statusOutOfMemory();
				return true;
			}

			memcpy(sampleCopy, sampleData, s->length);

			int32_t modTableOffset = 0;
			uint32_t modOffset = 0; // 21.11fp

			for (int32_t j = 0; j < s->length; j++)
			{
				const int32_t sampleReadPos = modOffset >> 11;
				sampleData[j] = sampleCopy[MIN(sampleReadPos, s->length-1)];

				modTableOffset += editor.modulateSpeed;
				modOffset += modulationTable[(modTableOffset >> 12) & 63];
			}

			free(sampleCopy);

			fixSampleBeep(s);
			if (ui.samplerScreenShown)
				displaySample();

			updateWindowTitle(MOD_IS_MODIFIED);
		}
		break;

		case PTB_EO_MOD_UP: edModUpButton(); break;
		case PTB_EO_MOD_DOWN: edModDownButton(); break;

		case PTB_EO_X_FADE:
		{
			if (editor.sampleZero)
			{
				statusNotSampleZero();
				break;
			}

			moduleSample_t *s = &song->samples[editor.currSample];

			if (s->length == 0)
			{
				statusSampleIsEmpty();
				break;
			}

			int8_t *sampleData = &song->sampleData[s->offset];
			int32_t lastSamplePoint = s->length - 1;

			for (int32_t j = 0; j < s->length / 2; j++)
			{
				int16_t tmp16 = sampleData[j] + sampleData[lastSamplePoint-j];
				if (editor.halveSampleFlag)
					tmp16 >>= 1;

				CLAMP8(tmp16);
				int8_t tmpSmp = (int8_t)tmp16;

				sampleData[j] = tmpSmp;
				sampleData[lastSamplePoint-j] = tmpSmp;
			}

			fixSampleBeep(s);
			if (ui.samplerScreenShown)
				displaySample();

			updateWindowTitle(MOD_IS_MODIFIED);
		}
		break;

		case PTB_EO_BACKWD:
		{
			int8_t *ptr8_1, *ptr8_2;

			if (editor.sampleZero)
			{
				statusNotSampleZero();
				break;
			}

			moduleSample_t *s = &song->samples[editor.currSample];
			if (s->length == 0)
			{
				statusSampleIsEmpty();
				break;
			}

			if (editor.markStartOfs != -1 && editor.markStartOfs != editor.markEndOfs && editor.markEndOfs != 0)
			{
				ptr8_1 = &song->sampleData[s->offset+editor.markStartOfs];
				ptr8_2 = &song->sampleData[s->offset+editor.markEndOfs-1];
			}
			else
			{
				ptr8_1 = &song->sampleData[s->offset];
				ptr8_2 = &song->sampleData[s->offset+s->length-1];
			}

			for (int32_t j = 0; j < s->length / 2; j++)
			{
				const int8_t tmpSmp = *ptr8_1;
				*ptr8_1++ = *ptr8_2;
				*ptr8_2-- = tmpSmp;
			}

			fixSampleBeep(s);
			if (ui.samplerScreenShown)
				displaySample();

			updateWindowTitle(MOD_IS_MODIFIED);
		}
		break;

		case PTB_EO_CB:
		{
			if (editor.sampleZero)
			{
				statusNotSampleZero();
				break;
			}

			moduleSample_t *s = &song->samples[editor.currSample];
			if (s->length == 0)
			{
				statusSampleIsEmpty();
				break;
			}

			if (editor.samplePos == 0)
			{
				displayErrorMsg("SET SAMPLE POS !");
				break;
			}

			if (editor.samplePos >= s->length)
			{
				displayErrorMsg("INVALID POS !");
				break;
			}

			turnOffVoices();

			memcpy(&song->sampleData[s->offset], &song->sampleData[s->offset + editor.samplePos], config.maxSampleLength - editor.samplePos);
			memset(&song->sampleData[s->offset + (config.maxSampleLength - editor.samplePos)], 0, editor.samplePos);

			if (editor.samplePos > s->loopStart)
			{
				s->loopStart = 0;
				s->loopLength = 2;
			}
			else
			{
				s->loopStart = (s->loopStart - editor.samplePos) & config.maxSampleLength;
			}

			s->length = (s->length - editor.samplePos) & config.maxSampleLength;

			editor.samplePos = 0;
			fixSampleBeep(s);
			updateCurrSample();

			updateWindowTitle(MOD_IS_MODIFIED);
		}
		break;

		case PTB_EO_CHORD:
		{
			ui.editOpScreen = 3;
			renderEditOpScreen();
		}
		break;

		// fade up
		case PTB_EO_FU:
		{
			if (editor.sampleZero)
			{
				statusNotSampleZero();
				break;
			}

			moduleSample_t *s = &song->samples[editor.currSample];
			if (s->length == 0)
			{
				statusSampleIsEmpty();
				break;
			}

			if (editor.samplePos == 0)
			{
				displayErrorMsg("INVALID POS !");
				break;
			}

			double dDelta = 1.0 / editor.samplePos;
			double dPos = 0.0;

			int8_t *ptr8 = &song->sampleData[s->offset];
			for (int32_t j = 0; j < editor.samplePos; j++)
			{
				int32_t smp32 = (int32_t)((*ptr8) * dPos);
				CLAMP8(smp32);
				*ptr8++ = (int8_t)smp32;

				dPos += dDelta;
			}

			fixSampleBeep(s);
			if (ui.samplerScreenShown)
				displaySample();

			updateWindowTitle(MOD_IS_MODIFIED);
		}
		break;

		// fade down
		case PTB_EO_FD:
		{
			if (editor.sampleZero)
			{
				statusNotSampleZero();
				break;
			}

			moduleSample_t *s = &song->samples[editor.currSample];
			if (s->length == 0)
			{
				statusSampleIsEmpty();
				break;
			}

			if (editor.samplePos >= s->length-1)
			{
				displayErrorMsg("INVALID POS !");
				break;
			}

			int32_t tmp32 = (s->length - 1) - editor.samplePos;
			if (tmp32 == 0)
				tmp32 = 1;

			double dDelta = 1.0 / tmp32;
			double dPos = 0.0;

			int8_t *ptr8 = &song->sampleData[s->offset+s->length-1];
			for (int32_t j = editor.samplePos; j < s->length; j++)
			{
				int32_t smp32 = (int32_t)((*ptr8) * dPos);
				CLAMP8(smp32);
				*ptr8-- = (int8_t)smp32;

				dPos += dDelta;
			}

			fixSampleBeep(s);
			if (ui.samplerScreenShown)
				displaySample();

			updateWindowTitle(MOD_IS_MODIFIED);
		}
		break;

		case PTB_EO_UPSAMP:
		{
			moduleSample_t *s = &song->samples[editor.currSample];
			if (s->length == 0)
			{
				statusSampleIsEmpty();
				break;
			}

			if (askBox(ASKBOX_YES_NO, "UPSAMPLE ?"))
				upSample();
		}
		break;

		case PTB_EO_DNSAMP:
		{
			moduleSample_t *s = &song->samples[editor.currSample];
			if (s->length == 0)
			{
				statusSampleIsEmpty();
				break;
			}

			if (askBox(ASKBOX_YES_NO, "DOWNSAMPLE ?"))
				downSample();
		}
		break;

		case PTB_EO_VOL_NUM:
		{
			if (mouse.rightButtonPressed)
			{
				editor.sampleVol = 100;
				ui.updateVolText = true;
			}
			else
			{
				textEdit.tmpDisp16 = editor.sampleVol;
				editor.sampleVolDisp = &textEdit.tmpDisp16;
				textEdit.numPtr16 = &textEdit.tmpDisp16;
				textEdit.numDigits = 3;
				textEdit.cursorStartX = 248;
				textEdit.cursorStartY = 96;
				enterNumberEditMode(TEXT_EDIT_DECIMAL, PTB_EO_VOL_NUM);
			}
		}
		break;

		case PTB_EO_VOL:
		{
			if (editor.sampleZero)
			{
				statusNotSampleZero();
				break;
			}

			moduleSample_t *s = &song->samples[editor.currSample];
			if (s->length == 0)
			{
				statusSampleIsEmpty();
				break;
			}

			if (editor.sampleVol != 100)
			{
				int8_t *ptr8 = &song->sampleData[s->offset];
				int32_t sampleMul = (((1UL << 19) * editor.sampleVol) + 50) / 100;

				for (int32_t j = 0; j < s->length; j++)
				{
					int16_t tmp16 = (ptr8[j] * sampleMul) >> 19;
					CLAMP8(tmp16);
					ptr8[j] = (int8_t)tmp16;
				}

				fixSampleBeep(s);
				if (ui.samplerScreenShown)
					displaySample();

				updateWindowTitle(MOD_IS_MODIFIED);
			}
		}
		break;

		case PTB_EO_VOL_UP: edVolUpButton(); break;
		case PTB_EO_VOL_DOWN: edVolDownButton(); break;
		// ----------------------------------------------------------

		// Edit Op. Screen #4 (chord maker)

		case PTB_EO_DOCHORD:
		{
			if (askBox(ASKBOX_YES_NO, "MAKE CHORD?"))
				mixChordSample();
		}
		break;

		case PTB_EO_NOTE1: selectChordNote1(); break;
		case PTB_EO_NOTE2: selectChordNote2(); break;
		case PTB_EO_NOTE3: selectChordNote3(); break;
		case PTB_EO_NOTE4: selectChordNote4(); break;
		case PTB_EO_NOTE1_UP: edNote1UpButton(); break;
		case PTB_EO_NOTE1_DOWN: edNote1DownButton(); break;
		case PTB_EO_NOTE2_UP: edNote2UpButton(); break;
		case PTB_EO_NOTE2_DOWN: edNote2DownButton(); break;
		case PTB_EO_NOTE3_UP: edNote3UpButton(); break;
		case PTB_EO_NOTE3_DOWN: edNote3DownButton(); break;
		case PTB_EO_NOTE4_UP: edNote4UpButton(); break;
		case PTB_EO_NOTE4_DOWN: edNote4DownButton(); break;
		case PTB_EO_RESET: resetChord(); break;
		case PTB_EO_UNDO: undoChord(); break;

		case PTB_EO_LENGTH:
		{
			if (config.maxSampleLength != 65534 && mouse.x > 157) // yuck!
				break;

			toggleChordLength();
		}
		break;

		case PTB_EO_MAJOR: setChordMajor(); break;
		case PTB_EO_MINOR: setChordMinor(); break;
		case PTB_EO_SUS4: setChordSus4(); break;
		case PTB_EO_MAJOR7: setChordMajor7(); break;
		case PTB_EO_MINOR7: setChordMinor7(); break;
		case PTB_EO_MAJOR6: setChordMajor6(); break;
		case PTB_EO_MINOR6: setChordMinor6(); break;
		// ----------------------------------------------------------

		case PTB_ABOUT:
		{
			ui.aboutScreenShown ^= 1;

			if (ui.aboutScreenShown)
				renderAboutScreen();
			else if (ui.visualizerMode == VISUAL_QUADRASCOPE)
				renderQuadrascopeBg();
			else if (ui.visualizerMode == VISUAL_SPECTRUM)
				renderSpectrumAnalyzerBg();
		}
		break;

		case PTB_PE_PATT:
		{
			if (editor.currMode == MODE_IDLE || editor.currMode == MODE_EDIT)
			{
				textEdit.tmpDisp16 = song->currPos;
				if (textEdit.tmpDisp16 > song->header.songLength-1)
					textEdit.tmpDisp16 = song->header.songLength-1;

				textEdit.tmpDisp16 = song->header.patternTable[textEdit.tmpDisp16];
				editor.currPosEdPattDisp = &textEdit.tmpDisp16;
				textEdit.numPtr16 = &textEdit.tmpDisp16;
				textEdit.numDigits = 2;
				textEdit.cursorStartX = 160;
				textEdit.cursorStartY = 58;
				enterNumberEditMode(TEXT_EDIT_DECIMAL, PTB_PE_PATT);
			}
		}
		break;

		case PTB_PE_PATTNAME: posEdEditName(); break;
		case PTB_PE_SCROLLTOP: posEdScrollToTop(); break;
		case PTB_PE_SCROLLUP: posEdScrollUp(); break;
		case PTB_PE_SCROLLDOWN: posEdScrollDown(); break;
		case PTB_PE_SCROLLBOT: posEdScrollToBottom(); break;

		case PTB_PE_EXIT:
		{
			ui.aboutScreenShown = false;
			ui.posEdScreenShown = false;
			displayMainScreen();
		}
		break;

		case PTB_POS:
		case PTB_POSED:
			posEdToggle();
		break;

		case PTB_POSS:
		{
			if (editor.currMode == MODE_IDLE || editor.currMode == MODE_EDIT)
			{
				if (mouse.rightButtonPressed)
				{
					song->currPos = 0;
					editor.currPatternDisp = &song->header.patternTable[song->currPos];

					if (ui.posEdScreenShown)
						ui.updatePosEd = true;
				}
				else
				{
					textEdit.tmpDisp16 = song->currPos;
					editor.currPosDisp = &textEdit.tmpDisp16;
					textEdit.numPtr16 = &textEdit.tmpDisp16;
					textEdit.numDigits = 3;
					textEdit.cursorStartX = 72;
					textEdit.cursorStartY = 8;
					enterNumberEditMode(TEXT_EDIT_DECIMAL, PTB_POSS);
				}
			}
		}
		break;

		case PTB_PATTERNS:
		{
			if (editor.currMode == MODE_IDLE || editor.currMode == MODE_EDIT)
			{
				if (mouse.rightButtonPressed)
				{
					song->header.patternTable[song->currPos] = 0;

					ui.updateSongSize = true;
					updateWindowTitle(MOD_IS_MODIFIED);

					if (ui.posEdScreenShown)
						ui.updatePosEd = true;
				}
				else
				{
					textEdit.tmpDisp16 = song->header.patternTable[song->currPos];
					editor.currPatternDisp = &textEdit.tmpDisp16;
					textEdit.numPtr16 = &textEdit.tmpDisp16;
					textEdit.numDigits = 2;
					textEdit.cursorStartX = 80;
					textEdit.cursorStartY = 19;
					enterNumberEditMode(TEXT_EDIT_DECIMAL, PTB_PATTERNS);
				}
			}
		}
		break;

		case PTB_LENGTHS:
		{
			if (editor.currMode == MODE_IDLE || editor.currMode == MODE_EDIT)
			{
				if (mouse.rightButtonPressed)
				{
					song->header.songLength = 1;

					int16_t tmp16 = song->currPos;
					if (tmp16 > song->header.songLength-1)
						tmp16 = song->header.songLength-1;

					editor.currPosEdPattDisp = &song->header.patternTable[tmp16];

					ui.updateSongSize = true;
					updateWindowTitle(MOD_IS_MODIFIED);

					if (ui.posEdScreenShown)
						ui.updatePosEd = true;
				}
				else
				{
					textEdit.tmpDisp16 = song->header.songLength;
					editor.currLengthDisp = &textEdit.tmpDisp16;
					textEdit.numPtr16 = &textEdit.tmpDisp16;
					textEdit.numDigits = 3;
					textEdit.cursorStartX = 72;
					textEdit.cursorStartY = 30;
					enterNumberEditMode(TEXT_EDIT_DECIMAL, PTB_LENGTHS);
				}
			}
		}
		break;

		// pattern editor main area (sets current pattern number to edit)
		case PTB_PATTBOX:
		case PTB_PATTDATA:
		{
			if (!ui.introTextShown && (editor.currMode == MODE_IDLE || editor.currMode == MODE_EDIT || editor.playMode != PLAY_MODE_NORMAL))
			{
				// limit click coordinate to pattern edit number box if needed
				if (config.restrictedPattEditClick && (mouse.x < 6 || mouse.y < 124 || mouse.x >= 26 || mouse.y >= 135))
					break;

				textEdit.tmpDisp16 = song->currPattern;
				editor.currEditPatternDisp = &textEdit.tmpDisp16;
				textEdit.numPtr16 = &textEdit.tmpDisp16;
				textEdit.numDigits = 2;
				textEdit.cursorStartX = 8;
				textEdit.cursorStartY = 132;
				enterNumberEditMode(TEXT_EDIT_DECIMAL, PTB_PATTDATA);
			}
		}
		break;

		case PTB_SAMPLES:
		{
			if (editor.sampleZero)
			{
				editor.sampleZero = false;
				ui.updateCurrSampleNum = true;
			}

			textEdit.tmpDisp8 = editor.currSample;
			editor.currSampleDisp = &textEdit.tmpDisp8;
			textEdit.numPtr8 = &textEdit.tmpDisp8;
			textEdit.numDigits = 2;
			textEdit.numBits = 8;
			textEdit.cursorStartX = 80;
			textEdit.cursorStartY = 52;
			enterNumberEditMode(TEXT_EDIT_HEX, PTB_SAMPLES);
		}
		break;

		case PTB_SVOLUMES:
		{
			if (editor.sampleZero)
			{
				statusNotSampleZero();
				break;
			}

			if (mouse.rightButtonPressed)
			{
				song->samples[editor.currSample].volume = 0;
			}
			else
			{
				textEdit.tmpDisp8 = song->samples[editor.currSample].volume;
				song->samples[editor.currSample].volumeDisp = &textEdit.tmpDisp8;
				textEdit.numPtr8 = &textEdit.tmpDisp8;
				textEdit.numDigits = 2;
				textEdit.numBits = 8;
				textEdit.cursorStartX = 80;
				textEdit.cursorStartY = 63;
				enterNumberEditMode(TEXT_EDIT_HEX, PTB_SVOLUMES);
			}
		}
		break;

		case PTB_SLENGTHS:
		{
			if (config.maxSampleLength == 65534 && mouse.x < 62) // yuck!
				break;

			if (editor.sampleZero)
			{
				statusNotSampleZero();
				break;
			}

			if (mouse.rightButtonPressed)
			{
				moduleSample_t *s = &song->samples[editor.currSample];

				turnOffVoices();

				s->length = 0;
				if (s->loopStart+s->loopLength > 2)
				{
					if (s->length < s->loopStart+s->loopLength)
						s->length = s->loopStart+s->loopLength;
				}

				ui.updateSongSize = true;
				ui.updateCurrSampleLength = true;

				if (ui.samplerScreenShown)
					redrawSample();

				recalcChordLength();
				updateWindowTitle(MOD_IS_MODIFIED);
			}
			else
			{
				textEdit.force32BitNumPtr = true;

				textEdit.tmpDisp32 = song->samples[editor.currSample].length;
				song->samples[editor.currSample].lengthDisp = &textEdit.tmpDisp32;
				textEdit.numPtr32 = &textEdit.tmpDisp32;

				if (config.maxSampleLength == 65534)
				{
					textEdit.numDigits = 4;
					textEdit.numBits = 16;
					textEdit.cursorStartX = 64;
					textEdit.cursorStartY = 74;
				}
				else
				{
					textEdit.numDigits = 5;
					textEdit.numBits = 17;
					textEdit.cursorStartX = 56;
					textEdit.cursorStartY = 74;
				}

				enterNumberEditMode(TEXT_EDIT_HEX, PTB_SLENGTHS);
			}
		}
		break;

		case PTB_SREPEATS:
		{
			if (config.maxSampleLength == 65534 && mouse.x < 62) // yuck!
				break;

			if (editor.sampleZero)
			{
				statusNotSampleZero();
				break;
			}

			if (mouse.rightButtonPressed)
			{
				moduleSample_t *s = &song->samples[editor.currSample];

				s->loopStart = 0;
				if (s->length >= s->loopLength)
				{
					if (s->loopStart+s->loopLength > s->length)
						s->loopStart = s->length-s->loopLength;
				}
				else
				{
					s->loopStart = 0;
				}

				ui.updateCurrSampleRepeat = true;
				if (ui.editOpScreenShown && ui.editOpScreen == 3)
					ui.updateChordLengthText = true;

				if (ui.samplerScreenShown)
					setLoopSprites();

				updatePaulaLoops();
				updateWindowTitle(MOD_IS_MODIFIED);
			}
			else
			{
				textEdit.force32BitNumPtr = true;

				textEdit.tmpDisp32 = song->samples[editor.currSample].loopStart;
				song->samples[editor.currSample].loopStartDisp = &textEdit.tmpDisp32;
				textEdit.numPtr32 = &textEdit.tmpDisp32;

				if (config.maxSampleLength == 65534)
				{
					textEdit.numDigits = 4;
					textEdit.numBits = 16;
					textEdit.cursorStartX = 64;
					textEdit.cursorStartY = 85;
				}
				else
				{
					textEdit.numDigits = 5;
					textEdit.numBits = 17;
					textEdit.cursorStartX = 56;
					textEdit.cursorStartY = 85;
				}

				enterNumberEditMode(TEXT_EDIT_HEX, PTB_SREPEATS);
			}
		}
		break;

		case PTB_SREPLENS:
		{
			if (config.maxSampleLength == 65534 && mouse.x < 62) // yuck!
				break;

			if (editor.sampleZero)
			{
				statusNotSampleZero();
				break;
			}

			if (mouse.rightButtonPressed)
			{
				moduleSample_t *s = &song->samples[editor.currSample];

				s->loopLength = 0;
				if (s->length >= s->loopStart)
				{
					if (s->loopStart+s->loopLength > s->length)
						s->loopLength = s->length-s->loopStart;
				}
				else
				{
					s->loopLength = 2;
				}

				if (s->loopLength < 2)
					s->loopLength = 2;

				ui.updateCurrSampleReplen = true;
				if (ui.editOpScreenShown && ui.editOpScreen == 3)
					ui.updateChordLengthText = true;

				if (ui.samplerScreenShown)
					setLoopSprites();

				updatePaulaLoops();
				updateWindowTitle(MOD_IS_MODIFIED);
			}
			else
			{
				textEdit.force32BitNumPtr = true;

				textEdit.tmpDisp32 = song->samples[editor.currSample].loopLength;
				song->samples[editor.currSample].loopLengthDisp = &textEdit.tmpDisp32;
				textEdit.numPtr32 = &textEdit.tmpDisp32;

				if (config.maxSampleLength == 0xFFFE)
				{
					textEdit.numDigits = 4;
					textEdit.numBits = 16;
					textEdit.cursorStartX = 64;
					textEdit.cursorStartY = 96;
				}
				else
				{
					textEdit.numDigits = 5;
					textEdit.numBits = 17;
					textEdit.cursorStartX = 56;
					textEdit.cursorStartY = 96;
				}

				enterNumberEditMode(TEXT_EDIT_HEX, PTB_SREPLENS);
			}
		}
		break;

		case PTB_EDITOP:
		{
			if (ui.editOpScreenShown)
			{
				if (ui.editOpScreen == 3)
					ui.editOpScreen = 0;
				else
					ui.editOpScreen = (ui.editOpScreen + 1) % 3;
			}
			else
			{
				ui.editOpScreenShown = true;
			}

			renderEditOpScreen();
		}
		break;

		case PTB_DO_LOADMODULE:
		{
			diskop.mode = DISKOP_MODE_MOD;
			setPathFromDiskOpMode();
			diskop.scrollOffset = 0;
			diskop.cached = false;
			ui.updateDiskOpFileList = true;
			ui.updateLoadMode = true;
		}
		break;

		case PTB_DO_LOADSAMPLE:
		{
			diskop.mode = DISKOP_MODE_SMP;
			setPathFromDiskOpMode();
			diskop.scrollOffset = 0;
			diskop.cached = false;
			ui.updateDiskOpFileList = true;
			ui.updateLoadMode = true;
		}
		break;

		case PTB_LOADSAMPLE: // "LOAD" button next to sample name
		{
			ui.posEdScreenShown = false;
			diskop.mode = DISKOP_MODE_SMP;
			setPathFromDiskOpMode();
			diskop.scrollOffset = 0;
			diskop.cached = false;

			if (!ui.diskOpScreenShown)
			{
				ui.diskOpScreenShown = true;
				renderDiskOpScreen();
			}
			else
			{
				ui.updateDiskOpFileList = true;
				ui.updateLoadMode = true;
			}
		}
		break;

		case PTB_DO_SAVESAMPLE:
		{
			bool changeLoadMode = (diskop.mode != DISKOP_MODE_SMP);
			if (changeLoadMode)
			{
				diskop.mode = DISKOP_MODE_SMP;
				setPathFromDiskOpMode();
				ui.updateLoadMode = true; // redraw load mode cursor
				updateDiskOp();
			}

			if (askBox(ASKBOX_YES_NO, "SAVE SAMPLE ?"))
				saveSample(CHECK_IF_FILE_EXIST, DONT_GIVE_NEW_FILENAME); // also updates file list

			// if we changed mode, re-read directory in Disk Op.
			if (changeLoadMode)
			{
				diskop.scrollOffset = 0;
				diskop.cached = false; // read new directory
				ui.updateDiskOpFileList = true;
				updateDiskOp();
			}
		}
		break;

		case PTB_MOD2WAV:
		{
			if (askBox(ASKBOX_MOD2WAV, "PLEASE SELECT"))
			{
				char fileName[20 + 4 + 1];

				memset(fileName, 0, sizeof (fileName));

				if (song->header.name[0] != '\0')
				{
					for (int32_t i = 0; i < 20; i++)
					{
						fileName[i] = (char)tolower(song->header.name[i]);
						if (fileName[i] == '\0') break;
						sanitizeFilenameChar(&fileName[i]);
					}

					strcat(fileName, ".wav");
				}
				else
				{
					strcpy(fileName, "untitled.wav");
				}

				mod2WavRender(fileName);
			}
		}
		break;

		case PTB_SA_SAMPLE:
		{
			ui.samplingBoxShown = true;
			renderSamplingBox();
		}
		break;

		case PTB_SA_RESAMPLE:
		{
			if (askBox(ASKBOX_YES_NO, "RESAMPLE?"))
				samplerResample();
		}
		break;

		case PTB_SA_RESAMPLENOTE:
		{
			ui.changingSmpResample = true;
			ui.updateResampleNote = true;
			setStatusMessage("SELECT NOTE", NO_CARRY);
			pointerSetMode(POINTER_MODE_MSG1, NO_CARRY);
		}
		break;

		case PTB_SA_SAMPLEAREA:
		{
			if (ui.sampleMarkingPos == -1)
			{
				samplerSamplePressed(MOUSE_BUTTON_NOT_HELD);
				return true;
			}
		}
		break;

		case PTB_SA_ZOOMBARAREA:
		{
			mouse.lastGUIButton = button;
			if (!ui.forceSampleDrag)
			{
				samplerBarPressed(MOUSE_BUTTON_NOT_HELD);
				return true;
			}
		}
		break;

		case PTB_SA_FIXDC: samplerRemoveDcOffset(); break;
		case PTB_SA_CUT: samplerSamDelete(SAMPLE_CUT); break;
		case PTB_SA_PASTE: samplerSamPaste(); break;
		case PTB_SA_COPY: samplerSamCopy(); break;
		case PTB_SA_LOOP: samplerLoopToggle(); break;
		case PTB_SA_PLAYWAVE: samplerPlayWaveform(); break;
		case PTB_SA_PLAYDISPLAY: samplerPlayDisplay(); break;
		case PTB_SA_PLAYRANGE: samplerPlayRange(); break;
		case PTB_SA_RANGEALL: samplerRangeAll(); break;
		case PTB_SA_SHOWALL: samplerShowAll(); break;
		case PTB_SA_SHOWRANGE: samplerShowRange(); break;
		case PTB_SA_RANGECENTER: sampleMarkerToCenter(); break;
		case PTB_SA_RANGEBEG: sampleMarkerToBeg(); break;
		case PTB_SA_RANGEEND: sampleMarkerToEnd(); break;
		case PTB_SA_ZOOMOUT: samplerZoomOut2x(); break;

		case PTB_SA_VOLUME:
		{
			ui.samplerVolBoxShown = true;
			renderSamplerVolBox();
		}
		break;

		case PTB_SA_FILTERS:
		{
			ui.samplerFiltersBoxShown = true;
			renderSamplerFiltersBox();
			fillSampleFilterUndoBuffer();
		}
		break;

		case PTB_SA_STOP: turnOffVoices(); break;

		case PTB_DO_REFRESH:
		{
			diskop.scrollOffset = 0;
			diskop.cached = false;
			ui.updateDiskOpFileList = true;
		}
		break;

		// TODO: Find a PowerPacker packer and enable this
		// case PTB_DO_PACKMOD: diskop.modPackFlg ^= 1; break;

		case PTB_DO_SAMPLEFORMAT:
		{
			diskop.smpSaveType = (diskop.smpSaveType + 1) % 3;
			ui.updateSaveFormatText = true;
		}
		break;

		case PTB_DO_MODARROW:
		{
			diskop.mode = DISKOP_MODE_MOD;
			diskop.scrollOffset = 0;
			diskop.cached = false;
			ui.updateDiskOpFileList = true;
			ui.updateLoadMode = true;
		}
		break;

		case PTB_DO_SAMPLEARROW:
		{
			diskop.mode = DISKOP_MODE_SMP;
			diskop.scrollOffset = 0;
			diskop.cached = false;
			ui.updateDiskOpFileList = true;
			ui.updateLoadMode = true;
		}
		break;

		case PTB_SA_TUNETONE: toggleTuningTone(); break;

		case PTB_POSINS: posEdInsert(); break;
		case PTB_POSDEL: posEdDelete(); break;

		case PTB_DO_SAVEMODULE:
		{
			bool changeLoadMode = (diskop.mode != DISKOP_MODE_MOD);
			if (changeLoadMode)
			{
				diskop.mode = DISKOP_MODE_MOD;
				setPathFromDiskOpMode();
				ui.updateLoadMode = true; // redraw load mode cursor
				updateDiskOp();
			}

			if (askBox(ASKBOX_YES_NO, "SAVE MODULE ?"))
				saveModule(CHECK_IF_FILE_EXIST, DONT_GIVE_NEW_FILENAME); // also updates file list

			// if we changed mode, re-read directory in Disk Op.
			if (changeLoadMode)
			{
				diskop.scrollOffset = 0;
				diskop.cached = false; // read new directory
				ui.updateDiskOpFileList = true;
				updateDiskOp();
			}
		}
		break;

		case PTB_DO_DATAPATH:
		{
			if (mouse.rightButtonPressed)
			{
				memset(editor.currPath, 0, PATH_MAX + 1);
				ui.updateDiskOpPathText = true;
			}

			textEdit.textStartPtr = editor.currPath;
			textEdit.textEndPtr = &editor.currPath[PATH_MAX - 1];
			textEdit.numBlocks = 26;
			textEdit.cursorStartX = 24;
			textEdit.cursorStartY = 30;
			textEdit.scrollable = true;
			enterTextEditMode(PTB_DO_DATAPATH);
		}
		break;

		case PTB_SONGNAME:
		{
			if (mouse.rightButtonPressed)
			{
				memset(song->header.name, 0, sizeof (song->header.name));
				ui.updateSongName = true;
				updateWindowTitle(MOD_IS_MODIFIED);
			}
			else
			{
				textEdit.textStartPtr = song->header.name;
				textEdit.textEndPtr = song->header.name + 19;
				textEdit.numBlocks = 20;
				textEdit.cursorStartX = 104;
				textEdit.cursorStartY = 107;
				textEdit.scrollable = false;
				enterTextEditMode(PTB_SONGNAME);
			}
		}
		break;

		case PTB_SAMPLENAME:
		{
			if (mouse.rightButtonPressed)
			{
				memset(song->samples[editor.currSample].text, 0, sizeof (song->samples[editor.currSample].text));
				ui.updateCurrSampleName = true;
				updateWindowTitle(MOD_IS_MODIFIED);
			}
			else
			{
				textEdit.textStartPtr = song->samples[editor.currSample].text;
				textEdit.textEndPtr = song->samples[editor.currSample].text + 21;
				textEdit.numBlocks = 22;
				textEdit.cursorStartX = 104;
				textEdit.cursorStartY = 118;
				textEdit.scrollable = false;
				enterTextEditMode(PTB_SAMPLENAME);
			}
		}
		break;

		case PTB_VISUALS:
		{
			if (ui.aboutScreenShown)
			{
				ui.aboutScreenShown = false;
			}
			else if (!mouse.rightButtonPressed)
			{
				ui.visualizerMode = (ui.visualizerMode + 1) % 2;
				if (ui.visualizerMode == VISUAL_SPECTRUM)
					memset((int8_t *)editor.spectrumVolumes, 0, sizeof (editor.spectrumVolumes));
			}

			if (ui.visualizerMode == VISUAL_QUADRASCOPE)
				renderQuadrascopeBg();
			else if (ui.visualizerMode == VISUAL_SPECTRUM)
				renderSpectrumAnalyzerBg();
		}
		break;

		case PTB_QUIT:
		{
			if (askBox(ASKBOX_YES_NO, "REALLY QUIT ?"))
				ui.throwExit = true;
		}
		break;

		case PTB_CHAN1:
		{
			if (mouse.rightButtonPressed)
			{
				editor.muted[0] = false;
				editor.muted[1] = true;
				editor.muted[2] = true;
				editor.muted[3] = true;
			}
			else
			{
				editor.muted[0] ^= 1;
			}

			renderMuteButtons();
		}
		break;

		case PTB_CHAN2:
		{
			if (mouse.rightButtonPressed)
			{
				editor.muted[0] = true;
				editor.muted[1] = false;
				editor.muted[2] = true;
				editor.muted[3] = true;
			}
			else
			{
				editor.muted[1] ^= 1;
			}

			renderMuteButtons();
		}
		break;

		case PTB_CHAN3:
		{
			if (mouse.rightButtonPressed)
			{
				editor.muted[0] = true;
				editor.muted[1] = true;
				editor.muted[2] = false;
				editor.muted[3] = true;
			}
			else
			{
				editor.muted[2] ^= 1;
			}

			renderMuteButtons();
		}
		break;

		case PTB_CHAN4:
		{
			if (mouse.rightButtonPressed)
			{
				editor.muted[0] = true;
				editor.muted[1] = true;
				editor.muted[2] = true;
				editor.muted[3] = false;
			}
			else
			{
				editor.muted[3] ^= 1;
			}

			renderMuteButtons();
		}
		break;

		case PTB_SAMPLER: samplerScreen(); break;
		case PTB_SA_EXIT: exitFromSam();   break;

		case PTB_DO_FILEAREA: diskOpLoadFile((mouse.y - 34) / 6, true); break;
		case PTB_DO_PARENT:
		{
#ifdef _WIN32
			diskOpSetPath(L"..", DISKOP_CACHE);
#else
			diskOpSetPath("..", DISKOP_CACHE);
#endif
		}
		break;

		case PTB_DISKOP:
		{
			editor.blockMarkFlag = false;
			ui.diskOpScreenShown = true;
			renderDiskOpScreen();
		}
		break;

		case PTB_DO_EXIT:
		{
			ui.aboutScreenShown = false;
			ui.diskOpScreenShown = false;
			editor.blockMarkFlag = false;
			pointerSetPreviousMode();
			setPrevStatusMessage();
			displayMainScreen();
		}
		break;

		case PTB_DO_SCROLLUP:
		{
			if (diskop.scrollOffset > 0)
			{
				diskop.scrollOffset--;
				ui.updateDiskOpFileList = true;
			}
		}
		break;

		case PTB_DO_SCROLLTOP:
		{
			diskop.scrollOffset = 0;
			ui.updateDiskOpFileList = true;
		}
		break;

		case PTB_DO_SCROLLDOWN:
		{
			if (diskop.numEntries > DISKOP_LINES && diskop.scrollOffset < diskop.numEntries-DISKOP_LINES)
			{
				diskop.scrollOffset++;
				ui.updateDiskOpFileList = true;
			}
		}
		break;

		case PTB_DO_SCROLLBOT:
		{
			if (diskop.numEntries > DISKOP_LINES)
			{
				diskop.scrollOffset = diskop.numEntries - DISKOP_LINES;
				ui.updateDiskOpFileList = true;
			}
		}
		break;

		case PTB_STOP:
		{
			editor.playMode = PLAY_MODE_NORMAL;
			modStop();
			editor.currMode = MODE_IDLE;
			pointerSetMode(POINTER_MODE_IDLE, DO_CARRY);
			statusAllRight();
		}
		break;

		case PTB_PLAY:
		{
			editor.playMode = PLAY_MODE_NORMAL;

			if (mouse.rightButtonPressed)
				modPlay(DONT_SET_PATTERN, song->currPos, song->currRow);
			else
				modPlay(DONT_SET_PATTERN, song->currPos, DONT_SET_ROW);

			editor.currMode = MODE_PLAY;
			pointerSetMode(POINTER_MODE_PLAY, DO_CARRY);
			statusAllRight();
		}
		break;

		case PTB_PATTERN:
		{
			editor.playMode = PLAY_MODE_PATTERN;

			if (mouse.rightButtonPressed)
				modPlay(song->currPattern, DONT_SET_ORDER, song->currRow);
			else
				modPlay(song->currPattern, DONT_SET_ORDER, DONT_SET_ROW);

			editor.currMode = MODE_PLAY;
			pointerSetMode(POINTER_MODE_PLAY, DO_CARRY);
			statusAllRight();
		}
		break;

		case PTB_EDIT:
		{
			if (!ui.samplerScreenShown)
			{
				editor.playMode = PLAY_MODE_NORMAL;
				modStop();
				editor.currMode = MODE_EDIT;
				pointerSetMode(POINTER_MODE_EDIT, DO_CARRY);
				statusAllRight();
			}
		}
		break;

		case PTB_RECORD:
		{
			if (!ui.samplerScreenShown)
			{
				editor.playMode = PLAY_MODE_PATTERN;

				if (mouse.rightButtonPressed)
					modPlay(song->currPattern, DONT_SET_ORDER, song->currRow);
				else
					modPlay(song->currPattern, DONT_SET_ORDER, DONT_SET_ROW);

				editor.currMode = MODE_RECORD;
				pointerSetMode(POINTER_MODE_EDIT, DO_CARRY);
				statusAllRight();
			}
		}
		break;

		case PTB_CLEAR:
		{
			int32_t result = askBox(ASKBOX_CLEAR, "PLEASE SELECT");
			if (result == ASKBOX_CLEAR_CANCEL)
				break;

			editor.playMode = PLAY_MODE_NORMAL;
			modStop();

			if (result == ASKBOX_CLEAR_SONG)
			{
				clearSong();
			}
			else if (result == ASKBOX_CLEAR_SAMPLES)
			{
				clearSamples();
			}
			else if (result == ASKBOX_CLEAR_ALL)
			{
				clearSong();
				clearSamples();
			}

			editor.currMode = MODE_IDLE;
			pointerSetMode(POINTER_MODE_IDLE, DO_CARRY);
			statusAllRight();

			// hide edit op. and about screen, and redraw visualizer

			ui.editOpScreenShown = false;
			ui.aboutScreenShown = false;

			     if (ui.visualizerMode == VISUAL_QUADRASCOPE) renderQuadrascopeBg();
			else if (ui.visualizerMode == VISUAL_SPECTRUM) renderSpectrumAnalyzerBg();

			updateWindowTitle(MOD_IS_MODIFIED);
		}
		break;

		case PTB_SAMPLEU:
		{
			if (mouse.rightButtonPressed)
			{
				editor.sampleZero = true;
				ui.updateCurrSampleNum = true;
			}
			else
			{
				sampleUpButton();
			}
		}
		break;

		case PTB_SAMPLED:
		{
			if (mouse.rightButtonPressed)
			{
				editor.sampleZero = true;
				ui.updateCurrSampleNum = true;
			}
			else
			{
				sampleDownButton();
			}
		}
		break;

		case PTB_FTUNEU:
		{
			if (!editor.sampleZero && (song->samples[editor.currSample].fineTune & 0xF) != 7)
			{
				sampleFineTuneUpButton();
				updateWindowTitle(MOD_IS_MODIFIED);
			}
		}
		break;

		case PTB_FTUNED:
		{
			if (!editor.sampleZero && (song->samples[editor.currSample].fineTune & 0xF) != 8)
			{
				sampleFineTuneDownButton();
				updateWindowTitle(MOD_IS_MODIFIED);
			}
		}
		break;

		case PTB_SVOLUMEU:
		{
			if (!editor.sampleZero && song->samples[editor.currSample].volume < 64)
			{
				sampleVolumeUpButton();
				updateWindowTitle(MOD_IS_MODIFIED);
			}
		}
		break;

		case PTB_SVOLUMED:
		{
			if (!editor.sampleZero && song->samples[editor.currSample].volume > 0)
			{
				sampleVolumeDownButton();
				updateWindowTitle(MOD_IS_MODIFIED);
			}
		}
		break;

		case PTB_SLENGTHU:
		{
			if (!editor.sampleZero && song->samples[editor.currSample].length < config.maxSampleLength)
			{
				sampleLengthUpButton(INCREMENT_SLOW);
				updateWindowTitle(MOD_IS_MODIFIED);
			}
		}
		break;

		case PTB_SLENGTHD:
		{
			if (!editor.sampleZero && song->samples[editor.currSample].length > 0)
			{
				sampleLengthDownButton(INCREMENT_SLOW);
				updateWindowTitle(MOD_IS_MODIFIED);
			}
		}
		break;

		case PTB_SREPEATU:
		{
			if (!editor.sampleZero)
			{
				int32_t oldVal = song->samples[editor.currSample].loopStart;
				sampleRepeatUpButton(INCREMENT_SLOW);
				if (song->samples[editor.currSample].loopStart != oldVal)
					updateWindowTitle(MOD_IS_MODIFIED);
			}
		}
		break;

		case PTB_SREPEATD:
		{
			if (!editor.sampleZero)
			{
				int32_t oldVal = song->samples[editor.currSample].loopStart;
				sampleRepeatDownButton(INCREMENT_SLOW);
				if (song->samples[editor.currSample].loopStart != oldVal)
					updateWindowTitle(MOD_IS_MODIFIED);
			}
		}
		break;

		case PTB_SREPLENU:
		{
			if (!editor.sampleZero)
			{
				int32_t oldVal = song->samples[editor.currSample].loopLength;
				sampleRepeatLengthUpButton(INCREMENT_SLOW);
				if (song->samples[editor.currSample].loopLength != oldVal)
					updateWindowTitle(MOD_IS_MODIFIED);
			}
		}
		break;

		case PTB_SREPLEND:
		{
			if (!editor.sampleZero)
			{
				int32_t oldVal = song->samples[editor.currSample].loopLength;
				sampleRepeatLengthDownButton(INCREMENT_SLOW);
				if (song->samples[editor.currSample].loopLength != oldVal)
					updateWindowTitle(MOD_IS_MODIFIED);
			}
		}
		break;

		case PTB_TEMPOU: tempoUpButton(); break;
		case PTB_TEMPOD: tempoDownButton(); break;

		case PTB_LENGTHU:
		{
			if (song->header.songLength < 128)
			{
				songLengthUpButton();
				updateWindowTitle(MOD_IS_MODIFIED);
			}
		}
		break;

		case PTB_LENGTHD:
		{
			if (song->header.songLength > 1)
			{
				songLengthDownButton();
				updateWindowTitle(MOD_IS_MODIFIED);
			}
		}
		break;

		case PTB_PATTERNU:
		{
			if (song->header.patternTable[song->currPos] < 99)
			{
				patternUpButton();
				updateWindowTitle(MOD_IS_MODIFIED);
			}
		}
		break;

		case PTB_PATTERND:
		{
			if (song->header.patternTable[song->currPos] > 0)
			{
				patternDownButton();
				updateWindowTitle(MOD_IS_MODIFIED);
			}
		}
		break;

		case PTB_POSU: positionUpButton(); break;
		case PTB_POSD: positionDownButton(); break;

		default: displayErrorMsg("NOT IMPLEMENTED"); return false; // button not mapped
	}

	mouse.lastGUIButton = button;
	return false;
}

static void handleRepeatedGUIButtons(void)
{
	// repeat button
	switch (mouse.lastGUIButton)
	{
		case PTB_EO_NOTE1_UP:
		{
			if (mouse.repeatCounter >= 4)
			{
				mouse.repeatCounter = 0;
				edNote1UpButton();
			}
		}
		break;

		case PTB_EO_NOTE1_DOWN:
		{
			if (mouse.repeatCounter >= 4)
			{
				mouse.repeatCounter = 0;
				edNote1DownButton();
			}
		}
		break;

		case PTB_EO_NOTE2_UP:
		{
			if (mouse.repeatCounter >= 4)
			{
				mouse.repeatCounter = 0;
				edNote2UpButton();
			}
		}
		break;

		case PTB_EO_NOTE2_DOWN:
		{
			if (mouse.repeatCounter >= 4)
			{
				mouse.repeatCounter = 0;
				edNote2DownButton();
			}
		}
		break;

		case PTB_EO_NOTE3_UP:
		{
			if (mouse.repeatCounter >= 4)
			{
				mouse.repeatCounter = 0;
				edNote3UpButton();
			}
		}
		break;

		case PTB_EO_NOTE3_DOWN:
		{
			if (mouse.repeatCounter >= 4)
			{
				mouse.repeatCounter = 0;
				edNote3DownButton();
			}
		}
		break;

		case PTB_EO_NOTE4_UP:
		{
			if (mouse.repeatCounter >= 4)
			{
				mouse.repeatCounter = 0;
				edNote4UpButton();
			}
		}
		break;

		case PTB_EO_NOTE4_DOWN:
		{
			if (mouse.repeatCounter >= 4)
			{
				mouse.repeatCounter = 0;
				edNote4DownButton();
			}
		}
		break;

		case PTB_EO_VOL_UP:
		{
			if (mouse.repeatCounter >= 2)
			{
				mouse.repeatCounter = 0;
				edVolUpButton();
			}
		}
		break;

		case PTB_EO_VOL_DOWN:
		{
			if (mouse.repeatCounter >= 2)
			{
				mouse.repeatCounter = 0;
				edVolDownButton();
			}
		}
		break;

		case PTB_EO_MOD_UP:
		{
			if (mouse.repeatCounter >= 2)
			{
				mouse.repeatCounter = 0;
				edModUpButton();
			}
		}
		break;

		case PTB_EO_MOD_DOWN:
		{
			if (mouse.repeatCounter >= 2)
			{
				mouse.repeatCounter = 0;
				edModDownButton();
			}
		}
		break;

		case PTB_EO_POS_UP:
		{
			if (mouse.repeatCounter >= 1)
			{
				mouse.repeatCounter = 0;
				edPosUpButton(INCREMENT_FAST);
			}
		}
		break;

		case PTB_EO_POS_DOWN:
		{
			if (mouse.repeatCounter >= 1)
			{
				mouse.repeatCounter = 0;
				edPosDownButton(INCREMENT_FAST);
			}
		}
		break;

		case PTB_EO_FROM_UP:
		{
			if (mouse.repeatCounter >= 2)
			{
				mouse.repeatCounter = 0;
				if (editor.sampleFrom < 0x1F)
				{
					editor.sampleFrom++;
					ui.updateFromText = true;
				}
			}
		}
		break;

		case PTB_EO_FROM_DOWN:
		{
			if (mouse.repeatCounter >= 2)
			{
				mouse.repeatCounter = 0;
				if (editor.sampleFrom > 0x00)
				{
					editor.sampleFrom--;
					ui.updateFromText = true;
				}
			}
		}
		break;

		case PTB_EO_TO_UP:
		{
			if (mouse.repeatCounter >= 2)
			{
				mouse.repeatCounter = 0;
				if (editor.sampleTo < 0x1F)
				{
					editor.sampleTo++;
					ui.updateToText = true;
				}
			}
		}
		break;

		case PTB_EO_TO_DOWN:
		{
			if (mouse.repeatCounter >= 2)
			{
				mouse.repeatCounter = 0;
				if (editor.sampleTo > 0x00)
				{
					editor.sampleTo--;
					ui.updateToText = true;
				}
			}
		}
		break;

		case PTB_SAMPLEU:
		{
			if (mouse.repeatCounter >= 5)
			{
				mouse.repeatCounter = 0;
				if (!mouse.rightButtonPressed)
					sampleUpButton();
				else
					ui.updateCurrSampleNum = true;
			}
		}
		break;

		case PTB_SAMPLED:
		{
			if (mouse.repeatCounter >= 5)
			{
				mouse.repeatCounter = 0;
				if (!mouse.rightButtonPressed)
					sampleDownButton();
				else
					ui.updateCurrSampleNum = true;
			}
		}
		break;

		case PTB_FTUNEU:
		{
			if (mouse.repeatCounter >= 5)
			{
				mouse.repeatCounter = 0;
				sampleFineTuneUpButton();
			}
		}
		break;

		case PTB_FTUNED:
		{
			if (mouse.repeatCounter >= 5)
			{
				mouse.repeatCounter = 0;
				sampleFineTuneDownButton();
			}
		}
		break;

		case PTB_SVOLUMEU:
		{
			if (mouse.repeatCounter >= 5)
			{
				mouse.repeatCounter = 0;
				sampleVolumeUpButton();
			}
		}
		break;

		case PTB_SVOLUMED:
		{
			if (mouse.repeatCounter >= 5)
			{
				mouse.repeatCounter = 0;
				sampleVolumeDownButton();
			}
		}
		break;

		case PTB_SLENGTHU:
		{
			if (mouse.rightButtonPressed || mouse.repeatCounter >= 1)
			{
				mouse.repeatCounter = 0;
				sampleLengthUpButton(INCREMENT_FAST);
			}
		}
		break;

		case PTB_SLENGTHD:
		{
			if (mouse.rightButtonPressed || mouse.repeatCounter >= 1)
			{
				mouse.repeatCounter = 0;
				sampleLengthDownButton(INCREMENT_FAST);
			}
		}
		break;

		case PTB_SREPEATU:
		{
			if (mouse.rightButtonPressed || mouse.repeatCounter >= 1)
			{
				mouse.repeatCounter = 0;
				sampleRepeatUpButton(INCREMENT_FAST);
			}
		}
		break;

		case PTB_SREPEATD:
		{
			if (mouse.rightButtonPressed || mouse.repeatCounter >= 1)
			{
				mouse.repeatCounter = 0;
				sampleRepeatDownButton(INCREMENT_FAST);
			}
		}
		break;

		case PTB_SREPLENU:
		{
			if (mouse.rightButtonPressed || mouse.repeatCounter >= 1)
			{
				mouse.repeatCounter = 0;
				sampleRepeatLengthUpButton(INCREMENT_FAST);
			}
		}
		break;

		case PTB_SREPLEND:
		{
			if (mouse.rightButtonPressed || mouse.repeatCounter >= 1)
			{
				mouse.repeatCounter = 0;
				sampleRepeatLengthDownButton(INCREMENT_FAST);
			}
		}
		break;

		case PTB_TEMPOU:
		{
			if (mouse.repeatCounter >= 3)
			{
				mouse.repeatCounter = 0;
				tempoUpButton();
			}
		}
		break;

		case PTB_TEMPOD:
		{
			if (mouse.repeatCounter >= 3)
			{
				mouse.repeatCounter = 0;
				tempoDownButton();
			}
		}
		break;

		case PTB_LENGTHU:
		{
			if (mouse.repeatCounter >= 7)
			{
				mouse.repeatCounter = 0;
				songLengthUpButton();
			}
		}
		break;

		case PTB_LENGTHD:
		{
			if (mouse.repeatCounter >= 7)
			{
				mouse.repeatCounter = 0;
				songLengthDownButton();
			}
		}
		break;

		case PTB_PATTERNU:
		{
			if (mouse.repeatCounter >= 7)
			{
				mouse.repeatCounter = 0;
				patternUpButton();
			}
		}
		break;

		case PTB_PATTERND:
		{
			if (mouse.repeatCounter >= 7)
			{
				mouse.repeatCounter = 0;
				patternDownButton();
			}
		}
		break;

		case PTB_POSU:
		{
			if (mouse.repeatCounter >= 7)
			{
				mouse.repeatCounter = 0;
				positionUpButton();
			}
		}
		break;

		case PTB_POSD:
		{
			if (mouse.repeatCounter >= 7)
			{
				mouse.repeatCounter = 0;
				positionDownButton();
			}
		}
		break;

		case PTB_PE_SCROLLUP:
		{
			if (mouse.repeatCounter >= 2)
			{
				mouse.repeatCounter = 0;
				posEdScrollUp();
			}
		}
		break;

		case PTB_PE_SCROLLDOWN:
		{
			if (mouse.repeatCounter >= 2)
			{
				mouse.repeatCounter = 0;
				posEdScrollDown();
			}
		}
		break;

		case PTB_DO_SCROLLUP:
		{
			if (mouse.repeatCounter >= 1)
			{
				mouse.repeatCounter = 0;

				diskop.scrollOffset--;
				if (mouse.rightButtonPressed)
					diskop.scrollOffset -= 3;

				if (diskop.scrollOffset < 0)
					diskop.scrollOffset = 0;

				ui.updateDiskOpFileList = true;
			}
		}
		break;

		case PTB_DO_SCROLLDOWN:
		{
			if (mouse.repeatCounter >= 1)
			{
				mouse.repeatCounter = 0;

				if (diskop.numEntries > DISKOP_LINES)
				{
					diskop.scrollOffset++;
					if (mouse.rightButtonPressed)
						diskop.scrollOffset += 3;

					if (diskop.scrollOffset > diskop.numEntries-DISKOP_LINES)
						diskop.scrollOffset = diskop.numEntries-DISKOP_LINES;

					ui.updateDiskOpFileList = true;
				}
			}
		}
		break;

		case PTB_SA_ZOOMBARAREA:
		{
			if (mouse.repeatCounter >= 4)
			{
				mouse.repeatCounter = 0;
				if (!ui.forceSampleDrag)
					samplerBarPressed(MOUSE_BUTTON_NOT_HELD);
			}
		}
		break;

		default: break;
	}
}
