// C port of ProTracker 2.3D's replayer (with some modifications, but still accurate)

// for finding memory leaks in debug mode with Visual Studio 
#if defined _DEBUG && defined _MSC_VER
#include <crtdbg.h>
#endif

#include <stdint.h>
#include <stdbool.h>
#include "pt2_audio.h"
#include "pt2_helpers.h"
#include "pt2_tables.h"
#include "pt2_config.h"
#include "pt2_visuals.h"
#include "pt2_scopes.h"
#include "pt2_sync.h"
#include "pt2_amigafilters.h"

static bool posJumpAssert, pBreakFlag, modRenderDone;
static bool doStopSong; // from F00 (Set Speed)
static int8_t pBreakPosition, oldRow, modPattern;
static uint8_t pattDelTime, lowMask = 0xFF, pattDelTime2;
static int16_t modOrder, oldPattern, oldOrder;
static uint16_t DMACONtemp;
static int32_t modBPM, oldBPM, oldSpeed, ciaSetBPM;

static const uint8_t funkTable[16] = // EFx (FunkRepeat/InvertLoop)
{
	0x00, 0x05, 0x06, 0x07, 0x08, 0x0A, 0x0B, 0x0D,
	0x10, 0x13, 0x16, 0x1A, 0x20, 0x2B, 0x40, 0x80
};

double ciaBpm2Hz(int32_t bpm)
{
	if (bpm == 0)
		return 0.0;

	const uint32_t ciaPeriod = 1773447 / bpm; // yes, PT truncates here
	return (double)CIA_PAL_CLK / (ciaPeriod+1); // +1, CIA triggers on underflow
}

void updatePaulaLoops(void) // used after manipulating sample loop points while Paula is live
{
	const bool audioWasntLocked = !audio.locked;
	if (audioWasntLocked)
		lockAudio();

	for (int32_t i = 0; i < PAULA_VOICES; i++)
	{
		const moduleChannel_t *ch = &song->channels[i];
		if (ch->n_samplenum == editor.currSample) // selected sample matches channel's sample?
		{
			const moduleSample_t *s = &song->samples[editor.currSample];

			paulaSetData(i, ch->n_start + s->loopStart);
			paulaSetLength(i, (uint16_t)(s->loopLength >> 1));
		}
	}

	if (audioWasntLocked)
		unlockAudio();
}

void turnOffVoices(void)
{
	const bool audioWasntLocked = !audio.locked;
	if (audioWasntLocked)
		lockAudio();

	paulaSetDMACON(0x000F); // turn off all voice DMAs

	for (int32_t i = 0; i < PAULA_VOICES; i++)
		paulaSetVolume(i, 0);

	// reset dithering/filter states (so that every playback session is identical)
	resetAmigaFilterStates();
	resetAudioDithering();

	if (audioWasntLocked)
		unlockAudio();

	editor.tuningToneFlag = false;
}

void initializeModuleChannels(module_t *s)
{
	assert(s != NULL);

	memset(s->channels, 0, sizeof (s->channels));

	moduleChannel_t *ch = s->channels;
	for (uint8_t i = 0; i < PAULA_VOICES; i++, ch++)
	{
		ch->n_chanindex = i;
		ch->n_dmabit = 1 << i;
	}
}

void setReplayerPosToTrackerPos(void)
{
	if (song == NULL)
		return;

	modPattern = (int8_t)song->currPattern;
	modOrder = song->currOrder;
	song->row = song->currRow;
	song->tick = 0;
}

int8_t *allocMemForAllSamples(void)
{
	// allocate memory for all sample data blocks (+ 2 extra, for quirk + safety)
	const size_t allocLen = (MOD_SAMPLES + 2) * config.maxSampleLength;
	return (int8_t *)calloc(allocLen, 1);
}

void modSetSpeed(int32_t speed)
{
	song->currSpeed = song->speed = speed;
	song->tick = 0;
}

void doStopIt(bool resetPlayMode)
{
	editor.songPlaying = false;

	pattDelTime = pattDelTime2 = 0;

	if (resetPlayMode)
	{
		editor.playMode = PLAY_MODE_NORMAL;
		editor.currMode = MODE_IDLE;
		pointerSetModeThreadSafe(POINTER_MODE_IDLE, true);
	}

	if (song != NULL)
	{
		moduleChannel_t *ch = song->channels;
		for (int32_t i = 0; i < PAULA_VOICES; i++, ch++)
		{
			ch->n_wavecontrol = 0;
			ch->n_glissfunk = 0;
			ch->n_finetune = 0;
			ch->n_loopcount = 0;
		}
	}

	doStopSong = false; // just in case this flag was stuck from command F00 (stop song)
}

void setPattern(int16_t pattern)
{
	if (pattern > MAX_PATTERNS-1)
		pattern = MAX_PATTERNS-1;

	song->currPattern = modPattern = (int8_t)pattern;
}

void storeTempVariables(void) // this one is accessed in other files, so non-static
{
	oldBPM = song->currBPM;
	oldRow = song->currRow;
	oldOrder = song->currOrder;
	oldSpeed = song->currSpeed;
	oldPattern = song->currPattern;
}

static void setVUMeterHeight(moduleChannel_t *ch)
{
	if (editor.muted[ch->n_chanindex])
		return;

	uint8_t vol = ch->n_volume;
	if ((ch->n_cmd & 0xF00) == 0xC00) // handle Cxx effect
		vol = ch->n_cmd & 0xFF;

	if (vol > 64)
		vol = 64;

	if (!editor.songPlaying)
	{
		editor.vuMeterVolumes[ch->n_chanindex] = vuMeterHeights[vol];
	}
	else
	{
		ch->syncVuVolume = vol;
		ch->syncFlags |= UPDATE_VUMETER;
	}
}

static void updateFunk(moduleChannel_t *ch)
{
	const int8_t funkSpeed = ch->n_glissfunk >> 4;
	if (funkSpeed == 0)
		return;

	ch->n_funkoffset += funkTable[funkSpeed];
	if (ch->n_funkoffset >= 128)
	{
		ch->n_funkoffset = 0;

		if (ch->n_loopstart != NULL && ch->n_wavestart != NULL) // ProTracker bugfix
		{
			if (++ch->n_wavestart >= ch->n_loopstart + (ch->n_replen << 1))
				ch->n_wavestart = ch->n_loopstart;

			*ch->n_wavestart = -1 - *ch->n_wavestart;
		}
	}
}

