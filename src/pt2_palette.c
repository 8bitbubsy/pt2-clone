#include "pt2_structs.h"
#include "pt2_palette.h"

void setDefaultPalette(void)
{
	// default ProTracker palette
	video.palette[PAL_BACKGRD] = 0x000000;
	video.palette[PAL_BORDER] = 0xBBBBBB;
	video.palette[PAL_GENBKG] = 0x888888;
	video.palette[PAL_GENBKG2] = 0x555555;
	video.palette[PAL_QADSCP] = 0xFFDD00;
	video.palette[PAL_PATCURSOR] = 0xDD0044;
	video.palette[PAL_GENTXT] = 0x000000;
	video.palette[PAL_PATTXT] = 0x3344FF;
	video.palette[PAL_SAMPLLINE] = 0x00FFFF;
	video.palette[PAL_LOOPPIN] = 0x0000FF;
	video.palette[PAL_TEXTMARK] = 0x770077;
	video.palette[PAL_MOUSE_1] = 0x444444;
	video.palette[PAL_MOUSE_2] = 0x777777;
	video.palette[PAL_MOUSE_3] = 0xAAAAAA;

	video.palette[PAL_COLORKEY] = 0xC0FFEE;
}
