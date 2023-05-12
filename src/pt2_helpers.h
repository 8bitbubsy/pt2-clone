#pragma once

#include <stdint.h>
#include <stdbool.h>

// fast 32-bit -> 16-bit clamp
#define CLAMP16(i) if ((int16_t)(i) != i) i = 0x7FFF ^ (i >> 31)

// fast 16-bit -> 8-bit clamp
#define CLAMP8(i) if ((int8_t)(i) != i) i = 0x7F ^ (i >> 15)

#define SWAP16(value) \
( \
	(((uint16_t)((value) & 0x00FF)) << 8) | \
	(((uint16_t)((value) & 0xFF00)) >> 8)   \
)

#define SWAP32(value) \
( \
	(((uint32_t)((value) & 0x000000FF)) << 24) | \
	(((uint32_t)((value) & 0x0000FF00)) <<  8) | \
	(((uint32_t)((value) & 0x00FF0000)) >>  8) | \
	(((uint32_t)((value) & 0xFF000000)) >> 24)   \
)

#define SWAP64(x) \
( \
	(((x) << 56) & 0xFF00000000000000ULL) | \
	(((x) << 40) & 0x00FF000000000000ULL) | \
	(((x) << 24) & 0x0000FF0000000000ULL) | \
	(((x) <<  8) & 0x000000FF00000000ULL) | \
	(((x) >>  8) & 0x00000000FF000000ULL) | \
	(((x) >> 24) & 0x0000000000FF0000ULL) | \
	(((x) >> 40) & 0x000000000000FF00ULL) | \
	(((x) >> 56) & 0x00000000000000FFULL)  \
)

#define SGN(x) (((x) >= 0) ? 1 : -1)
#define ABS(a) (((a) < 0) ? -(a) : (a))
#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#define MAX(a, b) (((a) > (b)) ? (a) : (b))
#define CLAMP(x, low, high) (((x) > (high)) ? (high) : (((x) < (low)) ? (low) : (x)))

#define R12(x) (((x) >> 8) & 0xF)
#define G12(x) (((x) >> 4) & 0xF)
#define B12(x) ((x) & 0xF)

// 0..15 -> 0..255
#define R12_to_R24(x) (R12(x) * 17)
#define G12_to_G24(x) (G12(x) * 17)
#define B12_to_B24(x) (B12(x) * 17)

#define RGB12_to_RGB24(x) ((R12_to_R24(x) << 16) | (G12_to_G24(x) << 8) | (B12_to_B24(x)))

#define R24(x) (((x) >> 16) & 0xFF)
#define G24(x) (((x) >> 8) & 0xFF)
#define B24(x) ((x) & 0xFF)

#define RGB24(r, g, b) (((r) << 16) | ((g) << 8) | (b))

void showErrorMsgBox(const char *fmt, ...);

void sanitizeFilenameChar(char *chr);
bool sampleNameIsEmpty(char *name);
bool moduleNameIsEmpty(char *name);
void updateWindowTitle(bool modified);
