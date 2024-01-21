// for finding memory leaks in debug mode with Visual Studio 
#if defined _DEBUG && defined _MSC_VER
#include <crtdbg.h>
#endif

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <math.h>
#include "pt2_config.h"
#include "pt2_helpers.h"
#include "pt2_visuals.h"
#include "pt2_audio.h"
#include "pt2_sampler.h"
#include "pt2_textout.h"
#include "pt2_tables.h"
#include "pt2_downsample2x.h"
#include "pt2_replayer.h"

static const char *noteStr[12] =
{
	"c-", "c#", "d-", "d#", "e-", "f-", "f#", "g-", "g#", "a-", "a#", "b-"
};

static bool pat2SmpEndReached;
static uint8_t pat2SmpFinetune = 4, pat2SmpNote = 33; // A-3 finetune +4 (default, max safe frequency)
static uint8_t pat2SmpStartRow = 0, pat2SmpRows = 32;
static int32_t pat2SmpPos;
static double *dMixBufferL, *dMixBufferR, *dPat2SmpBuf, dPat2SmpFreq, dSeconds;

static void pat2SmpOutputAudio(int32_t numSamples, bool outputEnable)
{
	int32_t samplesTodo = numSamples;

	if (outputEnable && pat2SmpPos+samplesTodo > config.maxSampleLength)
		samplesTodo = config.maxSampleLength - pat2SmpPos;

	paulaGenerateSamples(dMixBufferL, dMixBufferR, samplesTodo*2); // 2x oversampling

	if (outputEnable)
	{
		for (int32_t i = 0; i < samplesTodo; i++)
		{
			// 2x downsampling (decimation)
			double dL = decimate2x_L(dMixBufferL[(i << 1) + 0], dMixBufferL[(i << 1) + 1]);
			double dR = decimate2x_R(dMixBufferR[(i << 1) + 0], dMixBufferR[(i << 1) + 1]);

			dPat2SmpBuf[pat2SmpPos+i] = (dL + dR) * 0.5; // stereo -> mono, normalized to -128..127 later
		}

		pat2SmpPos += samplesTodo;
		if (pat2SmpPos >= config.maxSampleLength)
			pat2SmpEndReached = true;
	}	
}

void pat2SmpDrawNote(void)
{
	fillRect(165, 51, FONT_CHAR_W*3, FONT_CHAR_H, video.palette[PAL_GENBKG]);
	textOut(165, 51, noteNames1[2+pat2SmpNote], video.palette[PAL_GENTXT]);
}

void pat2SmpDrawFinetune(void)
{
	fillRect(173, 62, FONT_CHAR_W*2, FONT_CHAR_H, video.palette[PAL_GENBKG]);
	textOut(173, 62, ftuneStrTab[pat2SmpFinetune], video.palette[PAL_GENTXT]);
}

void pat2SmpDrawFrequency(void)
{
	const int32_t maxTextWidth = 19 * FONT_CHAR_W;
	fillRect(164, 74, maxTextWidth, FONT_CHAR_H, video.palette[PAL_GENBKG]);

	if (dPat2SmpFreq*2.0 < PAL_PAULA_MAX_HZ)
	{
		textOut(164, 74, "TOO LOW!", video.palette[PAL_GENTXT]);
	}
	else
	{
		char textBuf[32];
		sprintf(textBuf, "%dHz (%.1f secs)", (int32_t)(dPat2SmpFreq + 0.5), dSeconds);
		textOut(164, 74, textBuf, video.palette[PAL_GENTXT]);
	}
}

void pat2SmpDrawStartRow(void)
{
	fillRect(276, 51, FONT_CHAR_W*2, FONT_CHAR_H, video.palette[PAL_GENBKG]);
	printTwoDecimals(276, 51, pat2SmpStartRow, video.palette[PAL_GENTXT]);
}

void pat2SmpDrawRows(void)
{
	fillRect(276, 62, FONT_CHAR_W*2, FONT_CHAR_H, video.palette[PAL_GENBKG]);
	printTwoDecimals(276, 62, pat2SmpRows, video.palette[PAL_GENTXT]);
}

