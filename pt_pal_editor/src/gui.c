// for finding memory leaks in debug mode with Visual Studio 
#if defined _DEBUG && defined _MSC_VER
#include <crtdbg.h>
#endif

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <SDL2/SDL.h>
#include "tinyfiledialogs/tinyfiledialogs.h"
#include "config.h"
#include "palette.h"
#include "gui.h"

// number of frames
#define BUTTON_DELAY 15
#define BUTTON_REPEAT_DELAY 2

static bool rainbowHeldDown, rainbowDownHeldDown, rainbowUpHeldDown, spreadFlag;
static uint8_t whichSliderHeldDown, spreadFrom;
static uint16_t oldColorVal;
static uint32_t buttonCounter, randSeed;

extern const uint32_t arrowDown[42];
extern const uint32_t arrowUp[42];
extern const uint32_t editText[42880];
extern const uint8_t visualsData[31680];
extern const uint8_t spectrumData[11000];
extern const uint8_t samplerData[42880];
extern const uint8_t songNameData[7040];
extern const uint8_t patternEditorData[42880];
extern const uint8_t diskOpData[31680];
extern const uint8_t fontData[5168];

// globalized
bool topScreenShown = false, bottomScreenShown = false, analyzerShown = false;
uint8_t currColor = 0, rainbowPos = 0, colorsMax = 48;
uint16_t *theRightColors = vuColors;

// Delphi/Pascal LCG Random()
static inline uint32_t myRandom(uint32_t limit)
{
	randSeed = randSeed * 134775813 + 1;
	return ((int64_t)randSeed * limit) >> 32;
}

static void blit(const uint8_t *srcPtr, int32_t xPos, int32_t yPos, int32_t w, int32_t h)
{
	uint32_t pixel;

	uint32_t *dstPtr = &frameBuffer[(yPos * SCREEN_W) + xPos];
	for (int32_t y = 0; y < h; y++)
	{
		for (int32_t x = 0; x < w; x++)
		{
			const uint8_t pal = srcPtr[x];

				 if (pal ==  8) pixel = 0x373737; // sample middle line
			else if (pal ==  9) pixel = 0x666666; // sample mark #1
			else if (pal == 10) pixel = 0xCCCCCC; // sample mark #2
			else pixel = RGB12_to_RGB24(palette[pal]);

			dstPtr[x] = pixel;
		}

		srcPtr += w;
		dstPtr += SCREEN_W;
	}
}

static void blit32(int32_t xPos, int32_t yPos, int32_t w, int32_t h, const uint32_t *srcPtr)
{
	uint32_t *dstPtr = &frameBuffer[(yPos * SCREEN_W) + xPos];
	for (int32_t y = 0; y < h; y++)
	{
		memcpy(dstPtr, srcPtr, w * sizeof (int32_t));

		srcPtr += w;
		dstPtr += SCREEN_W;
	}
}

void charOut(int32_t xPos, int32_t yPos, uint32_t color, char chr)
{
	const uint8_t *srcPtr = &fontData[(chr * (8 * 5)) + 1];
	uint32_t *dstPtr = &frameBuffer[(yPos * SCREEN_W) + xPos];

	for (int32_t y = 0; y < 5; y++)
	{
		for (int32_t x = 0; x < 6; x++)
		{
			if (srcPtr[x])
				dstPtr[x] = color;
		}

		srcPtr += 8;
		dstPtr += SCREEN_W;
	}
}

void textOut(int32_t xPos, int32_t yPos, uint32_t color, const char *text)
{
	int32_t x = xPos;
	while (*text != '\0')
	{
		charOut(x, yPos, color, *text++);
		x += 7;
	}
}

void textOutShadow(int32_t xPos, int32_t yPos, uint32_t fgColor, uint32_t bgColor, const char *text)
{
	int32_t x = xPos;
	while (*text != '\0')
	{
		charOut(x+1, yPos+1, bgColor, *text);
		charOut(x+0, yPos+0, fgColor, *text);

		text++;
		x += 7;
	}
}

void hLine(int32_t x, int32_t y, int32_t w, uint32_t color)
{
#ifdef _DEBUG
	if (x >= SCREEN_W || y >= SCREEN_H || x+w > SCREEN_W)
		__debugbreak();
#endif

	uint32_t *dstPtr = &frameBuffer[(y * SCREEN_W) + x];
	for (int32_t i = 0; i < w; i++)
		dstPtr[i] = color;
}

void vLine(int32_t x, int32_t y, int32_t h, uint32_t color)
{
#ifdef _DEBUG
	if (y >= SCREEN_W || x >= SCREEN_W || y+h > SCREEN_H)
		__debugbreak();
#endif

	uint32_t *dstPtr = &frameBuffer[(y * SCREEN_W) + x];
	for (int32_t i = 0; i < h; i++)
	{
		*dstPtr  = color;
		 dstPtr += SCREEN_W;
	}
}

