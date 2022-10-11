#include <stdint.h>
#include <stdbool.h>
#include "pt2_sync.h"
#include "pt2_scopes.h"
#include "pt2_visuals.h"
#include "pt2_tables.h"

static volatile bool chQueueClearing;

chSyncData_t *chSyncEntry; // globalized
chSync_t chSync; // globalized

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

void updateChannelSyncBuffer(void)
{
	uint8_t updateFlags[PAULA_VOICES];

	chSyncEntry = NULL;

	memset(updateFlags, 0, sizeof (updateFlags)); // this is needed

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

			if (flags & SET_SCOPE_DATA)
				scope[ch].newData = c->newData;

			if (flags & SET_SCOPE_LENGTH)
				scope[ch].newLength = c->newLength;

			if (flags & STOP_SCOPE) // this must be handled *before* TRIGGER_SCOPE
				scope[ch].active = false;

			if (flags & TRIGGER_SCOPE)
			{
				s->newData = c->triggerData;
				s->newLength = c->triggerLength;
				scopeTrigger(ch);
			}

			if (flags & UPDATE_ANALYZER)
				updateSpectrumAnalyzer(c->analyzerVolume, c ->analyzerPeriod);

			if (flags & UPDATE_VUMETER) // for fake VU-meters only
			{
				if (c->vuVolume <= 64)
					editor.vuMeterVolumes[ch] = vuMeterHeights[c->vuVolume];
			}
		}
	}
}
