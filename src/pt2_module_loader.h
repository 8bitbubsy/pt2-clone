#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "pt2_header.h"
#include "pt2_unicode.h"
#include "pt2_structs.h"

void loadModFromArg(char *arg);
void loadDroppedFile(char *fullPath, uint32_t fullPathLen, bool autoPlay, bool songModifiedCheck);
module_t *modLoad(UNICHAR *fileName);
void setupLoadedMod(void);
