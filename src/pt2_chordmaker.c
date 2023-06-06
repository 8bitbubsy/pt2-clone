// for finding memory leaks in debug mode with Visual Studio 
#if defined _DEBUG && defined _MSC_VER
#include <crtdbg.h>
#endif

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <math.h>
#include "pt2_config.h"
#include "pt2_textout.h"
#include "pt2_visuals.h"
#include "pt2_helpers.h"
#include "pt2_tables.h"
#include "pt2_sampler.h"
#include "pt2_audio.h"
#include "pt2_blep.h"
#include "pt2_downsample2x.h"
#include "pt2_replayer.h"

#define MAX_NOTES 4

typedef struct sampleMixer_t
{
	bool active;
	int32_t length, pos;
	double dDelta, dPhase, dLastPhase;
} sampleMixer_t;

static void sortNotes(void)
{
	for (int32_t i = 0; i < 3; i++)
	{
		if (editor.note2 == 36)
		{
			editor.note2 = editor.note3;
			editor.note3 = editor.note4;
			editor.note4 = 36;
		}
	}

	for (int32_t i = 0; i < 3; i++)
	{
		if (editor.note3 == 36)
		{
			editor.note3 = editor.note4;
			editor.note4 = 36;
		}
	}
}

static void removeDuplicateNotes(void)
{
	if (editor.note4 == editor.note3) editor.note4 = 36;
	if (editor.note4 == editor.note2) editor.note4 = 36;
	if (editor.note3 == editor.note2) editor.note3 = 36;
}

static void setupMixVoice(sampleMixer_t *v, int32_t length, double dDelta)
{
	v->dDelta = dDelta;
	v->length = (int32_t)ceil(length / dDelta);

	if (v->length > 0)
		v->active = true;
}

