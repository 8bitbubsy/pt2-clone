// for finding memory leaks in debug mode with Visual Studio 
#if defined _DEBUG && defined _MSC_VER
#include <crtdbg.h>
#endif

#include <stdint.h>
#include <stdbool.h>
#include "pt2_palette.h"
#include "pt2_structs.h"
#include "pt2_helpers.h"
#include "pt2_bmp.h"
#include "pt2_tables.h"

uint32_t *aboutScreenBMP   = NULL, *diskOpScreenBMP  = NULL, *editOpModeCharsBMP = NULL;
uint32_t *editOpScreen1BMP = NULL, *editOpScreen2BMP = NULL, *samplerVolumeBMP   = NULL;
uint32_t *editOpScreen3BMP = NULL, *editOpScreen4BMP = NULL, *spectrumVisualsBMP = NULL;
uint32_t *muteButtonsBMP   = NULL, *posEdBMP         = NULL, *samplerFiltersBMP  = NULL;
uint32_t *samplerScreenBMP = NULL, *trackerFrameBMP  = NULL, *sampleMonitorBMP   = NULL;
uint32_t *samplingBoxBMP   = NULL;

// fix-bitmaps for 128K sample mode
uint32_t *fix128KTrackerBMP = NULL;
uint32_t *fix128KPosBMP = NULL;
uint32_t *fix128KChordBMP = NULL;

