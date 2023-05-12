#pragma once

#include <stdint.h>
#include <stdbool.h>

bool detectXPK(FILE *f);
bool unpackXPK(FILE *f, uint32_t *filesize, uint8_t **out);
