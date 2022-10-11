/* Simple Paula emulator by 8bitbubsy (with BLEP synthesis by aciddose).
** The Amiga filters are handled in pt2_amigafilters.c
**
** Limitation: The audio output frequency can't be below 31389Hz ( ceil(PAULA_PAL_CLK / 113.0) )
*/

#include <stdint.h>
#include <stdbool.h>
#include "pt2_audio.h"
#include "pt2_paula.h"
#include "pt2_blep.h"
#include "pt2_sync.h"
#include "pt2_scopes.h" 
#include "pt2_config.h"

paulaVoice_t paula[PAULA_VOICES]; // globalized

static blep_t blep[PAULA_VOICES];
static double dPeriodToDeltaDiv;

void paulaSetOutputFrequency(double dAudioFreq, bool oversampling2x)
{
	dPeriodToDeltaDiv = PAULA_PAL_CLK / dAudioFreq;
	if (oversampling2x)
		dPeriodToDeltaDiv /= 2.0;
}

void paulaSetPeriod(int32_t ch, uint16_t period)
{
	paulaVoice_t *v = &paula[ch];

	int32_t realPeriod = period;
	if (realPeriod == 0)
		realPeriod = 1+65535; // confirmed behavior on real Amiga
	else if (realPeriod < 113)
		realPeriod = 113; // close to what happens on real Amiga (and needed for BLEP synthesis)

	// to be read on next sampling step (or on DMA trigger)
	v->AUD_PER_delta = dPeriodToDeltaDiv / realPeriod;
	v->AUD_PER_deltamul = 1.0 / v->AUD_PER_delta; // for BLEP synthesis (prevents division in inner mixing loop)

	// handle BLEP synthesis edge-cases

	if (v->dLastDelta == 0.0)
		v->dLastDelta = v->AUD_PER_delta;

	if (v->dLastDeltaMul == 0.0)
		v->dLastDeltaMul = v->AUD_PER_deltamul;

	// handle visualizers

	if (editor.songPlaying)
	{
		v->syncPeriod = realPeriod;
		v->syncFlags |= SET_SCOPE_PERIOD;
	}
	else
	{
		scopeSetPeriod(ch, realPeriod);
	}
}

void paulaSetVolume(int32_t ch, uint16_t vol)
{
	paulaVoice_t *v = &paula[ch];

	int32_t realVol = vol & 127;
	if (realVol > 64)
		realVol = 64;

	// multiplying sample point by this also scales the sample from -128..127 -> -1.0 .. ~0.99
	v->AUD_VOL = realVol * (1.0 / (128.0 * 64.0));

	// handle visualizers

	if (editor.songPlaying)
	{
		v->syncVolume = (uint8_t)realVol;
		v->syncFlags |= SET_SCOPE_VOLUME;
	}
	else
	{
		scope[ch].volume = (uint8_t)realVol;
	}
}

void paulaSetLength(int32_t ch, uint16_t len)
{
	paulaVoice_t *v = &paula[ch];

	v->AUD_LEN = len;

	// handle visualizers

	if (editor.songPlaying)
		v->syncFlags |= SET_SCOPE_LENGTH;
	else
		scope[ch].newLength = len * 2;
}

void paulaSetData(int32_t ch, const int8_t *src)
{
	paulaVoice_t *v = &paula[ch];

	// if pointer is NULL, use empty 128kB sample slot after sample 31 in the tracker
	if (src == NULL)
		src = &song->sampleData[config.reservedSampleOffset];

	v->AUD_LC = src;

	// handle visualizers

	if (editor.songPlaying)
		v->syncFlags |= SET_SCOPE_DATA;
	else
		scope[ch].newData = src;
}

static inline void refetchPeriod(paulaVoice_t *v) // Paula stage
{
	// set BLEP stuff
	v->dLastPhase = v->dPhase;
	v->dLastDelta = v->dDelta;
	v->dLastDeltaMul = v->dDeltaMul;
	v->dBlepOffset = v->dLastPhase * v->dLastDeltaMul;

	// Paula only updates period (delta) during period refetching (this stage)
	v->dDelta = v->AUD_PER_delta;
	v->dDeltaMul = v->AUD_PER_deltamul;

	v->nextSampleStage = true;
}