void pat2SmpCalculateFreq(void)
{
	if (pat2SmpFinetune > 15)
		pat2SmpFinetune = 15;

	if (pat2SmpNote > 35)
		pat2SmpNote = 35;

	dPat2SmpFreq = PAULA_PAL_CLK / (double)periodTable[(pat2SmpFinetune * 37) + pat2SmpNote];
	if (dPat2SmpFreq > PAL_PAULA_MAX_HZ)
		dPat2SmpFreq = PAL_PAULA_MAX_HZ;

	dSeconds = config.maxSampleLength / dPat2SmpFreq;
	pat2SmpDrawFrequency();
}

void pat2SmpNoteUp(void)
{
	if (pat2SmpNote < 35)
	{
		pat2SmpNote++;
		pat2SmpDrawNote();

		if (pat2SmpNote == 35 && pat2SmpFinetune < 8) // high-limit to B-3 finetune 0
		{
			pat2SmpFinetune = 0;
			pat2SmpDrawFinetune();
		}

		pat2SmpCalculateFreq();
	}
}

void pat2SmpNoteDown(void)
{
	if (pat2SmpNote > 23)
	{
		pat2SmpNote--;
		pat2SmpDrawNote();

		if (pat2SmpNote == 23 && pat2SmpFinetune > 7) // low-limit to B-2 finetune 0
		{
			pat2SmpFinetune = 0;
			pat2SmpDrawFinetune();
		}

		pat2SmpCalculateFreq();
	}
}

void pat2SmpSetFinetune(uint8_t finetune)
{
	pat2SmpFinetune = finetune & 0x0F;
	pat2SmpDrawFinetune();
	pat2SmpCalculateFreq();
}

void pat2SmpFinetuneUp(void)
{
	if ((pat2SmpFinetune & 0xF) != 7)
		pat2SmpFinetune = (pat2SmpFinetune + 1) & 0xF;

	if (pat2SmpNote == 35 && pat2SmpFinetune < 8) // for B-3, high-limit finetune to 0
		pat2SmpFinetune = 0;

	pat2SmpDrawFinetune();
	pat2SmpCalculateFreq();
}

void pat2SmpFinetuneDown(void)
{
	if ((pat2SmpFinetune & 0xF) != 8)
		pat2SmpFinetune = (pat2SmpFinetune - 1) & 0xF;

	if (pat2SmpNote == 23 && pat2SmpFinetune > 7) // for B-2, low-limit finetune to 0
		pat2SmpFinetune = 0;

	pat2SmpDrawFinetune();
	pat2SmpCalculateFreq();
}

void pat2SmpStartRowUp(void)
{
	if (pat2SmpStartRow+pat2SmpRows < 64)
	{
		pat2SmpStartRow++;
		pat2SmpDrawStartRow();
	}
}

void pat2SmpStartRowDown(void)
{
	if (pat2SmpStartRow > 0)
	{
		pat2SmpStartRow--;
		pat2SmpDrawStartRow();
	}
}

void pat2SmpRowsUp(void)
{
	if (pat2SmpStartRow+pat2SmpRows < 64)
	{
		pat2SmpRows++;
		pat2SmpDrawRows();
	}
}

void pat2SmpRowsDown(void)
{
	if (pat2SmpRows > 1)
	{
		pat2SmpRows--;
		pat2SmpDrawRows();
	}
}

