// for finding memory leaks in debug mode with Visual Studio 
#if defined _DEBUG && defined _MSC_VER
#include <crtdbg.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include "pt2_header.h"
#include "pt2_helpers.h"
#include "pt2_textout.h"
#include "pt2_audio.h"
#include "pt2_palette.h"
#include "pt2_tables.h"
#include "pt2_visuals.h"
#include "pt2_blep.h"
#include "pt2_mouse.h"
#include "pt2_scopes.h"

#define CENTER_LINE_COLOR 0x303030
#define MARK_COLOR_1 0x666666 /* inverted background */
#define MARK_COLOR_2 0xCCCCCC /* inverted waveform */
#define MARK_COLOR_3 0x7D7D7D /* inverted center line */

#define SAMPLE_AREA_Y_CENTER 169
#define SAMPLE_AREA_HEIGHT 64

typedef struct sampleMixer_t
{
	int32_t length, pos;
	uint32_t posFrac, delta;
} sampleMixer_t;

static int32_t samOffsetScaled;
static uint32_t waveInvertTable[8];

static const int8_t tuneToneData[32] = // Tuning Tone (Sine Wave)
{
	   0,  25,  49,  71,  91, 106, 118, 126,
	 127, 126, 118, 106,  91,  71,  49,  25,
	   0, -25, -49, -71, -91,-106,-118,-126,
	-127,-126,-118,-106, -91, -71, -49, -25
};

extern uint32_t *pixelBuffer; // pt_main.c

void setLoopSprites(void);

void createSampleMarkTable(void)
{
	// used for invertRange()  (sample data marking)

	waveInvertTable[0] = 0x00000000 | palette[PAL_BACKGRD];
	waveInvertTable[1] = 0x01000000 | palette[PAL_QADSCP];
	waveInvertTable[2] = 0x02000000 | CENTER_LINE_COLOR;
	waveInvertTable[3] = 0x03000000; // spacer, not used
	waveInvertTable[4] = 0x04000000 | MARK_COLOR_1;
	waveInvertTable[5] = 0x05000000 | MARK_COLOR_2;
	waveInvertTable[6] = 0x06000000 | MARK_COLOR_3;
	waveInvertTable[7] = 0x07000000; // spacer, not used
}

static void updateSamOffset(void)
{
	if (editor.sampler.samDisplay == 0)
		samOffsetScaled = 0;
	else
		samOffsetScaled = (editor.sampler.samOffset * SAMPLE_AREA_WIDTH) / editor.sampler.samDisplay; // truncate here
}

void fixSampleBeep(moduleSample_t *s)
{
	if (s->length >= 2 && s->loopStart+s->loopLength <= 2)
	{
		modEntry->sampleData[s->offset+0] = 0;
		modEntry->sampleData[s->offset+1] = 0;
	}
}

void updateSamplePos(void)
{
	moduleSample_t *s;

	assert(editor.currSample >= 0 && editor.currSample <= 30);
	if (editor.currSample >= 0 && editor.currSample <= 30)
	{
		s = &modEntry->samples[editor.currSample];
		if (editor.samplePos > s->length)
			editor.samplePos = s->length;

		if (editor.ui.editOpScreenShown && editor.ui.editOpScreen == 2)
			editor.ui.updatePosText = true;
	}
}

void fillSampleFilterUndoBuffer(void)
{
	moduleSample_t *s;

	assert(editor.currSample >= 0 && editor.currSample <= 30);
	if (editor.currSample >= 0 && editor.currSample <= 30)
	{
		s = &modEntry->samples[editor.currSample];
		memcpy(editor.tempSample, &modEntry->sampleData[s->offset], s->length);
	}
}

static void sampleLine(uint32_t *frameBuffer, int16_t line_x1, int16_t line_x2, int16_t line_y1, int16_t line_y2)
{
	int16_t d, x, y, ax, ay, sx, sy, dx, dy;
	uint32_t color = 0x01000000 | palette[PAL_QADSCP];

	assert(line_x1 >= 0 || line_x2 >= 0 || line_x1 < SCREEN_W || line_x2 < SCREEN_W);
	assert(line_y1 >= 0 || line_y2 >= 0 || line_y1 < SCREEN_H || line_y2 < SCREEN_H);

	dx = line_x2 - line_x1;
	ax = ABS(dx) * 2;
	sx = SGN(dx);
	dy = line_y2 - line_y1;
	ay = ABS(dy) * 2;
	sy = SGN(dy);
	x  = line_x1;
	y  = line_y1;

	if (ax > ay)
	{
		d = ay - ((uint16_t)ax / 2);
		while (true)
		{
			assert(y >= 0 || x >= 0 || y < SCREEN_H || x < SCREEN_W);

			frameBuffer[(y * SCREEN_W) + x] = color;

			if (x == line_x2)
				break;

			if (d >= 0)
			{
				y += sy;
				d -= ax;
			}

			x += sx;
			d += ay;
		}
	}
	else
	{
		d = ax - ((uint16_t)ay / 2);
		while (true)
		{
			assert(y >= 0 || x >= 0 || y < SCREEN_H || x < SCREEN_W);

			frameBuffer[(y * SCREEN_W) + x] = color;

			if (y == line_y2)
				break;

			if (d >= 0)
			{
				x += sx;
				d -= ay;
			}

			y += sy;
			d += ax;
		}
	}
}

static void setDragBar(void)
{
	int32_t pos;
	uint32_t *dstPtr, pixel, bgPixel;

	if (editor.sampler.samLength > 0 && editor.sampler.samDisplay != editor.sampler.samLength)
	{
		int32_t roundingBias = (uint32_t)editor.sampler.samLength / 2;

		// update drag bar coordinates
		pos = ((editor.sampler.samOffset * 311) + roundingBias) / editor.sampler.samLength;
		editor.sampler.dragStart = pos + 4;
		editor.sampler.dragStart = CLAMP(editor.sampler.dragStart, 4, 315);

		pos = (((editor.sampler.samDisplay + editor.sampler.samOffset) * 311) + roundingBias) / editor.sampler.samLength;
		editor.sampler.dragEnd = pos + 5;
		editor.sampler.dragEnd = CLAMP(editor.sampler.dragEnd, 5, 316);

		if (editor.sampler.dragStart > editor.sampler.dragEnd-1)
			editor.sampler.dragStart = editor.sampler.dragEnd-1;

		// draw drag bar

		dstPtr = &pixelBuffer[206 * SCREEN_W];
		pixel = palette[PAL_QADSCP];
		bgPixel = palette[PAL_BACKGRD];

		for (int32_t y = 0; y < 4; y++)
		{
			for (int32_t x = 4; x < 316; x++)
			{
				if (x >= editor.sampler.dragStart && x <= editor.sampler.dragEnd)
					dstPtr[x] = pixel; // drag bar
				else
					dstPtr[x] = bgPixel; // background
			}

			dstPtr += SCREEN_W;
		}
	}
	else
	{
		// clear drag bar background

		dstPtr = &pixelBuffer[(206 * SCREEN_W) + 4];
		pixel = palette[PAL_BACKGRD];

		for (int32_t y = 0; y < 4; y++)
		{
			for (int32_t x = 0; x < 312; x++)
				dstPtr[x] = pixel;

			dstPtr += SCREEN_W;
		}
	}
}

static int8_t getScaledSample(int32_t index)
{
	const int8_t *ptr8;

	if (editor.sampler.samLength <= 0 || index < 0 || index > editor.sampler.samLength)
		return 0;

	ptr8 = editor.sampler.samStart;
	if (ptr8 == NULL)
		return 0;

	return ptr8[index] >> 2;
}

int32_t smpPos2Scr(int32_t pos) // sample pos -> screen x pos
{
	if (editor.sampler.samDisplay == 0)
		return 0;

	uint32_t roundingBias = (uint32_t)editor.sampler.samDisplay >> 1;

	pos = (((uint32_t)pos * SAMPLE_AREA_WIDTH) + roundingBias) / (uint32_t)editor.sampler.samDisplay; // rounded
	pos -= samOffsetScaled;

	return pos;
}

int32_t scr2SmpPos(int32_t x) // screen x pos -> sample pos
{
	if (editor.sampler.samDisplay == 0)
		return 0;

	if (x < 0)
		x = 0;

	x += samOffsetScaled;
	x = (uint32_t)(x * editor.sampler.samDisplay) / SAMPLE_AREA_WIDTH; // truncate here

	return x;
}

static void getSampleDataPeak(int8_t *smpPtr, int32_t numBytes, int16_t *outMin, int16_t *outMax)
{
	int8_t smp, smpMin, smpMax;

	smpMin = 127;
	smpMax = -128;

	for (int32_t i = 0; i < numBytes; i++)
	{
		smp = smpPtr[i];
		if (smp < smpMin) smpMin = smp;
		if (smp > smpMax) smpMax = smp;
	}

	*outMin = SAMPLE_AREA_Y_CENTER - (smpMin >> 2);
	*outMax = SAMPLE_AREA_Y_CENTER - (smpMax >> 2);
}

