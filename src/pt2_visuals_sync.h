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
	UPDATE_SPECTRUM_ANALYZER = 128
};

// 2^n-1 - don't change this! Total queue buffer length is already big.
#define SYNC_QUEUE_LEN 8191

#ifdef _MSC_VER
#pragma pack(push)
#pragma pack(1)
#endif
typedef struct syncedChannel_t // pack to save RAM
{
	const int8_t *data, *newData;
	uint16_t length, newLength;
	uint16_t period, analyzerPeriod;
	uint8_t flags;
	uint8_t volume, analyzerVolume, vuVolume;
}
#ifdef __GNUC__
__attribute__ ((packed))
#endif
syncedChannel_t;
#ifdef _MSC_VER
#pragma pack(pop)
#endif

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

void calcAudioLatencyVars(int32_t audioBufferSize, int32_t audioFreq);
void setSyncTickTimeLen(uint32_t timeLenInt, uint64_t timeLenFrac);
void fillVisualsSyncBuffer(void);
void resetChSyncQueue(void);
void updateChannelSyncBuffer(void);

void setVisualsDMACON(uint16_t bits);
void setVisualsVolume(int32_t ch, uint16_t vol);
void setVisualsPeriod(int32_t ch, uint16_t period);
void setVisualsLength(int32_t ch, uint16_t len);
void setVisualsDataPtr(int32_t ch, const int8_t *src);
