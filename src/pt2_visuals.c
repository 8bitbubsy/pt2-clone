// for finding memory leaks in debug mode with Visual Studio 
#if defined _DEBUG && defined _MSC_VER
#include <crtdbg.h>
#endif

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <math.h> // modf()
#ifdef _WIN32
#define WIN32_MEAN_AND_LEAN
#include <windows.h>
#include <SDL2/SDL_syswm.h>
#endif
#include <stdint.h>
#include <stdbool.h>
#ifndef _WIN32
#include <unistd.h> // usleep()
#endif
#include <ctype.h> // tolower()
#include "pt2_header.h"
#include "pt2_keyboard.h"
#include "pt2_mouse.h"
#include "pt2_audio.h"
#include "pt2_palette.h"
#include "pt2_helpers.h"
#include "pt2_textout.h"
#include "pt2_tables.h"
#include "pt2_modloader.h"
#include "pt2_sampleloader.h"
#include "pt2_patternviewer.h"
#include "pt2_sampler.h"
#include "pt2_diskop.h"
#include "pt2_visuals.h"
#include "pt2_helpers.h"
#include "pt2_scopes.h"
#include "pt2_edit.h"

typedef struct sprite_t
{
	bool visible;
	int8_t pixelType;
	uint16_t newX, newY, x, y, w, h;
	uint32_t colorKey, *refreshBuffer;
	const void *data;
} sprite_t;

static uint32_t vuMetersBg[4 * (10 * 48)];
static uint64_t timeNext64, timeNext64Frac, _50HzCounter;

sprite_t sprites[SPRITE_NUM]; // globalized

extern bool forceMixerOff; // pt_audio.c

// pt_main.c
extern uint32_t *pixelBuffer;
extern SDL_Window *window;
extern SDL_Renderer *renderer;
extern SDL_Texture *texture;

static const uint16_t cursorPosTable[24] =
{
	 30,  54,  62,  70,  78,  86,
	102, 126, 134, 142, 150, 158,
	174, 198, 206, 214, 222, 230,
	246, 270, 278, 286, 294, 302
};

bool intMusic(void); // pt_modplayer.c
extern int32_t samplesPerTick; // pt_audio.c
void storeTempVariables(void); // pt_modplayer.c
void updateSongInfo1(void);
void updateSongInfo2(void);
void updateSampler(void);
void updatePatternData(void);
void updateMOD2WAVDialog(void);

void statusAllRight(void)
{
	setStatusMessage("ALL RIGHT", DO_CARRY);
}

void statusOutOfMemory(void)
{
	displayErrorMsg("OUT OF MEMORY !!!");
}

void setupPerfFreq(void)
{
	uint64_t perfFreq64;
	double dInt, dFrac;

	perfFreq64 = SDL_GetPerformanceFrequency(); assert(perfFreq64 != 0);
	editor.dPerfFreq = (double)perfFreq64;
	editor.dPerfFreqMulMicro = 1000000.0 / editor.dPerfFreq;

	// calculate vblank time for performance counters and split into int/frac
	dFrac = modf(editor.dPerfFreq / VBLANK_HZ, &dInt);

	// integer part
	editor.vblankTimeLen = (uint32_t)dInt;

	// fractional part scaled to 0..2^32-1

	dFrac *= UINT32_MAX + 1.0;
	if (dFrac > (double)UINT32_MAX)
		dFrac = (double)UINT32_MAX;

	editor.vblankTimeLenFrac = (uint32_t)round(dFrac);
}

void setupWaitVBL(void)
{
	// set next frame time
	timeNext64 = SDL_GetPerformanceCounter() + editor.vblankTimeLen;
	timeNext64Frac = editor.vblankTimeLenFrac;
}

void waitVBL(void)
{
	// this routine almost never delays if we have 60Hz vsync, but it's still needed in some occasions

	int32_t time32;
	uint32_t diff32;
	uint64_t time64;

	time64 = SDL_GetPerformanceCounter();
	if (time64 < timeNext64)
	{
		assert(timeNext64-time64 <= 0xFFFFFFFFULL);
		diff32 = (uint32_t)(timeNext64 - time64);

		// convert to microseconds and round to integer
		time32 = (uint32_t)((diff32 * editor.dPerfFreqMulMicro) + 0.5);

		// delay until we have reached next tick
		if (time32 > 0)
			usleep(time32);
	}

	// update next tick time
	timeNext64 += editor.vblankTimeLen;

	timeNext64Frac += editor.vblankTimeLenFrac;
	if (timeNext64Frac >= (1ULL << 32))
	{
		timeNext64Frac &= 0xFFFFFFFF;
		timeNext64++;
	}
}

void renderFrame(void)
{
	updateMOD2WAVDialog(); // must be first to avoid flickering issues

	updateSongInfo1(); // top left side of screen, when "disk op"/"pos ed" is hidden
	updateSongInfo2(); // two middle rows of screen, always visible
	updateEditOp();
	updatePatternData();
	updateDiskOp();
	updateSampler();
	updatePosEd();
	updateVisualizer();
	updateDragBars();
	drawSamplerLine();
}

void resetAllScreens(void)
{
	editor.mixFlag = false;
	editor.swapChannelFlag = false;
	editor.ui.clearScreenShown = false;
	editor.ui.changingChordNote = false;
	editor.ui.changingSmpResample = false;
	editor.ui.pat2SmpDialogShown = false;
	editor.ui.disablePosEd = false;
	editor.ui.disableVisualizer = false;

	if (editor.ui.samplerScreenShown)
	{
		editor.ui.samplerVolBoxShown = false;
		editor.ui.samplerFiltersBoxShown = false;

		displaySample();
	}

	if (editor.ui.editTextFlag)
		exitGetTextLine(EDIT_TEXT_NO_UPDATE);
}

void removeAskDialog(void)
{
	if (!editor.ui.askScreenShown && !editor.isWAVRendering)
		displayMainScreen();

	editor.ui.disablePosEd = false;
	editor.ui.disableVisualizer = false;
}

void renderAskDialog(void)
{
	const uint32_t *srcPtr;
	uint32_t *dstPtr;

	editor.ui.disablePosEd = true;
	editor.ui.disableVisualizer = true;

	// render ask dialog

	srcPtr = editor.ui.pat2SmpDialogShown ? pat2SmpDialogBMP : yesNoDialogBMP;
	dstPtr = &pixelBuffer[(51 * SCREEN_W) + 160];

	for (uint32_t y = 0; y < 39; y++)
	{
		memcpy(dstPtr, srcPtr, 104 * sizeof (int32_t));

		srcPtr += 104;
		dstPtr += SCREEN_W;
	}
}

static void fillFromVuMetersBgBuffer(void)
{
	const uint32_t *srcPtr;
	uint32_t *dstPtr;

	if (editor.ui.samplerScreenShown || editor.isWAVRendering || editor.isSMPRendering)
		return;

	srcPtr = vuMetersBg;
	dstPtr = &pixelBuffer[(187 * SCREEN_W) + 55];

	for (uint32_t i = 0; i < AMIGA_VOICES; i++)
	{
		for (uint32_t y = 0; y < 48; y++)
		{
			for (uint32_t x = 0; x < 10; x++)
				dstPtr[x] = srcPtr[x];

			srcPtr += 10;
			dstPtr -= SCREEN_W;
		}

		dstPtr += (SCREEN_W * 48) + 72;
	}
}

void fillToVuMetersBgBuffer(void)
{
	const uint32_t *srcPtr;
	uint32_t *dstPtr;

	if (editor.ui.samplerScreenShown || editor.isWAVRendering || editor.isSMPRendering)
		return;

	srcPtr = &pixelBuffer[(187 * SCREEN_W) + 55];
	dstPtr = vuMetersBg;

	for (uint32_t i = 0; i < AMIGA_VOICES; i++)
	{
		for (uint32_t y = 0; y < 48; y++)
		{
			for (uint32_t x = 0; x < 10; x++)
				dstPtr[x] = srcPtr[x];

			srcPtr -= SCREEN_W;
			dstPtr += 10;
		}

		srcPtr += (SCREEN_W * 48) + 72;
	}
}

void renderVuMeters(void)
{
	const uint32_t *srcPtr;
	uint32_t h, *dstPtr;

	if (editor.ui.samplerScreenShown || editor.isWAVRendering || editor.isSMPRendering)
		return;

	fillToVuMetersBgBuffer();
	
	dstPtr = &pixelBuffer[(187 * SCREEN_W) + 55];
	for (uint32_t i = 0; i < AMIGA_VOICES; i++)
	{
		if (ptConfig.realVuMeters)
			h = editor.realVuMeterVolumes[i];
		else
			h = editor.vuMeterVolumes[i];

		if (h > 48)
			h = 48;

		srcPtr = vuMeterBMP;
		for (uint32_t y = 0; y < h; y++)
		{
			for (uint32_t x = 0; x < 10; x++)
				dstPtr[x] = srcPtr[x];

			srcPtr += 10;
			dstPtr -= SCREEN_W;
		}

		dstPtr += (SCREEN_W * h) + 72;
	}
}

void updateSongInfo1(void) // left side of screen, when Disk Op. is hidden
{
	moduleSample_t *currSample;

	if (editor.ui.diskOpScreenShown)
		return;

	currSample = &modEntry->samples[editor.currSample];

	if (editor.ui.updateSongPos)
	{
		editor.ui.updateSongPos = false;
		printThreeDecimalsBg(pixelBuffer, 72, 3, *editor.currPosDisp, palette[PAL_GENTXT], palette[PAL_GENBKG]);
	}

	if (editor.ui.updateSongPattern)
	{
		editor.ui.updateSongPattern = false;
		printTwoDecimalsBg(pixelBuffer, 80, 14, *editor.currPatternDisp, palette[PAL_GENTXT], palette[PAL_GENBKG]);
	}

	if (editor.ui.updateSongLength)
	{
		editor.ui.updateSongLength = false;
		if (!editor.isWAVRendering)
			printThreeDecimalsBg(pixelBuffer, 72, 25, *editor.currLengthDisp,palette[PAL_GENTXT], palette[PAL_GENBKG]);
	}

	if (editor.ui.updateCurrSampleFineTune)
	{
		editor.ui.updateCurrSampleFineTune = false;

		if (!editor.isWAVRendering)
		{
			if (currSample->fineTune >= 8)
			{
				charOutBg(pixelBuffer, 80, 36, '-', palette[PAL_GENTXT], palette[PAL_GENBKG]);
				charOutBg(pixelBuffer, 88, 36, '0' + (0x10 - (currSample->fineTune & 0xF)), palette[PAL_GENTXT], palette[PAL_GENBKG]);
			}
			else if (currSample->fineTune > 0)
			{
				charOutBg(pixelBuffer, 80, 36, '+', palette[PAL_GENTXT], palette[PAL_GENBKG]);
				charOutBg(pixelBuffer, 88, 36, '0' + (currSample->fineTune & 0xF), palette[PAL_GENTXT], palette[PAL_GENBKG]);
			}
			else
			{
				charOutBg(pixelBuffer, 80, 36, ' ', palette[PAL_GENBKG], palette[PAL_GENBKG]);
				charOutBg(pixelBuffer, 88, 36, '0', palette[PAL_GENTXT], palette[PAL_GENBKG]);
			}
		}
	}

	if (editor.ui.updateCurrSampleNum)
	{
		editor.ui.updateCurrSampleNum = false;
		if (!editor.isWAVRendering)
		{
			printTwoHexBg(pixelBuffer, 80, 47,
				editor.sampleZero ? 0 : ((*editor.currSampleDisp) + 1), palette[PAL_GENTXT], palette[PAL_GENBKG]);
		}
	}

	if (editor.ui.updateCurrSampleVolume)
	{
		editor.ui.updateCurrSampleVolume = false;
		if (!editor.isWAVRendering)
			printTwoHexBg(pixelBuffer, 80, 58, *currSample->volumeDisp, palette[PAL_GENTXT], palette[PAL_GENBKG]);
	}

	if (editor.ui.updateCurrSampleLength)
	{
		editor.ui.updateCurrSampleLength = false;
		if (!editor.isWAVRendering)
			printFourHexBg(pixelBuffer, 64, 69, *currSample->lengthDisp, palette[PAL_GENTXT], palette[PAL_GENBKG]);
	}

	if (editor.ui.updateCurrSampleRepeat)
	{
		editor.ui.updateCurrSampleRepeat = false;
		printFourHexBg(pixelBuffer, 64, 80, *currSample->loopStartDisp, palette[PAL_GENTXT], palette[PAL_GENBKG]);
	}

	if (editor.ui.updateCurrSampleReplen)
	{
		editor.ui.updateCurrSampleReplen = false;
		printFourHexBg(pixelBuffer, 64, 91, *currSample->loopLengthDisp, palette[PAL_GENTXT], palette[PAL_GENBKG]);
	}
}

