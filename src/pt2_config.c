// for finding memory leaks in debug mode with Visual Studio 
#if defined _DEBUG && defined _MSC_VER
#include <crtdbg.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <ctype.h>
#ifndef _WIN32
#include <unistd.h>
#include <limits.h>
#endif
#include "pt2_helpers.h"
#include "pt2_config.h"
#include "pt2_tables.h"
#include "pt2_sampler.h"
#include "pt2_diskop.h" // changePathToDesktop(), changePathToHome()
#include "pt2_visuals.h" // MAX_UPSCALE_FACTOR

#ifndef _WIN32
static char oldCwd[PATH_MAX];
#endif

config_t config; // globalized

static bool loadProTrackerDotIni(FILE *f);
static FILE *openPTDotConfig(void);
static bool loadPTDotConfig(FILE *f);
static bool loadColorsDotIni(void);

void loadConfig(void)
{
#ifndef _WIN32
	bool colorsDotIniFound;
#endif
	FILE *f;

	// set default config values first
	config.noDownsampleOnSmpLoad = false;
	config.disableE8xEffect = false;
	config.fullScreenStretch = false;
	config.pattDots = false;
	config.waveformCenterLine = true;
	config.amigaModel = MODEL_A1200;
	config.soundFrequency = 48000;
	config.rememberPlayMode = false;
	config.stereoSeparation = 20;
	config.autoFitVideoScale = true;
	config.videoScaleFactor = 0; // will be set later if autoFitVideoScale is set
	config.realVuMeters = false;
	config.modDot = false;
	config.accidental = 0; // sharp
	config.quantizeValue = 1;
	config.transDel = false;
	config.blankZeroFlag = false;
	config.compoMode = false;
	config.soundBufferSize = 1024;
	config.autoCloseDiskOp = true;
	config.vsyncOff = false;
	config.hwMouse = true;
	config.startInFullscreen = false;
	config.pixelFilter = PIXELFILTER_NEAREST;
	config.integerScaling = true;
	config.audioInputFrequency = 44100;
	config.mod2WavOutputFreq = 44100;
	config.keepEditModeAfterStepPlay = false;
	config.maxSampleLength = 65534;

#ifndef _WIN32
	getcwd(oldCwd, PATH_MAX);
#endif

	// load protracker.ini
	bool proTrackerDotIniFound = false;

#ifdef _WIN32
	f = fopen("protracker.ini", "r");
	if (f != NULL)
		proTrackerDotIniFound = true;
#else
	// check in program directory
	f = fopen("protracker.ini", "r");
	if (f != NULL)
		proTrackerDotIniFound = true;

	// check in ~/.protracker/
	if (!proTrackerDotIniFound && changePathToHome() && chdir(".protracker") == 0)
	{
		f = fopen("protracker.ini", "r");
		if (f != NULL)
			proTrackerDotIniFound = true;
	}

	chdir(oldCwd);
#endif

	if (proTrackerDotIniFound)
		loadProTrackerDotIni(f);

	editor.oldTempo = editor.initialTempo;

	// load PT.Config (if available)
	bool ptDotConfigFound = false;

#ifdef _WIN32
	f = openPTDotConfig();
	if (f != NULL)
		ptDotConfigFound = true;
#else
	// check in program directory
	f = openPTDotConfig();
	if (f != NULL)
		ptDotConfigFound = true;

	// check in ~/.protracker/
	if (!ptDotConfigFound && changePathToHome() && chdir(".protracker") == 0)
	{
		f = openPTDotConfig();
		if (f != NULL)
			ptDotConfigFound = true;
	}

	chdir(oldCwd);
#endif

	if (ptDotConfigFound)
		loadPTDotConfig(f);

	if (proTrackerDotIniFound || ptDotConfigFound)
		editor.configFound = true;

	// load colors.ini (if available)
#ifdef _WIN32
	loadColorsDotIni();
#else
	// check in program directory
	colorsDotIniFound = loadColorsDotIni();

	// check in ~/.protracker/
	if (!colorsDotIniFound && changePathToHome() && chdir(".protracker") == 0)
		loadColorsDotIni();
#endif

#ifndef _WIN32
	chdir(oldCwd);
#endif

	// use palette for generating sample data mark (invert) table
	createSampleMarkTable();
}

