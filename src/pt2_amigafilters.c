/* Amiga 500 / Amiga 1200 filter implementation.
**
** Route:
** Paula output -> low-pass filter -> LED filter (if turned on) -> high-pass filter (centering of waveform)
*/

#include <stdint.h>
#include <stdbool.h>
#include "pt2_structs.h"
#include "pt2_audio.h"
#include "pt2_paula.h"
#include "pt2_rcfilter.h"
#include "pt2_math.h"
#include "pt2_textout.h"

typedef struct ledFilter_t
{
	double LIn1, LIn2, LOut1, LOut2;
	double RIn1, RIn2, ROut1, ROut2;
	double a1, a2, a3, b1, b2;
} ledFilter_t;

static int32_t filterModel;
static bool ledFilterEnabled, useA1200LowPassFilter;
static rcFilter_t filterLoA500, filterHiA500, filterLoA1200, filterHiA1200;
static ledFilter_t filterLED;

void (*processAmigaFilters)(double *, double *, int32_t); // globalized

static void processFiltersA1200_NoLED(double *dBufferL, double *dBufferR, int32_t numSamples);
static void processFiltersA1200_LED(double *dBufferL, double *dBufferR, int32_t numSamples);
static void processFiltersA500_NoLED(double *dBufferL, double *dBufferR, int32_t numSamples);
static void processFiltersA500_LED(double *dBufferL, double *dBufferR, int32_t numSamples);

// --------------------------------------------------------
// Crude LED filter implementation
// --------------------------------------------------------

void clearLEDFilterState(ledFilter_t *f)
{
	f->LIn1 = f->LIn2 = f->LOut1 = f->LOut2 = 0.0;
	f->RIn1 = f->RIn2 = f->ROut1 = f->ROut2 = 0.0;
}

static void calcLEDFilterCoeffs(double sr, double hz, double qfactor, ledFilter_t *filter)
{
	const double c = 1.0 / pt2_tan((PT2_PI * hz) / sr);
	const double r = 1.0 / qfactor;

	filter->a1 = 1.0 / (1.0 + r * c + c * c);
	filter->a2 = 2.0 * filter->a1;
	filter->a3 = filter->a1;
	filter->b1 = 2.0 * (1.0 - c*c) * filter->a1;
	filter->b2 = (1.0 - r * c + c * c) * filter->a1;
}

static void LEDFilter(ledFilter_t *f, const double *in, double *out)
{
	const double LOut = (f->a1 * in[0]) + (f->a2 * f->LIn1) + (f->a3 * f->LIn2) - (f->b1 * f->LOut1) - (f->b2 * f->LOut2);
	const double ROut = (f->a1 * in[1]) + (f->a2 * f->RIn1) + (f->a3 * f->RIn2) - (f->b1 * f->ROut1) - (f->b2 * f->ROut2);

	// shift states

	f->LIn2 = f->LIn1;
	f->LIn1 = in[0];
	f->LOut2 = f->LOut1;
	f->LOut1 = LOut;

	f->RIn2 = f->RIn1;
	f->RIn1 = in[1];
	f->ROut2 = f->ROut1;
	f->ROut1 = ROut;

	// set output
	out[0] = LOut;
	out[1] = ROut;
}

// --------------------------------------------------------
// --------------------------------------------------------

