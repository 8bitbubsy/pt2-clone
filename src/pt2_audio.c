// the audio filters and BLEP synthesis were coded by aciddose

// for finding memory leaks in debug mode with Visual Studio 
#if defined _DEBUG && defined _MSC_VER
#include <crtdbg.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <SDL2/SDL.h>
#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif
#include <math.h> // sqrt(),tan(),M_PI
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <limits.h>
#include "pt2_audio.h"
#include "pt2_header.h"
#include "pt2_helpers.h"
#include "pt2_blep.h"
#include "pt2_config.h"
#include "pt2_tables.h"
#include "pt2_palette.h"
#include "pt2_textout.h"
#include "pt2_visuals.h"
#include "pt2_scopes.h"
#include "pt2_mod2wav.h"
#include "pt2_structs.h"

#define INITIAL_DITHER_SEED 0x12345000

typedef struct ledFilter_t
{
	double buffer[4];
	double c, ci, feedback, bg, cg, c2;
} ledFilter_t;

typedef struct voice_t
{
	volatile bool active;
	const int8_t *data, *newData;
	int32_t length, newLength, pos;
	double dVolume, dDelta, dDeltaMul, dPhase, dLastDelta, dLastDeltaMul, dLastPhase, dPanL, dPanR;
} paulaVoice_t;

audio_t audio; // globalized

static volatile int8_t filterFlags;
static int8_t defStereoSep;
static bool amigaPanFlag;
static uint16_t ch1Pan, ch2Pan, ch3Pan, ch4Pan;
static int32_t oldPeriod, randSeed = INITIAL_DITHER_SEED;
static uint32_t oldScopeDelta, sampleCounter;
static double *dMixBufferL, *dMixBufferR, *dMixBufferLUnaligned, *dMixBufferRUnaligned, dOldVoiceDelta, dOldVoiceDeltaMul;
static double dPrngStateL, dPrngStateR;
static blep_t blep[AMIGA_VOICES], blepVol[AMIGA_VOICES];
static rcFilter_t filterLo, filterHi;
static ledFilter_t filterLED;
static paulaVoice_t paula[AMIGA_VOICES];
static SDL_AudioDeviceID dev;

// globalized
uint32_t samplesPerTick;

bool intMusic(void); // defined in pt_modplayer.c

static uint16_t bpm2SmpsPerTick(int32_t bpm, uint32_t audioFreq)
{
	if (bpm == 0)
		return 0;

	const int32_t ciaVal = (int32_t)(1773447 / bpm); // yes, PT truncates here
	const double dCiaHz = (double)CIA_PAL_CLK / ciaVal;

	int32_t smpsPerTick = (int32_t)((audioFreq / dCiaHz) + 0.5); // rounded
	return (uint16_t)smpsPerTick;
}

static void generateBpmTables(void)
{
	for (int32_t i = 32; i <= 255; i++)
	{
		audio.bpmTab[i-32] = bpm2SmpsPerTick(i, audio.outputRate);
		audio.bpmTab28kHz[i-32] = bpm2SmpsPerTick(i, 28836); // PAT2SMP hi quality
		audio.bpmTab22kHz[i-32] = bpm2SmpsPerTick(i, 22168); // PAT2SMP low quality
		audio.bpmTabMod2Wav[i-32] = bpm2SmpsPerTick(i, MOD2WAV_FREQ); // MOD2WAV
	}
}

static void clearLEDFilterState(void)
{
	filterLED.buffer[0] = 0.0; // left channel
	filterLED.buffer[1] = 0.0;
	filterLED.buffer[2] = 0.0; // right channel
	filterLED.buffer[3] = 0.0;
}

void setLEDFilter(bool state)
{
	const bool audioWasntLocked = !audio.locked;
	if (audioWasntLocked)
		lockAudio();

	editor.useLEDFilter = state;
	if (editor.useLEDFilter)
	{
		clearLEDFilterState();
		filterFlags |= FILTER_LED_ENABLED;
	}
	else
	{
		filterFlags &= ~FILTER_LED_ENABLED;
	}

	if (audioWasntLocked)
		unlockAudio();
}

void toggleLEDFilter(void)
{
	const bool audioWasntLocked = !audio.locked;
	if (audioWasntLocked)
		lockAudio();

	editor.useLEDFilter ^= 1;
	if (editor.useLEDFilter)
	{
		clearLEDFilterState();
		filterFlags |= FILTER_LED_ENABLED;
	}
	else
	{
		filterFlags &= ~FILTER_LED_ENABLED;
	}

	if (audioWasntLocked)
		unlockAudio();
}

/* Imperfect "LED" filter implementation. This may be further improved in the future.
** Based upon ideas posted by mystran @ the kvraudio.com forum.
**
** This filter may not function correctly used outside the fixed-cutoff context here!
*/

static double sigmoid(double x, double coefficient)
{
	/* Coefficient from:
	**   0.0 to  inf (linear)
	**  -1.0 to -inf (linear)
	*/
	return x / (x + coefficient) * (coefficient + 1.0);
}