static bool loadProTrackerDotIni(FILE *f)
{
	fseek(f, 0, SEEK_END);
	uint32_t configFileSize = ftell(f);
	rewind(f);

	char *configBuffer = (char *)malloc(configFileSize + 1);
	if (configBuffer == NULL)
	{
		fclose(f);
		showErrorMsgBox("Couldn't parse protracker.ini: Out of memory!");
		return false;
	}

	fread(configBuffer, 1, configFileSize, f);
	configBuffer[configFileSize] = '\0';
	fclose(f);

	char *configLine = strtok(configBuffer, "\n");
	while (configLine != NULL)
	{
		uint32_t lineLen = (uint32_t)strlen(configLine);

		// remove CR in CRLF linefeed (if present)
		if (lineLen > 1)
		{
			if (configLine[lineLen-1] == '\r')
			{
				configLine[lineLen-1] = '\0';
				lineLen--;
			}
		}

		// COMMENT OR CATEGORY
		if (*configLine == ';' || *configLine == '[')
		{
			configLine = strtok(NULL, "\n");
			continue;
		}

		// STEPPLAY_KEEP_EDITMODE
		else if (!_strnicmp(configLine, "STEPPLAY_KEEP_EDITMODE=", 23))
		{
			if (!_strnicmp(&configLine[23], "TRUE", 4))
				config.keepEditModeAfterStepPlay = true;
			else if (!_strnicmp(&configLine[23], "FALSE", 5))
				config.keepEditModeAfterStepPlay = false;
		}

		// 64K_LIMIT
		else if (!_strnicmp(configLine, "64K_LIMIT=", 10))
		{
			if (!_strnicmp(&configLine[10], "TRUE", 4))
				config.maxSampleLength = 65534;
			else if (!_strnicmp(&configLine[10], "FALSE", 5))
				config.maxSampleLength = 131070;
		}

		// NO_DWNSMP_ON_SMP_LOAD (no dialog for 2x downsample after >22kHz sample load)
		else if (!_strnicmp(configLine, "NO_DWNSMP_ON_SMP_LOAD=", 22))
		{
			if (!_strnicmp(&configLine[22], "TRUE", 4)) config.noDownsampleOnSmpLoad = true;
			else if (!_strnicmp(&configLine[22], "FALSE", 5)) config.noDownsampleOnSmpLoad = false;
		}

		// DISABLE_E8X (Karplus-Strong command)
		else if (!_strnicmp(configLine, "DISABLE_E8X=", 12))
		{
			     if (!_strnicmp(&configLine[12], "TRUE",  4)) config.disableE8xEffect = true;
			else if (!_strnicmp(&configLine[12], "FALSE", 5)) config.disableE8xEffect = false;
		}

		// HWMOUSE
		else if (!_strnicmp(configLine, "HWMOUSE=", 8))
		{
			     if (!_strnicmp(&configLine[8], "TRUE",  4)) config.hwMouse = true;
			else if (!_strnicmp(&configLine[8], "FALSE", 5)) config.hwMouse = false;
		}

		// VSYNCOFF
		else if (!_strnicmp(configLine, "VSYNCOFF=", 9))
		{
			     if (!_strnicmp(&configLine[9], "TRUE",  4)) config.vsyncOff = true;
			else if (!_strnicmp(&configLine[9], "FALSE", 5)) config.vsyncOff = false;
		}

		// INTEGERSCALING
		else if (!_strnicmp(configLine, "INTEGERSCALING=", 15))
		{
			     if (!_strnicmp(&configLine[15], "TRUE",  4)) config.integerScaling = true;
			else if (!_strnicmp(&configLine[15], "FALSE", 5)) config.integerScaling = false;
		}

		// FULLSCREENSTRETCH
		else if (!_strnicmp(configLine, "FULLSCREENSTRETCH=", 18))
		{
			     if (!_strnicmp(&configLine[18], "TRUE",  4)) config.fullScreenStretch = true;
			else if (!_strnicmp(&configLine[18], "FALSE", 5)) config.fullScreenStretch = false;
		}

		// HIDEDISKOPDATES
		else if (!_strnicmp(configLine, "HIDEDISKOPDATES=", 16))
		{
			     if (!_strnicmp(&configLine[16], "TRUE",  4)) config.hideDiskOpDates = true;
			else if (!_strnicmp(&configLine[16], "FALSE", 5)) config.hideDiskOpDates = false;
		}

		// AUTOCLOSEDISKOP
		else if (!_strnicmp(configLine, "AUTOCLOSEDISKOP=", 16))
		{
			     if (!_strnicmp(&configLine[16], "TRUE",  4)) config.autoCloseDiskOp = true;
			else if (!_strnicmp(&configLine[16], "FALSE", 5)) config.autoCloseDiskOp = false;
		}

		// FULLSCREEN
		else if (!_strnicmp(configLine, "FULLSCREEN=", 11))
		{
			     if (!_strnicmp(&configLine[11], "TRUE",  4)) config.startInFullscreen = true;
			else if (!_strnicmp(&configLine[11], "FALSE", 5)) config.startInFullscreen = false;
		}

		// PIXELFILTER
		else if (!_strnicmp(configLine, "PIXELFILTER=", 12))
		{
			     if (!_strnicmp(&configLine[12], "NEAREST", 7)) config.pixelFilter = PIXELFILTER_NEAREST;
			else if (!_strnicmp(&configLine[12], "LINEAR", 6)) config.pixelFilter = PIXELFILTER_LINEAR;
			else if (!_strnicmp(&configLine[12], "BEST", 4)) config.pixelFilter = PIXELFILTER_BEST;
		}

		// COMPOMODE
		else if (!_strnicmp(configLine, "COMPOMODE=", 10))
		{
			     if (!_strnicmp(&configLine[10], "TRUE",  4)) config.compoMode = true;
			else if (!_strnicmp(&configLine[10], "FALSE", 5)) config.compoMode = false;
		}

		// PATTDOTS
		else if (!_strnicmp(configLine, "PATTDOTS=", 9))
		{
			     if (!_strnicmp(&configLine[9], "TRUE",  4)) config.pattDots = true;
			else if (!_strnicmp(&configLine[9], "FALSE", 5)) config.pattDots = false;
		}

		// BLANKZERO
		else if (!_strnicmp(configLine, "BLANKZERO=", 10))
		{
			     if (!_strnicmp(&configLine[10], "TRUE",  4)) config.blankZeroFlag = true;
			else if (!_strnicmp(&configLine[10], "FALSE", 5)) config.blankZeroFlag = false;
		}

		// REALVUMETERS
		else if (!_strnicmp(configLine, "REALVUMETERS=", 13))
		{
			     if (!_strnicmp(&configLine[13], "TRUE",  4)) config.realVuMeters = true;
			else if (!_strnicmp(&configLine[13], "FALSE", 5)) config.realVuMeters = false;
		}

		// ACCIDENTAL
		else if (!_strnicmp(configLine, "ACCIDENTAL=", 11))
		{
			     if (!_strnicmp(&configLine[11], "SHARP", 4)) config.accidental = 0;
			else if (!_strnicmp(&configLine[11], "FLAT",  5)) config.accidental = 1;
		}

		// QUANTIZE
		else if (!_strnicmp(configLine, "QUANTIZE=", 9))
		{
			if (configLine[9] != '\0')
			{
				const int32_t num = atoi(&configLine[9]);
				config.quantizeValue = (int16_t)(CLAMP(num, 0, 63));
			}
		}

		// TRANSDEL
		else if (!_strnicmp(configLine, "TRANSDEL=", 9))
		{
			     if (!_strnicmp(&configLine[9], "TRUE",  4)) config.transDel = true;
			else if (!_strnicmp(&configLine[9], "FALSE", 5)) config.transDel = false;
		}

		// DOTTEDCENTER
		else if (!_strnicmp(configLine, "DOTTEDCENTER=", 13))
		{
			     if (!_strnicmp(&configLine[13], "TRUE",  4)) config.waveformCenterLine = true;
			else if (!_strnicmp(&configLine[13], "FALSE", 5)) config.waveformCenterLine = false;
		}

		// MODDOT
		else if (!_strnicmp(configLine, "MODDOT=", 7))
		{
			     if (!_strnicmp(&configLine[7], "TRUE",  4)) config.modDot = true;
			else if (!_strnicmp(&configLine[7], "FALSE", 5)) config.modDot = false;
		}

		// VIDEOSCALE
		else if (!_strnicmp(configLine, "VIDEOSCALE=", 11))
		{
			if (!_strnicmp(&configLine[11], "AUTO", 4))
			{
				config.autoFitVideoScale = true;
				config.videoScaleFactor = 0; // will be set later
			}
			else if (lineLen >= 13 && toupper(configLine[12]) == 'X' && isdigit(configLine[11]))
			{
				config.autoFitVideoScale = false;
				config.videoScaleFactor = (int8_t)(configLine[11] - '0');
				config.videoScaleFactor = CLAMP(config.videoScaleFactor, 1, MAX_UPSCALE_FACTOR);
			}
		}

		// REMEMBERPLAYMODE
		else if (!_strnicmp(configLine, "REMEMBERPLAYMODE=", 17))
		{
			     if (!_strnicmp(&configLine[17], "TRUE",  4)) config.rememberPlayMode = true;
			else if (!_strnicmp(&configLine[17], "FALSE", 5)) config.rememberPlayMode = false;
		}

		// DEFAULTDIR
		else if (!_strnicmp(configLine, "DEFAULTDIR=", 11))
		{
			if (lineLen > 11)
			{
				uint32_t i = 11;
				while (configLine[i] == ' ') i++; // remove spaces before string (if present)
				while (configLine[lineLen-1] == ' ') lineLen--; // remove spaces after string (if present)

				lineLen -= i;
				if (lineLen > 0)
					strncpy(config.defModulesDir, &configLine[i], (lineLen > PATH_MAX) ? PATH_MAX : lineLen);
			}
		}

		// DEFAULTSMPDIR
		else if (!_strnicmp(configLine, "DEFAULTSMPDIR=", 14))
		{
			if (lineLen > 14)
			{
				uint32_t i = 14;
				while (configLine[i] == ' ') i++; // remove spaces before string (if present)
				while (configLine[lineLen-1] == ' ') lineLen--; // remove spaces after string (if present)

				lineLen -= i;
				if (lineLen > 0)
					strncpy(config.defSamplesDir, &configLine[i], (lineLen > PATH_MAX) ? PATH_MAX : lineLen);
			}
		}

		// FILTERMODEL
		else if (!_strnicmp(configLine, "FILTERMODEL=", 12))
		{
			     if (!_strnicmp(&configLine[12], "A500",  4)) config.amigaModel = MODEL_A500;
			else if (!_strnicmp(&configLine[12], "A1200", 5)) config.amigaModel = MODEL_A1200;
		}

		// A500LOWPASSFILTER (deprecated, same as A4000LOWPASSFILTER)
		else if (!_strnicmp(configLine, "A500LOWPASSFILTER=", 18))
		{
			     if (!_strnicmp(&configLine[18], "TRUE",  4)) config.amigaModel = MODEL_A500;
			else if (!_strnicmp(&configLine[18], "FALSE", 5)) config.amigaModel = MODEL_A1200;
		}

		// SAMPLINGFREQ
		else if (!_strnicmp(configLine, "SAMPLINGFREQ=", 13))
		{
			if (configLine[10] != '\0')
			{
				const int32_t num = atoi(&configLine[13]);
				config.audioInputFrequency = CLAMP(num, 44100, 192000);
			}
		}

		// MOD2WAVFREQUENCY
		else if (!_strnicmp(configLine, "MOD2WAVFREQUENCY=", 17))
		{
			if (configLine[17] != '\0')
			{
				const int32_t num = atoi(&configLine[17]);
				config.mod2WavOutputFreq = CLAMP(num, MIN_AUDIO_FREQUENCY, MAX_AUDIO_FREQUENCY);
			}
		}

		// FREQUENCY
		else if (!_strnicmp(configLine, "FREQUENCY=", 10))
		{
			if (configLine[10] != '\0')
			{
				const int32_t num = atoi(&configLine[10]);
				config.soundFrequency = CLAMP(num, MIN_AUDIO_FREQUENCY, MAX_AUDIO_FREQUENCY);
			}
		}

		// BUFFERSIZE
		else if (!_strnicmp(configLine, "BUFFERSIZE=", 11))
		{
			if (configLine[11] != '\0')
			{
				const int32_t num = atoi(&configLine[11]);
				config.soundBufferSize = CLAMP(num, 128, 8192);
			}
		}

		// STEREOSEPARATION
		else if (!_strnicmp(configLine, "STEREOSEPARATION=", 17))
		{
			if (configLine[17] != '\0')
			{
				const int32_t num = atoi(&configLine[17]);
				config.stereoSeparation = (int8_t)(CLAMP(num, 0, 100));
			}
		}

		configLine = strtok(NULL, "\n");
	}

	free(configBuffer);
	return true;
}

