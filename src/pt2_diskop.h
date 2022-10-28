#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "pt2_header.h"

enum
{
	DISKOP_NO_CACHE = 0,
	DISKOP_CACHE = 1
};

#define DISKOP_LINES 10

void addSampleFileExt(char *fileName);
void diskOpShowSelectText(void);
void diskOpLoadFile(uint32_t fileEntryRow, bool songModifiedCheck);
void handleEntryJumping(SDL_Keycode jumpToChar);
bool diskOpEntryIsEmpty(int32_t fileIndex);
bool diskOpEntryIsDir(int32_t fileIndex);
char *diskOpGetAnsiEntry(int32_t fileIndex);
UNICHAR *diskOpGetUnicodeEntry(int32_t fileIndex);
bool diskOpSetPath(UNICHAR *path, bool cache);
void diskOpSetInitPath(void);
void diskOpRenderFileList(void);
bool allocDiskOpVars(void);
void freeDiskOpMem(void);
void freeDiskOpEntryMem(void);
void setPathFromDiskOpMode(void);
bool changePathToDesktop(void);
#ifndef _WIN32
bool changePathToHome(void);
#endif
void renderDiskOpScreen(void);
void updateDiskOp(void);