#pragma once

#include <stdint.h>
#include <stdbool.h>

bool DetectXPK(FILE *f);
bool UnpackXPK(FILE *f, uint32_t *filesize, uint8_t **out);