static FILE *openPTDotConfig(void)
{
	FILE *f = fopen("PT.Config", "rb"); // PT didn't read PT.Config with no number, but let's support it
	if (f == NULL)
	{
		// try regular PT config filenames (PT.Config-xx)
		char tmpFilename[16];
		int32_t i;

		for (i = 0; i < 100; i++)
		{
			sprintf(tmpFilename, "PT.Config-%02d", i);

			f = fopen(tmpFilename, "rb");
			if (f != NULL)
				break;
		}

		if (i == 100)
			return NULL;
	}

	return f;
}

static bool loadPTDotConfig(FILE *f)
{
	char cfgString[24];
	uint8_t tmp8;
	uint16_t tmp16;

	// get filesize
	fseek(f, 0, SEEK_END);
	uint32_t configFileSize = ftell(f);
	if (configFileSize != 1024)
	{
		// not a valid PT.Config file
		fclose(f);
		return false;
	}
	rewind(f);

	// check if file is a PT.Config file
	fread(cfgString, 1, 24, f);

	/* force version string to 2.3 so that we'll accept all versions.
	** AFAIK we're only loading values that were present since 1.0,
	** so it should be safe. */
	cfgString[2] = '2';
	cfgString[4] = '3';

	if (strncmp(cfgString, "PT2.3 Configuration File", 24) != 0)
	{
		fclose(f);
		return false;
	}

	// Palette
	fseek(f, 154, SEEK_SET);
	for (int32_t i = 0; i < 8; i++)
	{
		fread(&tmp16, 2, 1, f); // stored as Big-Endian
		tmp16 = SWAP16(tmp16);
		video.palette[i] = RGB12_to_RGB24(tmp16);
	}

	// Transpose Delete (delete out of range notes on transposing)
	fseek(f, 174, SEEK_SET);
	fread(&tmp8, 1, 1, f);
	config.transDel = tmp8 ? true : false;
	config.transDel = config.transDel;

	// Note style (sharps/flats)
	fseek(f, 200, SEEK_SET);
	fread(&tmp8, 1, 1, f);
	config.accidental = tmp8 ? 1 : 0;
	config.accidental = config.accidental;

	// Multi Mode Next
	fseek(f, 462, SEEK_SET);
	fread(&editor.multiModeNext[0], 1, 1, f);
	fread(&editor.multiModeNext[1], 1, 1, f);
	fread(&editor.multiModeNext[2], 1, 1, f);
	fread(&editor.multiModeNext[3], 1, 1, f);

	// Effect Macros
	fseek(f, 466, SEEK_SET);
	for (int32_t i = 0; i < 10; i++)
	{
		fread(&tmp16, 2, 1, f); // stored as Big-Endian
		tmp16 = SWAP16(tmp16);
		editor.effectMacros[i] = tmp16;
	}

	// Timing Mode (CIA/VBLANK)
	fseek(f, 487, SEEK_SET);
	fread(&tmp8, 1, 1, f);
	editor.timingMode = tmp8 ? TEMPO_MODE_CIA : TEMPO_MODE_VBLANK;

	// Blank Zeroes
	fseek(f, 490, SEEK_SET);
	fread(&tmp8, 1, 1, f);
	config.blankZeroFlag = tmp8 ? true : false;
	config.blankZeroFlag = config.blankZeroFlag;

	// Initial Tempo (don't load if timing is set to VBLANK)
	if (editor.timingMode == TEMPO_MODE_CIA)
	{
		fseek(f, 497, SEEK_SET);
		fread(&tmp8, 1, 1, f);
		if (tmp8 < 32) tmp8 = 32;
		editor.initialTempo = tmp8;
		editor.oldTempo = tmp8;
	}

	// Tuning Tone Note
	fseek(f, 501, SEEK_SET);
	fread(&tmp8, 1, 1, f);
	if (tmp8 > 35) tmp8 = 35;
	editor.tuningNote = tmp8;

	if (editor.tuningNote > 35)
		editor.tuningNote = 35;

	// Tuning Tone Volume
	fseek(f, 503, SEEK_SET);
	fread(&tmp8, 1, 1, f);
	if (tmp8 > 64) tmp8 = 64;
	editor.tuningVol = tmp8;

	// Initial Speed
	fseek(f, 545, SEEK_SET);
	fread(&tmp8, 1, 1, f);
	if (editor.timingMode == TEMPO_MODE_VBLANK)
	{
		editor.initialSpeed = tmp8;
	}
	else
	{
		if (tmp8 > 0x20) tmp8 = 0x20;
		editor.initialSpeed = tmp8;
	}

	// VU-Meter Colors
	fseek(f, 546, SEEK_SET);
	for (int32_t i = 0; i < 48; i++)
	{
		fread(&vuMeterColors[i], 2, 1, f); // stored as Big-Endian
		vuMeterColors[i] = SWAP16(vuMeterColors[i]);
	}

	// Spectrum Analyzer Colors
	fseek(f, 642, SEEK_SET);
	for (int32_t i = 0; i < 36; i++)
	{
		fread(&analyzerColors[i], 2, 1, f); // stored as Big-Endian
		analyzerColors[i] = SWAP16(analyzerColors[i]);
	}

	fclose(f);
	return true;
}

