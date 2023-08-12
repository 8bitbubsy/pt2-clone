// for finding memory leaks in debug mode with Visual Studio 
#if defined _DEBUG && defined _MSC_VER
#include <crtdbg.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <math.h>
#ifdef _WIN32
#include <direct.h>
#include <io.h>
#define WIN32_MEAN_AND_LEAN
#include <windows.h>
#include <shlobj.h> // SHGetFolderPathW()
#else
#include <unistd.h>
#include <dirent.h>
#endif
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>
#include <limits.h>
#include "pt2_textout.h"
#include "pt2_diskop.h"
#include "pt2_tables.h"
#include "pt2_module_loader.h"
#include "pt2_config.h"
#include "pt2_visuals.h"
#include "pt2_sample_loader.h"
#include "pt2_bmp.h"
#include "pt2_askbox.h"
#include "pt2_replayer.h"

typedef struct fileEntry_t
{
	UNICHAR *nameU;
	char firstAnsiChar; // for handleEntryJumping();
	char dateChanged[6 + 1];
	bool isDir;
	int32_t filesize;
} fileEntry_t;

// "look for file" flags
enum
{
	LFF_DONE = 0,
	LFF_SKIP = 1,
	LFF_OK = 2
};

#ifdef _WIN32
#define PARENT_DIR_STR L".."
static HANDLE hFind;
#else
#define PARENT_DIR_STR ".."
static DIR *hFind;
#endif

static char fileNameBuffer[PATH_MAX + 1];
static UNICHAR pathTmp[PATH_MAX + 2];
static fileEntry_t *diskOpEntry;

void addSampleFileExt(char *fileName)
{
	switch (diskop.smpSaveType)
	{
		case DISKOP_SMP_WAV: strcat(fileName, ".wav"); break;
		case DISKOP_SMP_IFF: strcat(fileName, ".iff"); break;
		default: case DISKOP_SMP_RAW: break;
	}
}

static fileEntry_t *bufferCreateEmptyDir(void) // special case: creates a dir entry with a ".." directory
{
	fileEntry_t *dirEntry = (fileEntry_t *)malloc(sizeof (fileEntry_t));
	if (dirEntry == NULL)
		return NULL;

	dirEntry->nameU = UNICHAR_STRDUP(PARENT_DIR_STR);
	if (dirEntry->nameU == NULL)
	{
		free(dirEntry);
		return NULL;
	}

	dirEntry->isDir = true;
	dirEntry->filesize = 0;

	return dirEntry;
}

// deciding whether to load this entry into the buffer or not
static bool listEntry(fileEntry_t *f)
{
	int32_t entryName = (int32_t)UNICHAR_STRLEN(f->nameU);

#ifdef _WIN32
	if (f->isDir && entryName == 2 && !UNICHAR_STRNICMP(f->nameU, "..", 2))
		return true; // always show ".." directory
#else
	if (f->isDir && entryName == 2 && !UNICHAR_STRNICMP(f->nameU, "..", 2) && !(editor.currPath[0] == '/' && editor.currPath[1] == '\0'))
		return true; // always show ".." directory (unless in root dir)
#endif

	if (!UNICHAR_STRNICMP(f->nameU, ".", 1))
		return false; // skip ".name" entries

	if (f->isDir || diskop.mode == DISKOP_MODE_SMP)
		return true; // list all entries

	if (entryName >= 4)
	{
		if (!UNICHAR_STRNICMP(f->nameU, "MOD.", 4) || !UNICHAR_STRNICMP(&f->nameU[entryName - 4], ".MOD", 4) ||
		    !UNICHAR_STRNICMP(f->nameU, "STK.", 4) || !UNICHAR_STRNICMP(&f->nameU[entryName - 4], ".STK", 4) ||
		    !UNICHAR_STRNICMP(f->nameU, "M15.", 4) || !UNICHAR_STRNICMP(&f->nameU[entryName - 4], ".M15", 4) ||
		    !UNICHAR_STRNICMP(f->nameU, "NST.", 4) || !UNICHAR_STRNICMP(&f->nameU[entryName - 4], ".NST", 4) ||
		    !UNICHAR_STRNICMP(f->nameU, "UST.", 4) || !UNICHAR_STRNICMP(&f->nameU[entryName - 4], ".UST", 4) ||
		    !UNICHAR_STRNICMP(f->nameU, "PP.",  3) || !UNICHAR_STRNICMP(&f->nameU[entryName - 3], ".PP",  3) ||
		    !UNICHAR_STRNICMP(f->nameU, "NT.",  3) || !UNICHAR_STRNICMP(&f->nameU[entryName - 3], ".NT",  3))
		{
			return true;
		}
	}

	return false;
}

