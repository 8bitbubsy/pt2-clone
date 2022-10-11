#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "pt2_tables.h"
#include "pt2_structs.h"
#include "pt2_bmp.h"

void charOut(uint32_t xPos, uint32_t yPos, char ch, uint32_t color)
{
	if (ch == '\0' || ch == ' ')
		return;

	int32_t h = FONT_CHAR_H;
	if (ch == 5 || ch == 6) // arrow up/down has 1 more scanline
		h++;
	
	const uint8_t *srcPtr = &fontBMP[(ch & 0x7F) << 3];
	uint32_t *dstPtr = &video.frameBuffer[(yPos * SCREEN_W) + xPos];

	for (int32_t y = 0; y < h; y++)
	{
		for (int32_t x = 0; x < FONT_CHAR_W; x++)
		{
			if (srcPtr[x])
				dstPtr[x] = color;
		}

		srcPtr += 127*FONT_CHAR_W;
		dstPtr += SCREEN_W;
	}
}

void charOut2(uint32_t xPos, uint32_t yPos, char ch) // for static GUI text
{
	if (ch == '\0' || ch == ' ')
		return;

	int32_t h = FONT_CHAR_H;
	if (ch == 5 || ch == 6) // arrow up/down has 1 more scanline
		h++;
	
	const uint8_t *srcPtr = &fontBMP[(ch & 0x7F) << 3];
	uint32_t *dstPtr = &video.frameBuffer[(yPos * SCREEN_W) + xPos];

	const uint32_t fgColor = video.palette[PAL_BORDER];
	const uint32_t bgColor = video.palette[PAL_GENBKG2];

	for (int32_t y = 0; y < h; y++)
	{
		for (int32_t x = 0; x < FONT_CHAR_W; x++)
		{
			if (srcPtr[x])
			{
				dstPtr[x+(SCREEN_W+1)] = bgColor;
				dstPtr[x] = fgColor;
			}
		}

		srcPtr += 127*FONT_CHAR_W;
		dstPtr += SCREEN_W;
	}
}

void charOutBg(uint32_t xPos, uint32_t yPos, char ch, uint32_t fgColor, uint32_t bgColor)
{
	uint32_t colors[2];

	if (ch == '\0')
		return;

	int32_t h = FONT_CHAR_H;
	if (ch == 5 || ch == 6) // arrow up/down has 1 more scanline
		h++;

	const uint8_t *srcPtr = &fontBMP[(ch & 0x7F) << 3];
	uint32_t *dstPtr = &video.frameBuffer[(yPos * SCREEN_W) + xPos];

	colors[0] = bgColor;
	colors[1] = fgColor;

	for (int32_t y = 0; y < h; y++)
	{
		for (int32_t x = 0; x < FONT_CHAR_W; x++)
			dstPtr[x] = colors[srcPtr[x]];

		srcPtr += 127*FONT_CHAR_W;
		dstPtr += SCREEN_W;
	}
}

void charOutBig(uint32_t xPos, uint32_t yPos, char ch, uint32_t color)
{
	if (ch == '\0' || ch == ' ')
		return;

	int32_t h = FONT_CHAR_H;
	if (ch == 5 || ch == 6) // arrow up/down has 1 more scanline
		h++;

	const uint8_t *srcPtr = &fontBMP[(ch & 0x7F) << 3];
	uint32_t *dstPtr1 = &video.frameBuffer[(yPos * SCREEN_W) + xPos];
	uint32_t *dstPtr2 = dstPtr1 + SCREEN_W;

	for (int32_t y = 0; y < h; y++)
	{
		for (int32_t x = 0; x < FONT_CHAR_W; x++)
		{
			if (srcPtr[x])
			{
				dstPtr1[x] = color;
				dstPtr2[x] = color;
			}
		}

		srcPtr += 127*FONT_CHAR_W;
		dstPtr1 += SCREEN_W*2;
		dstPtr2 += SCREEN_W*2;
	}
}