static void drawSpectrumAnalyzer(int32_t xPos, int32_t yPos)
{
	randSeed = 8675309;

	int32_t drawX = xPos + 129;
	for (int32_t i = 0; i < 23; i++)
	{
		const int32_t h = myRandom(35+1);

		int32_t drawY = 35 - h;
		const uint32_t *srcPtr = &spectrumAnalyzerBMP[h];
		uint32_t *dstPtr = &frameBuffer[((yPos+drawY+59) * SCREEN_W) + drawX];

		for (int32_t y = 0; y <= h; y++)
		{
			for (int32_t x = 0; x < 6; x++)
				dstPtr[x] = *srcPtr;

			dstPtr += SCREEN_W;
			srcPtr--;
		}

		drawX += 8;
	}
}

static void drawVUMeters(int32_t xPos, int32_t yPos)
{
	randSeed = 81549300;

	int32_t drawX = xPos + 55;
	for (int32_t i = 0; i < 4; i++)
	{
		int32_t h = myRandom(48+1);

		int32_t drawY  = 48 - h;
		const uint32_t *srcPtr = &vuMeterBMP[((48 - 1) * 10) - (drawY * 10)];
		uint32_t *dstPtr = &frameBuffer[((yPos+drawY+140) * SCREEN_W) + drawX];

		for (int32_t y = 0; y < h; y++)
		{
			for (int32_t x = 0; x < 10; x++)
				dstPtr[x]= srcPtr[x];

			dstPtr += SCREEN_W;
			srcPtr -= 10;
		}

		drawX += 72;
	}
}

static void drawPatternCursor(int32_t xPos, int32_t yPos)
{
	const uint32_t *srcPtr = patternCursorBMP;
	uint32_t *dstPtr = &frameBuffer[((yPos+188) * SCREEN_W) + (xPos+30)];

	for (int32_t y = 0; y < 14; y++)
	{
		for (int32_t x = 0; x < 11; x++)
		{
			const uint32_t pixel = srcPtr[x];
			if (pixel != RGB24_COLORKEY)
				dstPtr[x] = pixel;
		}

		srcPtr += 11;
		dstPtr += SCREEN_W;
	}
}

void drawTracker(void)
{
	const int32_t x = TRACKER_X;
	const int32_t y = TRACKER_Y;

	if (!topScreenShown)
		blit(visualsData, x, y, 320, 99);
	else
		blit(diskOpData,  x, y, 320, 99);

	if (!topScreenShown && analyzerShown)
	{
		blit(spectrumData, x+120, y+44, 200, 55);
		drawSpectrumAnalyzer(x, y);
	}

	blit(songNameData, x, y+99, 320, 22);

	if (!bottomScreenShown)
		blit(patternEditorData, x, y+121, 320, 134);
	else
		blit(samplerData, x, y+121, 320, 134);

	if (!bottomScreenShown)
	{
		drawVUMeters(x, y);
		drawPatternCursor(x, y);
	}
}

void handleSlidersHeldDown(void)
{
	int8_t value;
	int32_t mx;

	if (whichSliderHeldDown == 0)
		return; // no slider held down

	if (whichSliderHeldDown <= 3)
	{
		// palette RGB sliders

		mx = mouse.x - (COLORPICKER1_X+26);
		if (mx < 0)
			mx = 0;

		value = (int8_t)(mx / 3);
		if (value > 15)
			value = 15;

		oldColorVal = palette[currColor];

		palette[currColor] &= ~(15    << (4 * (3 - whichSliderHeldDown)));
		palette[currColor] |=  (value << (4 * (3 - whichSliderHeldDown)));

		if (palette[currColor] != oldColorVal)
		{
			configIsSaved = false;

			if (currColor == 5)
				updatePatternCursorBMP();

			drawColorPicker1();
			drawTracker();
			redrawScreen = true;
		}
	}
	else if (whichSliderHeldDown <= 6)
	{
		// visualizer RGB sliders

		mx = mouse.x - (COLORPICKER2_X+26);
		if (mx < 0)
			mx = 0;

		value = (int8_t)(mx / 3);
		if (value > 15)
			value = 15;

		oldColorVal = theRightColors[rainbowPos];

		theRightColors[rainbowPos] &= ~(15    << (4 * (6 - whichSliderHeldDown)));
		theRightColors[rainbowPos] |=  (value << (4 * (6 - whichSliderHeldDown)));

		if (theRightColors[rainbowPos] != oldColorVal)
		{
			configIsSaved = false;

			if (theRightColors == vuColors)
				updateVuMeterBMP();
			else
				updateSpectrumAnalyzerBMP();

			drawColorPicker2();
			drawTracker();
			redrawScreen = true;
		}
	}
}