static uint8_t hex2int(char ch)
{
	ch = (char)toupper(ch);

	if (ch >= 'A' && ch <= 'F')
		return 10 + (ch - 'A');
	else if (ch >= '0' && ch <= '9')
		return ch - '0';

	return 0; // not a hex
}

static bool loadColorsDotIni(void)
{
	FILE *f = fopen("colors.ini", "r");
	if (f == NULL)
		return false;

	// get filesize
	fseek(f, 0, SEEK_END);
	uint32_t fileSize = ftell(f);
	rewind(f);

	char *configBuffer = (char *)malloc(fileSize + 1);
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
	char *configLine = strtok(configBuffer, "\n");
	while (configLine != NULL)
	{
		uint32_t lineLen = (uint32_t)strlen(configLine);

		// read palette
		if (lineLen >= (sizeof ("[Palette]")-1))
		{
			if (!_strnicmp("[Palette]", configLine, sizeof ("[Palette]")-1))
			{
				configLine = strtok(NULL, "\n");

				uint32_t line = 0;
				while (configLine != NULL && line < 8)
				{
					uint16_t color = (hex2int(configLine[0]) << 8) | (hex2int(configLine[1]) << 4) | hex2int(configLine[2]);
					color &= 0xFFF;
					video.palette[line] = RGB12_to_RGB24(color);

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

				uint32_t line = 0;
				while (configLine != NULL && line < 48)
				{
					uint16_t color = (hex2int(configLine[0]) << 8) | (hex2int(configLine[1]) << 4) | hex2int(configLine[2]);
					vuMeterColors[line] = color & 0xFFF;

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

				uint32_t line = 0;
				while (configLine != NULL && line < 36)
				{
					uint16_t color = (hex2int(configLine[0]) << 8) | (hex2int(configLine[1]) << 4) | hex2int(configLine[2]);
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
