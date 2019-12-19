#include <stdint.h>
#include "pt2_palette.h"
#include "pt2_header.h"
#include "pt2_helpers.h"
#include "pt2_tables.h"

uint32_t palette[PALETTE_NUM] =
{
	// -----------------------------
	0x000000, // 00- PAL_BACKGRD
	0xBBBBBB, // 01- PAL_BORDER
	0x888888, // 02- PAL_GENBKG
	0x555555, // 03- PAL_GENBKG2
	0xFFDD00, // 04- PAL_QADSCP
	0xDD0044, // 05- PAL_PATCURSOR
	0x000000, // 06- PAL_GENTXT
	0x3344FF, // 07- PAL_PATTXT
	// -----------------------------
	0x00FFFF, // 08- PAL_SAMPLLINE
	0x0000FF, // 09- PAL_LOOPPIN
	0x770077, // 10- PAL_TEXTMARK
	0x444444, // 11- PAL_MOUSE_1
	0x777777, // 12- PAL_MOUSE_2
	0xAAAAAA, // 13- PAL_MOUSE_3
	// -----------------------------
	0xC0FFEE  // 14- PAL_COLORKEY
	// -----------------------------
};
