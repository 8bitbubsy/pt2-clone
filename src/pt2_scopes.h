#pragma once

#include <stdint.h>
#include <stdbool.h>

typedef struct scopeChannel_t // internal scope state
{
	const int8_t *data;
	bool loopFlag;
	int32_t length, pos, loopStart;
	uint32_t delta, posFrac;
} scopeChannel_t;

typedef struct scopeChannelExt // external scope state
{
	const int8_t *newData;
	volatile bool active, didSwapData;
	bool emptyScopeDrawn, newLoopFlag;
	int32_t newLength, newLoopStart;
} scopeChannelExt_t;

void setScopeDelta(uint8_t ch, uint32_t delta);
int32_t getSampleReadPos(uint8_t ch, uint8_t smpNum);
void updateScopes(void);
void drawScopes(void);
bool initScopes(void);
void waitOnScopes(void);
void clearScopes(void);

extern scopeChannel_t scope[4];
extern scopeChannelExt_t scopeExt[4];
