/* Experimental audio sampling support.
** There may be several bad practices here, as I don't really
** have the proper knowledge on this stuff.
**
** Some functions like sin() may be different depending on
** math library implementation, but we don't use pt2_math.c
** replacements for speed reasons.
*/

// for finding memory leaks in debug mode with Visual Studio 
#if defined _DEBUG && defined _MSC_VER
#include <crtdbg.h>
#endif

#include <math.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "pt2_textout.h"
#include "pt2_sampler.h"
#include "pt2_visuals.h"
#include "pt2_helpers.h"
#include "pt2_bmp.h"
#include "pt2_audio.h"
#include "pt2_tables.h"
#include "pt2_config.h"
#include "pt2_sampling.h"
#include "pt2_math.h" // PT2_PI
#include "pt2_replayer.h"

enum
{
	SAMPLE_LEFT = 0,
	SAMPLE_RIGHT = 1,
	SAMPLE_MIX  = 2
};

// this may change after opening the audio input device
#define SAMPLING_BUFFER_SIZE 1024

#define SAMPLING_MIXER_FRAC_BITS 32

// after several tests, these values yields a good trade-off between quality and compute time
#define SINC_TAPS 64
#define SINC_TAPS_BITS 6 /* log2(SINC_TAPS) */
#define SINC_PHASES 4096
#define SINC_PHASES_BITS 12 /* log2(SINC_PHASES */
#define SINC_FSHIFT (SAMPLING_MIXER_FRAC_BITS-(SINC_PHASES_BITS+SINC_TAPS_BITS))
#define SINC_FMASK ((SINC_TAPS*SINC_PHASES)-SINC_TAPS)

#define MID_TAP ((SINC_TAPS/2)*SINC_PHASES)

#define SAMPLE_PREVIEW_WITDH 194
#define SAMPLE_PREVIEW_HEIGHT 38
#define MAX_INPUT_DEVICES 99
#define VISIBLE_LIST_ENTRIES 4

static volatile bool callbackBusy, displayingBuffer, samplingEnded;
static bool audioDevOpen;
static char *audioInputDevs[MAX_INPUT_DEVICES];
static uint8_t samplingNote = 33, samplingFinetune = 4; // period 124, max safe period for PAL Paula
static int16_t displayBuffer[SAMPLING_BUFFER_SIZE];
static int32_t samplingMode = SAMPLE_MIX, inputFrequency, roundedOutputFrequency;
static int32_t numAudioInputDevs, audioInputDevListOffset, selectedDev;
static int32_t bytesSampled, maxSamplingLength, inputBufferSize;
static float *fSincTable, *fKaiserWindow, *fSamplingBuffer, *fSamplingBufferOrig;
static double dOutputFrequency;
static SDL_AudioDeviceID recordDev;

/*
** ----------------------------------------------------------------------------------
** Sinc code based on code from the OpenMPT project (has a similar BSD license)
** ----------------------------------------------------------------------------------
*/

// zeroth-order modified Bessel function of the first kind (series approximation)
static double besselI0(double z)
{
	double s = 1.0, ds = 1.0, d = 2.0;

	do
	{
		ds *= (z * z) / (d * d);
		s += ds;
		d += 2.0;
	}
	while (ds > s*1E-15);

	return s;
}

bool initKaiserTable(void) // called once on tracker init
{
	fKaiserWindow = (float *)malloc(SINC_TAPS * SINC_PHASES * sizeof (float));
	if (fKaiserWindow == NULL)
	{
		showErrorMsgBox("Out of memory!");
		return false;
	}

	const double beta = 9.6567817670946337; // 96.3dB sidelobe attenuation
	const double izeroBeta = besselI0(beta);

	for (int32_t i = 0; i < SINC_TAPS*SINC_PHASES; i++)
	{
		const int32_t ix = (((SINC_TAPS-1) - (i & (SINC_TAPS-1))) << SINC_PHASES_BITS) + (i >> SINC_TAPS_BITS);

		double dKaiser = 1.0;
		if (ix != MID_TAP)
		{
			const double x = (ix - MID_TAP) * (1.0 / SINC_PHASES);
			const double xMul = 1.0 / ((SINC_TAPS/2) * (SINC_TAPS/2));
			dKaiser = besselI0(beta * sqrt(1.0 - x * x * xMul)) / izeroBeta;
		}

		fKaiserWindow[i] = (float)dKaiser;
	}

	return true;
}

