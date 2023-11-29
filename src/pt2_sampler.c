// for finding memory leaks in debug mode with Visual Studio 
#if defined _DEBUG && defined _MSC_VER
#include <crtdbg.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include "pt2_helpers.h"
#include "pt2_textout.h"
#include "pt2_audio.h"
#include "pt2_tables.h"
#include "pt2_visuals.h"
#include "pt2_scopes.h"
#include "pt2_sampler.h"
#include "pt2_config.h"
#include "pt2_bmp.h"
#include "pt2_rcfilters.h"
#include "pt2_chordmaker.h"
#include "pt2_replayer.h"
#include "pt2_visuals_sync.h"

#define CENTER_LINE_COLOR 0x303030
#define MARK_COLOR_1 0x666666 /* inverted background */
#define MARK_COLOR_2 0xCCCCCC /* inverted waveform */
#define MARK_COLOR_3 0x7D7D7D /* inverted center line */

#define SAMPLE_AREA_Y_CENTER 169
#define SAMPLE_AREA_HEIGHT 64

static int32_t samOffsetScaled, lastDrawX, lastDrawY;
static uint16_t TToneBit;
static uint32_t waveInvertTable[8];

sampler_t sampler; // globalized

static const int8_t tuneToneData[32] = // Tuning Tone (sine, regenerated with 127-scale instead of 128)
{
	   0,   25,   49,   71,   90,  106,  117,  125,
	 127,  125,  117,  106,   90,   71,   49,   25,
	   0,  -25,  -49,  -71,  -90, -106, -117, -125,
	-127, -125, -117, -106,  -90,  -71,  -49,  -25
};

void killSample(void)
{
	if (editor.sampleZero)
	{
		statusNotSampleZero();
		return;
	}

	turnOffVoices();
	moduleSample_t *s = &song->samples[editor.currSample];

	s->fineTune = 0;
	s->volume = 0;
	s->length = 0;
	s->loopStart = 0;
	s->loopLength = 2;

	memset(s->text, 0, sizeof (s->text));
	memset(&song->sampleData[(editor.currSample * config.maxSampleLength)], 0, config.maxSampleLength);

	editor.samplePos = 0;
	updateCurrSample();

	ui.updateSongSize = true;
	updateWindowTitle(MOD_IS_MODIFIED);
}

void upSample(void)
{
	if (editor.sampleZero)
	{
		statusNotSampleZero();
		return;
	}

	moduleSample_t *s = &song->samples[editor.currSample];

	int32_t newLength = (s->length >> 1) & config.maxSampleLength;
	if (newLength < 2)
		return;

	turnOffVoices();

	// upsample
	int8_t *ptr8 = &song->sampleData[s->offset];
	for (int32_t i = 0; i < newLength; i++)
		ptr8[i] = ptr8[i << 1];

	// clear junk after shrunk sample
	if (newLength < config.maxSampleLength)
		memset(&ptr8[newLength], 0, config.maxSampleLength - newLength);

	s->length = newLength;
	s->loopStart = (s->loopStart >> 1) & ~1;
	s->loopLength = (s->loopLength >> 1) & ~1;

	if (s->loopLength < 2)
	{
		s->loopStart = 0;
		s->loopLength = 2;
	}

	fixSampleBeep(s);
	updateCurrSample();

	ui.updateSongSize = true;
	updateWindowTitle(MOD_IS_MODIFIED);
}

void downSample(void)
{
	if (editor.sampleZero)
	{
		statusNotSampleZero();
		return;
	}

	moduleSample_t *s = &song->samples[editor.currSample];

	int32_t newLength = s->length << 1;
	if (newLength > config.maxSampleLength)
		newLength = config.maxSampleLength;

	turnOffVoices();

	// downsample

	int8_t *ptr8 = &song->sampleData[s->offset];
	int8_t *ptr8_2 = ptr8 - 1;
	for (int32_t i = s->length-1; i > 0; i--)
	{
		ptr8[i<<1] = ptr8[i];
		ptr8_2[i<<1] = ptr8_2[i];
	}

	s->length = newLength;

	if (s->loopLength > 2)
	{
		int32_t loopStart = s->loopStart << 1;
		int32_t loopLength = s->loopLength << 1;

		if (loopStart+loopLength > s->length)
		{
			loopStart = 0;
			loopLength = 2;
		}

		s->loopStart = loopStart;
		s->loopLength = loopLength;
	}

	fixSampleBeep(s);
	updateCurrSample();

	ui.updateSongSize = true;
	updateWindowTitle(MOD_IS_MODIFIED);
}

void createSampleMarkTable(void)
{
	// used for invertRange() (sample data marking)

	waveInvertTable[0] = 0x00000000 | video.palette[PAL_BACKGRD];
	waveInvertTable[1] = 0x01000000 | video.palette[PAL_QADSCP];
	waveInvertTable[2] = 0x02000000 | CENTER_LINE_COLOR;
	waveInvertTable[3] = 0x03000000; // spacer, not used
	waveInvertTable[4] = 0x04000000 | MARK_COLOR_1;
	waveInvertTable[5] = 0x05000000 | MARK_COLOR_2;
	waveInvertTable[6] = 0x06000000 | MARK_COLOR_3;
	waveInvertTable[7] = 0x07000000; // spacer, not used
}

static void updateSamOffset(void)
{
	if (sampler.samDisplay == 0)
		samOffsetScaled = 0;
	else
		samOffsetScaled = (sampler.samOffset * SAMPLE_AREA_WIDTH) / sampler.samDisplay; // truncate here
}

void fixSampleBeep(moduleSample_t *s)
{
	if (s->length >= 2 && s->loopStart+s->loopLength <= 2)
	{
		song->sampleData[s->offset+0] = 0;
		song->sampleData[s->offset+1] = 0;
	}
}

void updateSamplePos(void)
{
	moduleSample_t *s;

	assert(editor.currSample >= 0 && editor.currSample <= 30);
	if (editor.currSample >= 0 && editor.currSample <= 30)
	{
		s = &song->samples[editor.currSample];
		if (editor.samplePos > s->length)
			editor.samplePos = s->length;

		if (ui.editOpScreenShown && ui.editOpScreen == 2)
			ui.updatePosText = true;
	}
}

void fillSampleFilterUndoBuffer(void)
{
	moduleSample_t *s;

	assert(editor.currSample >= 0 && editor.currSample <= 30);
	if (editor.currSample >= 0 && editor.currSample <= 30)
	{
		s = &song->samples[editor.currSample];
		memcpy(sampler.sampleUndoCopy, &song->sampleData[s->offset], s->length);
	}
}

