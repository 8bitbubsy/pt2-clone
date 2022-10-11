#pragma once

#include <stdbool.h>

#define MOD2WAV_CANCEL_BTN_X1 193
#define MOD2WAV_CANCEL_BTN_X2 247
#define MOD2WAV_CANCEL_BTN_Y1 81
#define MOD2WAV_CANCEL_BTN_Y2 92

void mod2WavDrawFadeoutToggle(void);
void mod2WavDrawFadeoutSeconds(void);
void mod2WavDrawLoopCount(void);
void toggleMod2WavFadeout(void);
void mod2WavFadeoutUp(void);
void mod2WavFadeoutDown(void);
void mod2WavLoopCountUp(void);
void mod2WavLoopCountDown(void);

void updateMod2WavDialog(void);
bool mod2WavRender(char *filename);
