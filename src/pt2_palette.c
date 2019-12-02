#include <stdint.h>
#include "pt2_palette.h"
#include "pt2_header.h"
#include "pt2_helpers.h"

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

void pointerErrorMode(void)
{
	palette[PAL_MOUSE_1] = 0x770000;
	palette[PAL_MOUSE_2] = 0x990000;
	palette[PAL_MOUSE_3] = 0xCC0000;

	editor.ui.refreshMousePointer = true;
}

void setMsgPointer(void)
{
	editor.ui.pointerMode = POINTER_MODE_READ_DIR;

	palette[PAL_MOUSE_1] = 0x004400;
	palette[PAL_MOUSE_2] = 0x007700;
	palette[PAL_MOUSE_3] = 0x00AA00;

	editor.ui.refreshMousePointer = true;
}

void pointerSetMode(int8_t pointerMode, bool carry)
{
	editor.ui.refreshMousePointer = true;

	switch (pointerMode)
	{
		case POINTER_MODE_IDLE:
		{
			editor.ui.pointerMode = pointerMode;
			if (carry)
				editor.ui.previousPointerMode = editor.ui.pointerMode;

			palette[PAL_MOUSE_1] = 0x444444;
			palette[PAL_MOUSE_2] = 0x777777;
			palette[PAL_MOUSE_3] = 0xAAAAAA;
		}
		break;

		case POINTER_MODE_PLAY:
		{
			editor.ui.pointerMode = pointerMode;
			if (carry)
				editor.ui.previousPointerMode = editor.ui.pointerMode;

			palette[PAL_MOUSE_1] = 0x444400;
			palette[PAL_MOUSE_2] = 0x777700;
			palette[PAL_MOUSE_3] = 0xAAAA00;
		}
		break;

		case POINTER_MODE_EDIT:
		{
			editor.ui.pointerMode = pointerMode;
			if (carry)
				editor.ui.previousPointerMode = editor.ui.pointerMode;

			palette[PAL_MOUSE_1] = 0x000066;
			palette[PAL_MOUSE_2] = 0x004499;
			palette[PAL_MOUSE_3] = 0x0055BB;
		}
		break;

		case POINTER_MODE_RECORD:
		{
			editor.ui.pointerMode = pointerMode;
			if (carry)
				editor.ui.previousPointerMode = editor.ui.pointerMode;

			palette[PAL_MOUSE_1] = 0x000066;
			palette[PAL_MOUSE_2] = 0x004499;
			palette[PAL_MOUSE_3] = 0x0055BB;
		}
		break;

		case POINTER_MODE_MSG1:
		{
			editor.ui.pointerMode = pointerMode;
			if (carry)
				editor.ui.previousPointerMode = editor.ui.pointerMode;

			palette[PAL_MOUSE_1] = 0x440044;
			palette[PAL_MOUSE_2] = 0x770077;
			palette[PAL_MOUSE_3] = 0xAA00AA;
		}
		break;

		case POINTER_MODE_READ_DIR:
		{
			editor.ui.pointerMode = pointerMode;
			if (carry)
				editor.ui.previousPointerMode = editor.ui.pointerMode;

			palette[PAL_MOUSE_1] = 0x004400;
			palette[PAL_MOUSE_2] = 0x007700;
			palette[PAL_MOUSE_3] = 0x00AA00;
		}
		break;

		case POINTER_MODE_LOAD:
		{
			editor.ui.pointerMode = pointerMode;
			if (carry)
				editor.ui.previousPointerMode = editor.ui.pointerMode;

			palette[PAL_MOUSE_1] = 0x0000AA;
			palette[PAL_MOUSE_2] = 0x000077;
			palette[PAL_MOUSE_3] = 0x000044;
		}
		break;

		default: break;
	}
}

void pointerSetPreviousMode(void)
{
	if (editor.ui.editTextFlag || editor.ui.askScreenShown || editor.ui.clearScreenShown)
		pointerSetMode(POINTER_MODE_MSG1, NO_CARRY);
	else
		pointerSetMode(editor.ui.previousPointerMode, NO_CARRY);
}