void freeKaiserTable(void)
{
	if (fKaiserWindow != NULL)
	{
		free(fKaiserWindow);
		fKaiserWindow = NULL;
	}
}

// calculated after completion of sampling (before downsampling)
static bool initSincTable(float cutoff)
{
	fSincTable = (float *)malloc(SINC_TAPS * SINC_PHASES * sizeof (float));
	if (fSincTable == NULL)
		return false;

	if (cutoff > 1.0f)
		cutoff = 1.0f;

	const float kPi = (float)(PT2_PI * cutoff);
	for (int32_t i = 0; i < SINC_TAPS * SINC_PHASES; i++)
	{
		const int32_t ix = (((SINC_TAPS-1) - (i & (SINC_TAPS-1))) << SINC_PHASES_BITS) + (i >> SINC_TAPS_BITS);

		float fSinc = 1.0f;
		if (ix != MID_TAP)
		{
			const float x = (ix - MID_TAP) * (1.0f / SINC_PHASES);
			const float xPi = x * kPi;

			fSinc = (sinf(xPi) / xPi) * fKaiserWindow[i];
		}

		fSincTable[i] = fSinc * cutoff;
	}

	return true;
}

static void freeSincTable(void)
{
	if (fSincTable != NULL)
	{
		free(fSincTable);
		fSincTable = NULL;
	}
}

static inline float sinc(const float *fSmpData, const uint32_t phase32)
{
	const float *t = fSincTable + ((phase32 >> SINC_FSHIFT) & SINC_FMASK);

	float fSmp = 0.0f;
	for (int32_t i = 0; i < SINC_TAPS; i++)
		fSmp += fSmpData[i] * t[i];

	return fSmp;
}

/*
** ----------------------------------------------------------------------------------
** ----------------------------------------------------------------------------------
*/

static void listAudioDevices(void);

static void updateOutputFrequency(void)
{
	if (samplingNote > 35)
		samplingNote = 35;

	int32_t period = periodTable[((samplingFinetune & 0xF) * 37) + samplingNote];
	if (period < 113) // also happens in our "set period" Paula function
		period = 113;

	dOutputFrequency = (double)PAULA_PAL_CLK / period;
	roundedOutputFrequency = (int32_t)(dOutputFrequency + 0.5);
}

static void SDLCALL samplingCallback(void *userdata, Uint8 *stream, int len)
{
	callbackBusy = true;

	if (!displayingBuffer)
	{
		if (len > SAMPLING_BUFFER_SIZE)
			len = SAMPLING_BUFFER_SIZE;

		const int16_t *L =  (int16_t *)stream;
		const int16_t *R = ((int16_t *)stream) + 1;

		int16_t *dst16 = displayBuffer;

		if (samplingMode == SAMPLE_LEFT)
		{
			for (int32_t i = 0; i < len; i++)
				dst16[i] = L[i << 1];
		}
		else if (samplingMode == SAMPLE_RIGHT)
		{
			for (int32_t i = 0; i < len; i++)
				dst16[i] = R[i << 1];
		}
		else
		{
			for (int32_t i = 0; i < len; i++)
				dst16[i] = (L[i << 1] + R[i << 1]) >> 1;
		}
	}

	if (audio.isSampling)
	{
		if (bytesSampled+len > maxSamplingLength)
			len = maxSamplingLength - bytesSampled;

		if (len > inputBufferSize)
			len = inputBufferSize;

		const int16_t *L = (int16_t *)stream;
		const int16_t *R = ((int16_t *)stream) + 1;

		float *fSmp = &fSamplingBuffer[bytesSampled];

		if (samplingMode == SAMPLE_LEFT)
		{
			for (int32_t i = 0; i < len; i++)
				fSmp[i] = L[i << 1] * (1.0f / 32768.0f);
		}
		else if (samplingMode == SAMPLE_RIGHT)
		{
			for (int32_t i = 0; i < len; i++)
				fSmp[i] = R[i << 1] * (1.0f / 32768.0f);
		}
		else
		{
			for (int32_t i = 0; i < len; i++)
				fSmp[i] = (L[i << 1] + R[i << 1]) * (1.0f / (32768.0f * 2.0f));
		}

		bytesSampled += len;
		if (bytesSampled >= maxSamplingLength)
		{
			audio.isSampling = true;
			samplingEnded = true;
		}
	}

	callbackBusy = false;
	(void)userdata;
}