static int8_t findFirst(fileEntry_t *searchRec)
{
#ifdef _WIN32
	WIN32_FIND_DATAW fData;
	SYSTEMTIME sysTime;
#else
	struct dirent *fData;
	struct stat st;
#endif

	searchRec->nameU = NULL; // this one must be initialized

#ifdef _WIN32
	hFind = FindFirstFileW(L"*", &fData);
	if (hFind == NULL || hFind == INVALID_HANDLE_VALUE)
		return LFF_DONE;

	searchRec->nameU = UNICHAR_STRDUP(fData.cFileName);
	if (searchRec->nameU == NULL)
		return LFF_SKIP;

	searchRec->filesize = (fData.nFileSizeHigh > 0) ? -1 : fData.nFileSizeLow;
	searchRec->isDir = (fData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? true : false;
#else
	hFind = opendir(".");
	if (hFind == NULL)
		return LFF_DONE;

	fData = readdir(hFind);
	if (fData == NULL)
		return LFF_DONE;

	searchRec->nameU = UNICHAR_STRDUP(fData->d_name);
	if (searchRec->nameU == NULL)
		return LFF_SKIP;

	searchRec->filesize = 0;
	searchRec->isDir = false;

	if (stat(fData->d_name, &st) == 0)
	{
		searchRec->isDir = !!(st.st_mode & S_IFDIR);
		searchRec->filesize = ((int64_t)st.st_size > INT32_MAX) ? -1 : (int32_t)st.st_size;
	}
#endif

	if (searchRec->filesize < -1)
		searchRec->filesize = -1;

	if (!listEntry(searchRec))
	{
		// skip entry
		free(searchRec->nameU);
		searchRec->nameU = NULL;
		return LFF_SKIP;
	}

#ifdef _WIN32
	FileTimeToSystemTime(&fData.ftLastWriteTime, &sysTime);
	snprintf(searchRec->dateChanged, 7, "%02d%02d%02d", sysTime.wDay, sysTime.wMonth, sysTime.wYear % 100);
	unicharToAnsi(&searchRec->firstAnsiChar, searchRec->nameU, 1);
#else
	strftime(searchRec->dateChanged, 7, "%d%m%y", localtime(&st.st_mtime));
	searchRec->firstAnsiChar = (char)searchRec->nameU[0];
#endif

	return LFF_OK;
}

static int8_t findNext(fileEntry_t *searchRec)
{
#ifdef _WIN32
	WIN32_FIND_DATAW fData;
	SYSTEMTIME sysTime;
#else
	struct dirent *fData;
	struct stat st;
#endif

	searchRec->nameU = NULL; // important

#ifdef _WIN32
	if (hFind == NULL || FindNextFileW(hFind, &fData) == 0)
		return LFF_DONE;

	searchRec->nameU = UNICHAR_STRDUP(fData.cFileName);
	if (searchRec->nameU == NULL)
		return LFF_SKIP;

	searchRec->filesize = (fData.nFileSizeHigh > 0) ? -1 : fData.nFileSizeLow;
	searchRec->isDir = (fData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? true : false;

	FileTimeToSystemTime(&fData.ftLastWriteTime, &sysTime);
#else
	if (hFind == NULL || (fData = readdir(hFind)) == NULL)
		return LFF_DONE;

	searchRec->nameU = UNICHAR_STRDUP(fData->d_name);
	if (searchRec->nameU == NULL)
		return LFF_SKIP;

	searchRec->filesize = 0;
	searchRec->isDir = false;

	if (stat(fData->d_name, &st) == 0)
	{
		searchRec->isDir = !!(st.st_mode & S_IFDIR);
		searchRec->filesize = ((int64_t)st.st_size > INT32_MAX) ? -1 : (int32_t)st.st_size;
	}
#endif

	if (searchRec->filesize < -1)
		searchRec->filesize = -1;

	if (!listEntry(searchRec))
	{
		// skip entry
		free(searchRec->nameU);
		searchRec->nameU = NULL;
		return LFF_SKIP;
	}

#ifdef _WIN32
	FileTimeToSystemTime(&fData.ftLastWriteTime, &sysTime);
	snprintf(searchRec->dateChanged, 7, "%02d%02d%02d", sysTime.wDay, sysTime.wMonth, sysTime.wYear % 100);
	unicharToAnsi(&searchRec->firstAnsiChar, searchRec->nameU, 1);
#else
	strftime(searchRec->dateChanged, 7, "%d%m%y", localtime(&st.st_mtime));
	searchRec->firstAnsiChar = (char)searchRec->nameU[0];
#endif

	return LFF_OK;
}

static void findClose(void)
{
	if (hFind != NULL)
	{
#ifdef _WIN32
		FindClose(hFind);
#else
		closedir(hFind);
#endif
		hFind = NULL;
	}
}

void diskOpShowSelectText(void)
{
	if (ui.pointerMode == POINTER_MODE_MSG1 || editor.errorMsgActive)
		return;

	if (diskop.mode == DISKOP_MODE_MOD)
		setStatusMessage("SELECT MODULE", NO_CARRY);
	else
		setStatusMessage("SELECT SAMPLE", NO_CARRY);
}

void handleEntryJumping(SDL_Keycode jumpToChar) // SHIFT+character
{
	if (diskOpEntry != NULL)
	{
		fileEntry_t *f;
		
		// if last jump key was the same, go to next page of matching character
		if (diskop.numEntries > DISKOP_LINES && diskop.lastEntryJumpKey == jumpToChar)
		{
			for (int32_t i = 0; i < DISKOP_LINES; i++)
			{
				int32_t offset = diskop.scrollOffset+DISKOP_LINES;
				if (offset > diskop.numEntries-DISKOP_LINES)
					offset = diskop.numEntries-DISKOP_LINES;

				f = &diskOpEntry[offset];

				if (!f->isDir && tolower(f->firstAnsiChar) == jumpToChar)
				{
					diskop.scrollOffset += DISKOP_LINES;
					if (diskop.scrollOffset > diskop.numEntries-DISKOP_LINES)
						diskop.scrollOffset = diskop.numEntries-DISKOP_LINES;

					ui.updateDiskOpFileList = true;
					return;
				}
			}
		}

		diskop.lastEntryJumpKey = jumpToChar;

		// jump to first match from the beginning of file list

		f = diskOpEntry;
		for (int32_t i = 0; i < diskop.numEntries; i++, f++)
		{
			if (!f->isDir && tolower(f->firstAnsiChar) == jumpToChar)
			{
				if (diskop.numEntries > DISKOP_LINES)
				{
					diskop.scrollOffset = i;
					if (diskop.scrollOffset > diskop.numEntries-DISKOP_LINES)
						diskop.scrollOffset = diskop.numEntries-DISKOP_LINES;

					ui.updateDiskOpFileList = true;
				}

				return;
			}
		}
	}

	// character not found in file list, show red mouse pointer (error)
	editor.errorMsgActive = true;
	editor.errorMsgBlock = true;
	editor.errorMsgCounter = 0;

	setErrPointer();
}

bool diskOpEntryIsEmpty(int32_t fileIndex)
{
	if (diskop.scrollOffset+fileIndex >= diskop.numEntries)
		return true;

	return false;
}

bool diskOpEntryIsDir(int32_t fileIndex)
{
	if (diskOpEntry != NULL)
	{
		if (!diskOpEntryIsEmpty(fileIndex))
			return diskOpEntry[diskop.scrollOffset+fileIndex].isDir;
	}

	return false; // couldn't look up entry
}

char *diskOpGetAnsiEntry(int32_t fileIndex)
{
	if (diskOpEntry != NULL && !diskOpEntryIsEmpty(fileIndex))
	{
		UNICHAR *filenameU = diskOpEntry[diskop.scrollOffset+fileIndex].nameU;
		if (filenameU != NULL)
		{
			unicharToAnsi(fileNameBuffer, filenameU, PATH_MAX);
			return fileNameBuffer;
		}
	}

	return NULL;
}

UNICHAR *diskOpGetUnicodeEntry(int32_t fileIndex)
{
	if (diskOpEntry != NULL && !diskOpEntryIsEmpty(fileIndex))
		return diskOpEntry[diskop.scrollOffset+fileIndex].nameU;

	return NULL;
}

static void setVisualPathToCwd(void)
{
	memset(editor.currPath, 0, PATH_MAX + 10);
	memset(editor.currPathU, 0, (PATH_MAX + 2) * sizeof (UNICHAR));
	UNICHAR_GETCWD(editor.currPathU, PATH_MAX);

	if (diskop.mode == DISKOP_MODE_MOD)
		UNICHAR_STRCPY(editor.modulesPathU, editor.currPathU);
	else
		UNICHAR_STRCPY(editor.samplesPathU, editor.currPathU);

	unicharToAnsi(editor.currPath, editor.currPathU, PATH_MAX);
	ui.updateDiskOpPathText = true;
}

bool changePathToDesktop(void)
{
#ifdef _WIN32
	UNICHAR pathU[PATH_MAX + 2];
	if (SHGetFolderPathW(NULL, CSIDL_DESKTOPDIRECTORY, NULL, 0, pathU) >= 0)
	{
		if (UNICHAR_CHDIR(pathU) == 0)
			return true;
	}

	return false;
#else
	char *homePath = getenv("HOME");
	if (homePath != NULL && chdir(homePath) == 0)
	{
		chdir("Desktop"); // keep home dir in case we couldn't change the dir to desktop
		return true;
	}

	return false;
#endif
}

#ifndef _WIN32
bool changePathToHome(void)
{
	char *homePath = getenv("HOME");
	if (homePath != NULL && chdir(homePath) == 0)
		return true;

	return false;
}
#endif

void setPathFromDiskOpMode(void)
{
	UNICHAR_CHDIR((diskop.mode == DISKOP_MODE_MOD) ? editor.modulesPathU : editor.samplesPathU);
	setVisualPathToCwd();
}

bool diskOpSetPath(UNICHAR *path, bool cache)
{
	if (path != NULL && UNICHAR_CHDIR(path) == 0)
	{
		setVisualPathToCwd();

		if (cache)
			diskop.cached = false;

		if (ui.diskOpScreenShown)
			ui.updateDiskOpFileList = true;

		diskop.scrollOffset = 0;
		diskop.lastEntryJumpKey = SDLK_UNKNOWN;

		return true;
	}
	else
	{
		setVisualPathToCwd();
		displayErrorMsg("CAN'T OPEN DIR !");
		return false;
	}
}

void diskOpSetInitPath(void)
{
	// default module path
	if (config.defModulesDir[0] != '\0')
	{
#ifdef _WIN32
		MultiByteToWideChar(CP_ACP, MB_PRECOMPOSED, config.defModulesDir, -1, pathTmp, PATH_MAX);
#else
		strcpy(pathTmp, config.defModulesDir);
#endif
		UNICHAR_STRCPY(editor.modulesPathU, pathTmp);
	}
	else
	{
		// no path set in config, set to current working directory
		UNICHAR_GETCWD(editor.modulesPathU, PATH_MAX);
	}

	// default sample path
	if (config.defSamplesDir[0] != '\0')
	{
#ifdef _WIN32
		MultiByteToWideChar(CP_ACP, MB_PRECOMPOSED, config.defSamplesDir, -1, pathTmp, PATH_MAX);
#else
		strcpy(pathTmp, config.defSamplesDir);
#endif
		UNICHAR_STRCPY(editor.samplesPathU, pathTmp);
	}
	else
	{
		// no path set in config, set to current working directory
		UNICHAR_GETCWD(editor.samplesPathU, PATH_MAX);
	}

	setPathFromDiskOpMode();
}

bool allocDiskOpVars(void)
{
	editor.fileNameTmpU = (UNICHAR *)calloc(PATH_MAX + 2, sizeof (UNICHAR));
	editor.entryNameTmp = (char *)calloc(PATH_MAX + 10, sizeof (char));
	editor.currPath = (char *)calloc(PATH_MAX + 10, sizeof (char));
	editor.currPathU = (UNICHAR *)calloc(PATH_MAX + 2, sizeof (UNICHAR));
	editor.modulesPathU = (UNICHAR *)calloc(PATH_MAX + 2, sizeof (UNICHAR));
	editor.samplesPathU = (UNICHAR *)calloc(PATH_MAX + 2, sizeof (UNICHAR));

	if (editor.fileNameTmpU == NULL || editor.entryNameTmp == NULL ||
		editor.currPath     == NULL || editor.currPathU    == NULL ||
		editor.modulesPathU == NULL || editor.samplesPathU == NULL)
	{
		// allocated leftovers are free'd lateron
		return false;
	}

	return true;
}

void freeDiskOpMem(void)
{
	if (editor.fileNameTmpU != NULL) free(editor.fileNameTmpU);
	if (editor.entryNameTmp != NULL) free(editor.entryNameTmp);
	if (editor.currPath != NULL) free(editor.currPath);
	if (editor.currPathU != NULL) free(editor.currPathU);
	if (editor.modulesPathU != NULL) free(editor.modulesPathU);
	if (editor.samplesPathU != NULL) free(editor.samplesPathU);
}

void freeDiskOpEntryMem(void)
{
	if (diskOpEntry != NULL)
	{
		for (int32_t i = 0; i < diskop.numEntries; i++)
		{
			if (diskOpEntry[i].nameU != NULL)
				free(diskOpEntry[i].nameU);
		}

		free(diskOpEntry);
		diskOpEntry = NULL;
	}

	diskop.numEntries = 0;
}

// thanks to aTc for creating this simplified routine for qsort() (I edited it a little bit)
static int32_t fileEntryCompare(const void *f1, const void *f2)
{
	static char fn_1[PATH_MAX+1], fn_2[PATH_MAX+1];

	fileEntry_t *fe1 = (fileEntry_t *)f1;
	fileEntry_t *fe2 = (fileEntry_t *)f2;

	unicharToAnsi(fn_1, fe1->nameU, PATH_MAX);
	unicharToAnsi(fn_2, fe2->nameU, PATH_MAX);

	// ".." directories are always sorted first
	if (fe1->isDir && strcmp(fn_1, "..") == 0) return -1;
	if (fe2->isDir && strcmp(fn_2, "..") == 0) return  1;

	/* ProTracker handles names in upper case during sorting,
	** convert the string case.
	*/
	const int32_t fe1_l = (int32_t)strlen(fn_1);
	const int32_t fe2_l = (int32_t)strlen(fn_2);
	for (int32_t i = 0; i < fe1_l; i++) fn_1[i] = (char)toupper(fn_1[i]);
	for (int32_t i = 0; i < fe2_l; i++) fn_2[i] = (char)toupper(fn_2[i]);

	// if both entries are the same type
	if (fe1->isDir == fe2->isDir)
		return strcmp(fn_1, fn_2);

	// different types, so one of them is a dir
	if (fe1->isDir)
		return -1; // first one is a dir

	return 1; // second one is a dir
}

static void sortEntries(void)
{
	if (diskop.numEntries >= 2)
		qsort(diskOpEntry, diskop.numEntries, sizeof (fileEntry_t), fileEntryCompare);
}

static bool diskOpFillBuffer(void)
{
	fileEntry_t tmpBuffer;

	diskop.scrollOffset = 0;
	diskop.lastEntryJumpKey = SDLK_UNKNOWN;

	// do we have a path set?
	if (editor.currPathU[0] == '\0')
		setVisualPathToCwd();

	freeDiskOpEntryMem();

	// fill disk op. buffer (type, size, path, file name, date changed)

	// read first file
	uint8_t lastFindFileFlag = findFirst(&tmpBuffer);
	if (lastFindFileFlag != LFF_DONE && lastFindFileFlag != LFF_SKIP)
	{
		diskOpEntry = (fileEntry_t *)malloc(sizeof (fileEntry_t) * (diskop.numEntries + 1));
		if (diskOpEntry == NULL)
		{
			findClose();
			freeDiskOpEntryMem();
			statusOutOfMemory();
			return false;
		}

		memcpy(&diskOpEntry[diskop.numEntries], &tmpBuffer, sizeof (fileEntry_t));
		diskop.numEntries++;
	}

	// read remaining files
	while (lastFindFileFlag != LFF_DONE)
	{
		lastFindFileFlag = findNext(&tmpBuffer);
		if (lastFindFileFlag != LFF_DONE && lastFindFileFlag != LFF_SKIP)
		{
			fileEntry_t *newPtr = (fileEntry_t *)realloc(diskOpEntry, sizeof (fileEntry_t) * (diskop.numEntries + 1));
			if (newPtr == NULL)
			{
				findClose();
				freeDiskOpEntryMem();
				statusOutOfMemory();
				return false;
			}
			diskOpEntry = newPtr;

			memcpy(&diskOpEntry[diskop.numEntries], &tmpBuffer, sizeof (fileEntry_t));
			diskop.numEntries++;
		}
	}

	findClose();

	if (diskop.numEntries > 0)
	{
		sortEntries();
	}
	else
	{
		// access denied or out of memory - create parent directory link
		diskOpEntry = bufferCreateEmptyDir();
		if (diskOpEntry != NULL)
			diskop.numEntries = 1;
		else
			statusOutOfMemory();
	}

	return true;
}

static int32_t SDLCALL diskOpFillThreadFunc(void *ptr)
{
	(void)ptr;

	diskop.isFilling = true;
	diskOpFillBuffer();
	diskop.isFilling = false;

	ui.updateDiskOpFileList = true;
	return true;
}

static void printFileSize(fileEntry_t *entry, uint16_t x, uint16_t y)
{
	char tmpStr[7];

	if (entry->filesize == -1) // -1 means that the original filesize is above 2GB in our directory reader
	{
		textOut(x, y, "  >2GB", video.palette[PAL_QADSCP]);
		return;
	}

	uint32_t fileSize = (uint32_t)entry->filesize;
	if (fileSize <= 999999)
	{
		// bytes
		snprintf(tmpStr, 7, "%06d", fileSize);
	}
	else if (fileSize <= 9999999)
	{
		// kilobytes
		fileSize /= 1000;
		snprintf(tmpStr, 7, "%04dKB", fileSize);
	}
	else
	{
		// megabytes
		fileSize /= 1000000;
		snprintf(tmpStr, 7, "%04dMB", fileSize);
	}

	// turn zeroes on the left side into spaces
	for (int32_t i = 0; i < 7; i++)
	{
		if (tmpStr[i] != '0')
			break;

		tmpStr[i] = ' ';
	}

	textOut(x, y, tmpStr, video.palette[PAL_QADSCP]);
}

static void printEntryName(char *entryName, int32_t entryLength, int32_t maxLength, uint16_t x, uint16_t y)
{
	if (entryLength > maxLength)
	{
		// shorten name and add ".." to end
		for (int32_t i = 0; i < maxLength-2; i++)
			charOut(x + (i * FONT_CHAR_W), y, entryName[i], video.palette[PAL_QADSCP]);

		textOut(x + ((maxLength - 2) * FONT_CHAR_W), y, "..", video.palette[PAL_QADSCP]);
	}
	else
	{
		// print whole name
		textOut(x, y, entryName, video.palette[PAL_QADSCP]);
	}
}

void diskOpRenderFileList(void)
{
	uint8_t maxFilenameChars, maxDirNameChars;
	uint16_t textXStart;

	if (config.hideDiskOpDates)
	{
		textXStart = 8;
		maxFilenameChars = 30;
		maxDirNameChars = 31;
	}
	else
	{
		textXStart = 64;
		maxFilenameChars = 23;
		maxDirNameChars = 24;
	}

	diskOpShowSelectText();

	if (diskop.forceStopReading)
		return;

	// if needed, update the file list and add entries
	if (!diskop.cached)
	{
		diskop.fillThread = SDL_CreateThread(diskOpFillThreadFunc, NULL, NULL);
		if (diskop.fillThread == NULL)
			return;

		diskop.cached = true;
		return;
	}

	// clear list
	fillRect(8, 35, 295, 59, video.palette[PAL_BACKGRD]);

	if (diskop.isFilling || diskOpEntry == NULL)
		return;

	// list entries
	for (int32_t i = 0; i < DISKOP_LINES; i++)
	{
		if (diskop.scrollOffset+i >= diskop.numEntries)
			break;

		fileEntry_t *entry = &diskOpEntry[diskop.scrollOffset+i];
		char *entryName = diskOpGetAnsiEntry(i);
		int32_t entryLength = (int32_t)strlen(entryName);

		uint16_t x = textXStart;
		uint16_t y = (uint8_t)(35 + (i * (FONT_CHAR_H + 1)));

		if (!entry->isDir)
		{
			printEntryName(entryName, entryLength, maxFilenameChars, x, y);

			// print modification date
			if (!config.hideDiskOpDates)
				textOut(8, y, entry->dateChanged, video.palette[PAL_QADSCP]);

			// print file size
			printFileSize(entry, 256, y);
		}
		else
		{
			printEntryName(entryName, entryLength, maxDirNameChars, x, y);
			textOut(264, y, "(DIR)", video.palette[PAL_QADSCP]);
		}
	}
}

void diskOpLoadFile(uint32_t fileEntryRow, bool songModifiedCheck)
{
	// if we clicked on an empty space, return...
	if (diskOpEntryIsEmpty(fileEntryRow))
		return;

	if (diskOpEntryIsDir(fileEntryRow))
	{
		diskOpSetPath(diskOpGetUnicodeEntry(fileEntryRow), DISKOP_CACHE);
	}
	else
	{
		UNICHAR *filePath = diskOpGetUnicodeEntry(fileEntryRow);
		if (filePath != NULL)
		{
			if (diskop.mode == DISKOP_MODE_MOD)
			{
				if (songModifiedCheck && song->modified)
				{
					if (!askBox(ASKBOX_YES_NO, "SONG IS UNSAVED !"))
						return;
				}

				module_t *newSong = modLoad(filePath);
				if (newSong != NULL)
				{
					uint8_t oldMode = editor.currMode;
					uint8_t oldPlayMode = editor.playMode;

					modStop();
					modFree();

					song = newSong;
					setupLoadedMod();
					song->loaded = true;

					statusAllRight();

					if (config.autoCloseDiskOp)
						ui.diskOpScreenShown = false;

					if (config.rememberPlayMode)
					{
						if (oldMode == MODE_PLAY || oldMode == MODE_RECORD)
						{
							editor.playMode = oldPlayMode;

							if (oldPlayMode == PLAY_MODE_PATTERN || oldMode == MODE_RECORD)
								modPlay(0, 0, 0);
							else
								modPlay(DONT_SET_PATTERN, 0, 0);

							if (oldMode == MODE_RECORD)
								pointerSetMode(POINTER_MODE_RECORD, DO_CARRY);
							else
								pointerSetMode(POINTER_MODE_PLAY, DO_CARRY);

							editor.currMode = oldMode;
						}
					}
					else
					{
						editor.currMode = MODE_IDLE;
						editor.playMode = PLAY_MODE_NORMAL;
						editor.songPlaying = false;

						pointerSetMode(POINTER_MODE_IDLE, DO_CARRY);
					}

					displayMainScreen();
				}
				else
				{
					editor.errorMsgActive  = true;
					editor.errorMsgBlock = true;
					editor.errorMsgCounter = 0;

					// status/error message is set in the mod loader
					setErrPointer();
				}
			}
			else if (diskop.mode == DISKOP_MODE_SMP)
			{
				loadSample(filePath, diskOpGetAnsiEntry(fileEntryRow));
			}
		}
	}
}

void renderDiskOpScreen(void)
{
	blit32(0, 0, 320, 99, diskOpScreenBMP);

	ui.updateDiskOpPathText = true;
	ui.updatePackText = true;
	ui.updateSaveFormatText = true;
	ui.updateLoadMode = true;
	ui.updateDiskOpFileList = true;
}

void updateDiskOp(void)
{
	if (!ui.diskOpScreenShown || ui.posEdScreenShown || ui.askBoxShown)
		return;

	if (ui.updateDiskOpFileList)
	{
		ui.updateDiskOpFileList = false;
		diskOpRenderFileList();
	}

	if (ui.updateLoadMode)
	{
		ui.updateLoadMode = false;

		// clear boxes
		fillRect(147,  3, FONT_CHAR_W, FONT_CHAR_H, video.palette[PAL_GENBKG]);
		fillRect(147, 14, FONT_CHAR_W, FONT_CHAR_H, video.palette[PAL_GENBKG]);

		// draw load mode arrow
		if (diskop.mode == 0)
			charOut(147, 3, ARROW_RIGHT, video.palette[PAL_GENTXT]);
		else
			charOut(147,14, ARROW_RIGHT, video.palette[PAL_GENTXT]);
	}

	if (ui.updatePackText)
	{
		ui.updatePackText = false;
		textOutBg(120, 3, diskop.modPackFlg ? "ON " : "OFF", video.palette[PAL_GENTXT], video.palette[PAL_GENBKG]);
	}

	if (ui.updateSaveFormatText)
	{
		ui.updateSaveFormatText = false;
		     if (diskop.smpSaveType == DISKOP_SMP_WAV) textOutBg(120, 14, "WAV", video.palette[PAL_GENTXT], video.palette[PAL_GENBKG]);
		else if (diskop.smpSaveType == DISKOP_SMP_IFF) textOutBg(120, 14, "IFF", video.palette[PAL_GENTXT], video.palette[PAL_GENBKG]);
		else if (diskop.smpSaveType == DISKOP_SMP_RAW) textOutBg(120, 14, "RAW", video.palette[PAL_GENTXT], video.palette[PAL_GENBKG]);
	}

	if (ui.updateDiskOpPathText)
	{
		ui.updateDiskOpPathText = false;

		// print disk op. path
		for (int32_t i = 0; i < 26; i++)
		{
			char ch;
			
			if (ui.editTextFlag)
				ch = editor.currPath[textEdit.scrollOffset + i];
			else
				ch = editor.currPath[i];

			if (ch == '\0')
				ch = '_';

			charOutBg(24 + (i * FONT_CHAR_W), 25, ch, video.palette[PAL_GENTXT], video.palette[PAL_GENBKG]);
		}
	}
}