static void calcLEDFilterCoeffs(const double sr, const double hz, const double fb, ledFilter_t *filter)
{
	/* tan() may produce NaN or other bad results in some cases!
	** It appears to work correctly with these specific coefficients.
	*/
	const double c = (hz < (sr / 2.0)) ? tan((M_PI * hz) / sr) : 1.0;
	const double g = 1.0 / (1.0 + c);

	// dirty compensation
	const double s = 0.5;
	const double t = 0.5;
	const double ic = c > t ? 1.0 / ((1.0 - s*t) + s*c) : 1.0;
	const double cg = c * g;
	const double fbg = 1.0 / (1.0 + fb * cg*cg);

	filter->c = c;
	filter->ci = g;
	filter->feedback = 2.0 * sigmoid(fb, 0.5);
	filter->bg = fbg * filter->feedback * ic;
	filter->cg = cg;
	filter->c2 = c * 2.0;
}

static inline void LEDFilter(ledFilter_t *f, const double *in, double *out)
{
	const double in_1 = DENORMAL_OFFSET;
	const double in_2 = DENORMAL_OFFSET;

	const double c = f->c;
	const double g = f->ci;
	const double cg = f->cg;
	const double bg = f->bg;
	const double c2 = f->c2;

	double *v = f->buffer;

	// left channel
	const double estimate_L = in_2 + g*(v[1] + c*(in_1 + g*(v[0] + c*in[0])));
	const double y0_L = v[0]*g + in[0]*cg + in_1 + estimate_L * bg;
	const double y1_L = v[1]*g + y0_L*cg + in_2;

	v[0] += c2 * (in[0] - y0_L);
	v[1] += c2 * (y0_L - y1_L);
	out[0] = y1_L;

	// right channel
	const double estimate_R = in_2 + g*(v[3] + c*(in_1 + g*(v[2] + c*in[1])));
	const double y0_R = v[2]*g + in[1]*cg + in_1 + estimate_R * bg;
	const double y1_R = v[3]*g + y0_R*cg + in_2;

	v[2] += c2 * (in[1] - y0_R);
	v[3] += c2 * (y0_R - y1_R);
	out[1] = y1_R;
}

void calcRCFilterCoeffs(double dSr, double dHz, rcFilter_t *f)
{
	f->c = tan((M_PI * dHz) / dSr);
	f->c2 = f->c * 2.0;
	f->g = 1.0 / (1.0 + f->c);
	f->cg = f->c * f->g;
}

void clearRCFilterState(rcFilter_t *f)
{
	f->buffer[0] = 0.0; // left channel
	f->buffer[1] = 0.0; // right channel
}

// aciddose: input 0 is resistor side of capacitor (low-pass), input 1 is reference side (high-pass)
static inline double getLowpassOutput(rcFilter_t *f, const double input_0, const double input_1, const double buffer)
{
	return buffer * f->g + input_0 * f->cg + input_1 * (1.0 - f->cg);
}

void RCLowPassFilter(rcFilter_t *f, const double *in, double *out)
{
	double output;

	// left channel RC low-pass
	output = getLowpassOutput(f, in[0], 0.0, f->buffer[0]);
	f->buffer[0] += (in[0] - output) * f->c2;
	out[0] = output;

	// right channel RC low-pass
	output = getLowpassOutput(f, in[1], 0.0, f->buffer[1]);
	f->buffer[1] += (in[1] - output) * f->c2;
	out[1] = output;
}

void RCHighPassFilter(rcFilter_t *f, const double *in, double *out)
{
	double low[2];

	RCLowPassFilter(f, in, low);

	out[0] = in[0] - low[0]; // left channel high-pass
	out[1] = in[1] - low[1]; // right channel high-pass
}

/* These two are used for the filters in the SAMPLER screen, and
** also the 2x downsampling when loading samples whose frequency
** is above 22kHz.
*/

void RCLowPassFilterMono(rcFilter_t *f, const double in, double *out)
{
	double output = getLowpassOutput(f, in, 0.0, f->buffer[0]);
	f->buffer[0] += (in - output) * f->c2;
	*out = output;
}

void RCHighPassFilterMono(rcFilter_t *f, const double in, double *out)
{
	double low;

	RCLowPassFilterMono(f, in, &low);
	*out = in - low; // high-pass
}

/* aciddose: these sin/cos approximations both use a 0..1
** parameter range and have 'normalized' (1/2 = 0db) coeffs
**
** the coeffs are for LERP(x, x * x, 0.224) * sqrt(2)
** max_error is minimized with 0.224 = 0.0013012886
*/
static double sinApx(double fX)
{
	fX = fX * (2.0 - fX);
	return fX * 1.09742972 + fX * fX * 0.31678383;
}

static double cosApx(double fX)
{
	fX = (1.0 - fX) * (1.0 + fX);
	return fX * 1.09742972 + fX * fX * 0.31678383;
}

