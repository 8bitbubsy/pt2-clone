#include <stdint.h>
#include "pt2_tables.h"
#include "pt2_textout.h"
#include "pt2_structs.h"
#include "pt2_config.h"
#include "pt2_visuals.h"

#define MIDDLE_ROW 7
#define VISIBLE_ROWS 15

static const char emptyDottedEffect[4] = { 0x02, 0x02, 0x02, 0x00 };
static const char emptyDottedSample[3] = { 0x02, 0x02, 0x00 };

static int32_t periodToNote(int32_t period) // 0 = no note, 1 = illegal note, 2..37 = note
{
	if (period == 0)
		return 0;

	int32_t beg = 0;
	int32_t end = 36 - 1;

	// do binary search
	while (beg <= end)
	{
		const int32_t mid = (beg + end) >> 1;

		int32_t tableVal = periodTable[mid];
		if (period == tableVal)
			return 2+mid;

		if (period < tableVal)
			beg = mid+1;
		else
			end = mid-1;
	}

	return 1; // illegal note
}

static void drawPatternNormal(void)
{
	const char **noteNames;
	if (config.accidental)
		noteNames = (const char **)noteNames2;
	else
		noteNames = (const char **)noteNames1;

	note_t *patt = song->patterns[song->currPattern];
	int32_t row = song->currRow - MIDDLE_ROW;
	int32_t y = 140;

	for (int32_t i = 0; i < VISIBLE_ROWS; i++, y += 7, row++)
	{
		if (row < 0 || row >= MOD_ROWS)
		{
			// clear empty rows outside of pattern data
			fillRect(8,         y, FONT_CHAR_W*2, FONT_CHAR_H, video.palette[PAL_BACKGRD]);
			fillRect(32+(0*72), y, FONT_CHAR_W*8, FONT_CHAR_H, video.palette[PAL_BACKGRD]);
			fillRect(32+(1*72), y, FONT_CHAR_W*8, FONT_CHAR_H, video.palette[PAL_BACKGRD]);
			fillRect(32+(2*72), y, FONT_CHAR_W*8, FONT_CHAR_H, video.palette[PAL_BACKGRD]);
			fillRect(32+(3*72), y, FONT_CHAR_W*8, FONT_CHAR_H, video.palette[PAL_BACKGRD]);
		}
		else
		{
			if (i == MIDDLE_ROW) // middle row has twice as tall glyphs
			{
				y++;
				printTwoDecimalsBigBg(8, y, row, video.palette[PAL_GENTXT], video.palette[PAL_GENBKG]);

				note_t *note = patt + (row << 2);
				int32_t x = 32;
				for (int32_t j = 0; j < PAULA_VOICES; j++, note++)
				{
					textOutBigBg(x, y, noteNames[periodToNote(note->period)], video.palette[PAL_GENTXT], video.palette[PAL_GENBKG]);
					x += 8*3;

					char smpChar = (config.blankZeroFlag && !(note->sample & 0xF0)) ? ' ' : hexTable[note->sample >> 4];
					charOutBigBg(x, y, smpChar, video.palette[PAL_GENTXT], video.palette[PAL_GENBKG]);
					x += 8;

					printOneHexBigBg(x, y, note->sample & 0x0F, video.palette[PAL_GENTXT], video.palette[PAL_GENBKG]);
					x += 8;

					printOneHexBigBg(x, y, note->command, video.palette[PAL_GENTXT], video.palette[PAL_GENBKG]);
					x += 8;

					printTwoHexBigBg(x, y, note->param, video.palette[PAL_GENTXT], video.palette[PAL_GENBKG]);
					x += (8*2)+8;
				}
				y += 6;
			}
			else // non-middle rows
			{
				printTwoDecimalsBg(8, y, row, video.palette[PAL_PATTXT], video.palette[PAL_BACKGRD]);

				note_t *note = patt + (row << 2);
				int32_t x = 32;
				for (int32_t j = 0; j < PAULA_VOICES; j++, note++)
				{
					textOutBg(x, y, noteNames[periodToNote(note->period)], video.palette[PAL_PATTXT], video.palette[PAL_BACKGRD]);
					x += 8*3;

					char smpChar = (config.blankZeroFlag && !(note->sample & 0xF0)) ? ' ' : hexTable[note->sample >> 4];
					charOutBg(x, y, smpChar, video.palette[PAL_PATTXT], video.palette[PAL_BACKGRD]);
					x += 8;

					printOneHexBg(x , y, note->sample & 0x0F, video.palette[PAL_PATTXT], video.palette[PAL_BACKGRD]);
					x += 8;

					printOneHexBg(x, y, note->command, video.palette[PAL_PATTXT], video.palette[PAL_BACKGRD]);
					x += 8;

					printTwoHexBg(x, y, note->param, video.palette[PAL_PATTXT], video.palette[PAL_BACKGRD]);
					x += (8*2)+8;
				}
			}
		}
	}
}

