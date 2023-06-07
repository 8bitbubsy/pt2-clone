// used for syncing audio from Paula writes to tracker visuals

#include <stdint.h>
#include <stdbool.h>
#include "pt2_audio.h"
#include "pt2_visuals_sync.h"
#include "pt2_scopes.h"
#include "pt2_visuals.h"
#include "pt2_tables.h"

typedef struct syncVoice_t
{
	const int8_t *newData, *data;
	uint8_t flags, volume;
	uint16_t period;
	uint16_t newLength, length;
} syncVoice_t;

static volatile bool chQueueClearing;
static uint32_t audLatencyPerfValInt;
static uint64_t audLatencyPerfValFrac;
static uint64_t tickTime64, tickTime64Frac;
static uint32_t tickTimeLenInt;
static uint64_t tickTimeLenFrac;
static syncVoice_t syncVoice[PAULA_VOICES];
static chSyncData_t *chSyncEntry;
static chSync_t chSync;

static void startDMA(int32_t ch)
{
	syncVoice_t *sv = &syncVoice[ch];

	if (editor.songPlaying)
	{
		sv->data = sv->newData;
		sv->length = sv->newLength;
		sv->flags |= TRIGGER_SCOPE;
	}
	else
	{
		scope_t *s = &scope[ch];
		s->data = sv->newData;
		s->length = sv->newLength * 2;
		scopeTrigger(ch);
	}
}

static void stopDMA(int32_t ch)
{
	if (editor.songPlaying)
		syncVoice[ch].flags |= STOP_SCOPE;
	else
		scope[ch].active = false;
}

void setVisualsDMACON(uint16_t bits)
{
	if (bits & 0x8000)
	{
		// set
		if (bits & 1) startDMA(0);
		if (bits & 2) startDMA(1);
		if (bits & 4) startDMA(2);
		if (bits & 8) startDMA(3);
	}
	else
	{
		// clear
		if (bits & 1) stopDMA(0);
		if (bits & 2) stopDMA(1);
		if (bits & 4) stopDMA(2);
		if (bits & 8) stopDMA(3);
	}
}

void setVisualsVolume(int32_t ch, uint16_t vol)
{
	int32_t realVol = vol & 127;
	if (realVol > 64)
		realVol = 64;

	if (editor.songPlaying)
	{
		syncVoice_t *sv = &syncVoice[ch];

		sv->volume = (uint8_t)realVol;
		sv->flags |= SET_SCOPE_VOLUME;
	}
	else
	{
		scope[ch].volume = (uint8_t)realVol;
	}
}

void setVisualsPeriod(int32_t ch, uint16_t period)
{
	if (period == 0)
		period = 65535; // On Amiga: period 0 = one full cycle with period 65536, then period 65535 for the rest
	else if (period < 113)
		period = 113; // close to what happens on real Amiga (and needed for BLEP synthesis)

	if (editor.songPlaying)
	{
		syncVoice_t *sv = &syncVoice[ch];

		sv->period = period;
		sv->flags |= SET_SCOPE_PERIOD;
	}
	else
	{
		scopeSetPeriod(ch, period);
	}
}

void setVisualsLength(int32_t ch, uint16_t len)
{
	syncVoice_t *sv = &syncVoice[ch];

	sv->newLength = len;

	if (editor.songPlaying)
		sv->flags |= SET_SCOPE_LENGTH;
	else
		scope[ch].newLength = len * 2;
}

void setVisualsDataPtr(int32_t ch, const int8_t *src)
{
	syncVoice_t *sv = &syncVoice[ch];

	if (src == NULL)
		src = paulaGetNullSamplePtr();

	sv->newData = src;

	if (editor.songPlaying)
		sv->flags |= SET_SCOPE_DATA;
	else
		scope[ch].newData = src;
}

void calcAudioLatencyVars(int32_t audioBufferSize, int32_t audioFreq)
{
	double dInt;

	if (audioFreq == 0)
		return;

	const double dAudioLatencySecs = audioBufferSize / (double)audioFreq;

	double dFrac = modf(dAudioLatencySecs * (double)hpcFreq.freq64, &dInt);

	audLatencyPerfValInt = (uint32_t)dInt;
	audLatencyPerfValFrac = (uint64_t)((dFrac * TICK_TIME_FRAC_SCALE) + 0.5); // rounded
}

void setSyncTickTimeLen(uint32_t timeLenInt, uint64_t timeLenFrac)
{
	tickTimeLenInt = timeLenInt;
	tickTimeLenFrac = timeLenFrac;
}

void resetChSyncQueue(void)
{
	chSync.data[0].timestamp = 0;
	chSync.writePos = 0;
	chSync.readPos = 0;
}

static int32_t chQueueReadSize(void)
{
	while (chQueueClearing);

	if (chSync.writePos > chSync.readPos)
		return chSync.writePos - chSync.readPos;
	else if (chSync.writePos < chSync.readPos)
		return chSync.writePos - chSync.readPos + SYNC_QUEUE_LEN + 1;
	else
		return 0;
}

static int32_t chQueueWriteSize(void)
{
	int32_t size;

	if (chSync.writePos > chSync.readPos)
	{
		size = chSync.readPos - chSync.writePos + SYNC_QUEUE_LEN;
	}
	else if (chSync.writePos < chSync.readPos)
	{
		chQueueClearing = true;

		/* Buffer is full, reset the read/write pos. This is actually really nasty since
		** read/write are two different threads, but because of timestamp validation it
		** shouldn't be that dangerous.
		** It will also create a small visual stutter while the buffer is getting filled,
		** though that is barely noticable on normal buffer sizes, and it takes a minute
		** or two at max BPM between each time (when queue size is default, 8191)
		*/
		chSync.data[0].timestamp = 0;
		chSync.readPos = 0;
		chSync.writePos = 0;

		size = SYNC_QUEUE_LEN;

		chQueueClearing = false;
	}
	else
	{
		size = SYNC_QUEUE_LEN;
	}

	return size;
}

