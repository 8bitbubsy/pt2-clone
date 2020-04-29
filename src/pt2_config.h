#pragma once

#include <stdint.h>
#include <stdbool.h>

typedef struct config_t
{
	char *defModulesDir, *defSamplesDir;
	bool dottedCenterFlag, pattDots, a500LowPassFilter, compoMode, autoCloseDiskOp, hideDiskOpDates, hwMouse;
	bool transDel, fullScreenStretch, vsyncOff, modDot, blankZeroFlag, realVuMeters, rememberPlayMode;
	bool sampleLowpass;
	int8_t stereoSeparation, videoScaleFactor, accidental;
	uint16_t quantizeValue;
	uint32_t soundFrequency, soundBufferSize;
} config_t;

extern config_t config; // pt2_config.c

void loadConfig(void);
