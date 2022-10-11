#pragma once

#include <stdbool.h>

bool saveModule(bool checkIfFileExist, bool giveNewFreeFilename);

bool modSave(char *fileName); // used by saveModule() and crash handler
