/* These are second variants of low-pass/high-pass filters that are better than
** the ones used in the main audio mixer. The reason we use a different one for
** the main audio mixer is because it makes it sound closer to real Amigas.
**
** These ones are used for filtering samples when loading samples, or with the
** FILTERS toolbox in the Sample Editor.
*/

#pragma once

#include <stdio.h>
#include <stdbool.h>

/* 8bitbubsy: Before we downsample a loaded WAV/AIFF (>22kHz) sample by 2x, we low-pass
** filter it.
**
*** I think this value ought to be 4.0 (nyquist freq. / 2), but it cuts off too much in
** my opinion! The improvement is only noticable on samples that has quite a bit of high
** frequencies in them to begin with.
**
** This is probably not how to do it, so if someone with a bit more knowledge can do this
** in a proper way without using an external resampler library, that would be neato!
*/
#define DOWNSAMPLE_CUTOFF_FACTOR 4.0

bool lowPassSample8Bit(int8_t *buffer, int32_t length, int32_t sampleFrequency, double cutoff);
bool lowPassSample8BitUnsigned(uint8_t *buffer, int32_t length, int32_t sampleFrequency, double cutoff);
bool lowPassSample16Bit(int16_t *buffer, int32_t length, int32_t sampleFrequency, double cutoff);
bool lowPassSample32Bit(int32_t *buffer, int32_t length, int32_t sampleFrequency, double cutoff);
bool lowPassSampleFloat(float *buffer, int32_t length, int32_t sampleFrequency, double cutoff);
bool lowPassSampleDouble(double *buffer, int32_t length, int32_t sampleFrequency, double cutoff);