static void startPaulaDMA(int32_t ch)
{
	paulaVoice_t *v = &paula[ch];

	// if pointer is NULL, use empty 128kB sample slot after sample 31 in the tracker
	if (v->AUD_LC == NULL)
		v->AUD_LC = &song->sampleData[config.reservedSampleOffset];

	// immediately update AUD_LC/AUD_LEN
	v->location = v->AUD_LC;
	v->lengthCounter = v->AUD_LEN;

	// make Paula fetch new samples immediately
	v->sampleCounter = 0;
	v->DMATriggerFlag = true;

	refetchPeriod(v);
	v->dPhase = 0.0; // kludge: must be cleared *after* refetchPeriod()

	v->DMA_active = true;

	// handle visualizers

	if (editor.songPlaying)
	{
		v->syncTriggerData = v->AUD_LC;
		v->syncTriggerLength = v->AUD_LEN * 2;
		v->syncFlags |= TRIGGER_SCOPE;
	}
	else
	{
		scope_t *s = &scope[ch];
		s->newData = v->AUD_LC;
		s->newLength = v->AUD_LEN * 2;
		scopeTrigger(ch);
	}
}

static void stopPaulaDMA(int32_t ch)
{
	paulaVoice_t *v = &paula[ch];

	v->DMA_active = false;

	// handle visualizers
 
	if (editor.songPlaying)
		v->syncFlags |= STOP_SCOPE;
	else
		scope[ch].active = false;
}

void paulaSetDMACON(uint16_t bits) // $DFF096 register write (only controls paula DMAs)
{
	if (bits & 0x8000)
	{
		// set
		if (bits & 1) startPaulaDMA(0);
		if (bits & 2) startPaulaDMA(1);
		if (bits & 4) startPaulaDMA(2);
		if (bits & 8) startPaulaDMA(3);
	}
	else
	{
		// clear
		if (bits & 1) stopPaulaDMA(0);
		if (bits & 2) stopPaulaDMA(1);
		if (bits & 4) stopPaulaDMA(2);
		if (bits & 8) stopPaulaDMA(3);
	}
}

static inline void nextSample(paulaVoice_t *v, blep_t *b)
{
	if (v->sampleCounter == 0)
	{
		// it's time to read new samples from DMA

		// don't update AUD_LEN/AUD_LC yet on DMA trigger
		if (!v->DMATriggerFlag)
		{
			if (--v->lengthCounter == 0)
			{
				v->lengthCounter = v->AUD_LEN;
				v->location = v->AUD_LC;
			}
		}

		v->DMATriggerFlag = false;

		// fill DMA data buffer
		v->AUD_DAT[0] = *v->location++;
		v->AUD_DAT[1] = *v->location++;
		v->sampleCounter = 2;
	}

	/* Pre-compute current sample point.
	** Output volume is only read from AUDxVOL at this stage,
	** and we don't emulate volume PWM anyway, so we can
	** pre-multiply by volume here.
	*/
	v->dSample = v->AUD_DAT[0] * v->AUD_VOL; // -128..127 * 0.0 .. 1.0

	// fill BLEP buffer if the new sample differs from the old one
	if (v->dSample != b->dLastValue)
	{
		if (v->dLastDelta > v->dLastPhase)
			blepAdd(b, v->dBlepOffset, b->dLastValue - v->dSample);

		b->dLastValue = v->dSample;
	}

	// progress AUD_DAT buffer
	v->AUD_DAT[0] = v->AUD_DAT[1];
	v->sampleCounter--;
}

void paulaGenerateSamples(double *dOutL, double *dOutR, int32_t numSamples)
{
	double *dMixBufSelect[PAULA_VOICES] = { dOutL, dOutR, dOutR, dOutL };

	memset(dOutL, 0, numSamples * sizeof (double));
	memset(dOutR, 0, numSamples * sizeof (double));

	paulaVoice_t *v = paula;
	blep_t *b = blep;

	for (int32_t i = 0; i < PAULA_VOICES; i++, v++, b++)
	{
		if (!v->DMA_active || v->location == NULL || v->AUD_LC == NULL)
			continue;

		double *dMixBuffer = dMixBufSelect[i]; // what output channel to mix into (L, R, R, L)
		for (int32_t j = 0; j < numSamples; j++)
		{
			if (v->nextSampleStage)
			{
				v->nextSampleStage = false;
				nextSample(v, b); // inlined
			}

			double dSample = v->dSample; // current Paula sample, pre-multiplied by volume, scaled to -1.0 .. 0.9921875
			if (b->samplesLeft > 0)
				dSample = blepRun(b, dSample);

			dMixBuffer[j] += dSample;

			v->dPhase += v->dDelta;
			if (v->dPhase >= 1.0)
			{
				v->dPhase -= 1.0;
				refetchPeriod(v); // inlined
			}
		}
	}
}
