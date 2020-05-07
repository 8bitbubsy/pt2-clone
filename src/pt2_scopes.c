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
static uint32_t scopeTimeLen, scopeTimeLenFrac;
static uint64_t timeNext64, timeNext64Frac;
static SDL_Thread *scopeThread;

scope_t scope[AMIGA_VOICES]; // global

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

void scopeTrigger(int32_t ch, int32_t length)
{
	volatile scope_t *sc = &scope[ch];
	scope_t tempState = *sc; // cache it

	const int8_t *newData = tempState.newData;
	if (newData == NULL)
		newData = &song->sampleData[RESERVED_SAMPLE_OFFSET]; // dummy sample

	if (length < 2)
	{
		sc->active = false;
		return;
	}

	tempState.posFrac = 0;
	tempState.pos = 0;
	tempState.data = newData;
	tempState.length = length;
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

		tempState.posFrac += tempState.delta;
		tempState.pos += tempState.posFrac >> SCOPE_FRAC_BITS;
		tempState.posFrac &= SCOPE_FRAC_MASK;

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

		int32_t samplesToScan = tmpScope.delta >> SCOPE_FRAC_BITS; // amount of integer samples getting skipped every frame
		if (samplesToScan <= 0)
			continue;

		// shouldn't happen (low period 113 -> samplesToScan=490), but let's not waste cycles if it does
		if (samplesToScan > 512)
			samplesToScan = 512;

		int32_t pos = tmpScope.pos;
		int32_t length = tmpScope.length;
		const int8_t *data = tmpScope.data;

		int32_t runningAmplitude = 0;
		for (int32_t x = 0; x < samplesToScan; x++)
		{
			int16_t amplitude = 0;
			if (data != NULL)
				amplitude = data[pos] * tmpScope.volume;

			runningAmplitude += ABS(amplitude);

			pos++;
			if (pos >= length)
			{
				pos = 0;

				/* Read cycle done, temporarily update the display data/length variables
				** before the scope thread does it.
				*/
				data = tmpScope.newData;
				length = tmpScope.newLength;
			}
		}

		double dAvgAmplitude = runningAmplitude / (double)samplesToScan;

		dAvgAmplitude *= (96.0 / (128.0 * 64.0)); // normalize

		int32_t vuHeight = (int32_t)dAvgAmplitude;
		if (vuHeight > 48) // max VU-meter height
			vuHeight = 48;

		if ((int8_t)vuHeight > editor.realVuMeterVolumes[i])
			editor.realVuMeterVolumes[i] = (int8_t)vuHeight;
	}
}

void drawScopes(void)
{
	int16_t scopeData;
	int32_t i, x, y;
	uint32_t *dstPtr, *scopeDrawPtr;
	volatile scope_t *sc;
	scope_t tmpScope;

	scopeDrawPtr = &video.frameBuffer[(71 * SCREEN_W) + 128];

	const uint32_t bgColor = video.palette[PAL_BACKGRD];
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
			dstPtr = &video.frameBuffer[(55 * SCREEN_W) + (128 + (i * (SCOPE_WIDTH + 8)))];
			for (y = 0; y < SCOPE_HEIGHT; y++)
			{
				for (x = 0; x < SCOPE_WIDTH; x++)
					dstPtr[x] = bgColor;

				dstPtr += SCREEN_W;
			}

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
				dstPtr = &video.frameBuffer[(55 * SCREEN_W) + (128 + (i * (SCOPE_WIDTH + 8)))];
				for (y = 0; y < SCOPE_HEIGHT; y++)
				{
					for (x = 0; x < SCOPE_WIDTH; x++)
						dstPtr[x] = bgColor;

					dstPtr += SCREEN_W;
				}

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
