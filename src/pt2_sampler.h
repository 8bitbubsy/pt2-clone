#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "pt2_structs.h"

typedef struct sampler_t
{
	const int8_t *samStart;
	int8_t *blankSample, *copyBuf;
	int16_t loopStartPos, loopEndPos;
	uint16_t dragStart, dragEnd;
	int32_t samPointWidth, samOffset, samDisplay, samLength, saveMouseX, lastSamPos;
	int32_t lastMouseX, lastMouseY, tmpLoopStart, tmpLoopLength;
	uint32_t copyBufSize, samDrawStart, samDrawEnd;
} sampler_t;

extern sampler_t sampler; // pt2_sampler.c

void downSample(void);
void upSample(void);
void createSampleMarkTable(void);
int32_t smpPos2Scr(int32_t pos);
int32_t scr2SmpPos(int32_t x);
void fixSampleBeep(moduleSample_t *s);
void highPassSample(int32_t cutOff);
void lowPassSample(int32_t cutOff);
void samplerRemoveDcOffset(void);
void mixChordSample(void);
void samplerResample(void);
void doMix(void);
void boostSample(int32_t sample, bool ignoreMark);
void filterSample(int32_t sample, bool ignoreMark);
void toggleTuningTone(void);
void samplerSamDelete(uint8_t cut);
void samplerSamPaste(void);
void samplerSamCopy(void);
void samplerLoopToggle(void);
void samplerBarPressed(bool mouseButtonHeld);
void samplerEditSample(bool mouseButtonHeld);
void samplerSamplePressed(bool mouseButtonHeld);
void volBoxBarPressed(bool mouseButtonHeld);
void samplerZoomInMouseWheel(void);
void samplerZoomOutMouseWheel(void);
void samplerZoomOut2x(void);
void sampleMarkerToBeg(void);
void sampleMarkerToCenter(void);
void sampleMarkerToEnd(void);
void samplerPlayWaveform(void);
void samplerPlayDisplay(void);
void samplerPlayRange(void);
void samplerRangeAll(void);
void samplerShowRange(void);
void samplerShowAll(void);
void redoSampleData(int8_t sample);
void fillSampleRedoBuffer(int8_t sample);
void updateSamplePos(void);
void fillSampleFilterUndoBuffer(void);
void exitFromSam(void);
void samplerScreen(void);
void displaySample(void);
void redrawSample(void);
bool allocSamplerVars(void);
void deAllocSamplerVars(void);
void setLoopSprites(void);
void drawSamplerLine(void);
