// for finding memory leaks in debug mode with Visual Studio 
#if defined _DEBUG && defined _MSC_VER
#include <crtdbg.h>
#endif

#include <SDL2/SDL.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <ctype.h> // toupper()
#include "pt2_structs.h"
#include "pt2_config.h"

void showErrorMsgBox(const char *fmt, ...)
{
	char strBuf[512+1];
	va_list args;

	// format the text string
	va_start(args, fmt);
	vsnprintf(strBuf, sizeof (strBuf)-1, fmt, args);
	va_end(args);

	// SDL message boxes can be very buggy on Windows XP, use MessageBoxA() instead
#ifdef _WIN32
	MessageBoxA(NULL, strBuf, "Error", MB_OK | MB_ICONERROR);
#else
	SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Error", strBuf, NULL);
#endif
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

	for (int32_t i = 0; i < 22; i++)
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

	for (int32_t i = 0; i < 20; i++)
	{
		if (name[i] != '\0')
			return false;
	}

	return true;
}

void updateWindowTitle(bool modified)
{
	char editStr[32], titleTemp[256];

	editStr[0] = '\0';
	if (editor.currMode == MODE_EDIT)
		strcpy(editStr, "[EDITING] ");

	if (modified)
		song->modified = true;
	else
		song->modified = false;

	if (song->header.name[0] != '\0')
	{
		if (modified)
		{
			if (config.modDot)
				sprintf(titleTemp, "ProTracker 2 clone v%s %s- \"mod.%s\" (unsaved)",
					PROG_VER_STR, editStr, song->header.name);
			else
				sprintf(titleTemp, "ProTracker 2 clone v%s %s- \"%s.mod\" (unsaved)",
					PROG_VER_STR, editStr, song->header.name);
		}
		else
		{
			if (config.modDot)
				sprintf(titleTemp, "ProTracker 2 clone v%s %s- \"mod.%s\"",
					PROG_VER_STR, editStr, song->header.name);
			else
				sprintf(titleTemp, "ProTracker 2 clone v%s %s- \"%s.mod\"",
					PROG_VER_STR, editStr, song->header.name);
		}
	}
	else
	{
		if (modified)
		{
			if (config.modDot)
				sprintf(titleTemp, "ProTracker 2 clone v%s %s- \"mod.untitled\" (unsaved)",
					PROG_VER_STR, editStr);
			else
				sprintf(titleTemp, "ProTracker 2 clone v%s %s- \"untitled.mod\" (unsaved)",
					PROG_VER_STR, editStr);
		}
		else
		{
			if (config.modDot)
				sprintf(titleTemp, "ProTracker 2 clone v%s %s- \"mod.untitled\"",
					PROG_VER_STR, editStr);
			else
				sprintf(titleTemp, "ProTracker 2 clone v%s %s- \"untitled.mod\"",
					PROG_VER_STR, editStr);
		}
	}

	 SDL_SetWindowTitle(video.window, titleTemp);
}
