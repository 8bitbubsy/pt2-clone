// C port of ProTracker 2.3D's replayer by 8bitbubsy, slightly modified.

// for finding memory leaks in debug mode with Visual Studio 
#if defined _DEBUG && defined _MSC_VER
#include <crtdbg.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <math.h>
#include "pt2_header.h"
#include "pt2_audio.h"
#include "pt2_helpers.h"
#include "pt2_palette.h"
#include "pt2_tables.h"
#include "pt2_module_loader.h"
#include "pt2_config.h"
#include "pt2_sampler.h"
#include "pt2_visuals.h"
#include "pt2_textout.h"
#include "pt2_scopes.h"
#include "pt2_sync.h"

static bool posJumpAssert, pBreakFlag, updateUIPositions, modHasBeenPlayed;
static int8_t pBreakPosition, oldRow, modPattern;
static uint8_t pattDelTime, setBPMFlag, lowMask = 0xFF, pattDelTime2, oldSpeed;
static int16_t modOrder, oldPattern, oldOrder;
static uint16_t modBPM, oldBPM;

static const uint8_t funkTable[16] = // EFx (FunkRepeat/InvertLoop)
{
	0x00, 0x05, 0x06, 0x07, 0x08, 0x0A, 0x0B, 0x0D,
	0x10, 0x13, 0x16, 0x1A, 0x20, 0x2B, 0x40, 0x80
};

int8_t *allocMemForAllSamples(void)
{
	/* Allocate memoru for all sample data blocks.
	**
	** We need three extra sample slots:
	** The 1st is extra safety padding since setting a Paula length of 0
	** results in reading (1+65535)*2 bytes. The 2nd and 3rd (64K*2 = 1x 128K)
	** are reserved for NULL pointers. This is needed for emulating a PT quirk.
	**
	** We have a padding of 4 bytes at the end for length=0 quirk safety.
	**
	** PS: I don't really know if it's possible for ProTracker to set a Paula
	** length of 0, but I fully support this Paula behavior just in case.
	*/
	const size_t allocLen = ((MOD_SAMPLES + 3) * MAX_SAMPLE_LEN) + 4;

	return (int8_t *)calloc(1, allocLen);
}

void modSetSpeed(uint8_t speed)
{
	song->speed = speed;
	song->currSpeed = speed;
	song->tick = 0;
}

void doStopIt(bool resetPlayMode)
{
	editor.songPlaying = false;

	resetCachedMixerPeriod();
	resetCachedScopePeriod();

	pattDelTime = 0;
	pattDelTime2 = 0;

	if (resetPlayMode)
	{
		editor.playMode = PLAY_MODE_NORMAL;
		editor.currMode = MODE_IDLE;

		pointerSetMode(POINTER_MODE_IDLE, DO_CARRY);
	}

	if (song != NULL)
	{
		for (int32_t i = 0; i < AMIGA_VOICES; i++)
		{
			moduleChannel_t *c = &song->channels[i];

			c->n_wavecontrol = 0;
			c->n_glissfunk = 0;
			c->n_finetune = 0;
			c->n_loopcount = 0;
		}
	}
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
	uint8_t vol;

	if (editor.muted[ch->n_chanindex])
		return;

	vol = ch->n_volume;
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

		if (ch->n_loopstart != NULL && ch->n_wavestart != NULL) // non-PT2 bug fix
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
	uint8_t tempParam;

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
		if (editor.isWAVRendering)
		{
			for (tempParam = pBreakPosition; tempParam <= song->row; tempParam++)
				editor.rowVisitTable[(modOrder * MOD_ROWS) + tempParam] = false;
		}
	}
}

static void setTremoloControl(moduleChannel_t *ch)
{
	ch->n_wavecontrol = ((ch->n_cmd & 0xF) << 4) | (ch->n_wavecontrol & 0xF);
}

static void karplusStrong(moduleChannel_t *ch)
{
	/* This effect is definitely the least used PT effect there is!
	** It trashes (filters) the sample data.
	** The reason I'm not implementing it is because a lot of songs used
	** E8x for syncing to demos/intros, and because I have never ever
	** seen this effect being used intentionally.
	*/

	(void)ch;
}