// this has 2x oversampling for BLEP to function properly with all pitches
void mixChordSample(void)
{
	char smpText[22+1];
	int32_t i;
	sampleMixer_t mixCh[MAX_NOTES];
	blep_t bleps[MAX_NOTES];

	if (editor.sampleZero)
	{
		statusNotSampleZero();
		return;
	}

	if (editor.note1 == 36)
	{
		displayErrorMsg("NO BASENOTE!");
		return;
	}

	moduleSample_t *s = &song->samples[editor.currSample];
	if (s->length == 0)
	{
		statusSampleIsEmpty();
		return;
	}

	// check if all notes are the same (illegal)
	bool sameNotes = true;
	if (editor.note2 != 36 && editor.note2 != editor.note1) sameNotes = false; else editor.note2 = 36;
	if (editor.note3 != 36 && editor.note3 != editor.note1) sameNotes = false; else editor.note3 = 36;
	if (editor.note4 != 36 && editor.note4 != editor.note1) sameNotes = false; else editor.note4 = 36;

	if (sameNotes)
	{
		displayErrorMsg("ONLY ONE NOTE!");
		return;
	}

	sortNotes();
	removeDuplicateNotes();

	ui.updateChordNote1Text = true;
	ui.updateChordNote2Text = true;
	ui.updateChordNote3Text = true;
	ui.updateChordNote4Text = true;

	// setup some variables
	int32_t smpLoopStart = s->loopStart;
	int32_t smpLoopLength = s->loopLength;
	bool smpLoopFlag = (smpLoopStart + smpLoopLength) > 2;
	int32_t smpEnd = smpLoopFlag ? (smpLoopStart + smpLoopLength) : s->length;
	int8_t *smpData = &song->sampleData[s->offset];

	if (editor.newOldFlag == 0)
	{
		// find a free sample slot for the new sample

		for (i = 0; i < MOD_SAMPLES; i++)
		{
			if (song->samples[i].length == 0)
				break;
		}

		if (i == MOD_SAMPLES)
		{
			displayErrorMsg("NO EMPTY SAMPLE!");
			return;
		}

		uint8_t smpFinetune = s->fineTune;
		int8_t smpVolume = s->volume;
		memcpy(smpText, s->text, sizeof (smpText));

		s = &song->samples[i];
		s->fineTune = smpFinetune;
		s->volume = smpVolume;

		memcpy(s->text, smpText, sizeof (smpText));
		editor.currSample = (int8_t)i;
	}

	double *dMixData = (double *)calloc(config.maxSampleLength*2, sizeof (double));
	if (dMixData == NULL)
	{
		statusOutOfMemory();
		return;
	}

	s->length = smpLoopFlag ? config.maxSampleLength : editor.chordLength; // if sample loops, set max length
	s->loopLength = 2;
	s->loopStart = 0;
	s->text[21] = '!'; // chord sample indicator
	s->text[22] = '\0';

	memset(mixCh, 0, sizeof (mixCh)); // also clears position and frac

	// setup mixing lengths and deltas

	uint8_t finetune = s->fineTune & 0xF;
	const double dOutputHz = ((double)PAULA_PAL_CLK / periodTable[24]) * 2.0;

	const double dClk = PAULA_PAL_CLK / dOutputHz;
	if (editor.note1 < 36) setupMixVoice(&mixCh[0], smpEnd, dClk / periodTable[(finetune * 37) + editor.note1]);
	if (editor.note2 < 36) setupMixVoice(&mixCh[1], smpEnd, dClk / periodTable[(finetune * 37) + editor.note2]);
	if (editor.note3 < 36) setupMixVoice(&mixCh[2], smpEnd, dClk / periodTable[(finetune * 37) + editor.note3]);
	if (editor.note4 < 36) setupMixVoice(&mixCh[3], smpEnd, dClk / periodTable[(finetune * 37) + editor.note4]);

	// start mixing
	memset(bleps, 0, sizeof (bleps));
	turnOffVoices();

	sampleMixer_t *v = mixCh;
	blep_t *bSmp = bleps;

	for (i = 0; i < MAX_NOTES; i++, v++, bSmp++)
	{
		if (!v->active || v->dDelta == 0.0)
			continue;

		for (int32_t j = 0; j < config.maxSampleLength*2; j++)
		{
			double dSmp = smpData[v->pos] * (1.0 / 128.0);

			if (dSmp != bSmp->dLastValue)
			{
				if (v->dDelta > v->dLastPhase)
					blepAdd(bSmp, v->dLastPhase / v->dDelta, bSmp->dLastValue - dSmp);

				bSmp->dLastValue = dSmp;
			}

			if (bSmp->samplesLeft > 0) dSmp = blepRun(bSmp, dSmp);

			dMixData[j] += dSmp;

			v->dPhase += v->dDelta;
			if (v->dPhase >= 1.0)
			{
				v->dPhase -= 1.0;
				v->dLastPhase = v->dPhase;

				v->pos++;
				if (v->pos >= smpEnd)
				{
					if (smpLoopFlag)
					{
						do
						{
							v->pos -= smpLoopLength;
						}
						while (v->pos >= smpEnd);
					}
					else
					{
						break;
					}
				}
			}
		}
	}

	// downsample oversampled buffer, normalize and quantize to 8-bit

	downsample2xDouble(dMixData, s->length * 2);

	double dAmp = 1.0;
	const double dPeak = getDoublePeak(dMixData, s->length);
	if (dPeak > 0.0)
		dAmp = INT8_MAX / dPeak;

	int8_t *smpPtr = &song->sampleData[s->offset];
	for (i = 0; i < s->length; i++)
	{
		const int32_t smp = (const int32_t)round(dMixData[i] * dAmp);
		assert(smp >= -128 && smp <= 127); // shouldn't happen according to dAmp (but just in case)
		smpPtr[i] = (int8_t)smp;
	}

	free(dMixData);

	// clear unused sample data (if sample is not full already)
	if (s->length < config.maxSampleLength)
		memset(&song->sampleData[s->offset + s->length], 0, config.maxSampleLength - s->length);

	// we're done

	editor.samplePos = 0;
	fixSampleBeep(s);
	updateCurrSample();

	updateWindowTitle(MOD_IS_MODIFIED);
}

