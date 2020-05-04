// for finding memory leaks in debug mode with Visual Studio 
#if defined _DEBUG && defined _MSC_VER
#include <crtdbg.h>
#endif

#include <stdint.h>
#include <stdbool.h>
#include "pt2_header.h"
#include "pt2_helpers.h"
#include "pt2_visuals.h"
#include "pt2_mouse.h"
#include "pt2_audio.h"
#include "pt2_sampler.h"
#include "pt2_textout.h"

bool intMusic(void); // pt_modplayer.c
extern uint32_t samplesPerTick; // pt_audio.c
void storeTempVariables(void); // pt_modplayer.c

void doPat2Smp(void)
{
	moduleSample_t *s;

	ui.pat2SmpDialogShown = false;

	if (editor.sampleZero)
	{
		statusNotSampleZero();
		return;
	}

	editor.pat2SmpBuf = (int16_t *)malloc(MAX_SAMPLE_LEN * sizeof (int16_t));
	if (editor.pat2SmpBuf == NULL)
	{
		statusOutOfMemory();
		return;
	}

	int8_t oldRow = editor.songPlaying ? 0 : song->currRow;
	uint32_t oldSamplesPerTick = samplesPerTick;

	editor.isSMPRendering = true; // this must be set before restartSong()
	storeTempVariables();
	restartSong();
	song->row = oldRow;
	song->currRow = song->row;

	editor.blockMarkFlag = false;
	pointerSetMode(POINTER_MODE_MSG2, NO_CARRY);
	setStatusMessage("RENDERING...", NO_CARRY);
	modSetTempo(song->currBPM);
	editor.pat2SmpPos = 0;

	editor.smpRenderingDone = false;
	while (!editor.smpRenderingDone)
	{
		if (!intMusic())
			editor.smpRenderingDone = true;

		outputAudio(NULL, samplesPerTick);
	}
	editor.isSMPRendering = false;
	resetSong();

	// set back old row and samplesPerTick
	song->row = oldRow;
	song->currRow = song->row;
	mixerSetSamplesPerTick(oldSamplesPerTick);

	normalize16bitSigned(editor.pat2SmpBuf, MIN(editor.pat2SmpPos, MAX_SAMPLE_LEN));

	s = &song->samples[editor.currSample];

	// quantize to 8-bit
	for (int32_t i = 0; i < editor.pat2SmpPos; i++)
		song->sampleData[s->offset+i] = editor.pat2SmpBuf[i] >> 8;

	// clear the rest of the sample
	if (editor.pat2SmpPos < MAX_SAMPLE_LEN)
		memset(&song->sampleData[s->offset+editor.pat2SmpPos], 0, MAX_SAMPLE_LEN - editor.pat2SmpPos);

	free(editor.pat2SmpBuf);

	memset(s->text, 0, sizeof (s->text));
	if (editor.pat2SmpHQ)
	{
		strcpy(s->text, "pat2smp (a-3 tune:+5)");
		s->fineTune = 5;
	}
	else
	{
		strcpy(s->text, "pat2smp (f-3 tune:+1)");
		s->fineTune = 1;
	}

	s->length = (uint16_t)editor.pat2SmpPos;
	s->volume = 64;
	s->loopStart = 0;
	s->loopLength = 2;

	editor.samplePos = 0;
	fixSampleBeep(s);
	updateCurrSample();

	pointerSetMode(POINTER_MODE_IDLE, DO_CARRY);
	displayMsg("ROWS RENDERED!");
	setMsgPointer();
}