void createBitmaps(void)
{
	uint8_t r8, g8, b8, r8_2, g8_2, b8_2;

	uint32_t pixel24 = video.palette[PAL_PATCURSOR];
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

			for (int32_t x = 0; x < 11; x++)
				patternCursorBMP[(y * 11) + x] = RGB24(r8, g8, b8);
		}

		// sides (same color)
		if (y >= 2 && y <= 12)
		{
			patternCursorBMP[(y * 11) + 0] = pixel24;

			for (int32_t x = 1; x < 10; x++)
				patternCursorBMP[(y * 11) + x] = video.palette[PAL_COLORKEY];

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

			for (int32_t x = 0; x < 11; x++)
				patternCursorBMP[(y * 11) + x] = RGB24(r8, g8, b8);
		}
	}

	// create spectrum analyzer bar graphics
	for (int32_t i = 0; i < 36; i++)
		analyzerColorsRGB24[i] = RGB12_to_RGB24(analyzerColors[35-i]);

	// create VU-Meter bar graphics
	for (int32_t i = 0; i < 48; i++)
	{
		uint16_t pixel12 = vuMeterColors[47-i];

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

void freeBMPs(void)
{
	if (fix128KChordBMP != NULL) free(fix128KChordBMP);
	if (fix128KPosBMP != NULL) free(fix128KPosBMP);
	if (fix128KTrackerBMP != NULL) free(fix128KTrackerBMP);
	if (trackerFrameBMP != NULL) free(trackerFrameBMP);
	if (samplerScreenBMP != NULL) free(samplerScreenBMP);
	if (samplerVolumeBMP != NULL) free(samplerVolumeBMP);
	if (samplerFiltersBMP != NULL) free(samplerFiltersBMP);
	if (diskOpScreenBMP != NULL) free(diskOpScreenBMP);
	if (posEdBMP != NULL) free(posEdBMP);
	if (spectrumVisualsBMP != NULL) free(spectrumVisualsBMP);
	if (editOpScreen1BMP != NULL) free(editOpScreen1BMP);
	if (editOpScreen2BMP != NULL) free(editOpScreen2BMP);
	if (editOpScreen3BMP != NULL) free(editOpScreen3BMP);
	if (editOpScreen4BMP != NULL) free(editOpScreen4BMP);
	if (aboutScreenBMP != NULL) free(aboutScreenBMP);
	if (muteButtonsBMP != NULL) free(muteButtonsBMP);
	if (editOpModeCharsBMP != NULL) free(editOpModeCharsBMP);
	if (sampleMonitorBMP != NULL) free(sampleMonitorBMP);
	if (samplingBoxBMP != NULL) free(samplingBoxBMP);
}

uint32_t *unpackBMP(const uint8_t *src, uint32_t packedLen)
{
	// RLE decode

	int32_t decodedLength = (src[0] << 24) | (src[1] << 16) | (src[2] << 8) | src[3];

	// 2-bit to 8-bit conversion
	uint32_t *dst = (uint32_t *)malloc(((decodedLength * 4) * sizeof (int32_t)) + 8);
	if (dst == NULL)
		return NULL;

	uint8_t *tmpBuffer = (uint8_t *)malloc(decodedLength + 128); // some margin is needed, the packer is buggy
	if (tmpBuffer == NULL)
	{
		free(dst);
		return NULL;
	}

	const uint8_t *packSrc = src + 4; // skip "length" field
	uint8_t *packDst = tmpBuffer;

	int32_t i = packedLen - 4; // subtract "length" field
	while (i > 0)
	{
		uint8_t byteIn = *packSrc++;
		if (byteIn == 0xCC) // compactor code
		{
			int16_t count  = *packSrc++;
			byteIn = *packSrc++;

			while (count-- >= 0)
				*packDst++ = byteIn;

			i -= 2;
		}
		else
		{
			*packDst++ = byteIn;
		}

		i--;
	}

	for (i = 0; i < decodedLength; i++)
	{
		const uint8_t byte1 = (tmpBuffer[i] & 0xC0) >> 6;
		assert(byte1 < PALETTE_NUM);
		dst[(i << 2) + 0] = video.palette[byte1];

		const uint8_t byte2 = (tmpBuffer[i] & 0x30) >> 4;
		assert(byte2 < PALETTE_NUM);
		dst[(i << 2) + 1] = video.palette[byte2];

		const uint8_t byte3 = (tmpBuffer[i] & 0x0C) >> 2;
		assert(byte3 < PALETTE_NUM);
		dst[(i << 2) + 2] = video.palette[byte3];

		const uint8_t byte4 = (tmpBuffer[i] & 0x03) >> 0;
		assert(byte4 < PALETTE_NUM);
		dst[(i << 2) + 3] = video.palette[byte4];
	}

	free(tmpBuffer);
	return dst;
}

bool unpackBMPs(void)
{
	fix128KChordBMP = unpackBMP(fix128KChordPackedBMP, sizeof (fix128KChordPackedBMP));
	fix128KPosBMP = unpackBMP(fix128KPosPackedBMP, sizeof (fix128KPosPackedBMP));
	fix128KTrackerBMP = unpackBMP(tracker128KFixPackedBMP, sizeof (tracker128KFixPackedBMP));
	trackerFrameBMP = unpackBMP(trackerFramePackedBMP, sizeof (trackerFramePackedBMP));
	samplerScreenBMP = unpackBMP(samplerScreenPackedBMP, sizeof (samplerScreenPackedBMP));
	samplerVolumeBMP = unpackBMP(samplerVolumePackedBMP, sizeof (samplerVolumePackedBMP));
	samplerFiltersBMP = unpackBMP(samplerFiltersPackedBMP, sizeof (samplerFiltersPackedBMP));
	diskOpScreenBMP = unpackBMP(diskOpScreenPackedBMP, sizeof (diskOpScreenPackedBMP));
	posEdBMP = unpackBMP(posEdPackedBMP, sizeof (posEdPackedBMP));
	spectrumVisualsBMP = unpackBMP(spectrumVisualsPackedBMP, sizeof (spectrumVisualsPackedBMP));
	editOpScreen1BMP = unpackBMP(editOpScreen1PackedBMP, sizeof (editOpScreen1PackedBMP));
	editOpScreen2BMP = unpackBMP(editOpScreen2PackedBMP, sizeof (editOpScreen2PackedBMP));
	editOpScreen3BMP = unpackBMP(editOpScreen3PackedBMP, sizeof (editOpScreen3PackedBMP));
	editOpScreen4BMP = unpackBMP(editOpScreen4PackedBMP, sizeof (editOpScreen4PackedBMP));
	aboutScreenBMP = unpackBMP(aboutScreenPackedBMP, sizeof (aboutScreenPackedBMP));
	muteButtonsBMP = unpackBMP(muteButtonsPackedBMP, sizeof (muteButtonsPackedBMP));
	editOpModeCharsBMP = unpackBMP(editOpModeCharsPackedBMP, sizeof (editOpModeCharsPackedBMP));
	sampleMonitorBMP = unpackBMP(sampleMonitorPackedBMP, sizeof (sampleMonitorPackedBMP));
	samplingBoxBMP = unpackBMP(samplingBoxPackedBMP, sizeof (samplingBoxPackedBMP));

	if (fix128KTrackerBMP  == NULL || fix128KPosBMP     == NULL || fix128KChordBMP    == NULL ||
		trackerFrameBMP    == NULL || samplerScreenBMP  == NULL || samplerVolumeBMP   == NULL ||
		diskOpScreenBMP    == NULL || posEdBMP          == NULL || spectrumVisualsBMP == NULL ||
		editOpScreen1BMP   == NULL || editOpScreen2BMP  == NULL || editOpScreen3BMP   == NULL ||
		editOpScreen4BMP   == NULL || aboutScreenBMP    == NULL || muteButtonsBMP     == NULL ||
		editOpModeCharsBMP == NULL || samplerFiltersBMP == NULL || sampleMonitorBMP   == NULL ||
		samplingBoxBMP     == NULL)
	{
		showErrorMsgBox("Out of memory!");
		return false; // BMPs are free'd in cleanUp()
	}

	createBitmaps();
	return true;
}