static void backupChord(void)
{
	editor.oldNote1 = editor.note1;
	editor.oldNote2 = editor.note2;
	editor.oldNote3 = editor.note3;
	editor.oldNote4 = editor.note4;
}

void recalcChordLength(void)
{
	int32_t note;

	moduleSample_t *s = &song->samples[editor.currSample];

	if (editor.chordLengthMin)
	{
		note = MAX(MAX((editor.note1 == 36) ? -1 : editor.note1,
		               (editor.note2 == 36) ? -1 : editor.note2),
		           MAX((editor.note3 == 36) ? -1 : editor.note3,
		               (editor.note4 == 36) ? -1 : editor.note4));
	}
	else
	{
		note = MIN(MIN(editor.note1, editor.note2), MIN(editor.note3, editor.note4));
	}

	if (note < 0 || note > 35)
	{
		editor.chordLength = 0;
	}
	else
	{
		int32_t len = (s->length * periodTable[(37 * s->fineTune) + note]) / periodTable[24];
		if (len > config.maxSampleLength)
			len = config.maxSampleLength;

		editor.chordLength = len & config.maxSampleLength;
	}

	if (ui.editOpScreenShown && ui.editOpScreen == 3)
		ui.updateChordLengthText = true;
}

void resetChord(void)
{
	editor.note1 = 36;
	editor.note2 = 36;
	editor.note3 = 36;
	editor.note4 = 36;

	editor.chordLengthMin = false;

	ui.updateChordNote1Text = true;
	ui.updateChordNote2Text = true;
	ui.updateChordNote3Text = true;
	ui.updateChordNote4Text = true;

	recalcChordLength();
}

void undoChord(void)
{
	editor.note1 = editor.oldNote1;
	editor.note2 = editor.oldNote2;
	editor.note3 = editor.oldNote3;
	editor.note4 = editor.oldNote4;

	ui.updateChordNote1Text = true;
	ui.updateChordNote2Text = true;
	ui.updateChordNote3Text = true;
	ui.updateChordNote4Text = true;

	recalcChordLength();
}

void toggleChordLength(void)
{
	if (song->samples[editor.currSample].loopLength == 2 && song->samples[editor.currSample].loopStart == 0)
	{
		editor.chordLengthMin = mouse.rightButtonPressed ? true : false;
		recalcChordLength();
	}
}

void setChordMajor(void)
{
	if (editor.note1 == 36)
	{
		displayErrorMsg("NO BASENOTE!");
		return;
	}

	backupChord();

	editor.note2 = editor.note1 + 4;
	editor.note3 = editor.note1 + 7;

	if (editor.note2 >= 36) editor.note2 -= 12;
	if (editor.note3 >= 36) editor.note3 -= 12;

	editor.note4 = 36;

	ui.updateChordNote2Text = true;
	ui.updateChordNote3Text = true;
	ui.updateChordNote4Text = true;

	recalcChordLength();
}

void setChordMinor(void)
{
	if (editor.note1 == 36)
	{
		displayErrorMsg("NO BASENOTE!");
		return;
	}

	backupChord();

	editor.note2 = editor.note1 + 3;
	editor.note3 = editor.note1 + 7;

	if (editor.note2 >= 36) editor.note2 -= 12;
	if (editor.note3 >= 36) editor.note3 -= 12;

	editor.note4 = 36;

	ui.updateChordNote2Text = true;
	ui.updateChordNote3Text = true;
	ui.updateChordNote4Text = true;

	recalcChordLength();
}

void setChordSus4(void)
{
	if (editor.note1 == 36)
	{
		displayErrorMsg("NO BASENOTE!");
		return;
	}

	backupChord();

	editor.note2 = editor.note1 + 5;
	editor.note3 = editor.note1 + 7;

	if (editor.note2 >= 36) editor.note2 -= 12;
	if (editor.note3 >= 36) editor.note3 -= 12;

	editor.note4 = 36;

	ui.updateChordNote2Text = true;
	ui.updateChordNote3Text = true;
	ui.updateChordNote4Text = true;

	recalcChordLength();
}