static void setGlissControl(moduleChannel_t *ch)
{
	ch->n_glissfunk = (ch->n_glissfunk & 0xF0) | (ch->n_cmd & 0x0F);
}

static void setVibratoControl(moduleChannel_t *ch)
{
	ch->n_wavecontrol = (ch->n_wavecontrol & 0xF0) | (ch->n_cmd & 0x0F);
}

static void setFineTune(moduleChannel_t *ch)
{
	ch->n_finetune = ch->n_cmd & 0xF;
}

static void jumpLoop(moduleChannel_t *ch)
{
	if (song->tick != 0)
		return;

	if ((ch->n_cmd & 0xF) == 0)
	{
		ch->n_pattpos = song->row;
	}
	else
	{
		if (ch->n_loopcount == 0)
			ch->n_loopcount = ch->n_cmd & 0xF;
		else if (--ch->n_loopcount == 0)
			return;

		pBreakPosition = ch->n_pattpos;
		pBreakFlag = true;

		// stuff used for MOD2WAV to determine if the song has reached its end
		if (editor.mod2WavOngoing)
		{
			for (int32_t tempParam = pBreakPosition; tempParam <= song->row; tempParam++)
				editor.rowVisitTable[(modOrder * MOD_ROWS) + tempParam] = false;
		}
	}
}

static void setTremoloControl(moduleChannel_t *ch)
{
	ch->n_wavecontrol = ((ch->n_cmd & 0xF) << 4) | (ch->n_wavecontrol & 0xF);
}

/* This is a little used effect, despite being present in original ProTracker.
** E8x was sometimes entirely replaced with code used for demo fx syncing in
** demo mod players, so it can be turned off by looking at DISABLE_E8X in
** protracker.ini if you so desire.
*/
static void karplusStrong(moduleChannel_t *ch)
{
	int8_t a, b;

	if (config.disableE8xEffect)
		return;

	if (ch->n_loopstart == NULL)
		return; // ProTracker bugfix

	int8_t *ptr8 = ch->n_loopstart;
	int16_t end = ((ch->n_replen * 2) & 0xFFFF) - 2;
	do
	{
		a = ptr8[0];
		b = ptr8[1];
		*ptr8++ = (a + b) >> 1;
	}
	while (--end >= 0);

	a = ptr8[0];
	b = ch->n_loopstart[0];
	*ptr8 = (a + b) >> 1;
}

static void doRetrg(moduleChannel_t *ch)
{
	paulaSetDMACON(ch->n_dmabit); // voice DMA off

	paulaSetData(ch->n_chanindex, ch->n_start); // n_start is increased on 9xx
	paulaSetLength(ch->n_chanindex, ch->n_length);
	paulaSetPeriod(ch->n_chanindex, ch->n_period);

	paulaSetDMACON(0x8000 | ch->n_dmabit); // voice DMA on

	// these take effect after the current DMA cycle is done
	paulaSetData(ch->n_chanindex, ch->n_loopstart);
	paulaSetLength(ch->n_chanindex, ch->n_replen);

	// set visuals

	ch->syncAnalyzerVolume = ch->n_volume;
	ch->syncAnalyzerPeriod = ch->n_period;
	ch->syncFlags |= UPDATE_ANALYZER;

	setVUMeterHeight(ch);
}

static void retrigNote(moduleChannel_t *ch)
{
	if ((ch->n_cmd & 0xF) > 0)
	{
		if (song->tick == 0 && (ch->n_note & 0xFFF) > 0)
			return;

		if (song->tick % (ch->n_cmd & 0xF) == 0)
			doRetrg(ch);
	}
}

static void volumeSlide(moduleChannel_t *ch)
{
	uint8_t param = ch->n_cmd & 0xFF;
	if ((param & 0xF0) == 0)
	{
		ch->n_volume -= param & 0x0F;
		if (ch->n_volume < 0)
			ch->n_volume = 0;
	}
	else
	{
		ch->n_volume += param >> 4;
		if (ch->n_volume > 64)
			ch->n_volume = 64;
	}
}

static void volumeFineUp(moduleChannel_t *ch)
{
	if (song->tick == 0)
	{
		ch->n_volume += ch->n_cmd & 0xF;
		if (ch->n_volume > 64)
			ch->n_volume = 64;
	}
}

static void volumeFineDown(moduleChannel_t *ch)
{
	if (song->tick == 0)
	{
		ch->n_volume -= ch->n_cmd & 0xF;
		if (ch->n_volume < 0)
			ch->n_volume = 0;
	}
}

static void noteCut(moduleChannel_t *ch)
{
	if (song->tick == (ch->n_cmd & 0xF))
		ch->n_volume = 0;
}

static void noteDelay(moduleChannel_t *ch)
{
	if (song->tick == (ch->n_cmd & 0xF) && (ch->n_note & 0xFFF) > 0)
		doRetrg(ch);
}

static void patternDelay(moduleChannel_t *ch)
{
	if (song->tick == 0 && pattDelTime2 == 0)
		pattDelTime = (ch->n_cmd & 0xF) + 1;
}

static void funkIt(moduleChannel_t *ch)
{
	if (song->tick == 0)
	{
		ch->n_glissfunk = ((ch->n_cmd & 0xF) << 4) | (ch->n_glissfunk & 0xF);

		if ((ch->n_glissfunk & 0xF0) > 0)
			updateFunk(ch);
	}
}

static void positionJump(moduleChannel_t *ch)
{
	// original PT doesn't do this check, but we have to
	if (editor.playMode != PLAY_MODE_PATTERN || (editor.currMode == MODE_RECORD && editor.recordMode != RECORD_PATT))
		modOrder = (ch->n_cmd & 0xFF) - 1; // B00 results in -1, but it safely jumps to order 0

	pBreakPosition = 0;
	posJumpAssert = true;
}

static void volumeChange(moduleChannel_t *ch)
{
	ch->n_volume = ch->n_cmd & 0xFF;
	if ((uint8_t)ch->n_volume > 64)
		ch->n_volume = 64;
}

static void patternBreak(moduleChannel_t *ch)
{
	pBreakPosition = (((ch->n_cmd & 0xF0) >> 4) * 10) + (ch->n_cmd & 0x0F);
	if ((uint8_t)pBreakPosition > 63)
		pBreakPosition = 0;

	posJumpAssert = true;
}