void handleRainbowHeldDown(void)
{
	if (!rainbowHeldDown)
		return;

	int32_t my = mouse.y - (COLORPICKER2_Y+3);

	     if (my <  0) my = 0;
	else if (my > 47) my = 47;

	if (theRightColors == analyzerColors && my > 35)
		my = 35;

	uint8_t oldOffset = rainbowPos;

	rainbowPos = (uint8_t)my;
	if (rainbowPos != oldOffset)
	{
		setUndo2Color(rainbowPos);
		drawColorPicker2();
		redrawScreen = true;
	}
}

static void rainbowUp(void)
{
	uint16_t tmp = *theRightColors;
	for (int32_t i = 0; i < colorsMax-1; i++)
		theRightColors[i] = theRightColors[i+1];
	theRightColors[colorsMax-1] = tmp;

	if (theRightColors == vuColors)
		updateVuMeterBMP();
	else
		updateSpectrumAnalyzerBMP();

	drawColorPicker2();
	drawTracker();
	redrawScreen = true;
}

static void rainbowDown(void)
{
	uint16_t tmp = theRightColors[colorsMax-1];
	for (int32_t i = colorsMax-1; i >= 1; i--)
		theRightColors[i] = theRightColors[i-1];
	theRightColors[0] = tmp;

	if (theRightColors == vuColors)
		updateVuMeterBMP();
	else
		updateSpectrumAnalyzerBMP();

	drawColorPicker2();
	drawTracker();
	redrawScreen = true;
}

static void rainbowSpread(void)
{
	spreadFlag = true;
	spreadFrom = rainbowPos;
}

#ifndef __APPLE__
static void colorPicker1(void)
{
	const uint16_t color = colorPicker(palette[currColor]);
	if (color != 0xFFFF)
	{
		palette[currColor] = color & 0xFFF;

		if (currColor == 5)
			updatePatternCursorBMP();

		drawColorPicker1();
		drawTracker();
		redrawScreen = true;
		configIsSaved = false;
	}
}

static void colorPicker2(void)
{
	const uint16_t color = colorPicker(theRightColors[rainbowPos]);
	if (color != 0xFFFF)
	{
		theRightColors[rainbowPos] = color & 0xFFF;
		if (theRightColors == vuColors)
			updateVuMeterBMP();
		else
			updateSpectrumAnalyzerBMP();

		drawColorPicker2();
		drawTracker();
		redrawScreen = true;
		configIsSaved = false;
	}
}
#endif

static bool handleColorPicker1(int32_t mx, int32_t my)
{
	uint8_t newColor;

	// color picker #1 buttons
	if (mx >= COLORPICKER1_X && mx < COLORPICKER1_X+108 && my >= COLORPICKER1_Y+33 && my < COLORPICKER1_Y+44)
	{
		if (mx >= COLORPICKER1_X+79) // "DEF"
		{
			configIsSaved = false;
			setDefaultPalette();
			updatePatternCursorBMP();
			drawTracker();
			drawColorPicker1();
			redrawScreen = true;
			return true;
		}
		else if (mx >= COLORPICKER1_X+33) // "CANCEL"
		{
			configIsSaved = false;
			cancel1Color();
			updatePatternCursorBMP();
			drawTracker();
			drawColorPicker1();
			redrawScreen = true;
			return true;
		}
		else // "UNDO"
		{
			configIsSaved = false;
			undo1Color();
			updatePatternCursorBMP();
			drawTracker();
			drawColorPicker1();
			redrawScreen = true;
			return true;
		}
	}

	// color picker #1 sliders
	if (mx >= COLORPICKER1_X+0 && mx < COLORPICKER1_X+79)
	{
		if (my >= COLORPICKER1_Y+0 && my < COLORPICKER1_Y+11)
		{
			whichSliderHeldDown = 1; // #1 R
			return true;
		}
		else if (my >= COLORPICKER1_Y+11 && my < COLORPICKER1_Y+22)
		{
			whichSliderHeldDown = 2; // #1 G
			return true;
		}
		else if (my >= COLORPICKER1_Y+22 && my < COLORPICKER1_Y+33)
		{
			whichSliderHeldDown = 3; // #1 B
			return true;
		}
	}

	// color picker #1 colors
	if (mx >= COLORPICKER1_X+82 && my >= COLORPICKER1_Y+5 && my < COLORPICKER1_Y+28)
	{
		if (mx < (COLORPICKER1_X+92))
		{
			// left colors
			newColor = (uint8_t)((my - (COLORPICKER1_Y+5)) / 6);
			if (currColor != newColor)
			{
				currColor = newColor;
				setUndo1Color(currColor);
				drawColorPicker1();
				redrawScreen = true;
			}

			return true;
		}
		else if (mx >= COLORPICKER1_X+95 && mx < COLORPICKER1_X+105)
		{
			// right colors
			newColor = (uint8_t)(4 + ((my - (COLORPICKER1_Y+5)) / 6));
			if (currColor != newColor)
			{
				currColor = newColor;
				setUndo1Color(currColor);
				drawColorPicker1();
				redrawScreen = true;
			}
		}
	}

#ifndef __APPLE__
	// "PICK"
	if (mx >= COLORPICKER1_X && mx < COLORPICKER1_X+33 && my >= COLORPICKER1_Y+44 && my < COLORPICKER1_Y+55)
		colorPicker1();
#endif

	return false;
}