void charOutBigBg(uint32_t xPos, uint32_t yPos, char ch, uint32_t fgColor, uint32_t bgColor)
{
	uint32_t colors[2];

	if (ch == '\0')
		return;

	const uint8_t *srcPtr = &fontBMP[(ch & 0x7F) << 3];
	uint32_t *dstPtr1 = &video.frameBuffer[(yPos * SCREEN_W) + xPos];
	uint32_t *dstPtr2 = dstPtr1 + SCREEN_W;

	colors[0] = bgColor;
	colors[1] = fgColor;

	for (int32_t y = 0; y < FONT_CHAR_H; y++)
	{
		for (int32_t x = 0; x < FONT_CHAR_W; x++)
		{
			const uint32_t pixel = colors[srcPtr[x]];
			dstPtr1[x] = pixel;
			dstPtr2[x] = pixel;
		}

		srcPtr += 127*FONT_CHAR_W;
		dstPtr1 += SCREEN_W*2;
		dstPtr2 += SCREEN_W*2;
	}
}

void textOut(uint32_t xPos, uint32_t yPos, const char *text, uint32_t color)
{
	assert(text != NULL);

	uint32_t x = xPos;
	while (*text != '\0')
	{
		charOut(x, yPos, *text++, color);
		x += FONT_CHAR_W;
	}
}

void textOutN(uint32_t xPos, uint32_t yPos, const char *text, uint32_t n, uint32_t color)
{
	assert(text != NULL);

	uint32_t x = xPos;
	uint32_t i = 0;

	while (*text != '\0' && i++ < n)
	{
		charOut(x, yPos, *text++, color);
		x += FONT_CHAR_W;
	}
}

void textOut2(uint32_t xPos, uint32_t yPos, const char *text) // for static GUI text
{
	assert(text != NULL);

	uint32_t x = xPos;
	while (*text != '\0')
	{
		charOut2(x, yPos, *text++);
		x += FONT_CHAR_W-1;
	}
}

void textOutTight(uint32_t xPos, uint32_t yPos, const char *text, uint32_t color)
{
	assert(text != NULL);

	uint32_t x = xPos;
	while (*text != '\0')
	{
		charOut(x, yPos, *text++, color);
		x += FONT_CHAR_W-1;
	}
}

void textOutTightN(uint32_t xPos, uint32_t yPos, const char *text, uint32_t n, uint32_t color)
{
	assert(text != NULL);

	uint32_t x = xPos;
	uint32_t i = 0;

	while (*text != '\0' && i++ < n)
	{
		charOut(x, yPos, *text++, color);
		x += FONT_CHAR_W-1;
	}
}

void textOutBg(uint32_t xPos, uint32_t yPos, const char *text, uint32_t fgColor, uint32_t bgColor)
{
	assert(text != NULL);

	uint32_t x = xPos;
	while (*text != '\0')
	{
		charOutBg(x, yPos, *text++, fgColor, bgColor);
		x += FONT_CHAR_W;
	}
}

void textOutBig(uint32_t xPos, uint32_t yPos, const char *text, uint32_t color)
{
	assert(text != NULL);

	uint32_t x = xPos;
	while (*text != '\0')
	{
		charOutBig(x, yPos, *text++, color);
		x += FONT_CHAR_W;
	}
}

void textOutBigBg(uint32_t xPos, uint32_t yPos, const char *text, uint32_t fgColor, uint32_t bgColor)
{
	assert(text != NULL);

	uint32_t x = xPos;
	while (*text != '\0')
	{
		charOutBigBg(x, yPos, *text++, fgColor, bgColor);
		x += FONT_CHAR_W;
	}
}

void printTwoDecimals(uint32_t x, uint32_t y, uint32_t value, uint32_t fontColor)
{
	if (value == 0)
	{
		textOut(x, y, "00", fontColor);
	}
	else
	{
		if (value > 99)
			value = 99;

		charOut(x + (FONT_CHAR_W * 1), y, '0' + (value % 10), fontColor); value /= 10;
		charOut(x + (FONT_CHAR_W * 0), y, '0' + (value % 10), fontColor);
	}
}

