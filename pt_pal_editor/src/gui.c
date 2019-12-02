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

static uint8_t rainbowHeldDown, rainbowDownHeldDown, rainbowUpHeldDown, sliderHeldDown;
static uint8_t spreadFlag, spreadFrom;
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
bool topScreen = false, bottomScreen = false, analyzerShown = false;
uint8_t currColor = 0, rainbowPos = 0, colorsMax = 48;
uint16_t *theRightColors = vuColors;

// Delphi/Pascal LCG Random()
static inline uint32_t myRandom(uint32_t limit)
{
    randSeed = randSeed * 134775813 + 1;
    return (((uint32_t)(randSeed) * (uint64_t)(limit)) >> 32ULL);
}

static void blit(const uint8_t *srcPtr, uint32_t xPos, uint32_t yPos, uint32_t w, uint32_t h)
{
    uint8_t pal;
    uint32_t pixel, x, y, *dstPtr;

    dstPtr = &frameBuffer[(yPos * SCREEN_W) + xPos];
    for (y = 0; y < h; ++y)
    {
        for (x = 0; x < w; ++x)
        {
            pal = srcPtr[x];

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

static void blit32(uint32_t xPos, uint32_t yPos, uint32_t w, uint32_t h, const uint32_t *srcPtr)
{
    uint32_t *dstPtr, y;

    dstPtr = &frameBuffer[(yPos * SCREEN_W) + xPos];
    for (y = 0; y < h; ++y)
    {
        memcpy(dstPtr, srcPtr, w * sizeof (int32_t));

        srcPtr += w;
        dstPtr += SCREEN_W;
    }
}

void charOut(uint32_t xPos, uint32_t yPos, uint32_t color, char chr)
{
    const uint8_t *srcPtr;
    uint32_t x, y, *dstPtr;

    srcPtr = &fontData[(chr * (8 * 5)) + 1];
    dstPtr = &frameBuffer[(yPos * SCREEN_W) + xPos];

    for (y = 0; y < 5; ++y)
    {
        for (x = 0; x < 6; ++x)
        {
            if (srcPtr[x])
                dstPtr[x] = color;
        }

        srcPtr += 8;
        dstPtr += SCREEN_W;
    }
}

void textOut(uint32_t xPos, uint32_t yPos, uint32_t color, const char *text)
{
    uint32_t x;

    x = xPos;
    while (*text != '\0')
    {
        charOut(x, yPos, color, *text++);
        x += 7;
    }
}

void textOutShadow(uint32_t xPos, uint32_t yPos, uint32_t fgColor, uint32_t bgColor, const char *text)
{
    uint32_t x;

    x = xPos;
    while (*text != '\0')
    {
        charOut(x+1, yPos+1, bgColor, *text);
        charOut(x+0, yPos+0, fgColor, *text);

        text++;
        x += 7;
    }
}

void hLine(uint32_t x, uint32_t y, uint32_t w, uint32_t color)
{
    uint32_t *dstPtr, i;

#ifdef _DEBUG
    if ((x >= SCREEN_W) || (y >= SCREEN_H) || ((x + w) > SCREEN_W))
        __debugbreak();
#endif

    dstPtr = &frameBuffer[(y * SCREEN_W) + x];
    for (i = 0; i < w; ++i)
        dstPtr[i] = color;
}

void vLine(uint32_t x, uint32_t y, uint32_t h, uint32_t color)
{
    uint32_t *dstPtr,i;

#ifdef _DEBUG
    if ((y >= SCREEN_W) || (x >= SCREEN_W) || ((y + h) > SCREEN_H))
        __debugbreak();
#endif

    dstPtr = &frameBuffer[(y * SCREEN_W) + x];
    for (i = 0; i < h; ++i)
    {
        *dstPtr  = color;
         dstPtr += SCREEN_W;
    }
}

static void drawSpectrumAnalyzer(uint32_t xPos, uint32_t yPos)
{
    uint8_t drawY, h, i;
    const uint32_t *srcPtr;
    uint32_t *dstPtr, x, y, drawX;

    randSeed = 8675309;

    drawX = xPos + 129;
    for (i = 0; i < 23; ++i)
    {
        h = (uint8_t)(myRandom(35 + 1));

        drawY   = 35 - h;
        srcPtr = &spectrumAnalyzerBMP[h];
        dstPtr = &frameBuffer[((yPos+drawY+59) * SCREEN_W) + drawX];

        for (y = 0; y <= h; ++y)
        {
            for (x = 0; x < 6; ++x)
                dstPtr[x] = *srcPtr;

            dstPtr += SCREEN_W;
            srcPtr--;
        }

        drawX += 8;
    }
}

static void drawVUMeters(uint32_t xPos, uint32_t yPos)
{
    uint8_t drawY, h, i;
    const uint32_t *srcPtr;
    uint32_t *dstPtr, x, y, drawX;

    randSeed = 81549300;

    drawX = xPos + 55;
    for (i = 0; i < 4; ++i)
    {
        h = (uint8_t)(myRandom(48 + 1));

        drawY  = 48 - h;
        srcPtr = &vuMeterBMP[((48 - 1) * 10) - (drawY * 10)];
        dstPtr = &frameBuffer[((yPos+drawY+140) * SCREEN_W) + drawX];

        for (y = 0; y < h; ++y)
        {
            for (x = 0; x < 10; ++x)
                dstPtr[x]= srcPtr[x];

            dstPtr += SCREEN_W;
            srcPtr -= 10;
        }

        drawX += 72;
    }
}

static void drawPatternCursor(uint32_t xPos, uint32_t yPos)
{
    int32_t x, y;
    const uint32_t *srcPtr;
    uint32_t pixel, *dstPtr;

    srcPtr = patternCursorBMP;
    dstPtr = &frameBuffer[((yPos+188) * SCREEN_W) + (xPos+30)];

    for (y = 0; y < 14; ++y)
    {
        for (x = 0; x < 11; ++x)
        {
            pixel = srcPtr[x];
            if (pixel != RGB24_COLORKEY)
                dstPtr[x] = pixel;
        }

        srcPtr += 11;
        dstPtr += SCREEN_W;
    }
}

void drawTracker(void)
{
    const uint32_t x = TRACKER_X;
    const uint32_t y = TRACKER_Y;

    if (topScreen == 0)
        blit(visualsData, x, y, 320, 99);
    else
        blit(diskOpData,  x, y, 320, 99);

    if ((topScreen == 0) && analyzerShown)
    {
        blit(spectrumData, x+120, y+44, 200, 55);
        drawSpectrumAnalyzer(x, y);
    }

    blit(songNameData, x, y+99, 320, 22);

    if (bottomScreen == 0)
        blit(patternEditorData, x, y+121, 320, 134);
    else
        blit(samplerData, x, y+121, 320, 134);

    if (bottomScreen == 0)
    {
        drawVUMeters(x, y);
        drawPatternCursor(x, y);
    }
}

void handleSlidersHeldDown(void)
{
    int8_t value;
    int32_t mx;

    if (sliderHeldDown == 0)
        return; // no slider held down

    if (sliderHeldDown <= 3)
    {
        // palette RGB sliders

        mx = input.mouse.x - (COLORPICKER1_X + 26);
        if (mx < 0)
            mx = 0;

        value = (int8_t)(mx / 3);
        if (value > 15)
            value = 15;

        oldColorVal = palette[currColor];

        palette[currColor] &= ~(15    << (4 * (3 - sliderHeldDown)));
        palette[currColor] |=  (value << (4 * (3 - sliderHeldDown)));

        if (palette[currColor] != oldColorVal)
        {
            configIsSaved = 0;

            if (currColor == 5)
                updatePatternCursorBMP();

            drawColorPicker1();
            drawTracker();
            redrawScreen = 1;
        }
    }
    else if (sliderHeldDown <= 6)
    {
        // visualizer RGB sliders

        mx = input.mouse.x - (COLORPICKER2_X + 26);
        if (mx < 0)
            mx = 0;

        value = (int8_t)(mx / 3);
        if (value > 15)
            value = 15;

        oldColorVal = theRightColors[rainbowPos];

        theRightColors[rainbowPos] &= ~(15    << (4 * (6 - sliderHeldDown)));
        theRightColors[rainbowPos] |=  (value << (4 * (6 - sliderHeldDown)));

        if (theRightColors[rainbowPos] != oldColorVal)
        {
            configIsSaved = 0;

            if (theRightColors == vuColors)
                updateVuMeterBMP();
            else
                updateSpectrumAnalyzerBMP();

            drawColorPicker2();
            drawTracker();
            redrawScreen = 1;
        }
    }
}

void handleRainbowHeldDown(void)
{
    uint8_t oldOffset;
    int32_t my;

    if (rainbowHeldDown == 0)
        return;

    my = input.mouse.y - (COLORPICKER2_Y+3);

         if (my <  0) my = 0;
    else if (my > 47) my = 47;

    if ((theRightColors == analyzerColors) && (my > 35))
        my = 35;

    oldOffset = rainbowPos;

    rainbowPos = (uint8_t)(my);
    if (rainbowPos != oldOffset)
    {
        setUndo2Color(rainbowPos);
        drawColorPicker2();
        redrawScreen = 1;
    }
}

static void rainbowUp(void)
{
    uint8_t i;
    uint16_t tmp;

    tmp = *theRightColors;
    for (i = 0; i < (colorsMax - 1); ++i)
        theRightColors[i] = theRightColors[i + 1];
    theRightColors[colorsMax - 1] = tmp;

    if (theRightColors == vuColors)
        updateVuMeterBMP();
    else
        updateSpectrumAnalyzerBMP();

    drawColorPicker2();
    drawTracker();
    redrawScreen = 1;
}

static void rainbowDown(void)
{
    uint16_t tmp;
    uint32_t i;

    tmp = theRightColors[colorsMax - 1];
    for (i = (colorsMax - 1); i >= 1; --i)
        theRightColors[i] = theRightColors[i - 1];
    theRightColors[0] = tmp;

    if (theRightColors == vuColors)
        updateVuMeterBMP();
    else
        updateSpectrumAnalyzerBMP();

    drawColorPicker2();
    drawTracker();
    redrawScreen = 1;
}

static void rainbowSpread(void)
{
    spreadFlag = 1;
    spreadFrom = rainbowPos;
}

#ifndef __APPLE__
static void colorPicker1(void)
{
    uint16_t color;

    color = colorPicker(palette[currColor]);
    if (color != 0xFFFF)
    {
        palette[currColor] = color & 0xFFF;

        if (currColor == 5)
            updatePatternCursorBMP();

        drawColorPicker1();
        drawTracker();
        redrawScreen = 1;
        configIsSaved = 0;
    }
}

static void colorPicker2(void)
{
    uint16_t color;

    color = colorPicker(theRightColors[rainbowPos]);
    if (color != 0xFFFF)
    {
        theRightColors[rainbowPos] = color &= 0xFFF;
        if (theRightColors == vuColors)
            updateVuMeterBMP();
        else
            updateSpectrumAnalyzerBMP();

        drawColorPicker2();
        drawTracker();
        redrawScreen = 1;
        configIsSaved = 0;
    }
}
#endif

int8_t handleColorPicker1(int32_t mx, int32_t my)
{
    uint8_t newColor;

    // color picker #1 buttons
    if ((mx >= COLORPICKER1_X) && (mx < (COLORPICKER1_X+108)) && (my >= (COLORPICKER1_Y+33)) && (my < (COLORPICKER1_Y+44)))
    {
        if (mx >= (COLORPICKER1_X+79)) // "DEF"
        {
            configIsSaved = 0;
            setDefaultPalette();
            updatePatternCursorBMP();
            drawTracker();
            drawColorPicker1();
            redrawScreen = 1;
            return (1);
        }
        else if (mx >= (COLORPICKER1_X+33)) // "CANCEL"
        {
            configIsSaved = 0;
            cancel1Color();
            updatePatternCursorBMP();
            drawTracker();
            drawColorPicker1();
            redrawScreen = 1;
            return (1);
        }
        else // "UNDO"
        {
            configIsSaved = 0;
            undo1Color();
            updatePatternCursorBMP();
            drawTracker();
            drawColorPicker1();
            redrawScreen = 1;
            return (1);
        }
    }

    // color picker #1 sliders
    if ((mx >= (COLORPICKER1_X+0)) && (mx < (COLORPICKER1_X+79)))
    {
        if ((my >= (COLORPICKER1_Y+0)) && (my < (COLORPICKER1_Y+11)))
        {
            sliderHeldDown = 1; // #1 R
            return (1);
        }
        else if ((my >= (COLORPICKER1_Y+11)) && (my < (COLORPICKER1_Y+22)))
        {
            sliderHeldDown = 2; // #1 G
            return (1);
        }
        else if ((my >= (COLORPICKER1_Y+22)) && (my < (COLORPICKER1_Y+33)))
        {
            sliderHeldDown = 3; // #1 B
            return (1);
        }
    }

    // color picker #1 colors
    if ((mx >= (COLORPICKER1_X+82)) && ((my >= (COLORPICKER1_Y+5))) && ((my < (COLORPICKER1_Y+28))))
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
                redrawScreen = 1;
            }

            return (1);
        }
        else if (mx >= (COLORPICKER1_X+95) && (mx < (COLORPICKER1_X+105)))
        {
            // right colors
            newColor = (uint8_t)(4 + ((my - (COLORPICKER1_Y+5)) / 6));
            if (currColor != newColor)
            {
                currColor = newColor;
                setUndo1Color(currColor);
                drawColorPicker1();
                redrawScreen = 1;
            }
        }
    }

#ifndef __APPLE__
    // "PICK"
    if ((mx >= COLORPICKER1_X) && (mx < (COLORPICKER1_X+33)) && (my >= (COLORPICKER1_Y+44)) && (my < (COLORPICKER1_Y+55)))
        colorPicker1();
#endif

    return (0);
}