static void setSpeed(moduleChannel_t *ch)
{
	if ((ch->n_cmd & 0xFF) > 0)
	{
		if (editor.timingMode == TEMPO_MODE_VBLANK || (ch->n_cmd & 0xFF) < 32)
			modSetSpeed(ch->n_cmd & 0xFF);
		else
			ciaSetBPM = ch->n_cmd & 0xFF; // the CIA chip doesn't use its new timer value until the next interrupt, so change it later
	}
	else
	{
		// F00 - stop song
		doStopSong = true;
	}
}

static void arpeggio(moduleChannel_t *ch)
{
	int32_t arpNote;

	int32_t arpTick = song->tick % 3; // 0, 1, 2
	if (arpTick == 1)
	{
		arpNote = ch->n_cmd >> 4;
	}
	else if (arpTick == 2)
	{
		arpNote = ch->n_cmd & 0xF;
	}
	else // arpTick 0
	{
		paulaSetPeriod(ch->n_chanindex, ch->n_period);
		return;
	}

	/* 8bitbubsy: If the finetune is -1, this can overflow up to
	** 15 words outside of the table. The table is padded with
	** the correct overflow values to allow this to safely happen
	** and sound correct at the same time.
	*/
	const int16_t *periods = &periodTable[ch->n_finetune * 37];
	for (int32_t baseNote = 0; baseNote < 37; baseNote++)
	{
		if (ch->n_period >= periods[baseNote])
		{
			paulaSetPeriod(ch->n_chanindex, periods[baseNote+arpNote]);
			break;
		}
	}
}

static void portaUp(moduleChannel_t *ch)
{
	ch->n_period -= (ch->n_cmd & 0xFF) & lowMask;
	lowMask = 0xFF;

	if ((ch->n_period & 0xFFF) < 113) // PT BUG: sign removed before comparison, underflow not clamped!
		ch->n_period = (ch->n_period & 0xF000) | 113;

	paulaSetPeriod(ch->n_chanindex, ch->n_period & 0xFFF);
}

static void portaDown(moduleChannel_t *ch)
{
	ch->n_period += (ch->n_cmd & 0xFF) & lowMask;
	lowMask = 0xFF;

	if ((ch->n_period & 0xFFF) > 856)
		ch->n_period = (ch->n_period & 0xF000) | 856;

	paulaSetPeriod(ch->n_chanindex, ch->n_period & 0xFFF);
}

static void filterOnOff(moduleChannel_t *ch)
{
	if (song->tick == 0) // added this (just pointless to call this during all ticks!)
	{
		const bool filterOn = (ch->n_cmd & 1) ^ 1;
		if (filterOn)
		{
			editor.useLEDFilter = true;
			setLEDFilter(true);
		}
		else
		{
			editor.useLEDFilter = false;
			setLEDFilter(false);
		}
	}
}

static void finePortaUp(moduleChannel_t *ch)
{
	if (song->tick == 0)
	{
		lowMask = 0xF;
		portaUp(ch);
	}
}

static void finePortaDown(moduleChannel_t *ch)
{
	if (song->tick == 0)
	{
		lowMask = 0xF;
		portaDown(ch);
	}
}

static void setTonePorta(moduleChannel_t *ch)
{
	uint16_t note = ch->n_note & 0xFFF;
	const int16_t *portaPointer = &periodTable[ch->n_finetune * 37];

	int32_t i = 0;
	while (true)
	{
		// portaPointer[36] = 0, so i=36 is safe
		if (note >= portaPointer[i])
			break;

		if (++i >= 37)
		{
			i = 35;
			break;
		}
	}

	if ((ch->n_finetune & 8) && i > 0)
		i--;

	ch->n_wantedperiod = portaPointer[i];
	ch->n_toneportdirec = 0;

	     if (ch->n_period == ch->n_wantedperiod) ch->n_wantedperiod = 0;
	else if (ch->n_period > ch->n_wantedperiod) ch->n_toneportdirec = 1;
}

static void tonePortNoChange(moduleChannel_t *ch)
{
	if (ch->n_wantedperiod <= 0)
		return;

	if (ch->n_toneportdirec > 0)
	{
		ch->n_period -= ch->n_toneportspeed;
		if (ch->n_period <= ch->n_wantedperiod)
		{
			ch->n_period = ch->n_wantedperiod;
			ch->n_wantedperiod = 0;
		}
	}
	else
	{
		ch->n_period += ch->n_toneportspeed;
		if (ch->n_period >= ch->n_wantedperiod)
		{
			ch->n_period = ch->n_wantedperiod;
			ch->n_wantedperiod = 0;
		}
	}

	if ((ch->n_glissfunk & 0xF) == 0)
	{
		paulaSetPeriod(ch->n_chanindex, ch->n_period);
	}
	else
	{
		const int16_t *portaPointer = &periodTable[ch->n_finetune * 37];

		int32_t i = 0;
		while (true)
		{
			// portaPointer[36] = 0, so i=36 is safe
			if (ch->n_period >= portaPointer[i])
				break;

			if (++i >= 37)
			{
				i = 35;
				break;
			}
		}

		paulaSetPeriod(ch->n_chanindex, portaPointer[i]);
	}
}

static void tonePortamento(moduleChannel_t *ch)
{
	if ((ch->n_cmd & 0xFF) > 0)
	{
		ch->n_toneportspeed = ch->n_cmd & 0xFF;
		ch->n_cmd &= 0xFF00;
	}

	tonePortNoChange(ch);
}

static void vibrato2(moduleChannel_t *ch)
{
	uint16_t vibratoData;

	const uint8_t vibratoPos = (ch->n_vibratopos >> 2) & 0x1F;
	const uint8_t vibratoType = ch->n_wavecontrol & 3;

	if (vibratoType == 0) // sine
	{
		vibratoData = vibratoTable[vibratoPos];
	}
	else if (vibratoType == 1) // ramp
	{
		if (ch->n_vibratopos < 128)
			vibratoData = vibratoPos << 3;
		else
			vibratoData = 255 - (vibratoPos << 3);
	}
	else // square
	{
		vibratoData = 255;
	}

	vibratoData = (vibratoData * (ch->n_vibratocmd & 0xF)) >> 7;

	if (ch->n_vibratopos < 128)
		vibratoData = ch->n_period + vibratoData;
	else
		vibratoData = ch->n_period - vibratoData;

	paulaSetPeriod(ch->n_chanindex, vibratoData);

	ch->n_vibratopos += (ch->n_vibratocmd >> 2) & 0x3C;
}

