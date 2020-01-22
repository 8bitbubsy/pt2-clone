#pragma once

#include <SDL2/SDL.h>
#include <stdint.h>
#include <stdbool.h>
#include <assert.h>
#ifdef _WIN32
#define WIN32_MEAN_AND_LEAN
#include <windows.h>
#else
#include <limits.h> // PATH_MAX
#endif
#include <stdint.h>
#include "pt2_unicode.h"

#define PROG_VER_STR "1.03"

#ifdef _WIN32
#define DIR_DELIMITER '\\'
#define PATH_MAX MAX_PATH
#else
#define DIR_DELIMITER '/'
#endif

#include "pt2_config.h" // this must be included after PATH_MAX definition

#ifdef _MSC_VER
#pragma warning(disable:4244) // disable 'conversion from' warings
#pragma warning(disable:4820) // disable struct padding warnings
#pragma warning(disable:4996) // disable deprecated POSIX warnings
#pragma warning(disable:4127) // disable while (true) warnings
#endif

#ifndef _WIN32
#define _stricmp strcasecmp
#define _strnicmp strncasecmp
#endif

#define SAMPLE_VIEW_HEIGHT 64
#define SAMPLE_AREA_WIDTH 314

#define SCREEN_W 320
#define SCREEN_H 255

/* "60Hz" ranges everywhere from 59..61Hz depending on the monitor, so with
** no vsync we will get stuttering because the rate is not perfect... */
#define VBLANK_HZ 60

/* Scopes are clocked at 64Hz instead of 60Hz to prevent +/- interference
** from monitors not being exactly 60Hz (and unstable non-vsync mode).
** Sadly the scopes might midly flicker from this. */
#define SCOPE_HZ 64

#define AMIGA_PAL_VBLANK_HZ 50

#define FONT_CHAR_W 8 // actual data length is 7, includes right spacing (1px column)
#define FONT_CHAR_H 5

#define MOD_ROWS 64
#define MOD_SAMPLES 31
#define MOD_ORDERS 128
#define MAX_PATTERNS 100

#define MAX_SAMPLE_LEN 65534
#define RESERVED_SAMPLE_OFFSET (31 * MAX_SAMPLE_LEN)

#define AMIGA_VOICES 4
#define SCOPE_WIDTH 40
#define SCOPE_HEIGHT 33
#define SPECTRUM_BAR_NUM 23
#define SPECTRUM_BAR_HEIGHT 36
#define SPECTRUM_BAR_WIDTH 6

#define POSED_LIST_SIZE 12

// main crystal oscillator
#define AMIGA_PAL_XTAL_HZ 28375160

#define PAULA_PAL_CLK (AMIGA_PAL_XTAL_HZ / 8)
#define CIA_PAL_CLK (AMIGA_PAL_XTAL_HZ / 40)

#define FILTERS_BASE_FREQ (PAULA_PAL_CLK / 214.0)

#define KEYB_REPEAT_DELAY 17

enum
{
	FORMAT_MK, // ProTracker 1.x
	FORMAT_MK2, // ProTracker 2.x (if tune has >64 patterns)
	FORMAT_FLT4, // StarTrekker
	FORMAT_1CHN,
	FORMAT_2CHN, // FastTracker II
	FORMAT_3CHN,
	FORMAT_4CHN, // rare type, not sure what tracker it comes from
	FORMAT_STK, // The Ultimate SoundTracker (15 samples)
	FORMAT_NT, // NoiseTracker
	FORMAT_FEST, // NoiseTracker (special one)
	FORMAT_UNKNOWN
};

enum
{
	FLAG_NOTE = 1,
	FLAG_SAMPLE = 2,
	FLAG_NEWSAMPLE = 4,

	TEMPFLAG_START = 1,
	TEMPFLAG_DELAY = 2,

	FILTER_A500 = 1,
	FILTER_LED_ENABLED = 2,

	NO_CARRY = 0,
	DO_CARRY = 1,

	INCREMENT_SLOW = 0,
	INCREMENT_FAST = 1,

