#pragma once

#include "pt2_header.h"

#define PAT2SMP_HI_PERIOD 124 /* A-3 finetune +4, 28603.99Hz */
#define PAT2SMP_LO_PERIOD 170 /* E-3 finetune  0, 20864.08Hz */

#define PAT2SMP_HI_FREQ ((double)PAULA_PAL_CLK / PAT2SMP_HI_PERIOD)
#define PAT2SMP_LO_FREQ ((double)PAULA_PAL_CLK / PAT2SMP_LO_PERIOD)

void doPat2Smp(void);
