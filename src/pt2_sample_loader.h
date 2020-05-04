#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "pt2_unicode.h"

void extLoadWAVOrAIFFSampleCallback(bool downsample);
bool loadSample(UNICHAR *fileName, char *entryName);