void pat2SmpRender(void)
{
	if (editor.sampleZero)
	{
		statusNotSampleZero();
		return;
	}

	dPat2SmpBuf = (double *)malloc(config.maxSampleLength * sizeof (double));
	if (dPat2SmpBuf == NULL)
	{
		statusOutOfMemory();
		return;
	}

	const double dAudioFrequency = dPat2SmpFreq * 2.0; // *2 for oversampling
	int32_t maxSamplesPerTick = (int32_t)ceil(dAudioFrequency / (MIN_BPM / 2.5)) + 1;

	dMixBufferL = (double *)malloc(maxSamplesPerTick * sizeof (double));
	dMixBufferR = (double *)malloc(maxSamplesPerTick * sizeof (double));

	if (dMixBufferL == NULL || dMixBufferR == NULL)
	{
		free(dPat2SmpBuf);

		if (dMixBufferL != NULL) free(dMixBufferL);
		if (dMixBufferR != NULL) free(dMixBufferR);

		statusOutOfMemory();
		return;
	}

	const int8_t oldRow = editor.songPlaying ? 0 : song->currRow;

	editor.pat2SmpOngoing = true; // this must be set first

	// do some prep work
	generateBpmTable(dPat2SmpFreq, editor.timingMode == TEMPO_MODE_VBLANK);
	paulaSetup(dPat2SmpFreq*2.0, MODEL_A1200);
	paulaDisableFilters();
	storeTempVariables();
	restartSong(); // this also updates BPM (samples per tick) with the PAT2SMP audio output rate
	clearMixerDownsamplerStates();

	song->currRow = song->row = 0;
	pat2SmpPos = 0;

	uint64_t samplesToMixFrac = 0;

	bool lastRow = false;

	pat2SmpEndReached = false;
	while (!pat2SmpEndReached && editor.songPlaying)
	{
		/* PT replayer ticker (also sets audio.samplesPerTickInt and audio.samplesPerTickFrac).
		** Returns false on end of song.
		*/
		if (!intMusic())
			lastRow = true;

		if (song->row > pat2SmpStartRow+pat2SmpRows)
			break; // we rendered as many rows as requested (don't write this tick to output)

		uint32_t samplesToMix = audio.samplesPerTickInt;

		samplesToMixFrac += audio.samplesPerTickFrac;
		if (samplesToMixFrac >= BPM_FRAC_SCALE)
		{
			samplesToMixFrac &= BPM_FRAC_MASK;
			samplesToMix++;
		}

		if (lastRow && song->tick == song->speed-1)
			pat2SmpEndReached = true;

		const bool outputEnableFlag = lastRow || song->row > pat2SmpStartRow;
		pat2SmpOutputAudio(samplesToMix, outputEnableFlag);
	}
	editor.pat2SmpOngoing = false;

	free(dMixBufferL);
	free(dMixBufferR);

	song->currRow = song->row = oldRow; // set back old row

	// set back audio configurations
	const int32_t paulaMixFrequency = audio.oversamplingFlag ? audio.outputRate*2 : audio.outputRate;
	paulaSetup(paulaMixFrequency, audio.amigaModel);
	generateBpmTable(audio.outputRate, editor.timingMode == TEMPO_MODE_VBLANK);
	clearMixerDownsamplerStates();
	resetSong(); // this also updates BPM (samples per tick) with the tracker's audio output rate

	moduleSample_t *s = &song->samples[editor.currSample];

	// normalize and quantize to 8-bit

	const double dPeak = getDoublePeak(dPat2SmpBuf, pat2SmpPos);

	double dAmp = INT8_MAX;
	if (dPeak > 0.0)
		dAmp = INT8_MAX / dPeak;

	int8_t *smpPtr = &song->sampleData[s->offset];
	for (int32_t i = 0; i < pat2SmpPos; i++)
	{
		double dSmp = dPat2SmpBuf[i] * dAmp;

		// faster than calling round()
		     if (dSmp < 0.0) dSmp -= 0.5;
		else if (dSmp > 0.0) dSmp += 0.5;

		int32_t smp = (int32_t)dSmp;
		assert(smp >= -128 && smp <= 127); // shouldn't happen according to dAmp
		smpPtr[i] = (int8_t)smp;
	}

	free(dPat2SmpBuf);

	int32_t newSampleLength = (pat2SmpPos + 1) & ~1;
	if (newSampleLength > config.maxSampleLength)
		newSampleLength = config.maxSampleLength;
	
	// clear the rest of the sample (if not full)
	if (newSampleLength < config.maxSampleLength)
		memset(&song->sampleData[s->offset+newSampleLength], 0, config.maxSampleLength - newSampleLength);

	// set sample attributes

	s->length = newSampleLength;
	s->volume = 64;
	s->fineTune = pat2SmpFinetune;
	s->loopStart = 0;
	s->loopLength = 2;

	// set sample name

	const int32_t note = pat2SmpNote % 12;
	const int32_t octave = (pat2SmpNote / 12) + 1;

	if (pat2SmpFinetune == 0)
		sprintf(s->text, "pat2smp(%s%d ftune: 0)", noteStr[note], octave);
	else if (pat2SmpFinetune < 8)
		sprintf(s->text, "pat2smp(%s%d ftune:+%d)", noteStr[note], octave, pat2SmpFinetune);
	else
		sprintf(s->text, "pat2smp(%s%d ftune:-%d)", noteStr[note], octave, (pat2SmpFinetune^7)-7);

	fixSampleBeep(s);
	updateCurrSample();

	editor.samplePos = 0; // reset Edit Op. sample position
	updateWindowTitle(MOD_IS_MODIFIED);
}
