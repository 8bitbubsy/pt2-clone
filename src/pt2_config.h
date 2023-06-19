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
	bool waveformCenterLine, pattDots, compoMode, autoCloseDiskOp, hideDiskOpDates, hwMouse;
	bool transDel, fullScreenStretch, vsyncOff, modDot, blankZeroFlag, realVuMeters, rememberPlayMode;
	bool startInFullscreen, integerScaling, disableE8xEffect, noDownsampleOnSmpLoad, keepEditModeAfterStepPlay;
	int8_t stereoSeparation, accidental;
	bool autoFitVideoScale;
	int8_t videoScaleFactor;
	uint8_t pixelFilter, amigaModel;
	uint16_t quantizeValue;
	int32_t maxSampleLength;
	uint32_t soundFrequency, soundBufferSize, audioInputFrequency, mod2WavOutputFreq;
} config_t;

extern config_t config; // pt2_config.c

void loadConfig(void);