void lockAudio(void)
{
	if (dev != 0)
		SDL_LockAudioDevice(dev);

	audio.locked = true;
}

void unlockAudio(void)
{
	if (dev != 0)
		SDL_UnlockAudioDevice(dev);

	audio.locked = false;
}

void mixerUpdateLoops(void) // updates Paula loop (+ scopes)
{
	for (int32_t i = 0; i < AMIGA_VOICES; i++)
	{
		const moduleChannel_t *ch = &song->channels[i];
		if (ch->n_samplenum == editor.currSample)
		{
			const moduleSample_t *s = &song->samples[editor.currSample];

			paulaSetData(i, ch->n_start + s->loopStart);
			paulaSetLength(i, s->loopLength >> 1);
		}
	}
}

static void mixerSetVoicePan(uint8_t ch, uint16_t pan) // pan = 0..256
{
	double dPan;

	/* aciddose: proper 'normalized' equal-power panning is (assuming pan left to right):
	** L = cos(p * pi * 1/2) * sqrt(2);
	** R = sin(p * pi * 1/2) * sqrt(2);
	*/
	dPan = pan * (1.0 / 256.0); // 0.0..1.0

	paula[ch].dPanL = cosApx(dPan);
	paula[ch].dPanR = sinApx(dPan);
}

void mixerKillVoice(int32_t ch)
{
	const bool audioWasntLocked = !audio.locked;
	if (audioWasntLocked)
		lockAudio();

	// copy old pans
	const double dOldPanL = paula[ch].dPanL;
	const double dOldPanR = paula[ch].dPanR;

	memset(&paula[ch], 0, sizeof (paulaVoice_t));
	stopScope(ch);

	// store old pans
	paula[ch].dPanL = dOldPanL;
	paula[ch].dPanR = dOldPanR;

	memset(&blep[ch], 0, sizeof (blep_t));
	memset(&blepVol[ch], 0, sizeof (blep_t));

	if (audioWasntLocked)
		unlockAudio();
}

void turnOffVoices(void)
{
	const bool audioWasntLocked = !audio.locked;
	if (audioWasntLocked)
		lockAudio();

	for (int32_t i = 0; i < AMIGA_VOICES; i++)
		mixerKillVoice(i);

	clearRCFilterState(&filterLo);
	clearRCFilterState(&filterHi);
	clearLEDFilterState();

	resetAudioDithering();

	editor.tuningFlag = false;

	if (audioWasntLocked)
		unlockAudio();
}

void resetCachedMixerPeriod(void)
{
	oldPeriod = -1;
}

// the following routines are only called from the mixer thread.

void paulaSetPeriod(int32_t ch, uint16_t period)
{
	int32_t realPeriod;
	double dPeriodToDeltaDiv;
	paulaVoice_t *v;

	v = &paula[ch];

	if (period == 0)
		realPeriod = 1+65535; // confirmed behavior on real Amiga
	else if (period < 113)
		realPeriod = 113; // close to what happens on real Amiga (and needed for BLEP synthesis)
	else
		realPeriod = period;

	// if the new period was the same as the previous period, use cached deltas
	if (realPeriod != oldPeriod)
	{
		oldPeriod = realPeriod;

		// this period is not cached, calculate mixer/scope deltas

		// during PAT2SMP or doing MOD2WAV, use different audio output rates
		if (editor.isSMPRendering)
			dPeriodToDeltaDiv = editor.pat2SmpHQ ? (PAULA_PAL_CLK / 28836.0) : (PAULA_PAL_CLK / 22168.0);
		else if (editor.isWAVRendering)
			dPeriodToDeltaDiv = PAULA_PAL_CLK / (double)MOD2WAV_FREQ;
		else
			dPeriodToDeltaDiv = audio.dPeriodToDeltaDiv;

		const double dPeriodToScopeDeltaDiv = ((double)PAULA_PAL_CLK * SCOPE_FRAC_SCALE) / SCOPE_HZ;

		// cache these
		dOldVoiceDelta = dPeriodToDeltaDiv / realPeriod;
		oldScopeDelta = (int32_t)((dPeriodToScopeDeltaDiv / realPeriod) + 0.5);
		dOldVoiceDeltaMul = 1.0 / dOldVoiceDelta; // for BLEP synthesis
	}

	v->dDelta = dOldVoiceDelta;
	scope[ch].delta = oldScopeDelta;

	// for BLEP synthesis
	v->dDeltaMul = dOldVoiceDeltaMul;
	if (v->dLastDelta == 0.0) v->dLastDelta = v->dDelta;
	if (v->dLastDeltaMul == 0.0) v->dLastDeltaMul = v->dDeltaMul;
}

void paulaSetVolume(int32_t ch, uint16_t vol)
{
	vol &= 127; // confirmed behavior on real Amiga

	if (vol > 64)
		vol = 64; // confirmed behavior on real Amiga

	paula[ch].dVolume = vol * (1.0 / 64.0);
	scope[ch].volume = (uint8_t)vol;
}

