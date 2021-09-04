#pragma once

#include <stdint.h>
#include <stdbool.h>

bool initKaiserTable(void); // called once on tracker init
void freeKaiserTable(void);

void stopSampling(void);
void freeAudioDeviceList(void);
void renderSampleMonitor(void);
void setSamplingNote(uint8_t note); // must be called from video thread!
void renderSamplingBox(void);
void writeSampleMonitorWaveform(void);
void removeSamplingBox(void);
void handleSamplingBox(void);
void handleRepeatedSamplingButtons(void);
void samplingSampleNumUp(void);
void samplingSampleNumDown(void);
