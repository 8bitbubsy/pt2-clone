// for finding memory leaks in debug mode with Visual Studio 
#if defined _DEBUG && defined _MSC_VER
#include <crtdbg.h>
#endif

#include <stdint.h>
#include <stdbool.h>
#include <math.h> // modf()
#ifndef _WIN32
#include <unistd.h> // usleep()
#endif
#include "pt2_header.h"
#include "pt2_helpers.h"
#include "pt2_visuals.h"
#include "pt2_scopes.h"
#include "pt2_sampler.h"
#include "pt2_palette.h"
#include "pt2_tables.h"
#include "pt2_structs.h"
#include "pt2_config.h"

// this uses code that is not entirely thread safe, but I have never had any issues so far...

static volatile bool scopesUpdatingFlag, scopesDisplayingFlag;
static int32_t oldPeriod = -1;
static uint32_t scopeTimeLen, scopeTimeLenFrac;
static uint64_t timeNext64, timeNext64Frac;
static float fOldScopeDelta;
static SDL_Thread *scopeThread;

scope_t scope[AMIGA_VOICES]; // global

void resetCachedScopePeriod(void)
{
	oldPeriod = -1;
}

int32_t getSampleReadPos(int32_t ch, uint8_t smpNum)
{
	const int8_t *data;
	volatile bool active;
	volatile int32_t pos;
	volatile scope_t *sc;

	moduleSample_t *s;
	
	sc = &scope[ch];

	// cache some stuff
	active = sc->active;
	data = sc->data;
	pos = sc->pos;

	if (!active || data == NULL || pos <= 2) // pos 0..2 = sample loop area for non-looping samples
		return -1;

	s = &song->samples[smpNum];

	// hackish way of getting real scope/sampling position
	pos = (int32_t)(&data[pos] - &song->sampleData[s->offset]);
	if (pos < 0 || pos >= s->length)
		return -1;

	return pos;
}

void scopeSetVolume(int32_t ch, uint16_t vol)
{
	vol &= 127; // confirmed behavior on real Amiga

	if (vol > 64)
		vol = 64; // confirmed behavior on real Amiga

	scope[ch].volume = (uint8_t)vol;
}

void scopeSetPeriod(int32_t ch, uint16_t period)
{
	int32_t realPeriod;

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

		// this period is not cached, calculate scope delta

		const float fPeriodToScopeDeltaDiv = PAULA_PAL_CLK / (float)SCOPE_HZ;
		fOldScopeDelta = fPeriodToScopeDeltaDiv / realPeriod;
	}

	scope[ch].fDelta = fOldScopeDelta;
}

void scopeSetData(int32_t ch, const int8_t *src)
{
	// set voice data
	if (src == NULL)
		src = &song->sampleData[RESERVED_SAMPLE_OFFSET]; // dummy sample

	scope[ch].newData = src;
}

void scopeSetLength(int32_t ch, uint16_t len)
{
	if (len == 0)
	{
		len = 65535;
		/* Confirmed behavior on real Amiga (also needed for safety).
		** And yes, we have room for this, it will never overflow!
		*/
	}
	scope[ch].newLength = len << 1;
}

void scopeTrigger(int32_t ch)
{
	volatile scope_t *sc = &scope[ch];
	scope_t tempState = *sc; // cache it

	const int8_t *newData = tempState.newData;
	if (newData == NULL)
		newData = &song->sampleData[RESERVED_SAMPLE_OFFSET]; // dummy sample

	int32_t newLength = tempState.newLength;
	if (newLength < 2)
		newLength = 2;

	tempState.fPhase = 0.0f;
	tempState.pos = 0;
	tempState.data = newData;
	tempState.length = newLength;
	tempState.active = true;

	/* Update live scope now.
	** In theory it -can- be written to in the middle of a cached read,
	** then the read thread writes its own non-updated cached copy back and
	** the trigger never happens. So far I have never seen it happen,
	** so it's probably very rare. Yes, this is not good coding...
	*/
	*sc = tempState;
}