static void renderSampleData(void)
{
	int8_t *smpPtr;
	int16_t y1, y2, min, max, oldMin, oldMax;
	int32_t x, y, smpIdx, smpNum;
	uint32_t *dstPtr, pixel;
	moduleSample_t *s;

	s = &modEntry->samples[editor.currSample];

	// clear sample data background

	dstPtr = &pixelBuffer[(138 * SCREEN_W) + 3];
	pixel = palette[PAL_BACKGRD];

	for (y = 0; y < SAMPLE_VIEW_HEIGHT; y++)
	{
		for (x = 0; x < SAMPLE_AREA_WIDTH; x++)
			dstPtr[x] = pixel;

		dstPtr += SCREEN_W;
	}

	// display center line
	if (ptConfig.dottedCenterFlag)
	{
		dstPtr = &pixelBuffer[(SAMPLE_AREA_Y_CENTER * SCREEN_W) + 3];
		for (x = 0; x < SAMPLE_AREA_WIDTH; x++)
			dstPtr[x] = 0x02000000 | CENTER_LINE_COLOR;
	}

	// render sample data
	if (editor.sampler.samDisplay >= 0 && editor.sampler.samDisplay <= MAX_SAMPLE_LEN)
	{
		y1 = SAMPLE_AREA_Y_CENTER - getScaledSample(scr2SmpPos(0));

		if (editor.sampler.samDisplay <= SAMPLE_AREA_WIDTH)
		{
			// 1:1 or zoomed in
			for (x = 1; x < SAMPLE_AREA_WIDTH; x++)
			{
				y2 = SAMPLE_AREA_Y_CENTER - getScaledSample(scr2SmpPos(x));
				sampleLine(pixelBuffer, x + 2, x + 3, y1, y2);
				y1 = y2;
			}
		}
		else
		{
			// zoomed out

			oldMin = y1;
			oldMax = y1;

			smpPtr = &modEntry->sampleData[s->offset];
			for (x = 0; x < SAMPLE_AREA_WIDTH; x++)
			{
				smpIdx = scr2SmpPos(x);
				smpNum = scr2SmpPos(x+1) - smpIdx;

				// prevent look-up overflow (yes, this can happen near the end of the sample)
				if (smpIdx+smpNum > editor.sampler.samLength)
					smpNum = editor.sampler.samLength - smpNum;

				if (smpNum < 1)
					smpNum = 1;

				getSampleDataPeak(&smpPtr[smpIdx], smpNum, &min, &max);

				if (x > 0)
				{
					if (min > oldMax) sampleLine(pixelBuffer, x + 2, x + 3, oldMax, min);
					if (max < oldMin) sampleLine(pixelBuffer, x + 2, x + 3, oldMin, max);
				}

				sampleLine(pixelBuffer, x + 3, x + 3, max, min);

				oldMin = min;
				oldMax = max;
			}
		}
	}

	// render "sample display" text
	if (editor.sampler.samStart == editor.sampler.blankSample)
		printFiveDecimalsBg(pixelBuffer, 272, 214, 0, palette[PAL_GENTXT], palette[PAL_GENBKG]);
	else
		printFiveDecimalsBg(pixelBuffer, 272, 214, editor.sampler.samDisplay, palette[PAL_GENTXT], palette[PAL_GENBKG]);

	setDragBar();
	setLoopSprites();
}

void invertRange(void)
{
	int32_t x, y, rangeLen, start, end;
	uint32_t *dstPtr;

	if (editor.markStartOfs == -1)
		return; // no marking

	start = smpPos2Scr(editor.markStartOfs);
	end = smpPos2Scr(editor.markEndOfs);

	if (editor.sampler.samDisplay < editor.sampler.samLength && (start >= SAMPLE_AREA_WIDTH || end < 0))
		return; // range is outside of view

	start = CLAMP(start, 0, SAMPLE_AREA_WIDTH-1);
	end = CLAMP(end, 0, SAMPLE_AREA_WIDTH-1);

	rangeLen = (end + 1) - start;
	if (rangeLen < 1)
		rangeLen = 1;

	dstPtr = &pixelBuffer[(138 * SCREEN_W) + (start + 3)];
	for (y = 0; y < 64; y++)
	{
		for (x = 0; x < rangeLen; x++)
			dstPtr[x] = waveInvertTable[((dstPtr[x] >> 24) & 7) ^ 4]; // It's magic! ptr[x]>>24 = wave/invert color number

		dstPtr += SCREEN_W;
	}
}

void displaySample(void)
{
	if (!editor.ui.samplerScreenShown)
		return;

	renderSampleData();
	if (editor.markStartOfs != -1)
		invertRange();

	editor.ui.update9xxPos = true;
}

void redrawSample(void)
{
	moduleSample_t *s;

	if (!editor.ui.samplerScreenShown)
		return;

	assert(editor.currSample >= 0 && editor.currSample <= 30);
	if (editor.currSample >= 0 && editor.currSample <= 30)
	{
		editor.markStartOfs = -1;

		editor.sampler.samOffset = 0;
		updateSamOffset();

		s = &modEntry->samples[editor.currSample];
		if (s->length > 0)
		{
			editor.sampler.samStart = &modEntry->sampleData[s->offset];
			editor.sampler.samDisplay = s->length;
			editor.sampler.samLength = s->length;
		}
		else
		{
			// "blank sample" template
			editor.sampler.samStart = editor.sampler.blankSample;
			editor.sampler.samLength = SAMPLE_AREA_WIDTH;
			editor.sampler.samDisplay = SAMPLE_AREA_WIDTH;
		}

		renderSampleData();
		updateSamplePos();

		editor.ui.update9xxPos = true;
		editor.ui.lastSampleOffset = 0x900;

		// for quadrascope
		editor.sampler.samDrawStart = s->offset;
		editor.sampler.samDrawEnd = s->offset + s->length;
	}
}

void highPassSample(int32_t cutOff)
{
	int32_t smp32, i, from, to;
	double *dSampleData, dBaseFreq, dCutOff;
	moduleSample_t *s;
	lossyIntegrator_t filterHi;

	assert(editor.currSample >= 0 && editor.currSample <= 30);

	if (cutOff == 0)
	{
		displayErrorMsg("CUTOFF CAN'T BE 0");
		return;
	}

	s = &modEntry->samples[editor.currSample];
	if (s->length == 0)
	{
		displayErrorMsg("SAMPLE IS EMPTY");
		return;
	}

	from = 0;
	to = s->length;

	if (editor.markStartOfs != -1)
	{
		from = editor.markStartOfs;
		to = editor.markEndOfs;

		if (to > s->length)
			to = s->length;

		if (from == to)
		{
			from = 0;
			to = s->length;
		}
	}

	dSampleData = (double *)malloc(s->length * sizeof (double));
	if (dSampleData == NULL)
	{
		statusOutOfMemory();
		return;
	}

	fillSampleFilterUndoBuffer();

	// setup filter coefficients

	dBaseFreq = FILTERS_BASE_FREQ;

	dCutOff = (double)cutOff;
	if (dCutOff >= dBaseFreq/2.0)
	{
		dCutOff = dBaseFreq/2.0;
		editor.hpCutOff = (uint16_t)dCutOff;
	}

	calcCoeffLossyIntegrator(dBaseFreq, dCutOff, &filterHi);

	// copy over sample data to double buffer
	for (i = 0; i < s->length; i++)
		dSampleData[i] = modEntry->sampleData[s->offset+i];

	filterHi.dBuffer[0] = 0.0;
	if (to <= s->length)
	{
		for (i = from; i < to; i++)
			lossyIntegratorHighPassMono(&filterHi, dSampleData[i], &dSampleData[i]);
	}

	if (editor.normalizeFiltersFlag)
		normalize8bitDoubleSigned(dSampleData, s->length);

	for (i = from; i < to; i++)
	{
		smp32 = (int32_t)dSampleData[i];
		CLAMP8(smp32);
		modEntry->sampleData[s->offset + i] = (int8_t)smp32;
	}

	free(dSampleData);

	fixSampleBeep(s);
	displaySample();
	updateWindowTitle(MOD_IS_MODIFIED);
}

void lowPassSample(int32_t cutOff)
{
	int32_t smp32, i, from, to;
	double *dSampleData, dBaseFreq, dCutOff;
	moduleSample_t *s;
	lossyIntegrator_t filterLo;

	assert(editor.currSample >= 0 && editor.currSample <= 30);

	if (cutOff == 0)
	{
		displayErrorMsg("CUTOFF CAN'T BE 0");
		return;
	}

	s = &modEntry->samples[editor.currSample];
	if (s->length == 0)
	{
		displayErrorMsg("SAMPLE IS EMPTY");
		return;
	}

	from = 0;
	to = s->length;

	if (editor.markStartOfs != -1)
	{
		from = editor.markStartOfs;
		to = editor.markEndOfs;

		if (to > s->length)
			to = s->length;

		if (from == to)
		{
			from = 0;
			to = s->length;
		}
	}

	dSampleData = (double *)malloc(s->length * sizeof (double));
	if (dSampleData == NULL)
	{
		statusOutOfMemory();
		return;
	}

	fillSampleFilterUndoBuffer();

	// setup filter coefficients

	dBaseFreq = FILTERS_BASE_FREQ;

	dCutOff = (double)cutOff;
	if (dCutOff >= dBaseFreq/2.0)
	{
		dCutOff = dBaseFreq/2.0;
		editor.lpCutOff = (uint16_t)dCutOff;
	}

	calcCoeffLossyIntegrator(dBaseFreq, dCutOff, &filterLo);

	// copy over sample data to double buffer
	for (i = 0; i < s->length; i++)
		dSampleData[i] = modEntry->sampleData[s->offset+i];

	filterLo.dBuffer[0] = 0.0;
	if (to <= s->length)
	{
		for (i = from; i < to; i++)
			lossyIntegratorMono(&filterLo, dSampleData[i], &dSampleData[i]);
	}

	if (editor.normalizeFiltersFlag)
		normalize8bitDoubleSigned(dSampleData, s->length);

	for (i = from; i < to; i++)
	{
		smp32 = (int32_t)dSampleData[i];
		CLAMP8(smp32);
		modEntry->sampleData[s->offset + i] = (int8_t)smp32;
	}

	free(dSampleData);

	fixSampleBeep(s);
	displaySample();
	updateWindowTitle(MOD_IS_MODIFIED);
}

