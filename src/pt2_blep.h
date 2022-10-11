// these BLEP routines were coded by aciddose

#pragma once

#include <stdint.h>
#include "pt2_paula.h" // PAULA_VOICES

/* aciddose:
** information on blep variables
**
** ZC = zero crossings, the number of ripples in the impulse
** OS = oversampling, how many samples per zero crossing are taken
** SP = step size per output sample, used to lower the cutoff (play the impulse slower)
** NS = number of samples of impulse to insert
** RNS = the lowest power of two greater than NS, minus one (used to wrap output buffer)
**
** ZC and OS are here only for reference, they depend upon the data in the table and can't be changed.
** SP, the step size can be any number lower or equal to OS, as long as the result NS remains an integer.
** for example, if ZC=8,OS=5, you can set SP=1, the result is NS=40, and RNS must then be 63.
** the result of that is the filter cutoff is set at nyquist * (SP/OS), in this case nyquist/5.
*/

#define BLEP_ZC 16
#define BLEP_OS 16
#define BLEP_SP 16
#define BLEP_NS (BLEP_ZC * BLEP_OS / BLEP_SP)
#define BLEP_RNS 31 // RNS = (2^ > NS) - 1

typedef struct blep_t
{
	int32_t index, samplesLeft;
	double dBuffer[BLEP_RNS + 1], dLastValue;
} blep_t;

void blepAdd(blep_t *b, double dOffset, double dAmplitude);
double blepRun(blep_t *b, double dInput);
