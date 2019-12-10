// for finding memory leaks in debug mode with Visual Studio 
#if defined _DEBUG && defined _MSC_VER
#include <crtdbg.h>
#endif

#include <stdint.h>
#include <stdbool.h>
#ifndef _WIN32
#include <unistd.h>
#endif
#include <stdio.h>
#include "pt2_header.h"
#include "pt2_mouse.h"
#include "pt2_helpers.h"
#include "pt2_palette.h"
#include "pt2_diskop.h"
#include "pt2_sampler.h"
#include "pt2_modloader.h"
#include "pt2_edit.h"
#include "pt2_sampleloader.h"
#include "pt2_visuals.h"
#include "pt2_tables.h"
#include "pt2_audio.h"
#include "pt2_textout.h"
#include "pt2_keyboard.h"

/* TODO: Move irrelevant routines outta here! Disgusting design!
** Keep in mind that this was programmed in my early programming days... */

extern SDL_Renderer *renderer;
extern SDL_Window *window;

void edNote1UpButton(void);
void edNote1DownButton(void);
void edNote2UpButton(void);
void edNote2DownButton(void);
void edNote3UpButton(void);
void edNote3DownButton(void);
void edNote4UpButton(void);
void edNote4DownButton(void);
void edPosUpButton(bool fast);
void edPosDownButton(bool fast);
void edModUpButton(void);
void edModDownButton(void);
void edVolUpButton(void);
void edVolDownButton(void);
void sampleUpButton(void);
void sampleDownButton(void);
void sampleFineTuneUpButton(void);
void sampleFineTuneDownButton(void);
void sampleVolumeUpButton(void);
void sampleVolumeDownButton(void);
void sampleLengthUpButton(bool fast);
void sampleLengthDownButton(bool fast);
void sampleRepeatUpButton(bool fast);
void sampleRepeatDownButton(bool fast);
void sampleRepeatLengthUpButton(bool fast);
void sampleRepeatLengthDownButton(bool fast);
void tempoUpButton(void);
void tempoDownButton(void);
void songLengthUpButton(void);
void songLengthDownButton(void);
void patternUpButton(void);
void patternDownButton(void);
void positionUpButton(void);
void positionDownButton(void);
void handleSamplerVolumeBox(void);

int32_t checkGUIButtons(void);
void handleTextEditing(uint8_t mouseButton);
bool handleRightMouseButton(void);
bool handleLeftMouseButton(void);
static bool handleGUIButtons(int32_t button);
static void handleRepeatedGUIButtons(void);
static void handleRepeatedSamplerFilterButtons(void);

void updateMouseScaling(void)
{
	double dScaleX, dScaleY;

	dScaleX = editor.ui.renderW / (double)SCREEN_W;
	dScaleY = editor.ui.renderH / (double)SCREEN_H;

	editor.ui.xScaleMul = (dScaleX == 0.0) ? 65536 : (uint32_t)round(65536.0 / dScaleX);
	editor.ui.yScaleMul = (dScaleY == 0.0) ? 65536 : (uint32_t)round(65536.0 / dScaleY);
}

void readMouseXY(void)
{
	int32_t mx, my;

	if (input.mouse.setPosFlag)
	{
		input.mouse.setPosFlag = false;

		if (SDL_GetWindowFlags(window) & SDL_WINDOW_SHOWN)
			SDL_WarpMouseInWindow(window, input.mouse.setPosX, input.mouse.setPosY);

		return;
	}

	SDL_PumpEvents(); // gathers all pending input from devices into the event queue (less mouse lag)
	SDL_GetMouseState(&mx, &my);

	/* in centered fullscreen mode, trap the mouse inside the framed image
	** and subtract the coords to match the OS mouse position (fixes touch from touchscreens) */
	if (editor.fullscreen && !ptConfig.fullScreenStretch)
	{
		if (mx < editor.ui.renderX)
		{
			mx = editor.ui.renderX;
			SDL_WarpMouseInWindow(window, mx, my);
		}
		else if (mx >= editor.ui.renderX+editor.ui.renderW)
		{
			mx = (editor.ui.renderX + editor.ui.renderW) - 1;
			SDL_WarpMouseInWindow(window, mx, my);
		}

		if (my < editor.ui.renderY)
		{
			my = editor.ui.renderY;
			SDL_WarpMouseInWindow(window, mx, my);
		}
		else if (my >= editor.ui.renderY+editor.ui.renderH)
		{
			my = (editor.ui.renderY + editor.ui.renderH) - 1;
			SDL_WarpMouseInWindow(window, mx, my);
		}

		mx -= editor.ui.renderX;
		my -= editor.ui.renderY;
	}

	if (mx < 0) mx = 0;
	if (my < 0) my = 0;

	// multiply coords by video scaling factors
	mx = (((uint32_t)mx * editor.ui.xScaleMul) + (1 << 15)) >> 16;
	my = (((uint32_t)my * editor.ui.yScaleMul) + (1 << 15)) >> 16;

	if (mx >= SCREEN_W) mx = SCREEN_W - 1;
	if (my >= SCREEN_H) my = SCREEN_H - 1;

	input.mouse.x = (int16_t)mx;
	input.mouse.y = (int16_t)my;

	if (ptConfig.hwMouse)
	{
		// hardware mouse (OS)
		hideSprite(SPRITE_MOUSE_POINTER);
	}
	else
	{
		// software mouse (PT mouse)
		setSpritePos(SPRITE_MOUSE_POINTER, input.mouse.x, input.mouse.y);
	}
}

void mouseButtonUpHandler(uint8_t mouseButton)
{
#ifndef __APPLE__
	if (!editor.fullscreen)
		SDL_SetWindowGrab(window, SDL_FALSE);
#endif

	input.mouse.buttonWaitCounter = 0;
	input.mouse.buttonWaiting = false;

	if (mouseButton == SDL_BUTTON_LEFT)
	{
		input.mouse.leftButtonPressed = false;
		editor.ui.forceSampleDrag = false;
		editor.ui.forceVolDrag = false;
		editor.ui.leftLoopPinMoving = false;
		editor.ui.rightLoopPinMoving = false;
		editor.ui.sampleMarkingPos = -1;

		switch (input.mouse.lastGUIButton)
		{
			case PTB_SLENGTHU:
			case PTB_SLENGTHD:
			{
				if (editor.ui.samplerScreenShown)
					redrawSample();

				recalcChordLength();
				updateSamplePos();

				editor.ui.updateSongSize = true;
			}
			break;

			case PTB_LENGTHU:
			case PTB_LENGTHD:
			case PTB_PATTERNU:
			case PTB_PATTERND:
			{
				editor.ui.updateSongSize = true;

				if (editor.ui.posEdScreenShown)
					editor.ui.updatePosEd = true;
			}
			break;

			default:
				break;
		}

		input.mouse.lastGUIButton = -1;
		input.mouse.lastSmpFilterButton = -1;
	}

	if (mouseButton == SDL_BUTTON_RIGHT)
	{
		input.mouse.rightButtonPressed = false;
		editor.ui.forceSampleEdit = false;
	}
}

void mouseButtonDownHandler(uint8_t mouseButton)
{
#ifndef __APPLE__
	if (!editor.fullscreen)
		SDL_SetWindowGrab(window, SDL_TRUE);
#endif

	if (mouseButton == SDL_BUTTON_LEFT)
	{
		input.mouse.leftButtonPressed = true;
		input.mouse.buttonWaiting = true;
	}

	if (mouseButton == SDL_BUTTON_RIGHT)
		input.mouse.rightButtonPressed = true;

	// when red mouse pointer (error), block further input for a while
	if (editor.errorMsgActive && editor.errorMsgBlock)
		return;

	if (handleRightMouseButton() || handleLeftMouseButton())
		return;

	handleTextEditing(mouseButton);
}

void handleGUIButtonRepeat(void)
{
	if (!input.mouse.leftButtonPressed)
	{
		// left mouse button released, stop repeating buttons
		input.mouse.repeatCounter = 0;
		return;
	}

	if (editor.ui.samplerFiltersBoxShown)
	{
		handleRepeatedSamplerFilterButtons();
		return;
	}

	if (input.mouse.lastGUIButton != checkGUIButtons()) // XXX: This can potentially do a ton of iterations, bad design!
	{
		// only repeat the button that was first clicked (e.g. if you hold and move mouse to another button)
		input.mouse.repeatCounter = 0;
		return;
	}

	handleRepeatedGUIButtons();
	input.mouse.repeatCounter++;
}

void edNote1UpButton(void)
{
	if (input.mouse.rightButtonPressed)
		editor.note1 += 12;
	else
		editor.note1++;

	if (editor.note1 > 36)
		editor.note1 = 36;

	editor.ui.updateNote1Text = true;
	recalcChordLength();
}

void edNote1DownButton(void)
{
	if (input.mouse.rightButtonPressed)
		editor.note1 -= 12;
	else
		editor.note1--;

	if (editor.note1 < 0)
		editor.note1 = 0;

	editor.ui.updateNote1Text = true;
	recalcChordLength();
}

void edNote2UpButton(void)
{
	if (input.mouse.rightButtonPressed)
		editor.note2 += 12;
	else
		editor.note2++;

	if (editor.note2 > 36)
		editor.note2 = 36;

	editor.ui.updateNote2Text = true;
	recalcChordLength();
}

void edNote2DownButton(void)
{
	if (input.mouse.rightButtonPressed)
		editor.note2 -= 12;
	else
		editor.note2--;

	if (editor.note2 < 0)
		editor.note2 = 0;

	editor.ui.updateNote2Text = true;
	recalcChordLength();
}

void edNote3UpButton(void)
{
	if (input.mouse.rightButtonPressed)
		editor.note3 += 12;
	else
		editor.note3++;

	if (editor.note3 > 36)
		editor.note3 = 36;

	editor.ui.updateNote3Text = true;
	recalcChordLength();
}

void edNote3DownButton(void)
{
	if (input.mouse.rightButtonPressed)
		editor.note3 -= 12;
	else
		editor.note3--;

	if (editor.note3 < 0)
		editor.note3 = 0;

	editor.ui.updateNote3Text = true;
	recalcChordLength();
}

void edNote4UpButton(void)
{
	if (input.mouse.rightButtonPressed)
		editor.note4 += 12;
	else
		editor.note4++;

	if (editor.note4 > 36)
		editor.note4 = 36;

	editor.ui.updateNote4Text = true;
	recalcChordLength();
}

void edNote4DownButton(void)
{
	if (input.mouse.rightButtonPressed)
		editor.note4 -= 12;
	else
		editor.note4--;

	if (editor.note4 < 0)
		editor.note4 = 0;

	editor.ui.updateNote4Text = true;
	recalcChordLength();
}

void edPosUpButton(bool fast)
{
	if (input.mouse.rightButtonPressed)
	{
		if (fast)
		{
			if (editor.samplePos <= 0xFFFF-544)
				editor.samplePos += 544; // 50Hz/60Hz scaled value
			else
				editor.samplePos = 0xFFFF;
		}
		else
		{
			if (editor.samplePos <= 0xFFFF-16)
				editor.samplePos += 16;
			else
				editor.samplePos = 0xFFFF;
		}
	}
	else
	{
		if (fast)
		{
			if (editor.samplePos <= 0xFFFF-37)
				editor.samplePos += 37; // 50Hz/60Hz scaled value
			else
				editor.samplePos = 0xFFFF;
		}
		else
		{
			if (editor.samplePos < 0xFFFF)
				editor.samplePos++;
			else
				editor.samplePos = 0xFFFF;
		}
	}

	if (editor.samplePos > modEntry->samples[editor.currSample].length)
		editor.samplePos = modEntry->samples[editor.currSample].length;

	editor.ui.updatePosText = true;
}