void updateScopes(void)
{
	scope_t tempState;

	if (editor.isWAVRendering)
		return;

	volatile scope_t *sc = scope;

	scopesUpdatingFlag = true;
	for (int32_t i = 0; i < AMIGA_VOICES; i++, sc++)
	{
		tempState = *sc; // cache it
		if (!tempState.active)
			continue; // scope is not active

		tempState.fPhase += tempState.fDelta;

		const int32_t wholeSamples = (int32_t)tempState.fPhase;
		tempState.fPhase -= wholeSamples;
		tempState.pos += wholeSamples;

		if (tempState.pos >= tempState.length)
		{
			// sample reached end, simulate Paula register update (sample swapping)

			/* Wrap pos around one time with current length, then set new length
			** and wrap around it (handles one-shot loops and sample swapping).
			*/
			tempState.pos -= tempState.length;

			tempState.length = tempState.newLength;
			if (tempState.length > 0)
				tempState.pos %= tempState.length;

			tempState.data = tempState.newData;
		}

		*sc = tempState; // update scope state
	}
	scopesUpdatingFlag = false;
}

/* This routine gets the average sample amplitude through the running scope voices.
** This gives a somewhat more stable result than getting the peak from the mixer,
** and we don't care about including filters/BLEP in the peak calculation.
*/
static void updateRealVuMeters(void) 
{
	scope_t tmpScope, *sc;

	// sink VU-meters first
	for (int32_t i = 0; i < AMIGA_VOICES; i++)
	{
		editor.realVuMeterVolumes[i] -= 3;
		if (editor.realVuMeterVolumes[i] < 0)
			editor.realVuMeterVolumes[i] = 0;
	}

	// get peak sample data from running scope voices
	sc = scope;
	for (int32_t i = 0; i < AMIGA_VOICES; i++, sc++)
	{
		tmpScope = *sc; // cache it

		if (!tmpScope.active || tmpScope.data == NULL || tmpScope.volume == 0 || tmpScope.length == 0)
			continue;

		int32_t samplesToScan = (int32_t)tmpScope.fDelta; // amount of integer samples getting skipped every frame
		if (samplesToScan <= 0)
			continue;

		int32_t pos = tmpScope.pos;
		int32_t length = tmpScope.length;
		const int8_t *data = tmpScope.data;

		int32_t runningAmplitude = 0;
		for (int32_t x = 0; x < samplesToScan; x++)
		{
			int32_t amplitude = 0;
			if (data != NULL)
				amplitude = data[pos] * tmpScope.volume;

			runningAmplitude += ABS(amplitude);

			if (++pos >= length)
			{
				pos = 0;

				/* Read cycle done, temporarily update the display data/length variables
				** before the scope thread does it.
				*/
				data = tmpScope.newData;
				length = tmpScope.newLength;
			}
		}

		float fAvgAmplitude = runningAmplitude / (float)samplesToScan;

		fAvgAmplitude *= 96.0f / (128.0f * 64.0f); // normalize

		int32_t vuHeight = (int32_t)(fAvgAmplitude + 0.5f); // rounded
		if (vuHeight > 48) // max VU-meter height
			vuHeight = 48;

		if ((int8_t)vuHeight > editor.realVuMeterVolumes[i])
			editor.realVuMeterVolumes[i] = (int8_t)vuHeight;
	}
}

