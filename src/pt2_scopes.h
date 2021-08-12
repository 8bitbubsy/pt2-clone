#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "pt2_header.h"
#include "pt2_structs.h"

typedef struct scope_t
{
	const int8_t *data;
	bool active, emptyScopeDrawn;
	uint8_t volume;
	int32_t length, pos;

	// cache
	int32_t oldPeriod;
	double dOldScopeDelta;

	double dDelta, dPhase;
	const int8_t *newData;
	int32_t newLength;
} scope_t;

void resetCachedScopePeriod(void);

void scopeSetPeriod(int32_t ch, int32_t period);
void scopeTrigger(int32_t ch);

int32_t getSampleReadPos(int32_t ch);
void updateScopes(void);
void drawScopes(void);
bool initScopes(void);
void stopScope(int32_t ch);
void stopAllScopes(void);

extern scope_t scope[AMIGA_VOICES]; // pt2_scopes.c