	NO_SAMPLE_CUT = 0,
	SAMPLE_CUT = 1,

	EDIT_SPECIAL = 0,
	EDIT_NORMAL = 1,

	EDIT_TEXT_NO_UPDATE = 0,
	EDIT_TEXT_UPDATE = 1,

	TRANSPOSE_ALL = 1,

	MOUSE_BUTTON_NOT_HELD = 0,
	MOUSE_BUTTON_HELD = 1,

	DONT_SET_ORDER = -1,
	DONT_SET_PATTERN = -1,
	DONT_SET_ROW = -1,

	REMOVE_SAMPLE_MARKING = 0,
	KEEP_SAMPLE_MARKING  = 1,

	MOD_NOT_MODIFIED = 0,
	MOD_IS_MODIFIED = 1,

	DONT_CHECK_IF_FILE_EXIST = 0,
	CHECK_IF_FILE_EXIST = 1,

	DONT_GIVE_NEW_FILENAME = 0,
	GIVE_NEW_FILENAME = 1,

	DONT_DOWNSAMPLE = 0,
	DO_DOWNSAMPLE = 1,

	SCREEN_ALL = 0,
	SCREEN_MAINSCREEN = 1,
	SCREEN_DISKOP = 2,
	SCREEN_SAMPLER = 4,
	SCREEN_QUIT = 8,
	SCREEN_CLEAR = 16,

	VISUAL_QUADRASCOPE = 0,
	VISUAL_SPECTRUM = 1,

	MODE_IDLE = 0,
	MODE_EDIT = 1,
	MODE_PLAY = 2,
	MODE_RECORD = 3,

	RECORD_PATT = 0,
	RECORD_SONG = 1,

	CURSOR_NOTE = 0,
	CURSOR_SAMPLE1 = 1,
	CURSOR_SAMPLE2 = 2,
	CURSOR_CMD = 3,
	CURSOR_PARAM1 = 4,
	CURSOR_PARAM2 = 5,

	PLAY_MODE_NORMAL = 0,
	PLAY_MODE_PATTERN = 1,

	OCTAVE_HIGH = 0,
	OCTAVE_LOW = 1,

	DISKOP_MODE_MOD = 0,
	DISKOP_MODE_SMP = 1,

	DISKOP_SMP_WAV = 0,
	DISKOP_SMP_IFF = 1,
	DISKOP_SMP_RAW = 2,

	ASK_QUIT = 0,
	ASK_SAVE_MODULE = 1,
	ASK_SAVE_SONG = 2,
	ASK_SAVE_SAMPLE = 3,
	ASK_MOD2WAV = 4,
	ASK_MOD2WAV_OVERWRITE = 5,
	ASK_SAVEMOD_OVERWRITE = 6,
	ASK_SAVESMP_OVERWRITE = 7,
	ASK_DOWNSAMPLING = 8,
	ASK_RESAMPLE = 9,
	ASK_KILL_SAMPLE = 10,
	ASK_UPSAMPLE = 11,
	ASK_DOWNSAMPLE = 12,
	ASK_FILTER_ALL_SAMPLES = 13,
	ASK_BOOST_ALL_SAMPLES = 14,
	ASK_MAKE_CHORD = 15,
	ASK_SAVE_ALL_SAMPLES = 16,
	ASK_PAT2SMP = 17,
	ASK_RESTORE_SAMPLE = 18,
	ASK_DISCARD_SONG = 19,
	ASK_DISCARD_SONG_DRAGNDROP = 20,

	TEMPO_MODE_CIA = 0,
	TEMPO_MODE_VBLANK = 1,

	TEXT_EDIT_STRING = 0,
	TEXT_EDIT_DECIMAL = 1,
	TEXT_EDIT_HEX = 2
};

typedef struct wavHeader_t
{
	uint32_t chunkID, chunkSize, format, subchunk1ID, subchunk1Size;
	uint16_t audioFormat, numChannels;
	uint32_t sampleRate, byteRate;
	uint16_t blockAlign, bitsPerSample;
	uint32_t subchunk2ID, subchunk2Size;
} wavHeader_t;