void printTwoDecimalsBig(uint32_t x, uint32_t y, uint32_t value, uint32_t fontColor)
{
	if (value == 0)
	{
		textOutBig(x, y, "00", fontColor);
	}
	else
	{
		if (value > 99)
			value = 99;

		charOutBig(x + (FONT_CHAR_W * 1), y, '0' + (value % 10), fontColor); value /= 10;
		charOutBig(x + (FONT_CHAR_W * 0), y, '0' + (value % 10), fontColor);
	}
}

void printThreeDecimals(uint32_t x, uint32_t y, uint32_t value, uint32_t fontColor)
{
	if (value == 0)
	{
		textOut(x, y, "000", fontColor);
	}
	else
	{
		if (value > 999)
			value = 999;

		charOut(x + (FONT_CHAR_W * 2), y, '0' + (value % 10), fontColor); value /= 10;
		charOut(x + (FONT_CHAR_W * 1), y, '0' + (value % 10), fontColor); value /= 10;
		charOut(x + (FONT_CHAR_W * 0), y, '0' + (value % 10), fontColor);
	}
}

void printFourDecimals(uint32_t x, uint32_t y, uint32_t value, uint32_t fontColor)
{
	if (value == 0)
	{
		textOut(x, y, "0000", fontColor);
	}
	else
	{
		if (value > 9999)
			value = 9999;

		charOut(x + (FONT_CHAR_W * 3), y, '0' + (value % 10), fontColor); value /= 10;
		charOut(x + (FONT_CHAR_W * 2), y, '0' + (value % 10), fontColor); value /= 10;
		charOut(x + (FONT_CHAR_W * 1), y, '0' + (value % 10), fontColor); value /= 10;
		charOut(x + (FONT_CHAR_W * 0), y, '0' + (value % 10), fontColor);
	}
}

void printFiveDecimals(uint32_t x, uint32_t y, uint32_t value, uint32_t fontColor)
{
	if (value == 0)
	{
		textOut(x, y, "00000", fontColor);
	}
	else
	{
		if (value > 99999)
			value = 99999;

		charOut(x + (FONT_CHAR_W * 4), y, '0' + (value % 10), fontColor); value /= 10;
		charOut(x + (FONT_CHAR_W * 3), y, '0' + (value % 10), fontColor); value /= 10;
		charOut(x + (FONT_CHAR_W * 2), y, '0' + (value % 10), fontColor); value /= 10;
		charOut(x + (FONT_CHAR_W * 1), y, '0' + (value % 10), fontColor); value /= 10;
		charOut(x + (FONT_CHAR_W * 0), y, '0' + (value % 10), fontColor);
	}
}

// this one is used for module size and sampler screen display length (zeroes are padded with space)
void printSixDecimals(uint32_t x, uint32_t y, uint32_t value, uint32_t fontColor)
{
	char numberText[7];

	if (value == 0)
	{
		textOut(x, y, "     0", fontColor);
	}
	else
	{
		if (value > 999999)
			value = 999999;

		numberText[6] = 0;
		numberText[5] = '0' + (value % 10); value /= 10;
		numberText[4] = '0' + (value % 10); value /= 10;
		numberText[3] = '0' + (value % 10); value /= 10;
		numberText[2] = '0' + (value % 10); value /= 10;
		numberText[1] = '0' + (value % 10); value /= 10;
		numberText[0] = '0' + (value % 10);

		int32_t i = 0;
		while (numberText[i] == '0')
			numberText[i++] = ' ';

		textOut(x, y, numberText, fontColor);
	}
}

void printOneHex(uint32_t x, uint32_t y, uint32_t value, uint32_t fontColor)
{
	charOut(x, y, hexTable[value & 15], fontColor);
}

void printOneHexBig(uint32_t x, uint32_t y, uint32_t value, uint32_t fontColor)
{
	charOutBig(x, y, hexTable[value & 15], fontColor);
}

void printTwoHex(uint32_t x, uint32_t y, uint32_t value, uint32_t fontColor)
{
	if (value == 0)
	{
		textOut(x, y, "00", fontColor);
	}
	else
	{
		value &= 0xFF;

		charOut(x + (FONT_CHAR_W * 0), y, hexTable[value >> 4], fontColor);
		charOut(x + (FONT_CHAR_W * 1), y, hexTable[value & 15], fontColor);
	}
}

