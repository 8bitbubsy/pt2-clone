// for finding memory leaks in debug mode with Visual Studio 
#if defined _DEBUG && defined _MSC_VER
#include <crtdbg.h>
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "gui.h"
#include "palette.h"
#include "unicode.h"
#include "tinyfiledialogs/tinyfiledialogs.h"

#ifndef _WIN32
#define _stricmp strcasecmp
#define _strnicmp strncasecmp
#endif

#define SWAP16(value) \
( \
	(((uint16_t)((value) & 0x00FF)) << 8) | \
	(((uint16_t)((value) & 0xFF00)) >> 8)   \
)

UNICHAR *loadedFile = NULL;
bool configIsSaved = true;

static uint8_t hex2int(char ch)
{
	ch = (char)toupper(ch);

	     if (ch >= 'A' && ch <= 'F') return 10 + (ch - 'A');
	else if (ch >= '0' && ch <= '9') return ch - '0';

	return 0; // not a hex
}

static bool _loadPaletteFromColorsIni(FILE *f)
{
	char *configBuffer, *configLine;
	uint16_t color;
	uint32_t line, fileSize, lineLen;

	// get filesize
	fseek(f, 0, SEEK_END);
	fileSize = ftell(f);
	rewind(f);

	configBuffer = (char *)malloc(fileSize + 1);
	if (configBuffer == NULL)
	{
		fclose(f);
		showErrorMsgBox("Couldn't parse colors.ini: Out of memory!");
		return false;
	}

	fread(configBuffer, 1, fileSize, f);
	configBuffer[fileSize] = '\0';
	fclose(f);

	// do parsing
	configLine = strtok(configBuffer, "\n");
	while (configLine != NULL)
	{
		lineLen = (uint32_t)strlen(configLine);

		// read palette
		if (lineLen >= sizeof ("[Palette]")-1)
		{
			if (!_strnicmp("[Palette]", configLine, sizeof ("[Palette]")-1))
			{
				configLine = strtok(NULL, "\n");

				line = 0;
				while (configLine != NULL && line < 8)
				{
					color = (hex2int(configLine[0]) << 8) | (hex2int(configLine[1]) << 4) | hex2int(configLine[2]);
					palette[line] = color & 0xFFF;

					configLine = strtok(NULL, "\n");
					line++;
				}
			}

			if (configLine == NULL)
				break;

			lineLen = (uint32_t)strlen(configLine);
		}

		// read VU-meter colors
		if (lineLen >= sizeof ("[VU-meter]")-1)
		{
			if (!_strnicmp("[VU-meter]", configLine, sizeof ("[VU-meter]")-1))
			{
				configLine = strtok(NULL, "\n");

				line = 0;
				while (configLine != NULL && line < 48)
				{
					color = (hex2int(configLine[0]) << 8) | (hex2int(configLine[1]) << 4) | hex2int(configLine[2]);
					vuColors[line] = color & 0xFFF;

					configLine = strtok(NULL, "\n");
					line++;
				}
			}

			if (configLine == NULL)
				break;

			lineLen = (uint32_t)strlen(configLine);
		}

		// read spectrum analyzer colors
		if (lineLen >= sizeof ("[SpectrumAnalyzer]")-1)
		{
			if (!_strnicmp("[SpectrumAnalyzer]", configLine, sizeof ("[SpectrumAnalyzer]")-1))
			{
				configLine = strtok(NULL, "\n");

				line = 0;
				while (configLine != NULL && line < 36)
				{
					color = (hex2int(configLine[0]) << 8) | (hex2int(configLine[1]) << 4) | hex2int(configLine[2]);
					analyzerColors[line] = color & 0xFFF;

					configLine = strtok(NULL, "\n");
					line++;
				}
			}

			if (configLine == NULL)
				break;
		}

		configLine = strtok(NULL, "\n");
	}

	free(configBuffer);
	return true;
}