typedef struct sampleLoop_t
{
	uint32_t dwIdentifier, dwType, dwStart;
	uint32_t dwEnd, dwFraction, dwPlayCount;
} sampleLoop_t;

typedef struct samplerChunk_t
{
	uint32_t chunkID, chunkSize, dwManufacturer, dwProduct;
	uint32_t dwSamplePeriod, dwMIDIUnityNote, wMIDIPitchFraction;
	uint32_t dwSMPTEFormat, dwSMPTEOffset, cSampleLoops, cbSamplerData;
	sampleLoop_t loop;
} samplerChunk_t;

typedef struct note_t
{
	uint8_t param, sample, command;
	uint16_t period;
} note_t;

typedef struct moduleHeader_t
{
	char moduleTitle[20 + 1];
	uint8_t ticks, format, restartPos;
	uint16_t order[MOD_ORDERS], orderCount, patternCount, tempo, initBPM;
	uint32_t moduleSize, totalSampleSize;
} moduleHeader_t;

typedef struct moduleSample_t
{
	volatile int8_t *volumeDisp;
	volatile uint16_t *lengthDisp, *loopStartDisp, *loopLengthDisp;
	char text[22 + 1];
	int8_t volume;
	uint8_t fineTune;
	uint16_t length, loopStart, loopLength, tmpLoopStart;
	int32_t offset, realLength;
} moduleSample_t;

typedef struct moduleChannel_t
{
	int8_t *n_start, *n_wavestart, *n_loopstart, n_chanindex, n_volume;
	int8_t n_toneportdirec, n_vibratopos, n_tremolopos, n_pattpos, n_loopcount;
	uint8_t n_wavecontrol, n_glissfunk, n_sampleoffset, n_toneportspeed;
	uint8_t n_vibratocmd, n_tremolocmd, n_finetune, n_funkoffset, n_samplenum;
	int16_t n_period, n_note, n_wantedperiod;
	uint16_t n_cmd;
	uint32_t n_scopedelta, n_length, n_replen;
} moduleChannel_t;

typedef struct module_t
{
	int8_t *sampleData, currRow, modified, row;
	uint8_t currSpeed, moduleLoaded;
	uint16_t currOrder, currPattern, currBPM;
	uint32_t rowsCounter, rowsInTotal;
	moduleHeader_t head;
	moduleSample_t samples[MOD_SAMPLES];
	moduleChannel_t channels[AMIGA_VOICES];
	note_t *patterns[MAX_PATTERNS];
} module_t;

struct cpu_t
{
	bool hasSSE, hasSSE2;
} cpu;

struct audio_t
{
	uint16_t bpmTab[256-32], bpmTab28kHz[256-32], bpmTab22kHz[256-32];
	uint32_t audioFreq, audioBufferSize;
	double dAudioFreq, dPeriodToDeltaDiv;
} audio;

struct input_t
{
	struct keyb_t
	{
		bool repeatKey, delayKey;
		bool shiftPressed, leftCtrlPressed, leftAltPressed;
		bool leftCommandPressed, leftAmigaPressed, keypadEnterPressed;
		uint8_t repeatCounter, delayCounter;
		uint64_t repeatFrac;
		SDL_Scancode lastRepKey, lastKey;
	} keyb;

	struct mouse_t
	{
		volatile bool setPosFlag;
		bool buttonWaiting, leftButtonPressed, rightButtonPressed;
		uint8_t repeatCounter, buttonWaitCounter;
		int32_t setPosX, setPosY, lastGUIButton, lastSmpFilterButton, prevX, prevY;
		int16_t x, y, lastMouseX;
	} mouse;
} input;

