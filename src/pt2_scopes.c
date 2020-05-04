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

static volatile bool scopesReading;
static uint32_t scopeTimeLen, scopeTimeLenFrac;
static uint64_t timeNext64, timeNext64Frac;
static SDL_Thread *scopeThread;

scopeChannel_t scope[AMIGA_VOICES]; // global
scopeChannelExt_t scopeExt[AMIGA_VOICES]; // global

int32_t getSampleReadPos(int32_t ch, uint8_t smpNum)
{
	const int8_t *data;
	int32_t pos;
	scopeChannel_t *sc;
	moduleSample_t *s;
	
	sc = &scope[ch];

	// cache some stuff
	data = sc->data;
	pos = sc->pos;

	if (scopeExt[ch].active && pos >= 2)
	{
		s = &song->samples[smpNum];

		/* Get real sampling position regardless of where the scope data points to
		** sc->data changes during loop, offset and so on, so this has to be done
		** (sadly, because it's really hackish).
		*/
		pos = (int32_t)(&data[pos] - &song->sampleData[s->offset]);
		if (pos >= s->length)
			return -1;

		return pos;
	}

	return -1;
}

void setScopeDelta(int32_t ch, uint32_t delta)
{
	scope[ch].delta = delta;
}

void updateScopes(void)
{
	scopeChannel_t *sc, tmp;
	scopeChannelExt_t *se, tmpExt;

	if (editor.isWAVRendering)
		return;

	for (int32_t i = 0; i < AMIGA_VOICES; i++)
	{
		sc = &scope[i];
		se = &scopeExt[i];

		// cache these
		tmp = *sc;
		tmpExt = *se;

		if (!tmpExt.active)
			continue; // scope is not active

		tmp.posFrac += tmp.delta;
		tmp.pos += tmp.posFrac >> 16;
		tmp.posFrac &= 0xFFFF;

		if (tmp.pos >= tmp.length)
		{
			// sample reached end, simulate Paula register update (sample swapping)

			/* Wrap pos around one time with current length, then set new length
			** and wrap around it (handles one-shot loops and sample swapping).
			*/
			tmp.pos -= tmp.length;
			tmp.length = tmpExt.newLength;

			if (tmp.length > 0)
				tmp.pos %= tmp.length;

			tmp.data = tmpExt.newData;
			tmp.loopFlag = tmpExt.newLoopFlag;
			tmp.loopStart = tmpExt.newLoopStart;

			se->didSwapData = true;
		}

		*sc = tmp; // update it
	}
}

/* This routine gets the average sample peak through the running scope voices.
** This gives a much more smooth and stable result than getting the peak from
** the mixer, and we don't care about including filters/BLEP in the peak calculation.
*/
static void updateRealVuMeters(void) 
{
	bool didSwapData;
	int16_t volume;
	int32_t i, x, readPos, samplesToScan, smpDat, smpPeak;
	scopeChannel_t tmpScope, *sc;
	scopeChannelExt_t *se;

	// sink VU-meters first
	for (i = 0; i < AMIGA_VOICES; i++)
	{
		editor.realVuMeterVolumes[i] -= 3;
		if (editor.realVuMeterVolumes[i] < 0)
			editor.realVuMeterVolumes[i] = 0;
	}

	// get peak sample data from running scope voices
	for (i = 0; i < AMIGA_VOICES; i++)
	{
		sc = &scope[i];
		se = &scopeExt[i];

		// cache these two
		tmpScope = *sc;
		didSwapData = se->didSwapData;

		samplesToScan = tmpScope.delta >> 16;
		if (samplesToScan <= 0)
			continue;

		if (samplesToScan > 512) // don't waste cycles on reading a ton of samples
			samplesToScan = 512;

		volume = song->channels[i].n_volume;

		if (se->active && tmpScope.data != NULL && volume != 0 && tmpScope.length > 0)
		{
			smpPeak = 0;
			readPos = tmpScope.pos;

			if (tmpScope.loopFlag)
			{
				for (x = 0; x < samplesToScan; x += 2) // loop enabled
				{
					if (didSwapData)
					{
						if (readPos >= tmpScope.length)
							readPos %= tmpScope.length; // s.data = loopStartPtr, wrap readPos to 0
					}
					else if (readPos >= tmpScope.length)
					{
						readPos = tmpScope.loopStart; // s.data = sampleStartPtr, wrap readPos to loop start
					}

					smpDat = tmpScope.data[readPos] * volume;

					smpDat = ABS(smpDat);
					if (smpDat > smpPeak)
						smpPeak = smpDat;

					readPos += 2;
				}
			}
			else
			{
				for (x = 0; x < samplesToScan; x += 2) // no loop
				{
					if (readPos >= tmpScope.length)
						break;

					smpDat = tmpScope.data[readPos] * volume;

					smpDat = ABS(smpDat);
					if (smpDat > smpPeak)
						smpPeak = smpDat;

					readPos += 2;
				}
			}

			smpPeak = ((smpPeak * 48) + (1 << 12)) >> 13; // rounded
			if (smpPeak > editor.realVuMeterVolumes[i])
				editor.realVuMeterVolumes[i] = (int8_t)smpPeak;
		}
	}
}

