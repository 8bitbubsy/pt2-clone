#pragma once

typedef struct ledFilter_t
{
	double LIn1, LIn2, LOut1, LOut2;
	double RIn1, RIn2, ROut1, ROut2;
	double a1, a2, a3, b1, b2;
} ledFilter_t;

void clearLEDFilterState(ledFilter_t *f);
void calcLEDFilterCoeffs(double sr, double hz, double qfactor, ledFilter_t *filter);
void LEDFilter(ledFilter_t *f, const double *in, double *out);