static void stopInputAudio(void)
{
	if (recordDev > 0)
	{
		SDL_CloseAudioDevice(recordDev);
		recordDev = 0;
	}
	callbackBusy = false;
}

static void startInputAudio(void)
{
	SDL_AudioSpec want, have;

	if (recordDev > 0)
		stopInputAudio();

	if (numAudioInputDevs == 0 || selectedDev >= numAudioInputDevs)
	{
		audioDevOpen = false;
		return;
	}

	assert(roundedOutputFrequency > 0);

	memset(&want, 0, sizeof (SDL_AudioSpec));
	want.freq = config.audioInputFrequency;
	want.format = AUDIO_S16;
	want.channels = 2;
	want.callback = samplingCallback;
	want.userdata = NULL;
	want.samples = SAMPLING_BUFFER_SIZE;

	recordDev = SDL_OpenAudioDevice(audioInputDevs[selectedDev], true, &want, &have, 0);
	audioDevOpen = (recordDev != 0);

	inputFrequency = have.freq;
	inputBufferSize = have.samples;

	SDL_PauseAudioDevice(recordDev, false);
}

static void selectAudioDevice(int32_t dev)
{
	if (dev < 0)
		return;

	if (numAudioInputDevs == 0)
	{
		listAudioDevices();
		return;
	}

	if (dev >= numAudioInputDevs)
		return;

	listAudioDevices();

	stopInputAudio();
	selectedDev = dev;
	listAudioDevices();
	startInputAudio();

	changeStatusText(ui.statusMessage);
}

void renderSampleMonitor(void)
{
	blit32(120, 44, 200, 55, sampleMonitorBMP);
	memset(displayBuffer, 0, sizeof (displayBuffer));
}

void freeAudioDeviceList(void)
{
	for (int32_t i = 0; i < numAudioInputDevs; i++)
	{
		if (audioInputDevs[i] != NULL)
		{
			free(audioInputDevs[i]);
			audioInputDevs[i] = NULL;
		}
	}
}

static void scanAudioDevices(void)
{
	freeAudioDeviceList();

	numAudioInputDevs = SDL_GetNumAudioDevices(true);
	if (numAudioInputDevs > MAX_INPUT_DEVICES)
		numAudioInputDevs = MAX_INPUT_DEVICES;

	for (int32_t i = 0; i < numAudioInputDevs; i++)
	{
		const char *deviceName = SDL_GetAudioDeviceName(i, true);
		if (deviceName == NULL)
		{
			numAudioInputDevs--; // hide device
			continue;
		}

		const uint32_t stringLen = (uint32_t)strlen(deviceName);

		audioInputDevs[i] = (char *)malloc(stringLen + 2);
		if (audioInputDevs[i] == NULL)
			break;

		if (stringLen > 0)
			strcpy(audioInputDevs[i], deviceName);

		audioInputDevs[i][stringLen+1] = '\0'; // UTF-8 needs double null termination (XXX: citation needed)
	}

	audioInputDevListOffset = 0; // reset scroll position

	if (selectedDev >= numAudioInputDevs)
		selectedDev = 0;
}

static void listAudioDevices(void)
{
	fillRect(3, 219, 163, 33, PAL_BACKGRD);

	if (numAudioInputDevs == 0)
	{
		textOut(16, 219+13, "NO DEVICES FOUND!", video.palette[PAL_QADSCP]);
		return;
	}

	for (int32_t i = 0; i < VISIBLE_LIST_ENTRIES; i++)
	{
		const int32_t dev = audioInputDevListOffset+i;
		if (audioInputDevListOffset+i >= numAudioInputDevs)
			break;

		if (dev == selectedDev)
			fillRect(4, 219+1+(i*(FONT_CHAR_H+3)), 161, 8, video.palette[PAL_GENBKG2]);

		if (audioInputDevs[dev] != NULL)
			textOutTightN(2+2, 219+2+(i*(FONT_CHAR_H+3)), audioInputDevs[dev], 23, video.palette[PAL_QADSCP]);
	}
}