static void _loadPaletteFromPtConfig(FILE *f)
{
	uint16_t tmp16;
	int32_t i;

	// read palette
	fseek(f, 154, SEEK_SET);
	for (i = 0; i < 8; i++)
	{
		fread(&tmp16, 2, 1, f); // stored as Big-Endian
		tmp16 = SWAP16(tmp16);

		palette[i] = tmp16 & 0xFFF;
	}

	// read vu colors
	fseek(f, 546, SEEK_SET);
	for (i = 0; i < 48; i++)
	{
		fread(&tmp16, 2, 1, f); // stored as Big-Endian
		tmp16 = SWAP16(tmp16);

		vuColors[i] = tmp16 & 0xFFF;
	}

	// read spectrum analyzer colors
	fseek(f, 642, SEEK_SET);
	for (i = 0; i < 36; i++)
	{
		fread(&tmp16, 2, 1, f); // stored as Big-Endian
		tmp16 = SWAP16(tmp16);

		analyzerColors[i] = tmp16 & 0xFFF;
	}
}

static bool loadPaletteFromConfig(const UNICHAR *file, uint8_t ptDotConfig)
{
	char cfgString[26];
	uint32_t filesize;
	FILE *f;

	f = UNICHAR_FOPEN(file, "rb");
	if (f == NULL)
	{
		showErrorMsgBox("Couldn't open file for reading!");
		return false;
	}

	fseek(f, 0, SEEK_END);
	filesize = ftell(f);
	rewind(f);

	if (ptDotConfig)
	{
		if (filesize != 1024)
		{
			fclose(f);
			showErrorMsgBox("This is not valid a PT.Config file!");
			return false;
		}

		// check if file is a PT.Config file
		fread(cfgString, 1, 25, f);
		rewind(f);
		cfgString[25] = '\0'; // add null terminator

		cfgString[2] = '1'; // force version to 1.0 so that we can compare string easily
		cfgString[4] = '0';

		if (!strcmp(cfgString, "PT1.0 Configuration File\012"))
		{
			_loadPaletteFromPtConfig(f);
		}
		else
		{
			fclose(f);
			showErrorMsgBox("This is not a valid PT.Config file!");
			return false;
		}
	}
	else
	{
		_loadPaletteFromColorsIni(f);
	}

	fclose(f);

	if (!ptDotConfig)
	{
		if (loadedFile != NULL)
			free(loadedFile);
 
		loadedFile = UNICHAR_STRDUP(file);
	}

	fillCancel1Colors();
	fillCancel2Colors();
	updateBMPs();
	drawTracker();
	drawColorPicker1();
	drawColorPicker2();
	redrawScreen = true;

	configIsSaved = false;
	return true;
}

void loadColorsDotIni(void)
{
#ifdef _WIN32
	const UNICHAR *aFilterPatterns[] = { L"colors.ini" };
#else
	const UNICHAR *aFilterPatterns[] = { "colors.ini" };
#endif
	const UNICHAR *file;

#ifdef _WIN32
	file = tinyfd_openFileDialogW(L"Please select a colors.ini to load...", L"colors.ini", 1,
		aFilterPatterns, NULL, 0);
#else
#ifdef __APPLE__
	file = tinyfd_openFileDialog("Please select a colors.ini to load...", "colors.ini", 0,
		NULL, NULL, 0);
#else
	file = tinyfd_openFileDialog("Please select a colors.ini to load...", "colors.ini", 1,
		aFilterPatterns, NULL, 0);
#endif
#endif

	if (file != NULL)
		loadPaletteFromConfig(file, 0);
}

void loadPTDotConfig(void)
{
	const UNICHAR *file;

#ifdef _WIN32
	file = tinyfd_openFileDialogW(L"Please select a PT.Config file to load...", NULL, 0, NULL, NULL, 0);
#else
	file = tinyfd_openFileDialog("Please select a PT.Config file to load...", NULL, 0, NULL, NULL, 0);
#endif

	if (file != NULL)
		loadPaletteFromConfig(file, 1);
}