void paulaSetLength(int32_t ch, uint16_t len)
{
	if (len == 0)
	{
		len = 65535;
		/* Confirmed behavior on real Amiga (also needed for safety).
		** And yes, we have room for this, it will never overflow!
		*/
	}

	scope[ch].newLength = paula[ch].newLength = len << 1; // our mixer works with bytes, not words
}

void paulaSetData(int32_t ch, const int8_t *src)
{
	// set voice data
	if (src == NULL)
		src = &song->sampleData[RESERVED_SAMPLE_OFFSET]; // dummy sample

	scope[ch].newData = paula[ch].newData = src;
}

void paulaStopDMA(int32_t ch)
{
	scope[ch].active = paula[ch].active = false;
}

void paulaStartDMA(int32_t ch)
{
	const int8_t *dat;
	int32_t length;
	paulaVoice_t *v;

	// trigger voice

	v  = &paula[ch];

	dat = v->newData;
	if (dat == NULL)
		dat = &song->sampleData[RESERVED_SAMPLE_OFFSET]; // dummy sample

	length = v->newLength;
	if (length < 2)
		length = 2; // for safety

	v->dPhase = 0.0;
	v->pos = 0;
	v->data = dat;
	v->length = length;
	v->active = true;

	scopeTrigger(ch, length);
}

void toggleA500Filters(void)
{
	const bool audioWasntLocked = !audio.locked;
	if (audioWasntLocked)
		lockAudio();

	clearRCFilterState(&filterLo);
	clearRCFilterState(&filterHi);
	clearLEDFilterState();

	if (filterFlags & FILTER_A500)
	{
		filterFlags &= ~FILTER_A500;
		displayMsg("LP FILTER: A1200");
	}
	else
	{
		filterFlags |= FILTER_A500;
		displayMsg("LP FILTER: A500");
	}

	if (audioWasntLocked)
		unlockAudio();
}

void mixChannels(int32_t numSamples)
{
	double dSmp, dVol;
	blep_t *bSmp, *bVol;
	paulaVoice_t *v;

	memset(dMixBufferL, 0, numSamples * sizeof (double));
	memset(dMixBufferR, 0, numSamples * sizeof (double));

	for (int32_t i = 0; i < AMIGA_VOICES; i++)
	{
		v = &paula[i];
		if (!v->active || v->data == NULL)
			continue;

		bSmp = &blep[i];
		bVol = &blepVol[i];

		for (int32_t j = 0; j < numSamples; j++)
		{
			assert(v->data != NULL);
			dSmp = v->data[v->pos] * (1.0 / 128.0);
			dVol = v->dVolume;

			if (dSmp != bSmp->dLastValue)
			{
				if (v->dLastDelta > v->dLastPhase)
				{
					// div->mul trick: v->dLastDeltaMul is 1.0 / v->dLastDelta
					blepAdd(bSmp, v->dLastPhase * v->dLastDeltaMul, bSmp->dLastValue - dSmp);
				}

				bSmp->dLastValue = dSmp;
			}

			if (dVol != bVol->dLastValue)
			{
				blepVolAdd(bVol, bVol->dLastValue - dVol);
				bVol->dLastValue = dVol;
			}

			if (bSmp->samplesLeft > 0) dSmp = blepRun(bSmp, dSmp);
			if (bVol->samplesLeft > 0) dVol = blepRun(bVol, dVol);

			dSmp *= dVol;

			dMixBufferL[j] += dSmp * v->dPanL;
			dMixBufferR[j] += dSmp * v->dPanR;

			v->dPhase += v->dDelta;
			if (v->dPhase >= 1.0)
			{
				v->dPhase -= 1.0;

				v->dLastPhase = v->dPhase;
				v->dLastDelta = v->dDelta;
				v->dLastDeltaMul = v->dDeltaMul;

				if (++v->pos >= v->length)
				{
					v->pos = 0;

					// re-fetch new Paula register values now
					v->length = v->newLength;
					v->data = v->newData;
				}
			}
		}
	}
}

