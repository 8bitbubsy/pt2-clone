// for finding memory leaks in debug mode with Visual Studio
#if defined _DEBUG && defined _MSC_VER
#include <crtdbg.h>
#endif

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <sys/stat.h> // stat()
#ifndef _WIN32
#include <unistd.h> // chdir()
#endif
#include "pt2_audio.h"
#include "pt2_mouse.h"
#include "pt2_textout.h"
#include "pt2_visuals.h"
#include "pt2_mod2wav.h"
#include "pt2_downsample2x.h"
#include "pt2_config.h"
#include "pt2_askbox.h"
#include "pt2_replayer.h"

#define FADEOUT_CHUNK_SAMPLES 16384
#define TICKS_PER_RENDER_CHUNK 64

static int16_t *mod2WavBuffer, fadeOutBuffer[FADEOUT_CHUNK_SAMPLES * 2];
static char lastFilename[PATH_MAX + 1];

static void calcMod2WavTotalRows(void);

void mod2WavDrawFadeoutToggle(void)
{
	fillRect(143, 50, FONT_CHAR_W, FONT_CHAR_H, video.palette[PAL_GENBKG]);
	if (editor.mod2WavFadeOut)
		charOut(143, 50, 'X', video.palette[PAL_GENTXT]);
}

void mod2WavDrawFadeoutSeconds(void)
{
	fillRect(259, 61, FONT_CHAR_W*2, FONT_CHAR_H+1, video.palette[PAL_GENBKG]);
	charOut(259, 62, '0' + ((editor.mod2WavFadeOutSeconds / 10) % 10), video.palette[PAL_GENTXT]);
	charOut(267, 62, '0' + ( editor.mod2WavFadeOutSeconds       % 10), video.palette[PAL_GENTXT]);
}

void mod2WavDrawLoopCount(void)
{
	fillRect(259, 72, FONT_CHAR_W*2, FONT_CHAR_H+1, video.palette[PAL_GENBKG]);
	charOut(259, 73, '0' + ((editor.mod2WavNumLoops / 10)  % 10), video.palette[PAL_GENTXT]);
	charOut(267, 73, '0' + ( editor.mod2WavNumLoops        % 10), video.palette[PAL_GENTXT]);
}

void toggleMod2WavFadeout(void)
{
	editor.mod2WavFadeOut ^= 1;
	mod2WavDrawFadeoutToggle();
}

void mod2WavFadeoutUp(void)
{
	if (editor.mod2WavFadeOutSeconds < 60)
	{
		editor.mod2WavFadeOutSeconds++;
		mod2WavDrawFadeoutSeconds();
	}
}

void mod2WavFadeoutDown(void)
{
	if (editor.mod2WavFadeOutSeconds > 1)
	{
		editor.mod2WavFadeOutSeconds--;
		mod2WavDrawFadeoutSeconds();
	}
}

void mod2WavLoopCountUp(void)
{
	if (editor.mod2WavNumLoops < 50)
	{
		editor.mod2WavNumLoops++;
		mod2WavDrawLoopCount();
	}
}

void mod2WavLoopCountDown(void)
{
	if (editor.mod2WavNumLoops > 0)
	{
		editor.mod2WavNumLoops--;
		mod2WavDrawLoopCount();
	}
}

void drawMod2WavProgressDialog(void)
{
	drawFramework3(120, 44, 200, 55);
	textOut2(150, 53, "- RENDERING MODULE -");

	const int32_t buttonW = (MOD2WAV_CANCEL_BTN_X2 - MOD2WAV_CANCEL_BTN_X1)+1;
	const int32_t buttonH = (MOD2WAV_CANCEL_BTN_Y2 - MOD2WAV_CANCEL_BTN_Y1)+1;
	drawButton1(MOD2WAV_CANCEL_BTN_X1, MOD2WAV_CANCEL_BTN_Y1, buttonW, buttonH, "CANCEL");
}