// this is massive...
struct editor_t
{
	volatile int8_t vuMeterVolumes[AMIGA_VOICES], spectrumVolumes[SPECTRUM_BAR_NUM];
	volatile int8_t *sampleFromDisp, *sampleToDisp, *currSampleDisp, realVuMeterVolumes[AMIGA_VOICES];
	volatile bool songPlaying, programRunning, isWAVRendering, isSMPRendering, smpRenderingDone;
	volatile uint8_t modTick, modSpeed;
	volatile uint16_t *quantizeValueDisp, *metroSpeedDisp, *metroChannelDisp, *sampleVolDisp;
	volatile uint16_t *vol1Disp, *vol2Disp, *currEditPatternDisp, *currPosDisp, *currPatternDisp;
	volatile uint16_t *currPosEdPattDisp, *currLengthDisp, *lpCutOffDisp, *hpCutOffDisp;
	volatile uint16_t *samplePosDisp, *chordLengthDisp;

	char mixText[16];
	char *entryNameTmp, *currPath, *dropTempFileName;
	UNICHAR *fileNameTmpU, *currPathU, *modulesPathU, *samplesPathU;

	bool errorMsgActive, errorMsgBlock, multiFlag, metroFlag, keypadToggle8CFlag, normalizeFiltersFlag;
	bool sampleAllFlag, halfClipFlag, newOldFlag, pat2SmpHQ, mixFlag, useLEDFilter;
	bool modLoaded, fullscreen, autoInsFlag, repeatKeyFlag, sampleZero, tuningFlag;
	bool stepPlayEnabled, stepPlayBackwards, blockBufferFlag, blockMarkFlag, didQuantize;
	bool swapChannelFlag, configFound, abortMod2Wav, chordLengthMin, rowVisitTable[MOD_ORDERS * MOD_ROWS];
	bool muted[AMIGA_VOICES];

	int8_t smpRedoFinetunes[MOD_SAMPLES], smpRedoVolumes[MOD_SAMPLES], multiModeNext[4], trackPattFlag;
	int8_t *smpRedoBuffer[MOD_SAMPLES], *tempSample, currSample, recordMode, sampleFrom, sampleTo, autoInsSlot;
	int8_t keypadSampleOffset, note1, note2, note3, note4, oldNote1, oldNote2, oldNote3, oldNote4;
	uint8_t playMode, currMode, tuningChan, tuningVol, errorMsgCounter, buffFromPos, buffToPos;
	uint8_t blockFromPos, blockToPos, timingMode, f6Pos, f7Pos, f8Pos, f9Pos, f10Pos, keyOctave, pNoteFlag;
	uint8_t tuningNote, resampleNote, initialTempo, initialSpeed, editMoveAdd;

	int16_t *mod2WavBuffer, *pat2SmpBuf, modulateSpeed;
	uint16_t metroSpeed, metroChannel, sampleVol, samplePos, chordLength;
	uint16_t effectMacros[10], oldTempo, currPlayNote, vol1, vol2, lpCutOff, hpCutOff;
	int32_t smpRedoLoopStarts[MOD_SAMPLES], smpRedoLoopLengths[MOD_SAMPLES], smpRedoLengths[MOD_SAMPLES];
	int32_t modulatePos, modulateOffset, markStartOfs, markEndOfs;
	uint32_t musicTime, vblankTimeLen, vblankTimeLenFrac, pat2SmpPos;
	double dPerfFreq, dPerfFreqMulMicro;
	note_t trackBuffer[MOD_ROWS], cmdsBuffer[MOD_ROWS], blockBuffer[MOD_ROWS];
	note_t patternBuffer[MOD_ROWS * AMIGA_VOICES], undoBuffer[MOD_ROWS * AMIGA_VOICES];
	SDL_Thread *mod2WavThread, *pat2SmpThread;

	struct diskop_t
	{
		volatile bool cached, isFilling, forceStopReading;
		bool modPackFlg;
		int8_t mode, smpSaveType;
		int32_t numEntries, scrollOffset;
		SDL_Thread *fillThread;
	} diskop;

	struct cursor_t
	{
		uint8_t lastPos, pos, mode, channel;
		uint32_t bgBuffer[11 * 14];
	} cursor;

	struct text_offsets_t
	{
		uint16_t diskOpPath;
	} textofs;