void mixChannelsMultiStep(int32_t numSamples) // for PAT2SMP
{
	double dSmp, dVol;
	blep_t *bSmp, *bVol;
	paulaVoice_t *v;

	memset(dMixBufferL, 0, numSamples * sizeof (double));
	memset(dMixBufferR, 0, numSamples * sizeof (double));

	for (int32_t i = 0; i < AMIGA_VOICES; i++)
	{
		v = &paula[i];
		if (!v->active || v->data == NULL)
			continue;

		bSmp = &blep[i];
		bVol = &blepVol[i];

		for (int32_t j = 0; j < numSamples; j++)
		{
			assert(v->data != NULL);
			dSmp = v->data[v->pos] * (1.0 / 128.0);
			dVol = v->dVolume;

			if (dSmp != bSmp->dLastValue)
			{
				if (v->dLastDelta > v->dLastPhase)
				{
					// div->mul trick: v->dLastDeltaMul is 1.0 / v->dLastDelta
					blepAdd(bSmp, v->dLastPhase * v->dLastDeltaMul, bSmp->dLastValue - dSmp);
				}

				bSmp->dLastValue = dSmp;
			}

			if (dVol != bVol->dLastValue)
			{
				blepVolAdd(bVol, bVol->dLastValue - dVol);
				bVol->dLastValue = dVol;
			}

			if (bSmp->samplesLeft > 0) dSmp = blepRun(bSmp, dSmp);
			if (bVol->samplesLeft > 0) dVol = blepRun(bVol, dVol);

			dSmp *= dVol;

			dMixBufferL[j] += dSmp * v->dPanL;
			dMixBufferR[j] += dSmp * v->dPanR;

			v->dPhase += v->dDelta;
			while (v->dPhase >= 1.0) // multi-step
			{
				v->dPhase -= 1.0;

				v->dLastPhase = v->dPhase;
				v->dLastDelta = v->dDelta;
				v->dLastDeltaMul = v->dDeltaMul;

				if (++v->pos >= v->length)
				{
					v->pos = 0;

					// re-fetch new Paula register values now
					v->length = v->newLength;
					v->data = v->newData;
				}
			}
		}
	}
}

void resetAudioDithering(void)
{
	randSeed = INITIAL_DITHER_SEED;
	dPrngStateL = 0.0;
	dPrngStateR = 0.0;
}

static inline int32_t random32(void)
{
	// LCG random 32-bit generator (quite good and fast)
	randSeed *= 134775813;
	randSeed++;
	return randSeed;
}

static inline void processMixedSamplesA1200(int32_t i, int16_t *out)
{
	int32_t smp32;
	double dOut[2], dPrng;

	dOut[0] = dMixBufferL[i];
	dOut[1] = dMixBufferR[i];

	// don't process any low-pass filter since the cut-off is around 34kHz on A1200

	// process high-pass filter
	RCHighPassFilter(&filterHi, dOut, dOut);

	// normalize and flip phase (A500/A1200 has an inverted audio signal)
	dOut[0] *= (-INT16_MAX / (double)AMIGA_VOICES);
	dOut[1] *= (-INT16_MAX / (double)AMIGA_VOICES);

	// left channel - 1-bit triangular dithering (high-pass filtered)
	dPrng = random32() * (0.5 / INT32_MAX); // -0.5..0.5
	dOut[0] = (dOut[0] + dPrng) - dPrngStateL;
	dPrngStateL = dPrng;
	smp32 = (int32_t)dOut[0];
	CLAMP16(smp32);
	out[0] = (int16_t)smp32;

	// right channel - 1-bit triangular dithering (high-pass filtered)
	dPrng = random32() * (0.5 / INT32_MAX); // -0.5..0.5
	dOut[1] = (dOut[1] + dPrng) - dPrngStateR;
	dPrngStateR = dPrng;
	smp32 = (int32_t)dOut[1];
	CLAMP16(smp32);
	out[1] = (int16_t)smp32;
}

static inline void processMixedSamplesA1200LED(int32_t i, int16_t *out)
{
	int32_t smp32;
	double dOut[2], dPrng;

	dOut[0] = dMixBufferL[i];
	dOut[1] = dMixBufferR[i];

	// don't process any low-pass filter since the cut-off is around 34kHz on A1200

	// process "LED" filter
	LEDFilter(&filterLED, dOut, dOut);

	// process high-pass filter
	RCHighPassFilter(&filterHi, dOut, dOut);

	// normalize and flip phase (A500/A1200 has an inverted audio signal)
	dOut[0] *= (-INT16_MAX / (double)AMIGA_VOICES);
	dOut[1] *= (-INT16_MAX / (double)AMIGA_VOICES);

	// left channel - 1-bit triangular dithering (high-pass filtered)
	dPrng = random32() * (0.5 / INT32_MAX); // -0.5..0.5
	dOut[0] = (dOut[0] + dPrng) - dPrngStateL;
	dPrngStateL = dPrng;
	smp32 = (int32_t)dOut[0];
	CLAMP16(smp32);
	out[0] = (int16_t)smp32;

	// right channel - 1-bit triangular dithering (high-pass filtered)
	dPrng = random32() * (0.5 / INT32_MAX); // -0.5..0.5
	dOut[1] = (dOut[1] + dPrng) - dPrngStateR;
	dPrngStateR = dPrng;
	smp32 = (int32_t)dOut[1];
	CLAMP16(smp32);
	out[1] = (int16_t)smp32;
}

