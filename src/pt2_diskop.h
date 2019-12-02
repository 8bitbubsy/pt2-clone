#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "pt2_unicode.h"

enum
{
	DISKOP_NO_CACHE = 0,
	DISKOP_CACHE = 1
};

#define DISKOP_LINES 10

void diskOpShowSelectText(void);
void diskOpLoadFile(uint32_t fileEntryRow, bool songModifiedCheck);
void diskOpLoadFile2(void);
void handleEntryJumping(char jumpToChar);
bool diskOpEntryIsEmpty(int32_t fileIndex);
bool diskOpEntryIsDir(int32_t fileIndex);
char *diskOpGetAnsiEntry(int32_t fileIndex);
UNICHAR *diskOpGetUnicodeEntry(int32_t fileIndex);
bool diskOpSetPath(UNICHAR *path, bool cache);
void diskOpSetInitPath(void);
void diskOpRenderFileList(uint32_t *frameBuffer);
bool allocDiskOpVars(void);
void freeDiskOpMem(void);
void freeDiskOpEntryMem(void);
void setPathFromDiskOpMode(void);
bool changePathToHome(void);
