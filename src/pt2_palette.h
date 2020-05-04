#pragma once

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
	PAL_SAMPLLINE = 8,
	PAL_LOOPPIN = 9,
	PAL_TEXTMARK = 10,
	PAL_MOUSE_1 = 11,
	PAL_MOUSE_2 = 12,
	PAL_MOUSE_3 = 13,
	// -----------------------------
	PAL_COLORKEY = 14,
	// -----------------------------
	PALETTE_NUM
};

void setDefaultPalette(void);
