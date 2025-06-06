// for finding memory leaks in debug mode with Visual Studio
#if defined _DEBUG && defined _MSC_VER
#include <crtdbg.h>
#endif

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
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
#include "pt2_helpers.h"
#include "pt2_textout.h"
#include "pt2_tables.h"
#include "pt2_pattern_viewer.h"
#include "pt2_sampler.h"
#include "pt2_diskop.h"
#include "pt2_visuals.h"
#include "pt2_scopes.h"
#include "pt2_edit.h"
#include "pt2_config.h"
#include "pt2_bmp.h"
#include "pt2_sampling.h"
#include "pt2_chordmaker.h"
#include "pt2_mod2wav.h"
#include "pt2_audio.h"
#include "pt2_posed.h"
#include "pt2_textedit.h"

typedef struct sprite_t
{
	bool visible;
	int8_t pixelType;
	int16_t newX, newY, x, y, w, h;
	uint32_t colorKey, *refreshBuffer;
	const void *data;
} sprite_t;

// for debug box
#define FPS_SCAN_FRAMES 60
static char fpsTextBuf[1024];
static uint64_t frameStartTime;
static double dRunningFrameDuration, dAvgFPS;
// ------------------

static int32_t oldCurrMode = -1;
static uint32_t vuMetersBg[4 * (10 * 48)];

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

void blit32(int32_t x, int32_t y, int32_t w, int32_t h, const uint32_t *src)
{
	const uint32_t *srcPtr = src;
	uint32_t *dstPtr = &video.frameBuffer[(y * SCREEN_W) + x];

	for (int32_t yy = 0; yy < h; yy++)
	{
		memcpy(dstPtr, srcPtr, w * sizeof (int32_t));

		srcPtr += w;
		dstPtr += SCREEN_W;
	}
}

void putPixel(int32_t x, int32_t y, const uint32_t pixelColor)
{
	video.frameBuffer[(y * SCREEN_W) + x] = pixelColor;
}

void hLine(int32_t x, int32_t y, int32_t w, const uint32_t pixelColor)
{
	uint32_t *dstPtr = &video.frameBuffer[(y * SCREEN_W) + x];
	for (int32_t xx = 0; xx < w; xx++)
		dstPtr[xx] = pixelColor;
}

void vLine(int32_t x, int32_t y, int32_t h, const uint32_t pixelColor)
{
	uint32_t *dstPtr = &video.frameBuffer[(y * SCREEN_W) + x];
	for (int32_t yy = 0; yy < h; yy++)
	{
		*dstPtr = pixelColor;
		dstPtr += SCREEN_W;
	}
}

void drawFramework1(int32_t x, int32_t y, int32_t w, int32_t h)
{
	hLine(x, y, w-1, video.palette[PAL_BORDER]);
	vLine(x, y+1, h-2, video.palette[PAL_BORDER]);
	hLine(x+1, y+h-1, w-1, video.palette[PAL_GENBKG2]);
	vLine(x+w-1, y+1, h-2, video.palette[PAL_GENBKG2]);

	putPixel(x, y+h-1, video.palette[PAL_GENBKG]);
	putPixel(x+w-1, y, video.palette[PAL_GENBKG]);

	fillRect(x+1, y+1, w-2, h-2, video.palette[PAL_GENBKG]);
}

void drawFramework2(int32_t x, int32_t y, int32_t w, int32_t h)
{
	hLine(x, y, w-1, video.palette[PAL_GENBKG2]);
	vLine(x, y+1, h-2, video.palette[PAL_GENBKG2]);
	hLine(x+1, y+h-1, w-1, video.palette[PAL_BORDER]);
	vLine(x+w-1, y+1, h-2, video.palette[PAL_BORDER]);

	putPixel(x, y+h-1, video.palette[PAL_GENBKG]);
	putPixel(x+w-1, y, video.palette[PAL_GENBKG]);

	fillRect(x+1, y+1, w-2, h-2, video.palette[PAL_GENBKG]);
}

void drawFramework3(int32_t x, int32_t y, int32_t w, int32_t h)
{
	fillRect(x, y, w, h, video.palette[PAL_GENBKG]);

	vLine(x,     y,     h-1, video.palette[PAL_BORDER]);
	vLine(x+1,   y,     h-2, video.palette[PAL_BORDER]);
	hLine(x+2,   y,     w-3, video.palette[PAL_BORDER]);
	hLine(x+2,   y+1,   w-4, video.palette[PAL_BORDER]);
	hLine(x+1,   y+h-1, w-1, video.palette[PAL_GENBKG2]);
	hLine(x+2,   y+h-2, w-2, video.palette[PAL_GENBKG2]);
	vLine(x+w-2, y+2,   h-4, video.palette[PAL_GENBKG2]);
	vLine(x+w-1, y+1,   h-3, video.palette[PAL_GENBKG2]);
}

void fillRect(int32_t x, int32_t y, int32_t w, int32_t h, const uint32_t pixelColor)
{
	uint32_t *dstPtr = &video.frameBuffer[(y * SCREEN_W) + x];

	for (int32_t yy = 0; yy < h; yy++)
	{
		for (int32_t xx = 0; xx < w; xx++)
			dstPtr[xx] = pixelColor;

		dstPtr += SCREEN_W;
	}
}

void drawButton1(int32_t x, int32_t y, int32_t w, int32_t h, const char *text)
{
	const int32_t textW = (int32_t)strlen(text) * (FONT_CHAR_W - 1);

	drawFramework1(x, y, w, h);

	int32_t textX = x + ((w - textW) / 2);
	int32_t textY = y + ((h - FONT_CHAR_H) / 2);

	// kludge
	if (!strcmp(text, ARROW_UP_STR) || !strcmp(text, ARROW_DOWN_STR))
	{
		textX--;
		textY--;
	}

	textOut2(textX, textY, text);
}