void redoSampleData(int8_t sample)
{
	moduleSample_t *s;

	assert(sample >= 0 && sample <= 30);

	s = &modEntry->samples[sample];

	turnOffVoices();

	if (editor.smpRedoBuffer[sample] != NULL && editor.smpRedoLengths[sample] > 0)
	{
		memcpy(&modEntry->sampleData[s->offset], editor.smpRedoBuffer[sample], editor.smpRedoLengths[sample]);

		if (editor.smpRedoLengths[sample] < MAX_SAMPLE_LEN)
			memset(&modEntry->sampleData[s->offset + editor.smpRedoLengths[sample]], 0, MAX_SAMPLE_LEN - editor.smpRedoLengths[sample]);
	}
	else
	{
		memset(&modEntry->sampleData[s->offset], 0, MAX_SAMPLE_LEN);
	}

	s->fineTune = editor.smpRedoFinetunes[sample];
	s->volume = editor.smpRedoVolumes[sample];
	s->length = editor.smpRedoLengths[sample];
	s->loopStart = editor.smpRedoLoopStarts[sample];
	s->loopLength = (editor.smpRedoLoopLengths[sample] < 2) ? 2 : editor.smpRedoLoopLengths[sample];

	displayMsg("SAMPLE RESTORED !");

	editor.samplePos = 0;
	updateCurrSample();

	// this routine can be called while the sampler toolboxes are open, so redraw them
	if (editor.ui.samplerScreenShown)
	{
		if (editor.ui.samplerVolBoxShown)
			renderSamplerVolBox();
		else if (editor.ui.samplerFiltersBoxShown)
			renderSamplerFiltersBox();
	}
}

void fillSampleRedoBuffer(int8_t sample)
{
	moduleSample_t *s;

	assert(sample >= 0 && sample <= 30);

	s = &modEntry->samples[sample];

	if (editor.smpRedoBuffer[sample] != NULL)
	{
		free(editor.smpRedoBuffer[sample]);
		editor.smpRedoBuffer[sample] = NULL;
	}

	editor.smpRedoFinetunes[sample] = s->fineTune;
	editor.smpRedoVolumes[sample] = s->volume;
	editor.smpRedoLengths[sample] = s->length;
	editor.smpRedoLoopStarts[sample] = s->loopStart;
	editor.smpRedoLoopLengths[sample] = s->loopLength;

	if (s->length > 0)
	{
		editor.smpRedoBuffer[sample] = (int8_t *)malloc(s->length);
		if (editor.smpRedoBuffer[sample] != NULL)
			memcpy(editor.smpRedoBuffer[sample], &modEntry->sampleData[s->offset], s->length);
	}
}

bool allocSamplerVars(void)
{
	editor.sampler.copyBuf = (int8_t *)malloc(MAX_SAMPLE_LEN);
	editor.sampler.blankSample = (int8_t *)calloc(MAX_SAMPLE_LEN, 1);

	if (editor.sampler.copyBuf == NULL || editor.sampler.blankSample == NULL)
		return false;

	return true;
}

void deAllocSamplerVars(void)
{
	if (editor.sampler.copyBuf != NULL)
	{
		free(editor.sampler.copyBuf);
		editor.sampler.copyBuf = NULL;
	}

	if (editor.sampler.blankSample != NULL)
	{
		free(editor.sampler.blankSample);
		editor.sampler.blankSample = NULL;
	}

	for (uint8_t i = 0; i < MOD_SAMPLES; i++)
	{
		if (editor.smpRedoBuffer[i] != NULL)
		{
			free(editor.smpRedoBuffer[i]);
			editor.smpRedoBuffer[i] = NULL;
		}
	}
}

void samplerRemoveDcOffset(void)
{
	int8_t *smpDat;
	int32_t smp32, i, from, to, offset;
	moduleSample_t *s;

	assert(editor.currSample >= 0 && editor.currSample <= 30);

	s = &modEntry->samples[editor.currSample];
	if (s->length == 0)
	{
		displayErrorMsg("SAMPLE IS EMPTY");
		return;
	}

	smpDat = &modEntry->sampleData[s->offset];

	from = 0;
	to = s->length;

	if (editor.markStartOfs != -1)
	{
		from = editor.markStartOfs;
		to = editor.markEndOfs;

		if (to > s->length)
			to = s->length;

		if (from == to)
		{
			from = 0;
			to = s->length;
		}
	}

	if (to <= 0)
		return;

	// calculate offset value
	offset = 0;
	for (i = from; i < to; i++)
		offset += smpDat[i];
	offset /= to;

	// remove DC offset
	for (i = from; i < to; i++)
	{
		smp32 = smpDat[i] - offset;
		CLAMP8(smp32);
		smpDat[i] = (int8_t)smp32;
	}

	fixSampleBeep(s);
	displaySample();
	updateWindowTitle(MOD_IS_MODIFIED);
}

#define INTRP_QUADRATIC_TAPS 3
#define INTRP8_QUADRATIC(s1, s2, s3, f) /* output: -32768..32767 (+ spline overshoot) */ \
{ \
	int32_t s4, frac = (f) >> 1; \
	\
	s2 <<= 8; \
	s4 = ((s1 + s3) << (8 - 1)) - s2; \
	s4 = ((s4 * frac) >> 16) + s2; \
	s3 = (s1 + s3) << (8 - 1); \
	s1 <<= 8; \
	s3 = (s1 + s3) >> 1; \
	s1 += ((s4 - s3) * frac) >> 14; \
} \

#define INTRP_LINEAR_TAPS 2
#define INTRP8_LINEAR(s1, s2, f) /* output: -127..128 */ \
	s2 -= s1; \
	s2 *= (int32_t)(f); \
	s1 <<= 8; \
	s2 >>= (16 - 8); \
	s1 += s2; \
	s1 >>= 8; \

void mixChordSample(void)
{
	bool smpLoopFlag;
	char smpText[22 + 1];
	int8_t *smpData, sameNotes, smpVolume;
	uint8_t smpFinetune, finetune;
	int32_t channels, samples[INTRP_QUADRATIC_TAPS], *mixData, i, j, k, pos, smpLoopStart, smpLoopLength, smpEnd;
	sampleMixer_t mixCh[4], *v;
	moduleSample_t *s;

	assert(editor.currSample >= 0 && editor.currSample <= 30);
	assert(editor.tuningNote <= 35);

	if (editor.note1 == 36)
	{
		displayErrorMsg("NO BASENOTE!");
		return;
	}

	if (modEntry->samples[editor.currSample].length == 0)
	{
		displayErrorMsg("SAMPLE IS EMPTY");
		return;
	}

	// check if all notes are the same (illegal)
	sameNotes = true;
	if ((editor.note2 != 36) && (editor.note2 != editor.note1)) sameNotes = false; else editor.note2 = 36;
	if ((editor.note3 != 36) && (editor.note3 != editor.note1)) sameNotes = false; else editor.note3 = 36;
	if ((editor.note4 != 36) && (editor.note4 != editor.note1)) sameNotes = false; else editor.note4 = 36;

	if (sameNotes)
	{
		displayErrorMsg("ONLY ONE NOTE!");
		return;
	}

	// sort the notes

	for (i = 0; i < 3; i++)
	{
		if (editor.note2 == 36)
		{
			editor.note2 = editor.note3;
			editor.note3 = editor.note4;
			editor.note4 = 36;
		}
	}

	for (i = 0; i < 3; i++)
	{
		if (editor.note3 == 36)
		{
			editor.note3 = editor.note4;
			editor.note4 = 36;
		}
	}

	// remove eventual note duplicates
	if (editor.note4 == editor.note3) editor.note4 = 36;
	if (editor.note4 == editor.note2) editor.note4 = 36;
	if (editor.note3 == editor.note2) editor.note3 = 36;

	editor.ui.updateNote1Text = true;
	editor.ui.updateNote2Text = true;
	editor.ui.updateNote3Text = true;
	editor.ui.updateNote4Text = true;

	// setup some variables

	smpLoopStart = modEntry->samples[editor.currSample].loopStart;
	smpLoopLength = modEntry->samples[editor.currSample].loopLength;
	smpLoopFlag = (smpLoopStart + smpLoopLength) > 2;
	smpEnd = smpLoopFlag ? (smpLoopStart + smpLoopLength) : modEntry->samples[editor.currSample].length;
	smpData = &modEntry->sampleData[modEntry->samples[editor.currSample].offset];

	if (editor.newOldFlag == 0)
	{
		// find a free sample slot for the new sample

		for (i = 0; i < MOD_SAMPLES; i++)
		{
			if (modEntry->samples[i].length == 0)
				break;
		}

		if (i == MOD_SAMPLES)
		{
			displayErrorMsg("NO EMPTY SAMPLE!");
			return;
		}

		smpFinetune = modEntry->samples[editor.currSample].fineTune;
		smpVolume = modEntry->samples[editor.currSample].volume;
		memcpy(smpText, modEntry->samples[editor.currSample].text, sizeof (smpText));

		s = &modEntry->samples[i];
		s->fineTune = smpFinetune;
		s->volume = smpVolume;

		memcpy(s->text, smpText, sizeof (smpText));
		editor.currSample = (int8_t)i;
	}
	else
	{
		// overwrite current sample
		s = &modEntry->samples[editor.currSample];
	}

	mixData = (int32_t *)calloc(MAX_SAMPLE_LEN, sizeof (int32_t));
	if (mixData == NULL)
	{
		statusOutOfMemory();
		return;
	}

	s->length = smpLoopFlag ? MAX_SAMPLE_LEN : editor.chordLength; // if sample loops, set max length
	s->loopLength = 2;
	s->loopStart = 0;
	s->text[21] = '!'; // chord sample indicator
	s->text[22] = '\0';

	memset(mixCh, 0, sizeof (mixCh));

	// setup mixing lengths and deltas

	finetune = s->fineTune & 0xF;
	channels = 0;

	if (editor.note1 < 36)
	{
		mixCh[0].delta = (periodTable[editor.tuningNote] << 16) / (periodTable[(finetune * 37) + editor.note1]);
		mixCh[0].length = (smpEnd * periodTable[(finetune * 37) + editor.note1]) / periodTable[editor.tuningNote];
		channels++;
	}

	if (editor.note2 < 36)
	{
		mixCh[1].delta = (periodTable[editor.tuningNote] << 16) / (periodTable[(finetune * 37) + editor.note2]);
		mixCh[1].length = (smpEnd * periodTable[(finetune * 37) + editor.note2]) / periodTable[editor.tuningNote];
		channels++;
	}

	if (editor.note3 < 36)
	{
		mixCh[2].delta = (periodTable[editor.tuningNote] << 16) / (periodTable[(finetune * 37) + editor.note3]);
		mixCh[2].length = (smpEnd * periodTable[(finetune * 37) + editor.note3]) / periodTable[editor.tuningNote];
		channels++;
	}

	if (editor.note4 < 36)
	{
		mixCh[3].delta = (periodTable[editor.tuningNote] << 16) / (periodTable[(finetune * 37) + editor.note4]);
		mixCh[3].length = (smpEnd * periodTable[(finetune * 37) + editor.note4]) / periodTable[editor.tuningNote];
		channels++;
	}

	// start mixing

	turnOffVoices();
	for (i = 0; i < channels; i++)
	{
		v = &mixCh[i];
		if (v->length <= 0)
			continue; // mix active channels only

		for (j = 0; j < MAX_SAMPLE_LEN; j++) // don't mix more than we can handle in a sample slot
		{
			// collect samples for interpolation
			for (k = 0; k < INTRP_QUADRATIC_TAPS; k++)
			{
				pos = v->pos + k;
				if (smpLoopFlag)
				{
					while (pos >= smpEnd)
						pos -= smpLoopLength;

					samples[k] = smpData[pos];
				}
				else
				{
					if (pos >= smpEnd)
						samples[k] = 0;
					else
						samples[k] = smpData[pos];
				}
			}

			INTRP8_QUADRATIC(samples[0], samples[1], samples[2], v->posFrac);
			mixData[j] += samples[0];

			v->posFrac += v->delta;
			if (v->posFrac > 0xFFFF)
			{
				v->pos += v->posFrac >> 16;
				v->posFrac &= 0xFFFF;

				if (smpLoopFlag)
				{
					while (v->pos >= smpEnd)
						v->pos -= smpLoopLength;
				}
			}
		}
	}

	normalize32bitSigned(mixData, s->length);

	// normalize gain and quantize to 8-bit
	for (i = 0; i < s->length; i++)
		modEntry->sampleData[s->offset + i] = (int8_t)(mixData[i] >> 24);

	if (s->length < MAX_SAMPLE_LEN)
		memset(&modEntry->sampleData[s->offset + s->length], 0, MAX_SAMPLE_LEN - s->length);

	// we're done

	free(mixData);

	editor.samplePos = 0;
	fixSampleBeep(s);
	updateCurrSample();

	updateWindowTitle(MOD_IS_MODIFIED);
}