static void drawSamplingNote(void)
{
	assert(samplingNote < 36);
	const char *str = config.accidental ? noteNames2[2+samplingNote]: noteNames1[2+samplingNote];
	textOutBg(262, 230, str, video.palette[PAL_GENTXT], video.palette[PAL_GENBKG]);
}

static void drawSamplingFinetune(void)
{
	textOutBg(254, 219, ftuneStrTab[samplingFinetune & 0xF], video.palette[PAL_GENTXT], video.palette[PAL_GENBKG]);
}

static void drawSamplingFrequency(void)
{
	char str[16];
	sprintf(str, "%05dHZ", roundedOutputFrequency);

	const int32_t maxSafeFrequency = (int32_t)(PAL_PAULA_MAX_SAFE_HZ + 0.5); // rounded
	textOutBg(262, 208, str, roundedOutputFrequency <= maxSafeFrequency ? video.palette[PAL_GENTXT] : 0x8C0F0F, video.palette[PAL_GENBKG]);
}

static void drawSamplingModeCross(void)
{
	// clear old crosses
	fillRect(4, 208, 6, 5, video.palette[PAL_GENBKG]);
	fillRect(51, 208, 6, 5, video.palette[PAL_GENBKG]);
	fillRect(105, 208, 6, 5, video.palette[PAL_GENBKG]);

	int16_t x;
	if (samplingMode == SAMPLE_LEFT)
		x = 3;
	else if (samplingMode == SAMPLE_RIGHT)
		x = 50;
	else
		x = 104;

	charOut(x, 208, 'X', video.palette[PAL_GENTXT]);
}

static void showCurrSample(void)
{
	updateCurrSample();

	// reset sampler screen attributes
	sampler.loopStartPos = 0;
	sampler.loopEndPos = 0;
	editor.markStartOfs = -1;
	editor.markEndOfs = -1;
	editor.samplePos = 0;
	hideSprite(SPRITE_LOOP_PIN_LEFT);
	hideSprite(SPRITE_LOOP_PIN_RIGHT);

	renderSampleData();
}

void renderSamplingBox(void)
{
	changeStatusText("PLEASE WAIT ...");
	flipFrame();
	hpc_ResetCounters(&video.vblankHpc);

	editor.sampleZero = false;
	editor.blockMarkFlag = false;

	// remove all open screens (except sampler)
	if (ui.diskOpScreenShown  || ui.posEdScreenShown || ui.editOpScreenShown)
	{
		ui.diskOpScreenShown = false;
		ui.posEdScreenShown = false;
		ui.editOpScreenShown = false;

		displayMainScreen();
	}
	setStatusMessage("ALL RIGHT", DO_CARRY);

	blit32(0, 203, 320, 52, samplingBoxBMP);

	updateOutputFrequency();
	drawSamplingNote();
	drawSamplingFinetune();
	drawSamplingFrequency();
	drawSamplingModeCross();
	renderSampleMonitor();

	scanAudioDevices();
	selectAudioDevice(selectedDev);

	showCurrSample();
	modStop();

	editor.songPlaying = false;
	editor.playMode = PLAY_MODE_NORMAL;
	editor.currMode = MODE_IDLE;
	pointerSetMode(POINTER_MODE_IDLE, DO_CARRY);
}

static int32_t scrPos2SmpBufPos(int32_t x) // x = 0..SAMPLE_PREVIEW_WITDH
{
	return (x * ((SAMPLING_BUFFER_SIZE << 16) / SAMPLE_PREVIEW_WITDH)) >> 16;
}

static uint8_t getDispBuffPeak(const int16_t *smpData, int32_t smpNum)
{
	int32_t smpAbs, max = 0;
	for (int32_t i = 0; i < smpNum; i++)
	{
		const int32_t smp = smpData[i];

		smpAbs = ABS(smp);
		if (smpAbs > max)
			max = smpAbs;
	}

	max = ((max * SAMPLE_PREVIEW_HEIGHT) + 32768) >> 16;
	if (max > (SAMPLE_PREVIEW_HEIGHT/2)-1)
		max = (SAMPLE_PREVIEW_HEIGHT/2)-1;

	return (uint8_t)max;
}