void handleRainbowUpDownButtons(void)
{
	if (buttonCounter++ < BUTTON_DELAY || buttonCounter % BUTTON_REPEAT_DELAY != 0)
		return;

	const int32_t mx = mouse.x;
	const int32_t my = mouse.y;

	if (rainbowUpHeldDown)
	{
		if (my >= COLORPICKER2_Y+12 && my < COLORPICKER2_Y+22
		 && mx >= COLORPICKER2_X+0  && mx < COLORPICKER2_X+16)
			rainbowUp();
	}
	else if (rainbowDownHeldDown)
	{
		if (my >= COLORPICKER2_Y+12 && my < COLORPICKER2_Y+22
		 && mx >= COLORPICKER2_X+16 && mx < COLORPICKER2_X+32)
			rainbowDown();
	}
}

static bool handleColorPicker2(int32_t mx, int32_t my)
{
	// color picker rainbow
	if (mx >= COLORPICKER2_X+79 && mx < COLORPICKER2_X+108 && my >= COLORPICKER2_Y && my < COLORPICKER2_Y+55)
	{
		rainbowHeldDown = true;
		return true;
	}

	spreadFlag = false;

	// color picker #2 edit button
	if (my >= COLORPICKER2_Y && my < COLORPICKER2_Y+11 && mx >= COLORPICKER2_X && mx < COLORPICKER2_X+79)
	{
		if (theRightColors == vuColors)
		{
			theRightColors = analyzerColors;
			colorsMax = 36;

			// make sure pos doesn't overflow analyzer graphics lines
			if (rainbowPos > 35)
				rainbowPos = 0;
		}
		else
		{
			theRightColors = vuColors;
			colorsMax = 48;
		}

		drawColorPicker2();
		redrawScreen = true;
		return true;
	}

	// color picker #2 top buttons
	if (mx >= COLORPICKER2_X && mx < COLORPICKER2_X+79 && my >= COLORPICKER2_Y+12 && my < COLORPICKER2_Y+22)
	{
		if (mx >= COLORPICKER2_X+32)
		{
			// "SPREAD"

			rainbowSpread();
			return true;
		}
		else if (mx >= COLORPICKER2_X+16)
		{
			// "DOWN"

			configIsSaved = false;
			rainbowDown();
			rainbowDownHeldDown = true;
			buttonCounter = 0;
			return true;
		}
		else
		{
			// "UP"

			configIsSaved = false;
			rainbowUp();
			rainbowUpHeldDown = true;
			buttonCounter = 0;
			return true;
		}
	}

	// color picker #2 bottom buttons
	if (mx >= COLORPICKER2_X && mx < COLORPICKER2_X+108 && my >= COLORPICKER2_Y+55 && my < COLORPICKER2_Y+66)
	{
		if (mx >= COLORPICKER2_X+79)
		{
			// "DEF"

			configIsSaved = false;

			if (theRightColors == vuColors)
			{
				setDefaultVuColors();
				updateVuMeterBMP();
			}
			else
			{
				setDefaultAnalyzerColors();
				updateSpectrumAnalyzerBMP();
			}

			drawTracker();
			drawColorPicker2();
			redrawScreen = true;
			return true;
		}
		else if (mx >= COLORPICKER2_X+33)
		{
			// "CANCEL"

			configIsSaved = false;
			cancel2Color();

			if (theRightColors == vuColors)
				updateVuMeterBMP();
			else
				updateSpectrumAnalyzerBMP();

			drawTracker();
			drawColorPicker2();
			redrawScreen = true;
			return true;
		}
		else
		{
			// "UNDO"

			configIsSaved = false;
			undo2Color();

			if (theRightColors == vuColors)
				updateVuMeterBMP();
			else
				updateSpectrumAnalyzerBMP();

			drawTracker();
			drawColorPicker2();
			redrawScreen = true;
			return true;
		}
	}

	// color picker #2 sliders
	if (mx >= COLORPICKER2_X+0 && mx < COLORPICKER2_X+79)
	{
		if (my >= COLORPICKER2_Y+13 && my < COLORPICKER2_Y+33)
		{
			whichSliderHeldDown = 4; // #2 R
			return true;
		}
		else if (my >= COLORPICKER2_Y+33 && my < COLORPICKER2_Y+44)
		{
			whichSliderHeldDown = 5; // #2 G
			return true;
		}
		else if (my >= COLORPICKER2_Y+44 && my < COLORPICKER2_Y+55)
		{
			whichSliderHeldDown = 6; // #2 B
			return true;
		}
	}

#ifndef __APPLE__
	// "PICK"
	if (mx >= COLORPICKER2_X && mx < COLORPICKER2_X+33 && my >= COLORPICKER2_Y+66 && my < COLORPICKER2_Y+77)
		colorPicker2();
#endif

	return false;
}

