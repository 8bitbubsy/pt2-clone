#pragma once

#include <SDL2/SDL.h>
#include <stdint.h>
#include <stdbool.h>

void saveUndo(void);
void undoLastChange(void);
void copySampleTrack(void);
void delSampleTrack(void);
void exchSampleTrack(void);
void trackNoteUp(bool sampleAllFlag, uint8_t from, uint8_t to);
void trackNoteDown(bool sampleAllFlag, uint8_t from, uint8_t to);
void trackOctaUp(bool sampleAllFlag, uint8_t from, uint8_t to);
void trackOctaDown(bool sampleAllFlag, uint8_t from, uint8_t to);
void pattNoteUp(bool sampleAllFlag);
void pattNoteDown(bool sampleAllFlag);
void pattOctaUp(bool sampleAllFlag);
void pattOctaDown(bool sampleAllFlag);
void handleEditKeys(SDL_Scancode scancode, bool normalMode);
bool handleSpecialKeys(SDL_Scancode scancode);
int8_t keyToNote(SDL_Scancode scancode);
void handleSampleJamming(SDL_Scancode scancode);