void drawScopes(void)
{
	int16_t scopeData;
	int32_t i, x;
	uint32_t *scopeDrawPtr;
	volatile scope_t *sc;
	scope_t tmpScope;

	scopeDrawPtr = &video.frameBuffer[(71 * SCREEN_W) + 128];

	const uint32_t fgColor = video.palette[PAL_QADSCP];

	sc = scope;

	scopesDisplayingFlag = true;
	for (i = 0; i < AMIGA_VOICES; i++, sc++)
	{
		tmpScope = *sc; // cache it

		// render scope
		if (tmpScope.active && tmpScope.data != NULL && tmpScope.volume != 0 && tmpScope.length > 0)
		{
			// scope is active

			sc->emptyScopeDrawn = false;

			// fill scope background
			fillRect(128 + (i * (SCOPE_WIDTH + 8)), 55, SCOPE_WIDTH, SCOPE_HEIGHT, video.palette[PAL_BACKGRD]);

			// render scope data

			int32_t pos = tmpScope.pos;
			int32_t length = tmpScope.length;
			const int16_t volume = -(tmpScope.volume << 7);
			const int8_t *data = tmpScope.data;

			for (x = 0; x < SCOPE_WIDTH; x++)
			{
				scopeData = 0;
				if (data != NULL)
					scopeData = (data[pos] * volume) >> 16;

				scopeDrawPtr[(scopeData * SCREEN_W) + x] = fgColor;

				pos++;
				if (pos >= length)
				{
					pos = 0;

					/* Read cycle done, temporarily update the display data/length variables
					** before the scope thread does it.
					*/
					length = tmpScope.newLength;
					data = tmpScope.newData;
				}
			}
		}
		else
		{
			// scope is inactive, draw empty scope once until it gets active again

			if (!sc->emptyScopeDrawn)
			{
				// fill scope background
				fillRect(128 + (i * (SCOPE_WIDTH + 8)), 55, SCOPE_WIDTH, SCOPE_HEIGHT, video.palette[PAL_BACKGRD]);

				// draw scope line
				for (x = 0; x < SCOPE_WIDTH; x++)
					scopeDrawPtr[x] = fgColor;

				sc->emptyScopeDrawn = true;
			}
		}

		scopeDrawPtr += SCOPE_WIDTH+8;
	}
	scopesDisplayingFlag = false;
}

static int32_t SDLCALL scopeThreadFunc(void *ptr)
{
	// this is needed for scope stability (confirmed)
	SDL_SetThreadPriority(SDL_THREAD_PRIORITY_HIGH);

	// set next frame time
	timeNext64 = SDL_GetPerformanceCounter() + scopeTimeLen;
	timeNext64Frac = scopeTimeLenFrac;

	while (editor.programRunning)
	{
		if (config.realVuMeters)
			updateRealVuMeters();

		updateScopes();

		uint64_t time64 = SDL_GetPerformanceCounter();
		if (time64 < timeNext64)
		{
			time64 = timeNext64 - time64;
			if (time64 > UINT32_MAX)
				time64 = UINT32_MAX;

			const uint32_t diff32 = (uint32_t)time64;

			// convert to microseconds and round to integer
			const int32_t time32 = (int32_t)((diff32 * editor.dPerfFreqMulMicro) + 0.5);

			// delay until we have reached the next frame
			if (time32 > 0)
				usleep(time32);
		}

		// update next tick time
		timeNext64 += scopeTimeLen;
		timeNext64Frac += scopeTimeLenFrac;
		if (timeNext64Frac > 0xFFFFFFFF)
		{
			timeNext64Frac &= 0xFFFFFFFF;
			timeNext64++;
		}
	}

	(void)ptr;
	return true;
}

bool initScopes(void)
{
	double dInt, dFrac;

	// calculate scope time for performance counters and split into int/frac
	dFrac = modf(editor.dPerfFreq / SCOPE_HZ, &dInt);

	// integer part
	scopeTimeLen = (int32_t)dInt;

	// fractional part scaled to 0..2^32-1
	dFrac *= UINT32_MAX;
	dFrac += 0.5;
	if (dFrac > UINT32_MAX)
		dFrac = UINT32_MAX;
	scopeTimeLenFrac = (uint32_t)dFrac;

	scopeThread = SDL_CreateThread(scopeThreadFunc, NULL, NULL);
	if (scopeThread == NULL)
	{
		showErrorMsgBox("Couldn't create scope thread!");
		return false;
	}

	SDL_DetachThread(scopeThread);
	return true;
}

void stopScope(int32_t ch)
{
	// wait for scopes to finish updating
	while (scopesUpdatingFlag);

	scope[ch].active = false;

	// wait for scope displaying to be done (safety)
	while (scopesDisplayingFlag);
}

void stopAllScopes(void)
{
	// wait for scopes to finish updating
	while (scopesUpdatingFlag);

	for (int32_t i = 0; i < AMIGA_VOICES; i++)
		scope[i].active = false;

	// wait for scope displaying to be done (safety)
	while (scopesDisplayingFlag);
}
