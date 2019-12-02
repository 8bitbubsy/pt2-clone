#pragma once

#include <stdint.h>

enum
{
	PAL_BACKGRD = 0,
	PAL_BORDER = 1,
	PAL_GENBKG = 2,
	PAL_GENBKG2 = 3,
	PAL_QADSCP = 4,
	PAL_PATCURSOR = 5,
	PAL_GENTXT = 6,
	PAL_PATTXT = 7
};

#define RGB24_COLORKEY 0xFF000000

#define R12(x) ((x >> 8) & 15)
#define G12(x) ((x >> 4) & 15)
#define B12(x) ((x >> 0) & 15)

#define R24(x) ((x >> 16) & 255)
#define G24(x) ((x >>  8) & 255)
#define B24(x) ((x >>  0) & 255)

#define R12_to_R24(x) (R12(x) * 17)
#define G12_to_G24(x) (G12(x) * 17)
#define B12_to_B24(x) (B12(x) * 17)

#define R24_to_R12(x) (R24(x) >> 4)
#define G24_to_G12(x) (G24(x) >> 4)
#define B24_to_B12(x) (B24(x) >> 4)

#define RGB12_to_RGB24(x) ((R12_to_R24(x) << 16) | (G12_to_G24(x) << 8) | (B12_to_B24(x)))
#define RGB24_to_RGB12(x) ((R24_to_R12(x) <<  8) | (G24_to_G12(x) << 4) | (B24_to_B12(x)))

#define RGB12(r, g, b) (((r) <<  8) | ((g) << 4) | (b))
#define RGB24(r, g, b) (((r) << 16) | ((g) << 8) | (b))

extern uint16_t palette[8];
extern uint16_t vuColors[48];
extern uint16_t analyzerColors[36];
extern uint32_t spectrumAnalyzerBMP[36];
extern uint32_t vuMeterBMP[480];
extern uint32_t patternCursorBMP[154];

void fillCancel1Colors(void);
void cancel1Color(void);
void setUndo1Color(uint8_t paletteIndex);
void undo1Color(void);

void fillCancel2Colors(void);
void cancel2Color(void);
void setUndo2Color(uint8_t colorIndex);
void undo2Color(void);

void setDefaultPalette(void);
void setDefaultVuColors(void);
void setDefaultAnalyzerColors(void);
void updatePatternCursorBMP(void);
void updateSpectrumAnalyzerBMP(void);
void updateVuMeterBMP(void);

uint16_t colorPicker(uint16_t inputColor);

void updateBMPs(void);