void writeSampleMonitorWaveform(void) // called every frame
{
	if (!ui.samplingBoxShown || ui.askBoxShown)
		return;

	if (samplingEnded)
	{
		samplingEnded = false;
		stopSampling();
	}

	// clear waveform background
	fillRect(123, 58, SAMPLE_PREVIEW_WITDH, SAMPLE_PREVIEW_HEIGHT, video.palette[PAL_BACKGRD]);

	if (!audioDevOpen)
	{
		textOutTight(136, 74, "CAN'T OPEN AUDIO DEVICE!", video.palette[PAL_QADSCP]);
		return;
	}

	uint32_t *centerPtr = &video.frameBuffer[(76 * SCREEN_W) + 123];

	// hardcoded for a buffer size of 512
	displayingBuffer = true;
	for (int32_t x = 0; x < SAMPLE_PREVIEW_WITDH; x++)
	{
		int32_t smpIdx = scrPos2SmpBufPos(x);
		int32_t smpNum = scrPos2SmpBufPos(x+1) - smpIdx;

		if (smpIdx+smpNum >= SAMPLING_BUFFER_SIZE)
			smpNum = SAMPLING_BUFFER_SIZE - smpIdx;

		const int32_t smpAbs = getDispBuffPeak(&displayBuffer[smpIdx], smpNum);
		if (smpAbs == 0)
			centerPtr[x] = video.palette[PAL_QADSCP];
		else
			vLine(x + 123, 76 - smpAbs, (smpAbs * 2) + 1, video.palette[PAL_QADSCP]);
	}
	displayingBuffer = false;
}

void removeSamplingBox(void)
{
	stopInputAudio();
	freeAudioDeviceList();

	ui.aboutScreenShown = false;
	editor.blockMarkFlag = false;
	displayMainScreen();
	updateVisualizer(); // kludge

	// re-render sampler screen
	exitFromSam();
	samplerScreen();
}

static void startSampling(void)
{
	if (!audioDevOpen)
	{
		displayErrorMsg("DEVICE ERROR !");
		return;
	}

	assert(roundedOutputFrequency > 0);

	maxSamplingLength = (int32_t)(ceil(((double)config.maxSampleLength*inputFrequency) / dOutputFrequency)) + 1;
	
	const int32_t allocLen = (SINC_TAPS/2) + maxSamplingLength + (SINC_TAPS/2);

	fSamplingBufferOrig = (float *)malloc(allocLen * sizeof (float));
	if (fSamplingBufferOrig == NULL)
	{
		statusOutOfMemory();
		return;
	}
	fSamplingBuffer = fSamplingBufferOrig + (SINC_TAPS/2); // allow negative look-up for sinc taps

	// clear tap area before sample
	memset(fSamplingBufferOrig, 0, (SINC_TAPS/2) * sizeof (float));

	bytesSampled = 0;
	audio.isSampling = true;
	samplingEnded = false;

	turnOffVoices();

	pointerSetMode(POINTER_MODE_RECORD, NO_CARRY);
	setStatusMessage("SAMPLING ...", NO_CARRY);
}

static int32_t downsampleSamplingBuffer(void)
{
	// clear tap area after sample
	memset(&fSamplingBuffer[bytesSampled], 0, (SINC_TAPS/2) * sizeof (float));

	const int32_t readLength = bytesSampled;
	const double dRatio = dOutputFrequency / inputFrequency;
	
	int32_t writeLength = (int32_t)(readLength * dRatio);
	if (writeLength > config.maxSampleLength)
		writeLength = config.maxSampleLength;

	float *fBuffer = (float *)malloc(writeLength * sizeof (float));
	if (fBuffer == NULL)
	{
		statusOutOfMemory();
		return -1;
	}

	if (!initSincTable((float)dRatio))
	{
		statusOutOfMemory();
		return -1;
	}

	// downsample

	int8_t *output = &song->sampleData[song->samples[editor.currSample].offset];
	const uint64_t delta64 = (uint64_t)round((inputFrequency / dOutputFrequency) * (UINT32_MAX+1.0));
	uint64_t phase64 = 0;

	// pre-centered (this is safe, look at how fSamplingBufferOrig is alloc'd)
	const float *fSmpData = &fSamplingBuffer[-((SINC_TAPS/2)-1)];

	float fPeakAmp = 0.0f;
	for (int32_t i = 0; i < writeLength; i++)
	{
		float fSmp = sinc(fSmpData, (uint32_t)phase64);
		fBuffer[i] = fSmp;

		// get peak for normalization stage
		if (fSmp < 0.0f)
			fSmp = -fSmp;

		if (fSmp > fPeakAmp)
			fPeakAmp = fSmp;
		// -------------------

		phase64 += delta64;
		fSmpData += phase64 >> 32;
		phase64 &= UINT32_MAX;
	}

	freeSincTable();

	// normalize and quantize to 8-bit integer

	float fAmp = INT8_MAX / fPeakAmp;

	/* If we have to amplify THIS much, it would mean that the gain was extremely low.
	** We don't want the result to be 99% noise, so keep it quantized to zero (silence).
	*/
	const float fAmp_dB = 20.0f * log10f(fAmp / 128.0f);
	if (fAmp_dB > 30.0f)
		fAmp = 0.0f;

	for (int32_t i = 0; i < writeLength; i++)
	{
		float fSmp = fBuffer[i] * fAmp;

		// add rounding bias
		if (fSmp < 0.0f) fSmp -= 0.5f;
		if (fSmp > 0.0f) fSmp += 0.5f;

		output[i] = (int8_t)fSmp;
	}

	free(fBuffer);
	return writeLength;
}