static inline void processMixedSamplesA500(int32_t i, int16_t *out)
{
	int32_t smp32;
	double dOut[2], dPrng;

	dOut[0] = dMixBufferL[i];
	dOut[1] = dMixBufferR[i];

	// process low-pass filter
	RCLowPassFilter(&filterLo, dOut, dOut);

	// process high-pass filter
	RCHighPassFilter(&filterHi, dOut, dOut);

	dOut[0] *= (-INT16_MAX / (double)AMIGA_VOICES);
	dOut[1] *= (-INT16_MAX / (double)AMIGA_VOICES);

	// left channel - 1-bit triangular dithering (high-pass filtered)
	dPrng = random32() * (0.5 / INT32_MAX); // -0.5..0.5
	dOut[0] = (dOut[0] + dPrng) - dPrngStateL;
	dPrngStateL = dPrng;
	smp32 = (int32_t)dOut[0];
	CLAMP16(smp32);
	out[0] = (int16_t)smp32;

	// right channel - 1-bit triangular dithering (high-pass filtered)
	dPrng = random32() * (0.5 / INT32_MAX); // -0.5..0.5
	dOut[1] = (dOut[1] + dPrng) - dPrngStateR;
	dPrngStateR = dPrng;
	smp32 = (int32_t)dOut[1];
	CLAMP16(smp32);
	out[1] = (int16_t)smp32;
}

static inline void processMixedSamplesA500LED(int32_t i, int16_t *out)
{
	int32_t smp32;
	double dOut[2], dPrng;

	dOut[0] = dMixBufferL[i];
	dOut[1] = dMixBufferR[i];

	// process low-pass filter
	RCLowPassFilter(&filterLo, dOut, dOut);

	// process "LED" filter
	LEDFilter(&filterLED, dOut, dOut);

	// process high-pass filter
	RCHighPassFilter(&filterHi, dOut, dOut);

	dOut[0] *= (-INT16_MAX / (double)AMIGA_VOICES);
	dOut[1] *= (-INT16_MAX / (double)AMIGA_VOICES);

	// left channel - 1-bit triangular dithering (high-pass filtered)
	dPrng = random32() * (0.5 / INT32_MAX); // -0.5..0.5
	dOut[0] = (dOut[0] + dPrng) - dPrngStateL;
	dPrngStateL = dPrng;
	smp32 = (int32_t)dOut[0];
	CLAMP16(smp32);
	out[0] = (int16_t)smp32;

	// right channel - 1-bit triangular dithering (high-pass filtered)
	dPrng = random32() * (0.5 / INT32_MAX); // -0.5..0.5
	dOut[1] = (dOut[1] + dPrng) - dPrngStateR;
	dPrngStateR = dPrng;
	smp32 = (int32_t)dOut[1];
	CLAMP16(smp32);
	out[1] = (int16_t)smp32;
}

// for PAT2SMP
static inline void processMixedSamplesRaw(int32_t i, int16_t *out)
{
	int32_t smp32;
	double dOut[2];

	dOut[0] = dMixBufferL[i];
	dOut[1] = dMixBufferR[i];

	// normalize 
	dOut[0] *= (INT16_MAX / (double)AMIGA_VOICES);
	dOut[1] *= (INT16_MAX / (double)AMIGA_VOICES);

	dOut[0] = (dOut[0] + dOut[1]) * 0.5; // mix to mono

	smp32 = (int32_t)dOut[0];
	CLAMP16(smp32);
	out[0] = (int16_t)smp32;
}

void outputAudio(int16_t *target, int32_t numSamples)
{
	int16_t *outStream, out[2];
	int32_t i;

	if (editor.isSMPRendering)
	{
		// render to sample (PAT2SMP)

		int32_t samplesTodo = numSamples;
		if (editor.pat2SmpPos+samplesTodo > MAX_SAMPLE_LEN)
			samplesTodo = MAX_SAMPLE_LEN-editor.pat2SmpPos;

		mixChannelsMultiStep(samplesTodo);

		outStream = &editor.pat2SmpBuf[editor.pat2SmpPos];
		for (i = 0; i < samplesTodo; i++)
		{
			processMixedSamplesRaw(i, out);
			outStream[i] = out[0];
		}

		editor.pat2SmpPos += samplesTodo;
		if (editor.pat2SmpPos >= MAX_SAMPLE_LEN)
		{
			editor.smpRenderingDone = true;
			updateWindowTitle(MOD_IS_MODIFIED);
		}
	}
	else
	{
		// render to stream

		mixChannels(numSamples);

		outStream = target;
		if (filterFlags & FILTER_A500)
		{
			// Amiga 500 filter model

			if (filterFlags & FILTER_LED_ENABLED)
			{
				for (i = 0; i < numSamples; i++)
				{
					processMixedSamplesA500LED(i, out);
					*outStream++ = out[0];
					*outStream++ = out[1];
				}
			}
			else
			{
				for (i = 0; i < numSamples; i++)
				{
					processMixedSamplesA500(i, out);
					*outStream++ = out[0];
					*outStream++ = out[1];
				}
			}
		}
		else
		{
			// Amiga 1200 filter model

			if (filterFlags & FILTER_LED_ENABLED)
			{
				for (i = 0; i < numSamples; i++)
				{
					processMixedSamplesA1200LED(i, out);
					*outStream++ = out[0];
					*outStream++ = out[1];
				}
			}
			else
			{
				for (i = 0; i < numSamples; i++)
				{
					processMixedSamplesA1200(i, out);
					*outStream++ = out[0];
					*outStream++ = out[1];
				}
			}
		}
	}
}