void sampleLine(int32_t line_x1, int32_t line_x2, int32_t line_y1, int32_t line_y2)
{
	const uint32_t color = 0x01000000 | video.palette[PAL_QADSCP]; // set alpha to 0x10 ( used for invertRange() as a hack )

	assert(line_x1 >= 0 || line_x2 >= 0 || line_x1 < SCREEN_W || line_x2 < SCREEN_W);
	assert(line_y1 >= 0 || line_y2 >= 0 || line_y1 < SCREEN_H || line_y2 < SCREEN_H);

	int32_t dx = line_x2 - line_x1;
	int32_t ax = ABS(dx) * 2;
	int32_t sx = SGN(dx);
	int32_t dy = line_y2 - line_y1;
	int32_t ay = ABS(dy) * 2;
	int32_t sy = SGN(dy);
	int32_t x  = line_x1;
	int32_t y  = line_y1;

	if (ax > ay)
	{
		int32_t d = ay - ((uint16_t)ax >> 1);
		while (true)
		{
			assert(y >= 0 || x >= 0 || y < SCREEN_H || x < SCREEN_W);

			video.frameBuffer[(y * SCREEN_W) + x] = color;

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
		int32_t d = ax - ((uint16_t)ay >> 1);
		while (true)
		{
			assert(y >= 0 || x >= 0 || y < SCREEN_H || x < SCREEN_W);

			video.frameBuffer[(y * SCREEN_W) + x] = color;

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
	// clear drag bar background
	fillRect(4, 206, 312, 4, video.palette[PAL_BACKGRD]);

	if (sampler.samLength > 0 && sampler.samDisplay != sampler.samLength)
	{
		const int32_t roundingBias = sampler.samLength >> 1;

		// update drag bar coordinates
		int32_t pos = 4 + (((sampler.samOffset * 311) + roundingBias) / sampler.samLength);
		sampler.dragStart = (uint16_t)CLAMP(pos, 4, 315);

		pos = 5 + ((((sampler.samDisplay + sampler.samOffset) * 311) + roundingBias) / sampler.samLength);
		sampler.dragEnd = (uint16_t)CLAMP(pos, 5, 316);

		if (sampler.dragStart > sampler.dragEnd-1)
			sampler.dragStart = sampler.dragEnd-1;

		// draw drag bar

		const uint32_t dragWidth = sampler.dragEnd - sampler.dragStart;
		if (dragWidth > 0)
			fillRect(sampler.dragStart, 206, dragWidth, 4, video.palette[PAL_QADSCP]);
	}
}

static int8_t getScaledSample(int32_t index)
{
	if (sampler.samLength <= 0 || index < 0 || index > sampler.samLength)
		return 0;

	const int8_t *ptr8 = sampler.samStart;
	if (ptr8 == NULL)
		return 0;

	return ptr8[index] >> 2;
}

int32_t smpPos2Scr(int32_t pos) // sample pos -> screen x pos
{
	if (sampler.samDisplay == 0)
		return 0;

	const uint32_t roundingBias = (const uint32_t)sampler.samDisplay >> 1;

	pos = (((uint32_t)pos * SAMPLE_AREA_WIDTH) + roundingBias) / (uint32_t)sampler.samDisplay; // rounded
	pos -= samOffsetScaled;

	return pos;
}

int32_t scr2SmpPos(int32_t x) // screen x pos -> sample pos
{
	if (sampler.samDisplay == 0)
		return 0;

	if (x < 0)
		x = 0;

	x += samOffsetScaled;
	x = (uint32_t)(x * sampler.samDisplay) / SAMPLE_AREA_WIDTH; // truncate here

	if (x > sampler.samLength)
		x = sampler.samLength;

	return x;
}

static void getSampleDataPeak(int8_t *smpPtr, int32_t numBytes, int16_t *outMin, int16_t *outMax)
{
	int8_t smpMin = 127;
	int8_t smpMax = -128;

	for (int32_t i = 0; i < numBytes; i++)
	{
		int8_t smp = smpPtr[i];
		if (smp < smpMin) smpMin = smp;
		if (smp > smpMax) smpMax = smp;
	}

	*outMin = SAMPLE_AREA_Y_CENTER - (smpMin >> 2);
	*outMax = SAMPLE_AREA_Y_CENTER - (smpMax >> 2);
}

void renderSampleData(void)
{
	moduleSample_t *s = &song->samples[editor.currSample];

	// clear sample data background
	fillRect(3, 138, SAMPLE_AREA_WIDTH, SAMPLE_VIEW_HEIGHT, video.palette[PAL_BACKGRD]);

	// display center line (if enabled)
	if (config.waveformCenterLine)
	{
		uint32_t *dstPtr = &video.frameBuffer[(SAMPLE_AREA_Y_CENTER * SCREEN_W) + 3];
		for (int32_t x = 0; x < SAMPLE_AREA_WIDTH; x++)
			dstPtr[x] = 0x02000000 | CENTER_LINE_COLOR;
	}

	// render sample data
	if (sampler.samDisplay >= 0 && sampler.samDisplay <= config.maxSampleLength)
	{
		int16_t y1 = SAMPLE_AREA_Y_CENTER - getScaledSample(scr2SmpPos(0));

		if (sampler.samDisplay <= SAMPLE_AREA_WIDTH)
		{
			// 1:1 or zoomed in
			for (int32_t x = 1; x < SAMPLE_AREA_WIDTH; x++)
			{
				int16_t y2 = SAMPLE_AREA_Y_CENTER - getScaledSample(scr2SmpPos(x));
				sampleLine(x + 2, x + 3, y1, y2);
				y1 = y2;
			}
		}
		else
		{
			// zoomed out

			int16_t min, max;

			int16_t oldMin = y1;
			int16_t oldMax = y1;

			int8_t *smpPtr = &song->sampleData[s->offset];
			for (int32_t x = 0; x < SAMPLE_AREA_WIDTH; x++)
			{
				int32_t smpIdx = scr2SmpPos(x);
				int32_t smpNum = scr2SmpPos(x+1) - smpIdx;

				// prevent look-up overflow (yes, this can happen near the end of the sample)
				if (smpIdx+smpNum > sampler.samLength)
					smpNum = sampler.samLength - smpNum;

				if (smpNum < 1)
					smpNum = 1;

				getSampleDataPeak(&smpPtr[smpIdx], smpNum, &min, &max);

				if (x > 0)
				{
					if (min > oldMax) sampleLine(x + 2, x + 3, oldMax, min);
					if (max < oldMin) sampleLine(x + 2, x + 3, oldMin, max);
				}

				sampleLine(x + 3, x + 3, max, min);

				oldMin = min;
				oldMax = max;
			}
		}
	}

	if (ui.samplingBoxShown)
		return;

	// render "sample display" text

	if (config.maxSampleLength == 0xFFFE)
	{
		if (sampler.samStart == sampler.blankSample)
			printFiveDecimalsBg(272, 214, 0, video.palette[PAL_GENTXT], video.palette[PAL_GENBKG]);
		else
			printFiveDecimalsBg(272, 214, sampler.samDisplay, video.palette[PAL_GENTXT], video.palette[PAL_GENBKG]);
	}
	else
	{
		if (sampler.samStart == sampler.blankSample)
			printSixDecimalsBg(270, 214, 0, video.palette[PAL_GENTXT], video.palette[PAL_GENBKG]);
		else
			printSixDecimalsBg(270, 214, sampler.samDisplay, video.palette[PAL_GENTXT], video.palette[PAL_GENBKG]);
	}

	setDragBar();
	setLoopSprites();
}

void invertRange(void)
{
	if (editor.markStartOfs == -1)
		return; // no marking

	int32_t start = smpPos2Scr(editor.markStartOfs);
	int32_t end = smpPos2Scr(editor.markEndOfs);

	if (sampler.samDisplay < sampler.samLength && (start >= SAMPLE_AREA_WIDTH || end < 0))
		return; // range is outside of view

	start = CLAMP(start, 0, SAMPLE_AREA_WIDTH-1);
	end = CLAMP(end, 0, SAMPLE_AREA_WIDTH-1);

	int32_t rangeLen = (end + 1) - start;
	if (rangeLen < 1)
		rangeLen = 1;

	uint32_t *dstPtr = &video.frameBuffer[(138 * SCREEN_W) + (start + 3)];
	for (int32_t y = 0; y < 64; y++)
	{
		for (int32_t x = 0; x < rangeLen; x++)
			dstPtr[x] = waveInvertTable[((dstPtr[x] >> 24) & 7) ^ 4]; // ptr[x]>>24 = wave/invert color number

		dstPtr += SCREEN_W;
	}
}

void displaySample(void)
{
	if (!ui.samplerScreenShown)
		return;

	renderSampleData();
	if (editor.markStartOfs != -1)
		invertRange();

	ui.update9xxPos = true;
}

void redrawSample(void)
{
	if (!ui.samplerScreenShown)
		return;

	assert(editor.currSample >= 0 && editor.currSample <= 30);
	if (editor.currSample >= 0 && editor.currSample <= 30)
	{
		editor.markStartOfs = -1;

		sampler.samOffset = 0;
		updateSamOffset();

		moduleSample_t *s = &song->samples[editor.currSample];
		if (s->length > 0)
		{
			sampler.samStart = &song->sampleData[s->offset];
			sampler.samDisplay = s->length;
			sampler.samLength = s->length;
		}
		else
		{
			// "blank sample" template
			sampler.samStart = sampler.blankSample;
			sampler.samLength = SAMPLE_AREA_WIDTH;
			sampler.samDisplay = SAMPLE_AREA_WIDTH;
		}

		renderSampleData();
		updateSamplePos();

		ui.update9xxPos = true;
		ui.lastSampleOffset = 0x900;
	}
}

void highPassSample(int32_t cutOff)
{
	onePoleFilter_t filterHi;

	assert(editor.currSample >= 0 && editor.currSample <= 30);

	if (editor.sampleZero)
	{
		statusNotSampleZero();
		return;
	}

	if (cutOff == 0)
	{
		displayErrorMsg("CUTOFF CAN'T BE 0");
		return;
	}

	moduleSample_t *s = &song->samples[editor.currSample];
	if (s->length == 0)
	{
		statusSampleIsEmpty();
		return;
	}

	int32_t from = 0;
	int32_t to = s->length;

	if (editor.markStartOfs != -1)
	{
		from = editor.markStartOfs;
		to = editor.markEndOfs;

		if (to > s->length)
			to = s->length;

		if (from == to || from >= s->length || to < from)
		{
			from = 0;
			to = s->length;
		}
	}

	double *dSampleData = (double *)malloc(s->length * sizeof (double));
	if (dSampleData == NULL)
	{
		statusOutOfMemory();
		return;
	}

	fillSampleFilterUndoBuffer();

	// setup filter coefficients

	double dBaseFreq = FILTERS_BASE_FREQ;

	double dCutOff = (double)cutOff;
	if (dCutOff >= dBaseFreq/2.0)
	{
		dCutOff = dBaseFreq/2.0;
		editor.hpCutOff = (uint16_t)dCutOff;
	}

	setupOnePoleFilter(dBaseFreq, dCutOff, &filterHi);

	clearOnePoleFilterState(&filterHi);
	if (to <= s->length)
	{
		const int8_t *smpPtr = &song->sampleData[s->offset];
		for (int32_t i = from; i < to; i++)
		{
			double dSmp = smpPtr[i];
			onePoleHPFilter(&filterHi, dSmp, &dSampleData[i]);
		}
	}

	double dAmp = 1.0;
	if (editor.normalizeFiltersFlag)
	{
		const double dPeak = getDoublePeak(dSampleData, s->length);
		if (dPeak > 0.0)
			dAmp = INT8_MAX / dPeak;
	}

	int8_t *smpPtr = &song->sampleData[s->offset];
	for (int32_t i = from; i < to; i++)
	{
		int16_t smp16 = (int16_t)round(dSampleData[i] * dAmp);
		CLAMP8(smp16);
		smpPtr[i] = (int8_t)smp16;
	}

	free(dSampleData);

	fixSampleBeep(s);
	displaySample();
	updateWindowTitle(MOD_IS_MODIFIED);
}

void lowPassSample(int32_t cutOff)
{
	onePoleFilter_t filterLo;

	if (editor.sampleZero)
	{
		statusNotSampleZero();
		return;
	}

	assert(editor.currSample >= 0 && editor.currSample <= 30);

	if (cutOff == 0)
	{
		displayErrorMsg("CUTOFF CAN'T BE 0");
		return;
	}

	moduleSample_t *s = &song->samples[editor.currSample];
	if (s->length == 0)
	{
		statusSampleIsEmpty();
		return;
	}

	int32_t from = 0;
	int32_t to = s->length;

	if (editor.markStartOfs != -1)
	{
		from = editor.markStartOfs;
		to = editor.markEndOfs;

		if (to > s->length)
			to = s->length;

		if (from == to || from >= s->length || to < from)
		{
			from = 0;
			to = s->length;
		}
	}

	double *dSampleData = (double *)malloc(s->length * sizeof (double));
	if (dSampleData == NULL)
	{
		statusOutOfMemory();
		return;
	}

	fillSampleFilterUndoBuffer();

	// setup filter coefficients

	double dBaseFreq = FILTERS_BASE_FREQ;

	double dCutOff = (double)cutOff;
	if (dCutOff >= dBaseFreq/2.0)
	{
		dCutOff = dBaseFreq/2.0;
		editor.lpCutOff = (uint16_t)dCutOff;
	}

	setupOnePoleFilter(dBaseFreq, dCutOff, &filterLo);

	// copy over sample data to double buffer
	for (int32_t i = 0; i < s->length; i++)
		dSampleData[i] = song->sampleData[s->offset+i];

	clearOnePoleFilterState(&filterLo);
	if (to <= s->length)
	{
		const int8_t *smpPtr = &song->sampleData[s->offset];
		for (int32_t i = from; i < to; i++)
		{ 
			double dSmp = smpPtr[i];
			onePoleLPFilter(&filterLo, dSmp, &dSampleData[i]);
		}
	}

	double dAmp = 1.0;

	if (editor.normalizeFiltersFlag)
	{
		const double dPeak = getDoublePeak(dSampleData, s->length);
		if (dPeak > 0.0)
			dAmp = INT8_MAX / dPeak;
	}

	int8_t *smpPtr = &song->sampleData[s->offset];
	for (int32_t i = from; i < to; i++)
	{
		int16_t smp16 = (int16_t)round(dSampleData[i] * dAmp);
		CLAMP8(smp16);
		smpPtr[i] = (int8_t)smp16;
	}

	free(dSampleData);

	fixSampleBeep(s);
	displaySample();
	updateWindowTitle(MOD_IS_MODIFIED);
}

void redoSampleData(int8_t sample)
{
	if (editor.sampleZero)
	{
		statusNotSampleZero();
		return;
	}

	assert(sample >= 0 && sample <= 30);
	moduleSample_t *s = &song->samples[sample];

	turnOffVoices();

	if (editor.smpRedoBuffer[sample] != NULL && editor.smpRedoLengths[sample] > 0)
	{
		memcpy(&song->sampleData[s->offset], editor.smpRedoBuffer[sample], editor.smpRedoLengths[sample]);

		if (editor.smpRedoLengths[sample] < config.maxSampleLength)
			memset(&song->sampleData[s->offset + editor.smpRedoLengths[sample]], 0, config.maxSampleLength - editor.smpRedoLengths[sample]);
	}
	else
	{
		memset(&song->sampleData[s->offset], 0, config.maxSampleLength);
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
	if (ui.samplerScreenShown)
	{
		if (ui.samplerVolBoxShown)
			renderSamplerVolBox();
		else if (ui.samplerFiltersBoxShown)
			renderSamplerFiltersBox();
	}
}

void fillSampleRedoBuffer(int8_t sample)
{
	assert(sample >= 0 && sample <= 30);
	moduleSample_t *s = &song->samples[sample];

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
			memcpy(editor.smpRedoBuffer[sample], &song->sampleData[s->offset], s->length);
	}
}

bool allocSamplerVars(void) // must be called after config is loaded
{
	sampler.copyBuf = (int8_t *)malloc(config.maxSampleLength);
	sampler.blankSample = (int8_t *)calloc(config.maxSampleLength, 1);
	sampler.sampleUndoCopy = (int8_t *)calloc(config.maxSampleLength, 1);

	if (sampler.copyBuf == NULL || sampler.blankSample == NULL || sampler.sampleUndoCopy == NULL)
		return false;

	return true;
}

void deAllocSamplerVars(void)
{
	if (sampler.copyBuf != NULL)
	{
		free(sampler.copyBuf);
		sampler.copyBuf = NULL;
	}

	if (sampler.blankSample != NULL)
	{
		free(sampler.blankSample);
		sampler.blankSample = NULL;
	}
	
	if (sampler.sampleUndoCopy != NULL)
	{
		free(sampler.sampleUndoCopy);
		sampler.sampleUndoCopy = NULL;
	}

	for (int32_t i = 0; i < MOD_SAMPLES; i++)
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
	if (editor.sampleZero)
	{
		statusNotSampleZero();
		return;
	}

	assert(editor.currSample >= 0 && editor.currSample <= 30);

	moduleSample_t *s = &song->samples[editor.currSample];
	if (s->length == 0)
	{
		statusSampleIsEmpty();
		return;
	}

	int8_t *smpDat = &song->sampleData[s->offset];

	int32_t from = 0;
	int32_t to = s->length;

	if (editor.markStartOfs != -1)
	{
		from = editor.markStartOfs;
		to = editor.markEndOfs;

		if (to > s->length)
			to = s->length;

		if (from == to || from >= s->length || to < from)
		{
			from = 0;
			to = s->length;
		}
	}

	if (to <= 0)
		return;

	// calculate offset value
	int32_t offset = 0;
	for (int32_t i = from; i < to; i++)
		offset += smpDat[i];
	offset /= to;

	// remove DC offset
	for (int32_t i = from; i < to; i++)
	{
		int32_t smp32 = smpDat[i] - offset;
		CLAMP8(smp32);
		smpDat[i] = (int8_t)smp32;
	}

	fixSampleBeep(s);
	displaySample();
	updateWindowTitle(MOD_IS_MODIFIED);
}

#define INTRP_LINEAR_TAPS 2
#define INTRP8_LINEAR(s1, s2, f) /* output: -127..128 */ \
	s2 -= s1; \
	s2 *= (int32_t)(f >> 16); \
	s1 <<= 8; \
	s2 >>= 16-8; \
	s1 += s2; \
	s1 >>= 8; \

void samplerResample(void)
{
	int32_t samples[INTRP_LINEAR_TAPS];

	if (editor.sampleZero)
	{
		statusNotSampleZero();
		return;
	}

	assert(editor.currSample >= 0 && editor.currSample <= 30);
	assert(editor.tuningNote <= 35 && editor.resampleNote <= 35);

	moduleSample_t *s = &song->samples[editor.currSample];
	if (s->length == 0)
	{
		statusSampleIsEmpty();
		return;
	}

	// setup resampling variables
	int32_t readPos = 0;
	int32_t writePos = 0;
	int8_t *writeData = &song->sampleData[s->offset];
	int16_t refPeriod = periodTable[editor.tuningNote];
	int16_t newPeriod = periodTable[(37 * (s->fineTune & 0xF)) + editor.resampleNote];
	int32_t readLength = s->length;
	int32_t writeLength = (readLength * newPeriod) / refPeriod;

	if (readLength == writeLength)
		return; // no resampling needed

	// allocate memory for our sample duplicate
	int8_t *readData = (int8_t *)malloc(s->length);
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

	uint64_t delta64 = ((uint64_t)readLength << 32) / writeLength;
	assert(delta64 != 0);

	writeLength = writeLength & ~1;
	if (writeLength > config.maxSampleLength)
		writeLength = config.maxSampleLength;

	memcpy(readData, writeData, readLength);

	// resample

	turnOffVoices();

	uint64_t frac64 = 0;
	while (writePos < writeLength)
	{
		// collect samples for interpolation
		for (int32_t i = 0; i < INTRP_LINEAR_TAPS; i++)
		{
			int32_t pos = readPos + i;
			if (pos >= readLength)
				samples[i] = 0;
			else
				samples[i] = readData[pos];
		}

		INTRP8_LINEAR(samples[0], samples[1], frac64);
		writeData[writePos++] = (int8_t)samples[0];

		frac64 += delta64;
		readPos += frac64 >> 32;
		frac64 &= UINT32_MAX;
	}
	free(readData);

	// wipe non-used data in new sample
	if (writeLength < config.maxSampleLength)
		memset(&writeData[writePos], 0, config.maxSampleLength - writeLength);

	// update sample attributes
	s->length = writeLength;
	s->fineTune = 0;

	// scale loop points (and deactivate if overflowing)
	if ((s->loopStart + s->loopLength) > 2)
	{
		int32_t loopStart = (int32_t)(((uint64_t)s->loopStart << 32) / delta64) & ~1;
		int32_t loopLength = (int32_t)(((uint64_t)s->loopLength << 32) / delta64) & ~1;

		if (loopStart+loopLength > s->length)
		{
			s->loopStart = 0;
			s->loopLength = 2;
		}
		else
		{
			s->loopStart = loopStart;
			s->loopLength = loopLength;
		}
	}

	fixSampleBeep(s);
	updateCurrSample();
	updateWindowTitle(MOD_IS_MODIFIED);
}

// reads two hex chars from pointer and converts them to one byte
static uint8_t hexToInteger2(char *ptr)
{
	char hi = ptr[0];
	char lo = ptr[1];

	if (hi >= '0' && hi <= '9')
		hi -= '0';
	else if (hi >= 'A' && hi <= 'F')
		hi -= 'A'-10;
	else if (hi >= 'a' && hi <= 'f')
		hi -= 'a'-10;
	else
		hi = 0;

	if (lo >= '0' && lo <= '9')
		lo -= '0';
	else if (lo >= 'A' && lo <= 'F')
		lo -= 'A'-10;
	else if (lo >= 'a' && lo <= 'f')
		lo -= 'a'-10;
	else
		lo = 0;

	return (hi << 4) | lo;
}

void doMix(void)
{
	int8_t *fromPtr1, *fromPtr2;
	int32_t mixLength;

	uint8_t smpFrom1 = hexToInteger2(&editor.mixText[4]);
	uint8_t smpFrom2 = hexToInteger2(&editor.mixText[7]);
	uint8_t smpTo = hexToInteger2(&editor.mixText[13]);

	if (smpFrom1 == 0 || smpFrom1 > 0x1F || smpFrom2 == 0 || smpFrom2 > 0x1F || smpTo == 0 || smpTo > 0x1F)
	{
		displayErrorMsg("NOT RANGE 01-1F !");
		return;
	}

	smpFrom1--;
	smpFrom2--;
	smpTo--;

	moduleSample_t *s1 = &song->samples[smpFrom1];
	moduleSample_t *s2 = &song->samples[smpFrom2];
	moduleSample_t *s3 = &song->samples[smpTo];

	if (s1->length == 0 || s2->length == 0)
	{
		displayErrorMsg("EMPTY SAMPLES !!!");
		return;
	}

	if (s1->length > s2->length)
	{
		fromPtr1 = &song->sampleData[s1->offset];
		fromPtr2 = &song->sampleData[s2->offset];
		mixLength = s1->length;
	}
	else
	{
		fromPtr1 = &song->sampleData[s2->offset];
		fromPtr2 = &song->sampleData[s1->offset];
		mixLength = s2->length;
	}

	int8_t *mixPtr = (int8_t *)malloc(mixLength);
	if (mixPtr == NULL)
	{
		statusOutOfMemory();
		return;
	}

	turnOffVoices();

	for (int32_t i = 0; i < mixLength; i++)
	{
		int16_t tmp16 = (i < s2->length) ? (fromPtr1[i] + fromPtr2[i]) : fromPtr1[i];
		if (editor.halfClipFlag == 0)
			tmp16 >>= 1;

		CLAMP8(tmp16);
		mixPtr[i] = (int8_t)tmp16;
	}

	memcpy(&song->sampleData[s3->offset], mixPtr, mixLength);
	if (mixLength < config.maxSampleLength)
		memset(&song->sampleData[s3->offset + mixLength], 0, config.maxSampleLength - mixLength);

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
void boostSample(int32_t sample, bool ignoreMark)
{
	assert(sample >= 0 && sample <= 30);

	moduleSample_t *s = &song->samples[sample];
	if (s->length == 0)
		return; // don't display warning/show warning pointer, it is done elsewhere

	int8_t *smpDat = &song->sampleData[s->offset];

	int32_t from = 0;
	int32_t to = s->length;

	if (!ignoreMark)
	{
		if (editor.markStartOfs != -1)
		{
			from = editor.markStartOfs;
			to = editor.markEndOfs;

			if (to > s->length)
				to = s->length;

			if (from == to || from >= s->length || to < from)
			{
				from = 0;
				to = s->length;
			}
		}
	}

	int8_t prevSmp = 0;
	for (int32_t i = from; i < to; i++)
	{
		int16_t tmp16 = smpDat[i] - prevSmp;

		int16_t smp16;
		if (tmp16 < 0)
			smp16 = smpDat[i] - (-tmp16 >> 2);
		else
			smp16 = smpDat[i] + ( tmp16 >> 2);

		prevSmp = smpDat[i];

		CLAMP8(smp16);
		smpDat[i] = (int8_t)smp16;
	}

	fixSampleBeep(s);

	// don't redraw sample here, it is done elsewhere
}

// this is actually treble decrease
void filterSample(int32_t sample, bool ignoreMark)
{
	assert(sample >= 0 && sample <= 30);

	moduleSample_t *s = &song->samples[sample];
	if (s->length == 0)
		return; // don't display warning/show warning pointer, it is done elsewhere

	int8_t *smpDat = &song->sampleData[s->offset];

	int32_t from = 1;
	int32_t to = s->length;

	if (!ignoreMark)
	{
		if (editor.markStartOfs != -1)
		{
			from = editor.markStartOfs;
			to = editor.markEndOfs;

			if (to > s->length)
				to = s->length;

			if (from == to || from >= s->length || to < from)
			{
				from = 0;
				to = s->length;
			}
		}
	}

	if (to < 1)
		return;
	to--;

	for (int32_t i = from; i < to; i++)
		smpDat[i] = (smpDat[i+0] + smpDat[i+1]) >> 1;

	fixSampleBeep(s);
	// don't redraw sample here, it is done elsewhere
}

void toggleTuningTone(void)
{
	// bugfix: don't allow tuning tone during play (it was very bugged anyway)
	if (editor.currMode == MODE_PLAY || editor.currMode == MODE_RECORD)
		return;

	editor.tuningToneFlag ^= 1;
	if (editor.tuningToneFlag)
	{
		// turn tuning tone on

		const int32_t chNum = (cursor.channel + 1) & 3;
		TToneBit = 1 << chNum;

		lockAudio();

		const uint32_t voiceAddr = 0xDFF0A0 + (chNum * 16);

		paulaWriteWord(0xDFF096, TToneBit); // voice DMA off

		paulaWriteWord(voiceAddr + 6, periodTable[editor.tuningNote]);
		paulaWriteWord(voiceAddr + 8, 64); // volume
		paulaWritePtr(voiceAddr + 0, tuneToneData);
		paulaWriteWord(voiceAddr + 4, sizeof (tuneToneData) / 2); // length

		paulaWriteWord(0xDFF096, 0x8000 | TToneBit); // voice DMA on

		// update tracker visuals
		setVisualsDMACON(TToneBit);
		setVisualsPeriod(chNum, periodTable[editor.tuningNote]);
		setVisualsVolume(chNum, 64);
		setVisualsDataPtr(chNum, tuneToneData);
		setVisualsLength(chNum, sizeof (tuneToneData) / 2);
		setVisualsDMACON(0x8000 | TToneBit);

		unlockAudio();
	}
	else
	{
		// turn tuning tone off

		lockAudio();
		paulaWriteWord(0xDFF096, TToneBit); // voice DMA off
		setVisualsDMACON(TToneBit);
		unlockAudio();
	}
}

void sampleMarkerToBeg(void)
{
	assert(editor.currSample >= 0 && editor.currSample <= 30);

	moduleSample_t *s = &song->samples[editor.currSample];
	if (s->length == 0)
	{
		invertRange();
		editor.markStartOfs = -1;
		editor.samplePos = 0;
	}
	else
	{
		invertRange();
		if (keyb.shiftPressed && editor.markStartOfs != -1)
		{
			editor.markStartOfs = sampler.samOffset;
		}
		else
		{
			editor.markStartOfs = sampler.samOffset;
			editor.markEndOfs = editor.markStartOfs;
		}
		invertRange();

		editor.samplePos = editor.markEndOfs;
	}

	updateSamplePos();
}

void sampleMarkerToCenter(void)
{
	assert(editor.currSample >= 0 && editor.currSample <= 30);

	moduleSample_t *s = &song->samples[editor.currSample];
	if (s->length == 0)
	{
		invertRange();
		editor.markStartOfs = -1;
		editor.samplePos = 0;
	}
	else
	{
		int32_t middlePos = sampler.samOffset + ((sampler.samDisplay + 1) / 2);

		invertRange();
		if (keyb.shiftPressed && editor.markStartOfs != -1)
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

		editor.samplePos = editor.markEndOfs;
	}

	updateSamplePos();
}

void sampleMarkerToEnd(void)
{
	assert(editor.currSample >= 0 && editor.currSample <= 30);

	moduleSample_t *s = &song->samples[editor.currSample];
	if (s->length == 0)
	{
		invertRange();
		editor.markStartOfs = -1;
		editor.samplePos = 0;
	}
	else
	{
		invertRange();
		if (keyb.shiftPressed && editor.markStartOfs != -1)
		{
			editor.markEndOfs = s->length;
		}
		else
		{
			editor.markStartOfs = s->length;
			editor.markEndOfs = editor.markStartOfs;
		}
		invertRange();

		editor.samplePos = editor.markEndOfs;
	}

	updateSamplePos();
}

void samplerSamCopy(void)
{
	if (editor.sampleZero)
	{
		statusNotSampleZero();
		return;
	}

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

	assert(editor.currSample >= 0 && editor.currSample <= 30);

	moduleSample_t *s = &song->samples[editor.currSample];
	if (s->length == 0)
	{
		statusSampleIsEmpty();
		return;
	}

	sampler.copyBufSize = editor.markEndOfs - editor.markStartOfs;

	if ((int32_t)(editor.markStartOfs + sampler.copyBufSize) > config.maxSampleLength)
	{
		displayErrorMsg("COPY ERROR !");
		return;
	}

	memcpy(sampler.copyBuf, &song->sampleData[s->offset+editor.markStartOfs], sampler.copyBufSize);
}

void samplerSamDelete(uint8_t cut)
{
	if (editor.sampleZero)
	{
		statusNotSampleZero();
		return;
	}

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

	assert(editor.currSample >= 0 && editor.currSample <= 30);
	moduleSample_t *s = &song->samples[editor.currSample];

	int32_t sampleLength = s->length;
	if (sampleLength == 0)
	{
		statusSampleIsEmpty();
		return;
	}

	turnOffVoices();

	// if whole sample is marked, wipe it
	if (editor.markEndOfs-editor.markStartOfs >= sampleLength)
	{
		memset(&song->sampleData[s->offset], 0, config.maxSampleLength);

		invertRange();
		editor.markStartOfs = -1;

		sampler.samStart = sampler.blankSample;
		sampler.samDisplay = SAMPLE_AREA_WIDTH;
		sampler.samLength = SAMPLE_AREA_WIDTH;

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

	int32_t markEnd = (editor.markEndOfs > sampleLength) ? sampleLength : editor.markEndOfs;
	int32_t markStart = editor.markStartOfs;

	int32_t copyLength = (editor.markStartOfs + sampleLength) - markEnd;
	if (copyLength < 2 || copyLength > config.maxSampleLength)
	{
		displayErrorMsg("SAMPLE CUT FAIL !");
		return;
	}

	int8_t *tmpBuf = (int8_t *)malloc(copyLength);
	if (tmpBuf == NULL)
	{
		statusOutOfMemory();
		return;
	}

	// copy start part
	memcpy(tmpBuf, &song->sampleData[s->offset], editor.markStartOfs);

	// copy end part
	if (sampleLength-markEnd > 0)
		memcpy(&tmpBuf[editor.markStartOfs], &song->sampleData[s->offset+markEnd], sampleLength - markEnd);

	// wipe sample data and copy over the result
	memcpy(&song->sampleData[s->offset], tmpBuf, copyLength);

	if (copyLength < config.maxSampleLength)
		memset(&song->sampleData[s->offset+copyLength], 0, config.maxSampleLength - copyLength);

	free(tmpBuf);

	sampler.samLength = copyLength;
	if (sampler.samOffset+sampler.samDisplay >= sampler.samLength)
	{
		if (sampler.samDisplay < sampler.samLength)
		{
			if (sampler.samLength-sampler.samDisplay < 0)
			{
				sampler.samOffset = 0;
				sampler.samDisplay = sampler.samLength;
			}
			else
			{
				sampler.samOffset = sampler.samLength - sampler.samDisplay;
			}
		}
		else
		{
			sampler.samOffset = 0;
			sampler.samDisplay = sampler.samLength;
		}

		updateSamOffset();
	}

	if (s->loopStart+s->loopLength > 2) // loop enabled?
	{
		if (markEnd > s->loopStart)
		{
			if (markStart < s->loopStart+s->loopLength)
			{
				// we cut data inside the loop, increase loop length
				int32_t val32 = (s->loopLength - (markEnd - markStart)) & ~1;
				if (val32 < 2)
					val32 = 2;

				s->loopLength = val32;
			}

			// we cut data after the loop, don't modify loop points
		}
		else
		{
			// we cut data before the loop, adjust loop start point
			int32_t val32 = (s->loopStart - (markEnd - markStart)) & ~1;
			if (val32 < 0)
			{
				s->loopStart = 0;
				s->loopLength = 2;
			}
			else
			{
				s->loopStart = val32;
			}
		}
	}

	s->length = copyLength & ~1;

	// disable loop if invalid
	if (s->loopStart+s->loopLength > s->length)
	{
		s->loopStart = 0;
		s->loopLength = 2;
	}

	if (sampler.samDisplay <= 2)
	{
		sampler.samStart = sampler.blankSample;
		sampler.samLength = SAMPLE_AREA_WIDTH;
		sampler.samDisplay = SAMPLE_AREA_WIDTH;
	}

	invertRange();
	if (sampler.samDisplay == 0)
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

	ui.updateCurrSampleLength = true;
	ui.updateCurrSampleRepeat = true;
	ui.updateCurrSampleReplen = true;
	ui.updateSongSize = true;

	updateWindowTitle(MOD_IS_MODIFIED);
}

void samplerSamPaste(void)
{
	if (editor.sampleZero)
	{
		statusNotSampleZero();
		return;
	}

	if (sampler.copyBuf == NULL || sampler.copyBufSize == 0)
	{
		displayErrorMsg("BUFFER IS EMPTY");
		return;
	}

	assert(editor.currSample >= 0 && editor.currSample <= 30);

	moduleSample_t *s = &song->samples[editor.currSample];
	if (s->length > 0 && editor.markStartOfs == -1)
	{
		displayErrorMsg("SET CURSOR POS");
		return;
	}

	int32_t markStart = editor.markStartOfs;
	if (s->length == 0)
		markStart = 0;

	if (s->length+sampler.copyBufSize > config.maxSampleLength)
	{
		displayErrorMsg("NOT ENOUGH ROOM");
		return;
	}

	int8_t *tmpBuf = (int8_t *)malloc(config.maxSampleLength);
	if (tmpBuf == NULL)
	{
		statusOutOfMemory();
		return;
	}

	uint32_t readPos = 0;
	turnOffVoices();
	bool wasZooming = (sampler.samDisplay != sampler.samLength);

	// copy start part
	if (markStart > 0)
	{
		memcpy(&tmpBuf[readPos], &song->sampleData[s->offset], markStart);
		readPos += markStart;
	}

	// copy actual buffer
	memcpy(&tmpBuf[readPos], sampler.copyBuf, sampler.copyBufSize);

	// copy end part
	if (markStart >= 0)
	{
		readPos += sampler.copyBufSize;

		if (s->length-markStart > 0)
			memcpy(&tmpBuf[readPos], &song->sampleData[s->offset+markStart], s->length - markStart);
	}

	int32_t newLength = (s->length + sampler.copyBufSize) & ~1;
	if (newLength > config.maxSampleLength)
		newLength = config.maxSampleLength;

	sampler.samLength = s->length = newLength;

	if (s->loopLength > 2) // loop enabled?
	{
		if (markStart > s->loopStart)
		{
			if (markStart < s->loopStart+s->loopLength)
			{
				// we pasted data inside the loop, increase loop length

				if (s->loopLength+sampler.copyBufSize > config.maxSampleLength)
				{
					s->loopStart = 0;
					s->loopLength = 2;
				}
				else
				{
					s->loopLength = (s->loopLength + sampler.copyBufSize) & config.maxSampleLength;
					if (s->loopStart+s->loopLength > s->length)
					{
						s->loopStart = 0;
						s->loopLength = 2;
					}
				}
			}

			// we pasted data after the loop, don't modify loop points
		}
		else
		{
			// we pasted data before the loop, adjust loop start point
			if (s->loopStart+sampler.copyBufSize > config.maxSampleLength)
			{
				s->loopStart = 0;
				s->loopLength = 2;
			}
			else
			{
				s->loopStart = (s->loopStart + sampler.copyBufSize) & config.maxSampleLength;
				if (s->loopStart+s->loopLength > s->length)
				{
					s->loopStart = 0;
					s->loopLength = 2;
				}
			}
		}
	}

	memcpy(&song->sampleData[s->offset], tmpBuf, s->length);

	// clear data after sample's length (if present)
	if (s->length < config.maxSampleLength)
		memset(&song->sampleData[s->offset+s->length], 0, config.maxSampleLength - s->length);

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

	ui.updateCurrSampleLength = true;
	ui.updateSongSize = true;

	updateWindowTitle(MOD_IS_MODIFIED);
}

static void playCurrSample(uint8_t chn, int32_t startOffset, int32_t endOffset, bool playWaveformFlag)
{
	assert(editor.currSample >= 0 && editor.currSample <= 30);
	assert(chn < PAULA_VOICES);
	assert(editor.currPlayNote <= 35);

	moduleSample_t *s = &song->samples[editor.currSample];
	moduleChannel_t *ch = &song->channels[chn];

	lockAudio();

	ch->n_samplenum = editor.currSample;
	ch->n_volume = s->volume;
	ch->n_period = periodTable[(37 * (s->fineTune & 0xF)) + editor.currPlayNote];
	
	if (playWaveformFlag)
	{
		ch->n_start = &song->sampleData[s->offset];
		ch->n_length = (uint16_t)((s->loopStart > 0) ? (s->loopStart + s->loopLength) >> 1 : s->length >> 1);
		ch->n_loopstart = &song->sampleData[s->offset + s->loopStart];
		ch->n_replen = (uint16_t)(s->loopLength >> 1);
	}
	else
	{
		ch->n_start = &song->sampleData[s->offset + startOffset];
		ch->n_length = (uint16_t)((uint32_t)(endOffset - startOffset) >> 1);
		ch->n_loopstart = &song->sampleData[s->offset];
		ch->n_replen = 1;
	}

	if (ch->n_length == 0)
		ch->n_length = 1;

	const uint32_t voiceAddr = 0xDFF0A0 + (chn * 16);

	paulaWriteWord(voiceAddr + 8, ch->n_volume);
	paulaWriteWord(voiceAddr + 6, ch->n_period);
	paulaWritePtr(voiceAddr + 0, ch->n_start);
	paulaWriteWord(voiceAddr + 4, ch->n_length);

	if (!editor.muted[chn])
		paulaWriteWord(0xDFF096, 0x8000 | ch->n_dmabit); // voice DMA on
	else
		paulaWriteWord(0xDFF096, ch->n_dmabit); // voice DMA off

	// these take effect after the current DMA cycle is done
	if (playWaveformFlag)
	{
		paulaWritePtr(voiceAddr + 0, ch->n_loopstart);
		paulaWriteWord(voiceAddr + 4, ch->n_replen);
	}
	else
	{
		paulaWritePtr(voiceAddr + 0, NULL); // data
		paulaWriteWord(voiceAddr + 4, 1); // length
	}

	// update tracker visuals

	setVisualsVolume(chn, ch->n_volume);
	setVisualsPeriod(chn, ch->n_period);
	setVisualsDataPtr(chn, ch->n_start);
	setVisualsLength(chn, ch->n_length);

	if (!editor.muted[chn])
		setVisualsDMACON(0x8000 | ch->n_dmabit);
	else
		setVisualsDMACON(ch->n_dmabit);

	if (playWaveformFlag)
	{
		setVisualsDataPtr(chn, ch->n_loopstart);
		setVisualsLength(chn, ch->n_replen);
	}
	else
	{
		setVisualsDataPtr(chn, NULL);
		setVisualsLength(chn, 1);
	}

	unlockAudio();

	// PT quirk: spectrum analyzer is still handled here even if channel is muted
	updateSpectrumAnalyzer(ch->n_volume, ch->n_period);
}

void samplerPlayWaveform(void)
{
	playCurrSample(cursor.channel, 0, 0, true);
}

void samplerPlayDisplay(void)
{
	int32_t start = sampler.samOffset;
	int32_t end = sampler.samOffset + sampler.samDisplay;

	playCurrSample(cursor.channel, start, end, false);
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

	playCurrSample(cursor.channel, editor.markStartOfs, editor.markEndOfs, false);
}

void setLoopSprites(void)
{
	if (!ui.samplerScreenShown)
	{
		hideSprite(SPRITE_LOOP_PIN_LEFT);
		hideSprite(SPRITE_LOOP_PIN_RIGHT);
		return;
	}

	assert(editor.currSample >= 0 && editor.currSample <= 30);

	moduleSample_t *s = &song->samples[editor.currSample];
	if (s->loopStart+s->loopLength > 2)
	{
		if (sampler.samDisplay > 0)
		{
			sampler.loopStartPos = (int16_t)smpPos2Scr(s->loopStart);
			if (sampler.loopStartPos >= 0 && sampler.loopStartPos <= SAMPLE_AREA_WIDTH)
				setSpritePos(SPRITE_LOOP_PIN_LEFT, sampler.loopStartPos, 138);
			else
				hideSprite(SPRITE_LOOP_PIN_LEFT);

			sampler.loopEndPos = (int16_t)smpPos2Scr(s->loopStart + s->loopLength);

			/* Nasty kludge for where the right loop pin would sometimes disappear
			** when zoomed in and scrolled all the way to the right.
			*/
			if (sampler.loopEndPos == SAMPLE_AREA_WIDTH+1)
				sampler.loopEndPos = SAMPLE_AREA_WIDTH;

			if (sampler.loopEndPos >= 0 && sampler.loopEndPos <= SAMPLE_AREA_WIDTH)
				setSpritePos(SPRITE_LOOP_PIN_RIGHT, sampler.loopEndPos + 3, 138);
			else
				hideSprite(SPRITE_LOOP_PIN_RIGHT);
		}
	}
	else
	{
		sampler.loopStartPos = 0;
		sampler.loopEndPos = 0;

		hideSprite(SPRITE_LOOP_PIN_LEFT);
		hideSprite(SPRITE_LOOP_PIN_RIGHT);
	}

	textOutBg(288, 225, (s->loopStart+s->loopLength > 2) ? "ON " : "OFF", video.palette[PAL_GENTXT], video.palette[PAL_GENBKG]);
}

void samplerShowAll(void)
{
	if (sampler.samDisplay == sampler.samLength)
		return; // don't attempt to show all if already showing all!

	sampler.samOffset = 0;
	sampler.samDisplay = sampler.samLength;

	updateSamOffset();
	displaySample();
}

static void samplerZoomIn(int32_t step, int32_t x)
{
	if (song->samples[editor.currSample].length == 0 || sampler.samDisplay <= 2)
		return;

	if (step < 1)
		step = 1;

	int32_t tmpDisplay = sampler.samDisplay - (step << 1);
	if (tmpDisplay < 2)
		tmpDisplay = 2;

	const int32_t roundingBias = SCREEN_W / 4;

	step += (((x - (SCREEN_W / 2)) * step) + roundingBias) / (SCREEN_W / 2);

	int32_t tmpOffset = sampler.samOffset + step;
	if (tmpOffset < 0)
		tmpOffset = 0;

	if (tmpOffset+tmpDisplay > sampler.samLength)
		tmpOffset = sampler.samLength-tmpDisplay;

	sampler.samOffset = tmpOffset;
	sampler.samDisplay = tmpDisplay;

	updateSamOffset();
	displaySample();
}

static void samplerZoomOut(int32_t step, int32_t x)
{
	int32_t tmpOffset;

	if (song->samples[editor.currSample].length == 0 || sampler.samDisplay == sampler.samLength)
		return;

	if (step < 1)
		step = 1;

	int32_t tmpDisplay = sampler.samDisplay + (step << 1);
	if (tmpDisplay > sampler.samLength)
	{
		tmpOffset  = 0;
		tmpDisplay = sampler.samLength;
	}
	else
	{
		const int32_t roundingBias = SCREEN_W / 4;

		step += (((x - (SCREEN_W / 2)) * step) + roundingBias) / (SCREEN_W / 2);

		tmpOffset = sampler.samOffset - step;
		if (tmpOffset < 0)
			tmpOffset = 0;

		if (tmpOffset+tmpDisplay > sampler.samLength)
			tmpOffset = sampler.samLength-tmpDisplay;
	}

	sampler.samOffset = tmpOffset;
	sampler.samDisplay = tmpDisplay;

	updateSamOffset();
	displaySample();
}

void samplerZoomInMouseWheel(void)
{
	samplerZoomIn((sampler.samDisplay + 5) / 10, mouse.x);
}

void samplerZoomOutMouseWheel(void)
{
	samplerZoomOut((sampler.samDisplay + 5) / 10, mouse.x);
}

void samplerZoomOut2x(void)
{
	samplerZoomOut((sampler.samDisplay + 1) / 2, SCREEN_W / 2);
}

void samplerRangeAll(void)
{
	assert(editor.currSample >= 0 && editor.currSample <= 30);

	moduleSample_t *s = &song->samples[editor.currSample];
	if (s->length == 0)
	{
		invertRange();
		editor.markStartOfs = -1;
	}
	else
	{
		invertRange();
		editor.markStartOfs = sampler.samOffset;
		editor.markEndOfs = sampler.samOffset + sampler.samDisplay;
		invertRange();
	}
}

void samplerShowRange(void)
{
	assert(editor.currSample >= 0 && editor.currSample <= 30);

	moduleSample_t *s = &song->samples[editor.currSample];
	if (s->length == 0)
	{
		statusSampleIsEmpty();
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

	sampler.samDisplay = editor.markEndOfs - editor.markStartOfs;
	sampler.samOffset = editor.markStartOfs;

	if (sampler.samDisplay+sampler.samOffset > sampler.samLength)
		sampler.samOffset = sampler.samLength-sampler.samDisplay;

	updateSamOffset();

	invertRange();
	editor.markStartOfs = -1;

	displaySample();
}

void volBoxBarPressed(bool mouseButtonHeld)
{
	if (!mouseButtonHeld)
	{
		if (mouse.x >= 72 && mouse.x <= 173)
		{
			if (mouse.y >= 154 && mouse.y <= 174) ui.forceVolDrag = 1;
			if (mouse.y >= 165 && mouse.y <= 175) ui.forceVolDrag = 2;
		}
	}
	else
	{
		if (sampler.lastMouseX != mouse.x)
		{
			sampler.lastMouseX = mouse.x;
			int32_t mouseX = CLAMP(sampler.lastMouseX - 107, 0, 60);

			if (ui.forceVolDrag == 1)
			{
				editor.vol1 = (int16_t)(((mouseX * 200) + (60/2)) / 60); // rounded
				ui.updateVolFromText = true;
				showVolFromSlider();
			}
			else if (ui.forceVolDrag == 2)
			{
				editor.vol2 = (int16_t)(((mouseX * 200) + (60/2)) / 60); // rounded
				ui.updateVolToText = true;
				showVolToSlider();
			}
		}
	}
}

void samplerBarPressed(bool mouseButtonHeld)
{
	if (!mouseButtonHeld)
	{
		if (mouse.x >= 4 && mouse.x <= 315)
		{
			if (mouse.x < sampler.dragStart)
			{
				int32_t tmp32 = sampler.samOffset - sampler.samDisplay;
				if (tmp32 < 0)
					tmp32 = 0;

				if (tmp32 == sampler.samOffset)
					return;

				sampler.samOffset = tmp32;

				updateSamOffset();
				displaySample();
				return;
			}

			if (mouse.x > sampler.dragEnd)
			{
				int32_t tmp32 = sampler.samOffset + sampler.samDisplay;
				if (tmp32+sampler.samDisplay <= sampler.samLength)
				{
					if (tmp32 == sampler.samOffset)
						return;

					sampler.samOffset = tmp32;
				}
				else
				{
					tmp32 = sampler.samLength - sampler.samDisplay;
					if (tmp32 == sampler.samOffset)
						return;

					sampler.samOffset = tmp32;
				}

				updateSamOffset();
				displaySample();
				return;
			}

			sampler.lastSamPos = mouse.x;
			sampler.saveMouseX = sampler.lastSamPos - sampler.dragStart;

			ui.forceSampleDrag = true;
		}
	}

	if (mouse.x != sampler.lastSamPos)
	{
		sampler.lastSamPos = mouse.x;

		int32_t tmp32 = sampler.lastSamPos - sampler.saveMouseX - 4;
		tmp32 = CLAMP(tmp32, 0, SAMPLE_AREA_WIDTH);

		tmp32 = ((tmp32 * sampler.samLength) + (311/2)) / 311; // rounded
		if (tmp32+sampler.samDisplay <= sampler.samLength)
		{
			if (tmp32 == sampler.samOffset)
				return;

			sampler.samOffset = tmp32;
		}
		else
		{
			tmp32 = sampler.samLength - sampler.samDisplay;
			if (tmp32 == sampler.samOffset)
				return;

			sampler.samOffset = tmp32;
		}

		updateSamOffset();
		displaySample();
	}
}

static int32_t mouseYToSampleY(int32_t my)
{
	if (my == SAMPLE_AREA_Y_CENTER) // center
	{
		return 128;
	}
	else
	{
		int32_t tmp32 = my - 138;
		tmp32 = ((tmp32 << 8) + (SAMPLE_AREA_HEIGHT/2)) / SAMPLE_AREA_HEIGHT;
		tmp32 = CLAMP(tmp32, 0, 255);
		tmp32 ^= 0xFF;

		return tmp32;
	}
}

void samplerEditSample(bool mouseButtonHeld)
{
	int32_t p, vl;

	if (editor.sampleZero)
	{
		statusNotSampleZero();
		return;
	}

	assert(editor.currSample >= 0 && editor.currSample <= 30);
	moduleSample_t *s = &song->samples[editor.currSample];

	if (s->length == 0)
	{
		displayErrorMsg("SAMPLE LENGTH = 0");
		return;
	}

	int32_t mx = mouse.x;
	if (mx > 4+SAMPLE_AREA_WIDTH)
		mx = 4+SAMPLE_AREA_WIDTH;

	int32_t my = mouse.y;

	if (!mouseButtonHeld)
	{
		lastDrawX = scr2SmpPos(mx);
		lastDrawY = mouseYToSampleY(my);

		ui.forceSampleEdit = true;
		updateWindowTitle(MOD_IS_MODIFIED);
	}
	else if (mx == sampler.lastMouseX && my == sampler.lastMouseY)
	{
		return; // don't continue if we didn't move the mouse
	}

	if (mx != sampler.lastMouseX)
		p = scr2SmpPos(mx);
	else
		p = lastDrawX;

	if (!keyb.shiftPressed && my != sampler.lastMouseY)
		vl = mouseYToSampleY(my);
	else
		vl = lastDrawY;

	sampler.lastMouseX = mx;
	sampler.lastMouseY = my;

	int32_t r = p;
	int32_t rvl = vl;

	// swap x/y if needed
	if (p > lastDrawX)
	{
		// swap x
		int32_t tmp32 = p;
		p = lastDrawX;
		lastDrawX = tmp32;

		// swap y
		tmp32 = lastDrawY;
		lastDrawY = vl;
		vl = tmp32;
	}

	int8_t *ptr8 = &song->sampleData[s->offset];

	int32_t start = p;
	if (start < 0)
		start = 0;

	int32_t end = lastDrawX+1;
	if (end > s->length)
		end = s->length;

	if (p == lastDrawX)
	{
		const int8_t smpVal = (int8_t)(vl ^ 0x80);
		for (int32_t i = start; i < end; i++)
			ptr8[i] = smpVal;
	}
	else
	{
		int32_t y = lastDrawY - vl;
		int32_t x = lastDrawX - p;

		if (x != 0)
		{
			double dMul = 1.0 / x;
			int32_t i = 0;

			for (int32_t rl = start; rl < end; rl++)
			{
				int32_t tvl = y * i;
				tvl = (int32_t)(tvl * dMul); // tvl /= x
				tvl += vl;
				tvl ^= 0x80;

				ptr8[rl] = (int8_t)tvl;
				i++;
			}
		}
	}

	lastDrawY = rvl;
	lastDrawX = r;

	displaySample();
}

void samplerSamplePressed(bool mouseButtonHeld)
{
	if (!mouseButtonHeld)
	{
		if (!editor.sampleZero && mouse.y < 142)
		{
			if (mouse.x >= sampler.loopStartPos && mouse.x <= sampler.loopStartPos+3)
			{
				ui.leftLoopPinMoving = true;
				ui.rightLoopPinMoving = false;
				ui.sampleMarkingPos = 1;
				sampler.lastMouseX = mouse.x;
				return;
			}
			else if (mouse.x >= sampler.loopEndPos+3 && mouse.x <= sampler.loopEndPos+6)
			{
				ui.rightLoopPinMoving = true;
				ui.leftLoopPinMoving = false;
				ui.sampleMarkingPos = 1;
				sampler.lastMouseX = mouse.x;
				return;
			}
		}
	}

	int32_t mouseX = CLAMP(mouse.x, 0, SCREEN_W+8); // allow some extra pixels outside of the screen

	assert(editor.currSample >= 0 && editor.currSample <= 30);
	moduleSample_t *s = &song->samples[editor.currSample];

	if (ui.leftLoopPinMoving)
	{
		if (sampler.lastMouseX != mouseX)
		{
			sampler.lastMouseX = mouseX;

			int32_t tmpPos = (scr2SmpPos(mouseX - 1) - s->loopStart) & ~1;
			if (tmpPos > config.maxSampleLength)
				tmpPos = config.maxSampleLength;

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

			ui.updateCurrSampleRepeat = true;
			ui.updateCurrSampleReplen = true;

			setLoopSprites();
			updatePaulaLoops();
			updateWindowTitle(MOD_IS_MODIFIED);
		}

		return;
	}

	if (ui.rightLoopPinMoving)
	{
		if (sampler.lastMouseX != mouseX)
		{
			sampler.lastMouseX = mouseX;

			s = &song->samples[editor.currSample];

			int32_t tmpPos = (scr2SmpPos(mouseX - 4) - s->loopStart) & ~1;
			tmpPos = CLAMP(tmpPos, 2, config.maxSampleLength);

			s->loopLength = tmpPos;

			ui.updateCurrSampleRepeat = true;
			ui.updateCurrSampleReplen = true;

			setLoopSprites();
			updatePaulaLoops();
			updateWindowTitle(MOD_IS_MODIFIED);
		}

		return;
	}

	if (!mouseButtonHeld)
	{
		if (mouseX < 0 || mouseX >= SCREEN_W)
			return;

		ui.sampleMarkingPos = (int16_t)mouseX;
		sampler.lastSamPos = ui.sampleMarkingPos;

		invertRange();
		if (s->length == 0)
		{
			editor.markStartOfs = -1; // clear marking
		}
		else
		{
			editor.markStartOfs = scr2SmpPos(ui.sampleMarkingPos - 3);
			editor.markEndOfs = scr2SmpPos(ui.sampleMarkingPos - 3);

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
			int32_t tmpPos = scr2SmpPos(mouseX - 3);
			if (tmpPos > s->length)
				tmpPos = s->length;

			editor.samplePos = tmpPos;
		}

		updateSamplePos();

		return;
	}

	mouseX = CLAMP(mouseX, 3, SCREEN_W);

	if (mouseX != sampler.lastSamPos)
	{
		sampler.lastSamPos = mouseX;

		invertRange();
		if (s->length == 0)
		{
			editor.markStartOfs = -1; // clear marking
		}
		else
		{
			if (sampler.lastSamPos > ui.sampleMarkingPos)
			{
				editor.markStartOfs = scr2SmpPos(ui.sampleMarkingPos - 3);
				editor.markEndOfs = scr2SmpPos(sampler.lastSamPos - 3);
			}
			else
			{
				editor.markStartOfs = scr2SmpPos(sampler.lastSamPos - 3);
				editor.markEndOfs = scr2SmpPos(ui.sampleMarkingPos - 3);
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
		int32_t tmpPos = scr2SmpPos(mouseX - 3);
		if (tmpPos > s->length)
			tmpPos = s->length;

		 editor.samplePos = tmpPos;
	}

	updateSamplePos();
}

void samplerLoopToggle(void)
{
	if (editor.sampleZero)
	{
		statusNotSampleZero();
		return;
	}

	assert(editor.currSample >= 0 && editor.currSample <= 30);

	moduleSample_t *s = &song->samples[editor.currSample];
	if (s->length < 2)
		return;

	turnOffVoices();

	if (s->loopStart+s->loopLength > 2)
	{
		// disable loop

		sampler.tmpLoopStart = s->loopStart;
		sampler.tmpLoopLength = s->loopLength;

		s->loopStart = 0;
		s->loopLength = 2;
	}
	else
	{
		// enable loop

		if (sampler.tmpLoopStart == 0 && sampler.tmpLoopLength == 0)
		{
			s->loopStart = 0;
			s->loopLength = s->length;
		}
		else
		{
			s->loopStart = sampler.tmpLoopStart;
			s->loopLength = sampler.tmpLoopLength;

			if (s->loopStart+s->loopLength > s->length)
			{
				s->loopStart = 0;
				s->loopLength = s->length;
			}
		}
	}

	ui.updateCurrSampleRepeat = true;
	ui.updateCurrSampleReplen = true;

	displaySample();
	updatePaulaLoops();
	recalcChordLength();
	updateWindowTitle(MOD_IS_MODIFIED);
}

void exitFromSam(void)
{
	ui.samplerScreenShown = false;
	memcpy(&video.frameBuffer[121 * SCREEN_W], &trackerFrameBMP[121 * SCREEN_W], 320 * 134 * sizeof (int32_t));

	updateCursorPos();
	setLoopSprites();

	ui.updateStatusText = true;
	ui.updateSongSize = true;
	ui.updateSongTiming = true;
	ui.updateSongBPM = true;
	ui.updateCurrPattText = true;
	ui.updatePatternData = true;

	editor.markStartOfs = -1;
}

void samplerScreen(void)
{
	if (ui.samplerScreenShown)
	{
		exitFromSam();
		return;
	}

	ui.samplerScreenShown = true;
	memcpy(&video.frameBuffer[(121 * SCREEN_W)], samplerScreenBMP, 320 * 134 * sizeof (int32_t));
	hideSprite(SPRITE_PATTERN_CURSOR);

	ui.updateStatusText = true;
	ui.updateSongSize = true;
	ui.updateSongTiming = true;
	ui.updateResampleNote = true;
	ui.update9xxPos = true;

	redrawSample();
}

void drawSamplerLine(void)
{
	hideSprite(SPRITE_SAMPLING_POS_LINE);
	if (!ui.samplerScreenShown || ui.samplerVolBoxShown || ui.samplerFiltersBoxShown)
		return;

	for (int32_t ch = 0; ch < PAULA_VOICES; ch++)
	{
		int32_t pos = getSampleReadPos(ch);
		if (pos >= 0)
		{
			pos = 3 + smpPos2Scr(pos);
			if (pos >= 3 && pos <= 316)
				setSpritePos(SPRITE_SAMPLING_POS_LINE, pos, 138);
		}
	}
}
