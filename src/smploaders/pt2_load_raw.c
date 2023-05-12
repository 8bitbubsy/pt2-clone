#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include "../pt2_header.h"
#include "../pt2_config.h"
#include "../pt2_structs.h"
#include "../pt2_replayer.h"

bool loadRAWSample(FILE *f, uint32_t filesize, moduleSample_t *s)
{
	uint32_t sampleLength = filesize;
	if (sampleLength > (uint32_t)config.maxSampleLength)
		sampleLength = config.maxSampleLength;

	int8_t *smpDataPtr = &song->sampleData[s->offset];

	turnOffVoices();
	fread(smpDataPtr, 1, sampleLength, f);

	if (sampleLength & 1)
	{
		if (++sampleLength > (uint32_t)config.maxSampleLength)
			sampleLength = config.maxSampleLength;
	}

	s->volume = 64;
	s->fineTune = 0;
	s->length = sampleLength;
	s->loopStart = 0;
	s->loopLength = 2;

	return true;
}