void handleRainbowUpDownButtons(void)
{
    int32_t mx, my;

    if (buttonCounter++ >= BUTTON_DELAY)
    {
        if ((buttonCounter % BUTTON_REPEAT_DELAY) == 0)
        {
            mx = input.mouse.x;
            my = input.mouse.y;

            if (rainbowUpHeldDown)
            {
                if ((my >= (COLORPICKER2_Y+12)) && (my < (COLORPICKER2_Y+22))
                 && (mx >= (COLORPICKER2_X+0))  && (mx < (COLORPICKER2_X+16)))
                    rainbowUp();
            }
            else if (rainbowDownHeldDown)
            {
                if ((my >= (COLORPICKER2_Y+12)) && (my < (COLORPICKER2_Y+22))
                 && (mx >= (COLORPICKER2_X+16)) && (mx < (COLORPICKER2_X+32)))
                    rainbowDown();
            }
        }
    }
}

static int8_t handleColorPicker2(int32_t mx, int32_t my)
{
    // color picker rainbow
    if ((mx >= (COLORPICKER2_X+79)) && (mx < (COLORPICKER2_X+108)) && (my >= COLORPICKER2_Y) && (my < COLORPICKER2_Y+55))
    {
        rainbowHeldDown = 1;
        return (1);
    }

    spreadFlag = 0;

    // color picker #2 edit button
    if ((my >= COLORPICKER2_Y) && (my < (COLORPICKER2_Y+11)) && (mx >= COLORPICKER2_X) && (mx < (COLORPICKER2_X+79)))
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
        redrawScreen = 1;
        return (1);
    }

    // color picker #2 top buttons
    if ((mx >= COLORPICKER2_X) && (mx < (COLORPICKER2_X+79)) && (my >= (COLORPICKER2_Y+12)) && (my < (COLORPICKER2_Y+22)))
    {
        if (mx >= (COLORPICKER2_X+32))
        {
            // "SPREAD"

            rainbowSpread();
            return (1);
        }
        else if (mx >= (COLORPICKER2_X+16))
        {
            // "DOWN"

            configIsSaved = 0;
            rainbowDown();
            rainbowDownHeldDown = 1;
            buttonCounter = 0;
            return (1);
        }
        else
        {
            // "UP"

            configIsSaved = 0;
            rainbowUp();
            rainbowUpHeldDown = 1;
            buttonCounter = 0;
            return (1);
        }
    }

    // color picker #2 bottom buttons
    if ((mx >= COLORPICKER2_X) && (mx < (COLORPICKER2_X+108)) && (my >= (COLORPICKER2_Y+55)) && (my < (COLORPICKER2_Y+66)))
    {
        if (mx >= (COLORPICKER2_X+79))
        {
            // "DEF"

            configIsSaved = 0;

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
            redrawScreen = 1;
            return (1);
        }
        else if (mx >= (COLORPICKER2_X+33))
        {
            // "CANCEL"

            configIsSaved = 0;
            cancel2Color();

            if (theRightColors == vuColors)
                updateVuMeterBMP();
            else
                updateSpectrumAnalyzerBMP();

            drawTracker();
            drawColorPicker2();
            redrawScreen = 1;
            return (1);
        }
        else
        {
            // "UNDO"

            configIsSaved = 0;
            undo2Color();

            if (theRightColors == vuColors)
                updateVuMeterBMP();
            else
                updateSpectrumAnalyzerBMP();

            drawTracker();
            drawColorPicker2();
            redrawScreen = 1;
            return (1);
        }
    }

    // color picker #2 sliders
    if ((mx >= (COLORPICKER2_X+0)) && (mx < (COLORPICKER2_X+79)))
    {
        if ((my >= (COLORPICKER2_Y+13)) && (my < (COLORPICKER2_Y+33)))
        {
            sliderHeldDown = 4; // #2 R
            return (1);
        }
        else if ((my >= (COLORPICKER2_Y+33)) && (my < (COLORPICKER2_Y+44)))
        {
            sliderHeldDown = 5; // #2 G
            return (1);
        }
        else if ((my >= (COLORPICKER2_Y+44)) && (my < (COLORPICKER2_Y+55)))
        {
            sliderHeldDown = 6; // #2 B
            return (1);
        }
    }