static void SDLCALL audioCallback(void *userdata, Uint8 *stream, int len)
{
	int16_t *streamOut;
	uint32_t samplesLeft;

	if (audio.forceMixerOff) // during MOD2WAV
	{
		memset(stream, 0, len);
		return;
	}

	streamOut = (int16_t *)stream;

	samplesLeft = len >> 2;
	while (samplesLeft > 0)
	{
		if (sampleCounter == 0)
		{
			if (editor.songPlaying)
				intMusic();

			sampleCounter = samplesPerTick;
		}

		uint32_t samplesTodo = sampleCounter;
		if (samplesTodo > samplesLeft)
			samplesTodo = samplesLeft;

		outputAudio(streamOut, samplesTodo);
		streamOut += samplesTodo << 1;

		samplesLeft -= samplesTodo;
		sampleCounter -= samplesTodo;
	}

	(void)userdata;
}

static void calculateFilterCoeffs(void)
{
	/* Amiga 500 filter emulation, by aciddose
	**
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
	** In the Amiga 1200, the low-pass cutoff is 26kHz+, so the
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
	*/

	double R, C, R1, R2, C1, C2, fc, fb;

	// A500 one-pole 6db/oct static RC low-pass filter:
	R = 360.0; // R321 (360 ohm resistor)
	C = 1e-7;  // C321 (0.1uF capacitor)
	fc = 1.0 / (2.0 * M_PI * R * C); // ~4420.97Hz
	calcRCFilterCoeffs(audio.outputRate, fc, &filterLo);

	// A500/A1200 Sallen-Key filter ("LED"):
	R1 = 10000.0; // R322 (10K ohm resistor)
	R2 = 10000.0; // R323 (10K ohm resistor)
	C1 = 6.8e-9;  // C322 (6800pF capacitor)
	C2 = 3.9e-9;  // C323 (3900pF capacitor)
	fc = 1.0 / (2.0 * M_PI * sqrt(R1 * R2 * C1 * C2)); // ~3090.53Hz
	fb = 0.125; // Fb = 0.125 : Q ~= 1/sqrt(2) (Butterworth)
	calcLEDFilterCoeffs(audio.outputRate, fc, fb, &filterLED);

	// A500/A1200 one-pole 6db/oct static RC high-pass filter:
	R = 1000.0 + 390.0; // R324 (1K ohm resistor) + R325 (390 ohm resistor)
	C = 2.2e-5;         // C334 (22uF capacitor) (+ C324 (0.33uF capacitor) if A500)
	fc = 1.0 / (2.0 * M_PI * R * C); // ~5.2Hz
	calcRCFilterCoeffs(audio.outputRate, fc, &filterHi);
}

void mixerCalcVoicePans(uint8_t stereoSeparation)
{
	uint8_t scaledPanPos = (stereoSeparation * 128) / 100;

	ch1Pan = 128 - scaledPanPos;
	ch2Pan = 128 + scaledPanPos;
	ch3Pan = 128 + scaledPanPos;
	ch4Pan = 128 - scaledPanPos;

	mixerSetVoicePan(0, ch1Pan);
	mixerSetVoicePan(1, ch2Pan);
	mixerSetVoicePan(2, ch3Pan);
	mixerSetVoicePan(3, ch4Pan);
}

bool setupAudio(void)
{
	SDL_AudioSpec want, have;

	want.freq = config.soundFrequency;
	want.samples = (uint16_t)config.soundBufferSize;
	want.format = AUDIO_S16;
	want.channels = 2;
	want.callback = audioCallback;
	want.userdata = NULL;

	dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
	if (dev == 0)
	{
		showErrorMsgBox("Unable to open audio device: %s", SDL_GetError());
		return false;
	}

	if (have.freq < 32000) // lower than this is not safe for the BLEP synthesis in the mixer
	{
		showErrorMsgBox("Unable to open audio: An audio rate below 32kHz can't be used!");
		return false;
	}

	if (have.format != want.format)
	{
		showErrorMsgBox("Unable to open audio: The sample format (signed 16-bit) couldn't be used!");
		return false;
	}

	audio.outputRate = have.freq;
	audio.audioBufferSize = have.samples;
	audio.dPeriodToDeltaDiv = (double)PAULA_PAL_CLK / audio.outputRate;

	generateBpmTables();

	/* If the audio output rate is lower than MOD2WAV_FREQ, we need
	** to allocate slightly more space so that MOD2WAV rendering
	** won't overflow.
	*/

	uint32_t maxSamplesToMix;
	if (MOD2WAV_FREQ > audio.outputRate)
		maxSamplesToMix = audio.bpmTabMod2Wav[32-32]; // BPM 32
	else
		maxSamplesToMix = audio.bpmTab[32-32]; // BPM 32

	dMixBufferLUnaligned = (double *)MALLOC_PAD(maxSamplesToMix * sizeof (double), 256);
	dMixBufferRUnaligned = (double *)MALLOC_PAD(maxSamplesToMix * sizeof (double), 256);

	if (dMixBufferLUnaligned == NULL || dMixBufferRUnaligned == NULL)
	{
		showErrorMsgBox("Out of memory!");
		return false;
	}

	dMixBufferL = (double *)ALIGN_PTR(dMixBufferLUnaligned, 256);
	dMixBufferR = (double *)ALIGN_PTR(dMixBufferRUnaligned, 256);

	mixerCalcVoicePans(config.stereoSeparation);
	defStereoSep = config.stereoSeparation;

	filterFlags = config.a500LowPassFilter ? FILTER_A500 : 0;
	calculateFilterCoeffs();

	samplesPerTick = audio.bpmTab[125-32]; // BPM 125
	sampleCounter = 0;

	SDL_PauseAudioDevice(dev, false);
	return true;
}