void samplerResample(void)
{
	int8_t *readData, *writeData;
	int16_t refPeriod, newPeriod;
	int32_t samples[INTRP_LINEAR_TAPS], i, pos, readPos, writePos;
	int32_t readLength, writeLength, loopStart, loopLength;
	uint32_t posFrac, delta;
	moduleSample_t *s;

	assert(editor.currSample >= 0 && editor.currSample <= 30);
	assert(editor.tuningNote <= 35 && editor.resampleNote <= 35);

	s = &modEntry->samples[editor.currSample];
	if (s->length == 0)
	{
		displayErrorMsg("SAMPLE IS EMPTY");
		return;
	}

	// setup resampling variables
	readPos = 0;
	writePos = 0;
	writeData = &modEntry->sampleData[s->offset];
	refPeriod = periodTable[editor.tuningNote];
	newPeriod = periodTable[(37 * (s->fineTune & 0xF)) + editor.resampleNote];
	readLength = s->length;
	writeLength = (readLength * newPeriod) / refPeriod;

	if (readLength == writeLength)
		return; // no resampling needed

	// allocate memory for our sample duplicate
	readData = (int8_t *)malloc(s->length);
	if (readData == NULL)
	{
		statusOutOfMemory();
		return;
	}

	if (writeLength <= 0)
	{
		free(readData);
		displayErrorMsg("RESAMPLE ERROR !");
		return;
	}

	delta = ((uint32_t)readLength << 16) / (uint32_t)writeLength;
	assert(delta != 0);

	writeLength = writeLength & 0xFFFFFFFE;
	if (writeLength > MAX_SAMPLE_LEN)
		writeLength = MAX_SAMPLE_LEN;

	memcpy(readData, writeData, readLength);

	// resample

	posFrac = 0;

	turnOffVoices();
	while (writePos < writeLength)
	{
		// collect samples for interpolation
		for (i = 0; i < INTRP_LINEAR_TAPS; i++)
		{
			pos = readPos + i;
			if (pos >= readLength)
				samples[i] = 0;
			else
				samples[i] = readData[pos];
		}

		INTRP8_LINEAR(samples[0], samples[1], posFrac);
		writeData[writePos++] = (int8_t)samples[0];

		posFrac += delta;
		readPos += posFrac >> 16;
		posFrac &= 0xFFFF;
	}
	free(readData);

	// wipe non-used data in new sample
	if (writeLength < MAX_SAMPLE_LEN)
		memset(&writeData[writePos], 0, MAX_SAMPLE_LEN - writeLength);

	// update sample attributes
	s->length = writeLength;
	s->fineTune = 0;

	// scale loop points (and deactivate if overflowing)
	if ((s->loopStart + s->loopLength) > 2)
	{
		loopStart  = (int32_t)(((uint32_t)s->loopStart << 16) / delta) & 0xFFFFFFFE;
		loopLength = (int32_t)(((uint32_t)s->loopLength << 16) / delta) & 0xFFFFFFFE;

		if (loopStart+loopLength > s->length)
		{
			s->loopStart = 0;
			s->loopLength = 2;
		}
		else
		{
			s->loopStart = (uint16_t)loopStart;
			s->loopLength = (uint16_t)loopLength;
		}
	}

	fixSampleBeep(s);
	updateCurrSample();
	updateWindowTitle(MOD_IS_MODIFIED);
}

static uint8_t hexToInteger2(char *ptr)
{
	char lo, hi;

	/* This routine must ONLY be used on an address
	** where two bytes can be read. It will mess up
	** if the ASCII values are not '0 .. 'F' */

	hi = ptr[0];
	lo = ptr[1];

	// high nybble
	if (hi >= 'a')
		hi -= ' ';

	hi -= '0';
	if (hi > 9)
		hi -= 7;

	// low nybble
	if (lo >= 'a')
		lo -= ' ';

	lo -= '0';
	if (lo > 9)
		lo -= 7;

	return (hi << 4) | lo;
}

void doMix(void)
{
	int8_t *fromPtr1, *fromPtr2, *mixPtr;
	uint8_t smpFrom1, smpFrom2, smpTo;
	int16_t tmp16;
	int32_t i, mixLength;
	moduleSample_t *s1, *s2, *s3;

	smpFrom1 = hexToInteger2(&editor.mixText[4]);
	smpFrom2 = hexToInteger2(&editor.mixText[7]);
	smpTo = hexToInteger2(&editor.mixText[13]);

	if (smpFrom1 == 0 || smpFrom1 > 0x1F || smpFrom2 == 0 || smpFrom2 > 0x1F || smpTo == 0 || smpTo > 0x1F)
	{
		displayErrorMsg("NOT RANGE 01-1F !");
		return;
	}

	s1 = &modEntry->samples[--smpFrom1];
	s2 = &modEntry->samples[--smpFrom2];
	s3 = &modEntry->samples[--smpTo];

	if (s1->length == 0 || s2->length == 0)
	{
		displayErrorMsg("EMPTY SAMPLES !!!");
		return;
	}

	if (s1->length > s2->length)
	{
		fromPtr1 = &modEntry->sampleData[s1->offset];
		fromPtr2 = &modEntry->sampleData[s2->offset];
		mixLength = s1->length;
	}
	else
	{
		fromPtr1 = &modEntry->sampleData[s2->offset];
		fromPtr2 = &modEntry->sampleData[s1->offset];
		mixLength = s2->length;
	}

	mixPtr = (int8_t *)malloc(mixLength);
	if (mixPtr == NULL)
	{
		statusOutOfMemory();
		return;
	}

	turnOffVoices();
	if (mixLength <= MAX_SAMPLE_LEN)
	{
		for (i = 0; i < mixLength; i++)
		{
			tmp16 = (i < s2->length) ? (fromPtr1[i] + fromPtr2[i]) : fromPtr1[i];
			if (editor.halfClipFlag == 0)
				tmp16 >>= 1;

			CLAMP8(tmp16);
			mixPtr[i] = (int8_t)tmp16;
		}

		memcpy(&modEntry->sampleData[s3->offset], mixPtr, mixLength);
		if (mixLength < MAX_SAMPLE_LEN)
			memset(&modEntry->sampleData[s3->offset + mixLength], 0, MAX_SAMPLE_LEN - mixLength);
	}

	free(mixPtr);

	s3->length = mixLength;
	s3->volume = 64;
	s3->fineTune = 0;
	s3->loopStart = 0;
	s3->loopLength = 2;

	editor.currSample = smpTo;
	editor.samplePos = 0;

	fixSampleBeep(s3);
	updateCurrSample();
	updateWindowTitle(MOD_IS_MODIFIED);
}

// this is actually treble increase
void boostSample(int8_t sample, bool ignoreMark)
{
	int8_t *smpDat;
	int16_t tmp16_0, tmp16_1, tmp16_2;
	int32_t i, from, to;
	moduleSample_t *s;

	assert(sample >= 0 && sample <= 30);

	s = &modEntry->samples[sample];
	if (s->length == 0)
		return; // don't display warning/show warning pointer, it is done elsewhere

	smpDat = &modEntry->sampleData[s->offset];

	from = 0;
	to = s->length;

	if (!ignoreMark)
	{
		if (editor.markStartOfs != -1)
		{
			from = editor.markStartOfs;
			to = editor.markEndOfs;

			if (to > s->length)
				to = s->length;

			if (from == to)
			{
				from = 0;
				to = s->length;
			}
		}
	}

	tmp16_0 = 0;
	for (i = from; i < to; i++)
	{
		tmp16_1 = smpDat[i];
		tmp16_2 = tmp16_1;
		tmp16_1 -= tmp16_0;
		tmp16_0 = tmp16_2;
		tmp16_1 >>= 2;
		tmp16_2 += tmp16_1;

		CLAMP8(tmp16_2);
		smpDat[i] = (int8_t)tmp16_2;
	}

	fixSampleBeep(s);

	// don't redraw sample here, it is done elsewhere
}

