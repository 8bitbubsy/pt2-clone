#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "pt2_paula.h"

enum // flags
{
	SET_SCOPE_VOLUME = 1,
	SET_SCOPE_PERIOD = 2,
	SET_SCOPE_DATA = 4,
	SET_SCOPE_LENGTH = 8,
	TRIGGER_SCOPE = 16,
	STOP_SCOPE = 32,

	UPDATE_VUMETER = 64,
	UPDATE_ANALYZER = 128
};

// 2^n-1 - don't change this! Queue buffer is already ~1MB in size
#define SYNC_QUEUE_LEN 8191

typedef struct syncedChannel_t
{
	uint8_t flags;
	const int8_t *triggerData, *newData;
	int32_t triggerLength, newLength;
	uint8_t volume, vuVolume, analyzerVolume;
	uint16_t analyzerPeriod;
	int32_t period;
} syncedChannel_t;

typedef struct chSyncData_t
{
	syncedChannel_t channels[PAULA_VOICES];
	uint64_t timestamp;
} chSyncData_t;

typedef struct chSync_t
{
	volatile int32_t readPos, writePos;
	chSyncData_t data[SYNC_QUEUE_LEN + 1];
} chSync_t;

void resetChSyncQueue(void);
bool chQueuePush(chSyncData_t t);
void updateChannelSyncBuffer(void);

extern chSyncData_t *chSyncEntry; // pt2_sync.c
extern chSync_t chSync; // pt2_sync.c