bool chQueuePush(chSyncData_t t)
{
	if (!chQueueWriteSize())
		return false;

	assert(chSync.writePos <= SYNC_QUEUE_LEN);
	chSync.data[chSync.writePos] = t;
	chSync.writePos = (chSync.writePos + 1) & SYNC_QUEUE_LEN;

	return true;
}

static bool chQueuePop(void)
{
	if (!chQueueReadSize())
		return false;

	chSync.readPos = (chSync.readPos + 1) & SYNC_QUEUE_LEN;
	assert(chSync.readPos <= SYNC_QUEUE_LEN);

	return true;
}

static chSyncData_t *chQueuePeek(void)
{
	if (!chQueueReadSize())
		return NULL;

	assert(chSync.readPos <= SYNC_QUEUE_LEN);
	return &chSync.data[chSync.readPos];
}

static uint64_t getChQueueTimestamp(void)
{
	if (!chQueueReadSize())
		return 0;

	assert(chSync.readPos <= SYNC_QUEUE_LEN);
	return chSync.data[chSync.readPos].timestamp;
}

void fillVisualsSyncBuffer(void)
{
	chSyncData_t chSyncData;

	if (audio.resetSyncTickTimeFlag)
	{
		audio.resetSyncTickTimeFlag = false;

		tickTime64 = SDL_GetPerformanceCounter() + audLatencyPerfValInt;
		tickTime64Frac = audLatencyPerfValFrac;
	}

	if (song != NULL)
	{
		moduleChannel_t *ch = song->channels;
		syncVoice_t *sv = syncVoice;
		syncedChannel_t *sc = chSyncData.channels;

		for (int32_t i = 0; i < PAULA_VOICES; i++, ch++, sc++, sv++)
		{
			sc->flags = sv->flags | ch->syncFlags;
			ch->syncFlags = sv->flags = 0; // clear sync flags

			sc->volume = sv->volume;
			sc->period = sv->period;
			sc->data = sv->data;
			sc->length = sv->length;
			sc->newData = sv->newData;
			sc->newLength = sv->newLength;
			sc->vuVolume = ch->syncVuVolume;
			sc->analyzerVolume = ch->syncAnalyzerVolume;
			sc->analyzerPeriod = ch->syncAnalyzerPeriod;
		}

		chSyncData.timestamp = tickTime64;
		chQueuePush(chSyncData);
	}

	tickTime64 += tickTimeLenInt;

	tickTime64Frac += tickTimeLenFrac;
	if (tickTime64Frac >= TICK_TIME_FRAC_SCALE)
	{
		tickTime64Frac &= TICK_TIME_FRAC_MASK;
		tickTime64++;
	}
}

void updateChannelSyncBuffer(void)
{
	uint8_t updateFlags[PAULA_VOICES];

	chSyncEntry = NULL;

	*(uint32_t *)updateFlags = 0; // clear all channel update flags (this is needed)

	const uint64_t frameTime64 = SDL_GetPerformanceCounter();

	// handle channel sync queue

	while (chQueueClearing);
	while (chQueueReadSize() > 0)
	{
		if (frameTime64 < getChQueueTimestamp())
			break; // we have no more stuff to render for now

		chSyncEntry = chQueuePeek();
		if (chSyncEntry == NULL)
			break;

		for (int32_t i = 0; i < PAULA_VOICES; i++)
			updateFlags[i] |= chSyncEntry->channels[i].flags; // yes, OR the status

		if (!chQueuePop())
			break;
	}

	/* Extra validation because of possible issues when the buffer is full
	** and positions are being reset, which is not entirely thread safe.
	*/
	if (chSyncEntry != NULL && chSyncEntry->timestamp == 0)
		chSyncEntry = NULL;

	// do actual updates
	if (chSyncEntry != NULL)
	{
		scope_t *s = scope;
		syncedChannel_t *c = chSyncEntry->channels;
		for (int32_t ch = 0; ch < PAULA_VOICES; ch++, s++, c++)
		{
			const uint8_t flags = updateFlags[ch];
			if (flags == 0)
				continue;

			if (flags & SET_SCOPE_VOLUME)
				scope[ch].volume = c->volume;

			if (flags & SET_SCOPE_PERIOD)
				scopeSetPeriod(ch, c->period);

			// the following handling order is important, don't change it!

			if (flags & STOP_SCOPE)
				scope[ch].active = false;

			if (flags & TRIGGER_SCOPE)
			{
				s->data = c->data;
				s->length = c->length * 2;
				scopeTrigger(ch);
			}

			if (flags & SET_SCOPE_DATA)
				scope[ch].newData = c->newData;

			if (flags & SET_SCOPE_LENGTH)
				scope[ch].newLength = c->newLength * 2;

			// ---------------------------------------------------------------

			if (flags & UPDATE_SPECTRUM_ANALYZER)
				updateSpectrumAnalyzer(c->analyzerVolume, c ->analyzerPeriod);

			if (flags & UPDATE_VUMETER) // for fake VU-meters only
			{
				if (c->vuVolume <= 64)
					editor.vuMeterVolumes[ch] = vuMeterHeights[c->vuVolume];
			}
		}
	}
}