static void vibrato(moduleChannel_t *ch)
{
	if ((ch->n_cmd & 0x0F) > 0)
		ch->n_vibratocmd = (ch->n_vibratocmd & 0xF0) | (ch->n_cmd & 0x0F);

	if ((ch->n_cmd & 0xF0) > 0)
		ch->n_vibratocmd = (ch->n_cmd & 0xF0) | (ch->n_vibratocmd & 0x0F);

	vibrato2(ch);
}

static void tonePlusVolSlide(moduleChannel_t *ch)
{
	tonePortNoChange(ch);
	volumeSlide(ch);
}

static void vibratoPlusVolSlide(moduleChannel_t *ch)
{
	vibrato2(ch);
	volumeSlide(ch);
}

static void tremolo(moduleChannel_t *ch)
{
	int16_t tremoloData;

	if ((ch->n_cmd & 0x0F) > 0)
		ch->n_tremolocmd = (ch->n_tremolocmd & 0xF0) | (ch->n_cmd & 0x0F);

	if ((ch->n_cmd & 0xF0) > 0)
		ch->n_tremolocmd = (ch->n_cmd & 0xF0) | (ch->n_tremolocmd & 0x0F);

	const uint8_t tremoloPos = (ch->n_tremolopos >> 2) & 0x1F;
	const uint8_t tremoloType = (ch->n_wavecontrol >> 4) & 3;

	if (tremoloType == 0) // sine
	{
		tremoloData = vibratoTable[tremoloPos];
	}
	else if (tremoloType == 1) // ramp
	{
		if (ch->n_vibratopos < 128) // PT bug, should've been ch->n_tremolopos
			tremoloData = tremoloPos << 3;
		else
			tremoloData = 255 - (tremoloPos << 3);
	}
	else // square
	{
		tremoloData = 255;
	}

	tremoloData = ((uint16_t)tremoloData * (ch->n_tremolocmd & 0xF)) >> 6;

	if (ch->n_tremolopos < 128)
	{
		tremoloData = ch->n_volume + tremoloData;
		if (tremoloData > 64)
			tremoloData = 64;
	}
	else
	{
		tremoloData = ch->n_volume - tremoloData;
		if (tremoloData < 0)
			tremoloData = 0;
	}

	paulaSetVolume(ch->n_chanindex, tremoloData);

	ch->n_tremolopos += (ch->n_tremolocmd >> 2) & 0x3C;
}

static void sampleOffset(moduleChannel_t *ch)
{
	if ((ch->n_cmd & 0xFF) > 0)
		ch->n_sampleoffset = ch->n_cmd & 0xFF;

	uint16_t newOffset = ch->n_sampleoffset << 7;

	// this signed test is the reason for the 9xx "sample >64kB = silence" bug
	if ((int16_t)newOffset < (int16_t)ch->n_length)
	{
		ch->n_length -= newOffset;
		ch->n_start += newOffset << 1;
	}
	else
	{
		ch->n_length = 1;
	}
}

static void E_Commands(moduleChannel_t *ch)
{
	const uint8_t ecmd = (ch->n_cmd & 0x00F0) >> 4;
	switch (ecmd)
	{
		case 0x0: filterOnOff(ch);       return;
		case 0x1: finePortaUp(ch);       return;
		case 0x2: finePortaDown(ch);     return;
		case 0x3: setGlissControl(ch);   return;
		case 0x4: setVibratoControl(ch); return;
		case 0x5: setFineTune(ch);       return;
		case 0x6: jumpLoop(ch);          return;
		case 0x7: setTremoloControl(ch); return;
		case 0x8: karplusStrong(ch);     return;
		case 0xE: patternDelay(ch);      return;
		default: break;
	}

	if (editor.muted[ch->n_chanindex])
		return;

	switch (ecmd)
	{
		case 0x9: retrigNote(ch);     return;
		case 0xA: volumeFineUp(ch);   return;
		case 0xB: volumeFineDown(ch); return;
		case 0xC: noteCut(ch);        return;
		case 0xD: noteDelay(ch);      return;
		case 0xF: funkIt(ch);         return;
		default: break;
	}
}

static void checkMoreEffects(moduleChannel_t *ch)
{
	const uint8_t cmd = (ch->n_cmd & 0x0F00) >> 8;
	switch (cmd)
	{
		case 0x9: sampleOffset(ch); return; // note the returns here, not breaks!
		case 0xB: positionJump(ch); return;
		case 0xD: patternBreak(ch); return;
		case 0xE: E_Commands(ch);   return;
		case 0xF: setSpeed(ch);     return;
		default: break;
	}

	if (editor.muted[ch->n_chanindex])
		return;

	if (cmd == 0xC)
	{
		volumeChange(ch);
		return;
	}

	paulaSetPeriod(ch->n_chanindex, ch->n_period);
}

static void chkefx2(moduleChannel_t *ch)
{
	updateFunk(ch);

	if ((ch->n_cmd & 0xFFF) == 0)
		return;

	const uint8_t cmd = (ch->n_cmd & 0x0F00) >> 8;
	switch (cmd)
	{
		case 0x0: arpeggio(ch);            return; // note the returns here, not breaks!
		case 0x1: portaUp(ch);             return;
		case 0x2: portaDown(ch);           return;
		case 0x3: tonePortamento(ch);      return;
		case 0x4: vibrato(ch);             return;
		case 0x5: tonePlusVolSlide(ch);    return;
		case 0x6: vibratoPlusVolSlide(ch); return;
		case 0xE: E_Commands(ch);          return;
		default: break;
	}

	paulaSetPeriod(ch->n_chanindex, ch->n_period);

	if (cmd == 0x7)
		tremolo(ch);
	else if (cmd == 0xA)
		volumeSlide(ch);
}

static void checkEffects(moduleChannel_t *ch)
{
	if (editor.muted[ch->n_chanindex])
		return;

	chkefx2(ch);

	/* This is not very clear in the original PT replayer code,
	** but the tremolo effect skips chkefx2()'s return address
	** in the stack so that it jumps to checkEffects()'s return
	** address instead of ending up here. In other words, volume
	** is not updated here after tremolo (it's done inside the
	** tremolo routine itself).
	*/
	const uint8_t cmd = (ch->n_cmd & 0x0F00) >> 8;
	if (cmd != 0x7)
		paulaSetVolume(ch->n_chanindex, ch->n_volume);
}