void setupAmigaFilters(double dAudioFreq)
{
	/* Amiga 500/1200 filter emulation
	**
	** aciddose:
	** First comes a static low-pass 6dB formed by the supply current
	** from the Paula's mixture of channels A+B / C+D into the opamp with
	** 0.1uF capacitor and 360 ohm resistor feedback in inverting mode biased by
	** dac vRef (used to center the output).
	**
	** R = 360 ohm
	** C = 0.1uF
	** Low Hz = 4420.97~ = 1 / (2pi * 360 * 0.0000001)
	**
	** Under spice simulation the circuit yields -3dB = 4400Hz.
	** In the Amiga 1200, the low-pass cutoff is ~34kHz, so the
	** static low-pass filter is disabled in the mixer in A1200 mode.
	**
	** Next comes a bog-standard Sallen-Key filter ("LED") with:
	** R1 = 10K ohm
	** R2 = 10K ohm
	** C1 = 6800pF
	** C2 = 3900pF
	** Q ~= 1/sqrt(2)
	**
	** This filter is optionally bypassed by an MPF-102 JFET chip when
	** the LED filter is turned off.
	**
	** Under spice simulation the circuit yields -3dB = 2800Hz.
	** 90 degrees phase = 3000Hz (so, should oscillate at 3kHz!)
	**
	** The buffered output of the Sallen-Key passes into an RC high-pass with:
	** R = 1.39K ohm (1K ohm + 390 ohm)
	** C = 22uF (also C = 330nF, for improved high-frequency)
	**
	** High Hz = 5.2~ = 1 / (2pi * 1390 * 0.000022)
	** Under spice simulation the circuit yields -3dB = 5.2Hz.
	**
	** 8bitbubsy:
	** Keep in mind that many of the Amiga schematics that are floating around on
	** the internet have wrong RC values! They were most likely very early schematics
	** that didn't change before production (or changes that never reached production).
	** This has been confirmed by measuring the components on several Amiga motherboards.
	**
	** Correct values for A500, >rev3 (?) (A500_R6.pdf):
	** - 1-pole RC 6dB/oct low-pass: R=360 ohm, C=0.1uF
	** - Sallen-key low-pass ("LED"): R1/R2=10k ohm, C1=6800pF, C2=3900pF
	** - 1-pole RC 6dB/oct high-pass: R=1390 ohm (1000+390), C=22.33uF (22+0.33)
	**
	** Correct values for A1200, all revs (A1200_R2.pdf):
	** - 1-pole RC 6dB/oct low-pass: R=680 ohm, C=6800pF
	** - Sallen-key low-pass ("LED"): R1/R2=10k ohm, C1=6800pF, C2=3900pF (same as A500)
	** - 1-pole RC 6dB/oct high-pass: R=1390 ohm (1000+390), C=22uF
	*/
	double R, C, R1, R2, C1, C2, cutoff, qfactor;

	if (audio.oversamplingFlag)
		dAudioFreq *= 2.0; // 2x oversampling

	const bool audioWasntLocked = !audio.locked;
	if (audioWasntLocked)
		lockAudio();

	// A500 1-pole (6db/oct) static RC low-pass filter:
	R = 360.0; // R321 (360 ohm)
	C = 1e-7;  // C321 (0.1uF)
	cutoff = 1.0 / (PT2_TWO_PI * R * C); // ~4420.971Hz
	calcRCFilterCoeffs(dAudioFreq, cutoff, &filterLoA500);

	// (optional) A1200 1-pole (6db/oct) static RC low-pass filter:
	R = 680.0;  // R321 (680 ohm)
	C = 6.8e-9; // C321 (6800pF)
	cutoff = 1.0 / (PT2_TWO_PI * R * C); // ~34419.322Hz

	useA1200LowPassFilter = false;
	if (dAudioFreq/2.0 > cutoff)
	{
		calcRCFilterCoeffs(dAudioFreq, cutoff, &filterLoA1200);
		useA1200LowPassFilter = true;
	}

	// Sallen-Key low-pass filter ("LED" filter, same values on A500/A1200):
	R1 = 10000.0; // R322 (10K ohm)
	R2 = 10000.0; // R323 (10K ohm)
	C1 = 6.8e-9;  // C322 (6800pF)
	C2 = 3.9e-9;  // C323 (3900pF)
	cutoff = 1.0 / (PT2_TWO_PI * pt2_sqrt(R1 * R2 * C1 * C2)); // ~3090.533Hz
	qfactor = pt2_sqrt(R1 * R2 * C1 * C2) / (C2 * (R1 + R2)); // ~0.660225
	calcLEDFilterCoeffs(dAudioFreq, cutoff, qfactor, &filterLED);

	// A500 1-pole (6dB/oct) static RC high-pass filter:
	R = 1390.0;   // R324 (1K ohm) + R325 (390 ohm)
	C = 2.233e-5; // C334 (22uF) + C335 (0.33uF)
	cutoff = 1.0 / (PT2_TWO_PI * R * C); // ~5.128Hz
	calcRCFilterCoeffs(dAudioFreq, cutoff, &filterHiA500);

	// A1200 1-pole (6dB/oct) static RC high-pass filter:
	R = 1390.0; // R324 (1K ohm resistor) + R325 (390 ohm resistor)
	C = 2.2e-5; // C334 (22uF capacitor)
	cutoff = 1.0 / (PT2_TWO_PI * R * C); // ~5.205Hz
	calcRCFilterCoeffs(dAudioFreq, cutoff, &filterHiA1200);

	if (audioWasntLocked)
		unlockAudio();
}

void resetAmigaFilterStates(void)
{
	const bool audioWasntLocked = !audio.locked;
	if (audioWasntLocked)
		lockAudio();

	clearRCFilterState(&filterLoA500);
	clearRCFilterState(&filterLoA1200);
	clearRCFilterState(&filterHiA500);
	clearRCFilterState(&filterHiA1200);
	clearLEDFilterState(&filterLED);

	if (audioWasntLocked)
		unlockAudio();
}

