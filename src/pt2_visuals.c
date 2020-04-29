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
#include "pt2_pat2smp.h"
#include "pt2_mod2wav.h"

typedef struct sprite_t
{
	bool visible;
	int8_t pixelType;
	int16_t newX, newY, x, y, w, h;
	uint32_t colorKey, *refreshBuffer;
	const void *data;
} sprite_t;

static uint32_t vuMetersBg[4 * (10 * 48)];
static uint64_t timeNext64, timeNext64Frac;

sprite_t sprites[SPRITE_NUM]; // globalized

static const uint16_t cursorPosTable[24] =
{
	 30,  54,  62,  70,  78,  86,
	102, 126, 134, 142, 150, 158,
	174, 198, 206, 214, 222, 230,
	246, 270, 278, 286, 294, 302
};


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
	editor.vblankTimeLen = (int32_t)dInt;

	// fractional part scaled to 0..2^32-1
	dFrac *= UINT32_MAX;
	dFrac += 0.5;
	if (dFrac > UINT32_MAX)
		dFrac = UINT32_MAX;
	editor.vblankTimeLenFrac = (uint32_t)dFrac;
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
		time32 = (int32_t)((diff32 * editor.dPerfFreqMulMicro) + 0.5);

		// delay until we have reached next tick
		if (time32 > 0)
			usleep(time32);
	}

	// update next tick time
	timeNext64 += editor.vblankTimeLen;
	timeNext64Frac += editor.vblankTimeLenFrac;
	if (timeNext64Frac > 0xFFFFFFFF)
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
	handleLastGUIObjectDown();
	drawSamplerLine();
}

void resetAllScreens(void)
{
	editor.mixFlag = false;
	editor.swapChannelFlag = false;
	ui.clearScreenShown = false;
	ui.changingChordNote = false;
	ui.changingSmpResample = false;
	ui.pat2SmpDialogShown = false;
	ui.disablePosEd = false;
	ui.disableVisualizer = false;

	if (ui.samplerScreenShown)
	{
		ui.samplerVolBoxShown = false;
		ui.samplerFiltersBoxShown = false;

		displaySample();
	}

	if (ui.editTextFlag)
		exitGetTextLine(EDIT_TEXT_NO_UPDATE);
}

void removeAskDialog(void)
{
	if (!ui.askScreenShown && !editor.isWAVRendering)
		displayMainScreen();

	ui.disablePosEd = false;
	ui.disableVisualizer = false;
}

void renderAskDialog(void)
{
	const uint32_t *srcPtr;
	uint32_t *dstPtr;

	ui.disablePosEd = true;
	ui.disableVisualizer = true;

	// render ask dialog

	srcPtr = ui.pat2SmpDialogShown ? pat2SmpDialogBMP : yesNoDialogBMP;
	dstPtr = &video.frameBuffer[(51 * SCREEN_W) + 160];

	for (uint32_t y = 0; y < 39; y++)
	{
		memcpy(dstPtr, srcPtr, 104 * sizeof (int32_t));

		srcPtr += 104;
		dstPtr += SCREEN_W;
	}
}

void renderBigAskDialog(void)
{
	const uint32_t *srcPtr;
	uint32_t *dstPtr;

	ui.disablePosEd = true;
	ui.disableVisualizer = true;

	// render custom big ask dialog

	srcPtr = bigYesNoDialogBMP;
	dstPtr = &video.frameBuffer[(44 * SCREEN_W) + 120];

	for (uint32_t y = 0; y < 55; y++)
	{
		memcpy(dstPtr, srcPtr, 200 * sizeof (int32_t));

		srcPtr += 200;
		dstPtr += SCREEN_W;
	}
}

void showDownsampleAskDialog(void)
{
	ui.askScreenShown = true;
	ui.askScreenType = ASK_LOAD_DOWNSAMPLE;
	pointerSetMode(POINTER_MODE_MSG1, NO_CARRY);
	setStatusMessage("PLEASE SELECT", NO_CARRY);
	renderBigAskDialog();

	textOutTight(133, 49, "THE SAMPLE'S FREQUENCY IS", video.palette[PAL_BACKGRD]);
	textOutTight(178, 57, "ABOVE 22KHZ.", video.palette[PAL_BACKGRD]);
	textOutTight(133, 65, "DO YOU WANT TO DOWNSAMPLE", video.palette[PAL_BACKGRD]);
	textOutTight(156, 73, "BEFORE LOADING IT?", video.palette[PAL_BACKGRD]);
}