void edPosDownButton(bool fast)
{
	if (input.mouse.rightButtonPressed)
	{
		if (fast)
		{
			if (editor.samplePos > 544)
				editor.samplePos -= 544; // 50Hz/60Hz scaled value
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
			if (editor.samplePos > 37)
				editor.samplePos -= 37; // 50Hz/60Hz scaled value
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

	editor.ui.updatePosText = true;
}

void edModUpButton(void)
{
	if (input.mouse.rightButtonPressed)
		editor.modulateSpeed += 10;
	else
		editor.modulateSpeed++;

	if (editor.modulateSpeed > 127)
		editor.modulateSpeed = 127;

	editor.ui.updateModText = true;
}

void edModDownButton(void)
{
	if (input.mouse.rightButtonPressed)
	{
		editor.modulateSpeed -= 10;
	}
	else
	{
		editor.modulateSpeed--;
	}

	if (editor.modulateSpeed < -128)
		editor.modulateSpeed = -128;

	editor.ui.updateModText = true;
}

void edVolUpButton(void)
{
	if (input.mouse.rightButtonPressed)
	{
		if (editor.sampleVol <= 999-10)
			editor.sampleVol += 10;
	}
	else
	{
		if (editor.sampleVol < 999)
			editor.sampleVol++;
	}

	editor.ui.updateVolText = true;
}

void edVolDownButton(void)
{
	if (input.mouse.rightButtonPressed)
	{
		if (editor.sampleVol >= 10)
			editor.sampleVol -= 10;
	}
	else
	{
		if (editor.sampleVol > 0)
			editor.sampleVol--;
	}

	editor.ui.updateVolText = true;
}

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

void sampleFineTuneUpButton(void)
{
	int8_t finetune = modEntry->samples[editor.currSample].fineTune & 0x0F;
	if (finetune != 7)
		modEntry->samples[editor.currSample].fineTune = (finetune + 1) & 0x0F;

	if (input.mouse.rightButtonPressed)
		modEntry->samples[editor.currSample].fineTune = 0;

	recalcChordLength();
	editor.ui.updateCurrSampleFineTune = true;
}

void sampleFineTuneDownButton(void)
{
	int8_t finetune = modEntry->samples[editor.currSample].fineTune & 0x0F;
	if (finetune != 8)
		modEntry->samples[editor.currSample].fineTune = (finetune - 1) & 0x0F;

	if (input.mouse.rightButtonPressed)
		modEntry->samples[editor.currSample].fineTune = 0;

	recalcChordLength();
	editor.ui.updateCurrSampleFineTune = true;
}

void sampleVolumeUpButton(void)
{
	int8_t val = modEntry->samples[editor.currSample].volume;

	if (input.mouse.rightButtonPressed)
		val += 16;
	else
		val++;

	if (val > 64)
		val = 64;

	modEntry->samples[editor.currSample].volume = (uint8_t)val;
	editor.ui.updateCurrSampleVolume = true;
}

void sampleVolumeDownButton(void)
{
	int8_t val = modEntry->samples[editor.currSample].volume;

	if (input.mouse.rightButtonPressed)
		val -= 16;
	else
		val--;

	if (val < 0)
		val = 0;

	modEntry->samples[editor.currSample].volume = (uint8_t)val;
	editor.ui.updateCurrSampleVolume = true;
}

void sampleLengthUpButton(bool fast)
{
	int32_t val;

	if (modEntry->samples[editor.currSample].length == MAX_SAMPLE_LEN)
		return;

	turnOffVoices();

	val = modEntry->samples[editor.currSample].length;
	if (input.mouse.rightButtonPressed)
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

	if (val > MAX_SAMPLE_LEN)
		val = MAX_SAMPLE_LEN;

	modEntry->samples[editor.currSample].length = val;
	editor.ui.updateCurrSampleLength = true;
}

void sampleLengthDownButton(bool fast)
{
	int32_t val;
	moduleSample_t *s;

	s = &modEntry->samples[editor.currSample];
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

	turnOffVoices();

	val = modEntry->samples[editor.currSample].length;
	if (input.mouse.rightButtonPressed)
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

	s->length = val;
	if (s->loopStart+s->loopLength > 2)
	{
		if (s->length < s->loopStart+s->loopLength)
			s->length = s->loopStart+s->loopLength;
	}

	editor.ui.updateCurrSampleLength = true;
}

void sampleRepeatUpButton(bool fast)
{
	int32_t val, loopLen, len;

	val = modEntry->samples[editor.currSample].loopStart;
	loopLen = modEntry->samples[editor.currSample].loopLength;
	len = modEntry->samples[editor.currSample].length;

	if (len == 0)
	{
		modEntry->samples[editor.currSample].loopStart = 0;
		return;
	}

	if (input.mouse.rightButtonPressed)
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

	modEntry->samples[editor.currSample].loopStart = val;
	editor.ui.updateCurrSampleRepeat = true;

	mixerUpdateLoops();

	if (editor.ui.samplerScreenShown)
		setLoopSprites();

	if (editor.ui.editOpScreenShown && editor.ui.editOpScreen == 3) // sample chord editor
		editor.ui.updateLengthText = true;
}

void sampleRepeatDownButton(bool fast)
{
	int32_t val, len;

	val = modEntry->samples[editor.currSample].loopStart;
	len = modEntry->samples[editor.currSample].length;

	if (len == 0)
	{
		modEntry->samples[editor.currSample].loopStart = 0;
		return;
	}

	if (input.mouse.rightButtonPressed)
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

	modEntry->samples[editor.currSample].loopStart = val;
	editor.ui.updateCurrSampleRepeat = true;

	mixerUpdateLoops();

	if (editor.ui.samplerScreenShown)
		setLoopSprites();

	if (editor.ui.editOpScreenShown && editor.ui.editOpScreen == 3) // sample chord editor
		editor.ui.updateLengthText = true;
}

void sampleRepeatLengthUpButton(bool fast)
{
	int32_t val, loopStart, len;

	val = modEntry->samples[editor.currSample].loopLength;
	loopStart = modEntry->samples[editor.currSample].loopStart;
	len = modEntry->samples[editor.currSample].length;

	if (len == 0)
	{
		modEntry->samples[editor.currSample].loopLength = 2;
		return;
	}

	if (input.mouse.rightButtonPressed)
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

	modEntry->samples[editor.currSample].loopLength = val;
	editor.ui.updateCurrSampleReplen = true;

	mixerUpdateLoops();

	if (editor.ui.samplerScreenShown)
		setLoopSprites();

	if (editor.ui.editOpScreenShown && editor.ui.editOpScreen == 3) // sample chord editor
		editor.ui.updateLengthText = true;
}

void sampleRepeatLengthDownButton(bool fast)
{
	int32_t val, len;

	val = modEntry->samples[editor.currSample].loopLength;
	len = modEntry->samples[editor.currSample].length;

	if (len == 0)
	{
		modEntry->samples[editor.currSample].loopLength = 2;
		return;
	}

	if (input.mouse.rightButtonPressed)
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

	modEntry->samples[editor.currSample].loopLength = val;
	editor.ui.updateCurrSampleReplen = true;

	mixerUpdateLoops();

	if (editor.ui.samplerScreenShown)
		setLoopSprites();

	if (editor.ui.editOpScreenShown && editor.ui.editOpScreen == 3) // sample chord editor
		editor.ui.updateLengthText = true;
}

void tempoUpButton(void)
{
	int16_t val;

	if (editor.timingMode == TEMPO_MODE_VBLANK)
		return;

	val = modEntry->currBPM;
	if (input.mouse.rightButtonPressed)
		val += 10;
	else
		val++;

	if (val > 255)
		val = 255;

	modEntry->currBPM = val;
	modSetTempo(modEntry->currBPM);
	editor.ui.updateSongBPM = true;
}

void tempoDownButton(void)
{
	int16_t val;

	if (editor.timingMode == TEMPO_MODE_VBLANK)
		return;

	val = modEntry->currBPM;
	if (input.mouse.rightButtonPressed)
		val -= 10;
	else
		val--;

	if (val < 32)
		val = 32;

	modEntry->currBPM = val;
	modSetTempo(modEntry->currBPM);
	editor.ui.updateSongBPM = true;
}

void songLengthUpButton(void)
{
	int16_t val;

	val = modEntry->head.orderCount;
	if (input.mouse.rightButtonPressed)
		val += 10;
	else
		val++;

	if (val > 128)
		val = 128;

	modEntry->head.orderCount = (uint8_t)val;

	val = modEntry->currOrder;
	if (val > modEntry->head.orderCount-1)
		val = modEntry->head.orderCount-1;

	editor.currPosEdPattDisp = &modEntry->head.order[val];
	editor.ui.updateSongLength = true;
}

void songLengthDownButton(void)
{
	int16_t val = modEntry->head.orderCount;

	if (input.mouse.rightButtonPressed)
		val -= 10;
	else
		val--;

	if (val < 1)
		val = 1;

	modEntry->head.orderCount = (uint8_t)val;

	val = modEntry->currOrder;
	if (val > modEntry->head.orderCount-1)
		val = modEntry->head.orderCount-1;

	editor.currPosEdPattDisp = &modEntry->head.order[val];
	editor.ui.updateSongLength = true;
}

void patternUpButton(void)
{
	int16_t val = modEntry->head.order[modEntry->currOrder];

	if (input.mouse.rightButtonPressed)
		val += 10;
	else
		val++;

	if (val > MAX_PATTERNS-1)
		val = MAX_PATTERNS-1;

	modEntry->head.order[modEntry->currOrder] = (uint8_t)val;

	if (editor.ui.posEdScreenShown)
		editor.ui.updatePosEd = true;

	editor.ui.updateSongPattern = true;
}

void patternDownButton(void)
{
	int16_t val = modEntry->head.order[modEntry->currOrder];

	if (input.mouse.rightButtonPressed)
		val -= 10;
	else
		val--;

	if (val < 0)
		val = 0;

	modEntry->head.order[modEntry->currOrder] = (uint8_t)val;

	if (editor.ui.posEdScreenShown)
		editor.ui.updatePosEd = true;

	editor.ui.updateSongPattern = true;
}

void positionUpButton(void)
{
	int16_t val = modEntry->currOrder;

	if (input.mouse.rightButtonPressed)
		val += 10;
	else
		val++;

	if (val > 127)
		val = 127;

	modSetPos(val, DONT_SET_ROW);
}

void positionDownButton(void)
{
	int16_t val = modEntry->currOrder;

	if (input.mouse.rightButtonPressed)
		val -= 10;
	else
		val--;

	if (val < 0)
		val = 0;

	modSetPos(val, DONT_SET_ROW);
}

void handleSamplerVolumeBox(void)
{
	int8_t *sampleData;
	uint8_t i;
	int16_t sample, sampleVol;
	int32_t smp32, sampleIndex, sampleLength;
	double dSmp;
	moduleSample_t *s;

	if (input.mouse.rightButtonPressed)
	{
		if (editor.ui.editTextFlag)
		{
			exitGetTextLine(EDIT_TEXT_NO_UPDATE);
		}
		else
		{
			editor.ui.samplerVolBoxShown = false;
			removeSamplerVolBox();
		}

		return;
	}

	if (editor.ui.editTextFlag)
		return;

	// check buttons
	if (input.mouse.leftButtonPressed)
	{
		// restore sample ask dialog
		if (editor.ui.askScreenShown && editor.ui.askScreenType == ASK_RESTORE_SAMPLE)
		{
			if (input.mouse.y >= 71 && input.mouse.y <= 81)
			{
				if (input.mouse.x >= 171 && input.mouse.x <= 196)
				{
					// YES button
					editor.ui.askScreenShown = false;
					editor.ui.answerNo = false;
					editor.ui.answerYes = true;
					handleAskYes();
				}
				else if (input.mouse.x >= 234 && input.mouse.x <= 252)
				{
					// NO button
					editor.ui.askScreenShown = false;
					editor.ui.answerNo = true;
					editor.ui.answerYes = false;
					handleAskNo();
				}
			}

			return;
		}

		// MAIN SCREEN STOP
		if (!editor.ui.diskOpScreenShown && !editor.ui.posEdScreenShown)
		{
			if (input.mouse.x >= 182 && input.mouse.x <= 243 && input.mouse.y >= 0 && input.mouse.y <= 10)
			{
				modStop();
				return;
			}
		}

		if (input.mouse.x >= 32 && input.mouse.x <= 95)
		{
			// SAMPLER SCREEN PLAY WAVEFORM
			if (input.mouse.y >= 211 && input.mouse.y <= 221)
			{
				samplerPlayWaveform();
				return;
			}

			// SAMPLER SCREEN PLAY DISPLAY
			else if (input.mouse.y >= 222 && input.mouse.y <= 232)
			{
				samplerPlayDisplay();
				return;
			}

			// SAMPLER SCREEN PLAY RANGE
			else if (input.mouse.y >= 233 && input.mouse.y <= 243)
			{
				samplerPlayRange();
				return;
			}
		}

		// SAMPLER SCREEN STOP
		if (input.mouse.x >= 0 && input.mouse.x <= 31 && input.mouse.y >= 222 && input.mouse.y <= 243)
		{
			for (i = 0; i < AMIGA_VOICES; i++)
				mixerKillVoice(i);
			return;
		}

		// VOLUME button (toggle)
		if (input.mouse.x >= 96 && input.mouse.x <= 135 && input.mouse.y >= 244 && input.mouse.y <= 254)
		{
			editor.ui.samplerVolBoxShown = false;
			removeSamplerVolBox();
			return;
		}

		// DRAG BOXES
		if (input.mouse.x >= 72 && input.mouse.x <= 173 && input.mouse.y >= 154 && input.mouse.y <= 175)
		{
			volBoxBarPressed(MOUSE_BUTTON_NOT_HELD);
			return;
		}


		if (input.mouse.x >= 174 && input.mouse.x <= 207)
		{
			// FROM NUM
			if (input.mouse.y >= 154 && input.mouse.y <= 164)
			{
				editor.ui.tmpDisp16 = editor.vol1;
				editor.vol1Disp = &editor.ui.tmpDisp16;
				editor.ui.numPtr16 = &editor.ui.tmpDisp16;
				editor.ui.numLen = 3;
				editor.ui.editTextPos = 6342; // (y * 40) + x
				getNumLine(TEXT_EDIT_DECIMAL, PTB_SA_VOL_FROM_NUM);
				return;
			}

			// TO NUM
			else if (input.mouse.y >= 165 && input.mouse.y <= 175)
			{
				editor.ui.tmpDisp16 = editor.vol2;
				editor.vol2Disp = &editor.ui.tmpDisp16;
				editor.ui.numPtr16 = &editor.ui.tmpDisp16;
				editor.ui.numLen = 3;
				editor.ui.editTextPos = 6782; // (y * 40) + x
				getNumLine(TEXT_EDIT_DECIMAL, PTB_SA_VOL_TO_NUM);
				return;
			}
		}

		if (input.mouse.y >= 176 && input.mouse.y <= 186)
		{
			// NORMALIZE
			if (input.mouse.x >= 101 && input.mouse.x <= 143)
			{
				s = &modEntry->samples[editor.currSample];
				if (s->length == 0)
				{
					displayErrorMsg("SAMPLE IS EMPTY");
					return;
				}

				sampleData = &modEntry->sampleData[s->offset];
				if (editor.markStartOfs != -1)
				{
					sampleData += editor.markStartOfs;
					sampleLength = editor.markEndOfs - editor.markStartOfs;
				}
				else
				{
					sampleLength = s->length;
				}

				sampleVol = 0;
				sampleIndex = 0;

				while (sampleIndex < sampleLength)
				{
					sample = *sampleData++;
					sample = ABS(sample);

					if (sampleVol < sample)
						sampleVol = sample;

					sampleIndex++;
				}

				if (sampleVol <= 0 || sampleVol > 127)
				{
					editor.vol1 = 100;
					editor.vol2 = 100;
				}
				else if (sampleVol < 64)
				{
					editor.vol1 = 200;
					editor.vol2 = 200;
				}
				else
				{
					editor.vol1 = (uint16_t)((100 * 127) / sampleVol);
					editor.vol2 = (uint16_t)((100 * 127) / sampleVol);
				}

				editor.ui.updateVolFromText = true;
				editor.ui.updateVolToText = true;

				showVolFromSlider();
				showVolToSlider();
				return;
			}

			// RAMP DOWN
			else if (input.mouse.x >= 144 && input.mouse.x <= 153)
			{
				editor.vol1 = 100;
				editor.vol2 = 0;
				editor.ui.updateVolFromText = true;
				editor.ui.updateVolToText = true;
				showVolFromSlider();
				showVolToSlider();
				return;
			}

			// RAMP UP
			else if (input.mouse.x >= 154 && input.mouse.x <= 163)
			{
				editor.vol1 = 0;
				editor.vol2 = 100;
				editor.ui.updateVolFromText = true;
				editor.ui.updateVolToText = true;
				showVolFromSlider();
				showVolToSlider();
				return;
			}

			// RAMP UNITY
			else if (input.mouse.x >= 164 && input.mouse.x <= 173)
			{
				editor.vol1 = 100;
				editor.vol2 = 100;
				editor.ui.updateVolFromText = true;
				editor.ui.updateVolToText = true;
				showVolFromSlider();
				showVolToSlider();
				return;
			}

			// CANCEL
			else if (input.mouse.x >= 174 && input.mouse.x <= 207)
			{
				editor.ui.samplerVolBoxShown = false;
				removeSamplerVolBox();
				return;
			}

			// RAMP
			else if (input.mouse.x >= 72 && input.mouse.x <= 100)
			{
				s = &modEntry->samples[editor.currSample];
				if (s->length == 0)
				{
					displayErrorMsg("SAMPLE IS EMPTY");
					return;
				}

				if (editor.vol1 == 100 && editor.vol2 == 100)
				{
					editor.ui.samplerVolBoxShown = false;
					removeSamplerVolBox();
					return;
				}

				sampleData = &modEntry->sampleData[s->offset];
				if (editor.markStartOfs != -1)
				{
					sampleData  += editor.markStartOfs;
					sampleLength = editor.markEndOfs - editor.markStartOfs;
				}
				else
				{
					sampleLength = s->length;
				}

				sampleIndex = 0;
				while (sampleIndex < sampleLength)
				{
					dSmp = (sampleIndex * editor.vol2) / (double)sampleLength;
					dSmp += ((sampleLength - sampleIndex) * editor.vol1) / (double)sampleLength;
					dSmp *= (double)(*sampleData);
					dSmp /= 100.0;

					smp32 = (int32_t)dSmp;
					CLAMP8(smp32);

					*sampleData++ = (int8_t)smp32;
					sampleIndex++;
				}

				fixSampleBeep(s);

				editor.ui.samplerVolBoxShown = false;
				removeSamplerVolBox();

				updateWindowTitle(MOD_IS_MODIFIED);
			}
		}
	}
}

static void handleRepeatedSamplerFilterButtons(void)
{
	if (!input.mouse.leftButtonPressed || input.mouse.lastSmpFilterButton == -1)
		return;

	switch (input.mouse.lastSmpFilterButton)
	{
		case 0: // low-pass cutoff up
		{
			if (input.mouse.rightButtonPressed)
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

			editor.ui.updateLPText = true;
		}
		break;

		case 1: // low-pass cutoff down
		{
			if (input.mouse.rightButtonPressed)
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

			editor.ui.updateLPText = true;
		}
		break;

		case 2: // high-pass cutoff up
		{
			if (input.mouse.rightButtonPressed)
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

			editor.ui.updateHPText = true;
		}
		break;

		case 3: // high-pass cutoff down
		{
			if (input.mouse.rightButtonPressed)
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

			editor.ui.updateHPText = true;
		}
		break;

		default: break;
	}
}

void handleSamplerFiltersBox(void)
{
	uint8_t i;
	moduleSample_t *s;

	if (input.mouse.rightButtonPressed && editor.ui.editTextFlag)
	{
		exitGetTextLine(EDIT_TEXT_NO_UPDATE);
		return;
	}

	if (editor.ui.editTextFlag || input.mouse.lastSmpFilterButton > -1 || !input.mouse.leftButtonPressed)
		return;

	// restore sample ask dialog
	if (editor.ui.askScreenShown && editor.ui.askScreenType == ASK_RESTORE_SAMPLE)
	{
		if (input.mouse.y >= 71 && input.mouse.y <= 81)
		{
			if (input.mouse.x >= 171 && input.mouse.x <= 196)
			{
				// YES button
				editor.ui.askScreenShown = false;
				editor.ui.answerNo = false;
				editor.ui.answerYes = true;
				handleAskYes();
			}
			else if ((input.mouse.x >= 234) && (input.mouse.x <= 252))
			{
				// NO button
				editor.ui.askScreenShown = false;
				editor.ui.answerNo = true;
				editor.ui.answerYes = false;
				handleAskNo();
			}
		}

		return;
	}

	// FILTERS button (toggle)
	if (input.mouse.x >= 211 && input.mouse.x <= 245 && input.mouse.y >= 244 && input.mouse.y <= 254)
	{
		editor.ui.samplerFiltersBoxShown = false;
		removeSamplerFiltersBox();
		return;
	}

	// MAIN SCREEN STOP
	if (!editor.ui.diskOpScreenShown && !editor.ui.posEdScreenShown)
	{
		if (input.mouse.x >= 182 && input.mouse.x <= 243 && input.mouse.y >= 0 && input.mouse.y <= 10)
		{
			modStop();
			return;
		}
	}

	if (input.mouse.x >= 32 && input.mouse.x <= 95)
	{
		// SAMPLER SCREEN PLAY WAVEFORM
		if (input.mouse.y >= 211 && input.mouse.y <= 221)
		{
			samplerPlayWaveform();
			return;
		}

		// SAMPLER SCREEN PLAY DISPLAY
		else if (input.mouse.y >= 222 && input.mouse.y <= 232)
		{
			samplerPlayDisplay();
			return;
		}

		// SAMPLER SCREEN PLAY RANGE
		else if (input.mouse.y >= 233 && input.mouse.y <= 243)
		{
			samplerPlayRange();
			return;
		}
	}

	// SAMPLER SCREEN STOP
	if (input.mouse.x >= 0 && input.mouse.x <= 31 && input.mouse.y >= 222 && input.mouse.y <= 243)
	{
		for (i = 0; i < AMIGA_VOICES; i++)
			mixerKillVoice(i);
		return;
	}

	// UNDO
	if (input.mouse.x >= 65 && input.mouse.x <= 75 && input.mouse.y >= 154 && input.mouse.y <= 184)
	{
		s = &modEntry->samples[editor.currSample];
		if (s->length == 0)
		{
			displayErrorMsg("SAMPLE IS EMPTY");
		}
		else
		{
			memcpy(&modEntry->sampleData[s->offset], editor.tempSample, MAX_SAMPLE_LEN);
			redrawSample();
			updateWindowTitle(MOD_IS_MODIFIED);
			renderSamplerFiltersBox();
		}

		return;
	}

	if (input.mouse.y >= 154 && input.mouse.y <= 164)
	{
		// DO LOW-PASS FILTER
		if (input.mouse.x >= 76 && input.mouse.x <= 165)
		{
			lowPassSample(editor.lpCutOff);
			renderSamplerFiltersBox();
			return;
		}

		// LOW-PASS CUTOFF
		else if (input.mouse.x >= 166 && input.mouse.x <= 217)
		{
			if (input.mouse.rightButtonPressed)
			{
				editor.lpCutOff = 0;
				editor.ui.updateLPText = true;
			}
			else
			{
				editor.ui.tmpDisp16 = editor.lpCutOff;
				editor.lpCutOffDisp = &editor.ui.tmpDisp16;
				editor.ui.numPtr16 = &editor.ui.tmpDisp16;
				editor.ui.numLen = 4;
				editor.ui.editTextPos = 6341; // (y * 40) + x
				getNumLine(TEXT_EDIT_DECIMAL, PTB_SA_FIL_LP_CUTOFF);
			}

			return;
		}

		// LOW-PASS CUTOFF UP
		else if (input.mouse.x >= 218 && input.mouse.x <= 228)
		{
			input.mouse.lastSmpFilterButton = 0;
			if (input.mouse.rightButtonPressed)
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

			editor.ui.updateLPText = true;
			return;
		}

		// LOW-PASS CUTOFF DOWN
		else if (input.mouse.x >= 229 && input.mouse.x <= 239)
		{
			input.mouse.lastSmpFilterButton = 1;
			if (input.mouse.rightButtonPressed)
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

			editor.ui.updateLPText = true;
			return;
		}
	}

	if (input.mouse.y >= 164 && input.mouse.y <= 174)
	{
		// DO HIGH-PASS FILTER
		if (input.mouse.x >= 76 && input.mouse.x <= 165)
		{
			highPassSample(editor.hpCutOff);
			renderSamplerFiltersBox();
			return;
		}

		// HIGH-PASS CUTOFF
		else if (input.mouse.x >= 166 && input.mouse.x <= 217)
		{
			if (input.mouse.rightButtonPressed)
			{
				editor.hpCutOff = 0;
				editor.ui.updateHPText = true;
			}
			else
			{
				editor.ui.tmpDisp16 = editor.hpCutOff;
				editor.hpCutOffDisp = &editor.ui.tmpDisp16;
				editor.ui.numPtr16 = &editor.ui.tmpDisp16;
				editor.ui.numLen = 4;
				editor.ui.editTextPos = 6781; // (y * 40) + x
				getNumLine(TEXT_EDIT_DECIMAL, PTB_SA_FIL_HP_CUTOFF);
			}

			return;
		}

		// HIGH-PASS CUTOFF UP
		else if (input.mouse.x >= 218 && input.mouse.x <= 228)
		{
			input.mouse.lastSmpFilterButton = 2;
			if (input.mouse.rightButtonPressed)
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

			editor.ui.updateHPText = true;
			return;
		}

		// HIGH-PASS CUTOFF DOWN
		else if (input.mouse.x >= 229 && input.mouse.x <= 239)
		{
			input.mouse.lastSmpFilterButton = 3;
			if (input.mouse.rightButtonPressed)
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

			editor.ui.updateHPText = true;
			return;
		}
	}

	// NORMALIZE SAMPLE FLAG
	if (input.mouse.x >= 76 && input.mouse.x <= 239 && input.mouse.y >= 174 && input.mouse.y <= 186)
	{
		editor.normalizeFiltersFlag ^= 1;
		editor.ui.updateNormFlag = true;
		return;
	}

	// EXIT
	if (input.mouse.x >= 240 && input.mouse.x <= 250 && input.mouse.y >= 154 && input.mouse.y <= 186)
	{
		editor.ui.samplerFiltersBoxShown = false;
		removeSamplerFiltersBox();
	}
}

static bool withinButtonRect(const guiButton_t *b)
{
	if (input.mouse.x >= b->x1 && input.mouse.x <= b->x2 &&
	    input.mouse.y >= b->y1 && input.mouse.y <= b->y2)
	{
		return true;
	}

	return false;
}

#define TEST_BUTTONS(bStruct, bNum) \
for (uint32_t i = 0; i < bNum; i++) \
	if (withinButtonRect(&bStruct[i])) \
		return bStruct[i].b; \

int32_t checkGUIButtons(void)
{
	// these two makes *no other* buttons clickable
	if (editor.ui.askScreenShown)
	{
		if (editor.ui.pat2SmpDialogShown)
		{
			TEST_BUTTONS(bPat2SmpAsk, PAT2SMP_ASK_BUTTONS);
		}
		else
		{
			TEST_BUTTONS(bAsk, ASK_BUTTONS);
		}

		return -1;
	}
	else if (editor.ui.clearScreenShown)
	{
		TEST_BUTTONS(bClear, CLEAR_BUTTONS);
		return -1;
	}

	// QUIT (xy 0,0) works on all screens except for ask/clear screen
	if (input.mouse.x == 0 && input.mouse.y == 0)
		return PTB_QUIT;

	// top screen buttons
	if (editor.ui.diskOpScreenShown)
	{
		TEST_BUTTONS(bDiskOp, DISKOP_BUTTONS);
	}
	else
	{
		if (editor.ui.posEdScreenShown)
		{
			TEST_BUTTONS(bPosEd, POSED_BUTTONS);
		}
		else if (editor.ui.editOpScreenShown)
		{
			switch (editor.ui.editOpScreen)
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
	if (editor.ui.samplerScreenShown)
	{
		TEST_BUTTONS(bSampler, SAMPLER_BUTTONS);
	}
	else
	{
		TEST_BUTTONS(bBotScreen, BOTSCREEN_BUTTONS);
	}

	return -1;
}

void handleTextEditing(uint8_t mouseButton)
{
	char *tmpRead;
	int16_t tmp16;

	// handle mouse while editing text/numbers
	if (editor.ui.editTextFlag)
	{
		if (editor.ui.editTextType != TEXT_EDIT_STRING)
		{
			if (mouseButton == SDL_BUTTON_RIGHT)
				exitGetTextLine(EDIT_TEXT_NO_UPDATE);
		}
		else if (mouseButton == SDL_BUTTON_LEFT && !editor.mixFlag)
		{
			tmp16 = input.mouse.y - editor.ui.lineCurY;
			if (tmp16 <= 2 && tmp16 >= -9)
			{
				tmp16 = ((input.mouse.x - editor.ui.lineCurX) + 4) >> 3;
				while (tmp16 != 0) // 0 = pos we want
				{
					if (tmp16 > 0)
					{
						if (editor.ui.editPos < editor.ui.textEndPtr && *editor.ui.editPos != '\0')
						{
							editor.ui.editPos++;
							textMarkerMoveRight();
						}
						tmp16--;
					}
					else if (tmp16 < 0)
					{
						if (editor.ui.editPos > editor.ui.dstPtr)
						{
							editor.ui.editPos--;
							textMarkerMoveLeft();
						}
						tmp16++;
					}
				}
			}
			else
			{
				exitGetTextLine(EDIT_TEXT_UPDATE);
			}
		}
		else if (mouseButton == SDL_BUTTON_RIGHT)
		{
			if (editor.mixFlag)
			{
				exitGetTextLine(EDIT_TEXT_UPDATE);
				editor.mixFlag = false;
				editor.ui.updateMixText = true;
			}
			else
			{
				tmpRead = editor.ui.dstPtr;
				while (tmpRead < editor.ui.textEndPtr)
					*tmpRead++ = '\0';

				*editor.ui.textEndPtr = '\0';

				// don't exit text edit mode if the disk op. path was about to be deleted
				if (editor.ui.editObject == PTB_DO_DATAPATH)
				{
					// move text cursor to pos 0
					while (editor.ui.editPos > editor.ui.dstPtr)
					{
						editor.ui.editPos--;
						textMarkerMoveLeft();
					}

					editor.ui.updateDiskOpPathText = true;
				}
				else
				{
					     if (editor.ui.editObject == PTB_SONGNAME) editor.ui.updateSongName = true;
					else if (editor.ui.editObject == PTB_SAMPLENAME) editor.ui.updateCurrSampleName = true;

					exitGetTextLine(EDIT_TEXT_UPDATE);
				}
			}
		}
	}
}

void mouseWheelUpHandler(void)
{
	if (editor.ui.editTextFlag || editor.ui.askScreenShown || editor.ui.clearScreenShown || editor.swapChannelFlag)
		return;

	if (input.mouse.y < 121)
	{
		// upper part of screen
		if (editor.ui.diskOpScreenShown)
		{
			if (editor.diskop.scrollOffset > 0)
			{
				editor.diskop.scrollOffset--;
				editor.ui.updateDiskOpFileList = true;
			}
		}
		else if (editor.ui.posEdScreenShown)
		{
			if (modEntry->currOrder > 0)
				modSetPos(modEntry->currOrder - 1, DONT_SET_ROW);
		}
	}
	else
	{
		// lower part of screen
		if (editor.ui.samplerScreenShown)
		{
			samplerZoomInMouseWheel();
		}
		else
		{
			// pattern data
			if (!editor.songPlaying && modEntry->currRow > 0)
				modSetPos(DONT_SET_ORDER, modEntry->currRow - 1);
		}
	}
}

void mouseWheelDownHandler(void)
{
	if (editor.ui.editTextFlag || editor.ui.askScreenShown || editor.ui.clearScreenShown || editor.swapChannelFlag)
		return;

	if (input.mouse.y < 121)
	{
		// upper part of screen

		if (editor.ui.diskOpScreenShown)
		{
			if (editor.diskop.numEntries > DISKOP_LINES && editor.diskop.scrollOffset < editor.diskop.numEntries-DISKOP_LINES)
			{
				editor.diskop.scrollOffset++;
				editor.ui.updateDiskOpFileList = true;
			}
		}
		else if (editor.ui.posEdScreenShown)
		{
			if (modEntry->currOrder < (modEntry->head.orderCount - 1))
				modSetPos(modEntry->currOrder + 1, DONT_SET_ROW);
		}
	}
	else
	{
		// lower part of screen
		if (editor.ui.samplerScreenShown)
		{
			samplerZoomOutMouseWheel();
		}
		else
		{
			// pattern data
			if (!editor.songPlaying && modEntry->currRow < MOD_ROWS)
				modSetPos(DONT_SET_ORDER, modEntry->currRow + 1);
		}
	}
}

bool handleRightMouseButton(void)
{
	if (!input.mouse.rightButtonPressed)
		return false;

	// exit sample swap mode with right mouse button (if present)
	if (editor.swapChannelFlag)
	{
		editor.swapChannelFlag = false;
		pointerSetPreviousMode();
		setPrevStatusMessage();
		return true;
	}

	// close clear dialog with right mouse button
	if (editor.ui.clearScreenShown)
	{
		editor.ui.clearScreenShown = false;
		setPrevStatusMessage();
		pointerSetPreviousMode();

		editor.errorMsgActive = true;
		editor.errorMsgBlock = true;
		editor.errorMsgCounter = 0;

		pointerErrorMode();
		removeClearScreen();
		return true;
	}

	// close ask dialogs with right mouse button
	if (editor.ui.askScreenShown)
	{
		editor.ui.askScreenShown = false;
		editor.ui.answerNo = true;
		editor.ui.answerYes = false;
		handleAskNo(); // mouse pointer is set to error (red) in here
		return true;
	}

	// toggle channel muting with right mouse button
	if (editor.ui.visualizerMode == VISUAL_QUADRASCOPE && input.mouse.y >= 55 && input.mouse.y <= 87)
	{
		if (!editor.ui.posEdScreenShown && !editor.ui.editOpScreenShown && !editor.ui.diskOpScreenShown &&
			!editor.ui.aboutScreenShown && !editor.ui.samplerVolBoxShown && !editor.ui.samplerFiltersBoxShown)
		{
			     if (input.mouse.x > 127 && input.mouse.x <= 167) editor.muted[0] ^= 1;
			else if (input.mouse.x > 175 && input.mouse.x <= 215) editor.muted[1] ^= 1;
			else if (input.mouse.x > 223 && input.mouse.x <= 263) editor.muted[2] ^= 1;
			else if (input.mouse.x > 271 && input.mouse.x <= 311) editor.muted[3] ^= 1;

			renderMuteButtons();
		}
	}

	// sample hand drawing
	if (input.mouse.y >= 138 && input.mouse.y <= 201 && editor.ui.samplerScreenShown &&
		!editor.ui.samplerVolBoxShown && !editor.ui.samplerFiltersBoxShown)
	{
		samplerEditSample(false);
	}

	return false;
}

bool handleLeftMouseButton(void)
{
	int32_t guiButton;

	if (editor.swapChannelFlag || editor.ui.editTextFlag)
		return false;

	// handle volume toolbox in sampler screen
	if (editor.ui.samplerVolBoxShown)
	{
		handleSamplerVolumeBox();
		return true;
	}

	// handle filters toolbox in sampler
	else if (editor.ui.samplerFiltersBoxShown)
	{
		handleSamplerFiltersBox();
		return true;
	}

	// cancel note input gadgets with left/right mouse button
	if (editor.ui.changingSmpResample || editor.ui.changingChordNote || editor.ui.changingDrumPadNote)
	{
		if (input.mouse.leftButtonPressed || input.mouse.rightButtonPressed)
		{
			editor.ui.changingSmpResample = false;
			editor.ui.changingChordNote = false;
			editor.ui.changingDrumPadNote = false;

			editor.ui.updateResampleNote = true;
			editor.ui.updateNote1Text = true;
			editor.ui.updateNote2Text = true;
			editor.ui.updateNote3Text = true;
			editor.ui.updateNote4Text = true;

			setPrevStatusMessage();
			pointerSetPreviousMode();
		}

		return true;
	}

	if (!input.mouse.leftButtonPressed)
		return false;

	// handle QUIT ask dialog while Disk Op. filling is ongoing
	if (editor.diskop.isFilling)
	{
		if (editor.ui.askScreenShown && editor.ui.askScreenType == ASK_QUIT)
		{
			if (input.mouse.y >= 71 && input.mouse.y <= 81)
			{
				if (input.mouse.x >= 171 && input.mouse.x <= 196)
				{
					// YES button
					editor.ui.askScreenShown = false;
					editor.ui.answerNo  = false;
					editor.ui.answerYes = true;
					handleAskYes();
				}
				else if (input.mouse.x >= 234 && input.mouse.x <= 252)
				{
					// NO button
					editor.ui.askScreenShown = false;
					editor.ui.answerNo  = true;
					editor.ui.answerYes = false;
					handleAskNo();
				}
			}
		}

		return true;
	}

	// CANCEL and YES/NO (ask exit) buttons while MOD2WAV is ongoing
	if (editor.isWAVRendering)
	{
		if (editor.ui.askScreenShown && editor.ui.askScreenType == ASK_QUIT)
		{
			if (input.mouse.x >= 171 && input.mouse.x <= 196)
			{
				// YES button
				editor.isWAVRendering = false;
				SDL_WaitThread(editor.mod2WavThread, NULL);

				editor.ui.askScreenShown = false;
				editor.ui.answerNo = false;
				editor.ui.answerYes = true;
				handleAskYes();
			}
			else if (input.mouse.x >= 234 && input.mouse.x <= 252)
			{
				// NO button
				editor.ui.askScreenShown = false;
				editor.ui.answerNo = true;
				editor.ui.answerYes = false;
				handleAskNo();

				pointerSetMode(POINTER_MODE_READ_DIR, NO_CARRY);
				setStatusMessage("RENDERING MOD...", NO_CARRY);
			}
		}
		else if (input.mouse.y >= 58 && input.mouse.y <= 68 && input.mouse.x >= 133 && input.mouse.x <= 186)
		{
			// CANCEL button
			editor.abortMod2Wav = true;
		}

		return true;
	}

	guiButton = checkGUIButtons();
	if (guiButton == -1)
		return false;

	return handleGUIButtons(guiButton);
}

void updateMouseCounters(void)
{
	if (input.mouse.buttonWaiting)
	{
		if (++input.mouse.buttonWaitCounter > VBLANK_HZ/4) // quarter of a second
		{
			input.mouse.buttonWaitCounter = 0;
			input.mouse.buttonWaiting = false;
		}
	}

	if (editor.errorMsgActive)
	{
		if (++editor.errorMsgCounter >= (uint8_t)(VBLANK_HZ/1.25))
		{
			editor.errorMsgCounter = 0;

			// don't reset status text/mouse color during certain modes
			if (!editor.ui.askScreenShown      && !editor.ui.clearScreenShown    &&
				!editor.ui.pat2SmpDialogShown  && !editor.ui.changingChordNote   &&
				!editor.ui.changingDrumPadNote && !editor.ui.changingSmpResample &&
				!editor.swapChannelFlag)
			{
				pointerSetPreviousMode();
				setPrevStatusMessage();
			}

			editor.errorMsgActive = false;
			editor.errorMsgBlock  = false;

			diskOpShowSelectText();
		}
	}
}

static bool handleGUIButtons(int32_t button) // are you prepared to enter the jungle?
{
	char pat2SmpText[24];
	int8_t *ptr8_1, *ptr8_2, *ptr8_3, *ptr8_4;
	int8_t tmpSmp, modTmp, modDat;
	uint8_t i;
	int16_t tmp16;
	int32_t smp32, j, modPos, oldVal, tmp32;
	double dSmp;
	moduleSample_t *s;

	switch (button)
	{
		case PTB_DUMMY: return false; // for gaps/empty spaces/dummies

		case PTB_PAT2SMP:
		{
			editor.ui.askScreenShown = true;
			editor.ui.askScreenType = ASK_PAT2SMP;
			editor.ui.pat2SmpDialogShown = true;
			pointerSetMode(POINTER_MODE_MSG1, NO_CARRY);

			if (editor.songPlaying)
				sprintf(pat2SmpText, "ROW 00 TO SMP %02X?", editor.currSample + 1);
			else
				sprintf(pat2SmpText, "ROW %02d TO SMP %02X?", modEntry->currRow, editor.currSample + 1);

			setStatusMessage(pat2SmpText, NO_CARRY);
			renderAskDialog();
		}
		break;

		// Edit Op. All Screens
		case PTB_EO_TITLEBAR:
		{
			     if (editor.ui.editOpScreen == 0) editor.sampleAllFlag ^= 1;
			else if (editor.ui.editOpScreen == 1) editor.trackPattFlag = (editor.trackPattFlag + 1) % 3;
			else if (editor.ui.editOpScreen == 2) editor.halfClipFlag ^= 1;
			else if (editor.ui.editOpScreen == 3) editor.newOldFlag ^= 1;

			renderEditOpMode();
		}
		break;

		case PTB_EO_1:
		{
			editor.ui.editOpScreen = 0;
			renderEditOpScreen();
		}
		break;

		case PTB_EO_2:
		{
			editor.ui.editOpScreen = 1;
			renderEditOpScreen();
		}
		break;

		case PTB_EO_3:
		{
			editor.ui.editOpScreen = 2;
			renderEditOpScreen();
		}
		break;

		case PTB_EO_EXIT:
		{
			editor.ui.aboutScreenShown = false;
			editor.ui.editOpScreenShown = false;
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
			editor.ui.updateRecordText = true;
		}
		break;

		case PTB_EO_DELETE: delSampleTrack(); break;
		case PTB_EO_EXCHGE: exchSampleTrack(); break;
		case PTB_EO_COPY: copySampleTrack(); break;

		case PTB_EO_FROM:
		{
			editor.sampleFrom = editor.currSample + 1;
			editor.ui.updateFromText = true;
		}
		break;

		case PTB_EO_TO:
		{
			editor.sampleTo = editor.currSample + 1;
			editor.ui.updateToText = true;
		}
		break;

		case PTB_EO_KILL:
		{
			editor.ui.askScreenShown = true;
			editor.ui.askScreenType = ASK_KILL_SAMPLE;
			pointerSetMode(POINTER_MODE_MSG1, NO_CARRY);
			setStatusMessage("KILL SAMPLE ?", NO_CARRY);
			renderAskDialog();
		}
		break;

		case PTB_EO_QUANTIZE:
		{
			editor.ui.tmpDisp16 = ptConfig.quantizeValue;
			editor.quantizeValueDisp = &editor.ui.tmpDisp16;
			editor.ui.numPtr16 = &editor.ui.tmpDisp16;
			editor.ui.numLen = 2;
			editor.ui.editTextPos = 2824; // (y * 40) + x
			getNumLine(TEXT_EDIT_DECIMAL, PTB_EO_QUANTIZE);
		}
		break;

		case PTB_EO_METRO_1: // metronome speed
		{
			editor.ui.tmpDisp16 = editor.metroSpeed;
			editor.metroSpeedDisp = &editor.ui.tmpDisp16;
			editor.ui.numPtr16 = &editor.ui.tmpDisp16;
			editor.ui.numLen = 2;
			editor.ui.editTextPos = 3261; // (y * 40) + x
			getNumLine(TEXT_EDIT_DECIMAL, PTB_EO_METRO_1);
		}
		break;

		case PTB_EO_METRO_2: // metronome channel
		{
			editor.ui.tmpDisp16 = editor.metroChannel;
			editor.metroChannelDisp = &editor.ui.tmpDisp16;
			editor.ui.numPtr16 = &editor.ui.tmpDisp16;
			editor.ui.numLen = 2;
			editor.ui.editTextPos = 3264; // (y * 40) + x
			getNumLine(TEXT_EDIT_DECIMAL, PTB_EO_METRO_2);
		}
		break;

		case PTB_EO_FROM_NUM:
		{
			editor.ui.tmpDisp8 = editor.sampleFrom;
			editor.sampleFromDisp = &editor.ui.tmpDisp8;
			editor.ui.numPtr8 = &editor.ui.tmpDisp8;
			editor.ui.numLen = 2;
			editor.ui.numBits = 8;
			editor.ui.editTextPos = 3273; // (y * 40) + x
			getNumLine(TEXT_EDIT_HEX, PTB_EO_FROM_NUM);
		}
		break;

		case PTB_EO_TO_NUM:
		{
			editor.ui.tmpDisp8 = editor.sampleTo;
			editor.sampleToDisp = &editor.ui.tmpDisp8;
			editor.ui.numPtr8 = &editor.ui.tmpDisp8;
			editor.ui.numLen = 2;
			editor.ui.numBits = 8;
			editor.ui.editTextPos = 3713; // (y * 40) + x
			getNumLine(TEXT_EDIT_HEX, PTB_EO_TO_NUM);
		}
		break;

		case PTB_EO_FROM_UP:
		{
			if (editor.sampleFrom < 0x1F)
			{
				editor.sampleFrom++;
				editor.ui.updateFromText = true;
			}
		}
		break;

		case PTB_EO_FROM_DOWN:
		{
			if (editor.sampleFrom > 0x00)
			{
				editor.sampleFrom--;
				editor.ui.updateFromText = true;
			}
		}
		break;

		case PTB_EO_TO_UP:
		{
			if (editor.sampleTo < 0x1F)
			{
				editor.sampleTo++;
				editor.ui.updateToText = true;
			}
		}
		break;

		case PTB_EO_TO_DOWN:
		{
			if (editor.sampleTo > 0x00)
			{
				editor.sampleTo--;
				editor.ui.updateToText = true;
			}
		}
		break;

		case PTB_EO_KEYS:
		{
			editor.multiFlag ^= 1;
			editor.ui.updateTrackerFlags = true;
			editor.ui.updateKeysText = true;
		}
		break;
		// ----------------------------------------------------------

		// Edit Op. Screen #3

		case PTB_EO_MIX:
		{
			if (!input.mouse.rightButtonPressed)
			{
				editor.mixFlag = true;
				editor.ui.showTextPtr = editor.mixText;
				editor.ui.textEndPtr = editor.mixText + 15;
				editor.ui.textLength = 16;
				editor.ui.editTextPos = 1936; // (y * 40) + x
				editor.ui.dstOffset = NULL;
				editor.ui.dstOffsetEnd = false;
				editor.ui.updateMixText = true;
				getTextLine(PTB_EO_MIX);
			}
			else
			{
				s = &modEntry->samples[editor.currSample];
				if (s->length == 0)
				{
					displayErrorMsg("SAMPLE IS EMPTY");
					break;
				}

				if (editor.samplePos == s->length)
				{
					displayErrorMsg("INVALID POS !");
					break;
				}

				ptr8_1 = (int8_t *)malloc(MAX_SAMPLE_LEN);
				if (ptr8_1 == NULL)
				{
					statusOutOfMemory();
					return true;
				}

				memcpy(ptr8_1, &modEntry->sampleData[s->offset], MAX_SAMPLE_LEN);

				ptr8_2 = &modEntry->sampleData[s->offset+editor.samplePos];
				ptr8_3 = &modEntry->sampleData[s->offset+s->length-1];
				ptr8_4 = ptr8_1;

				editor.modulateOffset = 0;
				editor.modulatePos = 0;

				do
				{
					tmp16 = *ptr8_2 + *ptr8_1;
					if (editor.halfClipFlag == 0)
						tmp16 >>= 1;

					CLAMP8(tmp16);
					*ptr8_2++ = (int8_t)tmp16;

					if (editor.modulateSpeed == 0)
					{
						ptr8_1++;
					}
					else
					{
						editor.modulatePos += editor.modulateSpeed;

						modTmp = (editor.modulatePos / 4096) & 0xFF;
						modDat = vibratoTable[modTmp & 0x1F] / 4;
						modPos = ((modTmp & 32) ? (editor.modulateOffset - modDat) : (editor.modulateOffset + modDat)) + 2048;

						editor.modulateOffset = modPos;
						modPos /= 2048;
						modPos = CLAMP(modPos, 0, s->length - 1);
						ptr8_1 = &ptr8_4[modPos];
					}
				}
				while (ptr8_2 < ptr8_3);
				free(ptr8_4);

				fixSampleBeep(s);
				if (editor.ui.samplerScreenShown)
					displaySample();

				updateWindowTitle(MOD_IS_MODIFIED);
			}
		}
		break;

		case PTB_EO_ECHO:
		{
			s = &modEntry->samples[editor.currSample];
			if (s->length == 0)
			{
				displayErrorMsg("SAMPLE IS EMPTY");
				break;
			}

			if (editor.samplePos == 0)
			{
				displayErrorMsg("SET SAMPLE POS !");
				break;
			}

			if (editor.samplePos == s->length)
			{
				displayErrorMsg("INVALID POS !");
				break;
			}

			ptr8_1 = &modEntry->sampleData[s->offset+editor.samplePos];
			ptr8_2 = &modEntry->sampleData[s->offset];
			ptr8_3 = ptr8_2;

			editor.modulateOffset = 0;
			editor.modulatePos = 0;

			for (j = 0; j < s->length; j++)
			{
				tmp16 = (*ptr8_2 + *ptr8_1) >> 1;
				CLAMP8(tmp16);

				*ptr8_1++ = (int8_t)tmp16;

				if (editor.modulateSpeed == 0)
				{
					ptr8_2++;
				}
				else
				{
					editor.modulatePos += editor.modulateSpeed;

					modTmp = (editor.modulatePos / 4096) & 0xFF;
					modDat = vibratoTable[modTmp & 0x1F] / 4;
					modPos = ((modTmp & 32) ? (editor.modulateOffset - modDat) : (editor.modulateOffset + modDat)) + 2048;

					editor.modulateOffset = modPos;
					modPos /= 2048;
					modPos = CLAMP(modPos, 0, s->length - 1);
					ptr8_2 = &ptr8_3[modPos];
				}
			}

			if (editor.halfClipFlag != 0)
			{
				for (j = 0; j < s->length; j++)
				{
					tmp16 = ptr8_3[j] + ptr8_3[j];
					CLAMP8(tmp16);
					ptr8_3[j] = (int8_t)tmp16;
				}
			}

			fixSampleBeep(s);
			if (editor.ui.samplerScreenShown)
				displaySample();

			updateWindowTitle(MOD_IS_MODIFIED);
		}
		break;

		case PTB_EO_POS_NUM:
		{
			if (input.mouse.rightButtonPressed)
			{
				editor.samplePos = 0;
				editor.ui.updatePosText = true;
			}
			else
			{
				editor.ui.tmpDisp16 = editor.samplePos;
				editor.samplePosDisp = &editor.ui.tmpDisp16;
				editor.ui.numPtr16 = &editor.ui.tmpDisp16;
				editor.ui.numLen = 4;
				editor.ui.numBits = 16;
				editor.ui.editTextPos = 2391; // (y * 40) + x
				getNumLine(TEXT_EDIT_HEX, PTB_EO_POS_NUM);
			}
		}
		break;

		case PTB_EO_POS_UP: edPosUpButton(INCREMENT_SLOW); break;
		case PTB_EO_POS_DOWN: edPosDownButton(INCREMENT_SLOW); break;

		case PTB_EO_BOOST: // this is actually treble increase
		{
			s = &modEntry->samples[editor.currSample];
			if (s->length == 0)
			{
				displayErrorMsg("SAMPLE IS EMPTY");
				break;
			}

			boostSample(editor.currSample, false);
			if (editor.ui.samplerScreenShown)
				displaySample();

			updateWindowTitle(MOD_IS_MODIFIED);
		}
		break;

		case PTB_EO_FILTER: // this is actually treble decrease
		{
			s = &modEntry->samples[editor.currSample];
			if (s->length == 0)
			{
				displayErrorMsg("SAMPLE IS EMPTY");
				break;
			}

			filterSample(editor.currSample, false);
			if (editor.ui.samplerScreenShown)
				displaySample();

			updateWindowTitle(MOD_IS_MODIFIED);
		}
		break;

		case PTB_EO_MOD_NUM:
		{
			if (input.mouse.rightButtonPressed)
			{
				editor.modulateSpeed = 0;
				editor.ui.updateModText = true;
			}
		}
		break;

		case PTB_EO_MOD:
		{
			s = &modEntry->samples[editor.currSample];
			if (s->length == 0)
			{
				displayErrorMsg("SAMPLE IS EMPTY");
				break;
			}

			if (editor.modulateSpeed == 0)
			{
				displayErrorMsg("SET MOD. SPEED !");
				break;
			}

			ptr8_1 = &modEntry->sampleData[s->offset];

			ptr8_3 = (int8_t *)malloc(MAX_SAMPLE_LEN);
			if (ptr8_3 == NULL)
			{
				statusOutOfMemory();
				return true;
			}

			ptr8_2 = ptr8_3;

			memcpy(ptr8_2, ptr8_1, MAX_SAMPLE_LEN);

			editor.modulateOffset = 0;
			editor.modulatePos = 0;

			for (j = 0; j < s->length; j++)
			{
				*ptr8_1++ = *ptr8_2;

				editor.modulatePos += editor.modulateSpeed;

				modTmp = (editor.modulatePos / 4096) & 0xFF;
				modDat = vibratoTable[modTmp & 0x1F] / 4;
				modPos = ((modTmp & 32) ? (editor.modulateOffset - modDat) : (editor.modulateOffset + modDat)) + 2048;

				editor.modulateOffset = modPos;

				modPos /= 2048;
				modPos = CLAMP(modPos, 0, s->length - 1);
				ptr8_2 = &ptr8_3[modPos];
			}

			free(ptr8_3);

			fixSampleBeep(s);
			if (editor.ui.samplerScreenShown)
				displaySample();

			updateWindowTitle(MOD_IS_MODIFIED);
		}
		break;

		case PTB_EO_MOD_UP: edModUpButton(); break;
		case PTB_EO_MOD_DOWN: edModDownButton(); break;

		case PTB_EO_X_FADE:
		{
			s = &modEntry->samples[editor.currSample];

			if (s->length == 0)
			{
				displayErrorMsg("SAMPLE IS EMPTY");
				break;
			}

			ptr8_1 = &modEntry->sampleData[s->offset];
			ptr8_2 = &modEntry->sampleData[s->offset+s->length-1];

			do
			{
				tmp16 = *ptr8_1 + *ptr8_2;
				if (editor.halfClipFlag == 0)
					tmp16 >>= 1;

				CLAMP8(tmp16);
				tmpSmp = (int8_t)tmp16;

				*ptr8_1++ = tmpSmp;
				*ptr8_2-- = tmpSmp;
			}
			while (ptr8_1 < ptr8_2);

			fixSampleBeep(s);
			if (editor.ui.samplerScreenShown)
				displaySample();

			updateWindowTitle(MOD_IS_MODIFIED);
		}
		break;

		case PTB_EO_BACKWD:
		{
			s = &modEntry->samples[editor.currSample];
			if (s->length == 0)
			{
				displayErrorMsg("SAMPLE IS EMPTY");
				break;
			}

			if (editor.markStartOfs != -1 && editor.markStartOfs != editor.markEndOfs && editor.markEndOfs != 0)
			{
				ptr8_1 = &modEntry->sampleData[s->offset+editor.markStartOfs];
				ptr8_2 = &modEntry->sampleData[s->offset+editor.markEndOfs-1];
			}
			else
			{
				ptr8_1 = &modEntry->sampleData[s->offset];
				ptr8_2 = &modEntry->sampleData[s->offset+s->length-1];
			}

			do
			{
				tmpSmp = *ptr8_1;
				*ptr8_1++ = *ptr8_2;
				*ptr8_2-- = tmpSmp;
			}
			while (ptr8_1 < ptr8_2);

			fixSampleBeep(s);
			if (editor.ui.samplerScreenShown)
				displaySample();

			updateWindowTitle(MOD_IS_MODIFIED);
		}
		break;

		case PTB_EO_CB:
		{
			s = &modEntry->samples[editor.currSample];
			if (s->length == 0)
			{
				displayErrorMsg("SAMPLE IS EMPTY");
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

			memcpy(&modEntry->sampleData[s->offset], &modEntry->sampleData[s->offset + editor.samplePos], MAX_SAMPLE_LEN - editor.samplePos);
			memset(&modEntry->sampleData[s->offset + (MAX_SAMPLE_LEN - editor.samplePos)], 0, editor.samplePos);

			if (editor.samplePos > s->loopStart)
			{
				s->loopStart = 0;
				s->loopLength = 2;
			}
			else
			{
				s->loopStart = (s->loopStart - editor.samplePos) & 0xFFFE;
			}

			s->length = (s->length - editor.samplePos) & 0xFFFE;

			editor.samplePos = 0;
			fixSampleBeep(s);
			updateCurrSample();

			updateWindowTitle(MOD_IS_MODIFIED);
		}
		break;

		case PTB_EO_CHORD:
		{
			editor.ui.editOpScreen = 3;
			renderEditOpScreen();
		}
		break;

		// fade up
		case PTB_EO_FU:
		{
			s = &modEntry->samples[editor.currSample];
			if (s->length == 0)
			{
				displayErrorMsg("SAMPLE IS EMPTY");
				break;
			}

			if (editor.samplePos == 0)
			{
				displayErrorMsg("INVALID POS !");
				break;
			}

			ptr8_1 = &modEntry->sampleData[s->offset];
			for (j = 0; j < editor.samplePos; j++)
			{
				dSmp = ((*ptr8_1) * j) / (double)editor.samplePos;
				smp32 = (int32_t)dSmp;
				CLAMP8(smp32);
				*ptr8_1++ = (int8_t)smp32;
			}

			fixSampleBeep(s);
			if (editor.ui.samplerScreenShown)
				displaySample();

			updateWindowTitle(MOD_IS_MODIFIED);
		}
		break;

		// fade down
		case PTB_EO_FD:
		{
			s = &modEntry->samples[editor.currSample];
			if (s->length == 0)
			{
				displayErrorMsg("SAMPLE IS EMPTY");
				break;
			}

			if (editor.samplePos >= (s->length - 1))
			{
				displayErrorMsg("INVALID POS !");
				break;
			}

			ptr8_1 = &modEntry->sampleData[s->offset+s->length-1];
			for (j = editor.samplePos; j < s->length; j++)
			{
				dSmp = (*ptr8_1) * (j - editor.samplePos);

				tmp32 = (s->length - 1) - editor.samplePos;
				if (tmp32 > 0)
					dSmp /= (double)tmp32;

				smp32 = (int32_t)dSmp;
				CLAMP8(smp32);
				*ptr8_1-- = (int8_t)smp32;
			}

			fixSampleBeep(s);
			if (editor.ui.samplerScreenShown)
				displaySample();

			updateWindowTitle(MOD_IS_MODIFIED);
		}
		break;

		case PTB_EO_UPSAMP:
		{
			s = &modEntry->samples[editor.currSample];
			if (s->length == 0)
			{
				displayErrorMsg("SAMPLE IS EMPTY");
				break;
			}

			editor.ui.askScreenShown = true;
			editor.ui.askScreenType = ASK_UPSAMPLE;
			pointerSetMode(POINTER_MODE_MSG1, NO_CARRY);
			setStatusMessage("UPSAMPLE ?", NO_CARRY);
			renderAskDialog();
		}
		break;

		case PTB_EO_DNSAMP:
		{
			s = &modEntry->samples[editor.currSample];
			if (s->length == 0)
			{
				displayErrorMsg("SAMPLE IS EMPTY");
				break;
			}

			editor.ui.askScreenShown = true;
			editor.ui.askScreenType = ASK_DOWNSAMPLE;
			pointerSetMode(POINTER_MODE_MSG1, NO_CARRY);
			setStatusMessage("DOWNSAMPLE ?", NO_CARRY);
			renderAskDialog();
		}
		break;

		case PTB_EO_VOL_NUM:
		{
			if (input.mouse.rightButtonPressed)
			{
				editor.sampleVol = 100;
				editor.ui.updateVolText = true;
			}
			else
			{
				editor.ui.tmpDisp16 = editor.sampleVol;
				editor.sampleVolDisp = &editor.ui.tmpDisp16;
				editor.ui.numPtr16 = &editor.ui.tmpDisp16;
				editor.ui.numLen = 3;
				editor.ui.editTextPos = 3711; // (y * 40) + x
				getNumLine(TEXT_EDIT_DECIMAL, PTB_EO_VOL_NUM);
			}
		}
		break;

		case PTB_EO_VOL:
		{
			s = &modEntry->samples[editor.currSample];
			if (s->length == 0)
			{
				displayErrorMsg("SAMPLE IS EMPTY");
				break;
			}

			if (editor.sampleVol != 100)
			{
				ptr8_1 = &modEntry->sampleData[modEntry->samples[editor.currSample].offset];
				for (j = 0; j < s->length; j++)
				{
					tmp16 = (int16_t)roundf(((*ptr8_1) * editor.sampleVol) / 100.0f);
					CLAMP8(tmp16);
					*ptr8_1++ = (int8_t)tmp16;
				}

				fixSampleBeep(s);
				if (editor.ui.samplerScreenShown)
					displaySample();

				updateWindowTitle(MOD_IS_MODIFIED);
			}
		}
		break;

		case PTB_EO_VOL_UP: edVolUpButton(); break;
		case PTB_EO_VOL_DOWN: edVolDownButton(); break;
		// ----------------------------------------------------------

		// Edit Op. Screen #4

		case PTB_EO_DOCHORD:
		{
			editor.ui.askScreenShown = true;
			editor.ui.askScreenType = ASK_MAKE_CHORD;
			pointerSetMode(POINTER_MODE_MSG1, NO_CARRY);
			setStatusMessage("MAKE CHORD?", NO_CARRY);
			renderAskDialog();
		}
		break;

		case PTB_EO_MAJOR:
		{
			if (editor.note1 == 36)
			{
				displayErrorMsg("NO BASENOTE!");
				break;
			}

			editor.oldNote1 = editor.note1;
			editor.oldNote2 = editor.note2;
			editor.oldNote3 = editor.note3;
			editor.oldNote4 = editor.note4;

			editor.note2 = editor.note1 + 4;
			editor.note3 = editor.note1 + 7;

			if (editor.note2 >= 36) editor.note2 -= 12;
			if (editor.note3 >= 36) editor.note3 -= 12;

			editor.note4 = 36;

			editor.ui.updateNote2Text = true;
			editor.ui.updateNote3Text = true;
			editor.ui.updateNote4Text = true;

			recalcChordLength();
		}
		break;

		case PTB_EO_MAJOR7:
		{
			if (editor.note1 == 36)
			{
				displayErrorMsg("NO BASENOTE!");
				break;
			}

			editor.oldNote1 = editor.note1;
			editor.oldNote2 = editor.note2;
			editor.oldNote3 = editor.note3;
			editor.oldNote4 = editor.note4;

			editor.note2 = editor.note1 + 4;
			editor.note3 = editor.note1 + 7;
			editor.note4 = editor.note1 + 11;

			if (editor.note2 >= 36) editor.note2 -= 12;
			if (editor.note3 >= 36) editor.note3 -= 12;
			if (editor.note4 >= 36) editor.note4 -= 12;

			editor.ui.updateNote2Text = true;
			editor.ui.updateNote3Text = true;
			editor.ui.updateNote4Text = true;

			recalcChordLength();
		}
		break;

		case PTB_EO_NOTE1:
		{
			if (input.mouse.rightButtonPressed)
			{
				editor.note1 = 36;
			}
			else
			{
				editor.ui.changingChordNote = 1;
				setStatusMessage("SELECT NOTE", NO_CARRY);
				pointerSetMode(POINTER_MODE_MSG1, NO_CARRY);
			}
			editor.ui.updateNote1Text = true;
		}
		break;

		case PTB_EO_NOTE1_UP: edNote1UpButton(); break;
		case PTB_EO_NOTE1_DOWN: edNote1DownButton(); break;

		case PTB_EO_NOTE2:
		{
			if (input.mouse.rightButtonPressed)
			{
				editor.note2 = 36;
			}
			else
			{
				editor.ui.changingChordNote = 2;
				setStatusMessage("SELECT NOTE", NO_CARRY);
				pointerSetMode(POINTER_MODE_MSG1, NO_CARRY);
			}
			editor.ui.updateNote2Text = true;
		}
		break;

		case PTB_EO_NOTE2_UP: edNote2UpButton(); break;
		case PTB_EO_NOTE2_DOWN: edNote2DownButton(); break;

		case PTB_EO_NOTE3:
		{
			if (input.mouse.rightButtonPressed)
			{
				editor.note3 = 36;
			}
			else
			{
				editor.ui.changingChordNote = 3;
				setStatusMessage("SELECT NOTE", NO_CARRY);
				pointerSetMode(POINTER_MODE_MSG1, NO_CARRY);
			}
			editor.ui.updateNote3Text = true;
		}
		break;

		case PTB_EO_NOTE3_UP: edNote3UpButton(); break;
		case PTB_EO_NOTE3_DOWN: edNote3DownButton(); break;

		case PTB_EO_NOTE4:
		{
			if (input.mouse.rightButtonPressed)
			{
				editor.note4 = 36;
			}
			else
			{
				editor.ui.changingChordNote = 4;
				setStatusMessage("SELECT NOTE", NO_CARRY);
				pointerSetMode(POINTER_MODE_MSG1, NO_CARRY);
			}
			editor.ui.updateNote4Text = true;
		}
		break;

		case PTB_EO_NOTE4_UP: edNote4UpButton(); break;
		case PTB_EO_NOTE4_DOWN: edNote4DownButton(); break;

		case PTB_EO_RESET:
		{
			editor.note1 = 36;
			editor.note2 = 36;
			editor.note3 = 36;
			editor.note4 = 36;

			editor.chordLengthMin = false;

			editor.ui.updateNote1Text = true;
			editor.ui.updateNote2Text = true;
			editor.ui.updateNote3Text = true;
			editor.ui.updateNote4Text = true;

			recalcChordLength();
		}
		break;

		case PTB_EO_MINOR:
		{
			if (editor.note1 == 36)
			{
				displayErrorMsg("NO BASENOTE!");
				break;
			}

			editor.oldNote1 = editor.note1;
			editor.oldNote2 = editor.note2;
			editor.oldNote3 = editor.note3;
			editor.oldNote4 = editor.note4;

			editor.note2 = editor.note1 + 3;
			editor.note3 = editor.note1 + 7;

			if (editor.note2 >= 36) editor.note2 -= 12;
			if (editor.note3 >= 36) editor.note3 -= 12;

			editor.note4 = 36;

			editor.ui.updateNote2Text = true;
			editor.ui.updateNote3Text = true;
			editor.ui.updateNote4Text = true;

			recalcChordLength();
		}
		break;

		case PTB_EO_MINOR7:
		{
			if (editor.note1 == 36)
			{
				displayErrorMsg("NO BASENOTE!");
				break;
			}

			editor.oldNote1 = editor.note1;
			editor.oldNote2 = editor.note2;
			editor.oldNote3 = editor.note3;
			editor.oldNote4 = editor.note4;

			editor.note2 = editor.note1 + 3;
			editor.note3 = editor.note1 + 7;
			editor.note4 = editor.note1 + 10;

			if (editor.note2 >= 36) editor.note2 -= 12;
			if (editor.note3 >= 36) editor.note3 -= 12;
			if (editor.note4 >= 36) editor.note4 -= 12;

			editor.ui.updateNote2Text = true;
			editor.ui.updateNote3Text = true;
			editor.ui.updateNote4Text = true;

			recalcChordLength();
		}
		break;

		case PTB_EO_UNDO:
		{
			editor.note1 = editor.oldNote1;
			editor.note2 = editor.oldNote2;
			editor.note3 = editor.oldNote3;
			editor.note4 = editor.oldNote4;

			editor.ui.updateNote1Text = true;
			editor.ui.updateNote2Text = true;
			editor.ui.updateNote3Text = true;
			editor.ui.updateNote4Text = true;

			recalcChordLength();
		}
		break;

		case PTB_EO_SUS4:
		{
			if (editor.note1 == 36)
			{
				displayErrorMsg("NO BASENOTE!");
				break;
			}

			editor.oldNote1 = editor.note1;
			editor.oldNote2 = editor.note2;
			editor.oldNote3 = editor.note3;
			editor.oldNote4 = editor.note4;

			editor.note2 = editor.note1 + 5;
			editor.note3 = editor.note1 + 7;

			if (editor.note2 >= 36) editor.note2 -= 12;
			if (editor.note3 >= 36) editor.note3 -= 12;

			editor.note4 = 36;

			editor.ui.updateNote2Text = true;
			editor.ui.updateNote3Text = true;
			editor.ui.updateNote4Text = true;

			recalcChordLength();
		}
		break;

		case PTB_EO_MAJOR6:
		{
			if (editor.note1 == 36)
			{
				displayErrorMsg("NO BASENOTE!");
				break;
			}

			editor.oldNote1 = editor.note1;
			editor.oldNote2 = editor.note2;
			editor.oldNote3 = editor.note3;
			editor.oldNote4 = editor.note4;

			editor.note2 = editor.note1 + 4;
			editor.note3 = editor.note1 + 7;
			editor.note4 = editor.note1 + 9;

			if (editor.note2 >= 36) editor.note2 -= 12;
			if (editor.note3 >= 36) editor.note3 -= 12;
			if (editor.note4 >= 36) editor.note4 -= 12;

			editor.ui.updateNote2Text = true;
			editor.ui.updateNote3Text = true;
			editor.ui.updateNote4Text = true;

			recalcChordLength();
		}
		break;

		case PTB_EO_LENGTH:
		{
			if (modEntry->samples[editor.currSample].loopLength == 2 && modEntry->samples[editor.currSample].loopStart == 0)
			{
				editor.chordLengthMin = input.mouse.rightButtonPressed ? true : false;
				recalcChordLength();
			}
		}
		break;

		case PTB_EO_MINOR6:
		{
			if (editor.note1 == 36)
			{
				displayErrorMsg("NO BASENOTE!");
				break;
			}

			editor.oldNote1 = editor.note1;
			editor.oldNote2 = editor.note2;
			editor.oldNote3 = editor.note3;
			editor.oldNote4 = editor.note4;

			editor.note2 = editor.note1 + 3;
			editor.note3 = editor.note1 + 7;
			editor.note4 = editor.note1 + 9;

			if (editor.note2 >= 36) editor.note2 -= 12;
			if (editor.note3 >= 36) editor.note3 -= 12;
			if (editor.note4 >= 36) editor.note4 -= 12;

			editor.ui.updateNote2Text = true;
			editor.ui.updateNote3Text = true;
			editor.ui.updateNote4Text = true;

			recalcChordLength();
		}
		break;
		// ----------------------------------------------------------

		case PTB_ABOUT:
		{
			editor.ui.aboutScreenShown ^= 1;
			if (editor.ui.aboutScreenShown)
			{
				renderAboutScreen();
			}
			else
			{
				     if (editor.ui.visualizerMode == VISUAL_QUADRASCOPE) renderQuadrascopeBg();
				else if (editor.ui.visualizerMode == VISUAL_SPECTRUM) renderSpectrumAnalyzerBg();
			}
		}
		break;

		case PTB_PE_PATT:
		{
			if (editor.currMode == MODE_IDLE || editor.currMode == MODE_EDIT)
			{
				editor.ui.tmpDisp16 = modEntry->currOrder;
				if (editor.ui.tmpDisp16 > modEntry->head.orderCount-1)
					editor.ui.tmpDisp16 = modEntry->head.orderCount-1;

				editor.ui.tmpDisp16 = modEntry->head.order[editor.ui.tmpDisp16];
				editor.currPosEdPattDisp = &editor.ui.tmpDisp16;
				editor.ui.numPtr16 = &editor.ui.tmpDisp16;
				editor.ui.numLen = 2;
				editor.ui.editTextPos = 2180; // (y * 40) + x
				getNumLine(TEXT_EDIT_DECIMAL, PTB_PE_PATT);
			}
		}
		break;

		case PTB_PE_SCROLLTOP:
		{
			if (modEntry->currOrder != 0)
				modSetPos(0, DONT_SET_ROW);
		}
		break;

		case PTB_PE_SCROLLUP:
		{
			if (modEntry->currOrder > 0)
				modSetPos(modEntry->currOrder - 1, DONT_SET_ROW);
		}
		break;

		case PTB_PE_SCROLLDOWN:
		{
			if (modEntry->currOrder < modEntry->head.orderCount-1)
				modSetPos(modEntry->currOrder + 1, DONT_SET_ROW);
		}
		break;

		case PTB_PE_SCROLLBOT:
		{
			if (modEntry->currOrder != modEntry->head.orderCount-1)
				modSetPos(modEntry->head.orderCount - 1, DONT_SET_ROW);
		}
		break;

		case PTB_PE_EXIT:
		{
			editor.ui.aboutScreenShown = false;
			editor.ui.posEdScreenShown = false;
			displayMainScreen();
		}
		break;

		case PTB_POS:
		case PTB_POSED:
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
		break;

		case PTB_POSS:
		{
			if (editor.currMode == MODE_IDLE || editor.currMode == MODE_EDIT)
			{
				if (input.mouse.rightButtonPressed)
				{
					modEntry->currOrder = 0;
					editor.currPatternDisp = &modEntry->head.order[modEntry->currOrder];

					if (editor.ui.posEdScreenShown)
						editor.ui.updatePosEd = true;
				}
				else
				{
					editor.ui.tmpDisp16 = modEntry->currOrder;
					editor.currPosDisp = &editor.ui.tmpDisp16;
					editor.ui.numPtr16 = &editor.ui.tmpDisp16;
					editor.ui.numLen = 3;
					editor.ui.editTextPos = 169; // (y * 40) + x
					getNumLine(TEXT_EDIT_DECIMAL, PTB_POSS);
				}
			}
		}
		break;

		case PTB_PATTERNS:
		{
			if (editor.currMode == MODE_IDLE || editor.currMode == MODE_EDIT)
			{
				if (input.mouse.rightButtonPressed)
				{
					modEntry->head.order[modEntry->currOrder] = 0;

					editor.ui.updateSongSize = true;
					updateWindowTitle(MOD_IS_MODIFIED);

					if (editor.ui.posEdScreenShown)
						editor.ui.updatePosEd = true;
				}
				else
				{
					editor.ui.tmpDisp16 = modEntry->head.order[modEntry->currOrder];
					editor.currPatternDisp = &editor.ui.tmpDisp16;
					editor.ui.numPtr16 = &editor.ui.tmpDisp16;
					editor.ui.numLen = 2;
					editor.ui.editTextPos = 610; // (y * 40) + x
					getNumLine(TEXT_EDIT_DECIMAL, PTB_PATTERNS);
				}
			}
		}
		break;

		case PTB_LENGTHS:
		{
			if (editor.currMode == MODE_IDLE || editor.currMode == MODE_EDIT)
			{
				if (input.mouse.rightButtonPressed)
				{
					modEntry->head.orderCount = 1;

					tmp16 = modEntry->currOrder;
					if (tmp16 > modEntry->head.orderCount-1)
						tmp16 = modEntry->head.orderCount-1;

					editor.currPosEdPattDisp = &modEntry->head.order[tmp16];

					editor.ui.updateSongSize = true;
					updateWindowTitle(MOD_IS_MODIFIED);

					if (editor.ui.posEdScreenShown)
						editor.ui.updatePosEd = true;
				}
				else
				{
					editor.ui.tmpDisp16 = modEntry->head.orderCount;
					editor.currLengthDisp = &editor.ui.tmpDisp16;
					editor.ui.numPtr16 = &editor.ui.tmpDisp16;
					editor.ui.numLen = 3;
					editor.ui.editTextPos = 1049; // (y * 40) + x
					getNumLine(TEXT_EDIT_DECIMAL, PTB_LENGTHS);
				}
			}
		}
		break;

		case PTB_PATTBOX:
		case PTB_PATTDATA:
		{
			if (!editor.ui.introScreenShown && (editor.currMode == MODE_IDLE || editor.currMode == MODE_EDIT || editor.playMode != PLAY_MODE_NORMAL))
			{
				editor.ui.tmpDisp16 = modEntry->currPattern;
				editor.currEditPatternDisp = &editor.ui.tmpDisp16;
				editor.ui.numPtr16 = &editor.ui.tmpDisp16;
				editor.ui.numLen = 2;
				editor.ui.editTextPos = 5121; // (y * 40) + x
				getNumLine(TEXT_EDIT_DECIMAL, PTB_PATTDATA);
			}
		}
		break;

		case PTB_SAMPLES:
		{
			editor.sampleZero = false;
			editor.ui.tmpDisp8 = editor.currSample;
			editor.currSampleDisp = &editor.ui.tmpDisp8;
			editor.ui.numPtr8 = &editor.ui.tmpDisp8;
			editor.ui.numLen = 2;
			editor.ui.numBits = 8;
			editor.ui.editTextPos = 1930; // (y * 40) + x
			getNumLine(TEXT_EDIT_HEX, PTB_SAMPLES);
		}
		break;

		case PTB_SVOLUMES:
		{
			if (input.mouse.rightButtonPressed)
			{
				modEntry->samples[editor.currSample].volume = 0;
			}
			else
			{
				editor.ui.tmpDisp8 = modEntry->samples[editor.currSample].volume;
				modEntry->samples[editor.currSample].volumeDisp = &editor.ui.tmpDisp8;
				editor.ui.numPtr8 = &editor.ui.tmpDisp8;
				editor.ui.numLen = 2;
				editor.ui.numBits = 8;
				editor.ui.editTextPos = 2370; // (y * 40) + x
				getNumLine(TEXT_EDIT_HEX, PTB_SVOLUMES);
			}
		}
		break;

		case PTB_SLENGTHS:
		{
			if (input.mouse.rightButtonPressed)
			{
				s = &modEntry->samples[editor.currSample];

				turnOffVoices();

				s->length = 0;
				if (s->loopStart+s->loopLength > 2)
				{
					if (s->length < s->loopStart+s->loopLength)
						s->length = s->loopStart+s->loopLength;
				}

				editor.ui.updateSongSize = true;
				editor.ui.updateCurrSampleLength = true;

				if (editor.ui.samplerScreenShown)
					redrawSample();

				recalcChordLength();
				updateWindowTitle(MOD_IS_MODIFIED);
			}
			else
			{
				editor.ui.tmpDisp16 = modEntry->samples[editor.currSample].length;
				modEntry->samples[editor.currSample].lengthDisp = &editor.ui.tmpDisp16;
				editor.ui.numPtr16 = &editor.ui.tmpDisp16;
				editor.ui.numLen = 4;
				editor.ui.numBits = 16;
				editor.ui.editTextPos = 2808; // (y * 40) + x
				getNumLine(TEXT_EDIT_HEX, PTB_SLENGTHS);
			}
		}
		break;

		case PTB_SREPEATS:
		{
			if (input.mouse.rightButtonPressed)
			{
				s = &modEntry->samples[editor.currSample];

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

				editor.ui.updateCurrSampleRepeat = true;
				if (editor.ui.editOpScreenShown && editor.ui.editOpScreen == 3)
					editor.ui.updateLengthText = true;

				if (editor.ui.samplerScreenShown)
					setLoopSprites();

				mixerUpdateLoops();
				updateWindowTitle(MOD_IS_MODIFIED);
			}
			else
			{
				editor.ui.tmpDisp16 = modEntry->samples[editor.currSample].loopStart;
				modEntry->samples[editor.currSample].loopStartDisp = &editor.ui.tmpDisp16;
				editor.ui.numPtr16 = &editor.ui.tmpDisp16;
				editor.ui.numLen = 4;
				editor.ui.numBits = 16;
				editor.ui.editTextPos = 3248; // (y * 40) + x
				getNumLine(TEXT_EDIT_HEX, PTB_SREPEATS);
			}
		}
		break;

		case PTB_SREPLENS:
		{
			if (input.mouse.rightButtonPressed)
			{
				s = &modEntry->samples[editor.currSample];

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

				editor.ui.updateCurrSampleReplen = true;
				if (editor.ui.editOpScreenShown && editor.ui.editOpScreen == 3)
					editor.ui.updateLengthText = true;

				if (editor.ui.samplerScreenShown)
					setLoopSprites();

				mixerUpdateLoops();
				updateWindowTitle(MOD_IS_MODIFIED);
			}
			else
			{
				editor.ui.tmpDisp16 = modEntry->samples[editor.currSample].loopLength;
				modEntry->samples[editor.currSample].loopLengthDisp = &editor.ui.tmpDisp16;
				editor.ui.numPtr16 = &editor.ui.tmpDisp16;
				editor.ui.numLen = 4;
				editor.ui.numBits = 16;
				editor.ui.editTextPos = 3688; // (y * 40) + x
				getNumLine(TEXT_EDIT_HEX, PTB_SREPLENS);
			}
		}
		break;

		case PTB_EDITOP:
		{
			if (editor.ui.editOpScreen == 3) // chord screen
			{
				editor.ui.editOpScreen = 0;
			}
			else
			{
				if (editor.ui.editOpScreenShown)
					editor.ui.editOpScreen = (editor.ui.editOpScreen + 1) % 3;
				else
					editor.ui.editOpScreenShown = true;
			}

			renderEditOpScreen();
		}
		break;

		case PTB_DO_LOADMODULE:
		{
			editor.diskop.mode = DISKOP_MODE_MOD;
			setPathFromDiskOpMode();
			editor.diskop.scrollOffset = 0;
			editor.diskop.cached = false;
			editor.ui.updateDiskOpFileList = true;
			editor.ui.updateLoadMode = true;
		}
		break;

		case PTB_DO_LOADSAMPLE:
		{
			editor.diskop.mode = DISKOP_MODE_SMP;
			setPathFromDiskOpMode();
			editor.diskop.scrollOffset = 0;
			editor.diskop.cached = false;
			editor.ui.updateDiskOpFileList = true;
			editor.ui.updateLoadMode = true;
		}
		break;

		case PTB_LOADSAMPLE: // "LOAD" button next to sample name
		{
			editor.ui.posEdScreenShown = false;
			editor.diskop.mode = DISKOP_MODE_SMP;
			setPathFromDiskOpMode();
			editor.diskop.scrollOffset = 0;
			editor.diskop.cached = false;

			if (!editor.ui.diskOpScreenShown)
			{
				editor.ui.diskOpScreenShown = true;
				renderDiskOpScreen();
			}
			else
			{
				editor.ui.updateDiskOpFileList = true;
				editor.ui.updateLoadMode = true;
			}
		}
		break;

		case PTB_DO_SAVESAMPLE:
		{
			if (editor.diskop.mode != DISKOP_MODE_SMP)
			{
				editor.diskop.mode = DISKOP_MODE_SMP;
				setPathFromDiskOpMode();
				editor.diskop.scrollOffset = 0;
				editor.diskop.cached = false;
				editor.ui.updateLoadMode = true;
			}

			editor.ui.askScreenShown = true;
			editor.ui.askScreenType = ASK_SAVE_SAMPLE;
			pointerSetMode(POINTER_MODE_MSG1, NO_CARRY);
			setStatusMessage("SAVE SAMPLE ?", NO_CARRY);
			renderAskDialog();
		}
		break;

		case PTB_MOD2WAV:
		{
			editor.ui.askScreenShown = true;
			editor.ui.askScreenType = ASK_MOD2WAV;
			pointerSetMode(POINTER_MODE_MSG1, NO_CARRY);
			setStatusMessage("RENDER WAV FILE?", NO_CARRY);
			renderAskDialog();
		}
		break;

		case PTB_SA_RESAMPLENOTE:
		{
			editor.ui.changingSmpResample = true;
			editor.ui.updateResampleNote = true;
			setStatusMessage("SELECT NOTE", NO_CARRY);
			pointerSetMode(POINTER_MODE_MSG1, NO_CARRY);
		}
		break;

		case PTB_SA_RESAMPLE:
		{
			editor.ui.askScreenShown = true;
			editor.ui.askScreenType = ASK_RESAMPLE;
			pointerSetMode(POINTER_MODE_MSG1, NO_CARRY);
			setStatusMessage("RESAMPLE?", NO_CARRY);
			renderAskDialog();
		}
		break;

		case PTB_SA_SAMPLEAREA:
		{
			if (editor.ui.sampleMarkingPos == -1)
			{
				samplerSamplePressed(MOUSE_BUTTON_NOT_HELD);
				return true;
			}
		}
		break;

		case PTB_SA_ZOOMBARAREA:
		{
			input.mouse.lastGUIButton = button;
			if (!editor.ui.forceSampleDrag)
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
			editor.ui.samplerVolBoxShown = true;
			renderSamplerVolBox();
		}
		break;

		case PTB_SA_FILTERS:
		{
			editor.ui.samplerFiltersBoxShown = true;
			renderSamplerFiltersBox();
			fillSampleFilterUndoBuffer();
		}
		break;

		case PTB_SA_STOP:
		{
			for (i = 0; i < AMIGA_VOICES; i++)
				mixerKillVoice(i);
		}
		break;

		case PTB_DO_REFRESH:
		{
			editor.diskop.scrollOffset = 0;
			editor.diskop.cached = false;
			editor.ui.updateDiskOpFileList = true;
		}
		break;

		// TODO: Find a PowerPacker packer and enable this
		// case PTB_DO_PACKMOD: editor.diskop.modPackFlg ^= 1; break;

		case PTB_DO_SAMPLEFORMAT:
		{
			editor.diskop.smpSaveType = (editor.diskop.smpSaveType + 1) % 3;
			editor.ui.updateSaveFormatText = true;
		}
		break;

		case PTB_DO_MODARROW:
		{
			editor.diskop.mode = DISKOP_MODE_MOD;
			editor.diskop.scrollOffset = 0;
			editor.diskop.cached = false;
			editor.ui.updateDiskOpFileList = true;
			editor.ui.updateLoadMode = true;
		}
		break;

		case PTB_DO_SAMPLEARROW:
		{
			editor.diskop.mode = DISKOP_MODE_SMP;
			editor.diskop.scrollOffset = 0;
			editor.diskop.cached = false;
			editor.ui.updateDiskOpFileList = true;
			editor.ui.updateLoadMode = true;
		}
		break;

		case PTB_SA_TUNETONE: toggleTuningTone(); break;

		case PTB_POSINS:
		{
			if ((editor.currMode == MODE_IDLE || editor.currMode == MODE_EDIT) && modEntry->head.orderCount < 128)
			{
				for (i = 0; i < 127-modEntry->currOrder; i++)
					modEntry->head.order[127-i] = modEntry->head.order[(127-i)-1];
				modEntry->head.order[modEntry->currOrder] = 0;

				modEntry->head.orderCount++;
				if (modEntry->currOrder > modEntry->head.orderCount-1)
					editor.currPosEdPattDisp = &modEntry->head.order[modEntry->head.orderCount-1];

				updateWindowTitle(MOD_IS_MODIFIED);

				editor.ui.updateSongSize = true;
				editor.ui.updateSongLength = true;
				editor.ui.updateSongPattern = true;

				if (editor.ui.posEdScreenShown)
					editor.ui.updatePosEd = true;
			}
		}
		break;

		case PTB_POSDEL:
		{
			if ((editor.currMode == MODE_IDLE || editor.currMode == MODE_EDIT) && modEntry->head.orderCount > 1)
			{
				for (i = 0; i < 128-modEntry->currOrder; i++)
					modEntry->head.order[modEntry->currOrder+i] = modEntry->head.order[modEntry->currOrder+i+1];
				modEntry->head.order[127] = 0;

				modEntry->head.orderCount--;
				if (modEntry->currOrder > modEntry->head.orderCount-1)
					editor.currPosEdPattDisp = &modEntry->head.order[modEntry->head.orderCount-1];

				updateWindowTitle(MOD_IS_MODIFIED);

				editor.ui.updateSongSize = true;
				editor.ui.updateSongLength = true;
				editor.ui.updateSongPattern = true;

				if (editor.ui.posEdScreenShown)
					editor.ui.updatePosEd = true;
			}
		}
		break;

		case PTB_DO_SAVEMODULE:
		{
			if (editor.diskop.mode != DISKOP_MODE_MOD)
			{
				editor.diskop.mode = DISKOP_MODE_MOD;
				setPathFromDiskOpMode();
				editor.diskop.scrollOffset = 0;
				editor.diskop.cached = false;
				editor.ui.updateLoadMode = true;
			}

			editor.ui.askScreenShown = true;
			editor.ui.askScreenType = ASK_SAVE_MODULE;
			pointerSetMode(POINTER_MODE_MSG1, NO_CARRY);
			setStatusMessage("SAVE MODULE ?", NO_CARRY);
			renderAskDialog();
		}
		break;

		case PTB_DO_DATAPATH:
		{
			if (input.mouse.rightButtonPressed)
			{
				memset(editor.currPath, 0, PATH_MAX + 1);
				editor.ui.updateDiskOpPathText = true;
			}

			editor.ui.showTextPtr = editor.currPath;
			editor.ui.textEndPtr = &editor.currPath[PATH_MAX - 1];
			editor.ui.textLength = 26;
			editor.ui.editTextPos = 1043; // (y * 40) + x
			editor.ui.dstOffset = &editor.textofs.diskOpPath;
			editor.ui.dstOffsetEnd = false;
			getTextLine(PTB_DO_DATAPATH);
		}
		break;

		case PTB_SONGNAME:
		{
			if (input.mouse.rightButtonPressed)
			{
				memset(modEntry->head.moduleTitle, 0, sizeof (modEntry->head.moduleTitle));
				editor.ui.updateSongName = true;
				updateWindowTitle(MOD_IS_MODIFIED);
			}
			else
			{
				editor.ui.showTextPtr = modEntry->head.moduleTitle;
				editor.ui.textEndPtr = modEntry->head.moduleTitle + 19;
				editor.ui.textLength = 20;
				editor.ui.editTextPos = 4133; // (y * 40) + x
				editor.ui.dstOffset = NULL;
				editor.ui.dstOffsetEnd = false;
				getTextLine(PTB_SONGNAME);
			}
		}
		break;

		case PTB_SAMPLENAME:
		{
			if (input.mouse.rightButtonPressed)
			{
				memset(modEntry->samples[editor.currSample].text, 0, sizeof (modEntry->samples[editor.currSample].text));
				editor.ui.updateCurrSampleName = true;
				updateWindowTitle(MOD_IS_MODIFIED);
			}
			else
			{
				editor.ui.showTextPtr = modEntry->samples[editor.currSample].text;
				editor.ui.textEndPtr = modEntry->samples[editor.currSample].text + 21;
				editor.ui.textLength = 22;
				editor.ui.editTextPos = 4573; // (y * 40) + x
				editor.ui.dstOffset = NULL;
				editor.ui.dstOffsetEnd = false;
				getTextLine(PTB_SAMPLENAME);
			}
		}
		break;

		case PTB_PAT2SMP_HI:
		{
			editor.ui.askScreenShown = false;
			editor.ui.answerNo = false;
			editor.ui.answerYes = true;
			editor.pat2SmpHQ = true;
			handleAskYes();
		}
		break;

		case PTB_PAT2SMP_LO:
		{
			editor.ui.askScreenShown = false;
			editor.ui.answerNo = false;
			editor.ui.answerYes = true;
			editor.pat2SmpHQ = false;
			handleAskYes();
		}
		break;

		case PTB_SUREY:
		{
			editor.ui.askScreenShown = false;
			editor.ui.answerNo = false;
			editor.ui.answerYes = true;
			handleAskYes();
		}
		break;

		case PTB_PAT2SMP_ABORT:
		case PTB_SUREN:
		{
			editor.ui.askScreenShown = false;
			editor.ui.answerNo = true;
			editor.ui.answerYes = false;
			handleAskNo();
		}
		break;

		case PTB_VISUALS:
		{
			if (editor.ui.aboutScreenShown)
			{
				editor.ui.aboutScreenShown = false;
			}
			else if (!input.mouse.rightButtonPressed)
			{
				editor.ui.visualizerMode = (editor.ui.visualizerMode + 1) % 2;
				if (editor.ui.visualizerMode == VISUAL_SPECTRUM)
					memset((int8_t *)editor.spectrumVolumes, 0, sizeof (editor.spectrumVolumes));
			}

			if (editor.ui.visualizerMode == VISUAL_QUADRASCOPE)
				renderQuadrascopeBg();
			else if (editor.ui.visualizerMode == VISUAL_SPECTRUM)
				renderSpectrumAnalyzerBg();
		}
		break;

		case PTB_QUIT:
		{
			editor.ui.askScreenShown = true;
			editor.ui.askScreenType = ASK_QUIT;
			pointerSetMode(POINTER_MODE_MSG1, NO_CARRY);
			setStatusMessage("REALLY QUIT ?", NO_CARRY);
			renderAskDialog();
		}
		break;

		case PTB_CHAN1:
		{
			if (input.mouse.rightButtonPressed)
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
			if (input.mouse.rightButtonPressed)
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
			if (input.mouse.rightButtonPressed)
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
			if (input.mouse.rightButtonPressed)
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

		case PTB_DO_FILEAREA: diskOpLoadFile((input.mouse.y - 34) / 6, true); break;
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
			editor.ui.diskOpScreenShown = true;
			renderDiskOpScreen();
		}
		break;

		case PTB_DO_EXIT:
		{
			editor.ui.aboutScreenShown = false;
			editor.ui.diskOpScreenShown = false;
			editor.blockMarkFlag = false;
			pointerSetPreviousMode();
			setPrevStatusMessage();
			displayMainScreen();
		}
		break;

		case PTB_DO_SCROLLUP:
		{
			if (editor.diskop.scrollOffset > 0)
			{
				editor.diskop.scrollOffset--;
				editor.ui.updateDiskOpFileList = true;
			}
		}
		break;

		case PTB_DO_SCROLLTOP:
		{
			editor.diskop.scrollOffset = 0;
			editor.ui.updateDiskOpFileList = true;
		}
		break;

		case PTB_DO_SCROLLDOWN:
		{
			if (editor.diskop.numEntries > DISKOP_LINES && editor.diskop.scrollOffset < editor.diskop.numEntries-DISKOP_LINES)
			{
				editor.diskop.scrollOffset++;
				editor.ui.updateDiskOpFileList = true;
			}
		}
		break;

		case PTB_DO_SCROLLBOT:
		{
			if (editor.diskop.numEntries > DISKOP_LINES)
			{
				editor.diskop.scrollOffset = editor.diskop.numEntries - DISKOP_LINES;
				editor.ui.updateDiskOpFileList = true;
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

			if (input.mouse.rightButtonPressed)
				modPlay(DONT_SET_PATTERN, modEntry->currOrder, modEntry->currRow);
			else
				modPlay(DONT_SET_PATTERN, modEntry->currOrder, DONT_SET_ROW);

			editor.currMode = MODE_PLAY;
			pointerSetMode(POINTER_MODE_PLAY, DO_CARRY);
			statusAllRight();
		}
		break;

		case PTB_PATTERN:
		{
			editor.playMode = PLAY_MODE_PATTERN;

			if (input.mouse.rightButtonPressed)
				modPlay(modEntry->currPattern, DONT_SET_ORDER, modEntry->currRow);
			else
				modPlay(modEntry->currPattern, DONT_SET_ORDER, DONT_SET_ROW);

			editor.currMode = MODE_PLAY;
			pointerSetMode(POINTER_MODE_PLAY, DO_CARRY);
			statusAllRight();
		}
		break;

		case PTB_EDIT:
		{
			if (!editor.ui.samplerScreenShown)
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
			if (!editor.ui.samplerScreenShown)
			{
				editor.playMode = PLAY_MODE_PATTERN;

				if (input.mouse.rightButtonPressed)
					modPlay(modEntry->currPattern, DONT_SET_ORDER, modEntry->currRow);
				else
					modPlay(modEntry->currPattern, DONT_SET_ORDER, DONT_SET_ROW);

				editor.currMode = MODE_RECORD;
				pointerSetMode(POINTER_MODE_EDIT, DO_CARRY);
				statusAllRight();
			}
		}
		break;

		case PTB_CLEAR:
		{
			editor.ui.clearScreenShown = true;
			pointerSetMode(POINTER_MODE_MSG1, NO_CARRY);
			setStatusMessage("PLEASE SELECT", NO_CARRY);
			renderClearScreen();
		}
		break;

		case PTB_CLEARSONG:
		{
			editor.ui.clearScreenShown = false;
			removeClearScreen();
			editor.playMode = PLAY_MODE_NORMAL;
			modStop();
			clearSong();
			editor.currMode = MODE_IDLE;
			pointerSetMode(POINTER_MODE_IDLE, DO_CARRY);
			statusAllRight();
		}
		break;

		case PTB_CLEARSAMPLES:
		{
			editor.ui.clearScreenShown = false;
			removeClearScreen();
			editor.playMode = PLAY_MODE_NORMAL;
			modStop();
			clearSamples();
			editor.currMode = MODE_IDLE;
			pointerSetMode(POINTER_MODE_IDLE, DO_CARRY);
			statusAllRight();
		}
		break;

		case PTB_CLEARALL:
		{
			editor.ui.clearScreenShown = false;
			removeClearScreen();
			editor.playMode = PLAY_MODE_NORMAL;
			modStop();
			clearAll();
			editor.currMode = MODE_IDLE;
			pointerSetMode(POINTER_MODE_IDLE, DO_CARRY);
			statusAllRight();
		}
		break;

		case PTB_CLEARCANCEL:
		{
			editor.ui.clearScreenShown = false;
			removeClearScreen();
			setPrevStatusMessage();
			pointerSetPreviousMode();
			editor.errorMsgActive = true;
			editor.errorMsgBlock = true;
			editor.errorMsgCounter = 0;
			pointerErrorMode();
		}
		break;

		case PTB_SAMPLEU:
		{
			if (input.mouse.rightButtonPressed)
			{
				editor.sampleZero = true;
				editor.ui.updateCurrSampleNum = true;
			}
			else
			{
				sampleUpButton();
			}
		}
		break;

		case PTB_SAMPLED:
		{
			if (input.mouse.rightButtonPressed)
			{
				editor.sampleZero = true;
				editor.ui.updateCurrSampleNum = true;
			}
			else
			{
				sampleDownButton();
			}
		}
		break;

		case PTB_FTUNEU:
		{
			if ((modEntry->samples[editor.currSample].fineTune & 0x0F) != 7)
			{
				sampleFineTuneUpButton();
				updateWindowTitle(MOD_IS_MODIFIED);
			}
		}
		break;

		case PTB_FTUNED:
		{
			if ((modEntry->samples[editor.currSample].fineTune & 0x0F) != 8)
			{
				sampleFineTuneDownButton();
				updateWindowTitle(MOD_IS_MODIFIED);

			}
		}
		break;

		case PTB_SVOLUMEU:
		{
			if (modEntry->samples[editor.currSample].volume < 64)
			{
				sampleVolumeUpButton();
				updateWindowTitle(MOD_IS_MODIFIED);
			}
		}
		break;

		case PTB_SVOLUMED:
		{
			if (modEntry->samples[editor.currSample].volume > 0)
			{
				sampleVolumeDownButton();
				updateWindowTitle(MOD_IS_MODIFIED);
			}
		}
		break;

		case PTB_SLENGTHU:
		{
			if (modEntry->samples[editor.currSample].length < MAX_SAMPLE_LEN)
			{
				sampleLengthUpButton(INCREMENT_SLOW);
				updateWindowTitle(MOD_IS_MODIFIED);
			}
		}
		break;

		case PTB_SLENGTHD:
		{
			if (modEntry->samples[editor.currSample].length > 0)
			{
				sampleLengthDownButton(INCREMENT_SLOW);
				updateWindowTitle(MOD_IS_MODIFIED);
			}
		}
		break;

		case PTB_SREPEATU:
		{
			oldVal = modEntry->samples[editor.currSample].loopStart;
			sampleRepeatUpButton(INCREMENT_SLOW);
			if (modEntry->samples[editor.currSample].loopStart != oldVal)
				updateWindowTitle(MOD_IS_MODIFIED);
		}
		break;

		case PTB_SREPEATD:
		{
			oldVal = modEntry->samples[editor.currSample].loopStart;
			sampleRepeatDownButton(INCREMENT_SLOW);
			if (modEntry->samples[editor.currSample].loopStart != oldVal)
				updateWindowTitle(MOD_IS_MODIFIED);
		}
		break;

		case PTB_SREPLENU:
		{
			oldVal = modEntry->samples[editor.currSample].loopLength;
			sampleRepeatLengthUpButton(INCREMENT_SLOW);
			if (modEntry->samples[editor.currSample].loopLength != oldVal)
				updateWindowTitle(MOD_IS_MODIFIED);
		}
		break;

		case PTB_SREPLEND:
		{
			oldVal = modEntry->samples[editor.currSample].loopLength;
			sampleRepeatLengthDownButton(INCREMENT_SLOW);
			if (modEntry->samples[editor.currSample].loopLength != oldVal)
				updateWindowTitle(MOD_IS_MODIFIED);
		}
		break;

		case PTB_TEMPOU: tempoUpButton(); break;
		case PTB_TEMPOD: tempoDownButton(); break;

		case PTB_LENGTHU:
		{
			if (modEntry->head.orderCount < 128)
			{
				songLengthUpButton();
				updateWindowTitle(MOD_IS_MODIFIED);
			}
		}
		break;

		case PTB_LENGTHD:
		{
			if (modEntry->head.orderCount > 1)
			{
				songLengthDownButton();
				updateWindowTitle(MOD_IS_MODIFIED);
			}
		}
		break;

		case PTB_PATTERNU:
		{
			if (modEntry->head.order[modEntry->currOrder] < 99)
			{
				patternUpButton();
				updateWindowTitle(MOD_IS_MODIFIED);
			}
		}
		break;

		case PTB_PATTERND:
		{
			if (modEntry->head.order[modEntry->currOrder] > 0)
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

	input.mouse.lastGUIButton = button;
	return false;
}

static void handleRepeatedGUIButtons(void)
{
	// repeat button
	switch (input.mouse.lastGUIButton)
	{
		case PTB_EO_NOTE1_UP:
		{
			if (input.mouse.repeatCounter >= 4)
			{
				input.mouse.repeatCounter = 0;
				edNote1UpButton();
			}
		}
		break;

		case PTB_EO_NOTE1_DOWN:
		{
			if (input.mouse.repeatCounter >= 4)
			{
				input.mouse.repeatCounter = 0;
				edNote1DownButton();
			}
		}
		break;

		case PTB_EO_NOTE2_UP:
		{
			if (input.mouse.repeatCounter >= 4)
			{
				input.mouse.repeatCounter = 0;
				edNote2UpButton();
			}
		}
		break;

		case PTB_EO_NOTE2_DOWN:
		{
			if (input.mouse.repeatCounter >= 4)
			{
				input.mouse.repeatCounter = 0;
				edNote2DownButton();
			}
		}
		break;

		case PTB_EO_NOTE3_UP:
		{
			if (input.mouse.repeatCounter >= 4)
			{
				input.mouse.repeatCounter = 0;
				edNote3UpButton();
			}
		}
		break;

		case PTB_EO_NOTE3_DOWN:
		{
			if (input.mouse.repeatCounter >= 4)
			{
				input.mouse.repeatCounter = 0;
				edNote3DownButton();
			}
		}
		break;

		case PTB_EO_NOTE4_UP:
		{
			if (input.mouse.repeatCounter >= 4)
			{
				input.mouse.repeatCounter = 0;
				edNote4UpButton();
			}
		}
		break;

		case PTB_EO_NOTE4_DOWN:
		{
			if (input.mouse.repeatCounter >= 4)
			{
				input.mouse.repeatCounter = 0;
				edNote4DownButton();
			}
		}
		break;

		case PTB_EO_VOL_UP:
		{
			if (input.mouse.repeatCounter >= 2)
			{
				input.mouse.repeatCounter = 0;
				edVolUpButton();
			}
		}
		break;

		case PTB_EO_VOL_DOWN:
		{
			if (input.mouse.repeatCounter >= 2)
			{
				input.mouse.repeatCounter = 0;
				edVolDownButton();
			}
		}
		break;

		case PTB_EO_MOD_UP:
		{
			if (input.mouse.repeatCounter >= 2)
			{
				input.mouse.repeatCounter = 0;
				edModUpButton();
			}
		}
		break;

		case PTB_EO_MOD_DOWN:
		{
			if (input.mouse.repeatCounter >= 2)
			{
				input.mouse.repeatCounter = 0;
				edModDownButton();
			}
		}
		break;

		case PTB_EO_POS_UP:
		{
			if (input.mouse.repeatCounter >= 1)
			{
				input.mouse.repeatCounter = 0;
				edPosUpButton(INCREMENT_FAST);
			}
		}
		break;

		case PTB_EO_POS_DOWN:
		{
			if (input.mouse.repeatCounter >= 1)
			{
				input.mouse.repeatCounter = 0;
				edPosDownButton(INCREMENT_FAST);
			}
		}
		break;

		case PTB_EO_FROM_UP:
		{
			if (input.mouse.repeatCounter >= 2)
			{
				input.mouse.repeatCounter = 0;
				if (editor.sampleFrom < 0x1F)
				{
					editor.sampleFrom++;
					editor.ui.updateFromText = true;
				}
			}
		}
		break;

		case PTB_EO_FROM_DOWN:
		{
			if (input.mouse.repeatCounter >= 2)
			{
				input.mouse.repeatCounter = 0;
				if (editor.sampleFrom > 0x00)
				{
					editor.sampleFrom--;
					editor.ui.updateFromText = true;
				}
			}
		}
		break;

		case PTB_EO_TO_UP:
		{
			if (input.mouse.repeatCounter >= 2)
			{
				input.mouse.repeatCounter = 0;
				if (editor.sampleTo < 0x1F)
				{
					editor.sampleTo++;
					editor.ui.updateToText = true;
				}
			}
		}
		break;

		case PTB_EO_TO_DOWN:
		{
			if (input.mouse.repeatCounter >= 2)
			{
				input.mouse.repeatCounter = 0;
				if (editor.sampleTo > 0x00)
				{
					editor.sampleTo--;
					editor.ui.updateToText = true;
				}
			}
		}
		break;

		case PTB_SAMPLEU:
		{
			if (input.mouse.repeatCounter >= 5)
			{
				input.mouse.repeatCounter = 0;
				if (!input.mouse.rightButtonPressed)
					sampleUpButton();
				else
					editor.ui.updateCurrSampleNum = true;
			}
		}
		break;

		case PTB_SAMPLED:
		{
			if (input.mouse.repeatCounter >= 5)
			{
				input.mouse.repeatCounter = 0;
				if (!input.mouse.rightButtonPressed)
					sampleDownButton();
				else
					editor.ui.updateCurrSampleNum = true;
			}
		}
		break;

		case PTB_FTUNEU:
		{
			if (input.mouse.repeatCounter >= 5)
			{
				input.mouse.repeatCounter = 0;
				sampleFineTuneUpButton();
			}
		}
		break;

		case PTB_FTUNED:
		{
			if (input.mouse.repeatCounter >= 5)
			{
				input.mouse.repeatCounter = 0;
				sampleFineTuneDownButton();
			}
		}
		break;

		case PTB_SVOLUMEU:
		{
			if (input.mouse.repeatCounter >= 5)
			{
				input.mouse.repeatCounter = 0;
				sampleVolumeUpButton();
			}
		}
		break;

		case PTB_SVOLUMED:
		{
			if (input.mouse.repeatCounter >= 5)
			{
				input.mouse.repeatCounter = 0;
				sampleVolumeDownButton();
			}
		}
		break;

		case PTB_SLENGTHU:
		{
			if (input.mouse.rightButtonPressed || input.mouse.repeatCounter >= 1)
			{
				input.mouse.repeatCounter = 0;
				sampleLengthUpButton(INCREMENT_FAST);
			}
		}
		break;

		case PTB_SLENGTHD:
		{
			if (input.mouse.rightButtonPressed || input.mouse.repeatCounter >= 1)
			{
				input.mouse.repeatCounter = 0;
				sampleLengthDownButton(INCREMENT_FAST);
			}
		}
		break;

		case PTB_SREPEATU:
		{
			if (input.mouse.rightButtonPressed || input.mouse.repeatCounter >= 1)
			{
				input.mouse.repeatCounter = 0;
				sampleRepeatUpButton(INCREMENT_FAST);
			}
		}
		break;

		case PTB_SREPEATD:
		{
			if (input.mouse.rightButtonPressed || input.mouse.repeatCounter >= 1)
			{
				input.mouse.repeatCounter = 0;
				sampleRepeatDownButton(INCREMENT_FAST);
			}
		}
		break;

		case PTB_SREPLENU:
		{
			if (input.mouse.rightButtonPressed || input.mouse.repeatCounter >= 1)
			{
				input.mouse.repeatCounter = 0;
				sampleRepeatLengthUpButton(INCREMENT_FAST);
			}
		}
		break;

		case PTB_SREPLEND:
		{
			if (input.mouse.rightButtonPressed || input.mouse.repeatCounter >= 1)
			{
				input.mouse.repeatCounter = 0;
				sampleRepeatLengthDownButton(INCREMENT_FAST);
			}
		}
		break;

		case PTB_TEMPOU:
		{
			if (input.mouse.repeatCounter >= 3)
			{
				input.mouse.repeatCounter = 0;
				tempoUpButton();
			}
		}
		break;

		case PTB_TEMPOD:
		{
			if (input.mouse.repeatCounter >= 3)
			{
				input.mouse.repeatCounter = 0;
				tempoDownButton();
			}
		}
		break;

		case PTB_LENGTHU:
		{
			if (input.mouse.repeatCounter >= 7)
			{
				input.mouse.repeatCounter = 0;
				songLengthUpButton();
			}
		}
		break;

		case PTB_LENGTHD:
		{
			if (input.mouse.repeatCounter >= 7)
			{
				input.mouse.repeatCounter = 0;
				songLengthDownButton();
			}
		}
		break;

		case PTB_PATTERNU:
		{
			if (input.mouse.repeatCounter >= 7)
			{
				input.mouse.repeatCounter = 0;
				patternUpButton();
			}
		}
		break;

		case PTB_PATTERND:
		{
			if (input.mouse.repeatCounter >= 7)
			{
				input.mouse.repeatCounter = 0;
				patternDownButton();
			}
		}
		break;

		case PTB_POSU:
		{
			if (input.mouse.repeatCounter >= 7)
			{
				input.mouse.repeatCounter = 0;
				positionUpButton();
			}
		}
		break;

		case PTB_POSD:
		{
			if (input.mouse.repeatCounter >= 7)
			{
				input.mouse.repeatCounter = 0;
				positionDownButton();
			}
		}
		break;

		case PTB_PE_SCROLLUP:
		{
			if (input.mouse.repeatCounter >= 2)
			{
				input.mouse.repeatCounter = 0;
				if (modEntry->currOrder > 0)
					modSetPos(modEntry->currOrder - 1, DONT_SET_ROW);
			}
		}
		break;

		case PTB_PE_SCROLLDOWN:
		{
			if (input.mouse.repeatCounter >= 2)
			{
				input.mouse.repeatCounter = 0;
				if (modEntry->currOrder < modEntry->head.orderCount-1)
					modSetPos(modEntry->currOrder + 1, DONT_SET_ROW);
			}
		}
		break;

		case PTB_DO_SCROLLUP:
		{
			if (input.mouse.repeatCounter >= 1)
			{
				input.mouse.repeatCounter = 0;

				editor.diskop.scrollOffset--;
				if (input.mouse.rightButtonPressed)
					editor.diskop.scrollOffset -= 3;

				if (editor.diskop.scrollOffset < 0)
					editor.diskop.scrollOffset = 0;

				editor.ui.updateDiskOpFileList = true;
			}
		}
		break;

		case PTB_DO_SCROLLDOWN:
		{
			if (input.mouse.repeatCounter >= 1)
			{
				input.mouse.repeatCounter = 0;

				if (editor.diskop.numEntries > DISKOP_LINES)
				{
					editor.diskop.scrollOffset++;
					if (input.mouse.rightButtonPressed)
						editor.diskop.scrollOffset += 3;

					if (editor.diskop.scrollOffset > editor.diskop.numEntries-DISKOP_LINES)
						editor.diskop.scrollOffset = editor.diskop.numEntries-DISKOP_LINES;

					editor.ui.updateDiskOpFileList = true;
				}
			}
		}
		break;

		case PTB_SA_ZOOMBARAREA:
		{
			if (input.mouse.repeatCounter >= 4)
			{
				input.mouse.repeatCounter = 0;
				if (!editor.ui.forceSampleDrag)
					samplerBarPressed(MOUSE_BUTTON_NOT_HELD);
			}
		}
		break;

		default: break;
	}
}