#ifndef __APPLE__
    // "PICK"
    if ((mx >= COLORPICKER2_X) && (mx < (COLORPICKER2_X+33)) && (my >= (COLORPICKER2_Y+66)) && (my < (COLORPICKER2_Y+77)))
        colorPicker2();
#endif

    return (0);
}

static int8_t handleTrackerButtons(int32_t mx, int32_t my)
{
    if (topScreen == 0)
    {
        // visuals screen

        // "DISK OP."
        if ((mx >= (TRACKER_X+182)) && (my >= (TRACKER_Y+33)) && (mx < (TRACKER_X+244)) && (my < (TRACKER_Y+44)))
        {
            topScreen = 1;
            drawTracker();
            redrawScreen = 1;
            return (1);
        }

        // "SAMPLER"
        else if ((mx >= (TRACKER_X+244)) && (my >= (TRACKER_Y+33)) && (mx < (TRACKER_X+306)) && (my < (TRACKER_Y+44)))
        {
            bottomScreen ^= 1;
            drawTracker();
            redrawScreen = 1;
            return (1);
        }

        // quadrascope (toggle spectrum analyzer)
        else if ((mx >= (TRACKER_X+120)) && (mx < (TRACKER_X+320)) && (my >= (TRACKER_Y+44)) && (my < (TRACKER_Y+99)))
        {
            analyzerShown ^= 1;
            drawTracker();
            redrawScreen = 1;
            return (1);
        }
    }
    else
    {
        // disk op. screen

        // "EXIT"
        if ((mx >= (TRACKER_X+308)) && (mx < (TRACKER_X+320)) && (my >= (TRACKER_Y+40)) && (my < (TRACKER_Y+81)))
        {
            topScreen = 0;
            drawTracker();
            redrawScreen = 1;
            return (1);
        }
    }

    if (bottomScreen == 1)
    {
        // sampler screen

        // "EXIT"
        if ((mx >= (TRACKER_X+7)) && (my >= (TRACKER_Y+124)) && (mx < (TRACKER_X+26)) && (my < (TRACKER_Y+135)))
        {
            bottomScreen ^= 1;
            drawTracker();
            redrawScreen = 1;
            return (1);
        }
    }

    return (0);
}