static void showMod2WavProgress(void)
{
	char percText[16];

	if (song->rowsInTotal == 0)
		return;

	// render progress bar

	int32_t percent = (song->rowsCounter * 100) / song->rowsInTotal;
	if (percent > 100)
		percent = 100;

	const int32_t x = 130;
	const int32_t y = 66;
	const int32_t w = 180;
	const int32_t h = 11;

	// foreground (progress)
	const int32_t progressBarWidth = (percent * w) / 100;
	if (progressBarWidth > 0)
		fillRect(x, y, progressBarWidth, h, video.palette[PAL_GENBKG2]); // foreground (progress)

	// background
	int32_t bgWidth = w - progressBarWidth;
	if (bgWidth > 0)
		fillRect(x+progressBarWidth, y, bgWidth, h, video.palette[PAL_BORDER]);

	// draw percentage text
	sprintf(percText, "%d%%", percent);
	const int32_t percTextW = (int32_t)strlen(percText) * (FONT_CHAR_W-1);
	textOutTight(x + ((w - percTextW) / 2), y + ((h - FONT_CHAR_H) / 2), percText, video.palette[PAL_GENTXT]);
}

static void resetAudio(void)
{
	audio.oversamplingFlag = (audio.outputRate < 96000);
	const int32_t paulaMixFrequency = audio.oversamplingFlag ? audio.outputRate*2 : audio.outputRate;
	paulaSetup(paulaMixFrequency, audio.amigaModel);
	generateBpmTable(audio.outputRate, editor.timingMode == TEMPO_MODE_VBLANK);
	clearMixerDownsamplerStates();
	modSetTempo(song->currBPM, true); // update BPM (samples per tick) with the tracker's audio frequency
}

static void handleMod2WavEnd(void)
{
	pointerSetMode(POINTER_MODE_IDLE, DO_CARRY);

	if (editor.abortMod2Wav)
	{
		displayErrorMsg("MOD2WAV ABORTED!");
	}
	else
	{
		displayMsg("MOD RENDERED!");
		setMsgPointer();
	}

	removeAskBox();
	resetAudio();

	editor.mod2WavOngoing = false; // must be set before calling resetSong() !
	resetSong();
}

void updateMod2WavDialog(void)
{
	if (ui.updateMod2WavDialog)
	{
		ui.updateMod2WavDialog = false;

		if (editor.mod2WavOngoing)
		{
			if (ui.mod2WavFinished)
			{
				ui.mod2WavFinished = false;
				handleMod2WavEnd();
			}
			else
			{
				showMod2WavProgress();
			}
		}
	}
}