static bool handleTrackerButtons(int32_t mx, int32_t my)
{
	if (!topScreenShown)
	{
		// visuals screen

		// "DISK OP."
		if (mx >= TRACKER_X+182 && my >= TRACKER_Y+33 && mx < TRACKER_X+244 && my < TRACKER_Y+44)
		{
			topScreenShown = true;
			drawTracker();
			redrawScreen = true;
			return true;
		}

		// "SAMPLER"
		else if (mx >= TRACKER_X+244 && my >= TRACKER_Y+33 && mx < TRACKER_X+306 && my < TRACKER_Y+44)
		{
			bottomScreenShown ^= 1;
			drawTracker();
			redrawScreen = true;
			return true;
		}

		// quadrascope (toggle spectrum analyzer)
		else if (mx >= TRACKER_X+120 && mx < TRACKER_X+320 && my >= TRACKER_Y+44 && my < TRACKER_Y+99)
		{
			analyzerShown ^= 1;
			drawTracker();
			redrawScreen = true;
			return true;
		}
	}
	else
	{
		// disk op. screen

		// "EXIT"
		if (mx >= TRACKER_X+308 && mx < TRACKER_X+320 && my >= TRACKER_Y+40 && my < TRACKER_Y+81)
		{
			topScreenShown = false;
			drawTracker();
			redrawScreen = true;
			return true;
		}
	}

	if (bottomScreenShown)
	{
		// sampler screen

		// "EXIT"
		if (mx >= TRACKER_X+7 && my >= TRACKER_Y+124 && mx < TRACKER_X+26 && my < TRACKER_Y+135)
		{
			bottomScreenShown ^= 1;
			drawTracker();
			redrawScreen = true;
			return true;
		}
	}

	return false;
}

static bool handleLoadSaveButtons(int32_t mx, int32_t my)
{
	if (mx >= LOADINI_X && mx < LOADINI_X+108 && my >= LOADINI_Y && my < LOADINI_Y+11)
	{
		// "LOAD COLORS.INI"
		loadColorsDotIni();
		return true;
	}

	if (mx >= LOADPT_X && mx < LOADPT_X+108 && my >= LOADPT_Y && my < LOADPT_Y+11)
	{
		// "LOAD PT.CONFIG"
		loadPTDotConfig();
		return true;
	}

	if (mx >= SAVE_X && mx < SAVE_X+108 && my >= SAVE_Y && my < SAVE_Y+11)
	{
		// "SAVE COLORS.INI"
		savePalette(true);
		return true;
	}

	return false;
}

static uint16_t colCrossFade(uint16_t col1, uint16_t col2, uint16_t idx, uint16_t len)
{
	uint16_t color = 0;
	for (int32_t ch = 0; ch < 3; ch++)
	{
		const int32_t a = (col1 >> (ch * 4)) & 15;
		const int32_t b = (col2 >> (ch * 4)) & 15;

		int32_t nybble = (uint16_t)((((a * ((len * 2) - (idx * 2))) + len) + (b * (idx * 2))) / (len * 2));
		if (nybble > 15) nybble = 15;

		color |= nybble << (ch * 4);
	}

	return color;
}

static void chkSpread(void)
{
	spreadFlag = false;

	uint8_t x1 = spreadFrom;
	uint8_t x2 = rainbowPos;

	// exchange x1/x2 if x1>x2
	if (x1 > x2)
	{
		uint8_t tmp = x1;
		x1 = x2;
		x2 = tmp;
	}

	uint16_t length = x2 - x1;
	if (length <= 1)
		return;

	for (uint8_t i = 0; i <= length; i++)
		theRightColors[x1+i] = colCrossFade(theRightColors[x1], theRightColors[x2], i, length);

	if (theRightColors == vuColors)
		updateVuMeterBMP();
	else
		updateSpectrumAnalyzerBMP();

	drawTracker();
	drawColorPicker2();
	redrawScreen = true;
	configIsSaved = false;
}

