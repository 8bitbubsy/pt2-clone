#pragma once

#include "pt2_header.h"

// we do 2x oversampling for BLEP synthesis to work right on all ProTracker pitches

#define PAT2SMP_HI_PERIOD 124 /* A-3 finetune +4, 28603.99Hz */
#define PAT2SMP_LO_PERIOD 170 /* E-3 finetune  0, 20864.08Hz */

#define PAT2SMP_HI_FREQ (PAULA_PAL_CLK / (PAT2SMP_HI_PERIOD / 2.0))
#define PAT2SMP_LO_FREQ (PAULA_PAL_CLK / (PAT2SMP_LO_PERIOD / 2.0))

void doPat2Smp(void);
