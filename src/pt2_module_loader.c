// for finding memory leaks in debug mode with Visual Studio 
#if defined _DEBUG && defined _MSC_VER
#include <crtdbg.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include "pt2_mouse.h"
#include "pt2_sampler.h"
#include "pt2_replayer.h"
#include "pt2_textout.h"
#include "pt2_audio.h"
#include "pt2_helpers.h"
#include "pt2_visuals.h"
#include "pt2_sample_loader.h"
#include "pt2_config.h"
#include "modloaders/pt2_load_mod15.h"
#include "modloaders/pt2_load_mod31.h"
#include "modloaders/pt2_xpk_unpack.h"
#include "modloaders/pt2_pp_unpack.h"
#include "pt2_askbox.h"
#include "pt2_posed.h"

static void fixZeroesInString(char *str, uint32_t maxLength); // converts zeroes to spaces in a string, up until the last zero found

module_t *modLoad(UNICHAR *fileName)
{
	uint32_t powerPackerID;

	uint8_t *modBuffer = NULL;
	module_t *newMod = NULL;

	FILE *f = UNICHAR_FOPEN(fileName, "rb");
	if (f == NULL)
	{
		displayErrorMsg("FILE I/O ERROR !");
		goto loadError;
	}

	fseek(f, 0, SEEK_END);
	uint32_t filesize = ftell(f);
	rewind(f);

	// check if mod is a powerpacker mod
	fread(&powerPackerID, 4, 1, f);
	if (powerPackerID == 0x30325850) // "PX20"
	{
		displayErrorMsg("ENCRYPTED MOD !");
		goto loadError;
	}
	else if (powerPackerID == 0x30325050) // "PP20"
	{
		modBuffer = unpackPP(f, &filesize);
		if (modBuffer == NULL)
		{
			displayErrorMsg("PP UNPACK ERROR !");
			goto loadError; // error msg is set in unpackPPModule()
		}
	}
	else
	{
		if (detectXPK(f))
		{
			if (!unpackXPK(f, &filesize, &modBuffer))
			{
				displayErrorMsg("XPK UNPACK ERROR");
				goto loadError;
			}

			if (modBuffer == NULL)
			{
				statusOutOfMemory();
				goto loadError;
			}

			fclose(f);
		}
		else
		{
			modBuffer = (uint8_t *)malloc(filesize);
			if (modBuffer == NULL)
			{
				statusOutOfMemory();
				goto loadError;
			}

			rewind(f);
			fread(modBuffer, 1, filesize, f);
			fclose(f);
		}
	}

	// read module is in now in modBuffer, time to load...

	bool isMod31 = detectMod31(modBuffer, filesize);
	if (isMod31)
		newMod = loadMod31(modBuffer, filesize);
	else
		newMod = loadMod15(modBuffer, filesize);

	free(modBuffer);
	modBuffer = NULL;

	if (newMod == NULL)
	{
		// error message is shown in the mod loader
		goto loadError;
	}

	// module is loaded, do some sanitation...

	newMod->header.name[20] = '\0';

	// convert illegal song name characters to space
	for (int32_t i = 0; i < 20; i++)
	{
		char tmpChar = newMod->header.name[i];
		if ((tmpChar < ' ' || tmpChar > '~') && tmpChar != '\0')
			tmpChar = ' ';

		newMod->header.name[i] = tmpChar;
	}

	fixZeroesInString(newMod->header.name, 20);

	moduleSample_t *s = newMod->samples;
	for (int32_t i = 0; i < MOD_SAMPLES; i++, s++)
	{
		s->text[22] = '\0';

		// convert illegal sample name characters to space
		for (int32_t j = 0; j < 22; j++)
		{
			char tmpChar = s->text[j];
			if ((tmpChar < ' ' || tmpChar > '~') && tmpChar != '\0')
				tmpChar = ' ';

			s->text[j] = tmpChar;
		}

		fixZeroesInString(s->text, 22);

		if (s->length > config.maxSampleLength)
			s->length = config.maxSampleLength;

		if ((uint8_t)s->volume > 64)
			s->volume = 64;

		if (s->loopLength < 2)
			s->loopLength = 2;

		// we don't support samples bigger than 65534 (or 128kB) bytes, disable uncompatible loops
		if (s->loopStart > config.maxSampleLength || s->loopStart+s->loopLength > config.maxSampleLength)
		{
			s->loopStart = 0;
			s->loopLength = 2;
		}

		// some modules are broken like this, adjust sample length if possible (this is ok if we have room)
		if (s->length > 0 && s->loopLength > 2 && s->loopStart+s->loopLength > s->length)
		{
			int32_t loopOverflowVal = (s->loopStart + s->loopLength) - s->length;
			if (s->length+loopOverflowVal <= config.maxSampleLength)
			{
				s->length += loopOverflowVal; // this is safe, we're allocating 65534 bytes per sample slot
			}
			else
			{
				s->loopStart = 0;
				s->loopLength = 2;
			}
		}

		// clear first two bytes of non-looping samples to prevent beep after sample has been played
		if (s->length >= 2 && s->loopStart+s->loopLength <= 2)
		{
			newMod->sampleData[s->offset+0] = 0;
			newMod->sampleData[s->offset+1] = 0;
		}
	}
	
	initializeModuleChannels(newMod);
	return newMod;

loadError:
	if (modBuffer != NULL)
		free(modBuffer);

	return NULL;
}

