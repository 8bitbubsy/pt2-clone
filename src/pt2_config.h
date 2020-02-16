#pragma once

#include <stdint.h>
#include <stdbool.h>

struct ptConfig_t
{
	char *defModulesDir, *defSamplesDir;
	bool dottedCenterFlag, pattDots, a500LowPassFilter, compoMode, autoCloseDiskOp, hideDiskOpDates, hwMouse;
	bool transDel, fullScreenStretch, vsyncOff, modDot, blankZeroFlag, realVuMeters, rememberPlayMode;
	bool sampleLowpass;
	int8_t stereoSeparation, videoScaleFactor, accidental;
	uint16_t quantizeValue;
	uint32_t soundFrequency, soundBufferSize;
} ptConfig;

void loadConfig(void);
