#pragma once

#include <stdint.h>
#include "pt2_palette.h"
#include "pt2_mouse.h"

// TABLES
extern const uint32_t cursorColors[6][3];
extern const char noteNames1[36][4];
extern const char noteNames2[36][4];
extern const uint8_t vibratoTable[32];
extern const int16_t periodTable[606];
extern int8_t pNoteTable[32];

// GFX
extern uint32_t iconBMP[1024];
extern const uint8_t mousePointerBMP[256];
extern const uint8_t fontBMP[5120];
extern const uint8_t arrowPaletteBMP[30];

// PACKED GFX
extern const uint8_t aboutScreenPackedBMP[1684];
extern const uint8_t clearDialogPackedBMP[525];
extern const uint8_t diskOpScreenPackedBMP[1898];
extern const uint8_t editOpModeCharsPackedBMP[88];
extern const uint8_t editOpScreen1PackedBMP[1481];
extern const uint8_t editOpScreen2PackedBMP[1502];
extern const uint8_t editOpScreen3PackedBMP[1736];
extern const uint8_t editOpScreen4PackedBMP[1713];
extern const uint8_t mod2wavPackedBMP[607];
extern const uint8_t muteButtonsPackedBMP[46];
extern const uint8_t posEdPackedBMP[1375];
extern const uint8_t samplerVolumePackedBMP[706];
extern const uint8_t samplerFiltersPackedBMP[933];
extern const uint8_t samplerScreenPackedBMP[3056];
extern const uint8_t spectrumVisualsPackedBMP[2217];
extern const uint8_t trackerFramePackedBMP[8486];
extern const uint8_t yesNoDialogPackedBMP[476];
extern const uint8_t bigYesNoDialogPackedBMP[472];
extern const uint8_t pat2SmpDialogPackedBMP[520];

// changable by config file
extern uint16_t analyzerColors[36];
extern uint16_t vuMeterColors[48];

// these are filled/normalized on init, so no const
extern uint32_t vuMeterBMP[480];
extern uint32_t loopPinsBMP[512];
extern uint32_t samplingPosBMP[64];
extern uint32_t spectrumAnaBMP[36];
extern uint32_t patternCursorBMP[154];
extern uint32_t *editOpScreen1BMP;
extern uint32_t *editOpScreen2BMP;
extern uint32_t *editOpScreen3BMP;
extern uint32_t *editOpScreen4BMP;
extern uint32_t *yesNoDialogBMP;
extern uint32_t *bigYesNoDialogBMP;
extern uint32_t *spectrumVisualsBMP;
extern uint32_t *posEdBMP;
extern uint32_t *mod2wavBMP;
extern uint32_t *diskOpScreenBMP;
extern uint32_t *clearDialogBMP;
extern uint32_t *samplerVolumeBMP;
extern uint32_t *samplerFiltersBMP;
extern uint32_t *samplerScreenBMP;
extern uint32_t *trackerFrameBMP;
extern uint32_t *aboutScreenBMP;
extern uint32_t *muteButtonsBMP;
extern uint32_t *arrowBMP;
extern uint32_t *editOpModeCharsBMP;
extern uint32_t *pat2SmpDialogBMP;

// button tables taken from the ptplay project + modified

// MODIFY THESE EVERY TIME YOU REMOVE/ADD A BUTTON!
#define ASK_BUTTONS 2
#define PAT2SMP_ASK_BUTTONS 3
#define CLEAR_BUTTONS 4
#define TOPSCREEN_BUTTONS 47
#define MIDSCREEN_BUTTONS 3
#define BOTSCREEN_BUTTONS 4
#define DISKOP_BUTTONS 17
#define POSED_BUTTONS 12
#define EDITOP1_BUTTONS 13
#define EDITOP2_BUTTONS 22
#define EDITOP3_BUTTONS 29
#define EDITOP4_BUTTONS 29
#define SAMPLER_BUTTONS 24
// -----------------------------------------------

extern const guiButton_t bAsk[];
extern const guiButton_t bPat2SmpAsk[];
extern const guiButton_t bClear[];
extern const guiButton_t bTopScreen[];
extern const guiButton_t bMidScreen[];
extern const guiButton_t bBotScreen[];
extern const guiButton_t bDiskOp[];
extern const guiButton_t bPosEd[];
extern const guiButton_t bEditOp1[];
extern const guiButton_t bEditOp2[];
extern const guiButton_t bEditOp3[];
extern const guiButton_t bEditOp4[];
extern const guiButton_t bSampler[];

#define EDOP_MODE_BMP_A_OFS ((7 * 6) * 0)
#define EDOP_MODE_BMP_C_OFS ((7 * 6) * 1)
#define EDOP_MODE_BMP_H_OFS ((7 * 6) * 2)
#define EDOP_MODE_BMP_N_OFS ((7 * 6) * 3)
#define EDOP_MODE_BMP_O_OFS ((7 * 6) * 4)
#define EDOP_MODE_BMP_P_OFS ((7 * 6) * 5)
#define EDOP_MODE_BMP_S_OFS ((7 * 6) * 6)
#define EDOP_MODE_BMP_T_OFS ((7 * 6) * 7)