static void fixZeroesInString(char *str, uint32_t maxLength)
{
	int32_t i;

	for (i = maxLength-1; i >= 0; i--)
	{
		if (str[i] != '\0')
			break;
	}

	// convert zeroes to spaces
	if (i > 0)
	{
		for (int32_t j = 0; j < i; j++)
		{
			if (str[j] == '\0')
				str[j] = ' ';
		}
	}
}

void setupLoadedMod(void)
{
	moduleSample_t *s = song->samples;
	for (int32_t i = 0; i < MOD_SAMPLES; i++, s++)
		fillSampleRedoBuffer((uint8_t)i);

	modSetPos(0, 0);
	modSetPattern(0); // set pattern to 00 instead of first order's pattern

	posEdClearNames();

	// setup GUI text pointers
	editor.currEditPatternDisp = &song->currPattern;
	editor.currPosDisp = &song->currPos;
	editor.currPatternDisp = &song->header.patternTable[0];
	editor.currPosEdPattDisp = &song->header.patternTable[0];
	editor.currLengthDisp = &song->header.songLength;

	// calculate MOD size
	ui.updateSongSize = true;

	editor.muted[0] = false;
	editor.muted[1] = false;
	editor.muted[2] = false;
	editor.muted[3] = false;

	editor.editMoveAdd = 1;
	editor.currSample = 0;

	editor.playbackSeconds = 0;
	editor.playbackSecondsFrac = 0;

	editor.modLoaded = true;
	editor.blockMarkFlag = false;
	editor.sampleZero = false;
	editor.hiLowInstr = 0;

	// disable LED filter after module load (real PT doesn't do this)
	setLEDFilter(false);

	updateWindowTitle(MOD_NOT_MODIFIED);

	editor.timingMode = TEMPO_MODE_CIA;
	updateReplayerTimingMode();

	modSetSpeed(6);
	modSetTempo(song->header.initialTempo, false); // 125 for normal MODs, custom value for certain STK/UST MODs

	updateCurrSample();
	editor.samplePos = 0;
	updateSamplePos();
}

void loadModFromArg(char *arg)
{
	ui.introTextShown = false;
	statusAllRight();

	uint32_t filenameLen = (uint32_t)strlen(arg);

	UNICHAR *filenameU = (UNICHAR *)calloc(filenameLen + 2, sizeof (UNICHAR));
	if (filenameU == NULL)
	{
		statusOutOfMemory();
		return;
	}

#ifdef _WIN32
	MultiByteToWideChar(CP_UTF8, 0, arg, -1, filenameU, filenameLen);
#else
	strcpy(filenameU, arg);
#endif

	module_t *newSong = modLoad(filenameU);
	if (newSong != NULL)
	{
		song->loaded = false;
		modFree();
		song = newSong;
		setupLoadedMod();
		song->loaded = true;
	}
	else
	{
		editor.errorMsgActive = true;
		editor.errorMsgBlock = true;
		editor.errorMsgCounter = 0;

		// status/error message is set in the mod loader
		setErrPointer();
	}

	free(filenameU);
}