bool savePalette(bool showNotes)
{
	int32_t i;
	FILE *f;
	UNICHAR *folder;

	if (loadedFile == NULL)
	{
		loadedFile = (UNICHAR *)calloc(4096+2, sizeof (UNICHAR));
		if (loadedFile == NULL)
		{
			configIsSaved = 0;
			showErrorMsgBox("Out of memory!");
			return false;
		}
	}

	if (loadedFile[0] == '\0' && loadedFile[1] == '\0')
	{
#ifdef _WIN32
		folder = (UNICHAR *)tinyfd_selectFolderDialogW(L"Please select ProTracker 2 clone folder...", NULL);
		if (folder == NULL || UNICHAR_STRLEN(folder) < 1)
			return false;

		UNICHAR_STRCPY(loadedFile, folder);
		UNICHAR_STRCAT(loadedFile, L"\\");
		UNICHAR_STRCAT(loadedFile, L"colors.ini");
#else
		folder = (UNICHAR *)tinyfd_selectFolderDialog("Please select ProTracker 2 clone folder...", NULL);
		if (folder == NULL || UNICHAR_STRLEN(folder) < 1)
			return false;

		UNICHAR_STRCPY(loadedFile, folder);
		UNICHAR_STRCAT(loadedFile, "/");
		UNICHAR_STRCAT(loadedFile, "colors.ini");
#endif
	}

	f = UNICHAR_FOPEN(loadedFile, "w");
	if (f == NULL)
	{
		showErrorMsgBox("Sorry, couldn't write to config.ini! Is the file in use?\n");
		return false;
	}

	fprintf(f, "; WARNING: DO NOT TOUCH ANYTHING EXCEPT THE HEX VALUES!\n");
	fprintf(f, "; I recommend that you use the palette editor tool instead.\n");
	fprintf(f, "; You can find it at www.16-bits.org/pt.php\n");
	fprintf(f, ";\n");
	fprintf(f, "; Info:\n");
	fprintf(f, "; - Colors are stored as 12-bit RGB in hex (4096 total colors).\n");
	fprintf(f, ";   The palette order is the same as ProTracker on Amiga.\n");
	fprintf(f, ";\n");
	fprintf(f, "; To convert a 24-bit RGB hex value to 12-bit RGB hex, just\n");
	fprintf(f, "; skip every other digit. F.ex. 89ABCD -> 8AC\n");
	fprintf(f, ";\n");
	fprintf(f, "; To convert the other way around, repeat the digits.\n");
	fprintf(f, "; F.ex. 8AC -> 88AACC\n");
	fprintf(f, "\n");
	fprintf(f, "[Palette]\n");
	fprintf(f, "%03X ; Background\n", palette[0]);
	fprintf(f, "%03X ; GUI box light border / static text\n", palette[1]);
	fprintf(f, "%03X ; GUI box background color\n", palette[2]);
	fprintf(f, "%03X ; GUI box dark border / static text shadow\n", palette[3]);
	fprintf(f, "%03X ; Quadrascope / disk op. text / pos. ed. text\n", palette[4]);
	fprintf(f, "%03X ; Pattern cursor\n", palette[5]);
	fprintf(f, "%03X ; GUI box (non-static) text\n", palette[6]);
	fprintf(f, "%03X ; Pattern text\n", palette[7]);
	fprintf(f, "\n");

	fprintf(f, "[VU-meter]\n");
	for (i = 0; i < 48; i++)
		fprintf(f, "%03X\n", vuColors[i]);
	fprintf(f, "\n");

	fprintf(f, "[SpectrumAnalyzer]\n");
	for (i = 0; i < 36; i++)
		fprintf(f, "%03X\n", analyzerColors[i]);

	fclose(f);

	configIsSaved = true;

	if (showNotes)
		SDL_ShowSimpleMessageBox(0, "Note", "Colors were successfully saved to colors.ini!", NULL);

	return true;
}