void updateSongInfo2(void) // two middle rows of screen, always present
{
	char tempChar;
	int32_t secs, MI_TimeM, MI_TimeS, x, i;
	moduleSample_t *currSample;

	if (editor.ui.updateStatusText)
	{
		editor.ui.updateStatusText = false;

		// clear background
		textOutBg(pixelBuffer, 88, 127, "                 ", palette[PAL_GENBKG], palette[PAL_GENBKG]);

		// render status text
		if (!editor.errorMsgActive && editor.blockMarkFlag && !editor.ui.askScreenShown
			&& !editor.ui.clearScreenShown && !editor.swapChannelFlag)
		{
			textOut(pixelBuffer, 88, 127, "MARK BLOCK", palette[PAL_GENTXT]);
			charOut(pixelBuffer, 192, 127, '-', palette[PAL_GENTXT]);

			editor.blockToPos = modEntry->currRow;
			if (editor.blockFromPos >= editor.blockToPos)
			{
				printTwoDecimals(pixelBuffer, 176, 127, editor.blockToPos, palette[PAL_GENTXT]);
				printTwoDecimals(pixelBuffer, 200, 127, editor.blockFromPos, palette[PAL_GENTXT]);
			}
			else
			{
				printTwoDecimals(pixelBuffer, 176, 127, editor.blockFromPos, palette[PAL_GENTXT]);
				printTwoDecimals(pixelBuffer, 200, 127, editor.blockToPos, palette[PAL_GENTXT]);
			}
		}
		else
		{
			textOut(pixelBuffer, 88, 127, editor.ui.statusMessage, palette[PAL_GENTXT]);
		}
	}

	if (editor.ui.updateSongBPM)
	{
		editor.ui.updateSongBPM = false;
		if (!editor.ui.samplerScreenShown)
			printThreeDecimalsBg(pixelBuffer, 32, 123, modEntry->currBPM, palette[PAL_GENTXT], palette[PAL_GENBKG]);
	}

	if (editor.ui.updateCurrPattText)
	{
		editor.ui.updateCurrPattText = false;
		if (!editor.ui.samplerScreenShown)
			printTwoDecimalsBg(pixelBuffer, 8, 127, *editor.currEditPatternDisp, palette[PAL_GENTXT], palette[PAL_GENBKG]);
	}

	if (editor.ui.updateTrackerFlags)
	{
		editor.ui.updateTrackerFlags = false;

		charOutBg(pixelBuffer, 1, 113, ' ', palette[PAL_GENTXT], palette[PAL_GENBKG]);
		charOutBg(pixelBuffer, 8, 113, ' ', palette[PAL_GENTXT], palette[PAL_GENBKG]);

		if (editor.autoInsFlag)
		{
			charOut(pixelBuffer, 0, 113, 'I', palette[PAL_GENTXT]);

			// in Amiga PT, "auto insert" 9 means 0
			if (editor.autoInsSlot == 9)
				charOut(pixelBuffer, 8, 113, '0', palette[PAL_GENTXT]);
			else
				charOut(pixelBuffer, 8, 113, '1' + editor.autoInsSlot, palette[PAL_GENTXT]);
		}

		charOutBg(pixelBuffer, 1, 102, ' ', palette[PAL_GENTXT], palette[PAL_GENBKG]);
		if (editor.metroFlag)
			charOut(pixelBuffer, 0, 102, 'M', palette[PAL_GENTXT]);

		charOutBg(pixelBuffer, 16, 102, ' ', palette[PAL_GENTXT], palette[PAL_GENBKG]);
		if (editor.multiFlag)
			charOut(pixelBuffer, 16, 102, 'M', palette[PAL_GENTXT]);

		charOutBg(pixelBuffer, 24, 102, '0' + editor.editMoveAdd,palette[PAL_GENTXT], palette[PAL_GENBKG]);

		charOutBg(pixelBuffer, 311, 128, ' ', palette[PAL_GENBKG], palette[PAL_GENBKG]);
		if (editor.pNoteFlag == 1)
		{
			pixelBuffer[(129 * SCREEN_W) + 314] = palette[PAL_GENTXT];
			pixelBuffer[(129 * SCREEN_W) + 315] = palette[PAL_GENTXT];
		}
		else if (editor.pNoteFlag == 2)
		{
			pixelBuffer[(128 * SCREEN_W) + 314] = palette[PAL_GENTXT];
			pixelBuffer[(128 * SCREEN_W) + 315] = palette[PAL_GENTXT];
			pixelBuffer[(130 * SCREEN_W) + 314] = palette[PAL_GENTXT];
			pixelBuffer[(130 * SCREEN_W) + 315] = palette[PAL_GENTXT];
		}
	}

	// playback timer

	secs = ((editor.musicTime / 256) * 5) / 512;
	secs -= ((secs / 3600) * 3600);

	if (secs <= 5999) // below 99 minutes 59 seconds
	{
		MI_TimeM = secs / 60;
		MI_TimeS = secs - (MI_TimeM * 60);

		// xx:xx
		printTwoDecimalsBg(pixelBuffer, 272, 102, MI_TimeM, palette[PAL_GENTXT], palette[PAL_GENBKG]);
		printTwoDecimalsBg(pixelBuffer, 296, 102, MI_TimeS, palette[PAL_GENTXT], palette[PAL_GENBKG]);
	}
	else
	{
		// 99:59
		printTwoDecimalsBg(pixelBuffer, 272, 102, 99, palette[PAL_GENTXT], palette[PAL_GENBKG]);
		printTwoDecimalsBg(pixelBuffer, 296, 102, 59, palette[PAL_GENTXT], palette[PAL_GENBKG]);
	}

	if (editor.ui.updateSongName)
	{
		editor.ui.updateSongName = false;
		for (x = 0; x < 20; x++)
		{
			tempChar = modEntry->head.moduleTitle[x];
			if (tempChar == '\0')
				tempChar = '_';

			charOutBg(pixelBuffer, 104 + (x * FONT_CHAR_W), 102, tempChar, palette[PAL_GENTXT], palette[PAL_GENBKG]);
		}
	}

	if (editor.ui.updateCurrSampleName)
	{
		editor.ui.updateCurrSampleName = false;
		currSample = &modEntry->samples[editor.currSample];

		for (x = 0; x < 22; x++)
		{
			tempChar = currSample->text[x];
			if (tempChar == '\0')
				tempChar = '_';

			charOutBg(pixelBuffer, 104 + (x * FONT_CHAR_W), 113, tempChar, palette[PAL_GENTXT], palette[PAL_GENBKG]);
		}
	}

	if (editor.ui.updateSongSize)
	{
		editor.ui.updateSongSize = false;

		// clear background
		textOutBg(pixelBuffer, 264, 123, "      ", palette[PAL_GENBKG], palette[PAL_GENBKG]);

		// calculate module length
		modEntry->head.totalSampleSize = 0;
		for (i = 0; i < MOD_SAMPLES; i++)
			modEntry->head.totalSampleSize += modEntry->samples[i].length;

		modEntry->head.patternCount = 0;
		for (i = 0; i < MOD_ORDERS; i++)
		{
			if (modEntry->head.order[i] > modEntry->head.patternCount)
				modEntry->head.patternCount = modEntry->head.order[i];
		}

		modEntry->head.moduleSize = 2108 + modEntry->head.totalSampleSize + (modEntry->head.patternCount * 1024);

		if (modEntry->head.moduleSize > 999999)
		{
			charOut(pixelBuffer, 304, 123, 'K', palette[PAL_GENTXT]);
			printFourDecimals(pixelBuffer, 272, 123, modEntry->head.moduleSize / 1000, palette[PAL_GENTXT]);
		}
		else
		{
			printSixDecimals(pixelBuffer, 264, 123, modEntry->head.moduleSize, palette[PAL_GENTXT]);
		}
	}

	if (editor.ui.updateSongTiming)
	{
		editor.ui.updateSongTiming = false;
		textOutBg(pixelBuffer, 288, 130, (editor.timingMode == TEMPO_MODE_CIA) ? "CIA" : "VBL", palette[PAL_GENTXT], palette[PAL_GENBKG]);
	}
}

void updateCursorPos(void)
{
	if (!editor.ui.samplerScreenShown)
		setSpritePos(SPRITE_PATTERN_CURSOR, cursorPosTable[editor.cursor.pos], 188);
}

void updateSampler(void)
{
	int32_t tmpSampleOffset;
	moduleSample_t *s;

	if (!editor.ui.samplerScreenShown)
		return;

	assert(editor.currSample >= 0 && editor.currSample <= 30);
	s = &modEntry->samples[editor.currSample];

	// update 9xx offset
	if (input.mouse.y >= 138 && input.mouse.y <= 201 && input.mouse.x >= 3 && input.mouse.x <= 316)
	{
		if (!editor.ui.samplerVolBoxShown && !editor.ui.samplerFiltersBoxShown && s->length > 0)
		{
			tmpSampleOffset = (scr2SmpPos(input.mouse.x - 3) + 128) >> 8;
			tmpSampleOffset = 0x900 + CLAMP(tmpSampleOffset, 0x00, 0xFF);

			if (tmpSampleOffset != editor.ui.lastSampleOffset)
			{
				editor.ui.lastSampleOffset = tmpSampleOffset;
				editor.ui.update9xxPos = true;
			}
		}
	}

	// display 9xx offset
	if (editor.ui.update9xxPos)
	{
		editor.ui.update9xxPos = false;
		printThreeHexBg(pixelBuffer, 288, 247, editor.ui.lastSampleOffset, palette[PAL_GENTXT], palette[PAL_GENBKG]);
	}

	if (editor.ui.updateResampleNote)
	{
		editor.ui.updateResampleNote = false;

		// show resample note
		if (editor.ui.changingSmpResample)
		{
			textOutBg(pixelBuffer, 288, 236, "---", palette[PAL_GENTXT], palette[PAL_GENBKG]);
		}
		else
		{
			assert(editor.resampleNote < 36);
			textOutBg(pixelBuffer, 288, 236,
				ptConfig.accidental ? noteNames2[editor.resampleNote] : noteNames1[editor.resampleNote],
				palette[PAL_GENTXT], palette[PAL_GENBKG]);
		}
	}

	if (editor.ui.samplerVolBoxShown)
	{
		if (editor.ui.updateVolFromText)
		{
			editor.ui.updateVolFromText = false;
			printThreeDecimalsBg(pixelBuffer, 176, 157, *editor.vol1Disp, palette[PAL_GENTXT], palette[PAL_GENBKG]);
		}

		if (editor.ui.updateVolToText)
		{
			editor.ui.updateVolToText = false;
			printThreeDecimalsBg(pixelBuffer, 176, 168, *editor.vol2Disp, palette[PAL_GENTXT], palette[PAL_GENBKG]);
		}
	}
	else if (editor.ui.samplerFiltersBoxShown)
	{
		if (editor.ui.updateLPText)
		{
			editor.ui.updateLPText = false;
			printFourDecimalsBg(pixelBuffer, 168, 157, *editor.lpCutOffDisp, palette[PAL_GENTXT], palette[PAL_GENBKG]);
		}

		if (editor.ui.updateHPText)
		{
			editor.ui.updateHPText = false;
			printFourDecimalsBg(pixelBuffer, 168, 168, *editor.hpCutOffDisp, palette[PAL_GENTXT], palette[PAL_GENBKG]);
		}

		if (editor.ui.updateNormFlag)
		{
			editor.ui.updateNormFlag = false;

			if (editor.normalizeFiltersFlag)
				textOutBg(pixelBuffer, 208, 179, "YES", palette[PAL_GENTXT], palette[PAL_GENBKG]);
			else
				textOutBg(pixelBuffer, 208, 179, "NO ", palette[PAL_GENTXT], palette[PAL_GENBKG]);
		}
	}
}

void showVolFromSlider(void)
{
	uint32_t *dstPtr, pixel, bgPixel, sliderStart, sliderEnd;

	sliderStart = (editor.vol1 * 3) / 10;
	sliderEnd  = sliderStart + 4;
	pixel = palette[PAL_QADSCP];
	bgPixel = palette[PAL_BACKGRD];
	dstPtr = &pixelBuffer[(158 * SCREEN_W) + 105];

	for (uint32_t y = 0; y < 3; y++)
	{
		for (uint32_t x = 0; x < 65; x++)
		{
			if (x >= sliderStart && x <= sliderEnd)
				dstPtr[x] = pixel;
			else
				dstPtr[x] = bgPixel;
		}

		dstPtr += SCREEN_W;
	}
}

