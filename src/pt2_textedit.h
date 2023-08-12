#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <SDL2/SDL.h>

void renderTextEditCursor(void);
void removeTextEditCursor(void);
void editTextPrevChar(void);
void editTextNextChar(void);
void handleTextEditing(uint8_t mouseButton);
void enterTextEditMode(int16_t editObject);
void enterNumberEditMode(uint8_t type, int16_t editObject);
void leaveTextEditMode(bool updateValue);
void handleTextEditInputChar(char textChar);
bool handleTextEditMode(SDL_Scancode scancode);