void drawButton2(int32_t x, int32_t y, int32_t w, int32_t h, const char *text)
{
	const int32_t textW = (int32_t)strlen(text) * (FONT_CHAR_W - 1);

	drawFramework2(x, y, w, h);

	int32_t textX = x + ((w - textW) / 2);
	int32_t textY = y + ((h - FONT_CHAR_H) / 2);

	// kludge
	if (!strcmp(text, ARROW_UP_STR) || !strcmp(text, ARROW_DOWN_STR))
	{
		textX--;
		textY--;
	}

	textOut2(textX, textY, text);
}

void drawUpButton(int32_t x, int32_t y)
{
	drawFramework1(x, y, 11, 11);
	textOut2(x+1, y+2, ARROW_UP_STR);
}

void drawDownButton(int32_t x, int32_t y)
{
	drawFramework1(x, y, 11, 11);
	textOut2(x+1, y+2, ARROW_DOWN_STR);
}

void statusAllRight(void)
{
	setStatusMessage("ALL RIGHT", DO_CARRY);
}

void statusOutOfMemory(void)
{
	displayErrorMsg("OUT OF MEMORY !!!");
}

void statusSampleIsEmpty(void)
{
	displayErrorMsg("SAMPLE IS EMPTY");
}

void changeStatusText(const char *text)
{
	fillRect(88, 127, 17*FONT_CHAR_W, FONT_CHAR_H, video.palette[PAL_GENBKG]);
	textOut(88, 127, text, video.palette[PAL_GENTXT]);
}

void statusNotSampleZero(void)
{
	/* This rather confusing error message actually means that
	** you can't load a sample to sample slot #0 (which isn't a
	** real sample slot).
	*/
	displayErrorMsg("NOT SAMPLE 0 !");
}

void renderFrame2(void) // use this in askBox()
{
	updateMod2WavDialog(); // must be first to avoid flickering issues

	updateSongInfo1(); // top left side of screen, when "disk op"/"pos ed" is hidden
	updateSongInfo2(); // two middle rows of screen, always visible
	updatePatternData();
	updateSampler();
	handleLastGUIObjectDown(); // XXX
	drawSamplerLine();
	writeSampleMonitorWaveform(); // XXX
}

void renderFrame(void)
{
	renderFrame2();

	updateEditOp();
	updateDiskOp();
	updatePosEd();
	updateVisualizer();

	// show [EDITING] in window title if in edit mode
	if (oldCurrMode != editor.currMode)
	{
		oldCurrMode = editor.currMode;
		if (song != NULL)
			updateWindowTitle(song->modified);
	}
}

void resetAllScreens(void) // prepare GUI for "really quit?" dialog
{
	editor.mixFlag = false;
	editor.swapChannelFlag = false;

	ui.changingChordNote = false;
	ui.changingSmpResample = false;
	ui.changingSamplingNote = false;
	ui.changingDrumPadNote = false;

	ui.disableVisualizer = false;

	if (ui.samplerScreenShown)
	{
		if (ui.samplingBoxShown)
		{
			ui.samplingBoxShown = false;
			removeSamplingBox();
		}

		ui.samplerVolBoxShown = false;
		ui.samplerFiltersBoxShown = false;
		displaySample(); // removes volume/filter box
	}

	if (ui.editTextFlag)
		leaveTextEditMode(EDIT_TEXT_NO_UPDATE);
}

static void fillFromVuMetersBgBuffer(void)
{
	if (ui.samplerScreenShown || editor.mod2WavOngoing || editor.pat2SmpOngoing)
		return;

	const uint32_t *srcPtr = vuMetersBg;
	uint32_t *dstPtr = &video.frameBuffer[(187 * SCREEN_W) + 55];

	for (uint32_t i = 0; i < PAULA_VOICES; i++)
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
	if (ui.samplerScreenShown || editor.mod2WavOngoing || editor.pat2SmpOngoing)
		return;

	const uint32_t *srcPtr = &video.frameBuffer[(187 * SCREEN_W) + 55];
	uint32_t *dstPtr = vuMetersBg;

	for (uint32_t i = 0; i < PAULA_VOICES; i++)
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
	if (ui.samplerScreenShown || editor.mod2WavOngoing || editor.pat2SmpOngoing)
		return;

	fillToVuMetersBgBuffer();

	uint32_t *dstPtr = &video.frameBuffer[(187 * SCREEN_W) + 55];
	for (uint32_t i = 0; i < PAULA_VOICES; i++)
	{
		uint32_t h;
		if (config.realVuMeters)
			h = editor.realVuMeterVolumes[i];
		else
			h = editor.vuMeterVolumes[i];

		if (h > 48)
			h = 48;

		const uint32_t *srcPtr = vuMeterBMP;
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
	if (ui.diskOpScreenShown)
		return;

	moduleSample_t *currSample = &song->samples[editor.currSample];

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
		if (!editor.mod2WavOngoing)
			printThreeDecimalsBg(72, 25, *editor.currLengthDisp, video.palette[PAL_GENTXT], video.palette[PAL_GENBKG]);
	}

	if (ui.updateCurrSampleFineTune)
	{
		ui.updateCurrSampleFineTune = false;

		if (!editor.mod2WavOngoing)
			textOutBg(80, 36, ftuneStrTab[currSample->fineTune & 0xF], video.palette[PAL_GENTXT], video.palette[PAL_GENBKG]);
	}

	if (ui.updateCurrSampleNum)
	{
		ui.updateCurrSampleNum = false;
		if (!editor.mod2WavOngoing)
		{
			printTwoHexBg(80, 47,
				editor.sampleZero ? 0 : ((*editor.currSampleDisp) + 1), video.palette[PAL_GENTXT], video.palette[PAL_GENBKG]);
		}
	}

	if (ui.updateCurrSampleVolume)
	{
		ui.updateCurrSampleVolume = false;
		if (!editor.mod2WavOngoing)
			printTwoHexBg(80, 58, *currSample->volumeDisp, video.palette[PAL_GENTXT], video.palette[PAL_GENBKG]);
	}

	if (ui.updateCurrSampleLength)
	{
		ui.updateCurrSampleLength = false;
		if (!editor.mod2WavOngoing)
		{
			if (config.maxSampleLength == 0xFFFE)
				printFourHexBg(64, 69, *currSample->lengthDisp, video.palette[PAL_GENTXT], video.palette[PAL_GENBKG]);
			else
				printFiveHexBg(56, 69, *currSample->lengthDisp, video.palette[PAL_GENTXT], video.palette[PAL_GENBKG]);
		}
	}

	if (ui.updateCurrSampleRepeat)
	{
		ui.updateCurrSampleRepeat = false;
		if (config.maxSampleLength == 0xFFFE)
			printFourHexBg(64, 80, *currSample->loopStartDisp, video.palette[PAL_GENTXT], video.palette[PAL_GENBKG]);
		else
			printFiveHexBg(56, 80, *currSample->loopStartDisp, video.palette[PAL_GENTXT], video.palette[PAL_GENBKG]);
	}

	if (ui.updateCurrSampleReplen)
	{
		ui.updateCurrSampleReplen = false;
		if (config.maxSampleLength == 0xFFFE)
			printFourHexBg(64, 91, *currSample->loopLengthDisp, video.palette[PAL_GENTXT], video.palette[PAL_GENBKG]);
		else
			printFiveHexBg(56, 91, *currSample->loopLengthDisp, video.palette[PAL_GENTXT], video.palette[PAL_GENBKG]);
	}
}