void showVolToSlider(void)
{
	uint32_t *dstPtr, pixel, bgPixel, sliderStart, sliderEnd;

	sliderStart = (editor.vol2 * 3) / 10;
	sliderEnd = sliderStart + 4;
	pixel = palette[PAL_QADSCP];
	bgPixel = palette[PAL_BACKGRD];
	dstPtr = &pixelBuffer[(169 * SCREEN_W) + 105];

	for (uint32_t y = 0; y < 3; y++)
	{
		for (uint32_t x = 0; x < 65; x++)
		{
			if (x >= sliderStart && x <= sliderEnd)
				dstPtr[x] = pixel;
			else
				dstPtr[x] = bgPixel;
		}

		dstPtr += SCREEN_W;
	}
}

void renderSamplerVolBox(void)
{
	const uint32_t *srcPtr;
	uint32_t *dstPtr;

	srcPtr = samplerVolumeBMP;
	dstPtr = &pixelBuffer[(154 * SCREEN_W) + 72];

	for (uint32_t y = 0; y < 33; y++)
	{
		memcpy(dstPtr, srcPtr, 136 * sizeof (int32_t));

		srcPtr += 136;
		dstPtr += SCREEN_W;
	}

	editor.ui.updateVolFromText = true;
	editor.ui.updateVolToText = true;
	showVolFromSlider();
	showVolToSlider();

	// hide loop sprites
	hideSprite(SPRITE_LOOP_PIN_LEFT);
	hideSprite(SPRITE_LOOP_PIN_RIGHT);
}

void removeSamplerVolBox(void)
{
	displaySample();
}

void renderSamplerFiltersBox(void)
{
	const uint32_t *srcPtr;
	uint32_t *dstPtr;

	srcPtr = samplerFiltersBMP;
	dstPtr = &pixelBuffer[(154 * SCREEN_W) + 65];

	for (uint32_t y = 0; y < 33; y++)
	{
		memcpy(dstPtr, srcPtr, 186 * sizeof (int32_t));

		srcPtr += 186;
		dstPtr += SCREEN_W;
	}

	textOut(pixelBuffer, 200, 157, "HZ", palette[PAL_GENTXT]);
	textOut(pixelBuffer, 200, 168, "HZ", palette[PAL_GENTXT]);

	editor.ui.updateLPText = true;
	editor.ui.updateHPText = true;
	editor.ui.updateNormFlag = true;

	// hide loop sprites
	hideSprite(SPRITE_LOOP_PIN_LEFT);
	hideSprite(SPRITE_LOOP_PIN_RIGHT);
}

void removeSamplerFiltersBox(void)
{
	displaySample();
}

void renderDiskOpScreen(void)
{
	memcpy(pixelBuffer, diskOpScreenBMP, (99 * 320) * sizeof (int32_t));

	editor.ui.updateDiskOpPathText = true;
	editor.ui.updatePackText = true;
	editor.ui.updateSaveFormatText = true;
	editor.ui.updateLoadMode = true;
	editor.ui.updateDiskOpFileList = true;
}

void updateDiskOp(void)
{
	char tmpChar;
	const uint32_t *srcPtr;
	uint32_t *dstPtr;

	if (!editor.ui.diskOpScreenShown || editor.ui.posEdScreenShown)
		return;

	if (editor.ui.updateDiskOpFileList)
	{
		editor.ui.updateDiskOpFileList = false;
		diskOpRenderFileList(pixelBuffer);
	}

	if (editor.ui.updateLoadMode)
	{
		editor.ui.updateLoadMode = false;

		// clear backgrounds
		charOutBg(pixelBuffer, 147,  3, ' ', palette[PAL_GENBKG], palette[PAL_GENBKG]);
		charOutBg(pixelBuffer, 147, 14, ' ', palette[PAL_GENBKG], palette[PAL_GENBKG]);

		// draw load mode arrow

		srcPtr = arrowBMP;
		dstPtr = &pixelBuffer[(((11 * editor.diskop.mode) + 3) * SCREEN_W) + 148];

		for (uint32_t y = 0; y < 5; y++)
		{
			for (uint32_t x = 0; x < 6; x++)
				dstPtr[x] = srcPtr[x];

			srcPtr += 6;
			dstPtr += SCREEN_W;
		}
	}

	if (editor.ui.updatePackText)
	{
		editor.ui.updatePackText = false;
		textOutBg(pixelBuffer, 120, 3, editor.diskop.modPackFlg ? "ON " : "OFF", palette[PAL_GENTXT], palette[PAL_GENBKG]);
	}

	if (editor.ui.updateSaveFormatText)
	{
		editor.ui.updateSaveFormatText = false;
		     if (editor.diskop.smpSaveType == DISKOP_SMP_WAV) textOutBg(pixelBuffer, 120, 14, "WAV", palette[PAL_GENTXT], palette[PAL_GENBKG]);
		else if (editor.diskop.smpSaveType == DISKOP_SMP_IFF) textOutBg(pixelBuffer, 120, 14, "IFF", palette[PAL_GENTXT], palette[PAL_GENBKG]);
		else if (editor.diskop.smpSaveType == DISKOP_SMP_RAW) textOutBg(pixelBuffer, 120, 14, "RAW", palette[PAL_GENTXT], palette[PAL_GENBKG]);
	}

	if (editor.ui.updateDiskOpPathText)
	{
		editor.ui.updateDiskOpPathText = false;

		// print disk op. path
		for (uint32_t i = 0; i < 26; i++)
		{
			tmpChar = editor.currPath[editor.textofs.diskOpPath+i];
			if (tmpChar == '\0')
				tmpChar = '_';

			charOutBg(pixelBuffer, 24 + (i * FONT_CHAR_W), 25, tmpChar, palette[PAL_GENTXT], palette[PAL_GENBKG]);
		}
	}
}

void updatePosEd(void)
{
	int16_t posEdPosition;
	int32_t x, y, y2;
	uint32_t *dstPtr, bgPixel;

	if (!editor.ui.posEdScreenShown || !editor.ui.updatePosEd)
		return;

	editor.ui.updatePosEd = false;

	if (!editor.ui.disablePosEd)
	{
		bgPixel = palette[PAL_BACKGRD];

		posEdPosition = modEntry->currOrder;
		if (posEdPosition > modEntry->head.orderCount-1)
			posEdPosition = modEntry->head.orderCount-1;

		// top five
		for (y = 0; y < 5; y++)
		{
			if (posEdPosition-(5-y) >= 0)
			{
				printThreeDecimalsBg(pixelBuffer, 128, 23+(y*6),
					posEdPosition-(5-y), palette[PAL_QADSCP], palette[PAL_BACKGRD]);

				printTwoDecimalsBg(pixelBuffer, 160, 23+(y*6), modEntry->head.order[posEdPosition-(5-y)],
					palette[PAL_QADSCP], palette[PAL_BACKGRD]);
			}
			else
			{
				dstPtr = &pixelBuffer[((23+(y*6)) * SCREEN_W) + 128];
				for (y2 = 0; y2 < 5; y2++)
				{
					for (x = 0; x < FONT_CHAR_W*22; x++)
						dstPtr[x] = bgPixel;

					dstPtr += SCREEN_W;
				}
			}
		}

		// middle
		printThreeDecimalsBg(pixelBuffer, 128, 53, posEdPosition, palette[PAL_GENTXT], palette[PAL_GENBKG]);
		printTwoDecimalsBg(pixelBuffer, 160, 53, *editor.currPosEdPattDisp, palette[PAL_GENTXT], palette[PAL_GENBKG]);

		// bottom six
		for (y = 0; y < 6; y++)
		{
			if (posEdPosition+y < modEntry->head.orderCount-1)
			{
				printThreeDecimalsBg(pixelBuffer, 128, 59+(y*6), posEdPosition+(y+1),
					palette[PAL_QADSCP], palette[PAL_BACKGRD]);

				printTwoDecimalsBg(pixelBuffer, 160, 59+(y*6), modEntry->head.order[posEdPosition+(y+1)],
					palette[PAL_QADSCP], palette[PAL_BACKGRD]);
			}
			else
			{
				dstPtr = &pixelBuffer[((59+(y*6)) * SCREEN_W) + 128];
				for (y2 = 0; y2 < 5; y2++)
				{
					for (x = 0; x < FONT_CHAR_W*22; x++)
						dstPtr[x] = bgPixel;

					dstPtr += SCREEN_W;
				}
			}
		}

		// kludge to fix bottom part of text edit marker in pos ed
		if (editor.ui.editTextFlag && editor.ui.editObject == PTB_PE_PATT)
			renderTextEditMarker();
	}
}

void renderPosEdScreen(void)
{
	const uint32_t *srcPtr;
	uint32_t *dstPtr;

	srcPtr = posEdBMP;
	dstPtr = &pixelBuffer[120];

	for (uint32_t y = 0; y < 99; y++)
	{
		memcpy(dstPtr, srcPtr, 200 * sizeof (int32_t));

		srcPtr += 200;
		dstPtr += SCREEN_W;
	}
}

void renderMuteButtons(void)
{
	const uint32_t *srcPtr;
	uint32_t *dstPtr, srcPitch;

	if (editor.ui.diskOpScreenShown || editor.ui.posEdScreenShown)
		return;

	dstPtr = &pixelBuffer[(3 * SCREEN_W) + 310];
	for (uint32_t i = 0; i < AMIGA_VOICES; i++)
	{
		if (editor.muted[i])
		{
			srcPtr = &muteButtonsBMP[i * (6 * 7)];
			srcPitch = 7;
		}
		else
		{
			srcPtr = &trackerFrameBMP[((3 + (i * 11)) * SCREEN_W) + 310];
			srcPitch = SCREEN_W;
		}

		for (uint32_t y = 0; y < 6; y++)
		{
			for (uint32_t x = 0; x < 7; x++)
				dstPtr[x] = srcPtr[x];

			srcPtr += srcPitch;
			dstPtr += SCREEN_W;
		}

		dstPtr += SCREEN_W * 5;
	}
}

void renderClearScreen(void)
{
	const uint32_t *srcPtr;
	uint32_t *dstPtr;

	editor.ui.disablePosEd = true;
	editor.ui.disableVisualizer = true;

	srcPtr = clearDialogBMP;
	dstPtr = &pixelBuffer[(51 * SCREEN_W) + 160];

	for (uint32_t y = 0; y < 39; y++)
	{
		memcpy(dstPtr, srcPtr, 104 * sizeof (int32_t));

		srcPtr += 104;
		dstPtr += SCREEN_W;
	}
}

void removeClearScreen(void)
{
	displayMainScreen();

	editor.ui.disablePosEd = false;
	editor.ui.disableVisualizer = false;
}

void updateCurrSample(void)
{
	editor.ui.updateCurrSampleName = true;
	editor.ui.updateSongSize = true;

	if (!editor.ui.diskOpScreenShown)
	{
		editor.ui.updateCurrSampleFineTune = true;
		editor.ui.updateCurrSampleNum = true;
		editor.ui.updateCurrSampleVolume = true;
		editor.ui.updateCurrSampleLength = true;
		editor.ui.updateCurrSampleRepeat = true;
		editor.ui.updateCurrSampleReplen = true;
	}

	if (editor.ui.samplerScreenShown)
		redrawSample();

	updateSamplePos();
	recalcChordLength();

	editor.sampler.tmpLoopStart = 0;
	editor.sampler.tmpLoopLength = 0;
}

void updatePatternData(void)
{
	if (editor.ui.updatePatternData)
	{
		editor.ui.updatePatternData = false;
		if (!editor.ui.samplerScreenShown)
			redrawPattern(pixelBuffer);
	}
}

void removeTextEditMarker(void)
{
	uint32_t *dstPtr, pixel;

	if (!editor.ui.editTextFlag)
		return;

	dstPtr = &pixelBuffer[((editor.ui.lineCurY - 1) * SCREEN_W) + (editor.ui.lineCurX - 4)];

	if (editor.ui.editObject == PTB_PE_PATT)
	{
		// position editor text editing

		pixel = palette[PAL_GENBKG2];
		for (uint32_t x = 0; x < 7; x++)
			dstPtr[x] = pixel;
		// no need to clear the second row of pixels

		editor.ui.updatePosEd = true;
	}
	else
	{
		// all others

		pixel = palette[PAL_GENBKG];
		for (uint32_t y = 0; y < 2; y++)
		{
			for (uint32_t x = 0; x < 7; x++)
				dstPtr[x] = pixel;

			dstPtr += SCREEN_W;
		}
	}
}

