#include <stdio.h>
#include <string.h>
#include "gui.h"
#include "palette.h"
#include "tinyfiledialogs/tinyfiledialogs.h"

// 12-bit RGB values
uint16_t palette[8];
uint16_t vuColors[48];
uint16_t analyzerColors[36];

// 12-bit RGB values
static const uint16_t originalPalette[8] =
{
	0x000, // 00- PAL_BACKGRD
	0xBBB, // 01- PAL_BORDER
	0x888, // 02- PAL_GENBKG
	0x555, // 03- PAL_GENBKG2
	0xFD0, // 04- PAL_QADSCP
	0xD04, // 05- PAL_PATCURSOR
	0x000, // 06- PAL_GENTXT
	0x34F  // 07- PAL_PATTXT
};

// 12-bit RGB values
static const uint16_t vuOriginalColors[48] =
{
	0xF00, 0xF00, 0xF10, 0xF10, 0xF20, 0xF20, 0xF30, 0xF30,
	0xF40, 0xF50, 0xF60, 0xF70, 0xF80, 0xF90, 0xFA0, 0xFB0,
	0xFC0, 0xFD0, 0xFE0, 0xFF0, 0xFF0, 0xEF0, 0xEF0, 0xDF0,
	0xDF0, 0xCF0, 0xCF0, 0xBF0, 0xBF0, 0xAF0, 0x9F0, 0x9F0,
	0x8F0, 0x8F0, 0x7F0, 0x7F0, 0x6F0, 0x6F0, 0x5F0, 0x5F0,
	0x4F0, 0x4F0, 0x3F0, 0x3F0, 0x2F0, 0x1F0, 0x0F0, 0x0E0
};

// 12-bit RGB values
static const uint16_t analyzerOriginalColors[48] =
{
	0xF00, 0xF10, 0xF20, 0xF30, 0xF40, 0xF50, 0xF60, 0xF70,
	0xF80, 0xF90, 0xFA0, 0xFB0, 0xFC0, 0xFD0, 0xFE0, 0xFF0,
	0xEF0, 0xDF0, 0xCF0, 0xBF0, 0xAF0, 0x9F0, 0x8F0, 0x7F0,
	0x6F0, 0x5F0, 0x4F0, 0x3F0, 0x2F0, 0x1F0, 0x0F0, 0x0E0,
	0x0D0, 0x0C0, 0x0B0, 0x0A0
};

static uint16_t undo1Col, undo1ColIndex, can1Cols[8];
static uint16_t undo2Col, undo2ColIndex, can2ColsVu[48], can2ColsAna[36];

// these will be generated in realtime based on vuColors
uint32_t spectrumAnalyzerBMP[36], vuMeterBMP[480], patternCursorBMP[154];

void fillCancel1Colors(void)
{
	memcpy(can1Cols, palette, sizeof (palette));
}

void cancel1Color(void)
{
	memcpy(palette, can1Cols, sizeof (palette));
}

void setUndo1Color(uint8_t paletteIndex)
{
	undo1ColIndex = paletteIndex;
	undo1Col = palette[undo1ColIndex];
}

void undo1Color(void)
{
	uint16_t oldColor = palette[undo1ColIndex];
	palette[undo1ColIndex] = undo1Col;
	undo1Col = oldColor;
}

void fillCancel2Colors(void)
{
	memcpy(can2ColsVu, vuColors, sizeof (vuColors));
	memcpy(can2ColsAna, analyzerColors, sizeof (analyzerColors));
}

void cancel2Color(void)
{
	if (theRightColors == vuColors)
		memcpy(vuColors, can2ColsVu, sizeof (vuColors));
	else
		memcpy(analyzerColors, can2ColsAna, sizeof (analyzerColors));
}

void setUndo2Color(uint8_t colorIndex)
{
	undo2ColIndex = colorIndex;
	undo2Col = theRightColors[colorIndex];
}

void undo2Color(void)
{
	uint16_t oldColor = theRightColors[undo2ColIndex];
	theRightColors[undo2ColIndex] = undo2Col;
	undo2Col = oldColor;
}

void setDefaultPalette(void)
{
	memcpy(palette, originalPalette, sizeof (palette));
}

void setDefaultVuColors(void)
{
	memcpy(vuColors, vuOriginalColors, sizeof (vuColors));
}

void setDefaultAnalyzerColors(void)
{
	memcpy(analyzerColors, analyzerOriginalColors, sizeof (analyzerColors));
}

