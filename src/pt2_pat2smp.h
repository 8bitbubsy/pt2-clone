#pragma once

#include "pt2_header.h"

#define PAT2SMP_HI_PERIOD 124 /* A-3 finetune +4, 28603.99Hz */
#define PAT2SMP_LO_PERIOD 160 /* F-3 finetune +1, 22168.09Hz */

#define PAT2SMP_HI_FREQ (PAULA_PAL_CLK / (double)PAT2SMP_HI_PERIOD)
#define PAT2SMP_LO_FREQ (PAULA_PAL_CLK / (double)PAT2SMP_LO_PERIOD)

void doPat2Smp(void);