void updateSongInfo2(void) // two middle rows of screen, always present
{
	if (ui.updateStatusText)
	{
		ui.updateStatusText = false;

		// clear background
		fillRect(88, 127, 17*FONT_CHAR_W, FONT_CHAR_H, video.palette[PAL_GENBKG]);

		// render status text
		if (!editor.errorMsgActive && editor.blockMarkFlag && !ui.askBoxShown && !editor.swapChannelFlag)
		{
			textOut(88, 127, "MARK BLOCK", video.palette[PAL_GENTXT]);
			charOut(192, 127, '-', video.palette[PAL_GENTXT]);

			editor.blockToPos = song->currRow;
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
			printThreeDecimalsBg(32, 123, song->currBPM, video.palette[PAL_GENTXT], video.palette[PAL_GENBKG]);
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
			putPixel(314, 129, video.palette[PAL_GENTXT]);
			putPixel(315, 129, video.palette[PAL_GENTXT]);
		}
		else if (editor.pNoteFlag == 2)
		{
			putPixel(314, 128, video.palette[PAL_GENTXT]);
			putPixel(315, 128, video.palette[PAL_GENTXT]);
			putPixel(314, 130, video.palette[PAL_GENTXT]);
			putPixel(315, 130, video.palette[PAL_GENTXT]);
		}
	}

	// playback timer

	uint32_t seconds = editor.playbackSeconds;
	if (seconds <= 5999) // below 100 minutes (99:59 is max for the UI)
	{
		const uint32_t MI_TimeM = seconds / 60;
		const uint32_t MI_TimeS = seconds - (MI_TimeM * 60);

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
		for (int32_t x = 0; x < 20; x++)
		{
			char tempChar = song->header.name[x];
			if (tempChar == '\0')
				tempChar = '_';

			charOutBg(104 + (x * FONT_CHAR_W), 102, tempChar, video.palette[PAL_GENTXT], video.palette[PAL_GENBKG]);
		}
	}

	if (ui.updateCurrSampleName)
	{
		ui.updateCurrSampleName = false;
		moduleSample_t *currSample = &song->samples[editor.currSample];

		for (int32_t x = 0; x < 22; x++)
		{
			char tempChar = currSample->text[x];
			if (tempChar == '\0')
				tempChar = '_';

			charOutBg(104 + (x * FONT_CHAR_W), 113, tempChar, video.palette[PAL_GENTXT], video.palette[PAL_GENBKG]);
		}
	}

	if (ui.updateSongSize)
	{
		ui.updateSongSize = false;

		// clear background
		fillRect(264, 123, 6*FONT_CHAR_W, FONT_CHAR_H, video.palette[PAL_GENBKG]);

		// calculate module length

		uint32_t totalSampleDataSize = 0;
		for (int32_t i = 0; i < MOD_SAMPLES; i++)
			totalSampleDataSize += song->samples[i].length;

		uint32_t totalPatterns = 0;
		for (int32_t i = 0; i < 128; i++)
		{
			if (song->header.patternTable[i] > totalPatterns)
				totalPatterns = song->header.patternTable[i];
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
	if (!ui.samplerScreenShown || ui.samplingBoxShown)
		return;

	assert(editor.currSample >= 0 && editor.currSample <= 30);
	moduleSample_t *s = &song->samples[editor.currSample];

	// update 9xx offset
	if (mouse.y >= 138 && mouse.y <= 201 && mouse.x >= 3 && mouse.x <= 316)
	{
		if (!ui.samplerVolBoxShown && !ui.samplerFiltersBoxShown && s->length > 0)
		{
			int32_t tmpSampleOffset = 0x900 + (scr2SmpPos(mouse.x-3) >> 8);
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
		if (ui.lastSampleOffset <= 0x900 || ui.lastSampleOffset > 0x9FF)
			textOutBg(288, 247, "---", video.palette[PAL_GENTXT], video.palette[PAL_GENBKG]);
		else
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
	uint32_t sliderStart = ((editor.vol1 * 3) + 5) / 10;
	uint32_t sliderEnd  = sliderStart + 4;
	uint32_t pixel = video.palette[PAL_QADSCP];
	uint32_t bgPixel = video.palette[PAL_BACKGRD];
	uint32_t *dstPtr = &video.frameBuffer[(158 * SCREEN_W) + 105];

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
	uint32_t sliderStart = ((editor.vol2 * 3) + 5) / 10;
	uint32_t sliderEnd = sliderStart + 4;
	uint32_t pixel = video.palette[PAL_QADSCP];
	uint32_t bgPixel = video.palette[PAL_BACKGRD];
	uint32_t *dstPtr = &video.frameBuffer[(169 * SCREEN_W) + 105];

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
	blit32(72, 154, 136, 33, samplerVolumeBMP);

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
	blit32(65, 154, 186, 33, samplerFiltersBMP);

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

void renderMuteButtons(void)
{
	if (ui.diskOpScreenShown || ui.posEdScreenShown)
		return;

	uint32_t *dstPtr = &video.frameBuffer[(3 * SCREEN_W) + 310];
	for (uint32_t i = 0; i < PAULA_VOICES; i++)
	{
		const uint32_t *srcPtr;
		uint32_t srcPitch;

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
	if (ui.disableVisualizer || ui.diskOpScreenShown ||
		ui.posEdScreenShown || ui.editOpScreenShown ||
		ui.aboutScreenShown || ui.askBoxShown ||
		editor.mod2WavOngoing || ui.samplingBoxShown)
	{
		return;
	}

	if (ui.visualizerMode == VISUAL_SPECTRUM)
	{
		// spectrum analyzer

		uint32_t *dstPtr = &video.frameBuffer[(59 * SCREEN_W) + 129];
		for (uint32_t i = 0; i < SPECTRUM_BAR_NUM; i++)
		{
			const uint32_t *srcPtr = analyzerColorsRGB24;
			uint32_t pixel = video.palette[PAL_GENBKG];

			int32_t tmpVol = editor.spectrumVolumes[i];
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
	const uint32_t *srcPtr = &trackerFrameBMP[(44 * SCREEN_W) + 120];
	uint32_t *dstPtr = &video.frameBuffer[(44 * SCREEN_W) + 120];

	for (uint32_t y = 0; y < 55; y++)
	{
		memcpy(dstPtr, srcPtr, 200 * sizeof (int32_t));

		srcPtr += SCREEN_W;
		dstPtr += SCREEN_W;
	}
}

void renderSpectrumAnalyzerBg(void)
{
	blit32(120, 44, 200, 55, spectrumVisualsBMP);
}

void renderAboutScreen(void)
{
	char verString[16];

	if (!ui.aboutScreenShown || ui.diskOpScreenShown || ui.posEdScreenShown || ui.editOpScreenShown)
		return;

	blit32(120, 44, 200, 55, aboutScreenBMP);

	// draw version string

	sprintf(verString, "v%s", PROG_VER_STR);
	uint32_t verStringX = 260 + (((63 - ((uint32_t)strlen(verString) * (FONT_CHAR_W - 1))) + 1) / 2);
	textOutTight(verStringX, 67, verString, video.palette[PAL_GENBKG2]);
}

void renderEditOpMode(void)
{
	const uint32_t *srcPtr;

	// select what character box to render
	switch (ui.editOpScreen)
	{
		default:
		case 0:
			srcPtr = &editOpModeCharsBMP[editor.sampleAllFlag ? EDOP_MODE_BMP_A_OFS : EDOP_MODE_BMP_S_OFS];
		break;

		case 1:
		{
			if (editor.trackPattFlag == 0)
				srcPtr = &editOpModeCharsBMP[EDOP_MODE_BMP_T_OFS];
			else if (editor.trackPattFlag == 1)
				srcPtr = &editOpModeCharsBMP[EDOP_MODE_BMP_P_OFS];
			else
				srcPtr = &editOpModeCharsBMP[EDOP_MODE_BMP_S_OFS];
		}
		break;

		case 2:
			srcPtr = &editOpModeCharsBMP[editor.halveSampleFlag ? EDOP_MODE_BMP_H_OFS : EDOP_MODE_BMP_C_OFS];
		break;

		case 3:
			srcPtr = (editor.newOldFlag == 0) ? &editOpModeCharsBMP[EDOP_MODE_BMP_N_OFS] : &editOpModeCharsBMP[EDOP_MODE_BMP_O_OFS];
		break;
	}

	blit32(310, 47, 7, 6, srcPtr);
}

void renderEditOpScreen(void)
{
	const uint32_t *srcPtr;

	// select which graphics to render
	switch (ui.editOpScreen)
	{
		default:
		case 0: srcPtr = editOpScreen1BMP; break;
		case 1: srcPtr = editOpScreen2BMP; break;
		case 2: srcPtr = editOpScreen3BMP; break;
		case 3: srcPtr = editOpScreen4BMP; break;
	}

	blit32(120, 44, 200, 55, srcPtr);

	// fix graphics in 128K sample mode
	if (config.maxSampleLength != 65534)
	{
		if (ui.editOpScreen == 2)
			blit32(213, 55, 32, 11, fix128KPosBMP);
		else if (ui.editOpScreen == 3)
			blit32(120, 88, 48, 11, fix128KChordBMP);
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

		ui.updateChordLengthText = true;
		ui.updateChordNote1Text = true;
		ui.updateChordNote2Text = true;
		ui.updateChordNote3Text = true;
		ui.updateChordNote4Text = true;
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
			if (config.maxSampleLength == 0xFFFE)
				printFourHexBg(248, 58, *editor.samplePosDisp, video.palette[PAL_GENTXT], video.palette[PAL_GENBKG]);
			else
				printFiveHexBg(240, 58, *editor.samplePosDisp, video.palette[PAL_GENTXT], video.palette[PAL_GENBKG]);
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
		if (ui.updateChordLengthText)
		{
			ui.updateChordLengthText = false;

			// clear background
			if (config.maxSampleLength != 65534)
				textOutBg(160, 91, "     ", video.palette[PAL_GENTXT], video.palette[PAL_GENBKG]);
			else
				textOutBg(168, 91, "    ", video.palette[PAL_GENTXT], video.palette[PAL_GENBKG]);

			charOut(198, 91, ':', video.palette[PAL_GENBKG]);

			if (song->samples[editor.currSample].loopLength > 2 || song->samples[editor.currSample].loopStart >= 2)
			{
				textOut(168, 91, "LOOP", video.palette[PAL_GENTXT]);
			}
			else
			{
				if (config.maxSampleLength == 0xFFFE)
					printFourHex(168, 91, *editor.chordLengthDisp, video.palette[PAL_GENTXT]); // chord max length
				else
					printFiveHex(160, 91, *editor.chordLengthDisp, video.palette[PAL_GENTXT]); // chord max length

				charOut(198, 91, (editor.chordLengthMin) ? '.' : ':', video.palette[PAL_GENTXT]); // min/max flag
			}
		}

		if (ui.updateChordNote1Text)
		{
			ui.updateChordNote1Text = false;
			if (editor.note1 > 35)
				textOutBg(256, 58, "---", video.palette[PAL_GENTXT], video.palette[PAL_GENBKG]);
			else
				textOutBg(256, 58, config.accidental ? noteNames2[2+editor.note1] : noteNames1[2+editor.note1],
					video.palette[PAL_GENTXT], video.palette[PAL_GENBKG]);
		}

		if (ui.updateChordNote2Text)
		{
			ui.updateChordNote2Text = false;
			if (editor.note2 > 35)
				textOutBg(256, 69, "---", video.palette[PAL_GENTXT], video.palette[PAL_GENBKG]);
			else
				textOutBg(256, 69, config.accidental ? noteNames2[2+editor.note2] : noteNames1[2+editor.note2],
					video.palette[PAL_GENTXT], video.palette[PAL_GENBKG]);
		}

		if (ui.updateChordNote3Text)
		{
			ui.updateChordNote3Text = false;
			if (editor.note3 > 35)
				textOutBg(256, 80, "---", video.palette[PAL_GENTXT], video.palette[PAL_GENBKG]);
			else
				textOutBg(256, 80, config.accidental ? noteNames2[2+editor.note3] : noteNames1[2+editor.note3],
					video.palette[PAL_GENTXT], video.palette[PAL_GENBKG]);
		}

		if (ui.updateChordNote4Text)
		{
			ui.updateChordNote4Text = false;
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
		{
			blit32(0, 0, 320, 121, trackerFrameBMP);

			if (config.maxSampleLength != 65534)
				blit32(1, 65, 62, 34, fix128KTrackerBMP); // fix for 128kB support mode
		}
	}
	else
	{
		if (!ui.diskOpScreenShown)
		{
			blit32(0, 0, 320, 255, trackerFrameBMP);

			if (config.maxSampleLength != 65534)
				blit32(1, 65, 62, 34, fix128KTrackerBMP); // fix for 128kB support mode
		}
		else
		{
			blit32(0, 121, 320, 134, &trackerFrameBMP[121 * SCREEN_W]);
		}

		ui.updateSongBPM = true;
		ui.updateCurrPattText = true;
		ui.updatePatternData = true;
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

		// draw zeroes that will never change (to the left of numbers)

		charOut(64,  3, '0', video.palette[PAL_GENTXT]);
		textOut(64, 14, "00", video.palette[PAL_GENTXT]);

		if (!editor.mod2WavOngoing)
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

void videoClose(void)
{
	if (video.texture     != NULL) SDL_DestroyTexture(video.texture);
	if (video.renderer    != NULL) SDL_DestroyRenderer(video.renderer);
	if (video.window      != NULL) SDL_DestroyWindow(video.window);
	if (video.frameBuffer != NULL) free(video.frameBuffer);
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
	for (int32_t i = SPRITE_NUM-1; i >= 0; i--) // erasing must be done in reverse order
	{
		sprite_t *s = &sprites[i];
		if (s->x >= SCREEN_W || s->y >= SCREEN_H) // sprite is hidden, don't draw nor fill clear buffer
			continue;

		assert(s->refreshBuffer != NULL);

		int32_t sw = s->w;
		int32_t sh = s->h;
		int32_t sx = s->x;
		int32_t sy = s->y;

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

		uint32_t *dst32 = &video.frameBuffer[(sy * SCREEN_W) + sx];
		const uint32_t *src32 = s->refreshBuffer;

		// handle x/y clipping
		if (sx+sw >= SCREEN_W) sw = SCREEN_W - sx;
		if (sy+sh >= SCREEN_H) sh = SCREEN_H - sy;

		int32_t srcPitch = s->w - sw;
		int32_t dstPitch = SCREEN_W - sw;

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
	renderVuMeters(); // let's put it here even though it's not sprite-based

	for (int32_t i = 0; i < SPRITE_NUM; i++)
	{
		sprite_t *s = &sprites[i];

		// set new sprite position
		s->x = s->newX;
		s->y = s->newY;

		if (s->x >= SCREEN_W || s->y >= SCREEN_H) // sprite is hidden, don't draw nor fill clear buffer
			continue;

		assert(s->data != NULL && s->refreshBuffer != NULL);

		int32_t sw = s->w;
		int32_t sh = s->h;
		int32_t sx = s->x;
		int32_t sy = s->y;
		int32_t srcPtrBias = 0;

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

		uint32_t *dst32 = &video.frameBuffer[(sy * SCREEN_W) + sx];
		uint32_t *clr32 = s->refreshBuffer;

		// handle x/y clipping
		if (sx+sw >= SCREEN_W) sw = SCREEN_W - sx;
		if (sy+sh >= SCREEN_H) sh = SCREEN_H - sy;

		int32_t srcPitch = s->w - sw;
		int32_t dstPitch = SCREEN_W - sw;

		uint32_t colorKey = sprites[i].colorKey;
		if (sprites[i].pixelType == SPRITE_TYPE_RGB)
		{
			// 24-bit RGB sprite
			const uint32_t *src32 = ((uint32_t *)sprites[i].data) + srcPtrBias;
			for (int32_t y = 0; y < sh; y++)
			{
				for (int32_t x = 0; x < sw; x++)
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
			const uint8_t *src8 = ((uint8_t *)sprites[i].data) + srcPtrBias;
			for (int32_t y = 0; y < sh; y++)
			{
				for (int32_t x = 0; x < sw; x++)
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

void resetFPSCounter(void)
{
	editor.framesPassed = 0;
	fpsTextBuf[0] = '\0';
	dRunningFrameDuration = 1000.0 / VBLANK_HZ;
}

void beginFPSCounter(void)
{
	if (video.debug)
		frameStartTime = SDL_GetPerformanceCounter();
}

void endFPSCounter(void)
{
	if (video.debug && frameStartTime > 0)
	{
		uint64_t frameTimeDiff64 = SDL_GetPerformanceCounter() - frameStartTime;
		if (frameTimeDiff64 > INT32_MAX)
			frameTimeDiff64 = INT32_MAX;

		dRunningFrameDuration += (int32_t)frameTimeDiff64 * hpcFreq.dFreqMulMs;
	}
}

static void drawDebugBox(void)
{
	SDL_version SDLVer;

	SDL_GetVersion(&SDLVer);

	if (editor.framesPassed >= FPS_SCAN_FRAMES && (editor.framesPassed % FPS_SCAN_FRAMES) == 0)
	{
		dAvgFPS = 1000.0 / (dRunningFrameDuration / FPS_SCAN_FRAMES);
		if (dAvgFPS < 0.0 || dAvgFPS > 99999999.9999)
			dAvgFPS = 99999999.9999; // prevent number from overflowing text box

		dRunningFrameDuration = 0.0;
	}

	drawFramework3(4, 4, SCREEN_W-8, 81);

	// if enough frame data isn't collected yet, show a message
	if (editor.framesPassed < FPS_SCAN_FRAMES)
	{
		const char text[] = "Gathering frame information...";
		const uint16_t textW = (sizeof (text)-1) * (FONT_CHAR_W-1);
		textOut2(4+(SCREEN_W-textW)/2, 4+(81/2) - (FONT_CHAR_H/2), text);
		return;
	}

	double dRefreshRate = video.dMonitorRefreshRate;
	if (dRefreshRate < 0.0 || dRefreshRate > 9999.9)
		dRefreshRate = 9999.9; // prevent number from overflowing text box

	sprintf(fpsTextBuf,
	    "SDL version: %u.%u.%u\n" \
	    "Frames per second: %.3f\n" \
	    "Monitor refresh: %.1fHz (vsync: %s)\n" \
	    "HPC frequency (timer): %.4fMHz\n" \
	    "Render size: %dx%d (offset %d,%d)\n" \
	    "Disp. size: %dx%d (window: %dx%d)\n" \
	    "Render scaling: x=%.4f, y=%.4f\n" \
	    "Mouse muls: x=%.4f, y=%.4f\n" \
	    "Relative mouse coords: %d,%d\n" \
	    "Absolute mouse coords: %d,%d\n" \
	    "Press CTRL+SHIFT+F to close this box.\n",
	    SDLVer.major, SDLVer.minor, SDLVer.patch,
	    dAvgFPS,
	    dRefreshRate,
	    video.vsync60HzPresent ? "yes" : "no",
	    hpcFreq.freq64 / (1000.0 * 1000.0),
	    video.renderW, video.renderH, video.renderX, video.renderY,
	    video.displayW, video.displayH, video.windowW, video.windowH,
	    (double)video.renderW / SCREEN_W, (double)video.renderH / SCREEN_H,
	    video.dMouseXMul, video.dMouseYMul,
	    mouse.x, mouse.y,
	    mouse.absX, mouse.absY);

	// draw text

	uint16_t xPos = 4+2;
	uint16_t yPos = 4+3;

	char *textPtr = fpsTextBuf;
	while (*textPtr != '\0')
	{
		const char ch = *textPtr++;
		if (ch == '\n')
		{
			yPos += FONT_CHAR_H+2;
			xPos = 4+2;
			continue;
		}

		charOut(xPos, yPos, ch, 0x00000000);
		xPos += FONT_CHAR_W-1;
	}

	// draw framerate tester symbol

	const uint16_t symbolEnd = 122;

	// ping-pong movement
	uint16_t x = editor.framesPassed % (symbolEnd * 2);
	if (x >= symbolEnd)
		x = (symbolEnd * 2) - x;

	charOut(179+4+x, 4+10, '*', 0x00000000);
}

void flipFrame(void)
{
	const uint32_t windowFlags = SDL_GetWindowFlags(video.window);
	bool minimized = (windowFlags & SDL_WINDOW_MINIMIZED) ? true : false;

	renderSprites();

	if (video.debug)
		drawDebugBox();

	SDL_UpdateTexture(video.texture, NULL, video.frameBuffer, SCREEN_W * sizeof (int32_t));

	// SDL 2.0.14 bug on Windows (?): This function consumes ever-increasing memory if the program is minimized
	if (!minimized)
		SDL_RenderClear(video.renderer);

	if (video.useCustomRenderRect)
		SDL_RenderCopy(video.renderer, video.texture, NULL, &video.renderRect);
	else
		SDL_RenderCopy(video.renderer, video.texture, NULL, NULL);

	SDL_RenderPresent(video.renderer);

	eraseSprites();

	if (!video.vsync60HzPresent)
	{
		// we have no VSync, do crude thread sleeping to sync to ~60Hz
		hpc_Wait(&video.vblankHpc);
	}
	else
	{
		/* We have VSync, but it can unexpectedly get inactive in certain scenarios.
		** We have to force thread sleeping (to ~60Hz) if so.
		*/
#ifdef __APPLE__
		// macOS: VSync gets disabled if the window is 100% covered by another window. Let's add a (crude) fix:
		if (minimized || !(windowFlags & SDL_WINDOW_INPUT_FOCUS))
			hpc_Wait(&video.vblankHpc);
#elif __unix__
		// *NIX: VSync gets disabled in fullscreen mode (at least on some distros/systems). Let's add a fix:
		if (minimized || video.fullscreen)
			hpc_Wait(&video.vblankHpc);
#else
		if (minimized)
			hpc_Wait(&video.vblankHpc);
#endif
	}

	editor.framesPassed++;

	/* Reset audio/video sync timestamp every half an hour to prevent
	** possible sync drifting after hours of playing a song without
	** a single song stop (resets timestamp) in-between.
	*/
	if (editor.framesPassed >= VBLANK_HZ*60*30)
		audio.resetSyncTickTimeFlag = true;
}

void updateSpectrumAnalyzer(uint8_t vol, uint16_t period)
{
	if (ui.visualizerMode != VISUAL_SPECTRUM || vol == 0)
		return;

	/* This routine is buggy in real PT. Behavior for periods outside of 108..907 is undefined.
	** Behavior for sane input outside of 113..856 (B-3 finetune > 0 and C-1 finetune < 0)
	** seems to be defined, and we simulate this.
	*/
	if (period < 108 || period > 907)
		return; // we don't really know what to do here

	// C-1 with finetune < 0 is ignored (confirmed behavior)
	if (period > 856)
		return;

	// B-3 with finetune > 0 behaves like finetune 0 (confirmed behavior)
	if (period < 113)
		period = 113;

	// just in case
	if (vol > 64)
		vol = 64;

	const uint8_t vol24 = (vol * 24) >> 6; // 0..64 -> 0..24

	// convert period from log (113..856) to linear (0..22)
	period = (856 - 113) - (period - 113); // 0..743 (inverted)
	const uint32_t index = (period * period) / 25093; // 0..22 (25093 = round[743^2 / 22])

	// increment bar
	editor.spectrumVolumes[index] += vol24;
	if (editor.spectrumVolumes[index] > SPECTRUM_BAR_HEIGHT)
		editor.spectrumVolumes[index] = SPECTRUM_BAR_HEIGHT;

	// increment left and right neighbor bars with half the volume

	if (index > 0)
	{
		editor.spectrumVolumes[index-1] += vol24 / 2;
		if (editor.spectrumVolumes[index-1] > SPECTRUM_BAR_HEIGHT)
			editor.spectrumVolumes[index-1] = SPECTRUM_BAR_HEIGHT;
	}

	if (index < SPECTRUM_BAR_NUM-1)
	{
		editor.spectrumVolumes[index+1] += vol24 / 2;
		if (editor.spectrumVolumes[index+1] > SPECTRUM_BAR_HEIGHT)
			editor.spectrumVolumes[index+1] = SPECTRUM_BAR_HEIGHT;
	}
}

void sinkVisualizerBars(void) // sinks visualizer bars @ 49.92Hz (Amiga PAL) rate
{
	static uint64_t counter50Hz; // pre-initialized to zero because of static

	counter50Hz += video.amigaVblankDelta; // 0.52 fixed-point
	if (counter50Hz > 1ULL<<52)
	{
		counter50Hz &= (1ULL<<52)-1;

		// sink VU-meters
		for (int32_t i = 0; i < PAULA_VOICES; i++)
		{
			if (editor.vuMeterVolumes[i] > 0)
				editor.vuMeterVolumes[i]--;
		}

		// sink "spectrum analyzer" bars
		for (int32_t i = 0; i < SPECTRUM_BAR_NUM; i++)
		{
			if (editor.spectrumVolumes[i] > 0)
				editor.spectrumVolumes[i]--;
		}
	}
}

void updateRenderSizeVars(void)
{
	SDL_DisplayMode dm;

	int32_t di = SDL_GetWindowDisplayIndex(video.window);
	if (di < 0)
		di = 0; // return display index 0 (default) on error

	SDL_GetDesktopDisplayMode(di, &dm);
	video.displayW = dm.w;
	video.displayH = dm.h;

	SDL_GetWindowSize(video.window, &video.windowW, &video.windowH);
	video.renderX = 0;
	video.renderY = 0;

	video.useCustomRenderRect = false;

	if (video.fullscreen)
	{
		if (config.fullScreenStretch)
		{
			// "streched out" windowed fullscreen

			video.renderW = video.windowW;
			video.renderH = video.windowH;
		}
		else
		{
			// centered windowed fullscreen, with pixel-perfect integer upscaling

			const int32_t maxUpscaleFactor = MIN(video.windowW / SCREEN_W, video.windowH / SCREEN_H);
			video.renderW = SCREEN_W * maxUpscaleFactor;
			video.renderH = SCREEN_H * maxUpscaleFactor;
			video.renderX = (video.windowW - video.renderW) / 2;
			video.renderY = (video.windowH - video.renderH) / 2;

			// get hi-DPI upscale factors (returns 1.0 if no hi-DPI upscaling)
			int32_t widthInPixels, heightInPixels;
			SDL_GL_GetDrawableSize(video.window, &widthInPixels, &heightInPixels);
			double dHiDPIScaleX = (double)widthInPixels / video.windowW;
			double dHiDPIScaleY = (double)heightInPixels / video.windowH;

			video.renderRect.x = (int32_t)floor(video.renderX * dHiDPIScaleX);
			video.renderRect.y = (int32_t)floor(video.renderY * dHiDPIScaleY);
			video.renderRect.w = (int32_t)floor(video.renderW * dHiDPIScaleX);
			video.renderRect.h = (int32_t)floor(video.renderH * dHiDPIScaleY);
			video.useCustomRenderRect = true; // use the destination coordinates above in SDL_RenderCopy()
		}
	}
	else
	{
		// windowed mode

		SDL_GetWindowSize(video.window, &video.renderW, &video.renderH);
	}

	// "hardware mouse" calculations
	video.mouseCursorUpscaleFactor = MIN(video.renderW / SCREEN_W, video.renderH / SCREEN_H);
	createMouseCursors();
}

void toggleFullscreen(void)
{
	video.fullscreen ^= 1;
	if (video.fullscreen)
		SDL_SetWindowFullscreen(video.window, SDL_WINDOW_FULLSCREEN_DESKTOP);
	else
		SDL_SetWindowFullscreen(video.window, 0);

	SDL_Delay(15); // fixes possible issues

	updateRenderSizeVars();
	updateMouseScaling();

	if (video.fullscreen)
	{
		mouse.setPosX = video.displayW / 2;
		mouse.setPosY = video.displayH / 2;
	}
	else
	{
		mouse.setPosX = video.renderW / 2;
		mouse.setPosY = video.renderH / 2;
	}

	mouse.setPosFlag = true;
}

bool setupVideo(void)
{
	if (config.autoFitVideoScale)
	{
		int8_t i;
		SDL_DisplayMode dm;

		int32_t di = SDL_GetWindowDisplayIndex(video.window);
		if (di < 0)
			di = 0; // return display index 0 (default) on error

		// find out which upscaling factor is the biggest to fit on screen
		if (SDL_GetDesktopDisplayMode(di, &dm) == 0)
		{
			for (i = MAX_UPSCALE_FACTOR; i >= 1; i--)
			{
				// height test is slightly taller because of window title, window borders and taskbar/menu/dock
				if (dm.w >= SCREEN_W*i && dm.h >= (SCREEN_H+64)*i)
				{
					config.videoScaleFactor = i;
					break;
				}
			}

			if (i == 0)
				config.videoScaleFactor = 1; // 1x is not going to fit, but use 1x anyways...
		}
		else
		{
			// couldn't get screen resolution, set to 1x
			config.videoScaleFactor = 1;
		}
	}

	int32_t screenW = SCREEN_W * config.videoScaleFactor;
	int32_t screenH = SCREEN_H * config.videoScaleFactor;

	uint32_t rendererFlags = 0;

	SDL_DisplayMode dm;

	int32_t di = SDL_GetWindowDisplayIndex(video.window);
	if (di < 0)
		di = 0; // return display index 0 (default) on error

	SDL_GetDesktopDisplayMode(di, &dm);
	video.dMonitorRefreshRate = (double)dm.refresh_rate;

	video.vsync60HzPresent = false;
	if (!config.vsyncOff)
	{

		if (dm.refresh_rate >= 59 && dm.refresh_rate <= 61)
		{
			video.vsync60HzPresent = true;
			rendererFlags |= SDL_RENDERER_PRESENTVSYNC;
		}
	}

	uint32_t windowFlags = SDL_WINDOW_HIDDEN | SDL_WINDOW_ALLOW_HIGHDPI;

#ifndef SDL_HINT_VIDEO_X11_NET_WM_BYPASS_COMPOSITOR
/* older SDL2 versions don't define this, don't fail the build for it */
#define SDL_HINT_VIDEO_X11_NET_WM_BYPASS_COMPOSITOR "SDL_VIDEO_X11_NET_WM_BYPASS_COMPOSITOR"
#endif
	SDL_SetHint(SDL_HINT_VIDEO_X11_NET_WM_BYPASS_COMPOSITOR, "0");

	video.window = SDL_CreateWindow("", SDL_WINDOWPOS_CENTERED,
		SDL_WINDOWPOS_CENTERED, screenW, screenH, windowFlags);

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

	SDL_SetRenderDrawBlendMode(video.renderer, SDL_BLENDMODE_NONE);

	if (config.pixelFilter == PIXELFILTER_LINEAR)
		SDL_SetHint("SDL_RENDER_SCALE_QUALITY", "linear");
	else if (config.pixelFilter == PIXELFILTER_BEST)
		SDL_SetHint("SDL_RENDER_SCALE_QUALITY", "best");
	else
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
	video.frameBuffer = (uint32_t *)malloc(SCREEN_W * SCREEN_H * sizeof (int32_t));
	if (video.frameBuffer == NULL)
	{
		showErrorMsgBox("Out of memory!");
		return false;
	}

	updateRenderSizeVars();
	updateMouseScaling();

	if (config.hwMouse)
		SDL_ShowCursor(SDL_TRUE);
	else
		SDL_ShowCursor(SDL_FALSE);

	SDL_SetRenderDrawColor(video.renderer, 0, 0, 0, SDL_ALPHA_OPAQUE);
	return true;
}