// this is actually treble decrease
void filterSample(int8_t sample, bool ignoreMark)
{
	int8_t *smpDat;
	int16_t tmp16;
	int32_t i, from, to;
	moduleSample_t *s;

	assert(sample >= 0 && sample <= 30);

	s = &modEntry->samples[sample];
	if (s->length == 0)
		return; // don't display warning/show warning pointer, it is done elsewhere

	smpDat = &modEntry->sampleData[s->offset];

	from = 1;
	to = s->length;

	if (!ignoreMark)
	{
		if (editor.markStartOfs != -1)
		{
			from = editor.markStartOfs;
			to = editor.markEndOfs;

			if (to > s->length)
				to = s->length;

			if (from == to)
			{
				from = 0;
				to = s->length;
			}
		}
	}

	if (to < 1)
		return;
	to--;

	for (i = from; i < to; i++)
	{
		tmp16 = (smpDat[i+0] + smpDat[i+1]) >> 1;
		CLAMP8(tmp16);
		smpDat[i] = (int8_t)tmp16;
	}

	fixSampleBeep(s);
	// don't redraw sample here, it is done elsewhere
}

void toggleTuningTone(void)
{
	if (editor.currMode == MODE_PLAY || editor.currMode == MODE_RECORD)
		return;

	editor.tuningFlag ^= 1;
	if (editor.tuningFlag)
	{
		// turn tuning tone on

		editor.tuningChan = (editor.cursor.channel + 1) & 3;

		if (editor.tuningNote > 35)
			editor.tuningNote = 35;

		modEntry->channels[editor.tuningChan].n_volume = 64; // we need this for the scopes

		paulaSetPeriod(editor.tuningChan, periodTable[editor.tuningNote]);
		paulaSetVolume(editor.tuningChan, 64);
		paulaSetData(editor.tuningChan, tuneToneData);
		paulaSetLength(editor.tuningChan, sizeof (tuneToneData) / 2);
		paulaStartDMA(editor.tuningChan);

		// force loop flag on for scopes
		scopeExt[editor.tuningChan].newLoopFlag = scope[editor.tuningChan].loopFlag = true;
	}
	else
	{
		// turn tuning tone off
		mixerKillVoice(editor.tuningChan);
	}
}

void sampleMarkerToBeg(void)
{
	moduleSample_t *s;

	assert(editor.currSample >= 0 && editor.currSample <= 30);

	s = &modEntry->samples[editor.currSample];
	if (s->length == 0)
	{
		invertRange();
		editor.markStartOfs = -1;
		editor.samplePos = 0;
	}
	else
	{
		invertRange();
		if (input.keyb.shiftPressed && editor.markStartOfs != -1)
		{
			editor.markStartOfs = editor.sampler.samOffset;
		}
		else
		{
			editor.markStartOfs = editor.sampler.samOffset;
			editor.markEndOfs = editor.markStartOfs;
		}
		invertRange();

		editor.samplePos = (uint16_t)editor.markEndOfs;
	}

	updateSamplePos();
}

void sampleMarkerToCenter(void)
{
	int32_t middlePos;
	moduleSample_t *s;

	assert(editor.currSample >= 0 && editor.currSample <= 30);

	s = &modEntry->samples[editor.currSample];
	if (s->length == 0)
	{
		invertRange();
		editor.markStartOfs = -1;
		editor.samplePos = 0;
	}
	else
	{
		middlePos = editor.sampler.samOffset + ((editor.sampler.samDisplay + 1) / 2);

		invertRange();
		if (input.keyb.shiftPressed && editor.markStartOfs != -1)
		{
			if (editor.markStartOfs < middlePos)
				editor.markEndOfs = middlePos;
			else if (editor.markEndOfs > middlePos)
				editor.markStartOfs = middlePos;
		}
		else
		{
			editor.markStartOfs = middlePos;
			editor.markEndOfs = editor.markStartOfs;
		}
		invertRange();

		editor.samplePos = (uint16_t)editor.markEndOfs;
	}

	updateSamplePos();
}

void sampleMarkerToEnd(void)
{
	moduleSample_t *s;

	assert(editor.currSample >= 0 && editor.currSample <= 30);

	s = &modEntry->samples[editor.currSample];
	if (s->length == 0)
	{
		invertRange();
		editor.markStartOfs = -1;
		editor.samplePos = 0;
	}
	else
	{
		invertRange();
		if (input.keyb.shiftPressed && editor.markStartOfs != -1)
		{
			editor.markEndOfs = s->length;
		}
		else
		{
			editor.markStartOfs = s->length;
			editor.markEndOfs = editor.markStartOfs;
		}
		invertRange();

		editor.samplePos = (uint16_t)editor.markEndOfs;
	}

	updateSamplePos();
}

void samplerSamCopy(void)
{
	moduleSample_t *s;

	assert(editor.currSample >= 0 && editor.currSample <= 30);

	if (editor.markStartOfs == -1)
	{
		displayErrorMsg("NO RANGE SELECTED");
		return;
	}

	if (editor.markEndOfs-editor.markStartOfs <= 0)
	{
		displayErrorMsg("SET LARGER RANGE");
		return;
	}

	s = &modEntry->samples[editor.currSample];
	if (s->length == 0)
	{
		displayErrorMsg("SAMPLE IS EMPTY");
		return;
	}

	editor.sampler.copyBufSize = editor.markEndOfs - editor.markStartOfs;

	if ((int32_t)(editor.markStartOfs + editor.sampler.copyBufSize) > MAX_SAMPLE_LEN)
	{
		displayErrorMsg("COPY ERROR !");
		return;
	}

	memcpy(editor.sampler.copyBuf, &modEntry->sampleData[s->offset+editor.markStartOfs], editor.sampler.copyBufSize);
}

void samplerSamDelete(uint8_t cut)
{
	int8_t *tmpBuf;
	int32_t val32, sampleLength, copyLength, markEnd, markStart;
	moduleSample_t *s;

	assert(editor.currSample >= 0 && editor.currSample <= 30);

	if (editor.markStartOfs == -1)
	{
		displayErrorMsg("NO RANGE SELECTED");
		return;
	}

	if (editor.markEndOfs-editor.markStartOfs <= 0)
	{
		displayErrorMsg("SET LARGER RANGE");
		return;
	}

	if (cut)
		samplerSamCopy();

	s = &modEntry->samples[editor.currSample];

	sampleLength = s->length;
	if (sampleLength == 0)
	{
		displayErrorMsg("SAMPLE IS EMPTY");
		return;
	}

	turnOffVoices();

	// if whole sample is marked, wipe it
	if (editor.markEndOfs-editor.markStartOfs >= sampleLength)
	{
		memset(&modEntry->sampleData[s->offset], 0, MAX_SAMPLE_LEN);

		invertRange();
		editor.markStartOfs = -1;

		editor.sampler.samStart = editor.sampler.blankSample;
		editor.sampler.samDisplay = SAMPLE_AREA_WIDTH;
		editor.sampler.samLength = SAMPLE_AREA_WIDTH;

		s->length = 0;
		s->loopStart = 0;
		s->loopLength = 2;
		s->volume = 0;
		s->fineTune = 0;

		editor.samplePos = 0;
		updateCurrSample();

		updateWindowTitle(MOD_IS_MODIFIED);
		return;
	}

	markEnd = (editor.markEndOfs > sampleLength) ? sampleLength : editor.markEndOfs;
	markStart = editor.markStartOfs;

	copyLength = (editor.markStartOfs + sampleLength) - markEnd;
	if (copyLength < 2 || copyLength > MAX_SAMPLE_LEN)
	{
		displayErrorMsg("SAMPLE CUT FAIL !");
		return;
	}

	tmpBuf = (int8_t *)malloc(copyLength);
	if (tmpBuf == NULL)
	{
		statusOutOfMemory();
		return;
	}

	// copy start part
	memcpy(tmpBuf, &modEntry->sampleData[s->offset], editor.markStartOfs);

	// copy end part
	if (sampleLength-markEnd > 0)
		memcpy(&tmpBuf[editor.markStartOfs], &modEntry->sampleData[s->offset+markEnd], sampleLength - markEnd);

	// nuke sample data and copy over the result
	memcpy(&modEntry->sampleData[s->offset], tmpBuf, copyLength);

	if (copyLength < MAX_SAMPLE_LEN)
		memset(&modEntry->sampleData[s->offset+copyLength], 0, MAX_SAMPLE_LEN - copyLength);

	free(tmpBuf);

	editor.sampler.samLength = copyLength;
	if (editor.sampler.samOffset+editor.sampler.samDisplay >= editor.sampler.samLength)
	{
		if (editor.sampler.samDisplay < editor.sampler.samLength)
		{
			if (editor.sampler.samLength-editor.sampler.samDisplay < 0)
			{
				editor.sampler.samOffset = 0;
				editor.sampler.samDisplay = editor.sampler.samLength;
			}
			else
			{
				editor.sampler.samOffset = editor.sampler.samLength - editor.sampler.samDisplay;
			}
		}
		else
		{
			editor.sampler.samOffset = 0;
			editor.sampler.samDisplay = editor.sampler.samLength;
		}

		updateSamOffset();
	}

	if (s->loopLength > 2) // loop enabled?
	{
		if (markEnd > s->loopStart)
		{
			if (markStart < s->loopStart+s->loopLength)
			{
				// we cut data inside the loop, increase loop length
				val32 = (s->loopLength - (markEnd - markStart)) & 0xFFFFFFFE;
				if (val32 < 2)
					val32 = 2;

				s->loopLength = (uint16_t)val32;
			}

			// we cut data after the loop, don't modify loop points
		}
		else
		{
			// we cut data before the loop, adjust loop start point
			val32 = (s->loopStart - (markEnd - markStart)) & 0xFFFFFFFE;
			if (val32 < 0)
			{
				s->loopStart = 0;
				s->loopLength = 2;
			}
			else
			{
				s->loopStart = (uint16_t)val32;
			}
		}
	}

	s->length = copyLength & 0xFFFE;

	if (editor.sampler.samDisplay <= 2)
	{
		editor.sampler.samStart = editor.sampler.blankSample;
		editor.sampler.samLength = SAMPLE_AREA_WIDTH;
		editor.sampler.samDisplay = SAMPLE_AREA_WIDTH;
	}

	invertRange();
	if (editor.sampler.samDisplay == 0)
	{
		editor.markStartOfs = -1; // clear marking
	}
	else
	{
		if (editor.markStartOfs >= s->length)
			editor.markStartOfs = s->length - 1;

		editor.markEndOfs = editor.markStartOfs;
		invertRange();
	}

	editor.samplePos = editor.markStartOfs;
	fixSampleBeep(s);
	updateSamplePos();
	recalcChordLength();
	displaySample();

	editor.ui.updateCurrSampleLength = true;
	editor.ui.updateCurrSampleRepeat = true;
	editor.ui.updateCurrSampleReplen = true;
	editor.ui.updateSongSize = true;

	updateWindowTitle(MOD_IS_MODIFIED);
}