void mouseButtonUpHandler(void)
{
	if (spreadFlag && rainbowHeldDown)
		chkSpread();

	buttonCounter = 0;
	whichSliderHeldDown = 0;
	rainbowHeldDown = false;
	rainbowUpHeldDown = false;
	rainbowDownHeldDown = false;
}

void mouseButtonDownHandler(void)
{
	uint32_t mx = mouse.x;
	uint32_t my = mouse.y;

	if (handleColorPicker1(mx, my))
		return;

	if (handleColorPicker2(mx, my))
		return;

	if (handleTrackerButtons(mx, my))
		return;

	if (handleLoadSaveButtons(mx, my))
		return;
}

void keyDownHandler(SDL_Keycode keyEntry)
{
	// exit sampler/disk op.
	if (keyEntry == SDLK_ESCAPE)
	{
		topScreenShown = false;
		bottomScreenShown = false;
		drawTracker();
		redrawScreen = true;
	}
}

static void fillRect(int32_t xPos, int32_t yPos, int32_t w, int32_t h, uint32_t color)
{
	uint32_t *dstPtr = &frameBuffer[(yPos * SCREEN_W) + xPos];
	for (int32_t y = 0; y < h; y++)
	{
		for (int32_t x = 0; x < w; x++)
			dstPtr[x] = color;

		dstPtr += SCREEN_W;
	}
}

static void drawBox1(int32_t x, int32_t y, int32_t w, int32_t h)
{
	fillRect(x + 1, y + 1, w - 1, h - 1, 0x888888);

	hLine(x+0,     y+0, w-1, 0xBBBBBB);
	vLine(x+0,     y+1, h-2, 0xBBBBBB);
	hLine(x+1,   y+h-1, w-1, 0x555555);
	vLine(x+w-1,   y+1, h-2, 0x555555);

	frameBuffer[((y + (h - 1)) * SCREEN_W) + x] = 0x888888;
	frameBuffer[(y * SCREEN_W) + (x + (w - 1))] = 0x888888;
}

static void drawBox2(int32_t x, int32_t y, int32_t w, int32_t h)
{
	fillRect(x + 1, y + 1, w - 1, h - 1, 0);

	hLine(x+0,     y+0, w-1, 0x555555);
	vLine(x+0,     y+1, h-2, 0x555555);
	hLine(x+1,   y+h-1, w-1, 0xBBBBBB);
	vLine(x+w-1,   y+1, h-2, 0xBBBBBB);

	frameBuffer[((y + (h - 1)) * SCREEN_W) + x] = 0x888888;
	frameBuffer[(y * SCREEN_W) + (x + (w - 1))] = 0x888888;
}

static void drawSliders1(int32_t x, int32_t y)
{
	int32_t xPos;

	// R
	xPos = x + 25 + (R12(palette[currColor]) * 3);
	fillRect(xPos, y + 4, 5, 3, 0xFFDD00);

	// G
	xPos = x + 25 + (G12(palette[currColor]) * 3);
	fillRect(xPos, y + 15, 5, 3, 0xFFDD00);

	// B
	xPos = x + 25 + (B12(palette[currColor]) * 3);
	fillRect(xPos, y + 26, 5, 3, 0xFFDD00);
}

static void drawSliders2(int32_t x, int32_t y)
{
	int32_t xPos;

	// R
	xPos = x + 25 + (R12(theRightColors[rainbowPos]) * 3);
	fillRect(xPos, y + 26, 5, 3, 0xFFDD00);

	// G
	xPos = x + 25 + (G12(theRightColors[rainbowPos]) * 3);
	fillRect(xPos, y + 37, 5, 3, 0xFFDD00);

	// B
	xPos = x + 25 + (B12(theRightColors[rainbowPos]) * 3);
	fillRect(xPos, y + 48, 5, 3, 0xFFDD00);
}

