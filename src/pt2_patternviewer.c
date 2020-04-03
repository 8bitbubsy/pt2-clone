// for finding memory leaks in debug mode with Visual Studio 
#if defined _DEBUG && defined _MSC_VER
#include <crtdbg.h>
#endif

#include <stdint.h>
#include "pt2_header.h"
#include "pt2_palette.h"
#include "pt2_tables.h"
#include "pt2_textout.h"
#include "pt2_helpers.h"

#define VISIBLE_ROWS 15

static uint8_t periodToNote(int16_t period)
{
	uint8_t l, m, h;

	l = 0;
	h = 35;

	while (h >= l)
	{
		m = (h + l) / 2;
		if (m >= 36)
			break; // should never happen, but let's stay on the safe side

		     if (periodTable[m] == period) return m;
		else if (periodTable[m] > period) l = m + 1;
		else h = m - 1;
	}

	return 255; // illegal period
}

static void drawPatternNormal(void)
{
	int8_t rowMiddlePos;
	uint8_t j, h, tempNote, rowDispCheck;
	uint16_t x, y, rowData;
	const uint32_t *srcPtr;
	uint32_t bufferOffset, *dstPtr;
	note_t note;

	for (uint8_t i = 0; i < VISIBLE_ROWS; i++)
	{
		rowMiddlePos = i - 7;
		rowDispCheck = modEntry->currRow + rowMiddlePos;

		if (rowDispCheck < MOD_ROWS)
		{
			rowData = rowDispCheck * 4;
			y = 140 + (i * 7);

			if (i == 7) // are we on the play row (middle)?
			{
				y++; // align font to play row (middle)

				// put current row number
				printTwoDecimalsBigBg(8, y, rowMiddlePos + modEntry->currRow, video.palette[PAL_GENTXT], video.palette[PAL_GENBKG]);

				// pattern data
				for (j = 0; j < AMIGA_VOICES; j++)
				{
					note = modEntry->patterns[modEntry->currPattern][rowData + j];
					x = 26 + (j * 72);

					if (note.period == 0)
					{
						textOutBigBg(x + 6, y, "---", video.palette[PAL_GENTXT], video.palette[PAL_GENBKG]);
					}
					else
					{
						tempNote = periodToNote(note.period);
						if (tempNote == 255)
							textOutBigBg(x + 6, y, "???", video.palette[PAL_GENTXT], video.palette[PAL_GENBKG]);
						else
							textOutBigBg(x + 6, y, config.accidental ? noteNames2[tempNote] : noteNames1[tempNote], video.palette[PAL_GENTXT], video.palette[PAL_GENBKG]);
					}

					if (config.blankZeroFlag)
					{
						if (note.sample & 0xF0)
							printOneHexBigBg(x + 30, y, note.sample >> 4, video.palette[PAL_GENTXT], video.palette[PAL_GENBKG]);
						else
							printOneHexBigBg(x + 30, y, ' ', video.palette[PAL_GENBKG], video.palette[PAL_GENBKG]);
					}
					else
					{
						printOneHexBigBg(x + 30, y, note.sample >> 4, video.palette[PAL_GENTXT], video.palette[PAL_GENBKG]);
					}

					printOneHexBigBg(x + 38, y, note.sample & 0x0F, video.palette[PAL_GENTXT], video.palette[PAL_GENBKG]);
					printOneHexBigBg(x + 46, y, note.command, video.palette[PAL_GENTXT], video.palette[PAL_GENBKG]);
					printTwoHexBigBg(x + 54, y, note.param, video.palette[PAL_GENTXT], video.palette[PAL_GENBKG]);
				}
			}
			else
			{
				if (i > 7)
					y += 7; // beyond play row, jump some pixels out of the row (middle)

				// put current row number
				printTwoDecimalsBg(8, y, rowMiddlePos + modEntry->currRow, video.palette[PAL_PATTXT], video.palette[PAL_BACKGRD]);

				// pattern data
				for (j = 0; j < AMIGA_VOICES; j++)
				{
					note = modEntry->patterns[modEntry->currPattern][rowData + j];
					x = 26 + (j * 72);

					if (note.period == 0)
					{
						textOutBg(x + 6, y, "---", video.palette[PAL_PATTXT], video.palette[PAL_BACKGRD]);
					}
					else
					{
						tempNote = periodToNote(note.period);
						if (tempNote == 255)
							textOutBg(x + 6, y, "???", video.palette[PAL_PATTXT], video.palette[PAL_BACKGRD]);
						else
							textOutBg(x + 6, y, config.accidental ? noteNames2[tempNote] : noteNames1[tempNote], video.palette[PAL_PATTXT], video.palette[PAL_BACKGRD]);
					}

					if (config.blankZeroFlag)
					{
						if (note.sample & 0xF0)
							printOneHexBg(x + 30, y, note.sample >> 4, video.palette[PAL_PATTXT], video.palette[PAL_BACKGRD]);
						else
							printOneHexBg(x + 30, y, ' ', video.palette[PAL_BACKGRD], video.palette[PAL_BACKGRD]);
					}
					else
					{
						printOneHexBg(x + 30, y, note.sample >> 4, video.palette[PAL_PATTXT], video.palette[PAL_BACKGRD]);
					}

					printOneHexBg(x + 38, y, note.sample & 0x0F, video.palette[PAL_PATTXT], video.palette[PAL_BACKGRD]);
					printOneHexBg(x + 46, y, note.command, video.palette[PAL_PATTXT], video.palette[PAL_BACKGRD]);
					printTwoHexBg(x + 54, y, note.param, video.palette[PAL_PATTXT], video.palette[PAL_BACKGRD]);
				}
			}
		}
	}

	// clear outside rows

	if (modEntry->currRow <= 6)
	{
		srcPtr = &trackerFrameBMP[140 * SCREEN_W];
		dstPtr = &video.frameBuffer[140 * SCREEN_W];
		memcpy(dstPtr, srcPtr, (SCREEN_W * sizeof (int32_t)) * ((7 - modEntry->currRow) * 7));
	}
	else if (modEntry->currRow >= 57)
	{
		h = (modEntry->currRow - 56) * 7;
		bufferOffset = (250 - h) * SCREEN_W;

		srcPtr = &trackerFrameBMP[bufferOffset];
		dstPtr = &video.frameBuffer[bufferOffset];
		memcpy(dstPtr, srcPtr, (SCREEN_W * sizeof (int32_t)) * h);
	}
}

