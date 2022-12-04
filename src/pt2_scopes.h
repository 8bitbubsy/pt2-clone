#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "pt2_header.h"
#include "pt2_structs.h"

typedef struct scope_t
{
	volatile bool active;
	const int8_t *data, *newData;
	uint8_t volume;
	int32_t length, newLength;
	int32_t pos;
	double dDelta, dPhase;
} scope_t;

void scopeSetPeriod(int32_t ch, int32_t period);
void scopeTrigger(int32_t ch);

int32_t getSampleReadPos(int32_t ch);
void updateScopes(void);
void drawScopes(void);
bool initScopes(void);
void stopScope(int32_t ch);
void stopAllScopes(void);

extern scope_t scope[PAULA_VOICES]; // pt2_scopes.c