void samplerSamPaste(void)
{
	bool wasZooming;
	int8_t *tmpBuf;
	int32_t markStart;
	uint32_t readPos;
	moduleSample_t *s;

	assert(editor.currSample >= 0 && editor.currSample <= 30);

	if (editor.sampler.copyBuf == NULL || editor.sampler.copyBufSize == 0)
	{
		displayErrorMsg("BUFFER IS EMPTY");
		return;
	}

	s = &modEntry->samples[editor.currSample];
	if (s->length > 0 && editor.markStartOfs == -1)
	{
		displayErrorMsg("SET CURSOR POS");
		return;
	}

	markStart = editor.markStartOfs;
	if (s->length == 0)
		markStart = 0;

	if (s->length+editor.sampler.copyBufSize > MAX_SAMPLE_LEN)
	{
		displayErrorMsg("NOT ENOUGH ROOM");
		return;
	}

	tmpBuf = (int8_t *)malloc(MAX_SAMPLE_LEN);
	if (tmpBuf == NULL)
	{
		statusOutOfMemory();
		return;
	}

	readPos = 0;
	turnOffVoices();
	wasZooming = (editor.sampler.samDisplay != editor.sampler.samLength);

	// copy start part
	if (markStart > 0)
	{
		memcpy(&tmpBuf[readPos], &modEntry->sampleData[s->offset], markStart);
		readPos += markStart;
	}

	// copy buffer
	memcpy(&tmpBuf[readPos], editor.sampler.copyBuf, editor.sampler.copyBufSize);

	// copy end part
	if (markStart >= 0)
	{
		readPos += editor.sampler.copyBufSize;

		if (s->length-markStart > 0)
			memcpy(&tmpBuf[readPos], &modEntry->sampleData[s->offset+markStart], s->length - markStart);
	}

	s->length = (s->length + editor.sampler.copyBufSize) & 0xFFFFFFFE;
	if (s->length > MAX_SAMPLE_LEN)
		s->length = MAX_SAMPLE_LEN;

	editor.sampler.samLength = s->length;

	if (s->loopLength > 2) // loop enabled?
	{
		if (markStart > s->loopStart)
		{
			if (markStart < s->loopStart+s->loopLength)
			{
				// we pasted data inside the loop, increase loop length
				s->loopLength += editor.sampler.copyBufSize & 0xFFFFFFFE;
				if (s->loopStart+s->loopLength > s->length)
				{
					s->loopStart = 0;
					s->loopLength = 2;
				}
			}

			// we pasted data after the loop, don't modify loop points
		}
		else
		{
			// we pasted data before the loop, adjust loop start point
			s->loopStart = (s->loopStart + editor.sampler.copyBufSize) & 0xFFFFFFFE;
			if (s->loopStart+s->loopLength > s->length)
			{
				s->loopStart = 0;
				s->loopLength = 2;
			}
		}
	}

	memcpy(&modEntry->sampleData[s->offset], tmpBuf, s->length);
	if (s->length < MAX_SAMPLE_LEN)
		memset(&modEntry->sampleData[s->offset+s->length], 0, MAX_SAMPLE_LEN - s->length);

	free(tmpBuf);

	invertRange();
	editor.markStartOfs = -1;

	fixSampleBeep(s);
	updateSamplePos();
	recalcChordLength();

	if (wasZooming)
		displaySample();
	else
		redrawSample();

	editor.ui.updateCurrSampleLength = true;
	editor.ui.updateSongSize = true;

	updateWindowTitle(MOD_IS_MODIFIED);
}

static void playCurrSample(uint8_t chn, int32_t startOffset, int32_t endOffset, bool playWaveformFlag)
{
	moduleChannel_t *ch;
	moduleSample_t *s;

	assert(editor.currSample >= 0 && editor.currSample <= 30);
	assert(chn < AMIGA_VOICES);
	assert(editor.currPlayNote <= 35);

	s = &modEntry->samples[editor.currSample];
	ch = &modEntry->channels[chn];

	ch->n_samplenum = editor.currSample;
	ch->n_volume = s->volume;
	ch->n_period = periodTable[(37 * (s->fineTune & 0xF)) + editor.currPlayNote];
	
	if (playWaveformFlag)
	{
		ch->n_start = &modEntry->sampleData[s->offset];
		ch->n_length = (s->loopStart > 0) ? (uint32_t)(s->loopStart + s->loopLength) / 2 : s->length / 2;
		ch->n_loopstart = &modEntry->sampleData[s->offset + s->loopStart];
		ch->n_replen = s->loopLength / 2;
	}
	else
	{
		ch->n_start = &modEntry->sampleData[s->offset + startOffset];
		ch->n_length = (endOffset - startOffset) / 2;
		ch->n_loopstart = &modEntry->sampleData[s->offset];
		ch->n_replen = 1;
	}

	if (ch->n_length == 0)
		ch->n_length = 1;

	paulaSetVolume(chn, ch->n_volume);
	paulaSetPeriod(chn, ch->n_period);
	paulaSetData(chn, ch->n_start);
	paulaSetLength(chn, ch->n_length);

	if (!editor.muted[chn])
		paulaStartDMA(chn);
	else
		paulaStopDMA(chn);

	// these take effect after the current DMA cycle is done
	if (playWaveformFlag)
	{
		paulaSetData(chn, ch->n_loopstart);
		paulaSetLength(chn, ch->n_replen);
	}
	else
	{
		paulaSetData(chn, NULL);
		paulaSetLength(chn, 1);
	}

	updateSpectrumAnalyzer(ch->n_volume, ch->n_period);
}

void samplerPlayWaveform(void)
{
	playCurrSample(editor.cursor.channel, 0, 0, true);
}

void samplerPlayDisplay(void)
{
	int32_t start = editor.sampler.samOffset;
	int32_t end = editor.sampler.samOffset + editor.sampler.samDisplay;

	playCurrSample(editor.cursor.channel, start, end, false);
}

void samplerPlayRange(void)
{
	if (editor.markStartOfs == -1)
	{
		displayErrorMsg("NO RANGE SELECTED");
		return;
	}

	if (editor.markEndOfs-editor.markStartOfs < 2)
	{
		displayErrorMsg("SET LARGER RANGE");
		return;
	}

	playCurrSample(editor.cursor.channel, editor.markStartOfs, editor.markEndOfs, false);
}

void setLoopSprites(void)
{
	moduleSample_t *s;

	if (!editor.ui.samplerScreenShown)
	{
		hideSprite(SPRITE_LOOP_PIN_LEFT);
		hideSprite(SPRITE_LOOP_PIN_RIGHT);
		return;
	}

	assert(editor.currSample >= 0 && editor.currSample <= 30);

	s = &modEntry->samples[editor.currSample];
	if (s->loopStart+s->loopLength > 2)
	{
		if (editor.sampler.samDisplay > 0)
		{
			editor.sampler.loopStartPos = smpPos2Scr(s->loopStart);
			if (editor.sampler.loopStartPos >= 0 && editor.sampler.loopStartPos <= SAMPLE_AREA_WIDTH)
				setSpritePos(SPRITE_LOOP_PIN_LEFT, editor.sampler.loopStartPos, 138);
			else
				hideSprite(SPRITE_LOOP_PIN_LEFT);

			editor.sampler.loopEndPos = smpPos2Scr(s->loopStart + s->loopLength);
			if (editor.sampler.loopEndPos >= 0 && editor.sampler.loopEndPos <= SAMPLE_AREA_WIDTH)
				setSpritePos(SPRITE_LOOP_PIN_RIGHT, editor.sampler.loopEndPos + 3, 138);
			else
				hideSprite(SPRITE_LOOP_PIN_RIGHT);
		}
	}
	else
	{
		editor.sampler.loopStartPos = 0;
		editor.sampler.loopEndPos = 0;

		hideSprite(SPRITE_LOOP_PIN_LEFT);
		hideSprite(SPRITE_LOOP_PIN_RIGHT);
	}

	textOutBg(pixelBuffer, 288, 225, (s->loopStart+s->loopLength > 2) ? "ON " : "OFF", palette[PAL_GENTXT], palette[PAL_GENBKG]);
}

void samplerShowAll(void)
{
	if (editor.sampler.samDisplay == editor.sampler.samLength)
		return; // don't attempt to show all if already showing all! }

	editor.sampler.samOffset = 0;
	editor.sampler.samDisplay = editor.sampler.samLength;

	updateSamOffset();
	displaySample();
}

