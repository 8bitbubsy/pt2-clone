#pragma once

#include <stdbool.h>

#define MOD2WAV_FREQ 96000

bool renderToWav(char *fileName, bool checkIfFileExist);
