// for finding memory leaks in debug mode with Visual Studio 
#if defined _DEBUG && defined _MSC_VER
#include <crtdbg.h>
#endif

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <sys/stat.h> // stat()
#include "pt2_header.h"
#include "pt2_audio.h"
#include "pt2_mouse.h"
#include "pt2_textout.h"
#include "pt2_visuals.h"
#include "pt2_mod2wav.h"
#include "pt2_structs.h"
#include "pt2_downsample2x.h"

#define TICKS_PER_RENDER_CHUNK 64

// pt2_replayer.c
void storeTempVariables(void);
bool intMusic(void);
// ---------------------

static int16_t *mod2WavBuffer;

static void calcMod2WavTotalRows(void);

static int32_t SDLCALL mod2WavThreadFunc(void *ptr)
{
	wavHeader_t wavHeader;

	FILE *f = (FILE *)ptr;
	assert(mod2WavBuffer != NULL && f != NULL);

	// skip wav header place, render data first
	fseek(f, sizeof (wavHeader_t), SEEK_SET);

	uint32_t sampleCounter = 0;
	uint8_t tickCounter = 8;
	int64_t tickSampleCounter64 = 0;

	clearMixerDownsamplerStates();

	bool renderDone = false;
	while (!renderDone)
	{
		uint32_t samplesInChunk = 0;

		// render several ticks at once to prevent frequent disk I/O (can speed up the process)
		int16_t *ptr16 = mod2WavBuffer;
		for (uint32_t i = 0; i < TICKS_PER_RENDER_CHUNK; i++)
		{
			if (!editor.isWAVRendering || renderDone || editor.abortMod2Wav || !editor.songPlaying)
			{
				renderDone = true;
				break;
			}

			if (tickSampleCounter64 <= 0) // new replayer tick
			{
				if (!intMusic())
					renderDone = true; // this tick is the last tick

				tickSampleCounter64 += audio.samplesPerTick64;
			}

			int32_t remainingTick = (tickSampleCounter64 + UINT32_MAX) >> 32; // ceil (rounded upwards)

			outputAudio(ptr16, remainingTick);
			tickSampleCounter64 -= (int64_t)remainingTick << 32;

			remainingTick *= 2; // stereo
			samplesInChunk += remainingTick;
			sampleCounter += remainingTick;

			ptr16 += remainingTick;

			if (++tickCounter >= 4)
			{
				tickCounter = 0;
				ui.updateMod2WavDialog = true;
			}
		}

		// write buffer to disk
		if (samplesInChunk > 0)
			fwrite(mod2WavBuffer, sizeof (int16_t), samplesInChunk, f);
	}

	free(mod2WavBuffer);

	if (sampleCounter & 1)
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
	wavHeader.sampleRate = audio.outputRate;
	wavHeader.bitsPerSample = 16;
	wavHeader.byteRate = (wavHeader.sampleRate * wavHeader.numChannels * wavHeader.bitsPerSample) / 8;
	wavHeader.blockAlign = (wavHeader.numChannels * wavHeader.bitsPerSample) / 8;
	wavHeader.subchunk2ID = 0x61746164; // "data"
	wavHeader.subchunk2Size = sampleCounter * sizeof (int16_t);

	// write main header
	fwrite(&wavHeader, sizeof (wavHeader_t), 1, f);
	fclose(f);

	clearMixerDownsamplerStates();

	ui.mod2WavFinished = true;
	ui.updateMod2WavDialog = true;

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
			ui.askScreenShown = true;
			ui.askScreenType = ASK_MOD2WAV_OVERWRITE;

			pointerSetMode(POINTER_MODE_MSG1, NO_CARRY);
			setStatusMessage("OVERWRITE FILE?", NO_CARRY);

			renderAskDialog();

			return false;
		}
	}

	if (ui.askScreenShown)
	{
		ui.askScreenShown = false;
		ui.answerNo = false;
		ui.answerYes = false;
	}

	fOut = fopen(fileName, "wb");
	if (fOut == NULL)
	{
		displayErrorMsg("FILE I/O ERROR");
		return false;
	}

	const int32_t lowestBPM = 32;
	const int64_t maxSamplesToMix64 = audio.bpmTable[lowestBPM-32];
	const int32_t maxSamplesToMix = ((TICKS_PER_RENDER_CHUNK * maxSamplesToMix64) + (1LL << 31)) >> 32; // ceil (rounded upwards)

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

	ui.disableVisualizer = true;
	editor.isWAVRendering = true;
	renderMOD2WAVDialog();

	editor.abortMod2Wav = false;

	modSetTempo(song->currBPM, true); // update BPM with MOD2WAV audio output rate

	if (headless)
	{
		mod2WavThreadFunc(fOut);
	}
	else
	{
		editor.mod2WavThread = SDL_CreateThread(mod2WavThreadFunc, NULL, fOut);
		if (editor.mod2WavThread == NULL)
		{
			free(mod2WavBuffer);

			ui.disableVisualizer = false;
			editor.isWAVRendering = false;

			displayErrorMsg("THREAD ERROR");

			pointerSetMode(POINTER_MODE_IDLE, DO_CARRY);
			statusAllRight();

			return false;
		}

		SDL_DetachThread(editor.mod2WavThread);
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

	song->rowsCounter = 0;
	song->rowsInTotal = 0;

	modRow = 0;
	modOrder = 0;
	modPattern = song->header.order[0];
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
			note = &song->patterns[modPattern][(modRow * AMIGA_VOICES) + ch];
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
		song->rowsInTotal++;

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
			if (modOrder >= song->header.numOrders)
			{
				modOrder = 0;
				calcingRows = false;
				break;
			}

			modPattern = song->header.order[modOrder];
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
