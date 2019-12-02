#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "pt2_header.h"
#include "pt2_unicode.h"

void showSongUnsavedAskBox(int8_t askScreenType);
void loadModFromArg(char *arg);
void loadDroppedFile(char *fullPath, uint32_t fullPathLen, bool autoPlay, bool songModifiedCheck);
void loadDroppedFile2(void);
module_t *createNewMod(void);
bool saveModule(bool checkIfFileExist, bool giveNewFreeFilename);
bool modSave(char *fileName);
module_t *modLoad(UNICHAR *fileName);
void setupNewMod(void);
