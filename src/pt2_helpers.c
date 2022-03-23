// for finding memory leaks in debug mode with Visual Studio 
#if defined _DEBUG && defined _MSC_VER
#include <crtdbg.h>
#endif

#include <SDL2/SDL.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <ctype.h> // toupper()
#ifndef _WIN32
#include <unistd.h>
#else
#define WIN32_MEAN_AND_LEAN
#include <windows.h>
#endif
#include "pt2_helpers.h"
#include "pt2_header.h"
#include "pt2_tables.h"
#include "pt2_structs.h"
#include "pt2_config.h"

void showErrorMsgBox(const char *fmt, ...)
{
	char strBuf[1024];
	va_list args;

	// format the text string
	va_start(args, fmt);
	vsnprintf(strBuf, sizeof (strBuf), fmt, args);
	va_end(args);

	// window can be NULL here, no problem...
	SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Critical Error", strBuf, video.window);
}

void sanitizeFilenameChar(char *chr)
{
	// some of these are legal on GNU/Linux and macOS, but whatever...
	     if (*chr == '\\') *chr = ' ';
	else if (*chr ==  '/') *chr = ' ';
	else if (*chr ==  ':') *chr = ' ';
	else if (*chr ==  '*') *chr = ' ';
	else if (*chr ==  '?') *chr = ' ';
	else if (*chr == '\"') *chr = ' ';
	else if (*chr ==  '<') *chr = ' ';
	else if (*chr ==  '>') *chr = ' ';
	else if (*chr ==  '|') *chr = ' ';
}

bool sampleNameIsEmpty(char *name)
{
	if (name == NULL)
		return true;

	for (uint8_t i = 0; i < 22; i++)
	{
		if (name[i] != '\0')
			return false;
	}

	return true;
}

bool moduleNameIsEmpty(char *name)
{
	if (name == NULL)
		return true;

	for (uint8_t i = 0; i < 20; i++)
	{
		if (name[i] != '\0')
			return false;
	}

	return true;
}

void updateWindowTitle(bool modified)
{
	if (headless)
		return;

	char titleTemp[128];

	if (modified)
		song->modified = true;
	else
		song->modified = false;

	if (song->header.name[0] != '\0')
	{
		if (modified)
		{
			if (config.modDot)
				sprintf(titleTemp, "ProTracker 2 clone v%s - \"mod.%s\" (unsaved)", PROG_VER_STR, song->header.name);
			else
				sprintf(titleTemp, "ProTracker 2 clone v%s - \"%s.mod\" (unsaved)", PROG_VER_STR, song->header.name);
		}
		else
		{
			if (config.modDot)
				sprintf(titleTemp, "ProTracker 2 clone v%s - \"mod.%s\"", PROG_VER_STR, song->header.name);
			else
				sprintf(titleTemp, "ProTracker 2 clone v%s - \"%s.mod\"", PROG_VER_STR, song->header.name);
		}
	}
	else
	{
		if (modified)
		{
			if (config.modDot)
				sprintf(titleTemp, "ProTracker 2 clone v%s - \"mod.untitled\" (unsaved)", PROG_VER_STR);
			else
				sprintf(titleTemp, "ProTracker 2 clone v%s - \"untitled.mod\" (unsaved)", PROG_VER_STR);
		}
		else
		{
			if (config.modDot)
				sprintf(titleTemp, "ProTracker 2 clone v%s - \"mod.untitled\"", PROG_VER_STR);
			else
				sprintf(titleTemp, "ProTracker 2 clone v%s - \"untitled.mod\"", PROG_VER_STR);
		}
	}

	 SDL_SetWindowTitle(video.window, titleTemp);
}