static void samplerZoomIn(int32_t step, int16_t x)
{
	int32_t tmpDisplay, tmpOffset;

	if (modEntry->samples[editor.currSample].length == 0 || editor.sampler.samDisplay <= 2)
		return;

	if (step < 1)
		step = 1;

	tmpDisplay = editor.sampler.samDisplay - (step * 2);
	if (tmpDisplay < 2)
		tmpDisplay = 2;

	const int32_t roundingBias = SCREEN_W / 4;

	step += (((x - (SCREEN_W / 2)) * step) + roundingBias) / (SCREEN_W / 2);

	tmpOffset = editor.sampler.samOffset + step;
	if (tmpOffset < 0)
		tmpOffset = 0;

	if (tmpOffset+tmpDisplay > editor.sampler.samLength)
		tmpOffset = editor.sampler.samLength-tmpDisplay;

	editor.sampler.samOffset = tmpOffset;
	editor.sampler.samDisplay = tmpDisplay;

	updateSamOffset();
	displaySample();
}

static void samplerZoomOut(int32_t step, int16_t x)
{
	int32_t tmpDisplay, tmpOffset;

	if (modEntry->samples[editor.currSample].length == 0 || editor.sampler.samDisplay == editor.sampler.samLength)
		return;

	if (step < 1)
		step = 1;

	tmpDisplay = editor.sampler.samDisplay + (step * 2);
	if (tmpDisplay > editor.sampler.samLength)
	{
		tmpOffset  = 0;
		tmpDisplay = editor.sampler.samLength;
	}
	else
	{
		const int32_t roundingBias = SCREEN_W / 4;

		step += (((x - (SCREEN_W / 2)) * step) + roundingBias) / (SCREEN_W / 2);

		tmpOffset = editor.sampler.samOffset - step;
		if (tmpOffset < 0)
			tmpOffset = 0;

		if (tmpOffset+tmpDisplay > editor.sampler.samLength)
			tmpOffset = editor.sampler.samLength-tmpDisplay;
	}

	editor.sampler.samOffset = tmpOffset;
	editor.sampler.samDisplay = tmpDisplay;

	updateSamOffset();
	displaySample();
}

void samplerZoomInMouseWheel(void)
{
	samplerZoomIn((editor.sampler.samDisplay + 5) / 10, input.mouse.x);
}

void samplerZoomOutMouseWheel(void)
{
	samplerZoomOut((editor.sampler.samDisplay + 5) / 10, input.mouse.x);
}

void samplerZoomOut2x(void)
{
	samplerZoomOut((editor.sampler.samDisplay + 1) / 2, SCREEN_W / 2);
}

void samplerRangeAll(void)
{
	moduleSample_t *s;

	assert(editor.currSample >= 0 && editor.currSample <= 30);

	s = &modEntry->samples[editor.currSample];
	if (s->length == 0)
	{
		invertRange();
		editor.markStartOfs = -1;
	}
	else
	{
		invertRange();
		editor.markStartOfs = editor.sampler.samOffset;
		editor.markEndOfs = editor.sampler.samOffset + editor.sampler.samDisplay;
		invertRange();
	}
}

void samplerShowRange(void)
{
	moduleSample_t *s;

	assert(editor.currSample >= 0 && editor.currSample <= 30);

	s = &modEntry->samples[editor.currSample];
	if (s->length == 0)
	{
		displayErrorMsg("SAMPLE IS EMPTY");
		return;
	}

	if (editor.markStartOfs == -1)
	{
		displayErrorMsg("NO RANGE SELECTED");
		return;
	}

	if (editor.markEndOfs-editor.markStartOfs < 2)
	{
		displayErrorMsg("SET LARGER RANGE");
		return;
	}

	editor.sampler.samDisplay = editor.markEndOfs - editor.markStartOfs;
	editor.sampler.samOffset = editor.markStartOfs;

	if (editor.sampler.samDisplay+editor.sampler.samOffset > editor.sampler.samLength)
		editor.sampler.samOffset = editor.sampler.samLength-editor.sampler.samDisplay;

	updateSamOffset();

	invertRange();
	editor.markStartOfs = -1;

	displaySample();
}

void volBoxBarPressed(bool mouseButtonHeld)
{
	int32_t mouseX;

	if (input.mouse.y < 0 || input.mouse.x < 0 || input.mouse.y >= SCREEN_H || input.mouse.x >= SCREEN_W)
		return;

	if (!mouseButtonHeld)
	{
		if (input.mouse.x >= 72 && input.mouse.x <= 173)
		{
			if (input.mouse.y >= 154 && input.mouse.y <= 174) editor.ui.forceVolDrag = 1;
			if (input.mouse.y >= 165 && input.mouse.y <= 175) editor.ui.forceVolDrag = 2;
		}
	}
	else
	{
		if (editor.sampler.lastMouseX != input.mouse.x)
		{
			editor.sampler.lastMouseX = input.mouse.x;
			mouseX = CLAMP(editor.sampler.lastMouseX - 107, 0, 60);

			if (editor.ui.forceVolDrag == 1)
			{
				editor.vol1 = (int16_t)(((mouseX * 200) + (60/2)) / 60); // rounded
				editor.ui.updateVolFromText = true;
				showVolFromSlider();
			}
			else if (editor.ui.forceVolDrag == 2)
			{
				editor.vol2 = (int16_t)(((mouseX * 200) + (60/2)) / 60); // rounded
				editor.ui.updateVolToText = true;
				showVolToSlider();
			}
		}
	}
}

void samplerBarPressed(bool mouseButtonHeld)
{
	int32_t tmp32;

	if (input.mouse.y < 0 || input.mouse.x < 0 || input.mouse.y >= SCREEN_H || input.mouse.x >= SCREEN_W)
		return;

	if (!mouseButtonHeld)
	{
		if (input.mouse.x >= 4 && input.mouse.x <= 315)
		{
			if (input.mouse.x < editor.sampler.dragStart)
			{
				tmp32 = editor.sampler.samOffset - editor.sampler.samDisplay;
				if (tmp32 < 0)
					tmp32 = 0;

				if (tmp32 == editor.sampler.samOffset)
					return;

				editor.sampler.samOffset = tmp32;

				updateSamOffset();
				displaySample();
				return;
			}

			if (input.mouse.x > editor.sampler.dragEnd)
			{
				tmp32 = editor.sampler.samOffset + editor.sampler.samDisplay;
				if (tmp32+editor.sampler.samDisplay <= editor.sampler.samLength)
				{
					if (tmp32 == editor.sampler.samOffset)
						return;

					editor.sampler.samOffset = tmp32;
				}
				else
				{
					tmp32 = editor.sampler.samLength - editor.sampler.samDisplay;
					if (tmp32 == editor.sampler.samOffset)
						return;

					editor.sampler.samOffset = tmp32;
				}

				updateSamOffset();
				displaySample();
				return;
			}

			editor.sampler.lastSamPos = (uint16_t)input.mouse.x;
			editor.sampler.saveMouseX = editor.sampler.lastSamPos - editor.sampler.dragStart;

			editor.ui.forceSampleDrag = true;
		}
	}

	if (input.mouse.x != editor.sampler.lastSamPos)
	{
		editor.sampler.lastSamPos = (uint16_t)input.mouse.x;

		tmp32 = editor.sampler.lastSamPos - editor.sampler.saveMouseX - 4;
		if (tmp32 < 0)
			tmp32 = 0;

		tmp32 = (int32_t)(((tmp32 * editor.sampler.samLength) + (311/2)) / 311); // rounded
		if (tmp32+editor.sampler.samDisplay <= editor.sampler.samLength)
		{
			if (tmp32 == editor.sampler.samOffset)
				return;

			editor.sampler.samOffset = tmp32;
		}
		else
		{
			tmp32 = editor.sampler.samLength - editor.sampler.samDisplay;
			if (tmp32 == editor.sampler.samOffset)
				return;

			editor.sampler.samOffset = tmp32;
		}

		updateSamOffset();
		displaySample();
	}
}

static int32_t x2LoopX(int32_t mouseX)
{
	moduleSample_t *s = &modEntry->samples[editor.currSample];

	mouseX -= 3;
	if (mouseX < 0)
		mouseX = 0;

	mouseX = scr2SmpPos(mouseX);
	mouseX = CLAMP(mouseX, 0, s->length);

	return mouseX;
}

static int32_t xToSmpX(int32_t x, int32_t smpLen)
{
	x = scr2SmpPos(x);
	x = CLAMP(x, 0, smpLen - 1);

	return x;
}

static int8_t yToSmpY(int32_t mouseY)
{
	mouseY = (SAMPLE_AREA_Y_CENTER - mouseY) * 4;
	CLAMP8(mouseY);

	return mouseY;
}

