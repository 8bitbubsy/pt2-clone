// for finding memory leaks in debug mode with Visual Studio 
#if defined _DEBUG && defined _MSC_VER
#include <crtdbg.h>
#endif

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include "pt2_header.h"
#include "pt2_mouse.h"
#include "pt2_textout.h"
#include "pt2_visuals.h"
#include "pt2_structs.h"
#include "pt2_helpers.h"
#include "pt2_tables.h"
#include "pt2_sampler.h"
#include "pt2_audio.h"
#include "pt2_blep.h"
#include "pt2_downsample2x.h"

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
	bool smpLoopFlag;
	char smpText[22+1];
	int8_t *smpData, sameNotes, smpVolume;
	uint8_t smpFinetune, finetune;
	int32_t i, smpLoopStart, smpLoopLength, smpEnd;
	sampleMixer_t mixCh[MAX_NOTES];
	moduleSample_t *s;
	blep_t blep[MAX_NOTES];

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

	s = &song->samples[editor.currSample];
	if (s->length == 0)
	{
		statusSampleIsEmpty();
		return;
	}

	// check if all notes are the same (illegal)
	sameNotes = true;
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

	ui.updateNote1Text = true;
	ui.updateNote2Text = true;
	ui.updateNote3Text = true;
	ui.updateNote4Text = true;

	// setup some variables
	smpLoopStart = s->loopStart;
	smpLoopLength = s->loopLength;
	smpLoopFlag = (smpLoopStart + smpLoopLength) > 2;
	smpEnd = smpLoopFlag ? (smpLoopStart + smpLoopLength) : s->length;
	smpData = &song->sampleData[s->offset];

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

		smpFinetune = s->fineTune;
		smpVolume = s->volume;
		memcpy(smpText, s->text, sizeof (smpText));

		s = &song->samples[i];
		s->fineTune = smpFinetune;
		s->volume = smpVolume;

		memcpy(s->text, smpText, sizeof (smpText));
		editor.currSample = (int8_t)i;
	}

	double *dMixData = (double *)calloc(MAX_SAMPLE_LEN*2, sizeof (double));
	if (dMixData == NULL)
	{
		statusOutOfMemory();
		return;
	}

	s->length = smpLoopFlag ? MAX_SAMPLE_LEN : editor.chordLength; // if sample loops, set max length
	s->loopLength = 2;
	s->loopStart = 0;
	s->text[21] = '!'; // chord sample indicator
	s->text[22] = '\0';

	memset(mixCh, 0, sizeof (mixCh)); // also clears position and frac

	// setup mixing lengths and deltas

	finetune = s->fineTune & 0xF;
	const double dOutputHz = ((double)PAULA_PAL_CLK / periodTable[24]) * 2.0;

	const double dClk = PAULA_PAL_CLK / dOutputHz;
	if (editor.note1 < 36) setupMixVoice(&mixCh[0], smpEnd, dClk / periodTable[(finetune * 37) + editor.note1]);
	if (editor.note2 < 36) setupMixVoice(&mixCh[1], smpEnd, dClk / periodTable[(finetune * 37) + editor.note2]);
	if (editor.note3 < 36) setupMixVoice(&mixCh[2], smpEnd, dClk / periodTable[(finetune * 37) + editor.note3]);
	if (editor.note4 < 36) setupMixVoice(&mixCh[3], smpEnd, dClk / periodTable[(finetune * 37) + editor.note4]);

	// start mixing
	memset(blep, 0, sizeof (blep));
	turnOffVoices();

	sampleMixer_t *v = mixCh;
	blep_t *bSmp = blep;

	for (i = 0; i < MAX_NOTES; i++, v++, bSmp++)
	{
		if (!v->active || v->dDelta == 0.0)
			continue;

		for (int32_t j = 0; j < MAX_SAMPLE_LEN*2; j++)
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

				if (++v->pos >= smpEnd)
				{
					if (smpLoopFlag)
					{
						do
						{
							v->pos -= smpLoopLength;
						}
						while (v->pos >= smpEnd);
					}
					else break; // we should insert an ending blep here, but I lost that code ages ago...
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
	if (s->length < MAX_SAMPLE_LEN)
		memset(&song->sampleData[s->offset + s->length], 0, MAX_SAMPLE_LEN - s->length);

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
	int8_t note;
	int32_t len;
	moduleSample_t *s;

	s = &song->samples[editor.currSample];

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
		len = (s->length * periodTable[(37 * s->fineTune) + note]) / periodTable[24];
		if (len > MAX_SAMPLE_LEN)
			len = MAX_SAMPLE_LEN;

		editor.chordLength = len & 0xFFFE;
	}

	if (ui.editOpScreenShown && ui.editOpScreen == 3)
		ui.updateLengthText = true;
}

void resetChord(void)
{
	editor.note1 = 36;
	editor.note2 = 36;
	editor.note3 = 36;
	editor.note4 = 36;

	editor.chordLengthMin = false;

	ui.updateNote1Text = true;
	ui.updateNote2Text = true;
	ui.updateNote3Text = true;
	ui.updateNote4Text = true;

	recalcChordLength();
}

void undoChord(void)
{
	editor.note1 = editor.oldNote1;
	editor.note2 = editor.oldNote2;
	editor.note3 = editor.oldNote3;
	editor.note4 = editor.oldNote4;

	ui.updateNote1Text = true;
	ui.updateNote2Text = true;
	ui.updateNote3Text = true;
	ui.updateNote4Text = true;

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

	ui.updateNote2Text = true;
	ui.updateNote3Text = true;
	ui.updateNote4Text = true;

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

	ui.updateNote2Text = true;
	ui.updateNote3Text = true;
	ui.updateNote4Text = true;

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

	ui.updateNote2Text = true;
	ui.updateNote3Text = true;
	ui.updateNote4Text = true;

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

	ui.updateNote2Text = true;
	ui.updateNote3Text = true;
	ui.updateNote4Text = true;

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

	ui.updateNote2Text = true;
	ui.updateNote3Text = true;
	ui.updateNote4Text = true;

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

	ui.updateNote2Text = true;
	ui.updateNote3Text = true;
	ui.updateNote4Text = true;

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

	ui.updateNote2Text = true;
	ui.updateNote3Text = true;
	ui.updateNote4Text = true;

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

	ui.updateNote1Text = true;
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

	ui.updateNote2Text = true;
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

	ui.updateNote3Text = true;
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

	ui.updateNote4Text = true;
}

void makeChord(void)
{
	ui.askScreenShown = true;
	ui.askScreenType = ASK_MAKE_CHORD;
	pointerSetMode(POINTER_MODE_MSG1, NO_CARRY);
	setStatusMessage("MAKE CHORD?", NO_CARRY);
	renderAskDialog();
}