void drawColorPicker1(void)
{
	char strBuf[24];
	int32_t i, j, xPos, yPos;
	uint32_t color;

	const uint32_t x = COLORPICKER1_X;
	const uint32_t y = COLORPICKER1_Y;

	// R, G, B
	drawBox1(x, y,    21, 11); textOutShadow(x+7, y+ 3, 0xBBBBBB, 0x555555, "R");
	drawBox1(x, y+11, 21, 11); textOutShadow(x+7, y+14, 0xBBBBBB, 0x555555, "G");
	drawBox1(x, y+22, 21, 11); textOutShadow(x+7, y+25, 0xBBBBBB, 0x555555, "B");

	// sliders
	drawBox1(x+21, y,    58, 11); drawBox2(x+23, y+2,    54, 7);
	drawBox1(x+21, y+11, 58, 11); drawBox2(x+23, y+2+11, 54, 7);
	drawBox1(x+21, y+22, 58, 11); drawBox2(x+23, y+2+22, 54, 7);

	// draw slider ticks
	for (i = 0; i < 3; i++)
	{
		xPos = x + 27;
		yPos = y + 2 + (i * 11);

		for (j = 0; j < 16; j++)
		{
			if ((j % 4) == 0 || j == 15)
				frameBuffer[((yPos-1) * SCREEN_W) + xPos] = 0xBBBBBB;

			frameBuffer[(yPos * SCREEN_W) + xPos] = 0xBBBBBB;

			xPos += 3;
		}
	}

	// buttons
	drawBox1(x,    y+33, 33, 11); textOutShadow(x+3,  y+36, 0xBBBBBB, 0x555555, "UNDO");
	drawBox1(x+33, y+33, 46, 11); textOutShadow(x+35, y+36, 0xBBBBBB, 0x555555, "CANCEL");
	drawBox1(x+79, y+33, 29, 11); textOutShadow(x+84, y+36, 0xBBBBBB, 0x555555, "DEF");

#ifndef __APPLE__
	// "PICK" button
	drawBox1(x, y + 44, 33, 11); textOutShadow(x + 3, y + 47, 0xBBBBBB, 0x555555, "PICK");

	// color value
	drawBox1(x + 33, y + 44, 75, 11);
	sprintf(strBuf, "%06X", RGB12_to_RGB24(palette[currColor]));
	textOut(x + 50, y + 47, 0, strBuf);
#else
	// color value
	drawBox1(x, y + 44, 108, 11); textOutShadow(x + 3, y + 47, 0xBBBBBB, 0x555555, "RGB24:");
	sprintf(strBuf, "%06X", RGB12_to_RGB24(palette[currColor]));
	textOut(x + 55, y + 47, 0, strBuf);
#endif

	// colors
	drawBox1(x+79, y, 29, 33); drawBox2(x+81, y+3, 12, 27); drawBox2(x+94, y+3, 12, 27);

	// draw colors
	for (i = 0; i < 2; i++)
	{
		xPos = (i == 0) ? (x + 83) : (x + 96);
		for (j = 0; j < 4; j++)
		{
			yPos = y + 5 + (j * 6);

			color = (i * 4) + j;
			if (color == currColor) // palette is selected
				fillRect(xPos-1, yPos-1, 10, 7, 0xFFDD00);

			fillRect(xPos, yPos, 8, 5, RGB12_to_RGB24(palette[color]));
		}
	}

	drawSliders1(x, y);
}