static void drawPatternDotted(void)
{
	int8_t rowMiddlePos;
	uint8_t j, h, tempNote, rowDispCheck;
	uint16_t x, y, rowData;
	const uint32_t *srcPtr;
	uint32_t bufferOffset, *dstPtr;
	note_t note;

	for (uint8_t i = 0; i < VISIBLE_ROWS; i++)
	{
		rowMiddlePos = i - 7;
		rowDispCheck = modEntry->currRow + rowMiddlePos;

		if (rowDispCheck < MOD_ROWS)
		{
			rowData = rowDispCheck * 4;
			y = 140 + (i * 7);

			if (i == 7) // are we on the play row (middle)?
			{
				y++; // align font to play row (middle)

				// put current row number
				printTwoDecimalsBigBg(8, y, rowMiddlePos + modEntry->currRow, video.palette[PAL_GENTXT], video.palette[PAL_GENBKG]);

				// pattern data
				for (j = 0; j < AMIGA_VOICES; j++)
				{
					note = modEntry->patterns[modEntry->currPattern][rowData + j];
					x = 26 + (j * 72);

					if (note.period == 0)
					{
						charOutBigBg(x + 6, y, -128, video.palette[PAL_GENTXT], video.palette[PAL_GENBKG]);
						charOutBigBg(x + 14, y, -128, video.palette[PAL_GENTXT], video.palette[PAL_GENBKG]);
						charOutBigBg(x + 22, y, -128, video.palette[PAL_GENTXT], video.palette[PAL_GENBKG]);
					}
					else
					{
						tempNote = periodToNote(note.period);
						if (tempNote == 255)
							textOutBigBg(x + 6, y, "???", video.palette[PAL_GENTXT], video.palette[PAL_GENBKG]);
						else
							textOutBigBg(x + 6, y, config.accidental ? noteNames2[tempNote] : noteNames1[tempNote], video.palette[PAL_GENTXT], video.palette[PAL_GENBKG]);
					}

					if (note.sample)
					{
						printOneHexBigBg(x + 30, y, note.sample >> 4, video.palette[PAL_GENTXT], video.palette[PAL_GENBKG]);
						printOneHexBigBg(x + 38, y, note.sample & 0x0F, video.palette[PAL_GENTXT], video.palette[PAL_GENBKG]);
					}
					else
					{
						charOutBigBg(x + 30, y, -128, video.palette[PAL_GENTXT], video.palette[PAL_GENBKG]);
						charOutBigBg(x + 38, y, -128, video.palette[PAL_GENTXT], video.palette[PAL_GENBKG]);
					}

					if ((note.command | note.param) == 0)
					{
						charOutBigBg(x + 46, y, -128, video.palette[PAL_GENTXT], video.palette[PAL_GENBKG]);
						charOutBigBg(x + 54, y, -128, video.palette[PAL_GENTXT], video.palette[PAL_GENBKG]);
						charOutBigBg(x + 62, y, -128, video.palette[PAL_GENTXT], video.palette[PAL_GENBKG]);
					}
					else
					{
						printOneHexBigBg(x + 46, y, note.command, video.palette[PAL_GENTXT], video.palette[PAL_GENBKG]);
						printTwoHexBigBg(x + 54, y, note.param, video.palette[PAL_GENTXT], video.palette[PAL_GENBKG]);
					}
				}
			}
			else
			{
				if (i > 7)
					y += 7; // beyond play row, jump some pixels out of the row (middle)

				// put current row number
				printTwoDecimalsBg(8, y, rowMiddlePos + modEntry->currRow, video.palette[PAL_PATTXT], video.palette[PAL_BACKGRD]);

				// pattern data
				for (j = 0; j < AMIGA_VOICES; j++)
				{
					note = modEntry->patterns[modEntry->currPattern][rowData + j];
					x = 26 + (j * 72);

					if (note.period == 0)
					{
						charOutBg(x + 6, y, -128, video.palette[PAL_PATTXT], video.palette[PAL_BACKGRD]);
						charOutBg(x + 14, y, -128, video.palette[PAL_PATTXT], video.palette[PAL_BACKGRD]);
						charOutBg(x + 22, y, -128, video.palette[PAL_PATTXT], video.palette[PAL_BACKGRD]);
					}
					else
					{
						tempNote = periodToNote(note.period);
						if (tempNote == 255)
							textOutBg(x + 6, y, "???", video.palette[PAL_PATTXT], video.palette[PAL_BACKGRD]);
						else
							textOutBg(x + 6, y, config.accidental ? noteNames2[tempNote] : noteNames1[tempNote], video.palette[PAL_PATTXT], video.palette[PAL_BACKGRD]);
					}

					if (note.sample)
					{
						printOneHexBg(x + 30, y, note.sample >> 4, video.palette[PAL_PATTXT], video.palette[PAL_BACKGRD]);
						printOneHexBg(x + 38, y, note.sample & 0x0F, video.palette[PAL_PATTXT], video.palette[PAL_BACKGRD]);
					}
					else
					{
						charOutBg(x + 30, y, -128, video.palette[PAL_PATTXT], video.palette[PAL_BACKGRD]);
						charOutBg(x + 38, y, -128, video.palette[PAL_PATTXT], video.palette[PAL_BACKGRD]);
					}

					if ((note.command | note.param) == 0)
					{
						charOutBg(x + 46, y, -128, video.palette[PAL_PATTXT], video.palette[PAL_BACKGRD]);
						charOutBg(x + 54, y, -128, video.palette[PAL_PATTXT], video.palette[PAL_BACKGRD]);
						charOutBg(x + 62, y, -128, video.palette[PAL_PATTXT], video.palette[PAL_BACKGRD]);
					}
					else
					{
						printOneHexBg(x + 46, y, note.command, video.palette[PAL_PATTXT], video.palette[PAL_BACKGRD]);
						printTwoHexBg(x + 54, y, note.param, video.palette[PAL_PATTXT], video.palette[PAL_BACKGRD]);
					}
				}
			}
		}
	}

	// clear outside rows

	if (modEntry->currRow <= 6)
	{
		srcPtr = &trackerFrameBMP[140 * SCREEN_W];
		dstPtr = &video.frameBuffer[140 * SCREEN_W];
		memcpy(dstPtr, srcPtr, (SCREEN_W * sizeof (int32_t)) * ((7 - modEntry->currRow) * 7));
	}
	else if (modEntry->currRow >= 57)
	{
		h = (modEntry->currRow - 56) * 7;
		bufferOffset = (250 - h) * SCREEN_W;

		srcPtr = &trackerFrameBMP[bufferOffset];
		dstPtr = &video.frameBuffer[bufferOffset];
		memcpy(dstPtr, srcPtr, (SCREEN_W * sizeof (int32_t)) * h);
	}
}

void redrawPattern(void)
{
	if (config.pattDots)
		drawPatternDotted();
	else
		drawPatternNormal();
}