static void drawPatternDotted(void)
{
	const char **noteNames;
	if (config.accidental)
		noteNames = (const char **)noteNames4;
	else
		noteNames = (const char **)noteNames3;

	note_t *patt = song->patterns[song->currPattern];
	int32_t row = song->currRow - MIDDLE_ROW;
	int32_t y = 140;

	for (int32_t i = 0; i < VISIBLE_ROWS; i++, y += 7, row++)
	{
		if (row < 0 || row >= MOD_ROWS)
		{
			// clear empty rows outside of pattern data
			fillRect(8,         y, FONT_CHAR_W*2, FONT_CHAR_H, video.palette[PAL_BACKGRD]);
			fillRect(32+(0*72), y, FONT_CHAR_W*8, FONT_CHAR_H, video.palette[PAL_BACKGRD]);
			fillRect(32+(1*72), y, FONT_CHAR_W*8, FONT_CHAR_H, video.palette[PAL_BACKGRD]);
			fillRect(32+(2*72), y, FONT_CHAR_W*8, FONT_CHAR_H, video.palette[PAL_BACKGRD]);
			fillRect(32+(3*72), y, FONT_CHAR_W*8, FONT_CHAR_H, video.palette[PAL_BACKGRD]);
		}
		else
		{
			if (i == MIDDLE_ROW) // middle row has twice as tall glyphs
			{
				y++;
				printTwoDecimalsBigBg(8, y, row, video.palette[PAL_GENTXT], video.palette[PAL_GENBKG]);

				note_t *note = patt + (row << 2);
				int32_t x = 32;
				for (int32_t j = 0; j < PAULA_VOICES; j++, note++)
				{
					textOutBigBg(x, y, noteNames[periodToNote(note->period)], video.palette[PAL_GENTXT], video.palette[PAL_GENBKG]);
					x += 8*3;

					if (note->sample == 0)
					{
						textOutBigBg(x, y, emptyDottedSample, video.palette[PAL_GENTXT], video.palette[PAL_GENBKG]);
						x += 8*2;
					}
					else
					{
						char smpChar = (note->sample & 0xF0) ? hexTable[note->sample >> 4] : 0x02;
						charOutBigBg(x, y, smpChar, video.palette[PAL_GENTXT], video.palette[PAL_GENBKG]);
						x += 8;
						printOneHexBigBg(x, y, note->sample & 0x0F, video.palette[PAL_GENTXT], video.palette[PAL_GENBKG]);
						x += 8;
					}

					if (note->command == 0 && note->param == 0)
					{
						textOutBigBg(x, y, emptyDottedEffect, video.palette[PAL_GENTXT], video.palette[PAL_GENBKG]);
						x += (8*3)+8;
					}
					else
					{
						printOneHexBigBg(x, y, note->command, video.palette[PAL_GENTXT], video.palette[PAL_GENBKG]);
						x += 8;
						printTwoHexBigBg(x, y, note->param, video.palette[PAL_GENTXT], video.palette[PAL_GENBKG]);
						x += (8*2)+8;
					}
				}
				y += 6;
			}
			else // non-middle rows
			{
				printTwoDecimalsBg(8, y, row, video.palette[PAL_PATTXT], video.palette[PAL_BACKGRD]);

				// pattern data
				note_t *note = patt + (row << 2);
				int32_t x = 32;
				for (int32_t j = 0; j < PAULA_VOICES; j++, note++)
				{
					textOutBg(x, y, noteNames[periodToNote(note->period)], video.palette[PAL_PATTXT], video.palette[PAL_BACKGRD]);
					x += 8*3;

					if (note->sample == 0)
					{
						textOutBg(x, y, emptyDottedSample, video.palette[PAL_PATTXT], video.palette[PAL_BACKGRD]);
						x += 8*2;
					}
					else
					{
						char smpChar = (note->sample & 0xF0) ? hexTable[note->sample >> 4] : 0x02;
						charOutBg(x, y, smpChar, video.palette[PAL_PATTXT], video.palette[PAL_BACKGRD]);
						x += 8;
						printOneHexBg(x, y, note->sample & 0x0F, video.palette[PAL_PATTXT], video.palette[PAL_BACKGRD]);
						x += 8;
					}

					if (note->command == 0 && note->param == 0)
					{
						textOutBg(x, y, emptyDottedEffect, video.palette[PAL_PATTXT], video.palette[PAL_BACKGRD]);
						x += (8*3)+8;
					}
					else
					{
						printOneHexBg(x, y, note->command, video.palette[PAL_PATTXT], video.palette[PAL_BACKGRD]);
						x += 8;
						printTwoHexBg(x, y, note->param, video.palette[PAL_PATTXT], video.palette[PAL_BACKGRD]);
						x += (8*2)+8;
					}
				}
			}
		}
	}
}

void redrawPattern(void)
{
	if (config.pattDots)
		drawPatternDotted();
	else
		drawPatternNormal();
}