void drawColorPicker2(void)
{
	char strBuf[24];
	int32_t i, j, xPos, yPos;
	uint32_t color, *dstPtr;

	const uint32_t x = COLORPICKER2_X;
	const uint32_t y = COLORPICKER2_Y;

	// edit mode box
	drawBox1(x, y, 79, 11);
	blit32(x+2, y+3, 19, 6, editText);
	textOut(x+22, y+3, 0, (theRightColors == vuColors) ? "VU-METER" : "ANALYZER");

	// colors box
	drawBox1(x+79, y+0, 29, 55);
	drawBox2(x+81, y+1, 25, 53);

	// top buttons
	drawBox1(x+0,  y+11, 16, 11); blit32(x+5,  y+13, 6, 7, arrowUp);
	drawBox1(x+16, y+11, 16, 11); blit32(x+21, y+13, 6, 7, arrowDown);
	drawBox1(x+32, y+11, 47, 11); textOutShadow(x+35, y+14, 0xBBBBBB, 0x555555, "SPREAD");

	// R, G, B
	drawBox1(x, y+22, 21, 11); textOutShadow(x+7, y+25, 0xBBBBBB, 0x555555, "R");
	drawBox1(x, y+33, 21, 11); textOutShadow(x+7, y+36, 0xBBBBBB, 0x555555, "G");
	drawBox1(x, y+44, 21, 11); textOutShadow(x+7, y+47, 0xBBBBBB, 0x555555, "B");

	// sliders
	drawBox1(x+21, y+22, 58, 11); drawBox2(x+23, y+24, 54, 7);
	drawBox1(x+21, y+33, 58, 11); drawBox2(x+23, y+35, 54, 7);
	drawBox1(x+21, y+44, 58, 11); drawBox2(x+23, y+46, 54, 7);

	// draw slider ticks
	for (i = 0; i < 3; i++)
	{
		xPos = x + 27;
		yPos = y + 24 + (i * 11);

		for (j = 0; j < 16; j++)
		{
			if ((j % 4) == 0 || j == 15)
				frameBuffer[((yPos-1) * SCREEN_W) + xPos] = 0xBBBBBB;

			frameBuffer[(yPos * SCREEN_W) + xPos] = 0xBBBBBB;

			xPos += 3;
		}
	}

	// bottom buttons
	drawBox1(x,    y+55, 33, 11); textOutShadow(x+3,  y+58, 0xBBBBBB, 0x555555, "UNDO");
	drawBox1(x+33, y+55, 46, 11); textOutShadow(x+35, y+58, 0xBBBBBB, 0x555555, "CANCEL");
	drawBox1(x+79, y+55, 29, 11); textOutShadow(x+84, y+58, 0xBBBBBB, 0x555555, "DEF");

	// draw colors
	for (i = 0; i < 48; i++)
	{
		if (i > 35 && theRightColors == analyzerColors)
			color = 0; // spectrum analyzer graphics only has 36 lines
		else
			color = RGB12_to_RGB24(theRightColors[i]);

		// draw current line
		dstPtr = &frameBuffer[((y+3+i) * SCREEN_W) + (x+88)];
		for (j = 0; j < 11; j++)
			dstPtr[j] = color;
	}

	// draw color selection lines
	hLine(x+ 83, y+3+rainbowPos, 4, 0xFFDD00);
	hLine(x+100, y+3+rainbowPos, 4, 0xFFDD00);

#ifndef __APPLE__
	// "PICK" button
	drawBox1(x, y + 66, 33, 11); textOutShadow(x + 3, y + 69, 0xBBBBBB, 0x555555, "PICK");

	// color value
	color = RGB12_to_RGB24(theRightColors[rainbowPos]);
	drawBox1(x + 33, y + 66, 75, 11);
	sprintf(strBuf, "%06X", color);
	textOut(x + 50, y + 69, 0, strBuf);
#else
	// color value
	color = RGB12_to_RGB24(theRightColors[rainbowPos]);
	drawBox1(x, y + 66, 108, 11); textOutShadow(x + 3, y + 69, 0xBBBBBB, 0x555555, "RGB24:");
	sprintf(strBuf, "%06X", color);
	textOut(x + 55, y + 69, 0, strBuf);
#endif

	drawSliders2(x, y);
}

void setupGUI(void)
{
	fillRect(0, 0, SCREEN_W, SCREEN_H, 0x323E68);

	setDefaultPalette();
	setDefaultVuColors();
	setDefaultAnalyzerColors();
	updateBMPs();
	fillCancel1Colors();
	fillCancel2Colors();
	setUndo1Color(currColor);
	setUndo2Color(rainbowPos);

	// draw texts
	textOutShadow(98,    5, 0xF2F2F2, 0x0F121C, "- TRACKER PREVIEW -");
	textOutShadow(343,   5, 0xF2F2F2, 0x0F121C, "- PALETTE -");
	textOutShadow(332,  93, 0xF2F2F2, 0x0F121C, "- VISUALIZER -");
	textOutShadow(325, 244, 0xF2F2F2, 0x0F121C, "<-- Clickable:");
	textOutShadow(326, 252, 0xF2F2F2, 0x0F121C, "DISK OP.");
	textOutShadow(326, 258, 0xF2F2F2, 0x0F121C, "SAMPLER");
	textOutShadow(326, 264, 0xF2F2F2, 0x0F121C, "QUADRASCOPE");

	drawTracker();

	// draw tracker preview border
	hLine(TRACKER_X-1,   TRACKER_Y-1,   320+2, 0xF2F2F2);
	vLine(TRACKER_X-1,   TRACKER_Y+0,   255+0, 0xF2F2F2);
	hLine(TRACKER_X-1,   TRACKER_Y+255, 320+2, 0xF2F2F2);
	vLine(TRACKER_X+320, TRACKER_Y+0,   255+0, 0xF2F2F2);

	drawColorPicker1();
	drawColorPicker2();

	// draw load/save buttons
	drawBox1(LOADINI_X, LOADINI_Y, 108, 11); textOutShadow(LOADINI_X+2, LOADINI_Y+3, 0xBBBBBB, 0x555555, "LOAD COLORS.INI");
	drawBox1(LOADPT_X,  LOADPT_Y,  108, 11); textOutShadow(LOADPT_X+5,  LOADPT_Y+3,  0xBBBBBB, 0x555555, "LOAD PT.CONFIG");
	drawBox1(SAVE_X,    SAVE_Y,    108, 11); textOutShadow(SAVE_X+2,    SAVE_Y+3,    0xBBBBBB, 0x555555, "SAVE COLORS.INI");

	redrawScreen = true;
}