void renderTextEditMarker(void)
{
	uint32_t *dstPtr, pixel;

	if (!editor.ui.editTextFlag)
		return;

	dstPtr = &pixelBuffer[((editor.ui.lineCurY - 1) * SCREEN_W) + (editor.ui.lineCurX - 4)];
	pixel = palette[PAL_TEXTMARK];

	for (uint32_t y = 0; y < 2; y++)
	{
		for (uint32_t x = 0; x < 7; x++)
			dstPtr[x] = pixel;

		dstPtr += SCREEN_W;
	}
}

void updateDragBars(void)
{
	if (editor.ui.sampleMarkingPos >= 0) samplerSamplePressed(MOUSE_BUTTON_HELD);
	if (editor.ui.forceSampleDrag) samplerBarPressed(MOUSE_BUTTON_HELD);
	if (editor.ui.forceSampleEdit) samplerEditSample(MOUSE_BUTTON_HELD);
	if (editor.ui.forceVolDrag) volBoxBarPressed(MOUSE_BUTTON_HELD);
}

void updateVisualizer(void)
{
	const uint32_t *srcPtr;
	int32_t tmpVol;
	uint32_t *dstPtr, pixel;

	if (editor.ui.disableVisualizer || editor.ui.diskOpScreenShown ||
		editor.ui.posEdScreenShown  || editor.ui.editOpScreenShown ||
		editor.ui.aboutScreenShown  || editor.ui.askScreenShown    ||
		editor.isWAVRendering)
	{
		return;
	}

	if (editor.ui.visualizerMode == VISUAL_SPECTRUM)
	{
		// spectrum analyzer

		dstPtr = &pixelBuffer[(59 * SCREEN_W) + 129];
		for (uint32_t i = 0; i < SPECTRUM_BAR_NUM; i++)
		{
			srcPtr = spectrumAnaBMP;
			pixel = palette[PAL_GENBKG];

			tmpVol = editor.spectrumVolumes[i];
			if (tmpVol > SPECTRUM_BAR_HEIGHT)
				tmpVol = SPECTRUM_BAR_HEIGHT;

			for (int32_t y = SPECTRUM_BAR_HEIGHT-1; y >= 0; y--)
			{
				if (y < tmpVol)
					pixel = srcPtr[y];

				for (uint32_t  x = 0; x < SPECTRUM_BAR_WIDTH; x++)
					dstPtr[x] = pixel;

				dstPtr += SCREEN_W;
			}

			dstPtr -= (SCREEN_W * SPECTRUM_BAR_HEIGHT) - (SPECTRUM_BAR_WIDTH + 2);
		}
	}
	else
	{
		drawScopes();
	}
}

void renderQuadrascopeBg(void)
{
	const uint32_t *srcPtr;
	uint32_t *dstPtr;

	srcPtr = &trackerFrameBMP[(44 * SCREEN_W) + 120];
	dstPtr = &pixelBuffer[(44 * SCREEN_W) + 120];

	for (uint32_t y = 0; y < 55; y++)
	{
		memcpy(dstPtr, srcPtr, 200 * sizeof (int32_t));

		srcPtr += SCREEN_W;
		dstPtr += SCREEN_W;
	}

	for (uint32_t i = 0; i < AMIGA_VOICES; i++)
		scopeExt[i].emptyScopeDrawn = false;
}

void renderSpectrumAnalyzerBg(void)
{
	const uint32_t *srcPtr;
	uint32_t *dstPtr;

	srcPtr = spectrumVisualsBMP;
	dstPtr = &pixelBuffer[(44 * SCREEN_W) + 120];

	for (uint32_t y = 0; y < 55; y++)
	{
		memcpy(dstPtr, srcPtr, 200 * sizeof (int32_t));

		srcPtr += 200;
		dstPtr += SCREEN_W;
	}
}

void renderAboutScreen(void)
{
	char verString[16];
	const uint32_t *srcPtr;
	uint32_t verStringX, *dstPtr;

	if (!editor.ui.aboutScreenShown || editor.ui.diskOpScreenShown || editor.ui.posEdScreenShown || editor.ui.editOpScreenShown)
		return;

	srcPtr = aboutScreenBMP;
	dstPtr = &pixelBuffer[(44 * SCREEN_W) + 120];

	for (uint32_t y = 0; y < 55; y++)
	{
		memcpy(dstPtr, srcPtr, 200 * sizeof (int32_t));

		srcPtr += 200;
		dstPtr += SCREEN_W;
	}

	// draw version string

	sprintf(verString, "v%s", PROG_VER_STR);
	verStringX = 260 + (63 - ((uint32_t)strlen(verString) * (FONT_CHAR_W - 1))) / 2;
	textOutTight(pixelBuffer, verStringX, 67, verString, palette[PAL_GENBKG2]);
}

void renderEditOpMode(void)
{
	const uint32_t *srcPtr;
	uint32_t *dstPtr;

	// select what character box to render

	switch (editor.ui.editOpScreen)
	{
		default:
		case 0:
			srcPtr = &editOpModeCharsBMP[editor.sampleAllFlag ? EDOP_MODE_BMP_A_OFS : EDOP_MODE_BMP_S_OFS];
		break;

		case 1:
		{
			     if (editor.trackPattFlag == 0) srcPtr = &editOpModeCharsBMP[EDOP_MODE_BMP_T_OFS];
			else if (editor.trackPattFlag == 1) srcPtr = &editOpModeCharsBMP[EDOP_MODE_BMP_P_OFS];
			else srcPtr = &editOpModeCharsBMP[EDOP_MODE_BMP_S_OFS];
		}
		break;

		case 2:
			srcPtr = &editOpModeCharsBMP[editor.halfClipFlag ? EDOP_MODE_BMP_C_OFS : EDOP_MODE_BMP_H_OFS];
		break;

		case 3:
			srcPtr = (editor.newOldFlag == 0) ? &editOpModeCharsBMP[EDOP_MODE_BMP_N_OFS] : &editOpModeCharsBMP[EDOP_MODE_BMP_O_OFS];
		break;
	}

	// render it...

	dstPtr = &pixelBuffer[(47 * SCREEN_W) + 310];
	for (uint32_t y = 0; y < 6; y++)
	{
		for (uint32_t x = 0; x < 7; x++)
			dstPtr[x] = srcPtr[x];

		srcPtr += 7;
		dstPtr += SCREEN_W;
	}
}

void renderEditOpScreen(void)
{
	const uint32_t *srcPtr;
	uint32_t *dstPtr;

	// select which background to render
	switch (editor.ui.editOpScreen)
	{
		default:
		case 0: srcPtr = editOpScreen1BMP; break;
		case 1: srcPtr = editOpScreen2BMP; break;
		case 2: srcPtr = editOpScreen3BMP; break;
		case 3: srcPtr = editOpScreen4BMP; break;
	}

	// render background
	dstPtr = &pixelBuffer[(44 * SCREEN_W) + 120];
	for (uint32_t y = 0; y < 55; y++)
	{
		memcpy(dstPtr, srcPtr, 200 * sizeof (int32_t));

		srcPtr += 200;
		dstPtr += SCREEN_W;
	}

	renderEditOpMode();

	// render text and content
	if (editor.ui.editOpScreen == 0)
	{
		textOut(pixelBuffer, 128, 47, "  TRACK      PATTERN  ", palette[PAL_GENTXT]);
	}
	else if (editor.ui.editOpScreen == 1)
	{
		textOut(pixelBuffer, 128, 47, "  RECORD     SAMPLES  ", palette[PAL_GENTXT]);

		editor.ui.updateRecordText = true;
		editor.ui.updateQuantizeText = true;
		editor.ui.updateMetro1Text = true;
		editor.ui.updateMetro2Text = true;
		editor.ui.updateFromText = true;
		editor.ui.updateKeysText = true;
		editor.ui.updateToText = true;
	}
	else if (editor.ui.editOpScreen == 2)
	{
		textOut(pixelBuffer, 128, 47, "    SAMPLE EDITOR     ", palette[PAL_GENTXT]);
		charOut(pixelBuffer, 272, 91, '%', palette[PAL_GENTXT]); // for Volume text

		editor.ui.updatePosText = true;
		editor.ui.updateModText = true;
		editor.ui.updateVolText = true;
	}
	else if (editor.ui.editOpScreen == 3)
	{
		textOut(pixelBuffer, 128, 47, " SAMPLE CHORD EDITOR  ", palette[PAL_GENTXT]);

		editor.ui.updateLengthText = true;
		editor.ui.updateNote1Text = true;
		editor.ui.updateNote2Text = true;
		editor.ui.updateNote3Text = true;
		editor.ui.updateNote4Text = true;
	}
}

void renderMOD2WAVDialog(void)
{
	const uint32_t *srcPtr;
	uint32_t *dstPtr;

	srcPtr = mod2wavBMP;
	dstPtr = &pixelBuffer[(27 * SCREEN_W) + 64];

	for (uint32_t y = 0; y < 48; y++)
	{
		memcpy(dstPtr, srcPtr, 192 * sizeof (int32_t));

		srcPtr += 192;
		dstPtr += SCREEN_W;
	}
}

void updateMOD2WAVDialog(void)
{
	int32_t barLength, percent;
	uint32_t *dstPtr, bgPixel, pixel;

	if (!editor.ui.updateMod2WavDialog)
		return;

	editor.ui.updateMod2WavDialog = false;

	if (editor.isWAVRendering)
	{
		if (editor.ui.mod2WavFinished)
		{
			editor.ui.mod2WavFinished = false;

			resetSong();
			pointerSetMode(POINTER_MODE_IDLE, DO_CARRY);

			if (editor.abortMod2Wav)
			{
				displayErrorMsg("MOD2WAV ABORTED !");
			}
			else
			{
				displayMsg("MOD RENDERED !");
				setMsgPointer();
			}

			editor.isWAVRendering = false;
			displayMainScreen();
		}
		else
		{
			// render progress bar

			percent = (uint8_t)((modEntry->rowsCounter * 100) / modEntry->rowsInTotal);
			if (percent > 100)
				percent = 100;

			barLength = (percent * 180) / 100;
			dstPtr = &pixelBuffer[(42 * SCREEN_W) + 70];
			pixel = palette[PAL_GENBKG2];
			bgPixel = palette[PAL_BORDER];

			for (int32_t y = 0; y < 11; y++)
			{
				for (int32_t x = 0; x < 180; x++)
					dstPtr[x] = (x < barLength) ? pixel : bgPixel;

				dstPtr += SCREEN_W;
			}

			// render percentage
			pixel = palette[PAL_GENTXT];
			if (percent > 99)
				printThreeDecimals(pixelBuffer, 144, 45, percent, pixel);
			else
				printTwoDecimals(pixelBuffer, 152, 45, percent, pixel);

			charOut(pixelBuffer, 168, 45, '%', pixel);
		}
	}
}

