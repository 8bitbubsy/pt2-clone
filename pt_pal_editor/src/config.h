#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "unicode.h"

void loadColorsDotIni(void);
void loadPTDotConfig(void);
bool savePalette(bool showNotes);

extern UNICHAR *loadedFile;
extern bool configIsSaved;
