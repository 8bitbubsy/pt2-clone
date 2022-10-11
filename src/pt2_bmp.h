#pragma once

#include <stdint.h>
#include <stdbool.h>

#define EDOP_MODE_BMP_A_OFS ((7 * 6) * 0)
#define EDOP_MODE_BMP_C_OFS ((7 * 6) * 1)
#define EDOP_MODE_BMP_H_OFS ((7 * 6) * 2)
#define EDOP_MODE_BMP_N_OFS ((7 * 6) * 3)
#define EDOP_MODE_BMP_O_OFS ((7 * 6) * 4)
#define EDOP_MODE_BMP_P_OFS ((7 * 6) * 5)
#define EDOP_MODE_BMP_S_OFS ((7 * 6) * 6)
#define EDOP_MODE_BMP_T_OFS ((7 * 6) * 7)

// GFX
extern uint32_t iconBMP[1024];
extern const uint8_t mousePointerBMP[256];
extern const uint8_t fontBMP[6096];

// PACKED GFX
extern const uint8_t aboutScreenPackedBMP[1408];
extern const uint8_t clearDialogPackedBMP[525];
extern const uint8_t diskOpScreenPackedBMP[1898];
extern const uint8_t fix128KChordPackedBMP[110];
extern const uint8_t fix128KPosPackedBMP[81];
extern const uint8_t editOpModeCharsPackedBMP[88];
extern const uint8_t editOpScreen1PackedBMP[1481];
extern const uint8_t editOpScreen2PackedBMP[1502];
extern const uint8_t editOpScreen3PackedBMP[1736];
extern const uint8_t editOpScreen4PackedBMP[1713];
extern const uint8_t muteButtonsPackedBMP[46];
extern const uint8_t posEdPackedBMP[1375];
extern const uint8_t sampleMonitorPackedBMP[441];
extern const uint8_t samplerVolumePackedBMP[706];
extern const uint8_t samplerFiltersPackedBMP[933];
extern const uint8_t samplerScreenPackedBMP[3076];
extern const uint8_t spectrumVisualsPackedBMP[2217];
extern const uint8_t tracker128KFixPackedBMP[363];
extern const uint8_t trackerFramePackedBMP[8486];
extern const uint8_t samplingBoxPackedBMP[1379];

// these are filled/normalized on init, so no const
extern uint32_t vuMeterBMP[480];
extern uint32_t loopPinsBMP[512];
extern uint32_t samplingPosBMP[64];
extern uint32_t analyzerColorsRGB24[36];
extern uint32_t patternCursorBMP[154];
extern uint32_t *editOpScreen1BMP;
extern uint32_t *editOpScreen2BMP;
extern uint32_t *editOpScreen3BMP;
extern uint32_t *editOpScreen4BMP;
extern uint32_t *spectrumVisualsBMP;
extern uint32_t *posEdBMP;
extern uint32_t *diskOpScreenBMP;
extern uint32_t *samplerVolumeBMP;
extern uint32_t *samplerFiltersBMP;
extern uint32_t *samplerScreenBMP;
extern uint32_t *trackerFrameBMP;
extern uint32_t *aboutScreenBMP;
extern uint32_t *muteButtonsBMP;
extern uint32_t *editOpModeCharsBMP;
extern uint32_t *sampleMonitorBMP;
extern uint32_t *samplingBoxBMP;

// fix-bitmaps for 128K sample mode
extern uint32_t *fix128KTrackerBMP;
extern uint32_t *fix128KPosBMP;
extern uint32_t *fix128KChordBMP;

bool unpackBMPs(void);
void createBitmaps(void);
void freeBMPs(void);