void stopSampling(void)
{
	while (callbackBusy);
	audio.isSampling = false;

	int32_t newLength = downsampleSamplingBuffer();
	if (newLength == -1)
		return; // out of memory

	if (fSamplingBufferOrig != NULL)
	{
		free(fSamplingBufferOrig);
		fSamplingBufferOrig = NULL;
	}

	moduleSample_t *s = &song->samples[editor.currSample];
	s->length = newLength;
	s->fineTune = samplingFinetune;
	s->loopStart = 0;
	s->loopLength = 2;
	s->volume = 64;
	fixSampleBeep(s);

	pointerSetMode(POINTER_MODE_IDLE, DO_CARRY);
	statusAllRight();

	showCurrSample();
}

static void scrollListUp(void)
{
	if (numAudioInputDevs <= VISIBLE_LIST_ENTRIES)
	{
		audioInputDevListOffset = 0;
		return;
	}

	if (audioInputDevListOffset > 0)
	{
		audioInputDevListOffset--;
		listAudioDevices();
		mouse.lastSamplingButton = 0;
	}
}

static void scrollListDown(void)
{
	if (numAudioInputDevs <= VISIBLE_LIST_ENTRIES)
	{
		audioInputDevListOffset = 0;
		return;
	}

	if (audioInputDevListOffset < numAudioInputDevs-VISIBLE_LIST_ENTRIES)
	{
		audioInputDevListOffset++;
		listAudioDevices();
		mouse.lastSamplingButton = 1;
	}
}

static void finetuneUp(void)
{
	if ((int8_t)samplingFinetune < 7)
	{
		samplingFinetune++;
		updateOutputFrequency();
		drawSamplingFinetune();
		drawSamplingFrequency();
		mouse.lastSamplingButton = 2;
	}
}

static void finetuneDown(void)
{
	if ((int8_t)samplingFinetune > -8)
	{
		samplingFinetune--;
		updateOutputFrequency();
		drawSamplingFinetune();
		drawSamplingFrequency();
		mouse.lastSamplingButton = 3;
	}
}

void samplingSampleNumUp(void)
{
	if (editor.currSample < 30)
	{
		editor.currSample++;
		showCurrSample();
	}
}

void samplingSampleNumDown(void)
{
	if (editor.currSample > 0)
	{
		editor.currSample--;
		showCurrSample();
	}
}