void printTwoHexBig(uint32_t x, uint32_t y, uint32_t value, uint32_t fontColor)
{
	if (value == 0)
	{
		textOutBig(x, y, "00", fontColor);
	}
	else
	{
		value &= 0xFF;

		charOutBig(x + (FONT_CHAR_W * 0), y, hexTable[value >> 4], fontColor);
		charOutBig(x + (FONT_CHAR_W * 1), y, hexTable[value & 15], fontColor);
	}
}

void printThreeHex(uint32_t x, uint32_t y, uint32_t value, uint32_t fontColor)
{
	if (value == 0)
	{
		textOut(x, y, "000", fontColor);
	}
	else
	{
		value &= 0xFFF;

		charOut(x + (FONT_CHAR_W * 0), y, hexTable[value >> 8], fontColor);
		charOut(x + (FONT_CHAR_W * 1), y, hexTable[(value & (15 << 4)) >> 4], fontColor);
		charOut(x + (FONT_CHAR_W * 2), y, hexTable[value & 15], fontColor);
	}
}

void printFourHex(uint32_t x, uint32_t y, uint32_t value, uint32_t fontColor)
{
	if (value == 0)
	{
		textOut(x, y, "0000", fontColor);
	}
	else
	{
		value &= 0xFFFF;

		charOut(x + (FONT_CHAR_W * 0), y, hexTable[value >> 12], fontColor);
		charOut(x + (FONT_CHAR_W * 1), y, hexTable[(value & (15 << 8)) >> 8], fontColor);
		charOut(x + (FONT_CHAR_W * 2), y, hexTable[(value & (15 << 4)) >> 4], fontColor);
		charOut(x + (FONT_CHAR_W * 3), y, hexTable[value & 15], fontColor);
	}
}

void printFiveHex(uint32_t x, uint32_t y, uint32_t value, uint32_t fontColor)
{
	if (value == 0)
	{
		textOut(x, y, "00000", fontColor);
	}
	else
	{
		value &= 0xFFFFF;

		charOut(x + (FONT_CHAR_W * 0), y, hexTable[value >> 16], fontColor);
		charOut(x + (FONT_CHAR_W * 1), y, hexTable[(value & (15 << 12)) >> 12], fontColor);
		charOut(x + (FONT_CHAR_W * 2), y, hexTable[(value & (15 << 8)) >> 8], fontColor);
		charOut(x + (FONT_CHAR_W * 3), y, hexTable[(value & (15 << 4)) >> 4], fontColor);
		charOut(x + (FONT_CHAR_W * 4), y, hexTable[value & 15], fontColor);
	}
}

void printTwoDecimalsBg(uint32_t x, uint32_t y, uint32_t value, uint32_t fontColor, uint32_t backColor)
{
	if (value == 0)
	{
		textOutBg(x, y, "00", fontColor, backColor);
	}
	else
	{
		if (value > 99)
			value = 99;

		charOutBg(x + (FONT_CHAR_W * 1), y, '0' + (value % 10), fontColor, backColor); value /= 10;
		charOutBg(x + (FONT_CHAR_W * 0), y, '0' + (value % 10), fontColor, backColor);
	}
}

void printTwoDecimalsBigBg(uint32_t x, uint32_t y, uint32_t value, uint32_t fontColor, uint32_t backColor)
{
	if (value == 0)
	{
		textOutBigBg(x, y, "00", fontColor, backColor);
	}
	else
	{
		if (value > 99)
			value = 99;

		charOutBigBg(x + (FONT_CHAR_W * 1), y, '0' + (value % 10), fontColor, backColor); value /= 10;
		charOutBigBg(x + (FONT_CHAR_W * 0), y, '0' + (value % 10), fontColor, backColor);
	}
}

