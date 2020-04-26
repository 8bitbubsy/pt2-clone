// for finding memory leaks in debug mode with Visual Studio 
#if defined _DEBUG && defined _MSC_VER
#include <crtdbg.h>
#endif

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include "pt2_header.h"
#include "pt2_audio.h"
#include "pt2_mouse.h"
#include "pt2_textout.h"
#include "pt2_visuals.h"
#include "pt2_mod2wav.h"

#define TICKS_PER_RENDER_CHUNK 32

void storeTempVariables(void); // pt_modplayer.c
bool intMusic(void); // pt_modplayer.c
extern uint32_t samplesPerTick; // pt_audio.c

static volatile bool wavRenderingDone;
static int16_t *mod2WavBuffer;

static void calcMod2WavTotalRows(void);

static uint32_t getAudioFrame(int16_t *outStream)
{
	if (!intMusic())
		wavRenderingDone = true;

	outputAudio(outStream, samplesPerTick);
	return samplesPerTick;
}

static int32_t SDLCALL mod2WavThreadFunc(void *ptr)
{
	wavHeader_t wavHeader;

	FILE *f = (FILE *)ptr;
	assert(mod2WavBuffer != NULL && f != NULL);

	// skip wav header place, render data first
	fseek(f, sizeof (wavHeader_t), SEEK_SET);

	wavRenderingDone = false;

	uint8_t loopCounter = 8;
	uint32_t totalSampleCounter = 0;

	bool renderDone = false;
	while (!renderDone)
	{
		uint32_t samplesInChunk = 0;

		// render several ticks at once to prevent frequent disk I/O (can speed up the process)
		int16_t *ptr16 = mod2WavBuffer;
		for (uint32_t i = 0; i < TICKS_PER_RENDER_CHUNK; i++)
		{
			if (!editor.isWAVRendering || wavRenderingDone || editor.abortMod2Wav)
			{
				renderDone = true;
				break;
			}

			uint32_t tickSamples = getAudioFrame(ptr16) << 1; // *2 for stereo

			samplesInChunk += tickSamples;
			totalSampleCounter += tickSamples;

			// increase buffer pointer
			ptr16 += tickSamples;

			if (++loopCounter >= 8)
			{
				loopCounter = 0;
				editor.ui.updateMod2WavDialog = true;
			}
		}

		// write buffer to disk
		if (samplesInChunk > 0)
			fwrite(mod2WavBuffer, sizeof (int16_t), samplesInChunk, f);
	}

	free(mod2WavBuffer);

	if (totalSampleCounter & 1)
		fputc(0, f); // pad align byte

	uint32_t totalRiffChunkLen = (uint32_t)ftell(f) - 8;

	// go back and fill in WAV header
	rewind(f);

	wavHeader.chunkID = 0x46464952; // "RIFF"
	wavHeader.chunkSize = totalRiffChunkLen;
	wavHeader.format = 0x45564157; // "WAVE"
	wavHeader.subchunk1ID = 0x20746D66; // "fmt "
	wavHeader.subchunk1Size = 16;
	wavHeader.audioFormat = 1;
	wavHeader.numChannels = 2;
	wavHeader.sampleRate = MOD2WAV_FREQ;
	wavHeader.bitsPerSample = 16;
	wavHeader.byteRate = (wavHeader.sampleRate * wavHeader.numChannels * wavHeader.bitsPerSample) / 8;
	wavHeader.blockAlign = (wavHeader.numChannels * wavHeader.bitsPerSample) / 8;
	wavHeader.subchunk2ID = 0x61746164; // "data"
	wavHeader.subchunk2Size = totalSampleCounter * sizeof (int16_t);

	// write main header
	fwrite(&wavHeader, sizeof (wavHeader_t), 1, f);
	fclose(f);

	editor.ui.mod2WavFinished = true;
	editor.ui.updateMod2WavDialog = true;

	return true;
}