static int8_t handleLoadSaveButtons(int32_t mx, int32_t my)
{
    if ((mx >= LOADINI_X) && (mx < (LOADINI_X+108)) && (my >= LOADINI_Y) && (my < (LOADINI_Y+11)))
    {
        // "LOAD COLORS.INI"
        loadColorsDotIni();
        return (1);
    }

    if ((mx >= LOADPT_X) && (mx < (LOADPT_X+108)) && (my >= LOADPT_Y) && (my < (LOADPT_Y+11)))
    {
        // "LOAD PT.CONFIG"
        loadPTDotConfig();
        return (1);
    }

    if ((mx >= SAVE_X) && (mx < (SAVE_X+108)) && (my >= SAVE_Y) && (my < (SAVE_Y+11)))
    {
        // "SAVE COLORS.INI"
        savePalette(1);
        return (1);
    }

    return (0);
}

static uint16_t colCrossFade(uint16_t col1, uint16_t col2, uint16_t idx, uint16_t len)
{
    uint16_t color, ch, a, b, nybble;

    color = 0;
    for (ch = 0; ch < 3; ++ch)
    {
        a = (col1 >> (ch * 4)) & 15;
        b = (col2 >> (ch * 4)) & 15;

        nybble = (((a * ((len * 2) - (idx * 2))) + len) + (b * (idx * 2))) / (len * 2);
        if (nybble > 15) nybble = 15;

        color |= (nybble << (ch * 4));
    }

    return (color);
}

