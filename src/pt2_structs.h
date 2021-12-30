#pragma once

#ifdef _WIN32
#define WIN32_MEAN_AND_LEAN
#include <windows.h>
#endif

#include <stdint.h>
#include <stdbool.h>
#include "pt2_header.h"
#include "pt2_hpc.h"

// for .WAV sample loading/saving
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

typedef struct mptExtraChunk_t
{
	uint32_t chunkID, chunkSize, flags;
	uint16_t defaultPan, defaultVolume, globalVolume, reserved;
	uint8_t vibratoType, vibratoSweep, vibratoDepth, vibratoRate;
} mptExtraChunk_t;
// -----------------------------------------

typedef struct note_t
{
	uint8_t param, sample, command;
	uint16_t period;
} note_t;

typedef struct moduleHeader_t
{
	char name[20 + 1];
	uint16_t order[MOD_ORDERS], numOrders;
	uint16_t initialTempo; // used for STK/UST modules after module is loaded
} moduleHeader_t;

typedef struct moduleSample_t
{
	volatile int8_t *volumeDisp;
	volatile int32_t *lengthDisp, *loopStartDisp, *loopLengthDisp;
	char text[22 + 1];
	int8_t volume;
	uint8_t fineTune;
	int32_t offset, length, loopStart, loopLength;
} moduleSample_t;

typedef struct moduleChannel_t
{
	int8_t *n_start, *n_wavestart, *n_loopstart, n_chanindex, n_volume;
	int8_t n_toneportdirec, n_pattpos, n_loopcount;
	uint8_t n_wavecontrol, n_glissfunk, n_sampleoffset, n_toneportspeed;
	uint8_t n_vibratocmd, n_tremolocmd, n_finetune, n_funkoffset, n_samplenum;
	uint8_t n_vibratopos, n_tremolopos;
	int16_t n_period, n_note, n_wantedperiod;
	uint16_t n_cmd, n_length, n_replen;
	uint32_t n_scopedelta;

	// for pt2_sync.c
	uint8_t syncFlags;
	int8_t syncAnalyzerVolume, syncVuVolume;
	uint16_t syncAnalyzerPeriod;
} moduleChannel_t;

typedef struct module_t
{
	bool loaded, modified;
	int8_t *sampleData;

	volatile int32_t tick, speed;

	int8_t row; // used for different things, so must not be internal to replayer

	moduleHeader_t header;
	moduleSample_t samples[MOD_SAMPLES];
	moduleChannel_t channels[AMIGA_VOICES];
	note_t *patterns[MAX_PATTERNS];

	// for pattern viewer
	int8_t currRow;
	int32_t currSpeed, currBPM;
	uint16_t currOrder, currPattern;

	// for MOD2WAV progress bar
	uint32_t rowsCounter, rowsInTotal;
} module_t;

typedef struct keyb_t
{
	bool repeatKey, delayKey;
	bool shiftPressed, leftCtrlPressed, leftAltPressed;
	bool leftCommandPressed, leftAmigaPressed, keypadEnterPressed;
	uint8_t repeatCounter, delayCounter;
	uint64_t repeatDelta, repeatFrac;
	SDL_Scancode lastRepKey, lastKey;
} keyb_t;

typedef struct mouse_t
{
	volatile bool setPosFlag, resetCursorColorFlag;
	bool buttonWaiting, leftButtonPressed, rightButtonPressed;
	uint8_t repeatCounter, buttonWaitCounter;
	int32_t x, y, lastMouseX, setPosX, setPosY, lastGUIButton, prevX, prevY;
	int32_t lastSmpFilterButton, lastSamplingButton;
	uint32_t buttonState;
} mouse_t;

typedef struct video_t
{
	bool fullscreen, vsync60HzPresent, windowHidden, useDesktopMouseCoords;
	int32_t renderX, renderY, renderW, renderH, displayW, displayH;
	int32_t xScale, yScale;
	float fMouseXMul, fMouseYMul;
	hpc_t vblankHpc;
	SDL_PixelFormat *pixelFormat;
	uint32_t *frameBuffer, *frameBufferUnaligned;

	SDL_Window *window;
	SDL_Renderer *renderer;
	SDL_Texture  *texture;

	uint32_t palette[PALETTE_NUM];

#ifdef _WIN32
	HWND hWnd;
#endif
} video_t;

