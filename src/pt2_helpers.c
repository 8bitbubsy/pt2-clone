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
#include "pt2_palette.h"
#include "pt2_structs.h"
#include "pt2_config.h"

// used for Windows usleep() implementation
#ifdef _WIN32
static NTSTATUS (__stdcall *NtDelayExecution)(BOOL Alertable, PLARGE_INTEGER DelayInterval);
#endif

// usleep() implementation for Windows
#ifdef _WIN32
void usleep(uint32_t usec)
{
	LARGE_INTEGER lpDueTime;

	if (NtDelayExecution == NULL)
	{
		// NtDelayExecution() is not available (shouldn't happen), use regular sleep()
		Sleep(usec / 1000);
	}
	else
	{
		// this prevents a 64-bit MUL (will not overflow with typical values anyway)
		lpDueTime.HighPart = 0xFFFFFFFF;
		lpDueTime.LowPart = (DWORD)(-10 * (int32_t)usec);

		NtDelayExecution(false, &lpDueTime);
	}
}

void setupWin32Usleep(void)
{
	NtDelayExecution = (NTSTATUS (__stdcall *)(BOOL, PLARGE_INTEGER))GetProcAddress(GetModuleHandle("ntdll.dll"), "NtDelayExecution");
	timeBeginPeriod(0); // enter highest timer resolution
}

void freeWin32Usleep(void)
{
	timeEndPeriod(0); // exit highest timer resolution
}
#endif

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

void recalcChordLength(void)
{
	int8_t note;
	int32_t len;
	moduleSample_t *s;

	s = &song->samples[editor.currSample];

	if (editor.chordLengthMin)
	{
		note = MAX(MAX((editor.note1 == 36) ? -1 : editor.note1,
		               (editor.note2 == 36) ? -1 : editor.note2),
		           MAX((editor.note3 == 36) ? -1 : editor.note3,
		               (editor.note4 == 36) ? -1 : editor.note4));
	}
	else
	{
		note = MIN(MIN(editor.note1, editor.note2), MIN(editor.note3, editor.note4));
	}

	if (note < 0 || note > 35)
	{
		editor.chordLength = 0;
	}
	else
	{
		assert(editor.tuningNote < 36);
		if (editor.tuningNote < 36)
		{
			len = (s->length * periodTable[(37 * s->fineTune) + note]) / periodTable[editor.tuningNote];
			if (len > MAX_SAMPLE_LEN)
				len = MAX_SAMPLE_LEN;

			editor.chordLength = len & 0xFFFE;
		}
	}

	if (ui.editOpScreenShown && ui.editOpScreen == 3)
		ui.updateLengthText = true;
}