void updateEditOp(void)
{
	if (!editor.ui.editOpScreenShown || editor.ui.posEdScreenShown || editor.ui.diskOpScreenShown)
		return;

	if (editor.ui.editOpScreen == 1)
	{
		if (editor.ui.updateRecordText)
		{
			editor.ui.updateRecordText = false;
			textOutBg(pixelBuffer, 176, 58, (editor.recordMode == RECORD_PATT) ? "PATT" : "SONG",
				palette[PAL_GENTXT], palette[PAL_GENBKG]);
		}

		if (editor.ui.updateQuantizeText)
		{
			editor.ui.updateQuantizeText = false;
			printTwoDecimalsBg(pixelBuffer, 192, 69, *editor.quantizeValueDisp, palette[PAL_GENTXT], palette[PAL_GENBKG]);
		}

		if (editor.ui.updateMetro1Text)
		{
			editor.ui.updateMetro1Text = false;
			printTwoDecimalsBg(pixelBuffer, 168, 80, *editor.metroSpeedDisp, palette[PAL_GENTXT], palette[PAL_GENBKG]);
		}

		if (editor.ui.updateMetro2Text)
		{
			editor.ui.updateMetro2Text = false;
			printTwoDecimalsBg(pixelBuffer, 192, 80, *editor.metroChannelDisp, palette[PAL_GENTXT], palette[PAL_GENBKG]);
		}

		if (editor.ui.updateFromText)
		{
			editor.ui.updateFromText = false;
			printTwoHexBg(pixelBuffer, 264, 80, *editor.sampleFromDisp, palette[PAL_GENTXT], palette[PAL_GENBKG]);
		}

		if (editor.ui.updateKeysText)
		{
			editor.ui.updateKeysText = false;
			textOutBg(pixelBuffer, 160, 91, editor.multiFlag ? "MULTI " : "SINGLE", palette[PAL_GENTXT], palette[PAL_GENBKG]);
		}

		if (editor.ui.updateToText)
		{
			editor.ui.updateToText = false;
			printTwoHexBg(pixelBuffer, 264, 91, *editor.sampleToDisp, palette[PAL_GENTXT], palette[PAL_GENBKG]);
		}
	}
	else if (editor.ui.editOpScreen == 2)
	{
		if (editor.ui.updateMixText)
		{
			editor.ui.updateMixText = false;
			if (editor.mixFlag)
			{
				textOutBg(pixelBuffer, 128, 47, editor.mixText, palette[PAL_GENTXT], palette[PAL_GENBKG]);
				textOutBg(pixelBuffer, 248, 47, "  ", palette[PAL_GENTXT], palette[PAL_GENBKG]);
			}
			else
			{
				textOutBg(pixelBuffer, 128, 47, "    SAMPLE EDITOR     ", palette[PAL_GENTXT], palette[PAL_GENBKG]);
			}
		}

		if (editor.ui.updatePosText)
		{
			editor.ui.updatePosText = false;
			printFourHexBg(pixelBuffer, 248, 58, *editor.samplePosDisp, palette[PAL_GENTXT], palette[PAL_GENBKG]);
		}

		if (editor.ui.updateModText)
		{
			editor.ui.updateModText = false;
			printThreeDecimalsBg(pixelBuffer, 256, 69,
				(editor.modulateSpeed < 0) ? (0 - editor.modulateSpeed) : editor.modulateSpeed,
				palette[PAL_GENTXT], palette[PAL_GENBKG]);

			if (editor.modulateSpeed < 0)
				charOutBg(pixelBuffer, 248, 69, '-', palette[PAL_GENTXT], palette[PAL_GENBKG]);
			else
				charOutBg(pixelBuffer, 248, 69, ' ', palette[PAL_GENTXT], palette[PAL_GENBKG]);
		}

		if (editor.ui.updateVolText)
		{
			editor.ui.updateVolText = false;
			printThreeDecimalsBg(pixelBuffer, 248, 91, *editor.sampleVolDisp, palette[PAL_GENTXT], palette[PAL_GENBKG]);
		}
	}
	else if (editor.ui.editOpScreen == 3)
	{
		if (editor.ui.updateLengthText)
		{
			editor.ui.updateLengthText = false;

			// clear background
			textOutBg(pixelBuffer, 168, 91, "    ", palette[PAL_GENTXT], palette[PAL_GENBKG]);
			charOut(pixelBuffer, 198, 91,    ':', palette[PAL_GENBKG]);

			if (modEntry->samples[editor.currSample].loopLength > 2 || modEntry->samples[editor.currSample].loopStart >= 2)
			{
				textOut(pixelBuffer, 168, 91, "LOOP", palette[PAL_GENTXT]);
			}
			else
			{
				printFourHex(pixelBuffer, 168, 91, *editor.chordLengthDisp, palette[PAL_GENTXT]); // CHORD MAX LENGTH
				charOut(pixelBuffer, 198, 91, (editor.chordLengthMin) ? '.' : ':', palette[PAL_GENTXT]); // MIN/MAX FLAG
			}
		}

		if (editor.ui.updateNote1Text)
		{
			editor.ui.updateNote1Text = false;
			if (editor.note1 > 35)
				textOutBg(pixelBuffer, 256, 58, "---", palette[PAL_GENTXT], palette[PAL_GENBKG]);
			else
				textOutBg(pixelBuffer, 256, 58, ptConfig.accidental ? noteNames2[editor.note1] : noteNames1[editor.note1],
					palette[PAL_GENTXT], palette[PAL_GENBKG]);
		}

		if (editor.ui.updateNote2Text)
		{
			editor.ui.updateNote2Text = false;
			if (editor.note2 > 35)
				textOutBg(pixelBuffer, 256, 69, "---", palette[PAL_GENTXT], palette[PAL_GENBKG]);
			else
				textOutBg(pixelBuffer, 256, 69, ptConfig.accidental ? noteNames2[editor.note2] : noteNames1[editor.note2],
					palette[PAL_GENTXT], palette[PAL_GENBKG]);
		}

		if (editor.ui.updateNote3Text)
		{
			editor.ui.updateNote3Text = false;
			if (editor.note3 > 35)
				textOutBg(pixelBuffer, 256, 80, "---", palette[PAL_GENTXT], palette[PAL_GENBKG]);
			else
				textOutBg(pixelBuffer, 256, 80, ptConfig.accidental ? noteNames2[editor.note3] : noteNames1[editor.note3],
					palette[PAL_GENTXT], palette[PAL_GENBKG]);
		}
			
		if (editor.ui.updateNote4Text)
		{
			editor.ui.updateNote4Text = false;
			if (editor.note4 > 35)
				textOutBg(pixelBuffer, 256, 91, "---", palette[PAL_GENTXT], palette[PAL_GENBKG]);
			else
				textOutBg(pixelBuffer, 256, 91, ptConfig.accidental ? noteNames2[editor.note4] : noteNames1[editor.note4],
					palette[PAL_GENTXT], palette[PAL_GENBKG]);
		}
	}
}