uint16_t colorPicker(uint16_t inputColor)
{
	uint8_t aDefaultRGB[3], aoResultRGB[3];
	uint32_t pixel32, defaultCol24;

	defaultCol24 = RGB12_to_RGB24(inputColor);

	aDefaultRGB[0] = R24(defaultCol24);
	aDefaultRGB[1] = G24(defaultCol24);
	aDefaultRGB[2] = B24(defaultCol24);

	if (tinyfd_colorChooser("Pick a color", NULL, aDefaultRGB, aoResultRGB) == NULL)
		return 0xFFFF; // cancel/exit

	pixel32 = (aoResultRGB[0] << 16) | (aoResultRGB[1] << 8) | aoResultRGB[2];
	return RGB24_to_RGB12(pixel32);
}

void updatePatternCursorBMP(void)
{
	uint8_t r8, g8, b8;
	int32_t x;
	uint32_t pixel24;

	pixel24 = RGB12_to_RGB24(palette[PAL_PATCURSOR]);
	for (int32_t y = 0; y < 14; y++)
	{
		// top two rows have a lighter color
		if (y < 2)
		{
			r8 = R24(pixel24);
			g8 = G24(pixel24);
			b8 = B24(pixel24);

			if (r8 <= 0xFF-0x33)
				r8 += 0x33;
			else
				r8 = 0xFF;

			if (g8 <= 0xFF-0x33)
				g8 += 0x33;
			else
				g8 = 0xFF;

			if (b8 <= 0xFF-0x33)
				b8 += 0x33;
			else
				b8 = 0xFF;

			for (x = 0; x < 11; x++)
				patternCursorBMP[(y * 11) + x] = RGB24(r8, g8, b8);
		}

		// sides (same color)
		if (y >= 2 && y <= 12)
		{
			patternCursorBMP[(y * 11) + 0] = pixel24;

			for (x = 1; x < 10; x++)
				patternCursorBMP[(y * 11) + x] = RGB24_COLORKEY;

			patternCursorBMP[(y * 11) + 10] = pixel24;
		}

		// bottom two rows have a darker color
		if (y > 11)
		{
			r8 = R24(pixel24);
			g8 = G24(pixel24);
			b8 = B24(pixel24);

			if (r8 >= 0x33)
				r8 -= 0x33;
			else
				r8 = 0x00;

			if (g8 >= 0x33)
				g8 -= 0x33;
			else
				g8 = 0x00;

			if (b8 >= 0x33)
				b8 -= 0x33;
			else
				b8 = 0x00;

			for (x = 0; x < 11; x++)
				patternCursorBMP[(y * 11) + x] = RGB24(r8, g8, b8);
		}
	}
}

void updateSpectrumAnalyzerBMP(void)
{
	for (int32_t i = 0; i < 36; i++)
		spectrumAnalyzerBMP[i] = RGB12_to_RGB24(analyzerColors[35-i]);
}

void updateVuMeterBMP(void)
{
	uint8_t r8, g8, b8, r8_2, g8_2, b8_2;
	uint16_t pixel12;
	uint32_t pixel24;

	for (int32_t i = 0; i < 48; i++)
	{
		pixel12 = vuColors[47-i];

		r8_2 = r8 = R12_to_R24(pixel12);
		g8_2 = g8 = G12_to_G24(pixel12);
		b8_2 = b8 = B12_to_B24(pixel12);

		// brighter pixels on the left side

		if (r8_2 <= 0xFF-0x33)
			r8_2 += 0x33;
		else
			r8_2 = 0xFF;

		if (g8_2 <= 0xFF-0x33)
			g8_2 += 0x33;
		else
			g8_2 = 0xFF;

		if (b8_2 <= 0xFF-0x33)
			b8_2 += 0x33;
		else
			b8_2 = 0xFF;

		pixel24 = RGB24(r8_2, g8_2, b8_2);

		vuMeterBMP[(i * 10) + 0] = pixel24;
		vuMeterBMP[(i * 10) + 1] = pixel24;

		// main pixels
		for (int32_t j = 2; j < 8; j++)
			vuMeterBMP[(i * 10) + j] = RGB24(r8, g8, b8);

		// darker pixels on the right side
		r8_2 = r8;
		g8_2 = g8;
		b8_2 = b8;

		if (r8_2 >= 0x33)
			r8_2 -= 0x33;
		else
			r8_2 = 0x00;

		if (g8_2 >= 0x33)
			g8_2 -= 0x33;
		else
			g8_2 = 0x00;

		if (b8_2 >= 0x33)
			b8_2 -= 0x33;
		else
			b8_2 = 0x00;

		pixel24 = RGB24(r8_2, g8_2, b8_2);

		vuMeterBMP[(i * 10) + 8] = pixel24;
		vuMeterBMP[(i * 10) + 9] = pixel24;
	}
}

void updateBMPs(void)
{
	updatePatternCursorBMP();
	updateSpectrumAnalyzerBMP();
	updateVuMeterBMP();
}