void audioClose(void)
{
	if (dev > 0)
	{
		SDL_PauseAudioDevice(dev, true);
		SDL_CloseAudioDevice(dev);
		dev = 0;
	}

	if (dMixBufferLUnaligned != NULL)
	{
		free(dMixBufferLUnaligned);
		dMixBufferLUnaligned = NULL;
	}

	if (dMixBufferRUnaligned != NULL)
	{
		free(dMixBufferRUnaligned);
		dMixBufferRUnaligned = NULL;
	}
}

void mixerSetSamplesPerTick(uint32_t val)
{
	samplesPerTick = val;
}

void mixerClearSampleCounter(void)
{
	sampleCounter = 0;
}

void toggleAmigaPanMode(void)
{
	const bool audioWasntLocked = !audio.locked;
	if (audioWasntLocked)
		lockAudio();

	amigaPanFlag ^= 1;
	if (!amigaPanFlag)
	{
		mixerCalcVoicePans(defStereoSep);
		displayMsg("AMIGA PANNING OFF");
	}
	else
	{
		mixerCalcVoicePans(100);
		displayMsg("AMIGA PANNING ON");
	}

	if (audioWasntLocked)
		unlockAudio();
}

void normalize32bitSigned(int32_t *sampleData, uint32_t sampleLength)
{
	int32_t sample, sampleVolPeak;
	uint32_t i;
	double dGain;

	sampleVolPeak = 0;
	for (i = 0; i < sampleLength; i++)
	{
		sample = ABS(sampleData[i]);
		if (sampleVolPeak < sample)
			sampleVolPeak = sample;
	}

	if (sampleVolPeak >= INT32_MAX)
		return; // sample is already normalized

	// prevent division by zero!
	if (sampleVolPeak <= 0)
		sampleVolPeak = 1;

	dGain = (double)INT32_MAX / sampleVolPeak;
	for (i = 0; i < sampleLength; i++)
	{
		sample = (int32_t)(sampleData[i] * dGain);
		sampleData[i] = (int32_t)sample;
	}
}

void normalize16bitSigned(int16_t *sampleData, uint32_t sampleLength)
{
	uint32_t i;
	int32_t sample, sampleVolPeak, gain;

	sampleVolPeak = 0;
	for (i = 0; i < sampleLength; i++)
	{
		sample = ABS(sampleData[i]);
		if (sampleVolPeak < sample)
			sampleVolPeak = sample;
	}

	if (sampleVolPeak >= INT16_MAX)
		return; // sample is already normalized

	if (sampleVolPeak < 1)
		return;

	gain = (INT16_MAX * 65536) / sampleVolPeak;
	for (i = 0; i < sampleLength; i++)
		sampleData[i] = (int16_t)((sampleData[i] * gain) >> 16);
}

void normalize8bitFloatSigned(float *fSampleData, uint32_t sampleLength)
{
	uint32_t i;
	float fSample, fSampleVolPeak, fGain;

	fSampleVolPeak = 0.0f;
	for (i = 0; i < sampleLength; i++)
	{
		fSample = fabsf(fSampleData[i]);
		if (fSampleVolPeak < fSample)
			fSampleVolPeak = fSample;
	}

	if (fSampleVolPeak <= 0.0f)
		return;

	fGain = INT8_MAX / fSampleVolPeak;
	for (i = 0; i < sampleLength; i++)
		fSampleData[i] *= fGain;
}

void normalize8bitDoubleSigned(double *dSampleData, uint32_t sampleLength)
{
	uint32_t i;
	double dSample, dSampleVolPeak, dGain;

	dSampleVolPeak = 0.0;
	for (i = 0; i < sampleLength; i++)
	{
		dSample = fabs(dSampleData[i]);
		if (dSampleVolPeak < dSample)
			dSampleVolPeak = dSample;
	}

	if (dSampleVolPeak <= 0.0)
		return;

	dGain = INT8_MAX / dSampleVolPeak;
	for (i = 0; i < sampleLength; i++)
		dSampleData[i] *= dGain;
}