void drawScopes(void)
{
	bool didSwapData;
	int16_t scopeData, volume;
	int32_t i, x, y, readPos;
	uint32_t *dstPtr, *scopePtr, scopePixel;
	scopeChannel_t tmpScope, *sc;
	scopeChannelExt_t *se;

	scopesReading = true;
	if (ui.visualizerMode == VISUAL_QUADRASCOPE)
	{
		// --- QUADRASCOPE ---

		scopePtr = &video.frameBuffer[(71 * SCREEN_W) + 128];
		for (i = 0; i < AMIGA_VOICES; i++)
		{
			sc = &scope[i];
			se = &scopeExt[i];

			// cache these two
			tmpScope = *sc;
			didSwapData = se->didSwapData;

			volume = -song->channels[i].n_volume; // invert volume

			// render scope
			if (se->active && tmpScope.data != NULL && volume != 0 && tmpScope.length > 0)
			{
				// scope is active

				se->emptyScopeDrawn = false;

				// draw scope background

				dstPtr = &video.frameBuffer[(55 * SCREEN_W) + (128 + (i * (SCOPE_WIDTH + 8)))];
				scopePixel = video.palette[PAL_BACKGRD]; // this palette can change

				for (y = 0; y < SCOPE_HEIGHT; y++)
				{
					for (x = 0; x < SCOPE_WIDTH; x++)
						dstPtr[x] = scopePixel;

					dstPtr += SCREEN_W;
				}

				// render scope data

				scopePixel = video.palette[PAL_QADSCP];

				readPos = tmpScope.pos;
				if (tmpScope.loopFlag)
				{
					// loop enabled

					for (x = 0; x < SCOPE_WIDTH; x++)
					{
						if (didSwapData)
						{
							if (readPos >= tmpScope.length)
								readPos %= tmpScope.length; // s.data = loopStartPtr, wrap readPos to 0
						}
						else if (readPos >= tmpScope.length)
						{
							readPos = tmpScope.loopStart; // s.data = sampleStartPtr, wrap readPos to loop start
						}

						scopeData = (tmpScope.data[readPos++] * volume) >> 9; // (-128..127)*(-64..0) / 2^9 = -15..16
						scopePtr[(scopeData * SCREEN_W) + x] = scopePixel;
					}
				}
				else
				{
					// no loop

					for (x = 0; x < SCOPE_WIDTH; x++)
					{
						if (readPos >= tmpScope.length)
						{
							scopePtr[x] = scopePixel; // end of data, draw center pixel
						}
						else
						{
							scopeData = (tmpScope.data[readPos++] * volume) >> 9; // (-128..127)*(-64..0) / 2^9 = -15..16
							scopePtr[(scopeData * SCREEN_W) + x] = scopePixel;
						}
					}
				}
			}
			else
			{
				// scope is inactive, draw empty scope once until it gets active again

				if (!se->emptyScopeDrawn)
				{
					// draw scope background

					dstPtr = &video.frameBuffer[(55 * SCREEN_W) + (128 + (i * (SCOPE_WIDTH + 8)))];
					scopePixel = video.palette[PAL_BACKGRD];

					for (y = 0; y < SCOPE_HEIGHT; y++)
					{
						for (x = 0; x < SCOPE_WIDTH; x++)
							dstPtr[x] = scopePixel;

						dstPtr += SCREEN_W;
					}

					// draw line

					scopePixel = video.palette[PAL_QADSCP];
					for (x = 0; x < SCOPE_WIDTH; x++)
						scopePtr[x] = scopePixel;

					se->emptyScopeDrawn = true;
				}
			}

			scopePtr += SCOPE_WIDTH+8;
		}
	}
	scopesReading = false;
}

static int32_t SDLCALL scopeThreadFunc(void *ptr)
{
	int32_t time32;
	uint32_t diff32;
	uint64_t time64;

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

		time64 = SDL_GetPerformanceCounter();
		if (time64 < timeNext64)
		{
			assert(timeNext64-time64 <= 0xFFFFFFFFULL);
			diff32 = (uint32_t)(timeNext64 - time64);

			// convert to microseconds and round to integer
			time32 = (int32_t)((diff32 * editor.dPerfFreqMulMicro) + 0.5);

			// delay until we have reached next tick
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
	while (scopesReading);
	memset(&scopeExt[ch], 0, sizeof (scopeChannelExt_t));

	while (scopesReading);
	memset(&scope[ch], 0, sizeof (scopeChannel_t));

	scope[ch].length = scopeExt[ch].newLength = 2;
	while (scopesReading); // final wait to make sure scopes are all inactive
}

void stopAllScopes(void)
{
	for (int32_t i = 0; i < AMIGA_VOICES; i++)
		stopScope(i);
}