static void chkSpread(void)
{
    uint8_t tmp, x1, x2;
    uint16_t i, length;

    spreadFlag = 0;

    x1 = spreadFrom;
    x2 = rainbowPos;

    // exchange x1/x2 if x1>x2
    if (x1 > x2)
    {
        tmp = x1;
        x1  = x2;
        x2  = tmp;
    }

    length = x2 - x1;
    if (length <= 1)
        return;

    for (i = 0; i <= length; ++i)
        theRightColors[x1 + i] = colCrossFade(theRightColors[x1], theRightColors[x2], i, length);

    if (theRightColors == vuColors)
        updateVuMeterBMP();
    else
        updateSpectrumAnalyzerBMP();

    drawTracker();
    drawColorPicker2();
    redrawScreen = 1;
    configIsSaved = 0;
}

void mouseButtonUpHandler(void)
{
    if (spreadFlag && rainbowHeldDown)
        chkSpread();

    buttonCounter = 0;
    sliderHeldDown = 0;
    rainbowHeldDown = 0;
    rainbowUpHeldDown = 0;
    rainbowDownHeldDown = 0;
}

void mouseButtonDownHandler(void)
{
    uint32_t mx, my;

    mx = input.mouse.x;
    my = input.mouse.y;

    if (handleColorPicker1(mx, my))    return;
    if (handleColorPicker2(mx, my))    return;
    if (handleTrackerButtons(mx, my))  return;
    if (handleLoadSaveButtons(mx, my)) return;
}