typedef struct editor_t
{
	volatile int8_t vuMeterVolumes[AMIGA_VOICES], spectrumVolumes[SPECTRUM_BAR_NUM];
	volatile int8_t *sampleFromDisp, *sampleToDisp, *currSampleDisp, realVuMeterVolumes[AMIGA_VOICES];
	volatile bool songPlaying, programRunning, isWAVRendering, isSMPRendering, smpRenderingDone;
	volatile uint16_t *quantizeValueDisp, *metroSpeedDisp, *metroChannelDisp, *sampleVolDisp;
	volatile uint16_t *vol1Disp, *vol2Disp, *currEditPatternDisp, *currPosDisp, *currPatternDisp;
	volatile uint16_t *currPosEdPattDisp, *currLengthDisp, *lpCutOffDisp, *hpCutOffDisp;
	volatile int32_t *samplePosDisp, *chordLengthDisp;

	char mixText[16];
	char *entryNameTmp, *currPath, *dropTempFileName;
	UNICHAR *fileNameTmpU, *currPathU, *modulesPathU, *samplesPathU;

	bool errorMsgActive, errorMsgBlock, multiFlag, metroFlag, keypadToggle8CFlag, normalizeFiltersFlag;
	bool sampleAllFlag, halfClipFlag, newOldFlag, pat2SmpHQ, mixFlag, useLEDFilter;
	bool modLoaded, autoInsFlag, repeatKeyFlag, sampleZero, tuningFlag;
	bool stepPlayEnabled, stepPlayBackwards, blockBufferFlag, blockMarkFlag, didQuantize;
	bool swapChannelFlag, configFound, abortMod2Wav, chordLengthMin, rowVisitTable[MOD_ORDERS * MOD_ROWS];
	bool muted[AMIGA_VOICES];

	int8_t smpRedoFinetunes[MOD_SAMPLES], smpRedoVolumes[MOD_SAMPLES], multiModeNext[4], trackPattFlag;
	int8_t *smpRedoBuffer[MOD_SAMPLES], *tempSample, currSample, recordMode, sampleFrom, sampleTo, autoInsSlot;
	int8_t hiLowInstr, note1, note2, note3, note4, oldNote1, oldNote2, oldNote3, oldNote4;
	uint8_t playMode, currMode, tuningChan, tuningVol, errorMsgCounter, buffFromPos, buffToPos;
	uint8_t blockFromPos, blockToPos, timingMode, f6Pos, f7Pos, f8Pos, f9Pos, f10Pos, keyOctave, pNoteFlag;
	uint8_t tuningNote, resampleNote, initialTempo, initialSpeed, editMoveAdd;

	int16_t modulateSpeed;
	uint16_t metroSpeed, metroChannel, sampleVol;
	uint16_t effectMacros[10], currPlayNote, vol1, vol2, lpCutOff, hpCutOff;
	int32_t smpRedoLoopStarts[MOD_SAMPLES], smpRedoLoopLengths[MOD_SAMPLES], smpRedoLengths[MOD_SAMPLES];
	int32_t oldTempo, modulatePos, modulateOffset, markStartOfs, markEndOfs, pat2SmpPos, samplePos, chordLength;
	uint64_t musicTime64;
	double *dPat2SmpBuf;
	note_t trackBuffer[MOD_ROWS], cmdsBuffer[MOD_ROWS], blockBuffer[MOD_ROWS];
	note_t patternBuffer[MOD_ROWS * AMIGA_VOICES], undoBuffer[MOD_ROWS * AMIGA_VOICES];
	SDL_Thread *mod2WavThread, *pat2SmpThread;
} editor_t;

typedef struct diskop_t
{
	volatile bool cached, isFilling, forceStopReading;
	bool modPackFlg;
	int8_t mode, smpSaveType;
	int32_t numEntries, scrollOffset;
	SDL_Thread *fillThread;
} diskop_t;

typedef struct cursor_t
{
	uint8_t lastPos, pos, mode, channel;
	uint32_t bgBuffer[11 * 14];
} cursor_t;

typedef struct ui_t
{
	char statusMessage[18], prevStatusMessage[18];
	char *dstPtr, *editPos, *textEndPtr, *showTextPtr;

	bool answerNo, answerYes, throwExit, editTextFlag, askScreenShown, samplerScreenShown;
	bool leftLoopPinMoving, rightLoopPinMoving, changingSmpResample, changingDrumPadNote;
	bool forceSampleDrag, forceSampleEdit, introScreenShown;
	bool aboutScreenShown, clearScreenShown, posEdScreenShown, diskOpScreenShown;
	bool samplerVolBoxShown, samplerFiltersBoxShown, samplingBoxShown, editOpScreenShown;

	bool changingSamplingNote;

	bool force32BitNumPtr;
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
	bool updateChordLengthText, updateChordNote1Text, updateChordNote2Text;
	bool updateChordNote3Text, updateChordNote4Text;

	// sampler
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

	int16_t lineCurX, lineCurY, editObject, sampleMarkingPos;
	uint16_t *numPtr16, tmpDisp16, *dstOffset, dstPos, textLength, editTextPos;
	uint16_t dstOffsetEnd, lastSampleOffset, diskOpPathTextOffset;
	int32_t askTempData, *numPtr32, tmpDisp32;
} ui_t;

extern keyb_t keyb;
extern mouse_t mouse;
extern video_t video;
extern editor_t editor;
extern diskop_t diskop;
extern cursor_t cursor;
extern ui_t ui;

extern module_t *song; // pt2_main.c