static bool testExtension(char *ext, uint8_t extLen, char *fullPath)
{
	// checks for EXT.filename and filename.EXT

	extLen++; // add one to length (dot)

	char *fileName = strrchr(fullPath, DIR_DELIMITER);
	if (fileName != NULL)
		fileName++;
	else
		fileName = fullPath;

	uint32_t fileNameLen = (uint32_t)strlen(fileName);
	if (fileNameLen >= extLen)
	{
		char begStr[8], endStr[8];

		sprintf(begStr, "%s.", ext);
		if (!_strnicmp(begStr, fileName, extLen))
			return true;

		sprintf(endStr, ".%s", ext);
		if (!_strnicmp(endStr, fileName + (fileNameLen - extLen), extLen))
			return true;
	}

	return false;
}

void loadDroppedFile(char *fullPath, uint32_t fullPathLen, bool autoPlay, bool songModifiedCheck)
{
	// don't allow drag n' drop if the tracker is busy
	if (ui.pointerMode == POINTER_MODE_MSG1 || diskop.isFilling ||
		editor.mod2WavOngoing || editor.pat2SmpOngoing ||
		ui.samplerFiltersBoxShown || ui.samplerVolBoxShown || ui.samplingBoxShown)
	{
		return;
	}

	char *ansiName = (char *)calloc(fullPathLen + 10, sizeof (char));
	if (ansiName == NULL)
	{
		statusOutOfMemory();
		return;
	}

	UNICHAR *fullPathU = (UNICHAR *)calloc(fullPathLen + 2, sizeof (UNICHAR));
	if (fullPathU == NULL)
	{
		free(ansiName);
		statusOutOfMemory();
		return;
	}

#ifdef _WIN32
	MultiByteToWideChar(CP_UTF8, 0, fullPath, -1, fullPathU, fullPathLen);
#else
	strcpy(fullPathU, fullPath);
#endif

	unicharToAnsi(ansiName, fullPathU, fullPathLen);

	// make a new pointer point to filename (strip path)
	char *fileName = strrchr(ansiName, DIR_DELIMITER);
	if (fileName != NULL)
		fileName++;
	else
		fileName = ansiName;

	// check if the file extension is a module (FIXME: check module by content instead..?)
	bool isMod = false;
	     if (testExtension("MOD", 3, fileName)) isMod = true;
	else if (testExtension("M15", 3, fileName)) isMod = true;
	else if (testExtension("STK", 3, fileName)) isMod = true;
	else if (testExtension("NST", 3, fileName)) isMod = true;
	else if (testExtension("UST", 3, fileName)) isMod = true;
	else if (testExtension("PP",  2, fileName)) isMod = true;
	else if (testExtension("NT",  2, fileName)) isMod = true;

	if (isMod)
	{
		if (songModifiedCheck && song->modified)
		{
			if (!askBox(ASKBOX_YES_NO, "SONG IS UNSAVED !"))
				goto DropExit;
		}

		module_t *newSong = modLoad(fullPathU);
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

			if (autoPlay)
			{
				editor.playMode = PLAY_MODE_NORMAL;
				editor.currMode = MODE_PLAY;

				// start normal playback
				modPlay(DONT_SET_PATTERN, 0, 0);

				pointerSetMode(POINTER_MODE_PLAY, DO_CARRY);
			}
			else if (oldMode == MODE_PLAY || oldMode == MODE_RECORD)
			{
				// use last mode
				editor.playMode = oldPlayMode;
				if (oldPlayMode == PLAY_MODE_PATTERN || oldMode == MODE_RECORD)
					modPlay(0, 0, 0);
				else
					modPlay(DONT_SET_PATTERN, 0, 0);
				editor.currMode = oldMode;

				if (oldMode == MODE_RECORD)
					pointerSetMode(POINTER_MODE_RECORD, DO_CARRY);
				else
					pointerSetMode(POINTER_MODE_PLAY, DO_CARRY);
			}
			else
			{
				// stop playback
				editor.playMode = PLAY_MODE_NORMAL;
				editor.currMode = MODE_IDLE;
				editor.songPlaying = false;
				pointerSetMode(POINTER_MODE_IDLE, DO_CARRY);
			}

			displayMainScreen();
		}
		else
		{
			editor.errorMsgActive = true;
			editor.errorMsgBlock = true;
			editor.errorMsgCounter = 0;
			setErrPointer(); // status/error message is set in the mod loader
		}
	}
	else
	{
		loadSample(fullPathU, fileName);
	}

DropExit:
	free(ansiName);
	free(fullPathU);
}
