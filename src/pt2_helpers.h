#pragma once

#include <stdint.h>
#include <stdbool.h>

#define BSWAP16(value) \
( \
	(((uint16_t)((value) & 0x00FF)) << 8) | \
	(((uint16_t)((value) & 0xFF00)) >> 8)   \
)

#define BSWAP32(value) \
( \
	(((uint32_t)((value) & 0x000000FF)) << 24) | \
	(((uint32_t)((value) & 0x0000FF00)) <<  8) | \
	(((uint32_t)((value) & 0x00FF0000)) >>  8) | \
	(((uint32_t)((value) & 0xFF000000)) >> 24)   \
)

#define BSWAP64(x) \
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

/* Byte order.  SWAP16/32/64 are used throughout for BIG-endian file data
** (MOD, IFF, AIFF, XPK, PT.Config) on a little-endian host; LE16/32 are for
** LITTLE-endian file data (WAV, FLAC chunks, IDs read with fread).  On a
** big-endian host (AMIX on the 68060) the roles swap. */
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#define PT2_BIG_ENDIAN 1
#define SWAP16(value) ((uint16_t)(value))
#define SWAP32(value) ((uint32_t)(value))
#define SWAP64(value) ((uint64_t)(value))
#define LE16(value) BSWAP16(value)
#define LE32(value) BSWAP32(value)
#define LE64(value) BSWAP64((uint64_t)(value))
#else
#define SWAP16(value) BSWAP16(value)
#define SWAP32(value) BSWAP32(value)
#define SWAP64(value) BSWAP64(value)
#define LE16(value) ((uint16_t)(value))
#define LE32(value) ((uint32_t)(value))
#define LE64(value) ((uint64_t)(value))
#endif

// convert arrays of little-endian words in place (no-ops on little-endian hosts)
static inline void le16Array(void *p, uint32_t n)
{
#ifdef PT2_BIG_ENDIAN
	uint16_t *w = (uint16_t *)p;
	for (uint32_t i = 0; i < n; i++) w[i] = LE16(w[i]);
#else
	(void)p; (void)n;
#endif
}

static inline void le32Array(void *p, uint32_t n)
{
#ifdef PT2_BIG_ENDIAN
	uint32_t *w = (uint32_t *)p;
	for (uint32_t i = 0; i < n; i++) w[i] = LE32(w[i]);
#else
	(void)p; (void)n;
#endif
}

static inline void le64Array(void *p, uint32_t n)
{
#ifdef PT2_BIG_ENDIAN
	uint64_t *w = (uint64_t *)p;
	for (uint32_t i = 0; i < n; i++) w[i] = LE64(w[i]);
#else
	(void)p; (void)n;
#endif
}

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
