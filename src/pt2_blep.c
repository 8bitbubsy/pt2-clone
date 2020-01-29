// These BLEP routines were coded by aciddose/adejr

#include <stdint.h>
#include "pt2_blep.h"
#include "pt2_helpers.h"

/* Why this table is not represented as readable floating-point numbers:
** Accurate float (double) representation in string format requires at least 14 digits and normalized
** (scientific) notation, notwithstanding compiler issues with precision or rounding error.
** Also, don't touch this table ever, just keep it exactly identical! */

// TODO: get a proper double-precision table. This one is converted from float.
static const uint64_t dBlepData[48] =
{
	0x3FEFFC3E20000000, 0x3FEFFAA900000000, 0x3FEFFAD460000000, 0x3FEFFA9C60000000,
	0x3FEFF5B0A0000000, 0x3FEFE42A40000000, 0x3FEFB7F5C0000000, 0x3FEF599BE0000000,
	0x3FEEA5E3C0000000, 0x3FED6E7080000000, 0x3FEB7F7960000000, 0x3FE8AB9E40000000,
	0x3FE4DCA480000000, 0x3FE0251880000000, 0x3FD598FB80000000, 0x3FC53D0D60000000,
	0x3F8383A520000000, 0xBFBC977CC0000000, 0xBFC755C080000000, 0xBFC91BDBA0000000,
	0xBFC455AFC0000000, 0xBFB6461340000000, 0xBF7056C400000000, 0x3FB1028220000000,
	0x3FBB5B7E60000000, 0x3FBC5903A0000000, 0x3FB55403E0000000, 0x3FA3CED340000000,
	0xBF7822DAE0000000, 0xBFA2805D00000000, 0xBFA7140D20000000, 0xBFA18A7760000000,
	0xBF87FF7180000000, 0x3F88CBFA40000000, 0x3F9D4AEC80000000, 0x3FA14A3AC0000000,
	0x3F9D5C5AA0000000, 0x3F92558B40000000, 0x3F7C997EE0000000, 0x0000000000000000,
	0x0000000000000000, 0x0000000000000000, 0x0000000000000000, 0x0000000000000000,
	0x0000000000000000, 0x0000000000000000, 0x0000000000000000, 0x0000000000000000
};

void blepAdd(blep_t *b, double dOffset, double dAmplitude)
{
	int8_t n;
	int32_t i;
	const double *dBlepSrc;
	double f;

	assert(dOffset >= 0.0 && dOffset < 1.0);

	f = dOffset * BLEP_SP;

	i = (int32_t)f; // get integer part of f
	dBlepSrc = (const double *)dBlepData + i + BLEP_OS;
	f -= i; // remove integer part from f

	i = b->index;

	n = BLEP_NS;
	while (n--)
	{
		b->dBuffer[i] += dAmplitude * LERP(dBlepSrc[0], dBlepSrc[1], f);
		i = (i + 1) & BLEP_RNS;
		dBlepSrc += BLEP_SP;
	}

	b->samplesLeft = BLEP_NS;
}

/* 8bitbubsy: simplified, faster version of blepAdd for blep'ing voice volume.
** Result is identical! (confirmed with binary comparison)
*/
void blepVolAdd(blep_t *b, double dAmplitude)
{
	int8_t n;
	int32_t i;
	const double *dBlepSrc;

	dBlepSrc = (const double *)dBlepData + BLEP_OS;

	i = b->index;

	n = BLEP_NS;
	while (n--)
	{
		b->dBuffer[i] += dAmplitude * (*dBlepSrc);
		i = (i + 1) & BLEP_RNS;
		dBlepSrc += BLEP_SP;
	}

	b->samplesLeft = BLEP_NS;
}

double blepRun(blep_t *b)
{
	double fBlepOutput;

	fBlepOutput = b->dBuffer[b->index];
	b->dBuffer[b->index] = 0.0;

	b->index = (b->index + 1) & BLEP_RNS;

	b->samplesLeft--;
	return fBlepOutput;
}
