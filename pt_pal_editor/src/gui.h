#pragma once

#define SCREEN_W 437
#define SCREEN_H 273

#define TRACKER_X 3
#define TRACKER_Y 15

#define COLORPICKER1_X 326
#define COLORPICKER1_Y 15

#define COLORPICKER2_X 326
#define COLORPICKER2_Y 103

#define LOADINI_X 326
#define LOADINI_Y 194

#define LOADPT_X 326
#define LOADPT_Y 208

#define SAVE_X 326
#define SAVE_Y 222

#include <stdint.h>
#include <stdbool.h>
#include <SDL2/SDL.h>

typedef struct keyb_t
{
	bool leftShiftKeyDown, leftCtrlKeyDown, leftAltKeyDown;
} keyb_t;

typedef struct mouse_t
{
	bool leftButtonPressed, rightButtonPressed;
	int32_t x, y;
	float fMouseXMul, fMouseYMul;
} mouse_t;

void charOut(int32_t xPos, int32_t yPos, uint32_t color, char chr);
void textOut(int32_t xPos, int32_t yPos, uint32_t color, const char *text);
void textOutShadow(int32_t xPos, int32_t yPos, uint32_t fgColor, uint32_t bgColor, const char *text);
void hLine(int32_t x, int32_t y, int32_t w, uint32_t color);
void vLine(int32_t x, int32_t y, int32_t h, uint32_t color);

void showErrorMsgBox(const char *fmt, ...); // main.c

void mouseButtonUpHandler(void);
void mouseButtonDownHandler(void);
void keyDownHandler(SDL_Keycode keyEntry);
void drawTracker(void);
void setupGUI(void);
void drawColorPicker2(void);
void drawColorPicker1(void);
void handleSlidersHeldDown(void);
void handleRainbowHeldDown(void);
void handleRainbowUpDownButtons(void);

// gui.c
extern bool topScreenShown, bottomScreenShown, analyzerShown;
extern uint8_t currColor, rainbowPos, colorsMax;
extern uint16_t *theRightColors;

// main.c
extern volatile bool programRunning, redrawScreen;
extern uint32_t *frameBuffer;
extern keyb_t keyb;
extern mouse_t mouse;