void displayMainScreen(void)
{
	editor.blockMarkFlag = false;

	editor.ui.updateSongName = true;
	editor.ui.updateSongSize = true;
	editor.ui.updateSongTiming = true;
	editor.ui.updateTrackerFlags = true;
	editor.ui.updateStatusText = true;

	editor.ui.updateCurrSampleName = true;

	if (!editor.ui.diskOpScreenShown)
	{
		editor.ui.updateCurrSampleFineTune = true;
		editor.ui.updateCurrSampleNum = true;
		editor.ui.updateCurrSampleVolume = true;
		editor.ui.updateCurrSampleLength = true;
		editor.ui.updateCurrSampleRepeat = true;
		editor.ui.updateCurrSampleReplen = true;
	}

	if (editor.ui.samplerScreenShown)
	{
		if (!editor.ui.diskOpScreenShown)
			memcpy(pixelBuffer, trackerFrameBMP, 320 * 121 * sizeof (int32_t));
	}
	else
	{
		if (!editor.ui.diskOpScreenShown)
			memcpy(pixelBuffer, trackerFrameBMP, 320 * 255 * sizeof (int32_t));
		else
			memcpy(&pixelBuffer[121 * SCREEN_W], &trackerFrameBMP[121 * SCREEN_W], 320 * 134 * sizeof (int32_t));

		editor.ui.updateSongBPM = true;
		editor.ui.updateCurrPattText = true;
		editor.ui.updatePatternData  = true;
	}

	if (editor.ui.diskOpScreenShown)
	{
		renderDiskOpScreen();
	}
	else
	{
		editor.ui.updateSongPos = true;
		editor.ui.updateSongPattern = true;
		editor.ui.updateSongLength = true;

		// zeroes (can't integrate zeroes in the graphics, the palette entry is above the 2-bit range)
		charOut(pixelBuffer, 64,  3, '0', palette[PAL_GENTXT]);
		textOut(pixelBuffer, 64, 14, "00", palette[PAL_GENTXT]);

		if (!editor.isWAVRendering)
		{
			charOut(pixelBuffer, 64, 25, '0', palette[PAL_GENTXT]);
			textOut(pixelBuffer, 64, 47, "00", palette[PAL_GENTXT]);
			textOut(pixelBuffer, 64, 58, "00", palette[PAL_GENTXT]);
		}

		if (editor.ui.posEdScreenShown)
		{
			renderPosEdScreen();
			editor.ui.updatePosEd = true;
		}
		else
		{
			if (editor.ui.editOpScreenShown)
			{
				renderEditOpScreen();
			}
			else
			{
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

			renderMuteButtons();
		}
	}
}

static void restoreStatusAndMousePointer(void)
{
	editor.errorMsgActive = false;
	editor.errorMsgBlock = false;
	editor.errorMsgCounter = 0;
	pointerSetPreviousMode();
	setPrevStatusMessage();
}

void handleAskNo(void)
{
	editor.ui.pat2SmpDialogShown = false;

	switch (editor.ui.askScreenType)
	{
		case ASK_SAVEMOD_OVERWRITE:
		{
			restoreStatusAndMousePointer();
			saveModule(DONT_CHECK_IF_FILE_EXIST, GIVE_NEW_FILENAME);
		}
		break;

		case ASK_SAVESMP_OVERWRITE:
		{
			restoreStatusAndMousePointer();
			saveSample(DONT_CHECK_IF_FILE_EXIST, GIVE_NEW_FILENAME);
		}
		break;

		case ASK_DOWNSAMPLING:
		{
			restoreStatusAndMousePointer();
			extLoadWAVOrAIFFSampleCallback(DONT_DOWNSAMPLE);
		}
		break;

		default:
		{
			restoreStatusAndMousePointer();
			editor.errorMsgActive = true;
			editor.errorMsgBlock = true;
			editor.errorMsgCounter = 0;
			setErrPointer();
		}
		break;
	}

	removeAskDialog();
}

void handleAskYes(void)
{
	char fileName[20 + 4 + 1];
	int8_t *tmpSmpBuffer, oldSample, oldRow;
	int32_t j, newLength, oldSamplesPerTick, loopStart, loopLength;
	uint32_t i;
	moduleSample_t *s;

	switch (editor.ui.askScreenType)
	{
		case ASK_DISCARD_SONG:
		{
			restoreStatusAndMousePointer();
			diskOpLoadFile2();
		}
		break;

		case ASK_DISCARD_SONG_DRAGNDROP:
		{
			restoreStatusAndMousePointer();
			loadDroppedFile2();
		}
		break;

		case ASK_RESTORE_SAMPLE:
		{
			restoreStatusAndMousePointer();
			redoSampleData(editor.currSample);
		}
		break;

		case ASK_PAT2SMP:
		{
			restoreStatusAndMousePointer();

			editor.ui.pat2SmpDialogShown = false;

			editor.pat2SmpBuf = (int16_t *)malloc(MAX_SAMPLE_LEN * sizeof (int16_t));
			if (editor.pat2SmpBuf == NULL)
			{
				statusOutOfMemory();
				return;
			}

			oldRow = editor.songPlaying ? 0 : modEntry->currRow;
			oldSamplesPerTick = samplesPerTick;

			editor.isSMPRendering = true; // this must be set before restartSong()
			storeTempVariables();
			restartSong();
			modEntry->row = oldRow;
			modEntry->currRow = modEntry->row;

			editor.blockMarkFlag = false;
			pointerSetMode(POINTER_MODE_MSG2, NO_CARRY);
			setStatusMessage("RENDERING...", NO_CARRY);
			modSetTempo(modEntry->currBPM);
			editor.pat2SmpPos = 0;

			editor.smpRenderingDone = false;
			while (!editor.smpRenderingDone)
			{
				if (!intMusic())
					editor.smpRenderingDone = true;

				outputAudio(NULL, samplesPerTick);
			}
			editor.isSMPRendering = false;
			resetSong();

			// set back old row and samplesPerTick
			modEntry->row = oldRow;
			modEntry->currRow = modEntry->row;
			mixerSetSamplesPerTick(oldSamplesPerTick);

			// normalize 16-bit samples
			normalize16bitSigned(editor.pat2SmpBuf, MIN(editor.pat2SmpPos, MAX_SAMPLE_LEN));

			s = &modEntry->samples[editor.currSample];

			// quantize to 8-bit
			for (i = 0; i < editor.pat2SmpPos; i++)
				modEntry->sampleData[s->offset+i] = editor.pat2SmpBuf[i] >> 8;

			// clear the rest of the sample
			if (editor.pat2SmpPos < MAX_SAMPLE_LEN)
				memset(&modEntry->sampleData[s->offset+editor.pat2SmpPos], 0, MAX_SAMPLE_LEN - editor.pat2SmpPos);

			// free temp mixing buffer
			free(editor.pat2SmpBuf);

			// zero out sample text
			memset(s->text, 0, sizeof (s->text));

			// set new sample text
			if (editor.pat2SmpHQ)
			{
				strcpy(s->text, "pat2smp (a-3 tune:+5)");
				s->fineTune = 5;
			}
			else
			{
				strcpy(s->text, "pat2smp (f-3 tune:+1)");
				s->fineTune = 1;
			}

			// new sample attributes
			s->length = editor.pat2SmpPos;
			s->volume = 64;
			s->loopStart = 0;
			s->loopLength = 2;

			pointerSetMode(POINTER_MODE_IDLE, DO_CARRY);
			displayMsg("ROWS RENDERED!");
			setMsgPointer();
			editor.samplePos = 0;
			fixSampleBeep(s);
			updateCurrSample();
		}
		break;

		case ASK_SAVE_ALL_SAMPLES:
		{
			editor.errorMsgActive = false;
			editor.errorMsgBlock = false;
			editor.errorMsgCounter = 0;

			oldSample = editor.currSample;
			for (i = 0; i < MOD_SAMPLES; i++)
			{
				editor.currSample = (int8_t)i;
				if (modEntry->samples[i].length > 2)
					saveSample(DONT_CHECK_IF_FILE_EXIST, GIVE_NEW_FILENAME);
			}
			editor.currSample = oldSample;

			displayMsg("SAMPLES SAVED !");
			setMsgPointer();
		}
		break;

		case ASK_MAKE_CHORD:
		{
			restoreStatusAndMousePointer();
			mixChordSample();
		}
		break;

		case ASK_BOOST_ALL_SAMPLES:
		{
			restoreStatusAndMousePointer();

			for (i = 0; i < MOD_SAMPLES; i++)
				boostSample(i, true);

			if (editor.ui.samplerScreenShown)
				redrawSample();

			updateWindowTitle(MOD_IS_MODIFIED);
		}
		break;

		case ASK_FILTER_ALL_SAMPLES:
		{
			restoreStatusAndMousePointer();

			for (i = 0; i < MOD_SAMPLES; i++)
				filterSample(i, true);

			if (editor.ui.samplerScreenShown)
				redrawSample();

			updateWindowTitle(MOD_IS_MODIFIED);
		}
		break;

		case ASK_UPSAMPLE:
		{
			restoreStatusAndMousePointer();

			s = &modEntry->samples[editor.currSample];

			tmpSmpBuffer = (int8_t *)malloc(s->length);
			if (tmpSmpBuffer == NULL)
			{
				statusOutOfMemory();
				return;
			}

			newLength = (s->length / 2) & 0xFFFE;
			if (newLength < 2)
				return;

			turnOffVoices();

			memcpy(tmpSmpBuffer, &modEntry->sampleData[s->offset], s->length);

			// upsample
			for (j = 0; j < newLength; j++)
				modEntry->sampleData[s->offset + j] = tmpSmpBuffer[j * 2];

			if (newLength < MAX_SAMPLE_LEN)
				memset(&modEntry->sampleData[s->offset + newLength], 0, MAX_SAMPLE_LEN - newLength);

			free(tmpSmpBuffer);

			s->length = newLength;
			s->loopStart = (s->loopStart / 2) & 0xFFFE;
			s->loopLength = (s->loopLength / 2) & 0xFFFE;

			if (s->loopLength < 2)
			{
				s->loopStart = 0;
				s->loopLength = 2;
			}

			fixSampleBeep(s);
			updateCurrSample();

			editor.ui.updateSongSize = true;
			updateWindowTitle(MOD_IS_MODIFIED);
		}
		break;

		case ASK_DOWNSAMPLE:
		{
			restoreStatusAndMousePointer();

			s = &modEntry->samples[editor.currSample];

			tmpSmpBuffer = (int8_t *)malloc(s->length);
			if (tmpSmpBuffer == NULL)
			{
				statusOutOfMemory();
				return;
			}

			newLength = s->length * 2;
			if (newLength > MAX_SAMPLE_LEN)
				newLength = MAX_SAMPLE_LEN;

			turnOffVoices();

			memcpy(tmpSmpBuffer, &modEntry->sampleData[s->offset], s->length);

			// downsample
			for (j = 0; j < newLength; j++)
				modEntry->sampleData[s->offset+j] = tmpSmpBuffer[j >> 1];

			if (newLength < MAX_SAMPLE_LEN)
				memset(&modEntry->sampleData[s->offset+newLength], 0, MAX_SAMPLE_LEN - newLength);

			free(tmpSmpBuffer);

			s->length = newLength;

			if (s->loopLength > 2)
			{
				loopStart = s->loopStart * 2;
				loopLength = s->loopLength * 2;

				if (loopStart+loopLength > s->length)
				{
					loopStart = 0;
					loopLength = 2;
				}

				s->loopStart = (uint16_t)loopStart;
				s->loopLength = (uint16_t)loopLength;
			}

			fixSampleBeep(s);
			updateCurrSample();

			editor.ui.updateSongSize = true;
			updateWindowTitle(MOD_IS_MODIFIED);
		}
		break;

		case ASK_KILL_SAMPLE:
		{
			restoreStatusAndMousePointer();

			turnOffVoices();
			s = &modEntry->samples[editor.currSample];

			s->fineTune = 0;
			s->volume = 0;
			s->length = 0;
			s->loopStart = 0;
			s->loopLength = 2;

			memset(s->text, 0, sizeof (s->text));
			memset(&modEntry->sampleData[(editor.currSample * MAX_SAMPLE_LEN)], 0, MAX_SAMPLE_LEN);

			editor.samplePos = 0;
			updateCurrSample();

			editor.ui.updateSongSize = true;
			updateWindowTitle(MOD_IS_MODIFIED);
		}
		break;

		case ASK_RESAMPLE:
		{
			restoreStatusAndMousePointer();
			samplerResample();
		}
		break;

		case ASK_DOWNSAMPLING:
		{
			// for WAV and AIFF sample loader
			restoreStatusAndMousePointer();
			extLoadWAVOrAIFFSampleCallback(DO_DOWNSAMPLE);
		}
		break;

		case ASK_MOD2WAV_OVERWRITE:
		{
			memset(fileName, 0, sizeof (fileName));

			if (modEntry->head.moduleTitle[0] != '\0')
			{
				for (i = 0; i < 20; i++)
				{
					fileName[i] = (char)tolower(modEntry->head.moduleTitle[i]);
					if (fileName[i] == '\0') break;
					sanitizeFilenameChar(&fileName[i]);
				}

				strcat(fileName, ".wav");
			}
			else
			{
				strcpy(fileName, "untitled.wav");
			}

			renderToWav(fileName, DONT_CHECK_IF_FILE_EXIST);
		}
		break;

		case ASK_MOD2WAV:
		{
			memset(fileName, 0, sizeof (fileName));

			if (modEntry->head.moduleTitle[0] != '\0')
			{
				for (i = 0; i < 20; i++)
				{
					fileName[i] = (char)(tolower(modEntry->head.moduleTitle[i]));
					if (fileName[i] == '\0') break;
					sanitizeFilenameChar(&fileName[i]);
				}

				strcat(fileName, ".wav");
			}
			else
			{
				strcpy(fileName, "untitled.wav");
			}

			renderToWav(fileName, CHECK_IF_FILE_EXIST);
		}
		break;

		case ASK_QUIT:
		{
			restoreStatusAndMousePointer();
			editor.ui.throwExit = true;
		}
		break;

		case ASK_SAVE_SAMPLE:
		{
			restoreStatusAndMousePointer();
			saveSample(CHECK_IF_FILE_EXIST, DONT_GIVE_NEW_FILENAME);
		}
		break;

		case ASK_SAVESMP_OVERWRITE:
		{
			restoreStatusAndMousePointer();
			saveSample(DONT_CHECK_IF_FILE_EXIST, DONT_GIVE_NEW_FILENAME);
		}
		break;

		case ASK_SAVE_MODULE:
		{
			restoreStatusAndMousePointer();
			saveModule(CHECK_IF_FILE_EXIST, DONT_GIVE_NEW_FILENAME);
		}
		break;

		case ASK_SAVEMOD_OVERWRITE:
		{
			restoreStatusAndMousePointer();
			saveModule(DONT_CHECK_IF_FILE_EXIST, DONT_GIVE_NEW_FILENAME);
		}
		break;

		default: break;
	}

	removeAskDialog();
}

void createBitmaps(void)
{
	uint8_t r8, g8, b8, r8_2, g8_2, b8_2;
	uint16_t pixel12;
	uint32_t i, j, x, y, pixel24;

	pixel24 = palette[PAL_PATCURSOR];
	for (y = 0; y < 14; y++)
	{
		// top two rows have a lighter color
		if (y < 2)
		{
			r8 = R24(pixel24);
			g8 = G24(pixel24);
			b8 = B24(pixel24);

			if (r8 <= 0xFF-0x33)
				r8 += 0x33;
			else
				r8 = 0xFF;

			if (g8 <= 0xFF-0x33)
				g8 += 0x33;
			else
				g8 = 0xFF;

			if (b8 <= 0xFF-0x33)
				b8 += 0x33;
			else
				b8 = 0xFF;

			for (x = 0; x < 11; x++)
				patternCursorBMP[(y * 11) + x] = RGB24(r8, g8, b8);
		}

		// sides (same color)
		if (y >= 2 && y <= 12)
		{
			patternCursorBMP[(y * 11) + 0] = pixel24;

			for (x = 1; x < 10; x++)
				patternCursorBMP[(y * 11) + x] = palette[PAL_COLORKEY];

			patternCursorBMP[(y * 11) + 10] = pixel24;
		}

		// bottom two rows have a darker color
		if (y > 11)
		{
			r8 = R24(pixel24);
			g8 = G24(pixel24);
			b8 = B24(pixel24);

			if (r8 >= 0x33)
				r8 -= 0x33;
			else
				r8 = 0x00;

			if (g8 >= 0x33)
				g8 -= 0x33;
			else
				g8 = 0x00;

			if (b8 >= 0x33)
				b8 -= 0x33;
			else
				b8 = 0x00;

			for (x = 0; x < 11; x++)
				patternCursorBMP[(y * 11) + x] = RGB24(r8, g8, b8);
		}
	}

	// create spectrum analyzer bar graphics
	for (i = 0; i < 36; i++)
		spectrumAnaBMP[i] = RGB12_to_RGB24(analyzerColors[35-i]);

	// create VU-Meter bar graphics
	for (i = 0; i < 48; i++)
	{
		pixel12 = vuMeterColors[47-i];

		r8_2 = r8 = R12_to_R24(pixel12);
		g8_2 = g8 = G12_to_G24(pixel12);
		b8_2 = b8 = B12_to_B24(pixel12);

		// brighter pixels on the left side

		if (r8_2 <= 0xFF-0x33)
			r8_2 += 0x33;
		else
			r8_2 = 0xFF;

		if (g8_2 <= 0xFF-0x33)
			g8_2 += 0x33;
		else
			g8_2 = 0xFF;

		if (b8_2 <= 0xFF-0x33)
			b8_2 += 0x33;
		else
			b8_2 = 0xFF;

		pixel24 = RGB24(r8_2, g8_2, b8_2);

		vuMeterBMP[(i * 10) + 0] = pixel24;
		vuMeterBMP[(i * 10) + 1] = pixel24;

		// main pixels
		for (j = 2; j < 8; j++)
			vuMeterBMP[(i * 10) + j] = RGB24(r8, g8, b8);

		// darker pixels on the right side
		r8_2 = r8;
		g8_2 = g8;
		b8_2 = b8;

		if (r8_2 >= 0x33)
			r8_2 -= 0x33;
		else
			r8_2 = 0x00;

		if (g8_2 >= 0x33)
			g8_2 -= 0x33;
		else
			g8_2 = 0x00;

		if (b8_2 >= 0x33)
			b8_2 -= 0x33;
		else
			b8_2 = 0x00;

		pixel24 = RGB24(r8_2, g8_2, b8_2);

		vuMeterBMP[(i * 10) + 8] = pixel24;
		vuMeterBMP[(i * 10) + 9] = pixel24;
	}

	for (i = 0; i < 30; i++) arrowBMP[i] = palette[arrowPaletteBMP[i]];
	for (i = 0; i < 64; i++) samplingPosBMP[i] = samplingPosBMP[i];
	for (i = 0; i < 512; i++) loopPinsBMP[i] = loopPinsBMP[i];
}

void freeBMPs(void)
{
	if (trackerFrameBMP != NULL) free(trackerFrameBMP);
	if (samplerScreenBMP != NULL) free(samplerScreenBMP);
	if (samplerVolumeBMP != NULL) free(samplerVolumeBMP);
	if (samplerFiltersBMP != NULL) free(samplerFiltersBMP);
	if (clearDialogBMP != NULL) free(clearDialogBMP);
	if (diskOpScreenBMP != NULL) free(diskOpScreenBMP);
	if (mod2wavBMP != NULL) free(mod2wavBMP);
	if (posEdBMP != NULL) free(posEdBMP);
	if (spectrumVisualsBMP != NULL) free(spectrumVisualsBMP);
	if (yesNoDialogBMP != NULL) free(yesNoDialogBMP);
	if (pat2SmpDialogBMP != NULL) free(pat2SmpDialogBMP);
	if (editOpScreen1BMP != NULL) free(editOpScreen1BMP);
	if (editOpScreen2BMP != NULL) free(editOpScreen2BMP);
	if (editOpScreen3BMP != NULL) free(editOpScreen3BMP);
	if (editOpScreen4BMP != NULL) free(editOpScreen4BMP);
	if (aboutScreenBMP != NULL) free(aboutScreenBMP);
	if (muteButtonsBMP != NULL) free(muteButtonsBMP);
	if (editOpModeCharsBMP != NULL) free(editOpModeCharsBMP);
	if (arrowBMP != NULL) free(arrowBMP);
}

uint32_t *unpackBMP(const uint8_t *src, uint32_t packedLen)
{
	const uint8_t *packSrc;
	uint8_t *tmpBuffer, *packDst, byteIn;
	int16_t count;
	uint32_t *dst, decodedLength, i;

	// RLE decode
	decodedLength = (src[0] << 24) | (src[1] << 16) | (src[2] << 8) | src[3];

	// 2-bit to 8-bit conversion
	dst = (uint32_t *)malloc((decodedLength * 4) * sizeof (int32_t));
	if (dst == NULL)
		return NULL;

	tmpBuffer = (uint8_t *)malloc(decodedLength + 512); // some margin is needed, the packer is buggy
	if (tmpBuffer == NULL)
	{
		free(dst);
		return NULL;
	}

	packSrc = src + 4;
	packDst = tmpBuffer;

	i = packedLen - 4;
	while (i > 0)
	{
		byteIn = *packSrc++;
		if (byteIn == 0xCC) // compactor code
		{
			count  = *packSrc++;
			byteIn = *packSrc++;

			while (count-- >= 0)
				*packDst++ = byteIn;

			i -= 2;
		}
		else
		{
			*packDst++ = byteIn;
		}

		i--;
	}

	for (i = 0; i < decodedLength; i++)
	{
		byteIn = (tmpBuffer[i] & 0xC0) >> 6;
		assert(byteIn < PALETTE_NUM);
		dst[(i * 4) + 0] = palette[byteIn];

		byteIn = (tmpBuffer[i] & 0x30) >> 4;
		assert(byteIn < PALETTE_NUM);
		dst[(i * 4) + 1] = palette[byteIn];

		byteIn = (tmpBuffer[i] & 0x0C) >> 2;
		assert(byteIn < PALETTE_NUM);
		dst[(i * 4) + 2] = palette[byteIn];

		byteIn = (tmpBuffer[i] & 0x03) >> 0;
		assert(byteIn < PALETTE_NUM);
		dst[(i * 4) + 3] = palette[byteIn];
	}

	free(tmpBuffer);
	return dst;
}

bool unpackBMPs(void)
{
	trackerFrameBMP = unpackBMP(trackerFramePackedBMP, sizeof (trackerFramePackedBMP));
	samplerScreenBMP = unpackBMP(samplerScreenPackedBMP, sizeof (samplerScreenPackedBMP));
	samplerVolumeBMP = unpackBMP(samplerVolumePackedBMP, sizeof (samplerVolumePackedBMP));
	samplerFiltersBMP = unpackBMP(samplerFiltersPackedBMP, sizeof (samplerFiltersPackedBMP));
	clearDialogBMP = unpackBMP(clearDialogPackedBMP, sizeof (clearDialogPackedBMP));
	diskOpScreenBMP = unpackBMP(diskOpScreenPackedBMP, sizeof (diskOpScreenPackedBMP));
	mod2wavBMP = unpackBMP(mod2wavPackedBMP, sizeof (mod2wavPackedBMP));
	posEdBMP = unpackBMP(posEdPackedBMP, sizeof (posEdPackedBMP));
	spectrumVisualsBMP = unpackBMP(spectrumVisualsPackedBMP, sizeof (spectrumVisualsPackedBMP));
	yesNoDialogBMP = unpackBMP(yesNoDialogPackedBMP, sizeof (yesNoDialogPackedBMP));
	pat2SmpDialogBMP = unpackBMP(pat2SmpDialogPackedBMP, sizeof (pat2SmpDialogPackedBMP));
	editOpScreen1BMP = unpackBMP(editOpScreen1PackedBMP, sizeof (editOpScreen1PackedBMP));
	editOpScreen2BMP = unpackBMP(editOpScreen2PackedBMP, sizeof (editOpScreen2PackedBMP));
	editOpScreen3BMP = unpackBMP(editOpScreen3PackedBMP, sizeof (editOpScreen3PackedBMP));
	editOpScreen4BMP = unpackBMP(editOpScreen4PackedBMP, sizeof (editOpScreen4PackedBMP));
	aboutScreenBMP = unpackBMP(aboutScreenPackedBMP, sizeof (aboutScreenPackedBMP));
	muteButtonsBMP = unpackBMP(muteButtonsPackedBMP, sizeof (muteButtonsPackedBMP));
	editOpModeCharsBMP = unpackBMP(editOpModeCharsPackedBMP, sizeof (editOpModeCharsPackedBMP));

	arrowBMP = (uint32_t *)malloc(30 * sizeof (int32_t)); // different format

	if (trackerFrameBMP    == NULL || samplerScreenBMP   == NULL || samplerVolumeBMP  == NULL ||
		clearDialogBMP     == NULL || diskOpScreenBMP    == NULL || mod2wavBMP        == NULL ||
		posEdBMP           == NULL || spectrumVisualsBMP == NULL || yesNoDialogBMP    == NULL ||
		editOpScreen1BMP   == NULL || editOpScreen2BMP   == NULL || editOpScreen3BMP  == NULL ||
		editOpScreen4BMP   == NULL || aboutScreenBMP     == NULL || muteButtonsBMP    == NULL ||
		editOpModeCharsBMP == NULL || arrowBMP           == NULL || samplerFiltersBMP == NULL ||
		yesNoDialogBMP     == NULL)
	{
		showErrorMsgBox("Out of memory!");
		return false; // BMPs are free'd in cleanUp()
	}

	createBitmaps();
	return true;
}

void videoClose(void)
{
	SDL_DestroyTexture(texture);
	SDL_DestroyRenderer(renderer);
	SDL_DestroyWindow(window);
	free(pixelBuffer);
}

void setupSprites(void)
{
	memset(sprites, 0, sizeof (sprites));

	sprites[SPRITE_MOUSE_POINTER].data = mousePointerBMP;
	sprites[SPRITE_MOUSE_POINTER].pixelType = SPRITE_TYPE_PALETTE;
	sprites[SPRITE_MOUSE_POINTER].colorKey = PAL_COLORKEY;
	sprites[SPRITE_MOUSE_POINTER].w = 16;
	sprites[SPRITE_MOUSE_POINTER].h = 16;
	hideSprite(SPRITE_MOUSE_POINTER);

	sprites[SPRITE_PATTERN_CURSOR].data = patternCursorBMP;
	sprites[SPRITE_PATTERN_CURSOR].pixelType = SPRITE_TYPE_RGB;
	sprites[SPRITE_PATTERN_CURSOR].colorKey = palette[PAL_COLORKEY];
	sprites[SPRITE_PATTERN_CURSOR].w = 11;
	sprites[SPRITE_PATTERN_CURSOR].h = 14;
	hideSprite(SPRITE_PATTERN_CURSOR);

	sprites[SPRITE_LOOP_PIN_LEFT].data = loopPinsBMP;
	sprites[SPRITE_LOOP_PIN_LEFT].pixelType = SPRITE_TYPE_RGB;
	sprites[SPRITE_LOOP_PIN_LEFT].colorKey = palette[PAL_COLORKEY];
	sprites[SPRITE_LOOP_PIN_LEFT].w = 4;
	sprites[SPRITE_LOOP_PIN_LEFT].h = 64;
	hideSprite(SPRITE_LOOP_PIN_LEFT);

	sprites[SPRITE_LOOP_PIN_RIGHT].data = &loopPinsBMP[4 * 64];
	sprites[SPRITE_LOOP_PIN_RIGHT].pixelType = SPRITE_TYPE_RGB;
	sprites[SPRITE_LOOP_PIN_RIGHT].colorKey = palette[PAL_COLORKEY];
	sprites[SPRITE_LOOP_PIN_RIGHT].w = 4;
	sprites[SPRITE_LOOP_PIN_RIGHT].h = 64;
	hideSprite(SPRITE_LOOP_PIN_RIGHT);

	sprites[SPRITE_SAMPLING_POS_LINE].data = samplingPosBMP;
	sprites[SPRITE_SAMPLING_POS_LINE].pixelType = SPRITE_TYPE_RGB;
	sprites[SPRITE_SAMPLING_POS_LINE].colorKey = palette[PAL_COLORKEY];
	sprites[SPRITE_SAMPLING_POS_LINE].w = 1;
	sprites[SPRITE_SAMPLING_POS_LINE].h = 64;
	hideSprite(SPRITE_SAMPLING_POS_LINE);

	// setup refresh buffer (used to clear sprites after each frame)
	for (uint32_t i = 0; i < SPRITE_NUM; i++)
		sprites[i].refreshBuffer = (uint32_t *)malloc((sprites[i].w * sprites[i].h) * sizeof (int32_t));
}

void freeSprites(void)
{
	for (uint8_t i = 0; i < SPRITE_NUM; i++)
		free(sprites[i].refreshBuffer);
}

void setSpritePos(uint8_t sprite, uint16_t x, uint16_t y)
{
	sprites[sprite].newX = x;
	sprites[sprite].newY = y;
}

void hideSprite(uint8_t sprite)
{
	sprites[sprite].newX = SCREEN_W;
}

void eraseSprites(void)
{
	int32_t sw, sh, srcPitch, dstPitch;
	const uint32_t *src32;
	uint32_t *dst32;
	sprite_t *s;

	for (int32_t i = SPRITE_NUM-1; i >= 0; i--) // erasing must be done in reverse order
	{
		s = &sprites[i];
		if (s->x >= SCREEN_W) // sprite is hidden, don't erase
			continue;

		assert(s->x >= 0 && s->y >= 0 && s->refreshBuffer != NULL);

		sw = s->w;
		sh = s->h;
		dst32 = &pixelBuffer[(s->y * SCREEN_W) + s->x];
		src32 = s->refreshBuffer;

		// handle xy clipping
		if (s->y+sh >= SCREEN_H) sh = SCREEN_H - s->y;
		if (s->x+sw >= SCREEN_W) sw = SCREEN_W - s->x;

		srcPitch = s->w - sw;
		dstPitch = SCREEN_W - sw;

		for (int32_t y = 0; y < sh; y++)
		{
			for (int32_t x = 0; x < sw; x++)
				*dst32++ = *src32++;

			src32 += srcPitch;
			dst32 += dstPitch;
		}
	}

	fillFromVuMetersBgBuffer(); // let's put it here even though it's not sprite-based
}

void renderSprites(void)
{
	const uint8_t *src8;
	int32_t x, y, sw, sh, srcPitch, dstPitch;
	const uint32_t *src32;
	uint32_t *dst32, *clr32;
	register uint32_t colorKey;
	sprite_t *s;

	renderVuMeters(); // let's put it here even though it's not sprite-based

	for (int32_t i = 0; i < SPRITE_NUM; i++)
	{
		s = &sprites[i];

		// set new sprite position
		s->x = s->newX;
		s->y = s->newY;

		if (s->x >= SCREEN_W) // sprite is hidden, don't draw nor fill clear buffer
			continue;

		assert(s->x >= 0 && s->y >= 0 && s->data != NULL && s->refreshBuffer != NULL);

		sw = s->w;
		sh = s->h;
		dst32 = &pixelBuffer[(s->y * SCREEN_W) + s->x];
		clr32 = s->refreshBuffer;

		// handle xy clipping
		if (s->y+sh >= SCREEN_H) sh = SCREEN_H - s->y;
		if (s->x+sw >= SCREEN_W) sw = SCREEN_W - s->x;

		srcPitch = s->w - sw;
		dstPitch = SCREEN_W - sw;

		colorKey = sprites[i].colorKey;
		if (sprites[i].pixelType == SPRITE_TYPE_RGB)
		{
			// 24-bit RGB sprite
			src32 = (uint32_t *)sprites[i].data;
			for (y = 0; y < sh; y++)
			{
				for (x = 0; x < sw; x++)
				{
					*clr32++ = *dst32; // fill clear buffer
					if (*src32 != colorKey)
						*dst32 = *src32;

					dst32++;
					src32++;
				}

				clr32 += srcPitch;
				src32 += srcPitch;
				dst32 += dstPitch;
			}
		}
		else
		{
			// 8-bit paletted sprite
			src8 = (uint8_t *)sprites[i].data;
			for (y = 0; y < sh; y++)
			{
				for (x = 0; x < sw; x++)
				{
					*clr32++ = *dst32; // fill clear buffer
					if (*src8 != colorKey)
					{
						assert(*src8 < PALETTE_NUM);
						*dst32 = palette[*src8];
					}

					dst32++;
					src8++;
				}

				clr32 += srcPitch;
				src8 += srcPitch;
				dst32 += dstPitch;
			}
		}
	}
}

void flipFrame(void)
{
	uint32_t windowFlags = SDL_GetWindowFlags(window);

	renderSprites();
	SDL_UpdateTexture(texture, NULL, pixelBuffer, SCREEN_W * sizeof (int32_t));
	SDL_RenderClear(renderer);
	SDL_RenderCopy(renderer, texture, NULL, NULL);
	SDL_RenderPresent(renderer);
	eraseSprites();

	if (!editor.ui.vsync60HzPresent)
	{
		waitVBL(); // we have no VSync, do crude thread sleeping to sync to ~60Hz
	}
	else
	{
		/* We have VSync, but it can unexpectedly get inactive in certain scenarios.
		** We have to force thread sleeping (to ~60Hz) if so.
		*/
#ifdef __APPLE__
		// macOS: VSync gets disabled if the window is 100% covered by another window. Let's add a (crude) fix:
		if ((windowFlags & SDL_WINDOW_MINIMIZED) || !(windowFlags & SDL_WINDOW_INPUT_FOCUS))
			waitVBL();
#elif __unix__
		// *NIX: VSync gets disabled in fullscreen mode (at least on some distros/systems). Let's add a fix:
		if ((windowFlags & SDL_WINDOW_MINIMIZED) || editor.fullscreen)
			waitVBL();
#else
		if (windowFlags & SDL_WINDOW_MINIMIZED)
			waitVBL();
#endif
	}
}

void updateSpectrumAnalyzer(int8_t vol, int16_t period)
{
	const uint8_t maxHeight = SPECTRUM_BAR_HEIGHT + 1; // +1 because of audio latency - allows full height to be seen
	int16_t scaledVol;
	int32_t scaledNote;

	if (editor.ui.visualizerMode != VISUAL_SPECTRUM || vol <= 0)
		return;

	scaledVol = (vol * 256) / ((64 * 256) / (SPECTRUM_BAR_NUM+1)); // 64 = max sample vol

	period = CLAMP(period, 113, 856);

	// 856 = C-1 period, 113 = B-3 period
	scaledNote = (856-113) - (period - 113);
	scaledNote *= scaledNote;
	scaledNote /= ((856 - 113) * (856 - 113)) / (SPECTRUM_BAR_NUM-1);

	// scaledNote now ranges 0..22, no need to clamp

	// increment main spectrum bar
	editor.spectrumVolumes[scaledNote] += scaledVol;
	if (editor.spectrumVolumes[scaledNote] > maxHeight)
		editor.spectrumVolumes[scaledNote] = maxHeight;

	// increment left side of spectrum bar with half volume
	if (scaledNote > 0)
	{
		editor.spectrumVolumes[scaledNote-1] += scaledVol >> 1;
		if (editor.spectrumVolumes[scaledNote-1] > maxHeight)
			editor.spectrumVolumes[scaledNote-1] = maxHeight;
	}

	// increment right side of spectrum bar with half volume
	if (scaledNote < SPECTRUM_BAR_NUM-1)
	{
		editor.spectrumVolumes[scaledNote+1] += scaledVol >> 1;
		if (editor.spectrumVolumes[scaledNote+1] > maxHeight)
			editor.spectrumVolumes[scaledNote+1] = maxHeight;
	}
}

void sinkVisualizerBars(void)
{
	// sink stuff @ 50Hz rate

	const uint64_t _50HzCounterDelta = ((uint64_t)AMIGA_PAL_VBLANK_HZ << 32) / VBLANK_HZ;

	_50HzCounter += _50HzCounterDelta; // 32.32 fixed-point counter
	if (_50HzCounter > 0xFFFFFFFF)
	{
		_50HzCounter &= 0xFFFFFFFF;

		// sink VU-meters
		for (uint32_t i = 0; i < AMIGA_VOICES; i++)
		{
			if (editor.vuMeterVolumes[i] > 0)
				editor.vuMeterVolumes[i]--;
		}

		// sink "spectrum analyzer" bars
		for (uint32_t i = 0; i < SPECTRUM_BAR_NUM; i++)
		{
			if (editor.spectrumVolumes[i] > 0)
				editor.spectrumVolumes[i]--;
		}
	}
}

void updateRenderSizeVars(void)
{
	int32_t di;
#ifdef __APPLE__
	int32_t actualScreenW, actualScreenH;
	double dXUpscale, dYUpscale;
#endif
	float fXScale, fYScale;
	SDL_DisplayMode dm;

	di = SDL_GetWindowDisplayIndex(window);
	if (di < 0)
		di = 0; /* return display index 0 (default) on error */

	SDL_GetDesktopDisplayMode(di, &dm);
	editor.ui.displayW = dm.w;
	editor.ui.displayH = dm.h;

	if (editor.fullscreen)
	{
		if (ptConfig.fullScreenStretch)
		{
			editor.ui.renderW = editor.ui.displayW;
			editor.ui.renderH = editor.ui.displayH;
			editor.ui.renderX = 0;
			editor.ui.renderY = 0;
		}
		else
		{
			SDL_RenderGetScale(renderer, &fXScale, &fYScale);

			editor.ui.renderW = (int32_t)(SCREEN_W * fXScale);
			editor.ui.renderH = (int32_t)(SCREEN_H * fYScale);

#ifdef __APPLE__
			// retina high-DPI hackery (SDL2 is bad at reporting actual rendering sizes on macOS w/ high-DPI)
			SDL_GL_GetDrawableSize(window, &actualScreenW, &actualScreenH);
			SDL_GetDesktopDisplayMode(0, &dm);

			dXUpscale = ((double)actualScreenW / editor.ui.displayW);
			dYUpscale = ((double)actualScreenH / editor.ui.displayH);

			// downscale back to correct sizes
			if (dXUpscale != 0.0) editor.ui.renderW = (int32_t)(editor.ui.renderW / dXUpscale);
			if (dYUpscale != 0.0) editor.ui.renderH = (int32_t)(editor.ui.renderH / dYUpscale);
#endif
			editor.ui.renderX = (editor.ui.displayW - editor.ui.renderW) / 2;
			editor.ui.renderY = (editor.ui.displayH - editor.ui.renderH) / 2;
		}
	}
	else
	{
		SDL_GetWindowSize(window, &editor.ui.renderW, &editor.ui.renderH);

		editor.ui.renderX = 0;
		editor.ui.renderY = 0;
	}

	// for mouse cursor creation
	editor.ui.xScale = (uint32_t)round(editor.ui.renderW / (double)SCREEN_W);
	editor.ui.yScale = (uint32_t)round(editor.ui.renderH / (double)SCREEN_H);
	createMouseCursors();
}

void toggleFullScreen(void)
{
	SDL_DisplayMode dm;

	editor.fullscreen ^= 1;
	if (editor.fullscreen)
	{
		if (ptConfig.fullScreenStretch)
		{
			SDL_GetDesktopDisplayMode(0, &dm);
			SDL_RenderSetLogicalSize(renderer, dm.w, dm.h);
		}
		else
		{
			SDL_RenderSetLogicalSize(renderer, SCREEN_W, SCREEN_H);
		}

		SDL_SetWindowSize(window, SCREEN_W, SCREEN_H);
		SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN_DESKTOP);
		SDL_SetWindowGrab(window, SDL_TRUE);
	}
	else
	{
		SDL_SetWindowFullscreen(window, 0);
		SDL_RenderSetLogicalSize(renderer, SCREEN_W, SCREEN_H);
		SDL_SetWindowSize(window, SCREEN_W * ptConfig.videoScaleFactor, SCREEN_H * ptConfig.videoScaleFactor);
		SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
		SDL_SetWindowGrab(window, SDL_FALSE);
	}

	updateRenderSizeVars();
	updateMouseScaling();

	if (editor.fullscreen)
	{
		input.mouse.setPosX = editor.ui.displayW / 2;
		input.mouse.setPosY = editor.ui.displayH / 2;
	}
	else
	{
		input.mouse.setPosX = editor.ui.renderW / 2;
		input.mouse.setPosY = editor.ui.renderH / 2;
	}

	input.mouse.setPosFlag = true;
}

