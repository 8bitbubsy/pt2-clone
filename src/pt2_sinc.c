/* These routines are heavily based upon code from 
** the OpenMPT project (Tables.cpp), which has a
** similar license.
**
** This code is not very readable, as I tried to
** make it as optimized as I could. The reason I don't
** make one big pre-calculated table is because I want
** *many* taps while preserving fractional precision.
**
** There might also be some errors in how I wrote this,
** but so far it sounds okay to my ears.
** Let me know if I did some crucials mistakes here!
*/

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <assert.h>
#include <math.h>
#include "pt2_sinc.h"

#define CENTER_TAP ((SINC_TAPS / 2) - 1)

static double *dWindowLUT, dKPi;

// Compute Bessel function Izero(y) using a series approximation
static double Izero(double y)
{
	double s = 1.0, ds = 1.0, d = 0.0;

	do
	{
		d = d + 2.0;
		ds = ds * (y * y) / (d * d);
		s = s + ds;
	}
	while (ds > 1E-7 * s);

	return s;
}

bool initSinc(double dCutoff) // dCutoff = 0.0 .. 0.999
{
	assert(SINC_TAPS > 0);
	if (dCutoff > 0.999)
		dCutoff = 0.999;

	const double dBeta = 9.6377;
	
	dKPi = M_PI * dCutoff;

	// generate window table

	dWindowLUT = (double *)malloc(SINC_TAPS * sizeof (double));
	if (dWindowLUT == NULL)
		return false;

	const double dMul1 = 1.0 / ((SINC_TAPS/2) * (SINC_TAPS/2));
	const double dMul2 = (1.0 / Izero(dBeta)) * dCutoff;

	double dX = CENTER_TAP;
	for (int32_t i = 0; i < SINC_TAPS; i++)
	{
		dWindowLUT[i] = Izero(dBeta * sqrt(1.0 - dX * dX * dMul1)) * dMul2; // Kaiser window
		dX -= 1.0;
	}

	return true;
}

void freeSinc(void)
{
	if (dWindowLUT != NULL)
	{
		free(dWindowLUT);
		dWindowLUT = NULL;
	}
}

double sinc(int16_t *smpPtr16, double dFrac)
{
	double dSmp = 0.0;
	double dX = (CENTER_TAP + dFrac) * dKPi;

	for (int32_t i = 0; i < SINC_TAPS; i++)
	{
		const double dSinc = (sin(dX) / dX) * dWindowLUT[i]; // if only I could replace this div with a mul...
		dSmp += smpPtr16[i] * dSinc;
		dX -= dKPi;
	}

	dSmp *= 1.0 / (SINC_TAPS / 2); // normalize (XXX: This is probably not how to do it?)

	return dSmp;
}