void handleSamplingBox(void)
{
	if (ui.changingSamplingNote)
	{
		ui.changingSamplingNote = false;
		setPrevStatusMessage();
		pointerSetPreviousMode();
		drawSamplingNote();
		return;
	}

	if (mouse.rightButtonPressed)
	{
		if (audio.isSampling)
			stopSampling();
		else
			startSampling();

		return;
	}

	if (!mouse.leftButtonPressed)
		return;

	mouse.lastSamplingButton = -1;
	mouse.repeatCounter = 0;

	if (audio.isSampling)
	{
		stopSampling();
		return;
	}

	// check buttons
	const int32_t mx = mouse.x;
	const int32_t my = mouse.y;

	if (mx >= 182 && mx <= 243 && my >= 0 && my <= 10) // STOP (main UI)
	{
		turnOffVoices();
	}

	if (mx >= 6 && mx <= 25 && my >= 124 && my <= 133) // EXIT (main UI)
	{
		ui.samplingBoxShown = false;
		removeSamplingBox();
		exitFromSam();
	}

	if (mx >= 98 && mx <= 108 && my >= 44 && my <= 54) // SAMPLE UP (main UI)
	{
		samplingSampleNumUp();
	}

	else if (mx >= 109 && mx <= 119 && my >= 44 && my <= 54) // SAMPLE DOWN (main UI)
	{
		samplingSampleNumDown();
	}

	else if (mx >= 143 && mx <= 176 && my >= 205 && my <= 215) // SCAN
	{
		if (audio.rescanAudioDevicesSupported)
		{
			scanAudioDevices();
			listAudioDevices();
		}
		else
		{
			displayErrorMsg("UNSUPPORTED !");
		}
	}

	else if (mx >= 4 && mx <= 165 && my >= 220 && my <= 250) // DEVICE LIST
	{
		selectAudioDevice(audioInputDevListOffset + ((my - 220) >> 3));
	}

	else if (mx >= 2 && mx <= 41 && my >= 206 && my <= 216) // LEFT
	{
		if (samplingMode != SAMPLE_LEFT)
		{
			samplingMode = SAMPLE_LEFT;
			drawSamplingModeCross();
		}
	}

	else if (mx >= 49 && mx <= 95 && my >= 206 && my <= 216) // RIGHT
	{
		if (samplingMode != SAMPLE_RIGHT)
		{
			samplingMode = SAMPLE_RIGHT;
			drawSamplingModeCross();
		}
	}

	else if (mx >= 103 && mx <= 135 && my >= 206 && my <= 216) // MIX
	{
		if (samplingMode != SAMPLE_MIX)
		{
			samplingMode = SAMPLE_MIX;
			drawSamplingModeCross();
		}
	}

	else if (mx >= 188 && mx <= 237 && my >= 242 && my <= 252) // SAMPLE
	{
		startSampling();
	}

	else if (mx >= 242 && mx <= 277 && my >= 242 && my <= 252) // NOTE
	{
		ui.changingSamplingNote = true;
		textOutBg(262, 230, "---", video.palette[PAL_GENTXT], video.palette[PAL_GENBKG]);
		setStatusMessage("SELECT NOTE", NO_CARRY);
		pointerSetMode(POINTER_MODE_MSG1, NO_CARRY);
	}

	else if (mx >= 282 && mx <= 317 && my >= 242 && my <= 252) // EXIT
	{
		ui.samplingBoxShown = false;
		removeSamplingBox();
	}

	else if (mx >= 166 && mx <= 177 && my >= 218 && my <= 228) // SCROLL LIST UP
	{
		scrollListUp();
	}

	else if (mx >= 166 && mx <= 177 && my >= 242 && my <= 252) // SCROLL LIST DOWN
	{
		scrollListDown();
	}

	else if (mx >= 296 && mx <= 306 && my >= 217 && my <= 227) // FINETUNE UP
	{
		finetuneUp();
	}

	else if (mx >= 307 && mx <= 317 && my >= 217 && my <= 227) // FINETUNE DOWN
	{
		finetuneDown();
	}
}

void setSamplingNote(uint8_t note) // must be called from video thread!
{
	if (note > 35)
		note = 35;

	samplingNote = note;
	samplingFinetune = 0;
	updateOutputFrequency();

	drawSamplingNote();
	drawSamplingFinetune();
	drawSamplingFrequency();
}

void handleRepeatedSamplingButtons(void)
{
	if (!mouse.leftButtonPressed || mouse.lastSamplingButton == -1)
		return;

	switch (mouse.lastSamplingButton)
	{
		case 0:
		{
			if (mouse.repeatCounter++ >= 3)
			{
				mouse.repeatCounter = 0;
				scrollListUp();
			}
		}
		break;

		case 1:
		{
			if (mouse.repeatCounter++ >= 3)
			{
				mouse.repeatCounter = 0;
				scrollListDown();
			}
		}
		break;

		case 2:
		{
			if (mouse.repeatCounter++ >= 5)
			{
				mouse.repeatCounter = 0;
				finetuneUp();
			}
		}
		break;

		case 3:
		{
			if (mouse.repeatCounter++ >= 5)
			{
				mouse.repeatCounter = 0;
				finetuneDown();
			}
		}
		break;

		default: break;
	}
}