void samplerEditSample(bool mouseButtonHeld)
{
	int8_t y;
	int32_t mouseY, x, smp_x0, smp_x1, xDistance, smp_y0, smp_y1, yDistance, smp;
	moduleSample_t *s;

	assert(editor.currSample >= 0 && editor.currSample <= 30);

	if (input.mouse.y < 0 || input.mouse.x < 0 || input.mouse.y >= SCREEN_H || input.mouse.x >= SCREEN_W)
		return;

	s = &modEntry->samples[editor.currSample];

	if (!mouseButtonHeld)
	{
		if (input.mouse.x >= 3 && input.mouse.x <= 316 && input.mouse.y >= 138 && input.mouse.y <= 201)
		{
			if (s->length == 0)
			{
				displayErrorMsg("SAMPLE LENGTH = 0");
			}
			else
			{
				editor.sampler.lastMouseX = input.mouse.x;
				editor.sampler.lastMouseY = input.mouse.y;
				editor.ui.forceSampleEdit = true;
				updateWindowTitle(MOD_IS_MODIFIED);
			}
		}

		return;
	}

	mouseY = input.keyb.shiftPressed ? editor.sampler.lastMouseY : input.mouse.y;

	x = xToSmpX(input.mouse.x - 3, s->length);
	y = yToSmpY(mouseY);

	modEntry->sampleData[s->offset+x] = y;

	// interpolate x gaps
	if (input.mouse.x != editor.sampler.lastMouseX)
	{
		smp_y0 = yToSmpY(editor.sampler.lastMouseY);

		smp_y1 = y;
		yDistance = smp_y1 - smp_y0;

		if (input.mouse.x > editor.sampler.lastMouseX)
		{
			smp_x1 = x;
			smp_x0 = xToSmpX(editor.sampler.lastMouseX - 3, s->length);

			xDistance = smp_x1 - smp_x0;
			if (xDistance > 0)
			{
				for (x = smp_x0; x < smp_x1; x++)
				{
					assert(x < s->length);

					smp = smp_y0 + (((x - smp_x0) * yDistance) / xDistance);
					CLAMP8(smp);
					modEntry->sampleData[s->offset + x] = (int8_t)smp;
				}
			}
		}
		else if (input.mouse.x < editor.sampler.lastMouseX)
		{
			smp_x0 = x;
			smp_x1 = xToSmpX(editor.sampler.lastMouseX - 3, s->length);

			xDistance = smp_x1 - smp_x0;
			if (xDistance > 0)
			{
				for (x = smp_x0; x < smp_x1; x++)
				{
					assert(x < s->length);

					smp = smp_y0 + (((smp_x1 - x) * yDistance) / xDistance);
					CLAMP8(smp);
					modEntry->sampleData[s->offset + x] = (int8_t)smp;
				}
			}
		}

		editor.sampler.lastMouseX = input.mouse.x;

		if (!input.keyb.shiftPressed)
			editor.sampler.lastMouseY = input.mouse.y;
	}

	displaySample();
}

void samplerSamplePressed(bool mouseButtonHeld)
{
	int16_t mouseX;
	int32_t tmpPos;
	moduleSample_t *s;

	assert(editor.currSample >= 0 && editor.currSample <= 30);

	if (!mouseButtonHeld)
	{
		if (input.mouse.y < 142)
		{
			if (input.mouse.x >= editor.sampler.loopStartPos && input.mouse.x <= editor.sampler.loopStartPos+3)
			{
				editor.ui.leftLoopPinMoving = true;
				editor.ui.rightLoopPinMoving = false;
				editor.ui.sampleMarkingPos = 1;
				editor.sampler.lastMouseX = input.mouse.x;
				return;
			}
			else if (input.mouse.x >= editor.sampler.loopEndPos+3 && input.mouse.x <= editor.sampler.loopEndPos+6)
			{
				editor.ui.rightLoopPinMoving = true;
				editor.ui.leftLoopPinMoving = false;
				editor.ui.sampleMarkingPos = 1;
				editor.sampler.lastMouseX = input.mouse.x;
				return;
			}
		}
	}

	mouseX = (int16_t)input.mouse.x;
	s = &modEntry->samples[editor.currSample];

	if (editor.ui.leftLoopPinMoving)
	{
		if (editor.sampler.lastMouseX != mouseX)
		{
			editor.sampler.lastMouseX = mouseX;

			tmpPos = (x2LoopX(mouseX + 2) - s->loopStart) & 0xFFFFFFFE;
			if (tmpPos > MAX_SAMPLE_LEN)
				tmpPos = MAX_SAMPLE_LEN;

			if (s->loopStart+tmpPos >= (s->loopStart+s->loopLength)-2)
			{
				s->loopStart  = (s->loopStart + s->loopLength) - 2;
				s->loopLength = 2;
			}
			else
			{
				s->loopStart = s->loopStart + tmpPos;

				if (s->loopLength-tmpPos > 2)
					s->loopLength -= tmpPos;
				else
					s->loopLength = 2;
			}

			editor.ui.updateCurrSampleRepeat = true;
			editor.ui.updateCurrSampleReplen = true;

			setLoopSprites();
			mixerUpdateLoops();
			updateWindowTitle(MOD_IS_MODIFIED);
		}

		return;
	}

	if (editor.ui.rightLoopPinMoving)
	{
		if (editor.sampler.lastMouseX != mouseX)
		{
			editor.sampler.lastMouseX = mouseX;

			s = &modEntry->samples[editor.currSample];

			tmpPos = (x2LoopX(mouseX - 1) - s->loopStart) & 0xFFFFFFFE;
			tmpPos = CLAMP(tmpPos, 2, MAX_SAMPLE_LEN);

			s->loopLength = tmpPos;

			editor.ui.updateCurrSampleRepeat = true;
			editor.ui.updateCurrSampleReplen = true;

			setLoopSprites();
			mixerUpdateLoops();
			updateWindowTitle(MOD_IS_MODIFIED);
		}

		return;
	}

	if (!mouseButtonHeld)
	{
		if (mouseX < 3 || mouseX > 319)
			return;

		editor.ui.sampleMarkingPos = (int16_t)mouseX;
		editor.sampler.lastSamPos = editor.ui.sampleMarkingPos;

		invertRange();
		if (s->length == 0)
		{
			editor.markStartOfs = -1; // clear marking
		}
		else
		{
			editor.markStartOfs = scr2SmpPos(editor.ui.sampleMarkingPos - 3);
			editor.markEndOfs = scr2SmpPos(editor.ui.sampleMarkingPos - 3);

			if (editor.markEndOfs > s->length)
				editor.markEndOfs = s->length;

			invertRange();
		}

		if (s->length == 0)
		{
			editor.samplePos = 0;
		}
		else
		{
			tmpPos = scr2SmpPos(mouseX - 3);
			if (tmpPos > s->length)
				tmpPos = s->length;

			editor.samplePos = (uint16_t)tmpPos;
		}

		updateSamplePos();

		return;
	}

	mouseX = CLAMP(mouseX, 3, 319);

	if (mouseX != editor.sampler.lastSamPos)
	{
		editor.sampler.lastSamPos = (uint16_t)mouseX;

		invertRange();
		if (s->length == 0)
		{
			editor.markStartOfs = -1; // clear marking
		}
		else
		{
			if (editor.sampler.lastSamPos > editor.ui.sampleMarkingPos)
			{
				editor.markStartOfs = scr2SmpPos(editor.ui.sampleMarkingPos - 3);
				editor.markEndOfs = scr2SmpPos(editor.sampler.lastSamPos - 3);
			}
			else
			{
				editor.markStartOfs = scr2SmpPos(editor.sampler.lastSamPos - 3);
				editor.markEndOfs = scr2SmpPos(editor.ui.sampleMarkingPos - 3);
			}

			if (editor.markEndOfs > s->length)
				editor.markEndOfs = s->length;

			invertRange();
		}
	}

	if (s->length == 0)
	{
		editor.samplePos = 0;
	}
	else
	{
		tmpPos = scr2SmpPos(mouseX - 3);
		if (tmpPos > s->length)
			tmpPos = s->length;

		 editor.samplePos = (uint16_t)tmpPos;
	}

	updateSamplePos();
}

void samplerLoopToggle(void)
{
	moduleSample_t *s;

	assert(editor.currSample >= 0 && editor.currSample <= 30);

	s = &modEntry->samples[editor.currSample];
	if (s->length < 2)
		return;

	turnOffVoices();

	if (s->loopStart+s->loopLength > 2)
	{
		// disable loop

		editor.sampler.tmpLoopStart = s->loopStart;
		editor.sampler.tmpLoopLength = s->loopLength;

		s->loopStart = 0;
		s->loopLength = 2;
	}
	else
	{
		// enable loop

		if (editor.sampler.tmpLoopStart == 0 && editor.sampler.tmpLoopLength == 0)
		{
			s->loopStart = 0;
			s->loopLength = s->length;
		}
		else
		{
			s->loopStart = editor.sampler.tmpLoopStart;
			s->loopLength = editor.sampler.tmpLoopLength;

			if (s->loopStart+s->loopLength > s->length)
			{
				s->loopStart = 0;
				s->loopLength = s->length;
			}
		}
	}

	editor.ui.updateCurrSampleRepeat = true;
	editor.ui.updateCurrSampleReplen = true;

	displaySample();
	mixerUpdateLoops();
	recalcChordLength();
	updateWindowTitle(MOD_IS_MODIFIED);
}

void exitFromSam(void)
{
	editor.ui.samplerScreenShown = false;
	memcpy(&pixelBuffer[121 * SCREEN_W], &trackerFrameBMP[121 * SCREEN_W], 320 * 134 * sizeof (int32_t));

	updateCursorPos();
	setLoopSprites();

	editor.ui.updateStatusText = true;
	editor.ui.updateSongSize = true;
	editor.ui.updateSongTiming = true;
	editor.ui.updateSongBPM = true;
	editor.ui.updateCurrPattText = true;
	editor.ui.updatePatternData = true;

	editor.markStartOfs = -1;
}

void samplerScreen(void)
{
	if (editor.ui.samplerScreenShown)
	{
		exitFromSam();
		return;
	}

	editor.ui.samplerScreenShown = true;
	memcpy(&pixelBuffer[(121 * SCREEN_W)], samplerScreenBMP, 320 * 134 * sizeof (int32_t));
	hideSprite(SPRITE_PATTERN_CURSOR);

	editor.ui.updateStatusText = true;
	editor.ui.updateSongSize = true;
	editor.ui.updateSongTiming = true;
	editor.ui.updateResampleNote = true;
	editor.ui.update9xxPos = true;

	redrawSample();
}

void drawSamplerLine(void)
{
	uint8_t i;
	int32_t pos;

	hideSprite(SPRITE_SAMPLING_POS_LINE);
	if (!editor.ui.samplerScreenShown || editor.ui.samplerVolBoxShown || editor.ui.samplerFiltersBoxShown)
		return;

	for (i = 0; i < AMIGA_VOICES; i++)
	{
		if (modEntry->channels[i].n_samplenum == editor.currSample && !editor.muted[i])
		{
			pos = getSampleReadPos(i, editor.currSample);
			if (pos >= 0)
			{
				pos = 3 + smpPos2Scr(pos);
				if (pos >= 3 && pos <= 316)
					setSpritePos(SPRITE_SAMPLING_POS_LINE, pos, 138);
			}
		}
	}
}
