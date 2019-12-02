#pragma once

#include <stdint.h>
#include <stdbool.h>

enum
{
	// -----------------------------
	PAL_BACKGRD = 0,
	PAL_BORDER = 1,
	PAL_GENBKG = 2,
	PAL_GENBKG2 = 3,
	PAL_QADSCP = 4,
	PAL_PATCURSOR = 5,
	PAL_GENTXT = 6,
	PAL_PATTXT = 7,
	// -----------------------------
	PAL_SAMPLLINE = 8,
	PAL_LOOPPIN = 9,
	PAL_TEXTMARK = 10,
	PAL_MOUSE_1 = 11,
	PAL_MOUSE_2 = 12,
	PAL_MOUSE_3 = 13,
	// -----------------------------
	PAL_COLORKEY = 14,
	// -----------------------------
	PALETTE_NUM,
	// -----------------------------

	POINTER_MODE_IDLE = 0,
	POINTER_MODE_EDIT = 1,
	POINTER_MODE_PLAY = 2,
	POINTER_MODE_MSG1 = 3,
	POINTER_MODE_LOAD = 4,
	POINTER_MODE_RECORD = 5,
	POINTER_MODE_READ_DIR = 6
};

void setMsgPointer(void);
void pointerErrorMode(void);
void pointerSetMode(int8_t pointerMode, bool carry);
void pointerSetPreviousMode(void);