bool setupVideo(void)
{
	int32_t screenW, screenH;
	uint32_t rendererFlags;
	SDL_DisplayMode dm;

	screenW = SCREEN_W * ptConfig.videoScaleFactor;
	screenH = SCREEN_H * ptConfig.videoScaleFactor;

	rendererFlags = 0;

#ifdef _WIN32
#if SDL_PATCHLEVEL >= 4
	SDL_SetHint(SDL_HINT_WINDOWS_NO_CLOSE_ON_ALT_F4, "1"); // this is for Windows only
#endif
#endif

#if SDL_PATCHLEVEL >= 5
	SDL_SetHint(SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH, "1");
#endif

	editor.ui.vsync60HzPresent = false;
	if (!ptConfig.vsyncOff)
	{
		SDL_GetDesktopDisplayMode(0, &dm);
		if (dm.refresh_rate >= 59 && dm.refresh_rate <= 61)
		{
			editor.ui.vsync60HzPresent = true;
			rendererFlags |= SDL_RENDERER_PRESENTVSYNC;
		}
	}

	window = SDL_CreateWindow("", SDL_WINDOWPOS_CENTERED,
		SDL_WINDOWPOS_CENTERED, screenW, screenH,
		SDL_WINDOW_HIDDEN | SDL_WINDOW_ALLOW_HIGHDPI);

	if (window == NULL)
	{
		showErrorMsgBox("Couldn't create SDL window:\n%s", SDL_GetError());
		return false;
	}

	renderer = SDL_CreateRenderer(window, -1, rendererFlags);
	if (renderer == NULL)
	{
		if (editor.ui.vsync60HzPresent) // try again without vsync flag
		{
			editor.ui.vsync60HzPresent = false;
			rendererFlags &= ~SDL_RENDERER_PRESENTVSYNC;
			renderer = SDL_CreateRenderer(window, -1, rendererFlags);
		}

		if (renderer == NULL)
		{
			showErrorMsgBox("Couldn't create SDL renderer:\n%s\n\n" \
			                "Is your GPU (+ driver) too old?", SDL_GetError());
			return false;
		}
	}

	SDL_RenderSetLogicalSize(renderer, SCREEN_W, SCREEN_H);

#if SDL_PATCHLEVEL >= 5
	SDL_RenderSetIntegerScale(renderer, SDL_TRUE);
#endif

	SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);

	SDL_SetHint("SDL_RENDER_SCALE_QUALITY", "nearest");

	texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, SCREEN_W, SCREEN_H);
	if (texture == NULL)
	{
		showErrorMsgBox("Couldn't create %dx%d GPU texture:\n%s\n\n" \
		                "Is your GPU (+ driver) too old?", SCREEN_W, SCREEN_H, SDL_GetError());
		return false;
	}

	SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_NONE);

	// frame buffer used by SDL (for texture)
	pixelBuffer = (uint32_t *)malloc(SCREEN_W * SCREEN_H * sizeof (int32_t));
	if (pixelBuffer == NULL)
	{
		showErrorMsgBox("Out of memory!");
		return false;
	}

	updateRenderSizeVars();
	updateMouseScaling();

	if (ptConfig.hwMouse)
		SDL_ShowCursor(SDL_TRUE);
	else
		SDL_ShowCursor(SDL_FALSE);

	return true;
}
