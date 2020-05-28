#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "pt2_header.h" // AMIGA_VOICES

enum // flags
{
	UPDATE_VOLUME = 1,
	UPDATE_PERIOD = 2,
	TRIGGER_SAMPLE = 4,
	UPDATE_DATA = 8,
	UPDATE_LENGTH = 16,
	UPDATE_VUMETER = 32,
	UPDATE_ANALYZER = 64
};

// 2^n-1 - don't change this! Queue buffer is already ~1MB in size
#define SYNC_QUEUE_LEN 8191

typedef struct syncedChannel_t
{
	uint8_t flags;
	const int8_t *triggerData, *newData;
	uint16_t triggerLength, newLength;
	uint8_t volume, vuVolume, analyzerVolume;
	uint16_t period, analyzerPeriod;
} syncedChannel_t;

typedef struct chSyncData_t
{
	syncedChannel_t channels[AMIGA_VOICES];
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