bool renderToWav(char *fileName, bool checkIfFileExist)
{
	FILE *fOut;
	struct stat statBuffer;

	if (checkIfFileExist)
	{
		if (stat(fileName, &statBuffer) == 0)
		{
			editor.ui.askScreenShown = true;
			editor.ui.askScreenType = ASK_MOD2WAV_OVERWRITE;

			pointerSetMode(POINTER_MODE_MSG1, NO_CARRY);
			setStatusMessage("OVERWRITE FILE?", NO_CARRY);

			renderAskDialog();

			return false;
		}
	}

	if (editor.ui.askScreenShown)
	{
		editor.ui.askScreenShown = false;
		editor.ui.answerNo = false;
		editor.ui.answerYes = false;
	}

	fOut = fopen(fileName, "wb");
	if (fOut == NULL)
	{
		displayErrorMsg("FILE I/O ERROR");
		return false;
	}

	const uint32_t maxSamplesToMix = TICKS_PER_RENDER_CHUNK * audio.bpmTabMod2Wav[32-32]; // BPM 32, stereo

	mod2WavBuffer = (int16_t *)malloc(maxSamplesToMix * (2 * sizeof (int16_t)));
	if (mod2WavBuffer == NULL)
	{
		statusOutOfMemory();
		return false;
	}

	storeTempVariables();
	calcMod2WavTotalRows();
	restartSong();

	editor.blockMarkFlag = false;

	pointerSetMode(POINTER_MODE_MSG2, NO_CARRY);
	setStatusMessage("RENDERING MOD...", NO_CARRY);

	editor.ui.disableVisualizer = true;
	editor.isWAVRendering = true;
	renderMOD2WAVDialog();

	editor.abortMod2Wav = false;

	modSetTempo(modEntry->currBPM); // update BPM with MOD2WAV audio output rate

	editor.mod2WavThread = SDL_CreateThread(mod2WavThreadFunc, NULL, fOut);
	if (editor.mod2WavThread != NULL)
	{
		SDL_DetachThread(editor.mod2WavThread);
	}
	else
	{
		free(mod2WavBuffer);

		editor.ui.disableVisualizer = false;
		editor.isWAVRendering = false;

		displayErrorMsg("THREAD ERROR");

		pointerSetMode(POINTER_MODE_IDLE, DO_CARRY);
		statusAllRight();

		return false;
	}

	return true;
}

// ONLY used for a visual percentage counter, so accuracy is not very important
static void calcMod2WavTotalRows(void)
{
	bool pBreakFlag, posJumpAssert, calcingRows;
	int8_t n_pattpos[AMIGA_VOICES], n_loopcount[AMIGA_VOICES];
	uint8_t modRow, pBreakPosition, ch, pos;
	int16_t modOrder;
	uint16_t modPattern;
	note_t *note;

	// for pattern loop
	memset(n_pattpos, 0, sizeof (n_pattpos));
	memset(n_loopcount, 0, sizeof (n_loopcount));

	modEntry->rowsCounter = 0;
	modEntry->rowsInTotal = 0;

	modRow = 0;
	modOrder = 0;
	modPattern = modEntry->head.order[0];
	pBreakPosition = 0;
	posJumpAssert = false;
	pBreakFlag = false;
	calcingRows = true;

	memset(editor.rowVisitTable, 0, MOD_ORDERS * MOD_ROWS);
	while (calcingRows)
	{
		editor.rowVisitTable[(modOrder * MOD_ROWS) + modRow] = true;

		for (ch = 0; ch < AMIGA_VOICES; ch++)
		{
			note = &modEntry->patterns[modPattern][(modRow * AMIGA_VOICES) + ch];
			if (note->command == 0x0B) // Bxx - Position Jump
			{
				modOrder = note->param - 1;
				pBreakPosition = 0;
				posJumpAssert = true;
			}
			else if (note->command == 0x0D) // Dxx - Pattern Break
			{
				pBreakPosition = (((note->param >> 4) * 10) + (note->param & 0x0F));
				if (pBreakPosition > 63)
					pBreakPosition = 0;

				posJumpAssert = true;
			}
			else if (note->command == 0x0F && note->param == 0) // F00 - Set Speed 0 (stop)
			{
				calcingRows = false;
				break;
			}
			else if (note->command == 0x0E && (note->param >> 4) == 0x06) // E6x - Pattern Loop
			{
				pos = note->param & 0x0F;
				if (pos == 0)
				{
					n_pattpos[ch] = modRow;
				}
				else if (n_loopcount[ch] == 0)
				{
					n_loopcount[ch] = pos;

					pBreakPosition = n_pattpos[ch];
					pBreakFlag = true;

					for (pos = pBreakPosition; pos <= modRow; pos++)
						editor.rowVisitTable[(modOrder * MOD_ROWS) + pos] = false;
				}
				else if (--n_loopcount[ch])
				{
					pBreakPosition = n_pattpos[ch];
					pBreakFlag = true;

					for (pos = pBreakPosition; pos <= modRow; pos++)
						editor.rowVisitTable[(modOrder * MOD_ROWS) + pos] = false;
				}
			}
		}

		modRow++;
		modEntry->rowsInTotal++;

		if (pBreakFlag)
		{
			modRow = pBreakPosition;
			pBreakPosition = 0;
			pBreakFlag = false;
		}

		if (modRow >= MOD_ROWS || posJumpAssert)
		{
			modRow = pBreakPosition;
			pBreakPosition = 0;
			posJumpAssert = false;

			modOrder = (modOrder + 1) & 0x7F;
			if (modOrder >= modEntry->head.orderCount)
			{
				modOrder = 0;
				calcingRows = false;
				break;
			}

			modPattern = modEntry->head.order[modOrder];
			if (modPattern > MAX_PATTERNS-1)
				modPattern = MAX_PATTERNS-1;
		}

		if (editor.rowVisitTable[(modOrder * MOD_ROWS) + modRow])
		{
			// row has been visited before, we're now done!
			calcingRows = false;
			break;
		}
	}
}