static void fillFromVuMetersBgBuffer(void)
{
	const uint32_t *srcPtr;
	uint32_t *dstPtr;

	if (ui.samplerScreenShown || editor.isWAVRendering || editor.isSMPRendering)
		return;

	srcPtr = vuMetersBg;
	dstPtr = &video.frameBuffer[(187 * SCREEN_W) + 55];

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

	if (ui.samplerScreenShown || editor.isWAVRendering || editor.isSMPRendering)
		return;

	srcPtr = &video.frameBuffer[(187 * SCREEN_W) + 55];
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

	if (ui.samplerScreenShown || editor.isWAVRendering || editor.isSMPRendering)
		return;

	fillToVuMetersBgBuffer();
	
	dstPtr = &video.frameBuffer[(187 * SCREEN_W) + 55];
	for (uint32_t i = 0; i < AMIGA_VOICES; i++)
	{
		if (config.realVuMeters)
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

	if (ui.diskOpScreenShown)
		return;

	currSample = &modEntry->samples[editor.currSample];

	if (ui.updateSongPos)
	{
		ui.updateSongPos = false;
		printThreeDecimalsBg(72, 3, *editor.currPosDisp, video.palette[PAL_GENTXT], video.palette[PAL_GENBKG]);
	}

	if (ui.updateSongPattern)
	{
		ui.updateSongPattern = false;
		printTwoDecimalsBg(80, 14, *editor.currPatternDisp, video.palette[PAL_GENTXT], video.palette[PAL_GENBKG]);
	}

	if (ui.updateSongLength)
	{
		ui.updateSongLength = false;
		if (!editor.isWAVRendering)
			printThreeDecimalsBg(72, 25, *editor.currLengthDisp, video.palette[PAL_GENTXT], video.palette[PAL_GENBKG]);
	}

	if (ui.updateCurrSampleFineTune)
	{
		ui.updateCurrSampleFineTune = false;

		if (!editor.isWAVRendering)
		{
			if (currSample->fineTune >= 8)
			{
				charOutBg(80, 36, '-', video.palette[PAL_GENTXT], video.palette[PAL_GENBKG]);
				charOutBg(88, 36, '0' + (0x10 - (currSample->fineTune & 0xF)), video.palette[PAL_GENTXT], video.palette[PAL_GENBKG]);
			}
			else if (currSample->fineTune > 0)
			{
				charOutBg(80, 36, '+', video.palette[PAL_GENTXT], video.palette[PAL_GENBKG]);
				charOutBg(88, 36, '0' + (currSample->fineTune & 0xF), video.palette[PAL_GENTXT], video.palette[PAL_GENBKG]);
			}
			else
			{
				charOutBg(80, 36, ' ', video.palette[PAL_GENBKG], video.palette[PAL_GENBKG]);
				charOutBg(88, 36, '0', video.palette[PAL_GENTXT], video.palette[PAL_GENBKG]);
			}
		}
	}

	if (ui.updateCurrSampleNum)
	{
		ui.updateCurrSampleNum = false;
		if (!editor.isWAVRendering)
		{
			printTwoHexBg(80, 47,
				editor.sampleZero ? 0 : ((*editor.currSampleDisp) + 1), video.palette[PAL_GENTXT], video.palette[PAL_GENBKG]);
		}
	}

	if (ui.updateCurrSampleVolume)
	{
		ui.updateCurrSampleVolume = false;
		if (!editor.isWAVRendering)
			printTwoHexBg(80, 58, *currSample->volumeDisp, video.palette[PAL_GENTXT], video.palette[PAL_GENBKG]);
	}

	if (ui.updateCurrSampleLength)
	{
		ui.updateCurrSampleLength = false;
		if (!editor.isWAVRendering)
			printFourHexBg(64, 69, *currSample->lengthDisp, video.palette[PAL_GENTXT], video.palette[PAL_GENBKG]);
	}

	if (ui.updateCurrSampleRepeat)
	{
		ui.updateCurrSampleRepeat = false;
		printFourHexBg(64, 80, *currSample->loopStartDisp, video.palette[PAL_GENTXT], video.palette[PAL_GENBKG]);
	}

	if (ui.updateCurrSampleReplen)
	{
		ui.updateCurrSampleReplen = false;
		printFourHexBg(64, 91, *currSample->loopLengthDisp, video.palette[PAL_GENTXT], video.palette[PAL_GENBKG]);
	}
}

void updateSongInfo2(void) // two middle rows of screen, always present
{
	char tempChar;
	int32_t secs, MI_TimeM, MI_TimeS, x, i;
	moduleSample_t *currSample;

	if (ui.updateStatusText)
	{
		ui.updateStatusText = false;

		// clear background
		textOutBg(88, 127, "                 ", video.palette[PAL_GENBKG], video.palette[PAL_GENBKG]);

		// render status text
		if (!editor.errorMsgActive && editor.blockMarkFlag && !ui.askScreenShown
			&& !ui.clearScreenShown && !editor.swapChannelFlag)
		{
			textOut(88, 127, "MARK BLOCK", video.palette[PAL_GENTXT]);
			charOut(192, 127, '-', video.palette[PAL_GENTXT]);

			editor.blockToPos = modEntry->currRow;
			if (editor.blockFromPos >= editor.blockToPos)
			{
				printTwoDecimals(176, 127, editor.blockToPos, video.palette[PAL_GENTXT]);
				printTwoDecimals(200, 127, editor.blockFromPos, video.palette[PAL_GENTXT]);
			}
			else
			{
				printTwoDecimals(176, 127, editor.blockFromPos, video.palette[PAL_GENTXT]);
				printTwoDecimals(200, 127, editor.blockToPos, video.palette[PAL_GENTXT]);
			}
		}
		else
		{
			textOut(88, 127, ui.statusMessage, video.palette[PAL_GENTXT]);
		}
	}

	if (ui.updateSongBPM)
	{
		ui.updateSongBPM = false;
		if (!ui.samplerScreenShown)
			printThreeDecimalsBg(32, 123, modEntry->currBPM, video.palette[PAL_GENTXT], video.palette[PAL_GENBKG]);
	}

	if (ui.updateCurrPattText)
	{
		ui.updateCurrPattText = false;
		if (!ui.samplerScreenShown)
			printTwoDecimalsBg(8, 127, *editor.currEditPatternDisp, video.palette[PAL_GENTXT], video.palette[PAL_GENBKG]);
	}

	if (ui.updateTrackerFlags)
	{
		ui.updateTrackerFlags = false;

		charOutBg(1, 113, ' ', video.palette[PAL_GENTXT], video.palette[PAL_GENBKG]);
		charOutBg(8, 113, ' ', video.palette[PAL_GENTXT], video.palette[PAL_GENBKG]);

		if (editor.autoInsFlag)
		{
			charOut(0, 113, 'I', video.palette[PAL_GENTXT]);

			// in Amiga PT, "auto insert" 9 means 0
			if (editor.autoInsSlot == 9)
				charOut(8, 113, '0', video.palette[PAL_GENTXT]);
			else
				charOut(8, 113, '1' + editor.autoInsSlot, video.palette[PAL_GENTXT]);
		}

		charOutBg(1, 102, ' ', video.palette[PAL_GENTXT], video.palette[PAL_GENBKG]);
		if (editor.metroFlag)
			charOut(0, 102, 'M', video.palette[PAL_GENTXT]);

		charOutBg(16, 102, ' ', video.palette[PAL_GENTXT], video.palette[PAL_GENBKG]);
		if (editor.multiFlag)
			charOut(16, 102, 'M', video.palette[PAL_GENTXT]);

		charOutBg(24, 102, '0' + editor.editMoveAdd, video.palette[PAL_GENTXT], video.palette[PAL_GENBKG]);

		charOutBg(311, 128, ' ', video.palette[PAL_GENBKG], video.palette[PAL_GENBKG]);
		if (editor.pNoteFlag == 1)
		{
			video.frameBuffer[(129 * SCREEN_W) + 314] = video.palette[PAL_GENTXT];
			video.frameBuffer[(129 * SCREEN_W) + 315] = video.palette[PAL_GENTXT];
		}
		else if (editor.pNoteFlag == 2)
		{
			video.frameBuffer[(128 * SCREEN_W) + 314] = video.palette[PAL_GENTXT];
			video.frameBuffer[(128 * SCREEN_W) + 315] = video.palette[PAL_GENTXT];
			video.frameBuffer[(130 * SCREEN_W) + 314] = video.palette[PAL_GENTXT];
			video.frameBuffer[(130 * SCREEN_W) + 315] = video.palette[PAL_GENTXT];
		}
	}

	// playback timer

	secs = ((editor.musicTime >> 8) * 5) >> 9;
	secs -= ((secs / 3600) * 3600);

	if (secs <= 5999) // below 99 minutes 59 seconds
	{
		MI_TimeM = secs / 60;
		MI_TimeS = secs - (MI_TimeM * 60);

		// xx:xx
		printTwoDecimalsBg(272, 102, MI_TimeM, video.palette[PAL_GENTXT], video.palette[PAL_GENBKG]);
		printTwoDecimalsBg(296, 102, MI_TimeS, video.palette[PAL_GENTXT], video.palette[PAL_GENBKG]);
	}
	else
	{
		// 99:59
		printTwoDecimalsBg(272, 102, 99, video.palette[PAL_GENTXT], video.palette[PAL_GENBKG]);
		printTwoDecimalsBg(296, 102, 59, video.palette[PAL_GENTXT], video.palette[PAL_GENBKG]);
	}

	if (ui.updateSongName)
	{
		ui.updateSongName = false;
		for (x = 0; x < 20; x++)
		{
			tempChar = modEntry->head.moduleTitle[x];
			if (tempChar == '\0')
				tempChar = '_';

			charOutBg(104 + (x * FONT_CHAR_W), 102, tempChar, video.palette[PAL_GENTXT], video.palette[PAL_GENBKG]);
		}
	}

	if (ui.updateCurrSampleName)
	{
		ui.updateCurrSampleName = false;
		currSample = &modEntry->samples[editor.currSample];

		for (x = 0; x < 22; x++)
		{
			tempChar = currSample->text[x];
			if (tempChar == '\0')
				tempChar = '_';

			charOutBg(104 + (x * FONT_CHAR_W), 113, tempChar, video.palette[PAL_GENTXT], video.palette[PAL_GENBKG]);
		}
	}

	if (ui.updateSongSize)
	{
		ui.updateSongSize = false;

		// clear background
		textOutBg(264, 123, "      ", video.palette[PAL_GENBKG], video.palette[PAL_GENBKG]);

		// calculate module length
		uint32_t totalSampleDataSize = 0;
		for (i = 0; i < MOD_SAMPLES; i++)
			totalSampleDataSize += modEntry->samples[i].length;

		uint32_t totalPatterns = 0;
		for (i = 0; i < MOD_ORDERS; i++)
		{
			if (modEntry->head.order[i] > totalPatterns)
				totalPatterns = modEntry->head.order[i];
		}

		uint32_t moduleSize = 2108 + (totalPatterns * 1024) + totalSampleDataSize;
		if (moduleSize > 999999)
		{
			charOut(304, 123, 'K', video.palette[PAL_GENTXT]);
			printFourDecimals(272, 123, moduleSize / 1000, video.palette[PAL_GENTXT]);
		}
		else
		{
			printSixDecimals(264, 123, moduleSize, video.palette[PAL_GENTXT]);
		}
	}

	if (ui.updateSongTiming)
	{
		ui.updateSongTiming = false;
		textOutBg(288, 130, (editor.timingMode == TEMPO_MODE_CIA) ? "CIA" : "VBL", video.palette[PAL_GENTXT], video.palette[PAL_GENBKG]);
	}
}

void updateCursorPos(void)
{
	if (!ui.samplerScreenShown)
		setSpritePos(SPRITE_PATTERN_CURSOR, cursorPosTable[cursor.pos], 188);
}

void updateSampler(void)
{
	int32_t tmpSampleOffset;
	moduleSample_t *s;

	if (!ui.samplerScreenShown)
		return;

	assert(editor.currSample >= 0 && editor.currSample <= 30);
	s = &modEntry->samples[editor.currSample];

	// update 9xx offset
	if (mouse.y >= 138 && mouse.y <= 201 && mouse.x >= 3 && mouse.x <= 316)
	{
		if (!ui.samplerVolBoxShown && !ui.samplerFiltersBoxShown && s->length > 0)
		{
			tmpSampleOffset = (scr2SmpPos(mouse.x-3) + (1 << 7)) >> 8; // rounded
			tmpSampleOffset = 0x900 + CLAMP(tmpSampleOffset, 0x00, 0xFF);

			if (tmpSampleOffset != ui.lastSampleOffset)
			{
				ui.lastSampleOffset = (uint16_t)tmpSampleOffset;
				ui.update9xxPos = true;
			}
		}
	}

	// display 9xx offset
	if (ui.update9xxPos)
	{
		ui.update9xxPos = false;
		printThreeHexBg(288, 247, ui.lastSampleOffset, video.palette[PAL_GENTXT], video.palette[PAL_GENBKG]);
	}

	if (ui.updateResampleNote)
	{
		ui.updateResampleNote = false;

		// show resample note
		if (ui.changingSmpResample)
		{
			textOutBg(288, 236, "---", video.palette[PAL_GENTXT], video.palette[PAL_GENBKG]);
		}
		else
		{
			assert(editor.resampleNote < 36);
			textOutBg(288, 236,
				config.accidental ? noteNames2[2+editor.resampleNote] : noteNames1[2+editor.resampleNote],
				video.palette[PAL_GENTXT], video.palette[PAL_GENBKG]);
		}
	}

	if (ui.samplerVolBoxShown)
	{
		if (ui.updateVolFromText)
		{
			ui.updateVolFromText = false;
			printThreeDecimalsBg(176, 157, *editor.vol1Disp, video.palette[PAL_GENTXT], video.palette[PAL_GENBKG]);
		}

		if (ui.updateVolToText)
		{
			ui.updateVolToText = false;
			printThreeDecimalsBg(176, 168, *editor.vol2Disp, video.palette[PAL_GENTXT], video.palette[PAL_GENBKG]);
		}
	}
	else if (ui.samplerFiltersBoxShown)
	{
		if (ui.updateLPText)
		{
			ui.updateLPText = false;
			printFourDecimalsBg(168, 157, *editor.lpCutOffDisp, video.palette[PAL_GENTXT], video.palette[PAL_GENBKG]);
		}

		if (ui.updateHPText)
		{
			ui.updateHPText = false;
			printFourDecimalsBg(168, 168, *editor.hpCutOffDisp, video.palette[PAL_GENTXT], video.palette[PAL_GENBKG]);
		}

		if (ui.updateNormFlag)
		{
			ui.updateNormFlag = false;

			if (editor.normalizeFiltersFlag)
				textOutBg(208, 179, "YES", video.palette[PAL_GENTXT], video.palette[PAL_GENBKG]);
			else
				textOutBg(208, 179, "NO ", video.palette[PAL_GENTXT], video.palette[PAL_GENBKG]);
		}
	}
}

void showVolFromSlider(void)
{
	uint32_t *dstPtr, pixel, bgPixel, sliderStart, sliderEnd;

	sliderStart = ((editor.vol1 * 3) + 5) / 10;
	sliderEnd  = sliderStart + 4;
	pixel = video.palette[PAL_QADSCP];
	bgPixel = video.palette[PAL_BACKGRD];
	dstPtr = &video.frameBuffer[(158 * SCREEN_W) + 105];

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

	sliderStart = ((editor.vol2 * 3) + 5) / 10;
	sliderEnd = sliderStart + 4;
	pixel = video.palette[PAL_QADSCP];
	bgPixel = video.palette[PAL_BACKGRD];
	dstPtr = &video.frameBuffer[(169 * SCREEN_W) + 105];

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
	dstPtr = &video.frameBuffer[(154 * SCREEN_W) + 72];

	for (uint32_t y = 0; y < 33; y++)
	{
		memcpy(dstPtr, srcPtr, 136 * sizeof (int32_t));

		srcPtr += 136;
		dstPtr += SCREEN_W;
	}

	ui.updateVolFromText = true;
	ui.updateVolToText = true;
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
	dstPtr = &video.frameBuffer[(154 * SCREEN_W) + 65];

	for (uint32_t y = 0; y < 33; y++)
	{
		memcpy(dstPtr, srcPtr, 186 * sizeof (int32_t));

		srcPtr += 186;
		dstPtr += SCREEN_W;
	}

	textOut(200, 157, "HZ", video.palette[PAL_GENTXT]);
	textOut(200, 168, "HZ", video.palette[PAL_GENTXT]);

	ui.updateLPText = true;
	ui.updateHPText = true;
	ui.updateNormFlag = true;

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
	memcpy(video.frameBuffer, diskOpScreenBMP, (99 * 320) * sizeof (int32_t));

	ui.updateDiskOpPathText = true;
	ui.updatePackText = true;
	ui.updateSaveFormatText = true;
	ui.updateLoadMode = true;
	ui.updateDiskOpFileList = true;
}

void updateDiskOp(void)
{
	char tmpChar;

	if (!ui.diskOpScreenShown || ui.posEdScreenShown)
		return;

	if (ui.updateDiskOpFileList)
	{
		ui.updateDiskOpFileList = false;
		diskOpRenderFileList();
	}

	if (ui.updateLoadMode)
	{
		ui.updateLoadMode = false;

		// draw load mode arrow
		if (diskop.mode == 0)
		{
			charOutBg(147,14, ' ', video.palette[PAL_GENTXT], video.palette[PAL_GENBKG]); // clear other box
			charOutBg(147, 3, 0x3, video.palette[PAL_GENTXT], video.palette[PAL_GENBKG]);
		}
		else
		{
			charOutBg(147, 3, ' ', video.palette[PAL_GENTXT], video.palette[PAL_GENBKG]); // clear other box
			charOutBg(147,14, 0x3, video.palette[PAL_GENTXT], video.palette[PAL_GENBKG]);
		}
	}

	if (ui.updatePackText)
	{
		ui.updatePackText = false;
		textOutBg(120, 3, diskop.modPackFlg ? "ON " : "OFF", video.palette[PAL_GENTXT], video.palette[PAL_GENBKG]);
	}

	if (ui.updateSaveFormatText)
	{
		ui.updateSaveFormatText = false;
		     if (diskop.smpSaveType == DISKOP_SMP_WAV) textOutBg(120, 14, "WAV", video.palette[PAL_GENTXT], video.palette[PAL_GENBKG]);
		else if (diskop.smpSaveType == DISKOP_SMP_IFF) textOutBg(120, 14, "IFF", video.palette[PAL_GENTXT], video.palette[PAL_GENBKG]);
		else if (diskop.smpSaveType == DISKOP_SMP_RAW) textOutBg(120, 14, "RAW", video.palette[PAL_GENTXT], video.palette[PAL_GENBKG]);
	}

	if (ui.updateDiskOpPathText)
	{
		ui.updateDiskOpPathText = false;

		// print disk op. path
		for (uint32_t i = 0; i < 26; i++)
		{
			tmpChar = editor.currPath[ui.diskOpPathTextOffset+i];
			if (tmpChar == '\0')
				tmpChar = '_';

			charOutBg(24 + (i * FONT_CHAR_W), 25, tmpChar, video.palette[PAL_GENTXT], video.palette[PAL_GENBKG]);
		}
	}
}

void updatePosEd(void)
{
	int16_t posEdPosition;
	int32_t x, y, y2;
	uint32_t *dstPtr, bgPixel;

	if (!ui.posEdScreenShown || !ui.updatePosEd)
		return;

	ui.updatePosEd = false;

	if (!ui.disablePosEd)
	{
		bgPixel = video.palette[PAL_BACKGRD];

		posEdPosition = modEntry->currOrder;
		if (posEdPosition > modEntry->head.orderCount-1)
			posEdPosition = modEntry->head.orderCount-1;

		// top five
		for (y = 0; y < 5; y++)
		{
			if (posEdPosition-(5-y) >= 0)
			{
				printThreeDecimalsBg(128, 23+(y*6), posEdPosition-(5-y), video.palette[PAL_QADSCP], video.palette[PAL_BACKGRD]);
				printTwoDecimalsBg(160, 23+(y*6), modEntry->head.order[posEdPosition-(5-y)], video.palette[PAL_QADSCP], video.palette[PAL_BACKGRD]);
			}
			else
			{
				dstPtr = &video.frameBuffer[((23+(y*6)) * SCREEN_W) + 128];
				for (y2 = 0; y2 < 5; y2++)
				{
					for (x = 0; x < FONT_CHAR_W*22; x++)
						dstPtr[x] = bgPixel;

					dstPtr += SCREEN_W;
				}
			}
		}

		// middle
		printThreeDecimalsBg(128, 53, posEdPosition, video.palette[PAL_GENTXT], video.palette[PAL_GENBKG]);
		printTwoDecimalsBg(160, 53, *editor.currPosEdPattDisp, video.palette[PAL_GENTXT], video.palette[PAL_GENBKG]);

		// bottom six
		for (y = 0; y < 6; y++)
		{
			if (posEdPosition+y < modEntry->head.orderCount-1)
			{
				printThreeDecimalsBg(128, 59+(y*6), posEdPosition+(y+1), video.palette[PAL_QADSCP], video.palette[PAL_BACKGRD]);
				printTwoDecimalsBg(160, 59+(y*6), modEntry->head.order[posEdPosition+(y+1)], video.palette[PAL_QADSCP], video.palette[PAL_BACKGRD]);
			}
			else
			{
				dstPtr = &video.frameBuffer[((59+(y*6)) * SCREEN_W) + 128];
				for (y2 = 0; y2 < 5; y2++)
				{
					for (x = 0; x < FONT_CHAR_W*22; x++)
						dstPtr[x] = bgPixel;

					dstPtr += SCREEN_W;
				}
			}
		}

		// kludge to fix bottom part of text edit marker in pos ed
		if (ui.editTextFlag && ui.editObject == PTB_PE_PATT)
			renderTextEditMarker();
	}
}

void renderPosEdScreen(void)
{
	const uint32_t *srcPtr;
	uint32_t *dstPtr;

	srcPtr = posEdBMP;
	dstPtr = &video.frameBuffer[120];

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

	if (ui.diskOpScreenShown || ui.posEdScreenShown)
		return;

	dstPtr = &video.frameBuffer[(3 * SCREEN_W) + 310];
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

	ui.disablePosEd = true;
	ui.disableVisualizer = true;

	srcPtr = clearDialogBMP;
	dstPtr = &video.frameBuffer[(51 * SCREEN_W) + 160];

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

	ui.disablePosEd = false;
	ui.disableVisualizer = false;
}

void updateCurrSample(void)
{
	ui.updateCurrSampleName = true;
	ui.updateSongSize = true;

	if (!ui.diskOpScreenShown)
	{
		ui.updateCurrSampleFineTune = true;
		ui.updateCurrSampleNum = true;
		ui.updateCurrSampleVolume = true;
		ui.updateCurrSampleLength = true;
		ui.updateCurrSampleRepeat = true;
		ui.updateCurrSampleReplen = true;
	}

	if (ui.samplerScreenShown)
		redrawSample();

	updateSamplePos();
	recalcChordLength();

	sampler.tmpLoopStart = 0;
	sampler.tmpLoopLength = 0;
}

void updatePatternData(void)
{
	if (ui.updatePatternData)
	{
		ui.updatePatternData = false;
		if (!ui.samplerScreenShown)
			redrawPattern();
	}
}

void removeTextEditMarker(void)
{
	uint32_t *dstPtr, pixel;

	if (!ui.editTextFlag)
		return;

	dstPtr = &video.frameBuffer[((ui.lineCurY - 1) * SCREEN_W) + (ui.lineCurX - 4)];

	if (ui.editObject == PTB_PE_PATT)
	{
		// position editor text editing

		pixel = video.palette[PAL_GENBKG2];
		for (uint32_t x = 0; x < 7; x++)
			dstPtr[x] = pixel;

		// no need to clear the second row of pixels

		ui.updatePosEd = true;
	}
	else
	{
		// all others

		pixel = video.palette[PAL_GENBKG];
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

	if (!ui.editTextFlag)
		return;

	dstPtr = &video.frameBuffer[((ui.lineCurY - 1) * SCREEN_W) + (ui.lineCurX - 4)];
	pixel = video.palette[PAL_TEXTMARK];

	for (uint32_t y = 0; y < 2; y++)
	{
		for (uint32_t x = 0; x < 7; x++)
			dstPtr[x] = pixel;

		dstPtr += SCREEN_W;
	}
}

static void sendMouseButtonUpEvent(uint8_t button)
{
	SDL_Event event;

	memset(&event, 0, sizeof (event));

	event.type = SDL_MOUSEBUTTONUP;
	event.button.button = button;

	SDL_PushEvent(&event);
}

void handleLastGUIObjectDown(void)
{
	bool testMouseButtonRelease = false;

	if (ui.sampleMarkingPos >= 0)
	{
		samplerSamplePressed(MOUSE_BUTTON_HELD);
		testMouseButtonRelease = true;
	}

	if (ui.forceSampleDrag)
	{
		samplerBarPressed(MOUSE_BUTTON_HELD);
		testMouseButtonRelease = true;
	}

	if (ui.forceSampleEdit)
	{
		samplerEditSample(MOUSE_BUTTON_HELD);
		testMouseButtonRelease = true;
	}

	if (ui.forceVolDrag)
	{
		volBoxBarPressed(MOUSE_BUTTON_HELD);
		testMouseButtonRelease = true;
	}

	/* Hack to send "mouse button up" events if we released the mouse button(s)
	** outside of the window...
	*/
	if (testMouseButtonRelease)
	{
		if (mouse.x < 0 || mouse.x >= SCREEN_W || mouse.y < 0 || mouse.y >= SCREEN_H)
		{
			if (mouse.leftButtonPressed && !(mouse.buttonState & SDL_BUTTON_LMASK))
				sendMouseButtonUpEvent(SDL_BUTTON_LEFT);

			if (mouse.rightButtonPressed && !(mouse.buttonState & SDL_BUTTON_RMASK))
				sendMouseButtonUpEvent(SDL_BUTTON_RIGHT);
		}
	}
}

void updateVisualizer(void)
{
	const uint32_t *srcPtr;
	int32_t tmpVol;
	uint32_t *dstPtr, pixel;

	if (ui.disableVisualizer || ui.diskOpScreenShown ||
		ui.posEdScreenShown  || ui.editOpScreenShown ||
		ui.aboutScreenShown  || ui.askScreenShown    ||
		editor.isWAVRendering)
	{
		return;
	}

	if (ui.visualizerMode == VISUAL_SPECTRUM)
	{
		// spectrum analyzer

		dstPtr = &video.frameBuffer[(59 * SCREEN_W) + 129];
		for (uint32_t i = 0; i < SPECTRUM_BAR_NUM; i++)
		{
			srcPtr = spectrumAnaBMP;
			pixel = video.palette[PAL_GENBKG];

			tmpVol = editor.spectrumVolumes[i];
			if (tmpVol > SPECTRUM_BAR_HEIGHT)
				tmpVol = SPECTRUM_BAR_HEIGHT;

			for (int32_t y = SPECTRUM_BAR_HEIGHT-1; y >= 0; y--)
			{
				if (y < tmpVol)
					pixel = srcPtr[y];

				for (uint32_t x = 0; x < SPECTRUM_BAR_WIDTH; x++)
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
	dstPtr = &video.frameBuffer[(44 * SCREEN_W) + 120];

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
	dstPtr = &video.frameBuffer[(44 * SCREEN_W) + 120];

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

	if (!ui.aboutScreenShown || ui.diskOpScreenShown || ui.posEdScreenShown || ui.editOpScreenShown)
		return;

	srcPtr = aboutScreenBMP;
	dstPtr = &video.frameBuffer[(44 * SCREEN_W) + 120];

	for (uint32_t y = 0; y < 55; y++)
	{
		memcpy(dstPtr, srcPtr, 200 * sizeof (int32_t));

		srcPtr += 200;
		dstPtr += SCREEN_W;
	}

	// draw version string

	sprintf(verString, "v%s", PROG_VER_STR);
	verStringX = 260 + (((63 - ((uint32_t)strlen(verString) * (FONT_CHAR_W - 1))) + 1) / 2);
	textOutTight(verStringX, 67, verString, video.palette[PAL_GENBKG2]);
}

void renderEditOpMode(void)
{
	const uint32_t *srcPtr;
	uint32_t *dstPtr;

	// select what character box to render

	switch (ui.editOpScreen)
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

	dstPtr = &video.frameBuffer[(47 * SCREEN_W) + 310];
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
	switch (ui.editOpScreen)
	{
		default:
		case 0: srcPtr = editOpScreen1BMP; break;
		case 1: srcPtr = editOpScreen2BMP; break;
		case 2: srcPtr = editOpScreen3BMP; break;
		case 3: srcPtr = editOpScreen4BMP; break;
	}

	// render background
	dstPtr = &video.frameBuffer[(44 * SCREEN_W) + 120];
	for (uint32_t y = 0; y < 55; y++)
	{
		memcpy(dstPtr, srcPtr, 200 * sizeof (int32_t));

		srcPtr += 200;
		dstPtr += SCREEN_W;
	}

	renderEditOpMode();

	// render text and content
	if (ui.editOpScreen == 0)
	{
		textOut(128, 47, "  TRACK      PATTERN  ", video.palette[PAL_GENTXT]);
	}
	else if (ui.editOpScreen == 1)
	{
		textOut(128, 47, "  RECORD     SAMPLES  ", video.palette[PAL_GENTXT]);

		ui.updateRecordText = true;
		ui.updateQuantizeText = true;
		ui.updateMetro1Text = true;
		ui.updateMetro2Text = true;
		ui.updateFromText = true;
		ui.updateKeysText = true;
		ui.updateToText = true;
	}
	else if (ui.editOpScreen == 2)
	{
		textOut(128, 47, "    SAMPLE EDITOR     ", video.palette[PAL_GENTXT]);
		charOut(272, 91, '%', video.palette[PAL_GENTXT]); // for Volume text

		ui.updatePosText = true;
		ui.updateModText = true;
		ui.updateVolText = true;
	}
	else if (ui.editOpScreen == 3)
	{
		textOut(128, 47, " SAMPLE CHORD EDITOR  ", video.palette[PAL_GENTXT]);

		ui.updateLengthText = true;
		ui.updateNote1Text = true;
		ui.updateNote2Text = true;
		ui.updateNote3Text = true;
		ui.updateNote4Text = true;
	}
}

void renderMOD2WAVDialog(void)
{
	const uint32_t *srcPtr;
	uint32_t *dstPtr;

	srcPtr = mod2wavBMP;
	dstPtr = &video.frameBuffer[(27 * SCREEN_W) + 64];

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

	if (!ui.updateMod2WavDialog)
		return;

	ui.updateMod2WavDialog = false;

	if (editor.isWAVRendering)
	{
		if (ui.mod2WavFinished)
		{
			ui.mod2WavFinished = false;

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
			modSetTempo(modEntry->currBPM); // update BPM with normal audio output rate
			displayMainScreen();
		}
		else
		{
			// render progress bar

			percent = (uint8_t)((modEntry->rowsCounter * 100) / modEntry->rowsInTotal);
			if (percent > 100)
				percent = 100;

			barLength = ((percent * 180) + 50) / 100;
			dstPtr = &video.frameBuffer[(42 * SCREEN_W) + 70];
			pixel = video.palette[PAL_GENBKG2];
			bgPixel = video.palette[PAL_BORDER];

			for (int32_t y = 0; y < 11; y++)
			{
				for (int32_t x = 0; x < 180; x++)
				{
					uint32_t color = bgPixel;
					if (x < barLength)
						color = pixel;

					dstPtr[x] = color;
				}

				dstPtr += SCREEN_W;
			}

			// render percentage
			pixel = video.palette[PAL_GENTXT];
			if (percent > 99)
				printThreeDecimals(144, 45, percent, pixel);
			else
				printTwoDecimals(152, 45, percent, pixel);

			charOut(168, 45, '%', pixel);
		}
	}
}

void updateEditOp(void)
{
	if (!ui.editOpScreenShown || ui.posEdScreenShown || ui.diskOpScreenShown)
		return;

	if (ui.editOpScreen == 1)
	{
		if (ui.updateRecordText)
		{
			ui.updateRecordText = false;
			textOutBg(176, 58, (editor.recordMode == RECORD_PATT) ? "PATT" : "SONG", video.palette[PAL_GENTXT], video.palette[PAL_GENBKG]);
		}

		if (ui.updateQuantizeText)
		{
			ui.updateQuantizeText = false;
			printTwoDecimalsBg(192, 69, *editor.quantizeValueDisp, video.palette[PAL_GENTXT], video.palette[PAL_GENBKG]);
		}

		if (ui.updateMetro1Text)
		{
			ui.updateMetro1Text = false;
			printTwoDecimalsBg(168, 80, *editor.metroSpeedDisp, video.palette[PAL_GENTXT], video.palette[PAL_GENBKG]);
		}

		if (ui.updateMetro2Text)
		{
			ui.updateMetro2Text = false;
			printTwoDecimalsBg(192, 80, *editor.metroChannelDisp, video.palette[PAL_GENTXT], video.palette[PAL_GENBKG]);
		}

		if (ui.updateFromText)
		{
			ui.updateFromText = false;
			printTwoHexBg(264, 80, *editor.sampleFromDisp, video.palette[PAL_GENTXT], video.palette[PAL_GENBKG]);
		}

		if (ui.updateKeysText)
		{
			ui.updateKeysText = false;
			textOutBg(160, 91, editor.multiFlag ? "MULTI " : "SINGLE", video.palette[PAL_GENTXT], video.palette[PAL_GENBKG]);
		}

		if (ui.updateToText)
		{
			ui.updateToText = false;
			printTwoHexBg(264, 91, *editor.sampleToDisp, video.palette[PAL_GENTXT], video.palette[PAL_GENBKG]);
		}
	}
	else if (ui.editOpScreen == 2)
	{
		if (ui.updateMixText)
		{
			ui.updateMixText = false;
			if (editor.mixFlag)
			{
				textOutBg(128, 47, editor.mixText, video.palette[PAL_GENTXT], video.palette[PAL_GENBKG]);
				textOutBg(248, 47, "  ", video.palette[PAL_GENTXT], video.palette[PAL_GENBKG]);
			}
			else
			{
				textOutBg(128, 47, "    SAMPLE EDITOR     ", video.palette[PAL_GENTXT], video.palette[PAL_GENBKG]);
			}
		}

		if (ui.updatePosText)
		{
			ui.updatePosText = false;
			printFourHexBg(248, 58, *editor.samplePosDisp, video.palette[PAL_GENTXT], video.palette[PAL_GENBKG]);
		}

		if (ui.updateModText)
		{
			ui.updateModText = false;
			printThreeDecimalsBg(256, 69,
				(editor.modulateSpeed < 0) ? (0 - editor.modulateSpeed) : editor.modulateSpeed,
				video.palette[PAL_GENTXT], video.palette[PAL_GENBKG]);

			if (editor.modulateSpeed < 0)
				charOutBg(248, 69, '-', video.palette[PAL_GENTXT], video.palette[PAL_GENBKG]);
			else
				charOutBg(248, 69, ' ', video.palette[PAL_GENTXT], video.palette[PAL_GENBKG]);
		}

		if (ui.updateVolText)
		{
			ui.updateVolText = false;
			printThreeDecimalsBg(248, 91, *editor.sampleVolDisp, video.palette[PAL_GENTXT], video.palette[PAL_GENBKG]);
		}
	}
	else if (ui.editOpScreen == 3)
	{
		if (ui.updateLengthText)
		{
			ui.updateLengthText = false;

			// clear background
			textOutBg(168, 91, "    ", video.palette[PAL_GENTXT], video.palette[PAL_GENBKG]);
			charOut(198, 91,    ':', video.palette[PAL_GENBKG]);

			if (modEntry->samples[editor.currSample].loopLength > 2 || modEntry->samples[editor.currSample].loopStart >= 2)
			{
				textOut(168, 91, "LOOP", video.palette[PAL_GENTXT]);
			}
			else
			{
				printFourHex(168, 91, *editor.chordLengthDisp, video.palette[PAL_GENTXT]); // chord max length
				charOut(198, 91, (editor.chordLengthMin) ? '.' : ':', video.palette[PAL_GENTXT]); // min/max flag
			}
		}

		if (ui.updateNote1Text)
		{
			ui.updateNote1Text = false;
			if (editor.note1 > 35)
				textOutBg(256, 58, "---", video.palette[PAL_GENTXT], video.palette[PAL_GENBKG]);
			else
				textOutBg(256, 58, config.accidental ? noteNames2[2+editor.note1] : noteNames1[2+editor.note1],
					video.palette[PAL_GENTXT], video.palette[PAL_GENBKG]);
		}

		if (ui.updateNote2Text)
		{
			ui.updateNote2Text = false;
			if (editor.note2 > 35)
				textOutBg(256, 69, "---", video.palette[PAL_GENTXT], video.palette[PAL_GENBKG]);
			else
				textOutBg(256, 69, config.accidental ? noteNames2[2+editor.note2] : noteNames1[2+editor.note2],
					video.palette[PAL_GENTXT], video.palette[PAL_GENBKG]);
		}

		if (ui.updateNote3Text)
		{
			ui.updateNote3Text = false;
			if (editor.note3 > 35)
				textOutBg(256, 80, "---", video.palette[PAL_GENTXT], video.palette[PAL_GENBKG]);
			else
				textOutBg(256, 80, config.accidental ? noteNames2[2+editor.note3] : noteNames1[2+editor.note3],
					video.palette[PAL_GENTXT], video.palette[PAL_GENBKG]);
		}
			
		if (ui.updateNote4Text)
		{
			ui.updateNote4Text = false;
			if (editor.note4 > 35)
				textOutBg(256, 91, "---", video.palette[PAL_GENTXT], video.palette[PAL_GENBKG]);
			else
				textOutBg(256, 91, config.accidental ? noteNames2[2+editor.note4] : noteNames1[2+editor.note4],
					video.palette[PAL_GENTXT], video.palette[PAL_GENBKG]);
		}
	}
}

void displayMainScreen(void)
{
	editor.blockMarkFlag = false;

	ui.updateSongName = true;
	ui.updateSongSize = true;
	ui.updateSongTiming = true;
	ui.updateTrackerFlags = true;
	ui.updateStatusText = true;

	ui.updateCurrSampleName = true;

	if (!ui.diskOpScreenShown)
	{
		ui.updateCurrSampleFineTune = true;
		ui.updateCurrSampleNum = true;
		ui.updateCurrSampleVolume = true;
		ui.updateCurrSampleLength = true;
		ui.updateCurrSampleRepeat = true;
		ui.updateCurrSampleReplen = true;
	}

	if (ui.samplerScreenShown)
	{
		if (!ui.diskOpScreenShown)
			memcpy(video.frameBuffer, trackerFrameBMP, 320 * 121 * sizeof (int32_t));
	}
	else
	{
		if (!ui.diskOpScreenShown)
			memcpy(video.frameBuffer, trackerFrameBMP, 320 * 255 * sizeof (int32_t));
		else
			memcpy(&video.frameBuffer[121 * SCREEN_W], &trackerFrameBMP[121 * SCREEN_W], 320 * 134 * sizeof (int32_t));

		ui.updateSongBPM = true;
		ui.updateCurrPattText = true;
		ui.updatePatternData  = true;
	}

	if (ui.diskOpScreenShown)
	{
		renderDiskOpScreen();
	}
	else
	{
		ui.updateSongPos = true;
		ui.updateSongPattern = true;
		ui.updateSongLength = true;

		// zeroes (can't integrate zeroes in the graphics, the palette entry is above the 2-bit range)
		charOut(64,  3, '0', video.palette[PAL_GENTXT]);
		textOut(64, 14, "00", video.palette[PAL_GENTXT]);

		if (!editor.isWAVRendering)
		{
			charOut(64, 25, '0', video.palette[PAL_GENTXT]);
			textOut(64, 47, "00", video.palette[PAL_GENTXT]);
			textOut(64, 58, "00", video.palette[PAL_GENTXT]);
		}

		if (ui.posEdScreenShown)
		{
			renderPosEdScreen();
			ui.updatePosEd = true;
		}
		else
		{
			if (ui.editOpScreenShown)
			{
				renderEditOpScreen();
			}
			else
			{
				if (ui.aboutScreenShown)
				{
					renderAboutScreen();
				}
				else
				{
					     if (ui.visualizerMode == VISUAL_QUADRASCOPE) renderQuadrascopeBg();
					else if (ui.visualizerMode == VISUAL_SPECTRUM) renderSpectrumAnalyzerBg();
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
	ui.pat2SmpDialogShown = false;

	switch (ui.askScreenType)
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

		case ASK_LOAD_DOWNSAMPLE:
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
	int8_t oldSample;
	uint32_t i;
	moduleSample_t *s;

	switch (ui.askScreenType)
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
			doPat2Smp();
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

			if (ui.samplerScreenShown)
				redrawSample();

			updateWindowTitle(MOD_IS_MODIFIED);
		}
		break;

		case ASK_FILTER_ALL_SAMPLES:
		{
			restoreStatusAndMousePointer();

			for (i = 0; i < MOD_SAMPLES; i++)
				filterSample(i, true);

			if (ui.samplerScreenShown)
				redrawSample();

			updateWindowTitle(MOD_IS_MODIFIED);
		}
		break;

		case ASK_UPSAMPLE:
		{
			restoreStatusAndMousePointer();
			upSample();
		}
		break;

		case ASK_DOWNSAMPLE:
		{
			restoreStatusAndMousePointer();
			downSample();
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

			ui.updateSongSize = true;
			updateWindowTitle(MOD_IS_MODIFIED);
		}
		break;

		case ASK_RESAMPLE:
		{
			restoreStatusAndMousePointer();
			samplerResample();
		}
		break;

		case ASK_LOAD_DOWNSAMPLE:
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
			ui.throwExit = true;
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

	pixel24 = video.palette[PAL_PATCURSOR];
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
				patternCursorBMP[(y * 11) + x] = video.palette[PAL_COLORKEY];

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
	if (bigYesNoDialogBMP != NULL) free(bigYesNoDialogBMP);
	if (pat2SmpDialogBMP != NULL) free(pat2SmpDialogBMP);
	if (editOpScreen1BMP != NULL) free(editOpScreen1BMP);
	if (editOpScreen2BMP != NULL) free(editOpScreen2BMP);
	if (editOpScreen3BMP != NULL) free(editOpScreen3BMP);
	if (editOpScreen4BMP != NULL) free(editOpScreen4BMP);
	if (aboutScreenBMP != NULL) free(aboutScreenBMP);
	if (muteButtonsBMP != NULL) free(muteButtonsBMP);
	if (editOpModeCharsBMP != NULL) free(editOpModeCharsBMP);
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
		dst[(i * 4) + 0] = video.palette[byteIn];

		byteIn = (tmpBuffer[i] & 0x30) >> 4;
		assert(byteIn < PALETTE_NUM);
		dst[(i * 4) + 1] = video.palette[byteIn];

		byteIn = (tmpBuffer[i] & 0x0C) >> 2;
		assert(byteIn < PALETTE_NUM);
		dst[(i * 4) + 2] = video.palette[byteIn];

		byteIn = (tmpBuffer[i] & 0x03) >> 0;
		assert(byteIn < PALETTE_NUM);
		dst[(i * 4) + 3] = video.palette[byteIn];
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
	bigYesNoDialogBMP = unpackBMP(bigYesNoDialogPackedBMP, sizeof (bigYesNoDialogPackedBMP));
	pat2SmpDialogBMP = unpackBMP(pat2SmpDialogPackedBMP, sizeof (pat2SmpDialogPackedBMP));
	editOpScreen1BMP = unpackBMP(editOpScreen1PackedBMP, sizeof (editOpScreen1PackedBMP));
	editOpScreen2BMP = unpackBMP(editOpScreen2PackedBMP, sizeof (editOpScreen2PackedBMP));
	editOpScreen3BMP = unpackBMP(editOpScreen3PackedBMP, sizeof (editOpScreen3PackedBMP));
	editOpScreen4BMP = unpackBMP(editOpScreen4PackedBMP, sizeof (editOpScreen4PackedBMP));
	aboutScreenBMP = unpackBMP(aboutScreenPackedBMP, sizeof (aboutScreenPackedBMP));
	muteButtonsBMP = unpackBMP(muteButtonsPackedBMP, sizeof (muteButtonsPackedBMP));
	editOpModeCharsBMP = unpackBMP(editOpModeCharsPackedBMP, sizeof (editOpModeCharsPackedBMP));

	if (trackerFrameBMP    == NULL || samplerScreenBMP   == NULL || samplerVolumeBMP  == NULL ||
		clearDialogBMP     == NULL || diskOpScreenBMP    == NULL || mod2wavBMP        == NULL ||
		posEdBMP           == NULL || spectrumVisualsBMP == NULL || yesNoDialogBMP    == NULL ||
		editOpScreen1BMP   == NULL || editOpScreen2BMP   == NULL || editOpScreen3BMP  == NULL ||
		editOpScreen4BMP   == NULL || aboutScreenBMP     == NULL || muteButtonsBMP    == NULL ||
		editOpModeCharsBMP == NULL || samplerFiltersBMP  == NULL || yesNoDialogBMP    == NULL ||
		bigYesNoDialogBMP  == NULL)
	{
		showErrorMsgBox("Out of memory!");
		return false; // BMPs are free'd in cleanUp()
	}

	createBitmaps();
	return true;
}

void videoClose(void)
{
	SDL_DestroyTexture(video.texture);
	SDL_DestroyRenderer(video.renderer);
	SDL_DestroyWindow(video.window);
	free(video.frameBufferUnaligned);
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
	sprites[SPRITE_PATTERN_CURSOR].colorKey = video.palette[PAL_COLORKEY];
	sprites[SPRITE_PATTERN_CURSOR].w = 11;
	sprites[SPRITE_PATTERN_CURSOR].h = 14;
	hideSprite(SPRITE_PATTERN_CURSOR);

	sprites[SPRITE_LOOP_PIN_LEFT].data = loopPinsBMP;
	sprites[SPRITE_LOOP_PIN_LEFT].pixelType = SPRITE_TYPE_RGB;
	sprites[SPRITE_LOOP_PIN_LEFT].colorKey = video.palette[PAL_COLORKEY];
	sprites[SPRITE_LOOP_PIN_LEFT].w = 4;
	sprites[SPRITE_LOOP_PIN_LEFT].h = 64;
	hideSprite(SPRITE_LOOP_PIN_LEFT);

	sprites[SPRITE_LOOP_PIN_RIGHT].data = &loopPinsBMP[4 * 64];
	sprites[SPRITE_LOOP_PIN_RIGHT].pixelType = SPRITE_TYPE_RGB;
	sprites[SPRITE_LOOP_PIN_RIGHT].colorKey = video.palette[PAL_COLORKEY];
	sprites[SPRITE_LOOP_PIN_RIGHT].w = 4;
	sprites[SPRITE_LOOP_PIN_RIGHT].h = 64;
	hideSprite(SPRITE_LOOP_PIN_RIGHT);

	sprites[SPRITE_SAMPLING_POS_LINE].data = samplingPosBMP;
	sprites[SPRITE_SAMPLING_POS_LINE].pixelType = SPRITE_TYPE_RGB;
	sprites[SPRITE_SAMPLING_POS_LINE].colorKey = video.palette[PAL_COLORKEY];
	sprites[SPRITE_SAMPLING_POS_LINE].w = 1;
	sprites[SPRITE_SAMPLING_POS_LINE].h = 64;
	hideSprite(SPRITE_SAMPLING_POS_LINE);

	// setup refresh buffer (used to clear sprites after each frame)
	for (uint32_t i = 0; i < SPRITE_NUM; i++)
		sprites[i].refreshBuffer = (uint32_t *)malloc((sprites[i].w * sprites[i].h) * sizeof (int32_t));
}

void freeSprites(void)
{
	for (int32_t i = 0; i < SPRITE_NUM; i++)
		free(sprites[i].refreshBuffer);
}

void setSpritePos(int32_t sprite, int32_t x, int32_t y)
{
	sprites[sprite].newX = (int16_t)x;
	sprites[sprite].newY = (int16_t)y;
}

void hideSprite(int32_t sprite)
{
	sprites[sprite].newX = SCREEN_W;
}

void eraseSprites(void)
{
	int32_t sx, sy, x, y, sw, sh, srcPitch, dstPitch;
	const uint32_t *src32;
	uint32_t *dst32;
	sprite_t *s;

	for (int32_t i = SPRITE_NUM-1; i >= 0; i--) // erasing must be done in reverse order
	{
		s = &sprites[i];
		if (s->x >= SCREEN_W || s->y >= SCREEN_H) // sprite is hidden, don't draw nor fill clear buffer
			continue;

		assert(s->refreshBuffer != NULL);

		sw = s->w;
		sh = s->h;
		sx = s->x;
		sy = s->y;

		// if x is negative, adjust variables
		if (sx < 0)
		{
			sw += sx; // subtraction
			sx = 0;
		}

		// if y is negative, adjust variables
		if (sy < 0)
		{
			sh += sy; // subtraction
			sy = 0;
		}

		dst32 = &video.frameBuffer[(sy * SCREEN_W) + sx];
		src32 = s->refreshBuffer;

		// handle x/y clipping
		if (sx+sw >= SCREEN_W) sw = SCREEN_W - sx;
		if (sy+sh >= SCREEN_H) sh = SCREEN_H - sy;

		srcPitch = s->w - sw;
		dstPitch = SCREEN_W - sw;

		for (y = 0; y < sh; y++)
		{
			for (x = 0; x < sw; x++)
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
	int32_t sx, sy, x, y, srcPtrBias, sw, sh, srcPitch, dstPitch;
	const uint32_t *src32;
	uint32_t *dst32, *clr32, colorKey;
	sprite_t *s;

	renderVuMeters(); // let's put it here even though it's not sprite-based

	for (int32_t i = 0; i < SPRITE_NUM; i++)
	{
		s = &sprites[i];

		// set new sprite position
		s->x = s->newX;
		s->y = s->newY;

		if (s->x >= SCREEN_W || s->y >= SCREEN_H) // sprite is hidden, don't draw nor fill clear buffer
			continue;

		assert(s->data != NULL && s->refreshBuffer != NULL);

		sw = s->w;
		sh = s->h;
		sx = s->x;
		sy = s->y;
		srcPtrBias = 0;

		// if x is negative, adjust variables
		if (sx < 0)
		{
			sw += sx; // subtraction
			srcPtrBias += -sx;
			sx = 0;
		}

		// if y is negative, adjust variables
		if (sy < 0)
		{
			sh += sy; // subtraction
			srcPtrBias += (-sy * s->w);
			sy = 0;
		}

		dst32 = &video.frameBuffer[(sy * SCREEN_W) + sx];
		clr32 = s->refreshBuffer;

		// handle x/y clipping
		if (sx+sw >= SCREEN_W) sw = SCREEN_W - sx;
		if (sy+sh >= SCREEN_H) sh = SCREEN_H - sy;

		srcPitch = s->w - sw;
		dstPitch = SCREEN_W - sw;

		colorKey = sprites[i].colorKey;
		if (sprites[i].pixelType == SPRITE_TYPE_RGB)
		{
			// 24-bit RGB sprite
			src32 = ((uint32_t *)sprites[i].data) + srcPtrBias;
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
			src8 = ((uint8_t *)sprites[i].data) + srcPtrBias;
			for (y = 0; y < sh; y++)
			{
				for (x = 0; x < sw; x++)
				{
					*clr32++ = *dst32; // fill clear buffer
					if (*src8 != colorKey)
					{
						assert(*src8 < PALETTE_NUM);
						*dst32 = video.palette[*src8];
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
	renderSprites();
	SDL_UpdateTexture(video.texture, NULL, video.frameBuffer, SCREEN_W * sizeof (int32_t));
	SDL_RenderClear(video.renderer);
	SDL_RenderCopy(video.renderer, video.texture, NULL, NULL);
	SDL_RenderPresent(video.renderer);
	eraseSprites();

	if (!video.vsync60HzPresent)
	{
		waitVBL(); // we have no VSync, do crude thread sleeping to sync to ~60Hz
	}
	else
	{
		uint32_t windowFlags = SDL_GetWindowFlags(video.window);

		/* We have VSync, but it can unexpectedly get inactive in certain scenarios.
		** We have to force thread sleeping (to ~60Hz) if so.
		*/
#ifdef __APPLE__
		// macOS: VSync gets disabled if the window is 100% covered by another window. Let's add a (crude) fix:
		if ((windowFlags & SDL_WINDOW_MINIMIZED) || !(windowFlags & SDL_WINDOW_INPUT_FOCUS))
			waitVBL();
#elif __unix__
		// *NIX: VSync gets disabled in fullscreen mode (at least on some distros/systems). Let's add a fix:
		if ((windowFlags & SDL_WINDOW_MINIMIZED) || video.fullscreen)
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
	int8_t scaledVol;
	int32_t scaledNote;

	if (ui.visualizerMode != VISUAL_SPECTRUM || vol <= 0)
		return;

	scaledVol = (vol * 24600L) >> 16; // scaledVol = (vol << 8) / 682

	period = CLAMP(period, 113, 856);

	scaledNote = 856 - period;
	scaledNote *= scaledNote;
	scaledNote = ((int64_t)scaledNote * 171162) >> 32; // scaledNote /= 25093

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
	int32_t i;

	// sink stuff @ 50Hz rate

	static uint64_t counter50Hz;
	const uint64_t counter50HzDelta = ((uint64_t)AMIGA_PAL_VBLANK_HZ << 32) / VBLANK_HZ;

	counter50Hz += counter50HzDelta; // 32.32 fixed-point counter
	if (counter50Hz > 0xFFFFFFFF)
	{
		counter50Hz &= 0xFFFFFFFF;

		// sink VU-meters
		for (i = 0; i < AMIGA_VOICES; i++)
		{
			if (editor.vuMeterVolumes[i] > 0)
				editor.vuMeterVolumes[i]--;
		}

		// sink "spectrum analyzer" bars
		for (i = 0; i < SPECTRUM_BAR_NUM; i++)
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

	di = SDL_GetWindowDisplayIndex(video.window);
	if (di < 0)
		di = 0; /* return display index 0 (default) on error */

	SDL_GetDesktopDisplayMode(di, &dm);
	video.displayW = dm.w;
	video.displayH = dm.h;

	if (video.fullscreen)
	{
		if (config.fullScreenStretch)
		{
			video.renderW = video.displayW;
			video.renderH = video.displayH;
			video.renderX = 0;
			video.renderY = 0;
		}
		else
		{
			SDL_RenderGetScale(video.renderer, &fXScale, &fYScale);

			video.renderW = (int32_t)(SCREEN_W * fXScale);
			video.renderH = (int32_t)(SCREEN_H * fYScale);

#ifdef __APPLE__
			// retina high-DPI hackery (SDL2 is bad at reporting actual rendering sizes on macOS w/ high-DPI)
			SDL_GL_GetDrawableSize(video.window, &actualScreenW, &actualScreenH);
			SDL_GetDesktopDisplayMode(0, &dm);

			dXUpscale = ((double)actualScreenW / video.displayW);
			dYUpscale = ((double)actualScreenH / video.displayH);

			// downscale back to correct sizes
			if (dXUpscale != 0.0) video.renderW = (int32_t)(video.renderW / dXUpscale);
			if (dYUpscale != 0.0) video.renderH = (int32_t)(video.renderH / dYUpscale);
#endif
			video.renderX = (video.displayW - video.renderW) >> 1;
			video.renderY = (video.displayH - video.renderH) >> 1;
		}
	}
	else
	{
		SDL_GetWindowSize(video.window, &video.renderW, &video.renderH);

		video.renderX = 0;
		video.renderY = 0;
	}

	// for mouse cursor creation
	video.xScale = (int32_t)((video.renderW * (1.0 / SCREEN_W)) + 0.5);
	video.yScale = (int32_t)((video.renderH * (1.0 / SCREEN_H)) + 0.5);
	createMouseCursors();
}

void toggleFullScreen(void)
{
	SDL_DisplayMode dm;

	video.fullscreen ^= 1;
	if (video.fullscreen)
	{
		if (config.fullScreenStretch)
		{
			SDL_GetDesktopDisplayMode(0, &dm);
			SDL_RenderSetLogicalSize(video.renderer, dm.w, dm.h);
		}
		else
		{
			SDL_RenderSetLogicalSize(video.renderer, SCREEN_W, SCREEN_H);
		}

		SDL_SetWindowSize(video.window, SCREEN_W, SCREEN_H);
		SDL_SetWindowFullscreen(video.window, SDL_WINDOW_FULLSCREEN_DESKTOP);
		SDL_SetWindowGrab(video.window, SDL_TRUE);
	}
	else
	{
		SDL_SetWindowFullscreen(video.window, 0);
		SDL_RenderSetLogicalSize(video.renderer, SCREEN_W, SCREEN_H);
		SDL_SetWindowSize(video.window, SCREEN_W * config.videoScaleFactor, SCREEN_H * config.videoScaleFactor);
		SDL_SetWindowPosition(video.window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
		SDL_SetWindowGrab(video.window, SDL_FALSE);
	}

	updateRenderSizeVars();
	updateMouseScaling();

	if (video.fullscreen)
	{
		mouse.setPosX = video.displayW >> 1;
		mouse.setPosY = video.displayH >> 1;
	}
	else
	{
		mouse.setPosX = video.renderW >> 1;
		mouse.setPosY = video.renderH >> 1;
	}

	mouse.setPosFlag = true;
}

bool setupVideo(void)
{
	int32_t screenW, screenH;
	uint32_t rendererFlags;
	SDL_DisplayMode dm;

	screenW = SCREEN_W * config.videoScaleFactor;
	screenH = SCREEN_H * config.videoScaleFactor;

	rendererFlags = 0;

#ifdef _WIN32
#if SDL_PATCHLEVEL >= 4
	SDL_SetHint(SDL_HINT_WINDOWS_NO_CLOSE_ON_ALT_F4, "1"); // this is for Windows only
#endif
#endif

#if SDL_PATCHLEVEL >= 5
	SDL_SetHint(SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH, "1");
#endif

	video.vsync60HzPresent = false;
	if (!config.vsyncOff)
	{
		SDL_GetDesktopDisplayMode(0, &dm);
		if (dm.refresh_rate >= 59 && dm.refresh_rate <= 61)
		{
			video.vsync60HzPresent = true;
			rendererFlags |= SDL_RENDERER_PRESENTVSYNC;
		}
	}

	video.window = SDL_CreateWindow("", SDL_WINDOWPOS_CENTERED,
		SDL_WINDOWPOS_CENTERED, screenW, screenH,
		SDL_WINDOW_HIDDEN | SDL_WINDOW_ALLOW_HIGHDPI);

	if (video.window == NULL)
	{
		showErrorMsgBox("Couldn't create SDL window:\n%s", SDL_GetError());
		return false;
	}

	video.renderer = SDL_CreateRenderer(video.window, -1, rendererFlags);
	if (video.renderer == NULL)
	{
		if (video.vsync60HzPresent) // try again without vsync flag
		{
			video.vsync60HzPresent = false;
			rendererFlags &= ~SDL_RENDERER_PRESENTVSYNC;
			video.renderer = SDL_CreateRenderer(video.window, -1, rendererFlags);
		}

		if (video.renderer == NULL)
		{
			showErrorMsgBox("Couldn't create SDL renderer:\n%s\n\n" \
			                "Is your GPU (+ driver) too old?", SDL_GetError());
			return false;
		}
	}

	SDL_RenderSetLogicalSize(video.renderer, SCREEN_W, SCREEN_H);

#if SDL_PATCHLEVEL >= 5
	SDL_RenderSetIntegerScale(video.renderer, SDL_TRUE);
#endif

	SDL_SetRenderDrawBlendMode(video.renderer, SDL_BLENDMODE_NONE);

	SDL_SetHint("SDL_RENDER_SCALE_QUALITY", "nearest");

	video.texture = SDL_CreateTexture(video.renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, SCREEN_W, SCREEN_H);
	if (video.texture == NULL)
	{
		showErrorMsgBox("Couldn't create %dx%d GPU texture:\n%s\n\n" \
		                "Is your GPU (+ driver) too old?", SCREEN_W, SCREEN_H, SDL_GetError());
		return false;
	}

	SDL_SetTextureBlendMode(video.texture, SDL_BLENDMODE_NONE);

	// frame buffer used by SDL (for texture)
	video.frameBufferUnaligned = (uint32_t *)MALLOC_PAD(SCREEN_W * SCREEN_H * sizeof (int32_t), 256);
	if (video.frameBufferUnaligned == NULL)
	{
		showErrorMsgBox("Out of memory!");
		return false;
	}

	// we want an aligned pointer
	video.frameBuffer = (uint32_t *)ALIGN_PTR(video.frameBufferUnaligned, 256);

	updateRenderSizeVars();
	updateMouseScaling();

	if (config.hwMouse)
		SDL_ShowCursor(SDL_TRUE);
	else
		SDL_ShowCursor(SDL_FALSE);

	return true;
}
