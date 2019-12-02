#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "pt2_unicode.h"

void extLoadWAVOrAIFFSampleCallback(bool downSample);
bool saveSample(bool checkIfFileExist, bool giveNewFreeFilename);
bool loadSample(UNICHAR *fileName, char *entryName);
