#pragma once

#include <stdint.h>
#include <stdbool.h>

enum
{
	PIXELFILTER_NEAREST = 0,
	PIXELFILTER_LINEAR = 1,
	PIXELFILTER_BEST = 2
};

typedef struct config_t
{
	char *defModulesDir, *defSamplesDir;
	bool dottedCenterFlag, pattDots, a500LowPassFilter, compoMode, autoCloseDiskOp, hideDiskOpDates, hwMouse;
	bool transDel, fullScreenStretch, vsyncOff, modDot, blankZeroFlag, realVuMeters, rememberPlayMode;
	bool sampleLowpass, startInFullscreen;
	int8_t stereoSeparation, videoScaleFactor, accidental;
	uint8_t pixelFilter;
	uint16_t quantizeValue;
	uint32_t soundFrequency, soundBufferSize;
} config_t;

extern config_t config; // pt2_config.c

void loadConfig(void);
