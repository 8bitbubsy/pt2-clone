#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define OUTPUT_PAD_SIZE 20
#define RLE_ID 0xCC // DO NOT change this!

static char input[4096 + 1];
static uint8_t *ToPtr, *DataPtr, *a0, *a1;
static int32_t DataLen, CompLen, d0, d1, d4, d7;

int main(int argc, char *argv[])
{
	char bitmap_magic[2], *out_filename;
	uint8_t p8_1, p8_2, p8_3, p8_4;
	int32_t bitmap_width, bitmap_height, bitmap_depth, bitmap_offset;
	int32_t x, y, i, oy, t;
	uint32_t *pixel_data, p32_1, p32_2, p32_3, p32_4;
	FILE *in, *out;

#ifndef _DEBUG
	if (argc != 2)
	{
		printf("Usage: bmp2pth <bitmap.bmp>\n");
		return -1;
	}

	strcpy(input, argv[1]);
#else
	strcpy(input, "debug.bmp");
#endif

	in = fopen(input, "rb");
	if (in == NULL)
	{
		printf("ERROR: Could not open input bitmap!\n");
		system("PAUSE");
		return 1;
	}

	out_filename = (char *)malloc(strlen(input) + 3);
	if (out_filename == NULL)
	{
		printf("ERROR: Out of memory!\n");
		fclose(in);
		system("PAUSE");
		return 1;
	}

	sprintf(out_filename, "%s.c", input);

	out = fopen(out_filename, "w");
	if (out == NULL)
	{
		printf("ERROR: Could not open output bitmap!\n");
		free(out_filename);
		fclose(in);
		system("PAUSE");
		return 1;
	}

	fread(bitmap_magic, 1, 2, in);
	if (strncmp(bitmap_magic, "BM", 2) != 0)
	{
		printf("ERROR: Input is not a valid Windows bitmap!\n");
		fclose(in);
		fclose(out);
		system("PAUSE");
		return 1;
	}

	fseek(in, 0x12, SEEK_SET);
	fread(&bitmap_width, 4, 1, in);
	fread(&bitmap_height, 4, 1, in);
	fseek(in, 0x1C, SEEK_SET);
	fread(&bitmap_depth, 4, 1, in);
	bitmap_depth &= 0xFF;
	fseek(in, 0x0A, SEEK_SET);
	fread(&bitmap_offset, 4, 1, in);
	fseek(in, bitmap_offset, SEEK_SET);

	if (bitmap_depth != 16 && bitmap_depth != 24 && bitmap_depth != 32)
	{
		printf("ERROR: Bitmap is not truecolor bitmap (16, 24, 32)\n");

		fclose(in);
		fclose(out);
		system("PAUSE");

		return 1;
	}

	pixel_data = (uint32_t *)malloc(bitmap_width * bitmap_height * (bitmap_depth / 8));
	if (pixel_data == NULL)
	{
		printf("ERROR: Out of memory!\n");

		fclose(in);
		fclose(out);
		system("PAUSE");

		return 1;
	}

	if (fread(pixel_data, bitmap_depth / 8, bitmap_width * bitmap_height, in) != (size_t)(bitmap_width * bitmap_height))
	{
		printf("ERROR: Could not copy bitmap data into memory!\n");

		fclose(in);
		fclose(out);
		free(pixel_data);

		system("PAUSE");

		return 1;
	}

	// mirror pixel data so we get the real deal
	for (y = 0; y < bitmap_height/2; y++)
	{
		for (x = 0; x < bitmap_width; x++)
		{
			oy = bitmap_height - 1 - y;
			t  = pixel_data[y * bitmap_width + x];

			pixel_data[y * bitmap_width + x]  = pixel_data[oy * bitmap_width + x];
			pixel_data[oy * bitmap_width + x] = t;
		}
	}

	DataLen = (bitmap_width * bitmap_height) / 4;
	DataPtr = (uint8_t *)malloc(DataLen);

	for (i = 0; i < DataLen; i++)
	{
		p32_1 = pixel_data[(i * 4) + 0];
		p32_2 = pixel_data[(i * 4) + 1];
		p32_3 = pixel_data[(i * 4) + 2];
		p32_4 = pixel_data[(i * 4) + 3];

		     if (p32_1 == 0x000000) p8_1 = 0;
		else if (p32_1 == 0xBBBBBB) p8_1 = 1;
		else if (p32_1 == 0x888888) p8_1 = 2;
		else if (p32_1 == 0x555555) p8_1 = 3;
		else
		{
			printf("ERROR: Illegal pixel 0x%08X found. Stopping!\n", p32_1);
			free(pixel_data);
			free(DataPtr);
			system("PAUSE");
			return 1;
		}

		     if (p32_2 == 0x000000) p8_2 = 0;
		else if (p32_2 == 0xBBBBBB) p8_2 = 1;
		else if (p32_2 == 0x888888) p8_2 = 2;
		else if (p32_2 == 0x555555) p8_2 = 3;
		else
		{
			printf("ERROR: Illegal pixel 0x%08X found. Stopping!\n", p32_2);
			free(pixel_data);
			free(DataPtr);
			fclose(out);
			system("PAUSE");
			return (1);
		}

		     if (p32_3 == 0x000000) p8_3 = 0;
		else if (p32_3 == 0xBBBBBB) p8_3 = 1;
		else if (p32_3 == 0x888888) p8_3 = 2;
		else if (p32_3 == 0x555555) p8_3 = 3;
		else
		{
			printf("ERROR: Illegal pixel 0x%08X found. Stopping!\n", p32_3);
			free(pixel_data);
			free(DataPtr);
			fclose(out);
			system("PAUSE");
			return 1;
		}

		     if (p32_4 == 0x000000) p8_4 = 0;
		else if (p32_4 == 0xBBBBBB) p8_4 = 1;
		else if (p32_4 == 0x888888) p8_4 = 2;
		else if (p32_4 == 0x555555) p8_4 = 3;
		else
		{
			printf("ERROR: Illegal pixel 0x%08X found. Stopping!\n", p32_4);
			free(pixel_data);
			free(DataPtr);
			fclose(out);
			system("PAUSE");
			return 1;
		}

		DataPtr[i] = (p8_1 << 6) | (p8_2 << 4) | (p8_3 << 2) | p8_4;
	}

	free(pixel_data);

	// The following mess is a direct 68k asm of ptcompactor.s found in
	// the ProTracker 1.3 source code archive.
	goto Main;

JustCode:
	*a1++ = RLE_ID; // Output compacter code
	*a1++ = 0;      // Output zero
	*a1++ = RLE_ID; // Output compacter code
	goto NextByte;  // Do next byte

Equal:
	d1 = d0;
	d4++;                // Add one to equal-count
	if (d4 >= 255)       // 255 or more?
		goto FlushBytes; // Yes, flush buffer
	goto NextByte;       // Do next byte

FlushBytes:
	if (d4 >= 3)         // 4 or more
		goto FourOrMore; // Yes, output codes
NotFour:
	*a1++ = (uint8_t)d1;          // Output byte
	if (--d4 != -1) goto NotFour; // Loop...
	d4 = 0;        // Zero count
	goto NextByte; // Another byte
FourOrMore:
	*a1++ = RLE_ID;      // Output compacter code
	*a1++ = (uint8_t)d4; // Output count
	*a1++ = (uint8_t)d1; // Output byte
	d4 = 0;              // Zero count
	d0++;
	goto NextByte; // Do next byte

Main:
	ToPtr = (uint8_t *)malloc(DataLen);
	a0 = DataPtr; // From ptr.
	a1 = ToPtr;   // To ptr.
	d7 = DataLen; // Length
	d4 = 0;       // Clear count

EqLoop:
	d0 = *a0++;        // Get a byte
	if (d0 == RLE_ID)  // Same as compacter code?
		goto JustCode; // Output JustCode

	if (d7 == 1)
		goto endskip;

	if (d0 == *a0)  // Same as previous byte?
		goto Equal; // Yes, it was equal
endskip:
	if (d4 > 0)          // Not equal, any equal buffered?
		goto FlushBytes; // Yes, output them
	*a1++ = (uint8_t)d0; // Output byte
	d4 = 0;
NextByte:
	d7--;                    // Subtract 1 from length
	if (d7 > 0) goto EqLoop; // Loop until length = 0

	if (d4 == 0)    // Any buffered bytes?
		goto endok; // No, goto end

	if (d4 >= 3)          // More than 4?
		goto FourOrMore2; // Yes, skip
NotFour2:
	*a1++ = (uint8_t)d0;           // Output byte
	if (--d4 != -1) goto NotFour2; // Loop...
	goto endok;                    // Goto end;
FourOrMore2:
	*a1++ = RLE_ID;      // Output compacter code
	*a1++ = (uint8_t)d4; // Output count
	*a1++ = (uint8_t)d0; // Output byte
endok:
	free(DataPtr);

	CompLen = (uint32_t)a1 - (uint32_t)ToPtr;

	fprintf(out, "#include <stdint.h>\n\n");

	fprintf(out, "// Final unpack length: %d\n", DataLen * 4);
	fprintf(out, "// Decoded length: %d (first four bytes of buffer)\n", DataLen);
	fprintf(out, "const uint8_t renameMe[%d] =\n{\n", CompLen + 4);
	fprintf(out, "\t0x%02X,0x%02X,0x%02X,0x%02X,",
		(DataLen & 0xFF000000) >> 24, (DataLen & 0x00FF0000) >> 16,
		(DataLen & 0x0000FF00) >> 8, DataLen & 0x000000FF);

	for (i = 4; i < CompLen+4; i++)
	{
		     if (i == CompLen+3) fprintf(out, "0x%02X\n};\n", ToPtr[i-4]);
		else if ((i % OUTPUT_PAD_SIZE) == 0) fprintf(out, "\t0x%02X,", ToPtr[i-4]);
		else if ((i % OUTPUT_PAD_SIZE) == OUTPUT_PAD_SIZE-1) fprintf(out, "0x%02X,\n", ToPtr[i-4]);
		else fprintf(out, "0x%02X,", ToPtr[i-4]);
	}

	free(ToPtr);
	fclose(out);

	printf("Done successfully.\n");
	return 0;
}