static void setPeriod(moduleChannel_t *ch)
{
	int32_t i;

	uint16_t note = ch->n_note & 0xFFF;
	for (i = 0; i < 37; i++)
	{
		// periodTable[36] = 0, so i=36 is safe
		if (note >= periodTable[i])
			break;
	}

	// yes it's safe if i=37 because of zero-padding
	ch->n_period = periodTable[(ch->n_finetune * 37) + i];

	if ((ch->n_cmd & 0xFF0) != 0xED0) // no note delay
	{
		paulaSetDMACON(ch->n_dmabit); // voice DMA off (turned on in setDMA() later)

		if ((ch->n_wavecontrol & 0x04) == 0) ch->n_vibratopos = 0;
		if ((ch->n_wavecontrol & 0x40) == 0) ch->n_tremolopos = 0;

		paulaSetLength(ch->n_chanindex, ch->n_length);
		paulaSetData(ch->n_chanindex, ch->n_start);

		if (ch->n_start == NULL)
		{
			ch->n_loopstart = NULL;
			paulaSetLength(ch->n_chanindex, 1);
			ch->n_replen = 1;
		}

		paulaSetPeriod(ch->n_chanindex, ch->n_period);

		DMACONtemp |= ch->n_dmabit;
	}

	checkMoreEffects(ch);
}

static void checkMetronome(moduleChannel_t *ch, note_t *note)
{
	if (editor.metroFlag && editor.metroChannel > 0)
	{
		if (ch->n_chanindex == editor.metroChannel-1 && (song->row % editor.metroSpeed) == 0)
		{
			note->sample = 31;
			note->period = (((song->row / editor.metroSpeed) % editor.metroSpeed) == 0) ? 160 : 214;
		}
	}
}

static void playVoice(moduleChannel_t *ch)
{
	if (ch->n_note == 0 && ch->n_cmd == 0) // test period, command and command parameter
		paulaSetPeriod(ch->n_chanindex, ch->n_period);

	note_t note = song->patterns[modPattern][(song->row * PAULA_VOICES) + ch->n_chanindex];

	checkMetronome(ch, &note);

	ch->n_note = note.period;
	ch->n_cmd = (note.command << 8) | note.param;

	if (note.sample >= 1 && note.sample <= 31) // SAFETY BUG FIX: don't handle sample-numbers >31
	{
		ch->n_samplenum = note.sample - 1;
		moduleSample_t *s = &song->samples[ch->n_samplenum];

		ch->n_start = &song->sampleData[s->offset];
		ch->n_finetune = s->fineTune & 0xF;
		ch->n_volume = s->volume;
		ch->n_length = (uint16_t)(s->length >> 1);
		ch->n_replen = (uint16_t)(s->loopLength >> 1);

		const uint16_t repeat = (uint16_t)(s->loopStart >> 1);
		if (repeat > 0)
		{
			ch->n_loopstart = ch->n_start + (repeat << 1);
			ch->n_wavestart = ch->n_loopstart;
			ch->n_length = repeat + ch->n_replen;
		}
		else
		{
			ch->n_loopstart = ch->n_start;
			ch->n_wavestart = ch->n_start;
		}

		// non-PT2 requirement (set safe sample space for uninitialized voices - f.ex. "the ultimate beeper.mod")
		if (ch->n_length == 0)
			ch->n_loopstart = ch->n_wavestart = &song->sampleData[config.reservedSampleOffset]; // 128K reserved sample
	}

	if ((ch->n_note & 0xFFF) > 0)
	{
		if ((ch->n_cmd & 0xFF0) == 0xE50) // set finetune
		{
			setFineTune(ch);
			setPeriod(ch);
		}
		else
		{
			uint8_t cmd = (ch->n_cmd & 0x0F00) >> 8;
			if (cmd == 3 || cmd == 5)
			{
				setVUMeterHeight(ch);
				setTonePorta(ch);
				checkMoreEffects(ch);
			}
			else if (cmd == 9)
			{
				checkMoreEffects(ch);
				setPeriod(ch);
			}
			else
			{
				setPeriod(ch);
			}
		}
	}
	else
	{
		checkMoreEffects(ch);
	}
}

static void updateUIPositions(void)
{
	if (editor.mod2WavOngoing || editor.pat2SmpOngoing)
		return; // don't update UI under MOD2WAV/PAT2SMP rendering

	song->currRow = song->row;
	song->currOrder = modOrder;
	song->currPattern = modPattern;

	uint16_t *currPatPtr = &song->header.order[modOrder];
	editor.currPatternDisp = currPatPtr;
	editor.currPosEdPattDisp = currPatPtr;
	editor.currPatternDisp = currPatPtr;
	editor.currPosEdPattDisp = currPatPtr;

	ui.updateSongPos = true;
	ui.updateSongPattern = true;
	ui.updateCurrPattText = true;
	ui.updatePatternData = true;

	if (ui.posEdScreenShown)
		ui.updatePosEd = true;
}

static void nextPosition(void)
{
	if (editor.pat2SmpOngoing)
		modRenderDone = true;

	song->row = pBreakPosition;
	pBreakPosition = 0;
	posJumpAssert = false;

	if (editor.playMode != PLAY_MODE_PATTERN ||
		(editor.currMode == MODE_RECORD && editor.recordMode != RECORD_PATT))
	{
		if (editor.stepPlayEnabled)
		{
			if (config.keepEditModeAfterStepPlay && editor.stepPlayLastMode == MODE_EDIT)
			{
				doStopIt(false);

				editor.playMode = PLAY_MODE_NORMAL;
				editor.currMode = MODE_EDIT;
				pointerSetModeThreadSafe(POINTER_MODE_EDIT, true);
			}
			else
			{
				doStopIt(true);
			}

			editor.stepPlayEnabled = false;
			editor.stepPlayBackwards = false;

			if (editor.stepPlayLastMode == MODE_EDIT || editor.stepPlayLastMode == MODE_IDLE)
			{
				song->row &= 63;
				song->currRow = song->row;
			}
			else
			{
				// if we were playing, set replayer row to tracker row (stay in sync)
				song->currRow &= 63;
				song->row = song->currRow;
			}

			return;
		}

		modOrder = (modOrder + 1) & 127;
		if (modOrder >= song->header.numOrders)
		{
			modOrder = 0;

			if (config.compoMode) // stop song for music competitions playing
			{
				doStopIt(true);
				turnOffVoices();

				modOrder = 0;
				modPattern = (int8_t)song->header.order[modOrder];
				song->row = 0;

				updateUIPositions();
			}

			if (editor.mod2WavOngoing)
				modRenderDone = true;
		}

		modPattern = (int8_t)song->header.order[modOrder];
		if (modPattern > MAX_PATTERNS-1)
			modPattern = MAX_PATTERNS-1;
	}
}