void printThreeDecimalsBg(uint32_t x, uint32_t y, uint32_t value, uint32_t fontColor, uint32_t backColor)
{
	if (value == 0)
	{
		textOutBg(x, y, "000", fontColor, backColor);
	}
	else
	{
		if (value > 999)
			value = 999;

		charOutBg(x + (FONT_CHAR_W * 2), y, '0' + (value % 10), fontColor, backColor); value /= 10;
		charOutBg(x + (FONT_CHAR_W * 1), y, '0' + (value % 10), fontColor, backColor); value /= 10;
		charOutBg(x + (FONT_CHAR_W * 0), y, '0' + (value % 10), fontColor, backColor);
	}
}

void printFourDecimalsBg(uint32_t x, uint32_t y, uint32_t value, uint32_t fontColor, uint32_t backColor)
{
	if (value == 0)
	{
		textOutBg(x, y, "0000", fontColor, backColor);
	}
	else
	{
		if (value > 9999)
			value = 9999;

		charOutBg(x + (FONT_CHAR_W * 3), y, '0' + (value % 10), fontColor, backColor); value /= 10;
		charOutBg(x + (FONT_CHAR_W * 2), y, '0' + (value % 10), fontColor, backColor); value /= 10;
		charOutBg(x + (FONT_CHAR_W * 1), y, '0' + (value % 10), fontColor, backColor); value /= 10;
		charOutBg(x + (FONT_CHAR_W * 0), y, '0' + (value % 10), fontColor, backColor);
	}
}

// this one is used for "DISP:" in the sampler screen (zeroes are padded with space)
void printFiveDecimalsBg(uint32_t x, uint32_t y, uint32_t value, uint32_t fontColor, uint32_t backColor)
{
	char numberText[6];

	if (value == 0)
	{
		textOutBg(x, y, "    0", fontColor, backColor);
	}
	else
	{
		if (value > 99999)
			value = 99999;

		numberText[5] = 0;
		numberText[4] = '0' + (value % 10); value /= 10;
		numberText[3] = '0' + (value % 10); value /= 10;
		numberText[2] = '0' + (value % 10); value /= 10;
		numberText[1] = '0' + (value % 10); value /= 10;
		numberText[0] = '0' + (value % 10);

		int32_t i = 0;
		while (numberText[i] == '0')
			numberText[i++] = ' ';

		textOutBg(x, y, numberText, fontColor, backColor);
	}
}

// this one is used for module size (zeroes are padded with space)
void printSixDecimalsBg(uint32_t x, uint32_t y, uint32_t value, uint32_t fontColor, uint32_t backColor)
{
	char numberText[7];

	if (value == 0)
	{
		textOutBg(x, y, "     0", fontColor, backColor);
	}
	else
	{
		if (value > 999999)
			value = 999999;

		numberText[6] = 0;
		numberText[5] = '0' + (value % 10); value /= 10;
		numberText[4] = '0' + (value % 10); value /= 10;
		numberText[3] = '0' + (value % 10); value /= 10;
		numberText[2] = '0' + (value % 10); value /= 10;
		numberText[1] = '0' + (value % 10); value /= 10;
		numberText[0] = '0' + (value % 10);

		int32_t i = 0;
		while (numberText[i] == '0')
			numberText[i++] = ' ';

		textOutBg(x, y, numberText, fontColor, backColor);
	}
}

void printOneHexBg(uint32_t x, uint32_t y, uint32_t value, uint32_t fontColor, uint32_t backColor)
{
	charOutBg(x, y, hexTable[value & 0xF], fontColor, backColor);
}

void printOneHexBigBg(uint32_t x, uint32_t y, uint32_t value, uint32_t fontColor, uint32_t backColor)
{
	charOutBigBg(x, y, hexTable[value & 0xF], fontColor, backColor);
}

void printTwoHexBg(uint32_t x, uint32_t y, uint32_t value, uint32_t fontColor, uint32_t backColor)
{
	if (value == 0)
	{
		textOutBg(x, y, "00", fontColor, backColor);
	}
	else
	{
		value &= 0xFF;

		charOutBg(x + (FONT_CHAR_W * 0), y, hexTable[value >> 4], fontColor, backColor);
		charOutBg(x + (FONT_CHAR_W * 1), y, hexTable[value & 15], fontColor, backColor);
	}
}

