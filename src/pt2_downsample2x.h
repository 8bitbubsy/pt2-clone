#pragma once

#include <stdint.h>

// reserved for main audio channel mixer, PAT2SMP and MOD2WAV
void clearMixerDownsamplerStates(void);
double decimate2x_L(double x0, double x1);
double decimate2x_R(double x0, double x1);
// --------------------------------------

// Warning: These can exceed -1.0 .. 1.0 because of undershoot/overshoot!
void downsample2xFloat(float *buffer, uint32_t originalLength);
void downsample2xDouble(double *buffer, uint32_t originalLength);

// Warning: These are slow and use normalization to prevent clipping from undershoot/overshoot!
bool downsample2x8Bit(int8_t *buffer, uint32_t originalLength);
bool downsample2x8BitU(uint8_t *buffer, uint32_t originalLength);
bool downsample2x16Bit(int16_t *buffer, uint32_t originalLength);
bool downsample2x32Bit(int32_t *buffer, uint32_t originalLength);
