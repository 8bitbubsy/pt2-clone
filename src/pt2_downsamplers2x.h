#pragma once

#include <stdint.h>

// all-pass halfband filters

// Warning: These can exceed -1.0 .. 1.0 because of undershoot/overshoot!

void downsample2xFloat(float *buffer, int32_t originalLength);
void downsample2xDouble(double *buffer, int32_t originalLength);

// Warning: These are slow and use normalization to prevent clipping from undershoot/overshoot!

void downsample2x8Bit(int8_t *buffer, int32_t originalLength);
void downsample2x8BitU(uint8_t *buffer, int32_t originalLength);
void downsample2x16Bit(int16_t *buffer, int32_t originalLength);
void downsample2x32Bit(int32_t *buffer, int32_t originalLength);