void setChordMajor6(void)
{
	if (editor.note1 == 36)
	{
		displayErrorMsg("NO BASENOTE!");
		return;
	}

	backupChord();

	editor.note2 = editor.note1 + 4;
	editor.note3 = editor.note1 + 7;
	editor.note4 = editor.note1 + 9;

	if (editor.note2 >= 36) editor.note2 -= 12;
	if (editor.note3 >= 36) editor.note3 -= 12;
	if (editor.note4 >= 36) editor.note4 -= 12;

	ui.updateChordNote2Text = true;
	ui.updateChordNote3Text = true;
	ui.updateChordNote4Text = true;

	recalcChordLength();
}

void setChordMinor6(void)
{
	if (editor.note1 == 36)
	{
		displayErrorMsg("NO BASENOTE!");
		return;
	}

	backupChord();

	editor.note2 = editor.note1 + 3;
	editor.note3 = editor.note1 + 7;
	editor.note4 = editor.note1 + 9;

	if (editor.note2 >= 36) editor.note2 -= 12;
	if (editor.note3 >= 36) editor.note3 -= 12;
	if (editor.note4 >= 36) editor.note4 -= 12;

	ui.updateChordNote2Text = true;
	ui.updateChordNote3Text = true;
	ui.updateChordNote4Text = true;

	recalcChordLength();
}

void setChordMajor7(void)
{
	if (editor.note1 == 36)
	{
		displayErrorMsg("NO BASENOTE!");
		return;
	}

	backupChord();

	editor.note2 = editor.note1 + 4;
	editor.note3 = editor.note1 + 7;
	editor.note4 = editor.note1 + 11;

	if (editor.note2 >= 36) editor.note2 -= 12;
	if (editor.note3 >= 36) editor.note3 -= 12;
	if (editor.note4 >= 36) editor.note4 -= 12;

	ui.updateChordNote2Text = true;
	ui.updateChordNote3Text = true;
	ui.updateChordNote4Text = true;

	recalcChordLength();
}

void setChordMinor7(void)
{
	if (editor.note1 == 36)
	{
		displayErrorMsg("NO BASENOTE!");
		return;
	}

	backupChord();

	editor.note2 = editor.note1 + 3;
	editor.note3 = editor.note1 + 7;
	editor.note4 = editor.note1 + 10;

	if (editor.note2 >= 36) editor.note2 -= 12;
	if (editor.note3 >= 36) editor.note3 -= 12;
	if (editor.note4 >= 36) editor.note4 -= 12;

	ui.updateChordNote2Text = true;
	ui.updateChordNote3Text = true;
	ui.updateChordNote4Text = true;

	recalcChordLength();
}

void selectChordNote1(void)
{
	if (mouse.rightButtonPressed)
	{
		editor.note1 = 36;
	}
	else
	{
		ui.changingChordNote = 1;
		setStatusMessage("SELECT NOTE", NO_CARRY);
		pointerSetMode(POINTER_MODE_MSG1, NO_CARRY);
	}

	ui.updateChordNote1Text = true;
}

void selectChordNote2(void)
{
	if (mouse.rightButtonPressed)
	{
		editor.note2 = 36;
	}
	else
	{
		ui.changingChordNote = 2;
		setStatusMessage("SELECT NOTE", NO_CARRY);
		pointerSetMode(POINTER_MODE_MSG1, NO_CARRY);
	}

	ui.updateChordNote2Text = true;
}

void selectChordNote3(void)
{
	if (mouse.rightButtonPressed)
	{
		editor.note3 = 36;
	}
	else
	{
		ui.changingChordNote = 3;
		setStatusMessage("SELECT NOTE", NO_CARRY);
		pointerSetMode(POINTER_MODE_MSG1, NO_CARRY);
	}

	ui.updateChordNote3Text = true;
}

void selectChordNote4(void)
{
	if (mouse.rightButtonPressed)
	{
		editor.note4 = 36;
	}
	else
	{
		ui.changingChordNote = 4;
		setStatusMessage("SELECT NOTE", NO_CARRY);
		pointerSetMode(POINTER_MODE_MSG1, NO_CARRY);
	}

	ui.updateChordNote4Text = true;
}
