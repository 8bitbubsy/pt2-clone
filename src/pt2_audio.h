#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "pt2_header.h" // AMIGA_VOICES

typedef struct audio_t
{
	volatile bool locked, isSampling;

	bool forceSoundCardSilence, oversamplingFlag;
	
	uint32_t outputRate, audioBufferSize;
	int64_t tickSampleCounter64, samplesPerTick64;
	int64_t bpmTable[256-32], bpmTable28kHz[256-32], bpmTable20kHz[256-32]; // 32.32 fixed-point
	double dPeriodToDeltaDiv;

	// for audio sampling
	bool rescanAudioDevicesSupported;

	// for audio/video syncing
	bool resetSyncTickTimeFlag;
	uint64_t tickLengthTable[224];
} audio_t;

typedef struct voice_t
{
	volatile bool DMA_active;

	// internal values (don't modify directly!)
	int8_t AUD_DAT[2]; // DMA data buffer
	const int8_t* location; // current location
	uint16_t lengthCounter; // current length
	int32_t sampleCounter; // how many bytes left in AUD_DAT
	double dSample; // current sample point

	// registers modified by Paula functions
	const int8_t* AUD_LC; // location
	uint16_t AUD_LEN; // length (in words)
	double AUD_PER_delta, AUD_PER_deltamul; // delta
	double AUD_VOL; // volume

	double dDelta, dPhase;

	// for BLEP synthesis
	double dLastDelta, dLastPhase, dLastDeltaMul, dBlepOffset, dDeltaMul;

	// period cache
	int32_t oldPeriod;
	double dOldVoiceDelta, dOldVoiceDeltaMul;

	// used for pt2_sync.c
	uint8_t syncFlags;
	uint8_t syncVolume;
	int32_t syncPeriod;
	int32_t syncTriggerLength;
	const int8_t *syncTriggerData;
} paulaVoice_t;

void updateReplayerTimingMode(void);

void setSyncTickTimeLen(uint32_t timeLen, uint32_t timeLenFrac);
void resetCachedMixerPeriod(void);
void resetAudioDithering(void);

uint16_t get16BitPeak(int16_t *sampleData, uint32_t sampleLength);
uint32_t get32BitPeak(int32_t *sampleData, uint32_t sampleLength);
float getFloatPeak(float *fSampleData, uint32_t sampleLength);
double getDoublePeak(double *dSampleData, uint32_t sampleLength);
void normalize16BitTo8Bit(int16_t *sampleData, uint32_t sampleLength);
void normalize32BitTo8Bit(int32_t *sampleData, uint32_t sampleLength);
void normalizeFloatTo8Bit(float *fSampleData, uint32_t sampleLength);
void normalizeDoubleTo8Bit(double *dSampleData, uint32_t sampleLength);

void setLEDFilter(bool state, bool doLockAudio);
void toggleLEDFilter(void);
void toggleAmigaPanMode(void);
void toggleFilterModel(void);
void paulaStopDMA(int32_t ch);
void paulaStartDMA(int32_t ch);
void paulaSetPeriod(int32_t ch, uint16_t period);
void paulaSetVolume(int32_t ch, uint16_t vol);
void paulaSetLength(int32_t ch, uint16_t len);
void paulaSetData(int32_t ch, const int8_t *src);
void lockAudio(void);
void unlockAudio(void);
void mixerUpdateLoops(void);
void mixerKillVoice(int32_t ch);
void turnOffVoices(void);
void mixerSetStereoSeparation(uint8_t percentage);
void outputAudio(int16_t *target, int32_t numSamples);

extern audio_t audio; // pt2_audio.c
extern paulaVoice_t paula[AMIGA_VOICES]; // pt2_audio.c