void keyDownHandler(SDL_Keycode keyEntry)
{
    // exit sampler/disk op.
    if (keyEntry == SDLK_ESCAPE)
    {
        topScreen    = 0;
        bottomScreen = 0;
        drawTracker();
        redrawScreen = 1;
    }
}

void fillRect(uint32_t xPos, uint32_t yPos, uint32_t w, uint32_t h, uint32_t color)
{
    uint32_t *dstPtr, x, y;

    dstPtr = &frameBuffer[(yPos * SCREEN_W) + xPos];
    for (y = 0; y < h; ++y)
    {
        for (x = 0; x < w; ++x)
            dstPtr[x] = color;

        dstPtr += SCREEN_W;
    }
}

void drawBox1(uint32_t x, uint32_t y, uint32_t w, uint32_t h)
{
    fillRect(x + 1, y + 1, w - 1, h - 1, 0x888888);

    hLine(x+0,     y+0, w-1, 0xBBBBBB);
    vLine(x+0,     y+1, h-2, 0xBBBBBB);
    hLine(x+1,   y+h-1, w-1, 0x555555);
    vLine(x+w-1,   y+1, h-2, 0x555555);

    frameBuffer[((y + (h - 1)) * SCREEN_W) + x] = 0x888888;
    frameBuffer[(y * SCREEN_W) + (x + (w - 1))] = 0x888888;
}