static int32_t SDLCALL mod2WavThreadFunc(void *ptr)
{
	wavHeader_t wavHeader;

	FILE *f = (FILE *)ptr;
	assert(mod2WavBuffer != NULL && f != NULL);

	// skip wav header place, render data first
	fseek(f, sizeof (wavHeader_t), SEEK_SET);

	uint32_t sampleCounter = 0;
	uint64_t samplesToMixFrac = 0;
	int8_t numLoops = editor.mod2WavNumLoops;

	bool renderDone = false;
	while (!renderDone)
	{
		uint32_t samplesInChunk = 0;

		// render several ticks at once to prevent frequent disk I/O (speeds up the process)
		int16_t *ptr16 = mod2WavBuffer;
		for (uint32_t i = 0; i < TICKS_PER_RENDER_CHUNK; i++)
		{
			if (!editor.mod2WavOngoing || renderDone || editor.abortMod2Wav)
			{
				renderDone = true;
				break;
			}

			/* PT replayer ticker (also sets audio.samplesPerTickInt and audio.samplesPerTickFrac).
			** Returns false on end of song.
			*/
			if (!intMusic())
			{
				if (--numLoops < 0)
				{
					renderDone = true; // this tick is the last tick
				}
				else
				{
					// clear the "last visisted rows" table and let the song continue playing (loop)
					memset(editor.rowVisitTable, 0, 128 * MOD_ROWS);
				}
			}

			uint32_t samplesToMix = audio.samplesPerTickInt;

			samplesToMixFrac += audio.samplesPerTickFrac;
			if (samplesToMixFrac >= BPM_FRAC_SCALE)
			{
				samplesToMixFrac &= BPM_FRAC_MASK;
				samplesToMix++;
			}

			outputAudio(ptr16, samplesToMix);
			ptr16 += samplesToMix * 2; // *2 for stereo

			samplesInChunk += samplesToMix;
			sampleCounter += samplesToMix;

			ui.updateMod2WavDialog = true;
		}

		// write buffer to disk
		if (samplesInChunk > 0)
			fwrite(mod2WavBuffer, sizeof (int16_t), samplesInChunk * 2, f);
	}

	ui.updateMod2WavDialog = true;

	uint32_t endOfDataOffset = ftell(f);

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
	wavHeader.sampleRate = config.mod2WavOutputFreq;
	wavHeader.bitsPerSample = 16;
	wavHeader.byteRate = (wavHeader.sampleRate * wavHeader.numChannels * wavHeader.bitsPerSample) / 8;
	wavHeader.blockAlign = (wavHeader.numChannels * wavHeader.bitsPerSample) / 8;
	wavHeader.subchunk2ID = 0x61746164; // "data"
	wavHeader.subchunk2Size = sampleCounter * sizeof (int16_t) * 2;

	// write main header
	fwrite(&wavHeader, sizeof (wavHeader_t), 1, f);
	fclose(f);

	// apply fadeout (if enabled)
	if (editor.mod2WavFadeOut)
	{
		uint32_t numFadeOutSamples = config.mod2WavOutputFreq * editor.mod2WavFadeOutSeconds;
		if (numFadeOutSamples > sampleCounter)
			numFadeOutSamples = sampleCounter;

		f = fopen(lastFilename, "r+b");

		const double dFadeOutDelta = 1.0 / numFadeOutSamples;
		double dFadeOutVal = 1.0;

		fseek(f, endOfDataOffset - (numFadeOutSamples * sizeof (int16_t) * 2), SEEK_SET);

		uint32_t samplesLeft = numFadeOutSamples;
		while (samplesLeft > 0)
		{
			uint32_t samplesTodo = FADEOUT_CHUNK_SAMPLES;
			if (samplesTodo > samplesLeft)
				samplesTodo = samplesLeft;

			fread(fadeOutBuffer, sizeof (int16_t), samplesTodo * 2, f);
			fseek(f, 0 - (samplesTodo * sizeof (int16_t) * 2), SEEK_CUR);

			// apply fadeout
			for (uint32_t i = 0; i < samplesTodo; i++)
			{
				fadeOutBuffer[(i*2)+0] = (int16_t)(fadeOutBuffer[(i*2)+0] * dFadeOutVal); // L
				fadeOutBuffer[(i*2)+1] = (int16_t)(fadeOutBuffer[(i*2)+1] * dFadeOutVal); // R
				dFadeOutVal -= dFadeOutDelta;
			}

			fwrite(fadeOutBuffer, sizeof (int16_t), samplesTodo * 2, f);

			samplesLeft -= samplesTodo;
		}

		fclose(f);
	}

	ui.mod2WavFinished = true;
	ui.updateMod2WavDialog = true;

	return true;
}

static void assureModulesDir(void)
{
	// if we're in samples mode in Disk Op., set dir to current modules dir
	if (diskop.mode == DISKOP_MODE_SMP && editor.modulesPathU != NULL)
		UNICHAR_CHDIR(editor.modulesPathU);
}

static void setBackDirIfNeeded(void)
{
	// if we're in samples mode in Disk Op., set dir back to current samples dir
	if (diskop.mode == DISKOP_MODE_SMP && editor.samplesPathU != NULL)
		UNICHAR_CHDIR(editor.samplesPathU);
}