static void increasePlaybackTimer(void)
{
	// the timer is not counting in "play pattern" mode
	if (editor.playMode != PLAY_MODE_PATTERN && modBPM >= 32 && modBPM <= 255)
	{
		if (editor.timingMode == TEMPO_MODE_CIA)
			editor.musicTime64 += musicTimeTab64[modBPM-32];
		else
			editor.musicTime64 += musicTimeTab64[(255-32)+1]; // vblank tempo mode
	}
}

static void setCurrRowToVisited(void) // for MOD2WAV
{
	if (editor.mod2WavOngoing)
		editor.rowVisitTable[(modOrder * MOD_ROWS) + song->row] = true;
}

static bool renderEndCheck(void) // for MOD2WAV/PAT2SMP
{
	if (!editor.mod2WavOngoing && !editor.pat2SmpOngoing)
		return true; // we're not doing MOD2WAV/PAT2SMP

	bool noPatternDelay = (pattDelTime2 == 0);
	if (noPatternDelay)
	{
		if (editor.pat2SmpOngoing)
		{
			if (modRenderDone)
				return false; // we're done rendering
		}

		if (editor.mod2WavOngoing && song->tick == song->speed-1)
		{
			bool rowVisited = editor.rowVisitTable[(modOrder * MOD_ROWS) + song->row];
			if (rowVisited || modRenderDone)
			{
				modRenderDone = false;
				return false; // we're done rendering
			}
		}
	}

	return true;
}

static void setDMA(void)
{
	if (editor.muted[0]) DMACONtemp &= ~1;
	if (editor.muted[1]) DMACONtemp &= ~2;
	if (editor.muted[2]) DMACONtemp &= ~4;
	if (editor.muted[3]) DMACONtemp &= ~8;

	paulaSetDMACON(0x8000 | DMACONtemp);

	moduleChannel_t *ch = song->channels;
	for (int32_t i = 0; i < PAULA_VOICES; i++, ch++)
	{
		if (DMACONtemp & ch->n_dmabit) // sample trigger
		{
			ch->syncAnalyzerVolume = ch->n_volume;
			ch->syncAnalyzerPeriod = ch->n_period;
			ch->syncFlags |= UPDATE_ANALYZER;

			setVUMeterHeight(ch);
		}

		// these take effect after the current DMA cycle is done
		paulaSetData(i, ch->n_loopstart);
		paulaSetLength(i, ch->n_replen);
	}
}

void modSetTempo(int32_t bpm, bool doLockAudio)
{
	if (bpm < 32 || bpm > 255)
		return;

	const bool audioWasntLocked = !audio.locked;
	if (doLockAudio && audioWasntLocked)
		lockAudio();
	
	modBPM = bpm;
	if (!editor.pat2SmpOngoing && !editor.mod2WavOngoing)
	{
		song->currBPM = bpm;
		ui.updateSongBPM = true;
	}

	bpm -= 32; // 32..255 -> 0..223
	audio.samplesPerTick64 = audio.samplesPerTickTable[bpm];

	// calculate tick time length for audio/video sync timestamp (visualizers)
	if (!editor.pat2SmpOngoing && !editor.mod2WavOngoing)
	{
		const uint64_t tickTimeLen64 = audio.tickLengthTable[bpm];
		const uint32_t tickTimeLen = tickTimeLen64 >> 32;
		const uint32_t tickTimeLenFrac = (uint32_t)tickTimeLen64;

		setSyncTickTimeLen(tickTimeLen, tickTimeLenFrac);
	}

	if (doLockAudio && audioWasntLocked)
		unlockAudio();
}

bool intMusic(void) // replayer ticker
{
	// quirk: CIA BPM changes are delayed by one tick in PT, so handle previous tick's BPM change now
	if (ciaSetBPM != -1)
	{
		const int32_t newBPM = ciaSetBPM;
		modSetTempo(newBPM, false);
		ciaSetBPM = -1;
	}

	increasePlaybackTimer();

	if (!editor.stepPlayEnabled)
		song->tick++;

	bool readNewNote = false;
	if ((uint32_t)song->tick >= (uint32_t)song->speed)
	{
		song->tick = 0;
		readNewNote = true;
	}

	if (readNewNote || editor.stepPlayEnabled) // tick 0
	{
		if (pattDelTime2 == 0) // no pattern delay, time to read note data
		{
			DMACONtemp = 0; // reset Paula DMA trigger states

			setCurrRowToVisited(); // for MOD2WAV
			updateUIPositions(); // update current song positions in UI

			// read note data and trigger voices
			moduleChannel_t *ch = song->channels;
			for (int32_t i = 0; i < PAULA_VOICES; i++, ch++)
			{
				playVoice(ch);
				paulaSetVolume(i, ch->n_volume);
			}

			setDMA();
		}
		else // pattern delay is on-going
		{
			moduleChannel_t *ch = song->channels;
			for (int32_t i = 0; i < PAULA_VOICES; i++, ch++)
				checkEffects(ch);
		}

		// increase row
		if (!editor.stepPlayBackwards)
		{
			song->row++;
			song->rowsCounter++; // for MOD2WAV's progress bar
		}

		if (pattDelTime > 0)
		{
			pattDelTime2 = pattDelTime;
			pattDelTime = 0;
		}

		// undo row increase if pattern delay is on-going
		if (pattDelTime2 > 0)
		{
			pattDelTime2--;
			if (pattDelTime2 > 0)
			{
				song->row--;
				song->rowsCounter--; // for MOD2WAV's progress bar
			}
		}

		if (pBreakFlag)
		{
			song->row = pBreakPosition;
			pBreakPosition = 0;
			pBreakFlag = false;
		}

		// step-play handling
		if (editor.stepPlayEnabled)
		{
			if (config.keepEditModeAfterStepPlay && editor.stepPlayLastMode == MODE_EDIT)
			{
				doStopIt(false);

				editor.playMode = PLAY_MODE_NORMAL;
				editor.currMode = MODE_EDIT;
				pointerSetModeThreadSafe(POINTER_MODE_EDIT, true);
			}
			else
			{
				doStopIt(true);
			}

			if (editor.stepPlayLastMode == MODE_EDIT || editor.stepPlayLastMode == MODE_IDLE)
			{
				song->row &= 0x3F;
				song->currRow = song->row;
			}
			else
			{
				// if we were playing, set replayer row to tracker row (stay in sync)
				song->currRow &= 0x3F;
				song->row = song->currRow;
			}

			editor.stepPlayEnabled = false;
			editor.stepPlayBackwards = false;

			ui.updatePatternData = true;
			return true;
		}

		if (song->row >= MOD_ROWS || posJumpAssert)
			nextPosition();

		// for pattern block mark feature
		if (editor.blockMarkFlag)
			ui.updateStatusText = true;
	}
	else // tick > 0 (handle effects)
	{
		moduleChannel_t *ch = song->channels;
		for (int32_t i = 0; i < PAULA_VOICES; i++, ch++)
			checkEffects(ch);

		if (posJumpAssert)
			nextPosition();
	}

	// command F00 = stop song, do it here (so that the scopes are updated properly)
	if (doStopSong)
	{
		doStopSong = false;

		editor.songPlaying = false;

		editor.playMode = PLAY_MODE_NORMAL;
		editor.currMode = MODE_IDLE;
		pointerSetModeThreadSafe(POINTER_MODE_IDLE, true);
	}

	return renderEndCheck(); // MOD2WAV/PAT2SMP listens to the return value (true = not done yet)
}