	struct ui_t
	{
		char statusMessage[18], prevStatusMessage[18];
		char *dstPtr, *editPos, *textEndPtr, *showTextPtr;

		bool answerNo, answerYes, throwExit, editTextFlag, askScreenShown, samplerScreenShown;
		bool leftLoopPinMoving, rightLoopPinMoving, changingSmpResample, changingDrumPadNote;
		bool forceSampleDrag, forceSampleEdit, introScreenShown;
		bool aboutScreenShown, clearScreenShown, posEdScreenShown, diskOpScreenShown;
		bool samplerVolBoxShown, samplerFiltersBoxShown, editOpScreenShown;

		int8_t *numPtr8, tmpDisp8, pointerMode, editOpScreen, editTextType, askScreenType;
		int8_t visualizerMode, previousPointerMode, forceVolDrag, changingChordNote;
		uint8_t numLen, numBits;

		// render/update flags
		bool updateStatusText, updatePatternData;
		bool updateSongName, updateMod2WavDialog, mod2WavFinished;

		// edit op. #2
		bool updateRecordText, updateQuantizeText, updateMetro1Text, updateMetro2Text;
		bool updateFromText, updateKeysText, updateToText;

		// edit op. #3
		bool updateMixText, updatePosText, updateModText, updateVolText;

		// edit op. #4 (sample chord editor)
		bool updateLengthText, updateNote1Text, updateNote2Text;
		bool updateNote3Text, updateNote4Text;

		//sampler
		bool updateResampleNote, updateVolFromText, updateVolToText, updateLPText;
		bool updateHPText, updateNormFlag, update9xxPos;

		// general
		bool updateSongPos, updateSongPattern, updateSongLength, updateCurrSampleFineTune;
		bool updateCurrSampleNum, updateCurrSampleVolume, updateCurrSampleLength;
		bool updateCurrSampleRepeat, updateCurrSampleReplen, updateCurrSampleName;
		bool updateSongSize, updateSongTiming, updateSongBPM;
		bool updateCurrPattText, updateTrackerFlags, pat2SmpDialogShown;

		// disk op.
		bool updateLoadMode, updatePackText, updateSaveFormatText, updateDiskOpPathText;

		// pos ed.
		bool updatePosEd, updateDiskOpFileList;

		// these are used when things are drawn on top, for example clear/ask dialogs
		bool disablePosEd, disableVisualizer;

		bool vsync60HzPresent;
		int16_t lineCurX, lineCurY, editObject, sampleMarkingPos;
		uint16_t *numPtr16, tmpDisp16, *dstOffset, dstPos, textLength, editTextPos;
		uint16_t dstOffsetEnd, lastSampleOffset;
		int32_t askTempData, renderX, renderY, renderW, renderH, displayW, displayH;
		uint32_t xScale, yScale;
		double dMouseXMul, dMouseYMul;
		SDL_PixelFormat *pixelFormat;
#ifdef _WIN32
		HWND hWnd;
#endif
	} ui;

	struct sampler_t
	{
		const int8_t *samStart;
		int8_t *blankSample, *copyBuf;
		int16_t loopStartPos, loopEndPos;
		uint16_t dragStart, dragEnd, saveMouseX, lastSamPos;
		int32_t samPointWidth, samOffset, samDisplay, samLength;
		int32_t lastMouseX, lastMouseY, tmpLoopStart, tmpLoopLength;
		uint32_t copyBufSize, samDrawStart, samDrawEnd;
	} sampler;
} editor;

void restartSong(void);
void resetSong(void);
void incPatt(void);
void decPatt(void);
void modSetPos(int16_t order, int16_t row);
void modStop(void);
void doStopIt(void);
void playPattern(int8_t startRow);
void modPlay(int16_t patt, int16_t order, int8_t row);
void modSetSpeed(uint8_t speed);
void modSetTempo(uint16_t bpm);
void modFree(void);
bool setupAudio(void);
void audioClose(void);
void clearSong(void);
void clearSamples(void);
void clearAll(void);
void modSetPattern(uint8_t pattern);

extern module_t *modEntry; // pt_main.c