static void doRetrg(moduleChannel_t *ch)
{
	paulaSetData(ch->n_chanindex, ch->n_start); // n_start is increased on 9xx
	paulaSetLength(ch->n_chanindex, ch->n_length);
	paulaSetPeriod(ch->n_chanindex, ch->n_period);
	paulaStartDMA(ch->n_chanindex);

	// these take effect after the current DMA cycle is done
	paulaSetData(ch->n_chanindex, ch->n_loopstart);
	paulaSetLength(ch->n_chanindex, ch->n_replen);

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
	uint8_t cmd = ch->n_cmd & 0xFF;

	if ((cmd & 0xF0) == 0)
	{
		ch->n_volume -= cmd & 0x0F;
		if (ch->n_volume < 0)
			ch->n_volume = 0;
	}
	else
	{
		ch->n_volume += cmd >> 4;
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
	modOrder = (ch->n_cmd & 0xFF) - 1; // 0xFF (B00) jumps to pat 0
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
		song->tick = 0;

		if (editor.timingMode == TEMPO_MODE_VBLANK || (ch->n_cmd & 0xFF) < 32)
			modSetSpeed(ch->n_cmd & 0xFF);
		else
			setBPMFlag = ch->n_cmd & 0xFF; // CIA doesn't refresh its registers until the next interrupt, so change it later
	}
	else
	{
		editor.songPlaying = false;
		editor.playMode = PLAY_MODE_NORMAL;
		editor.currMode = MODE_IDLE;

		pointerResetThreadSafe(); // set gray mouse cursor
	}
}