void modSetPattern(uint8_t pattern)
{
	modPattern = pattern;
	song->currPattern = modPattern;
	ui.updateCurrPattText = true;
}

void modSetPos(int16_t order, int16_t row)
{
	if (row != -1)
	{
		row = CLAMP(row, 0, 63);

		song->tick = 0;
		song->row = (int8_t)row;
		song->currRow = (int8_t)row;
	}

	if (order != -1)
	{
		if (order >= 0)
		{
			modOrder = order;
			song->currOrder = order;
			ui.updateSongPos = true;

			if (editor.currMode == MODE_PLAY && editor.playMode == PLAY_MODE_NORMAL)
			{
				modPattern = (int8_t)song->header.order[order];
				if (modPattern > MAX_PATTERNS-1)
					modPattern = MAX_PATTERNS-1;

				song->currPattern = modPattern;
				ui.updateCurrPattText = true;
			}

			ui.updateSongPattern = true;
			editor.currPatternDisp = &song->header.order[modOrder];

			int16_t posEdPos = song->currOrder;
			if (posEdPos > song->header.numOrders-1)
				posEdPos = song->header.numOrders-1;

			editor.currPosEdPattDisp = &song->header.order[posEdPos];

			if (ui.posEdScreenShown)
				ui.updatePosEd = true;
		}
	}

	ui.updatePatternData = true;

	if (editor.blockMarkFlag)
		ui.updateStatusText = true;
}

void modStop(void)
{
	editor.songPlaying = false;
	turnOffVoices();

	if (song != NULL)
	{
		moduleChannel_t *ch = song->channels;
		for (int32_t i = 0; i < PAULA_VOICES; i++, ch++)
		{
			ch->n_wavecontrol = 0;
			ch->n_glissfunk = 0;
			ch->n_finetune = 0;
			ch->n_loopcount = 0;
		}
	}

	pBreakFlag = false;
	pattDelTime = 0;
	pattDelTime2 = 0;
	pBreakPosition = 0;
	posJumpAssert = false;
	modRenderDone = true;

	/* The replayer is one tick ahead (unfortunately), so if the user was to stop the mod at the previous tick
	** before a position jump (pattern loop, pattern break, position jump, row 63->0 transition, etc),
	** it would be possible for the replayer to be at another order/pattern than the tracker.
	** Let's set the replayer state to the tracker state on mod stop, to fix possible confusion.
	*/
	setReplayerPosToTrackerPos();

	doStopSong = false; // just in case this flag was stuck from command F00 (stop song)
}

void playPattern(int8_t startRow)
{
	if (!editor.stepPlayEnabled)
		pointerSetMode(POINTER_MODE_PLAY, DO_CARRY);

	audio.tickSampleCounter64 = 0; // zero tick sample counter so that it will instantly initiate a tick
	song->currRow = song->row = startRow & 0x3F;

	if (!editor.stepPlayEnabled)
		song->tick = song->speed-1;
	else
		song->tick = 0;

	ciaSetBPM = -1; // fix possibly stuck "set BPM" flag

	editor.playMode = PLAY_MODE_PATTERN;
	editor.currMode = MODE_PLAY;
	editor.didQuantize = false;
	editor.songPlaying = true;
}

void incPatt(void)
{
	modPattern++;
	if (modPattern > MAX_PATTERNS-1)
		modPattern = 0;

	song->currPattern = modPattern;

	ui.updatePatternData = true;
	ui.updateCurrPattText = true;
}

void decPatt(void)
{
	modPattern--;
	if (modPattern < 0)
		modPattern = MAX_PATTERNS - 1;

	song->currPattern = modPattern;

	ui.updatePatternData = true;
	ui.updateCurrPattText = true;
}

void modPlay(int16_t patt, int16_t order, int8_t row)
{
	const bool audioWasntLocked = !audio.locked;
	if (audioWasntLocked)
		lockAudio();

	doStopIt(false);
	turnOffVoices();

	if (row != -1)
	{
		if (row >= 0 && row <= 63)
		{
			song->row = row;
			song->currRow = row;
		}
	}
	else
	{
		song->row = 0;
		song->currRow = 0;
	}

	if (editor.playMode != PLAY_MODE_PATTERN)
	{
		if (modOrder >= song->header.numOrders)
		{
			modOrder = 0;
			song->currOrder = 0;
		}

		if (order >= 0 && order < song->header.numOrders)
		{
			modOrder = order;
			song->currOrder = order;
		}

		if (order >= song->header.numOrders)
		{
			modOrder = 0;
			song->currOrder = 0;
		}
	}

	if (patt >= 0 && patt <= MAX_PATTERNS-1)
		song->currPattern = modPattern = (int8_t)patt;
	else
		song->currPattern = modPattern = (int8_t)song->header.order[modOrder];

	editor.currPatternDisp = &song->header.order[modOrder];
	editor.currPosEdPattDisp = &song->header.order[modOrder];

	song->tick = song->speed-1;
	ciaSetBPM = -1; // fix possibly stuck "set BPM" flag

	modRenderDone = false;
	editor.songPlaying = true;
	editor.didQuantize = false;

	if (editor.playMode != PLAY_MODE_PATTERN)
		editor.musicTime64 = 0; // don't reset playback counter in "play/rec pattern" mode

	audio.tickSampleCounter64 = 0; // zero tick sample counter so that it will instantly initiate a tick

	if (audioWasntLocked)
		unlockAudio();

	if (!editor.pat2SmpOngoing && !editor.mod2WavOngoing)
	{
		ui.updateSongPos = true;
		ui.updatePatternData = true;
		ui.updateSongPattern = true;
		ui.updateCurrPattText = true;
	}
}

