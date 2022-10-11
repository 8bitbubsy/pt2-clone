#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "pt2_structs.h"

#define REPLAYER_MIN_BPM 32

double ciaBpm2Hz(int32_t bpm);
void updatePaulaLoops(void); // used after manipulating sample loop points while Paula is live
void turnOffVoices(void);
void initializeModuleChannels(module_t *s);
int8_t *allocMemForAllSamples(void);
void setReplayerPosToTrackerPos(void);
void setPattern(int16_t pattern);
bool intMusic(void);
void storeTempVariables(void);
void restartSong(void);
void resetSong(void);
void incPatt(void);
void decPatt(void);
void modSetPos(int16_t order, int16_t row);
void modStop(void);
void doStopIt(bool resetPlayMode);
void playPattern(int8_t startRow);
void modPlay(int16_t patt, int16_t order, int8_t row);
void modSetSpeed(int32_t speed);
void modSetTempo(int32_t bpm, bool doLockAudio);
void modFree(void);
void clearSong(void);
void clearSamples(void);
void modSetPattern(uint8_t pattern);