static void arpeggio(moduleChannel_t *ch)
{
	uint8_t arpTick, arpNote;
	const int16_t *periods;

	assert(song->tick < 32);
	arpTick = arpTickTable[song->tick]; // 0, 1, 2

	if (arpTick == 1)
	{
		arpNote = (uint8_t)(ch->n_cmd >> 4);
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
	periods = &periodTable[ch->n_finetune * 37];
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

	if ((ch->n_period & 0xFFF) < 113)
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
	setLEDFilter(!(ch->n_cmd & 1), false);
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
	uint8_t i;
	const int16_t *portaPointer;
	uint16_t note;

	note = ch->n_note & 0xFFF;
	portaPointer = &periodTable[ch->n_finetune * 37];

	i = 0;
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
	uint8_t i;
	const int16_t *portaPointer;

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
		portaPointer = &periodTable[ch->n_finetune * 37];

		i = 0;
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

	if (vibratoType == 0)
	{
		vibratoData = vibratoTable[vibratoPos];
	}
	else
	{
		if (vibratoType == 1)
		{
			if (ch->n_vibratopos < 128)
				vibratoData = vibratoPos << 3;
			else
				vibratoData = 255 - (vibratoPos << 3);
		}
		else
		{
			vibratoData = 255;
		}
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

	if (tremoloType == 0)
	{
		tremoloData = vibratoTable[tremoloPos];
	}
	else
	{
		if (tremoloType == 1)
		{
			if (ch->n_vibratopos < 128) // PT bug, should've been ch->n_tremolopos
				tremoloData = tremoloPos << 3;
			else
				tremoloData = 255 - (tremoloPos << 3);
		}
		else
		{
			tremoloData = 255;
		}
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

	if ((int16_t)newOffset < ch->n_length)
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
	const uint8_t cmd = (ch->n_cmd & 0xF0) >> 4;
	switch (cmd)
	{
		case 0x0: filterOnOff(ch);       break;
		case 0x1: finePortaUp(ch);       break;
		case 0x2: finePortaDown(ch);     break;
		case 0x3: setGlissControl(ch);   break;
		case 0x4: setVibratoControl(ch); break;
		case 0x5: setFineTune(ch);       break;
		case 0x6: jumpLoop(ch);          break;
		case 0x7: setTremoloControl(ch); break;
		case 0x8: karplusStrong(ch);     break;
		default: break;
	}

	if (editor.muted[ch->n_chanindex])
		return;

	switch (cmd)
	{
		case 0x9: retrigNote(ch);     break;
		case 0xA: volumeFineUp(ch);   break;
		case 0xB: volumeFineDown(ch); break;
		case 0xC: noteCut(ch);        break;
		case 0xD: noteDelay(ch);      break;
		case 0xE: patternDelay(ch);   break;
		case 0xF: funkIt(ch);         break;
		default: break;
	}
}

static void checkMoreEffects(moduleChannel_t *ch)
{
	switch ((ch->n_cmd & 0xF00) >> 8)
	{
		case 0x9: sampleOffset(ch); break;
		case 0xB: positionJump(ch); break;

		case 0xC:
		{
			if (!editor.muted[ch->n_chanindex])
				volumeChange(ch);
		}
		break;

		case 0xD: patternBreak(ch); break;
		case 0xE: E_Commands(ch);   break;
		case 0xF: setSpeed(ch);     break;

		default:
		{
			if (!editor.muted[ch->n_chanindex])
				paulaSetPeriod(ch->n_chanindex, ch->n_period);
		}
		break;
	}
}

static void checkEffects(moduleChannel_t *ch)
{
	if (editor.muted[ch->n_chanindex])
		return;

	updateFunk(ch);

	const uint8_t effect = (ch->n_cmd & 0xF00) >> 8;
	if ((ch->n_cmd & 0xFFF) > 0)
	{
		switch (effect)
		{
			case 0x0: arpeggio(ch);            break;
			case 0x1: portaUp(ch);             break;
			case 0x2: portaDown(ch);           break;
			case 0x3: tonePortamento(ch);      break;
			case 0x4: vibrato(ch);             break;
			case 0x5: tonePlusVolSlide(ch);    break;
			case 0x6: vibratoPlusVolSlide(ch); break;
			case 0xE: E_Commands(ch);          break;

			case 0x7:
			{
				paulaSetPeriod(ch->n_chanindex, ch->n_period);
				tremolo(ch);
			}
			break;

			case 0xA:
			{
				paulaSetPeriod(ch->n_chanindex, ch->n_period);
				volumeSlide(ch);
			}
			break;

			default: paulaSetPeriod(ch->n_chanindex, ch->n_period); break;
		}
	}

	if (effect != 0x7)
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

		if (!editor.muted[ch->n_chanindex])
		{
			paulaStartDMA(ch->n_chanindex);

			ch->syncAnalyzerVolume = ch->n_volume;
			ch->syncAnalyzerPeriod = ch->n_period;
			ch->syncFlags |= UPDATE_ANALYZER;

			setVUMeterHeight(ch);
		}
		else
		{
			paulaStopDMA(ch->n_chanindex);
		}
	}

	checkMoreEffects(ch);
}

static void checkMetronome(moduleChannel_t *ch, note_t *note)
{
	if (editor.metroFlag && editor.metroChannel > 0)
	{
		if (ch->n_chanindex == editor.metroChannel-1 && (song->row % editor.metroSpeed) == 0)
		{
			note->sample = 0x1F;
			note->period = (((song->row / editor.metroSpeed) % editor.metroSpeed) == 0) ? 160 : 214;
		}
	}
}

static void playVoice(moduleChannel_t *ch)
{
	uint8_t cmd;
	moduleSample_t *s;
	note_t note;

	if (ch->n_note == 0 && ch->n_cmd == 0)
		paulaSetPeriod(ch->n_chanindex, ch->n_period);

	note = song->patterns[modPattern][(song->row * AMIGA_VOICES) + ch->n_chanindex];
	checkMetronome(ch, &note);

	ch->n_note = note.period;
	ch->n_cmd = (note.command << 8) | note.param;

	if (note.sample >= 1 && note.sample <= 31) // SAFETY BUG FIX: don't handle sample-numbers >31
	{
		ch->n_samplenum = note.sample - 1;
		s = &song->samples[ch->n_samplenum];

		ch->n_start = &song->sampleData[s->offset];
		ch->n_finetune = s->fineTune & 0xF;
		ch->n_volume = s->volume;
		ch->n_length = s->length >> 1;
		ch->n_replen = s->loopLength >> 1;

		const uint16_t repeat = s->loopStart >> 1;
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

		// non-PT2 quirk
		if (ch->n_length == 0)
			ch->n_loopstart = ch->n_wavestart = &song->sampleData[RESERVED_SAMPLE_OFFSET]; // 128K reserved sample
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
			cmd = (ch->n_cmd & 0xF00) >> 8;
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

static void nextPosition(void)
{
	song->row = pBreakPosition;
	pBreakPosition = 0;
	posJumpAssert = false;

	if (editor.playMode != PLAY_MODE_PATTERN ||
		(editor.currMode == MODE_RECORD && editor.recordMode != RECORD_PATT))
	{
		if (editor.stepPlayEnabled)
		{
			doStopIt(true);

			editor.stepPlayEnabled = false;
			editor.stepPlayBackwards = false;

			if (!editor.isWAVRendering && !editor.isSMPRendering)
				song->currRow = song->row;

			return;
		}

		modOrder = (modOrder + 1) & 0x7F;
		if (modOrder >= song->header.numOrders)
		{
			modOrder = 0;
			modHasBeenPlayed = true;

			if (config.compoMode) // stop song for music competitions playing
			{
				doStopIt(true);
				turnOffVoices();

				song->currOrder = 0;
				song->currRow = song->row = 0;
				song->currPattern = modPattern = (int8_t)song->header.order[0];

				editor.currPatternDisp = &song->currPattern;
				editor.currPosEdPattDisp = &song->currPattern;
				editor.currPatternDisp = &song->currPattern;
				editor.currPosEdPattDisp = &song->currPattern;

				if (ui.posEdScreenShown)
					ui.updatePosEd = true;

				ui.updateSongPos = true;
				ui.updateSongPattern = true;
				ui.updateCurrPattText = true;
			}
		}

		modPattern = (int8_t)song->header.order[modOrder];
		if (modPattern > MAX_PATTERNS-1)
			modPattern = MAX_PATTERNS-1;

		updateUIPositions = true;
	}
}

bool intMusic(void)
{
	uint8_t i;
	uint16_t *patt;
	moduleChannel_t *c;

	if (modBPM >= 32 && modBPM <= 255)
		editor.musicTime64 += musicTimeTab64[modBPM-32]; // for playback counter

	if (updateUIPositions)
	{
		updateUIPositions = false;

		if (!editor.isWAVRendering && !editor.isSMPRendering)
		{
			if (editor.playMode != PLAY_MODE_PATTERN)
			{
				song->currOrder = modOrder;
				song->currPattern = modPattern;

				patt = &song->header.order[modOrder];
				editor.currPatternDisp = patt;
				editor.currPosEdPattDisp = patt;
				editor.currPatternDisp = patt;
				editor.currPosEdPattDisp = patt;

				if (ui.posEdScreenShown)
					ui.updatePosEd = true;

				ui.updateSongPos = true;
				ui.updateSongPattern = true;
				ui.updateCurrPattText = true;
			}
		}
	}

	// PT quirk: CIA refreshes its timer values on the next interrupt, so do the real tempo change here
	if (setBPMFlag != 0)
	{
		modSetTempo(setBPMFlag, false);
		setBPMFlag = 0;
	}

	if (editor.isWAVRendering && song->tick == 0)
		editor.rowVisitTable[(modOrder * MOD_ROWS) + song->row] = true;

	if (!editor.stepPlayEnabled)
		song->tick++;

	if (song->tick >= song->speed || editor.stepPlayEnabled)
	{
		song->tick = 0;

		if (pattDelTime2 == 0)
		{
			for (i = 0; i < AMIGA_VOICES; i++)
			{
				c = &song->channels[i];

				playVoice(c);
				paulaSetVolume(i, c->n_volume);

				// these take effect after the current DMA cycle is done
				paulaSetData(i, c->n_loopstart);
				paulaSetLength(i, c->n_replen);
			}
		}
		else
		{
			for (i = 0; i < AMIGA_VOICES; i++)
				checkEffects(&song->channels[i]);
		}

		if (!editor.isWAVRendering && !editor.isSMPRendering)
		{
			song->currRow = song->row;
			ui.updatePatternData = true;
		}

		if (!editor.stepPlayBackwards)
		{
			song->row++;
			song->rowsCounter++;
		}

		if (pattDelTime > 0)
		{
			pattDelTime2 = pattDelTime;
			pattDelTime = 0;
		}

		if (pattDelTime2 > 0)
		{
			if (--pattDelTime2 > 0)
				song->row--;
		}

		if (pBreakFlag)
		{
			song->row = pBreakPosition;
			pBreakPosition = 0;
			pBreakFlag = false;
		}

		if (editor.blockMarkFlag)
			ui.updateStatusText = true;

		if (editor.stepPlayEnabled)
		{
			doStopIt(true);

			song->currRow = song->row & 0x3F;
			ui.updatePatternData = true;

			editor.stepPlayEnabled = false;
			editor.stepPlayBackwards = false;
			ui.updatePatternData = true;

			return true;
		}

		if (song->row >= MOD_ROWS || posJumpAssert)
		{
			if (editor.isSMPRendering)
				modHasBeenPlayed = true;

			nextPosition();
		}

		if (editor.isWAVRendering && !pattDelTime2 && editor.rowVisitTable[(modOrder * MOD_ROWS) + song->row])
			modHasBeenPlayed = true;
	}
	else
	{
		for (i = 0; i < AMIGA_VOICES; i++)
			checkEffects(&song->channels[i]);

		if (posJumpAssert)
			nextPosition();
	}

	if ((editor.isSMPRendering || editor.isWAVRendering) && modHasBeenPlayed && song->tick == song->speed-1)
	{
		modHasBeenPlayed = false;
		return false;
	}

	return true;
}

void modSetPattern(uint8_t pattern)
{
	modPattern = pattern;
	song->currPattern = modPattern;
	ui.updateCurrPattText = true;
}

void modSetPos(int16_t order, int16_t row)
{
	int16_t posEdPos;

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

			posEdPos = song->currOrder;
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

void modSetTempo(uint16_t bpm, bool doLockAudio)
{
	if (bpm < 32)
		return;

	const bool audioWasntLocked = !audio.locked;

	if (doLockAudio && audioWasntLocked)
		lockAudio();

	modBPM = bpm;
	if (!editor.isSMPRendering && !editor.isWAVRendering)
	{
		song->currBPM = bpm;
		ui.updateSongBPM = true;
	}

	bpm -= 32; // 32..255 -> 0..223

	double dSamplesPerTick;
	if (editor.isSMPRendering)
		dSamplesPerTick = editor.pat2SmpHQ ? audio.bpmTab28kHz[bpm] : audio.bpmTab22kHz[bpm];
	else if (editor.isWAVRendering)
		dSamplesPerTick = audio.bpmTabMod2Wav[bpm];
	else
		dSamplesPerTick = audio.bpmTab[bpm];

	audio.dSamplesPerTick = dSamplesPerTick;

	// calculate tick time length for audio/video sync timestamp
	const uint64_t tickTimeLen64 = audio.tickTimeLengthTab[bpm];
	const uint32_t tickTimeLen = tickTimeLen64 >> 32;
	const uint32_t tickTimeLenFrac = tickTimeLen64 & 0xFFFFFFFF;

	setSyncTickTimeLen(tickTimeLen, tickTimeLenFrac);

	if (doLockAudio && audioWasntLocked)
		unlockAudio();
}

void modStop(void)
{
	editor.songPlaying = false;
	turnOffVoices();

	if (song != NULL)
	{
		for (int32_t i = 0; i < AMIGA_VOICES; i++)
		{
			moduleChannel_t *c = &song->channels[i];

			c->n_wavecontrol = 0;
			c->n_glissfunk = 0;
			c->n_finetune = 0;
			c->n_loopcount = 0;
		}
	}

	pBreakFlag = false;
	pattDelTime = 0;
	pattDelTime2 = 0;
	pBreakPosition = 0;
	posJumpAssert = false;
	modHasBeenPlayed = true;
}

void playPattern(int8_t startRow)
{
	if (!editor.stepPlayEnabled)
		pointerSetMode(POINTER_MODE_PLAY, DO_CARRY);

	audio.dTickSampleCounter = 0.0; // zero tick sample counter so that it will instantly initiate a tick

	song->currRow = song->row = startRow & 0x3F;
	song->tick = song->speed;

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
	uint8_t oldPlayMode, oldMode;

	const bool audioWasntLocked = !audio.locked;
	if (audioWasntLocked)
		lockAudio();

	doStopIt(false);
	turnOffVoices();
	audio.dTickSampleCounter = 0.0; // zero tick sample counter so that it will instantly initiate a tick

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

	oldPlayMode = editor.playMode;
	oldMode = editor.currMode;

	editor.playMode = oldPlayMode;
	editor.currMode = oldMode;

	song->tick = song->speed;
	modHasBeenPlayed = false;
	editor.songPlaying = true;
	editor.didQuantize = false;
	editor.musicTime64 = 0;

	if (audioWasntLocked)
		unlockAudio();

	if (!editor.isSMPRendering && !editor.isWAVRendering)
	{
		ui.updateSongPos = true;
		ui.updatePatternData = true;
		ui.updateSongPattern = true;
		ui.updateCurrPattText = true;
	}
}

void clearSong(void)
{
	uint8_t i;
	moduleChannel_t *ch;

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

	for (i = 0; i < MAX_PATTERNS; i++)
		memset(song->patterns[i], 0, (MOD_ROWS * AMIGA_VOICES) * sizeof (note_t));

	for (i = 0; i < AMIGA_VOICES; i++)
	{
		ch = &song->channels[i];

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

	setLEDFilter(false, true); // real PT doesn't do this there, but that's insane
	updateCurrSample();

	ui.updateSongSize = true;
	renderMuteButtons();
	updateWindowTitle(MOD_IS_MODIFIED);
}

void clearSamples(void)
{
	moduleSample_t *s;

	assert(song != NULL);
	if (song == NULL)
		return;

	for (uint8_t i = 0; i < MOD_SAMPLES; i++)
	{
		s = &song->samples[i];

		s->fineTune = 0;
		s->length = 0;
		s->loopLength = 2;
		s->loopStart = 0;
		s->volume = 0;

		memset(s->text, 0, sizeof (s->text));
	}

	memset(song->sampleData, 0, (MOD_SAMPLES + 1) * MAX_SAMPLE_LEN);

	editor.currSample = 0;
	editor.hiLowInstr = 0;
	editor.sampleZero = false;
	ui.editOpScreenShown = false;
	ui.aboutScreenShown = false;
	editor.blockMarkFlag = false;

	editor.samplePos = 0;
	updateCurrSample();

	updateWindowTitle(MOD_IS_MODIFIED);
}

void clearAll(void)
{
	clearSamples();
	clearSong();

	updateWindowTitle(MOD_NOT_MODIFIED);
}

void modFree(void)
{
	uint8_t i;

	if (song == NULL)
		return; // not allocated

	const bool audioWasntLocked = !audio.locked;
	if (audioWasntLocked)
		lockAudio();

	turnOffVoices();

	for (i = 0; i < MAX_PATTERNS; i++)
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
	audio.forceMixerOff = true;

	song->row = 0;
	song->currRow = 0;
	song->rowsCounter = 0;

	memset(editor.rowVisitTable, 0, MOD_ORDERS * MOD_ROWS); // for MOD2WAV

	if (editor.isSMPRendering)
	{
		modPlay(DONT_SET_PATTERN, DONT_SET_ORDER, DONT_SET_ROW);
	}
	else
	{
		song->currSpeed = 6;
		song->currBPM = 125;
		modSetSpeed(6);
		modSetTempo(125, true);

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

	memset((int8_t *)editor.vuMeterVolumes,0, sizeof (editor.vuMeterVolumes));
	memset((int8_t *)editor.realVuMeterVolumes, 0, sizeof (editor.realVuMeterVolumes));
	memset((int8_t *)editor.spectrumVolumes, 0, sizeof (editor.spectrumVolumes));

	memset(song->channels, 0, sizeof (song->channels));
	for (uint8_t i = 0; i < AMIGA_VOICES; i++)
		song->channels[i].n_chanindex = i;

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
	modHasBeenPlayed = false;
	audio.forceMixerOff = false;
}