static void processFiltersA1200_NoLED(double *dBufferL, double *dBufferR, int32_t numSamples)
{
	if (useA1200LowPassFilter)
	{
		for (int32_t i = 0; i < numSamples; i++)
		{
			double dOut[2];

			dOut[0] = dBufferL[i];
			dOut[1] = dBufferR[i];

			// low-pass filter
			RCLowPassFilterStereo(&filterLoA1200, dOut, dOut);

			// high-pass RC filter
			RCHighPassFilterStereo(&filterHiA1200, dOut, dOut);

			dBufferL[i] = dOut[0];
			dBufferR[i] = dOut[1];
		}
	}
	else
	{
		for (int32_t i = 0; i < numSamples; i++)
		{
			double dOut[2];

			dOut[0] = dBufferL[i];
			dOut[1] = dBufferR[i];

			// high-pass RC filter
			RCHighPassFilterStereo(&filterHiA1200, dOut, dOut);

			dBufferL[i] = dOut[0];
			dBufferR[i] = dOut[1];
		}
	}
}

static void processFiltersA1200_LED(double *dBufferL, double *dBufferR, int32_t numSamples)
{
	if (useA1200LowPassFilter)
	{
		for (int32_t i = 0; i < numSamples; i++)
		{
			double dOut[2];

			dOut[0] = dBufferL[i];
			dOut[1] = dBufferR[i];

			// low-pass filter
			RCLowPassFilterStereo(&filterLoA1200, dOut, dOut);

			// "LED" Sallen-Key filter
			LEDFilter(&filterLED, dOut, dOut);

			// high-pass RC filter
			RCHighPassFilterStereo(&filterHiA1200, dOut, dOut);

			dBufferL[i] = dOut[0];
			dBufferR[i] = dOut[1];
		}
	}
	else
	{
		for (int32_t i = 0; i < numSamples; i++)
		{
			double dOut[2];

			dOut[0] = dBufferL[i];
			dOut[1] = dBufferR[i];

			// "LED" Sallen-Key filter
			LEDFilter(&filterLED, dOut, dOut);

			// high-pass RC filter
			RCHighPassFilterStereo(&filterHiA1200, dOut, dOut);

			dBufferL[i] = dOut[0];
			dBufferR[i] = dOut[1];
		}
	}
}

static void processFiltersA500_NoLED(double *dBufferL, double *dBufferR, int32_t numSamples)
{
	for (int32_t i = 0; i < numSamples; i++)
	{
		double dOut[2];

		dOut[0] = dBufferL[i];
		dOut[1] = dBufferR[i];

		// low-pass RC filter
		RCLowPassFilterStereo(&filterLoA500, dOut, dOut);

		// high-pass RC filter
		RCHighPassFilterStereo(&filterHiA500, dOut, dOut);

		dBufferL[i] = dOut[0];
		dBufferR[i] = dOut[1];
	}
}

static void processFiltersA500_LED(double *dBufferL, double *dBufferR, int32_t numSamples)
{
	for (int32_t i = 0; i < numSamples; i++)
	{
		double dOut[2];

		dOut[0] = dBufferL[i];
		dOut[1] = dBufferR[i];

		// low-pass RC filter
		RCLowPassFilterStereo(&filterLoA500, dOut, dOut);

		// "LED" Sallen-Key filter
		LEDFilter(&filterLED, dOut, dOut);

		// high-pass RC filter
		RCHighPassFilterStereo(&filterHiA500, dOut, dOut);

		dBufferL[i] = dOut[0];
		dBufferR[i] = dOut[1];
	}
}

static void updateAmigaFilterFunctions(void)
{
	if (filterModel == FILTERMODEL_A500)
	{
		if (ledFilterEnabled)
			processAmigaFilters = processFiltersA500_LED;
		else
			processAmigaFilters = processFiltersA500_NoLED;
	}
	else // A1200
	{
		if (ledFilterEnabled)
			processAmigaFilters = processFiltersA1200_LED;
		else
			processAmigaFilters = processFiltersA1200_NoLED;
	}
}

void setAmigaFilterModel(uint8_t model)
{
	const bool audioWasntLocked = !audio.locked;
	if (audioWasntLocked)
		lockAudio();

	filterModel = model;
	updateAmigaFilterFunctions();

	if (audioWasntLocked)
		unlockAudio();
}

void setLEDFilter(bool state)
{
	if (ledFilterEnabled == state)
		return; // same state as before!

	const bool audioWasntLocked = !audio.locked;
	if (audioWasntLocked)
		lockAudio();

	clearLEDFilterState(&filterLED);
	ledFilterEnabled = editor.useLEDFilter;
	updateAmigaFilterFunctions();

	if (audioWasntLocked)
		unlockAudio();
}

void toggleAmigaFilterModel(void)
{
	const bool audioWasntLocked = !audio.locked;
	if (audioWasntLocked)
		lockAudio();

	resetAmigaFilterStates();

	filterModel ^= 1;
	updateAmigaFilterFunctions();

	if (audioWasntLocked)
		unlockAudio();

	if (filterModel == FILTERMODEL_A500)
		displayMsg("AUDIO: AMIGA 500");
	else
		displayMsg("AUDIO: AMIGA 1200");
}