bool mod2WavRender(char *filename)
{
	struct stat statBuffer;

	lastFilename[0] = '\0'; // for rendering-thread

	assureModulesDir();

	if (stat(filename, &statBuffer) == 0)
	{
		if (!askBox(ASKBOX_YES_NO, "OVERWRITE FILE?"))
		{
			setBackDirIfNeeded();
			return false;
		}
	}

	FILE *fOut = fopen(filename, "wb");
	if (fOut == NULL)
	{
		displayErrorMsg("FILE I/O ERROR");
		setBackDirIfNeeded();
		return false;
	}

	setBackDirIfNeeded();

	strncpy(lastFilename, filename, PATH_MAX-1); // for rendering-thread

	const int32_t paulaMixFrequency = config.mod2WavOutputFreq * 2; // *2 for oversampling (we always do oversampling in MOD2WAV)
	int32_t maxSamplesPerTick = (int32_t)ceil(paulaMixFrequency / (MIN_BPM / 2.5)) + 1;

	mod2WavBuffer = (int16_t *)malloc((TICKS_PER_RENDER_CHUNK * maxSamplesPerTick) * sizeof (int16_t) * 2);
	if (mod2WavBuffer == NULL)
	{
		fclose(fOut);
		statusOutOfMemory();
		return false;
	}

	editor.mod2WavOngoing = true; // set this first

	// do some prep work
	audio.oversamplingFlag = true; 
	generateBpmTable(config.mod2WavOutputFreq, editor.timingMode == TEMPO_MODE_VBLANK);
	paulaSetup(paulaMixFrequency, audio.amigaModel);
	storeTempVariables();
	calcMod2WavTotalRows();
	restartSong(); // this also updates BPM (samples per tick) with the MOD2WAV audio output rate
	clearMixerDownsamplerStates();

	drawMod2WavProgressDialog();
	editor.abortMod2Wav = false;

	pointerSetMode(POINTER_MODE_MSG2, NO_CARRY);
	setStatusMessage("RENDERING MOD...", NO_CARRY);

	editor.mod2WavThread = SDL_CreateThread(mod2WavThreadFunc, NULL, fOut);
	if (editor.mod2WavThread == NULL)
	{
		fclose(fOut);
		free(mod2WavBuffer);

		doStopIt(true);

		editor.mod2WavOngoing = false; // must be set before calling resetAudio()
		resetAudio();

		removeAskBox();
		pointerSetMode(POINTER_MODE_IDLE, DO_CARRY);

		displayErrorMsg("THREAD ERROR !");
		return false;
	}

	return true;
}

#define CALC__END_OF_SONG \
if (--numLoops < 0) \
{ \
	calcingRows = false; \
	break; \
} \
else \
{ \
	memset(editor.rowVisitTable, 0, 128 * MOD_ROWS); \
}

// ONLY used for a visual percentage counter, so accuracy is not very important
static void calcMod2WavTotalRows(void)
{
	int8_t n_pattpos[PAULA_VOICES], n_loopcount[PAULA_VOICES];

	// for pattern loop
	memset(n_pattpos,   0, sizeof (n_pattpos));
	memset(n_loopcount, 0, sizeof (n_loopcount));

	song->rowsCounter = song->rowsInTotal  = 0;

	uint8_t modRow = 0;
	int16_t modPos = 0;
	uint16_t modPattern = song->header.patternTable[0];
	uint8_t pBreakPosition = 0;
	bool posJumpAssert = false;
	bool pBreakFlag = false;

	memset(editor.rowVisitTable, 0, 128 * MOD_ROWS);

	int8_t numLoops = editor.mod2WavNumLoops; // make a copy

	bool calcingRows = true;
	while (calcingRows)
	{
		editor.rowVisitTable[(modPos * MOD_ROWS) + modRow] = true;

		for (int32_t ch = 0; ch < PAULA_VOICES; ch++)
		{
			note_t *note = &song->patterns[modPattern][(modRow * PAULA_VOICES) + ch];
			if (note->command == 0x0B) // Bxx - Position Jump
			{
				modPos = note->param - 1;
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
				CALC__END_OF_SONG
			}
			else if (note->command == 0x0E && (note->param >> 4) == 0x06) // E6x - Pattern Loop
			{
				uint8_t pos = note->param & 0x0F;
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
						editor.rowVisitTable[(modPos * MOD_ROWS) + pos] = false;
				}
				else if (--n_loopcount[ch])
				{
					pBreakPosition = n_pattpos[ch];
					pBreakFlag = true;

					for (pos = pBreakPosition; pos <= modRow; pos++)
						editor.rowVisitTable[(modPos * MOD_ROWS) + pos] = false;
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

			modPos = (modPos + 1) & 127;
			if (modPos >= song->header.songLength)
			{
				modPos = 0;
				CALC__END_OF_SONG
			}

			modPattern = song->header.patternTable[modPos];
			if (modPattern > MAX_PATTERNS-1)
				modPattern = MAX_PATTERNS-1;
		}

		if (calcingRows && editor.rowVisitTable[(modPos * MOD_ROWS) + modRow])
		{
			// row has been visited before, we're now done!
			CALC__END_OF_SONG
		}
	}
}
