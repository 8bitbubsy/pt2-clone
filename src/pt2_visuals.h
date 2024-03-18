#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "pt2_header.h"
#include "pt2_hpc.h"

#define MAX_UPSCALE_FACTOR 16 // 5120x4080 - ought to be good enough for many years to come

enum
{
	SPRITE_PATTERN_CURSOR = 0,
	SPRITE_LOOP_PIN_LEFT = 1,
	SPRITE_LOOP_PIN_RIGHT = 2,
	SPRITE_SAMPLING_POS_LINE = 3,
	SPRITE_MOUSE_POINTER = 4, // above all other sprites

	SPRITE_NUM,

	SPRITE_TYPE_PALETTE = 0,
	SPRITE_TYPE_RGB = 1
};

void resetFPSCounter(void);
void beginFPSCounter(void);
void endFPSCounter(void);

void blit32(int32_t x, int32_t y, int32_t w, int32_t h, const uint32_t *src);
void putPixel(int32_t x, int32_t y, const uint32_t pixelColor);
void hLine(int32_t x, int32_t y, int32_t w, const uint32_t pixelColor);
void vLine(int32_t x, int32_t y, int32_t h, const uint32_t pixelColor);
void drawFramework1(int32_t x, int32_t y, int32_t w, int32_t h);
void drawFramework2(int32_t x, int32_t y, int32_t w, int32_t h);
void drawFramework3(int32_t x, int32_t y, int32_t w, int32_t h);
void fillRect(int32_t x, int32_t y, int32_t w, int32_t h, const uint32_t pixelColor);
void drawButton1(int32_t x, int32_t y, int32_t w, int32_t h, const char *text);
void drawButton2(int32_t x, int32_t y, int32_t w, int32_t h, const char *text);
void drawUpButton(int32_t x, int32_t y);
void drawDownButton(int32_t x, int32_t y);

void statusAllRight(void);
void statusOutOfMemory(void);
void statusSampleIsEmpty(void);
void statusNotSampleZero(void);
void changeStatusText(const char *text);
void resetAllScreens(void);

bool setupVideo(void);
void renderFrame2(void);
void renderFrame(void);
void flipFrame(void);
void updateSpectrumAnalyzer(uint8_t vol, uint16_t period);
void sinkVisualizerBars(void);
void updatePosEd(void);
void updateVisualizer(void);
void updateEditOp(void);
void toggleFullscreen(void);
void videoClose(void);
void displayMainScreen(void);
void renderMuteButtons(void);
void renderAboutScreen(void);
void renderQuadrascopeBg(void);
void renderSpectrumAnalyzerBg(void);
void renderEditOpMode(void);
void renderEditOpScreen(void);
void renderSamplerVolBox(void);
void renderSamplerFiltersBox(void);
void removeSamplerVolBox(void);
void removeSamplerFiltersBox(void);
void fillToVuMetersBgBuffer(void);
void showVolFromSlider(void);
void showVolToSlider(void);
void updateCurrSample(void);
void eraseSprites(void);
void renderSprites(void);
void handleLastGUIObjectDown(void);
void invertRange(void);
void updateCursorPos(void);
void renderVuMeters(void);
void setupSprites(void);
void freeSprites(void);
void setSpritePos(int32_t sprite, int32_t x, int32_t y);
void hideSprite(int32_t sprite);