void printTwoHexBigBg(uint32_t x, uint32_t y, uint32_t value, uint32_t fontColor, uint32_t backColor)
{
	if (value == 0)
	{
		textOutBigBg(x, y, "00", fontColor, backColor);
	}
	else
	{
		value &= 0xFF;

		charOutBigBg(x + (FONT_CHAR_W * 0), y, hexTable[value >> 4], fontColor, backColor);
		charOutBigBg(x + (FONT_CHAR_W * 1), y, hexTable[value & 15], fontColor, backColor);
	}
}

void printThreeHexBg(uint32_t x, uint32_t y, uint32_t value, uint32_t fontColor, uint32_t backColor)
{
	if (value == 0)
	{
		textOutBg(x, y, "000", fontColor, backColor);
	}
	else
	{
		value &= 0xFFF;

		charOutBg(x + (FONT_CHAR_W * 0), y, hexTable[value >> 8], fontColor, backColor);
		charOutBg(x + (FONT_CHAR_W * 1), y, hexTable[(value & (15 << 4)) >> 4], fontColor, backColor);
		charOutBg(x + (FONT_CHAR_W * 2), y, hexTable[value & 15], fontColor, backColor);
	}
}

void printFourHexBg(uint32_t x, uint32_t y, uint32_t value, uint32_t fontColor, uint32_t backColor)
{
	if (value == 0)
	{
		textOutBg(x, y, "0000", fontColor, backColor);
	}
	else
	{
		value &= 0xFFFF;

		charOutBg(x + (FONT_CHAR_W * 0), y, hexTable[value >> 12], fontColor, backColor);
		charOutBg(x + (FONT_CHAR_W * 1), y, hexTable[(value & (15 << 8)) >> 8], fontColor, backColor);
		charOutBg(x + (FONT_CHAR_W * 2), y, hexTable[(value & (15 << 4)) >> 4], fontColor, backColor);
		charOutBg(x + (FONT_CHAR_W * 3), y, hexTable[value & 15], fontColor, backColor);
	}
}

void printFiveHexBg(uint32_t x, uint32_t y, uint32_t value, uint32_t fontColor, uint32_t backColor)
{
	if (value == 0)
	{
		textOutBg(x, y, "00000", fontColor, backColor);
	}
	else
	{
		value &= 0xFFFFF;

		charOutBg(x + (FONT_CHAR_W * 0), y, hexTable[value >> 16], fontColor, backColor);
		charOutBg(x + (FONT_CHAR_W * 1), y, hexTable[(value & (15 << 12)) >> 12], fontColor, backColor);
		charOutBg(x + (FONT_CHAR_W * 2), y, hexTable[(value & (15 << 8)) >> 8], fontColor, backColor);
		charOutBg(x + (FONT_CHAR_W * 3), y, hexTable[(value & (15 << 4)) >> 4], fontColor, backColor);
		charOutBg(x + (FONT_CHAR_W * 4), y, hexTable[value & 15], fontColor, backColor);
	}
}

void setPrevStatusMessage(void)
{
	strcpy(ui.statusMessage, ui.prevStatusMessage);
	ui.updateStatusText = true;
}

void setStatusMessage(const char *msg, bool carry)
{
	assert(msg != NULL);

	if (carry)
		strcpy(ui.prevStatusMessage, msg);

	strcpy(ui.statusMessage, msg);
	ui.updateStatusText = true;
}

void displayMsg(const char *msg)
{
	assert(msg != NULL);

	editor.errorMsgActive = true;
	editor.errorMsgBlock = false;
	editor.errorMsgCounter = 0;

	if (*msg != '\0')
		setStatusMessage(msg, NO_CARRY);
}

void displayErrorMsg(const char *msg)
{
	assert(msg != NULL);

	editor.errorMsgActive = true;
	editor.errorMsgBlock = true;
	editor.errorMsgCounter = 0;

	if (*msg != '\0')
		setStatusMessage(msg, NO_CARRY);

	setErrPointer();
}
