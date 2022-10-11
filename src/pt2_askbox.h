#pragma once

#include <stdint.h>
#include <stdbool.h>

enum
{
	// dialog types
	ASKBOX_YES_NO = 0,
	ASKBOX_CLEAR = 1,
	ASKBOX_PAT2SMP = 2,
	ASKBOX_DOWNSAMPLE = 3,
	ASKBOX_MOD2WAV = 4,
	ASKBOX_NUM,

	// buttons

	ASKBOX_YES = 1,
	ASKBOX_NO = 0,

	ASKBOX_CLEAR_SONG = 0,
	ASKBOX_CLEAR_SAMPLES = 1,
	ASKBOX_CLEAR_ALL = 2,
	ASKBOX_CLEAR_CANCEL = 3
};

void breakAskBoxLoop(uint32_t _returnValue);
void handleThreadedAskBox(void);
uint32_t askBoxThreadSafe(uint32_t dialogType, const char *statusText);
uint32_t askBox(uint32_t dialogType, const char *statusText);
void removeAskBox(void);
