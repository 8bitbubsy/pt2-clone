#pragma once

#include <stdint.h>
#include <stdbool.h>

#if defined __amd64__ || defined __i386__ || defined _WIN64 || defined _WIN32
#define SINC_TAPS 512
#else
#define SINC_TAPS 128
#endif

bool initSinc(double dCutoff); // 0.0 .. 0.999
double sinc(int16_t *smpPtr16, double dFrac);
void freeSinc(void);