void clearSong(void)
{
	assert(song != NULL);
	if (song == NULL)
		return;

	memset(song->header.order, 0, sizeof (song->header.order));
	memset(song->header.name, 0, sizeof (song->header.name));

	editor.muted[0] = false;
	editor.muted[1] = false;
	editor.muted[2] = false;
	editor.muted[3] = false;

	editor.f6Pos = 0;
	editor.f7Pos = 16;
	editor.f8Pos = 32;
	editor.f9Pos = 48;
	editor.f10Pos = 63;

	editor.musicTime64 = 0;

	editor.metroFlag = false;
	editor.currSample = 0;
	editor.editMoveAdd = 1;
	editor.blockMarkFlag = false;
	editor.swapChannelFlag = false;

	song->header.numOrders = 1;

	for (int32_t i = 0; i < MAX_PATTERNS; i++)
		memset(song->patterns[i], 0, (MOD_ROWS * PAULA_VOICES) * sizeof (note_t));

	moduleChannel_t *ch = song->channels;
	for (int32_t i = 0; i < PAULA_VOICES; i++, ch++)
	{
		ch->n_wavecontrol = 0;
		ch->n_glissfunk = 0;
		ch->n_finetune = 0;
		ch->n_loopcount = 0;
	}

	modSetPos(0, 0); // this also refreshes pattern data

	song->currOrder = 0;
	song->currPattern = 0;
	editor.currPatternDisp = &song->header.order[0];
	editor.currPosEdPattDisp = &song->header.order[0];

	modSetTempo(editor.initialTempo, true);
	modSetSpeed(editor.initialSpeed);

	// disable the LED filter after clearing the song (real PT2 doesn't do this)
	editor.useLEDFilter = false;
	setLEDFilter(false);

	updateCurrSample();

	ui.updateSongSize = true;
	ui.updateSongLength = true;
	ui.updateSongName = true;
	renderMuteButtons();
}

void clearSamples(void)
{
	assert(song != NULL);
	if (song == NULL)
		return;

	moduleSample_t *s = song->samples;
	for (int32_t i = 0; i < MOD_SAMPLES; i++, s++)
	{
		s->fineTune = 0;
		s->length = 0;
		s->loopLength = 2;
		s->loopStart = 0;
		s->volume = 0;

		memset(s->text, 0, sizeof (s->text));
	}

	memset(song->sampleData, 0, (MOD_SAMPLES + 1) * config.maxSampleLength);

	editor.currSample = 0;
	editor.hiLowInstr = 0;
	editor.sampleZero = false;
	ui.editOpScreenShown = false;
	ui.aboutScreenShown = false;
	editor.blockMarkFlag = false;

	editor.samplePos = 0;
	updateCurrSample();
}

void modFree(void)
{
	if (song == NULL)
		return; // not allocated

	const bool audioWasntLocked = !audio.locked;
	if (audioWasntLocked)
		lockAudio();

	turnOffVoices();

	for (int32_t i = 0; i < MAX_PATTERNS; i++)
	{
		if (song->patterns[i] != NULL)
			free(song->patterns[i]);
	}

	if (song->sampleData != NULL)
		free(song->sampleData);

	free(song);
	song = NULL;

	if (audioWasntLocked)
		unlockAudio();
}

void restartSong(void) // for the beginning of MOD2WAV/PAT2SMP
{
	if (editor.songPlaying)
		modStop();

	editor.playMode = PLAY_MODE_NORMAL;
	editor.blockMarkFlag = false;

	song->row = 0;
	song->currRow = 0;
	song->rowsCounter = 0;

	memset(editor.rowVisitTable, 0, MOD_ORDERS * MOD_ROWS); // for MOD2WAV

	if (editor.pat2SmpOngoing)
	{
		modSetSpeed(song->currSpeed);
		modSetTempo(song->currBPM, true);
		modPlay(DONT_SET_PATTERN, DONT_SET_ORDER, 0);
	}
	else
	{
		song->currSpeed = 6;
		song->currBPM = 125;
		modSetSpeed(song->currSpeed);
		modSetTempo(song->currBPM, true);

		modPlay(DONT_SET_PATTERN, 0, 0);
	}
}

// this function is meant for the end of MOD2WAV/PAT2SMP
void resetSong(void) // only call this after storeTempVariables() has been called!
{
	modStop();

	editor.songPlaying = false;
	editor.playMode = PLAY_MODE_NORMAL;
	editor.currMode = MODE_IDLE;

	turnOffVoices();

	memset((int8_t *)editor.vuMeterVolumes,     0, sizeof (editor.vuMeterVolumes));
	memset((int8_t *)editor.realVuMeterVolumes, 0, sizeof (editor.realVuMeterVolumes));
	memset((int8_t *)editor.spectrumVolumes,    0, sizeof (editor.spectrumVolumes));

	initializeModuleChannels(song);

	modOrder = oldOrder;
	modPattern = (int8_t)oldPattern;

	song->row = oldRow;
	song->currRow = oldRow;
	song->currBPM = oldBPM;
	song->currOrder = oldOrder;
	song->currPattern = oldPattern;

	editor.currPosDisp = &song->currOrder;
	editor.currEditPatternDisp = &song->currPattern;
	editor.currPatternDisp = &song->header.order[song->currOrder];
	editor.currPosEdPattDisp = &song->header.order[song->currOrder];

	modSetSpeed(oldSpeed);
	modSetTempo(oldBPM, true);

	doStopIt(true);

	song->tick = 0;
	modRenderDone = false;
}