void drawBox2(uint32_t x, uint32_t y, uint32_t w, uint32_t h)
{
    fillRect(x + 1, y + 1, w - 1, h - 1, 0);

    hLine(x+0,     y+0, w-1, 0x555555);
    vLine(x+0,     y+1, h-2, 0x555555);
    hLine(x+1,   y+h-1, w-1, 0xBBBBBB);
    vLine(x+w-1,   y+1, h-2, 0xBBBBBB);

    frameBuffer[((y + (h - 1)) * SCREEN_W) + x] = 0x888888;
    frameBuffer[(y * SCREEN_W) + (x + (w - 1))] = 0x888888;
}

void drawSliders1(uint32_t x, uint32_t y)
{
    uint32_t xPos;

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

void drawSliders2(uint32_t x, uint32_t y)
{
    uint32_t xPos;

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
    uint32_t i, j, xPos, yPos, color;
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
    for (i = 0; i < 3; ++i)
    {
        xPos = x + 27;
        yPos = y + 2 + (i * 11);

        for (j = 0; j < 16; ++j)
        {
            if (((j % 4) == 0) || (j == 15))
                frameBuffer[((yPos - 1) * SCREEN_W) + xPos] = 0xBBBBBB;

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
    for (i = 0; i < 2; ++i)
    {
        xPos = (i == 0) ? (x + 83) : (x + 96);
        for (j = 0; j < 4; ++j)
        {
            yPos = y + 5 + (j * 6);

            color = (i * 4) + j;
            if (color == currColor) // palette is selected
                fillRect(xPos - 1, yPos - 1, 10, 7, 0xFFDD00);

            fillRect(xPos, yPos, 8, 5, RGB12_to_RGB24(palette[color]));
        }
    }

    drawSliders1(x, y);
}

void drawColorPicker2(void)
{
    char strBuf[24];
    uint32_t pixel24, i, j, xPos, yPos, *dstPtr;
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
    for (i = 0; i < 3; ++i)
    {
        xPos = x + 27;
        yPos = y + 24 + (i * 11);

        for (j = 0; j < 16; ++j)
        {
            if (((j % 4) == 0) || (j == 15))
                frameBuffer[((yPos - 1) * SCREEN_W) + xPos] = 0xBBBBBB;

            frameBuffer[(yPos * SCREEN_W) + xPos] = 0xBBBBBB;

            xPos += 3;
        }
    }

    // bottom buttons
    drawBox1(x,    y+55, 33, 11); textOutShadow(x+3,  y+58, 0xBBBBBB, 0x555555, "UNDO");
    drawBox1(x+33, y+55, 46, 11); textOutShadow(x+35, y+58, 0xBBBBBB, 0x555555, "CANCEL");
    drawBox1(x+79, y+55, 29, 11); textOutShadow(x+84, y+58, 0xBBBBBB, 0x555555, "DEF");

    // draw colors
    for (i = 0; i < 48; ++i)
    {
        if ((i > 35) && (theRightColors == analyzerColors))
            pixel24 = 0; // spectrum analyzer graphics only has 36 lines
        else
            pixel24 = RGB12_to_RGB24(theRightColors[i]);

        // draw current line
        dstPtr = &frameBuffer[((y+3+i) * SCREEN_W) + (x+88)];
        for (j = 0; j < 11; ++j)
            dstPtr[j] = pixel24;
    }

    // draw color selection lines
    hLine(x+ 83, y+3+rainbowPos, 4, 0xFFDD00);
    hLine(x+100, y+3+rainbowPos, 4, 0xFFDD00);

#ifndef __APPLE__
    // "PICK" button
    drawBox1(x, y + 66, 33, 11); textOutShadow(x + 3, y + 69, 0xBBBBBB, 0x555555, "PICK");

    // color value
    pixel24 = RGB12_to_RGB24(theRightColors[rainbowPos]);
    drawBox1(x + 33, y + 66, 75, 11);
    sprintf(strBuf, "%06X", pixel24);
    textOut(x + 50, y + 69, 0, strBuf);
#else
    // color value
    pixel24 = RGB12_to_RGB24(theRightColors[rainbowPos]);
    drawBox1(x, y + 66, 108, 11); textOutShadow(x + 3, y + 69, 0xBBBBBB, 0x555555, "RGB24:");
    sprintf(strBuf, "%06X", pixel24);
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

    redrawScreen = 1;
}
