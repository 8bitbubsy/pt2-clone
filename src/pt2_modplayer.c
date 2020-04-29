/* Very accurate C port of ProTracker 2.3D's replayer by 8bitbubsy, slightly modified.
** Earlier versions of the PT clone used a completely different and less accurate replayer.
*/

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
#include "pt2_modloader.h"
#include "pt2_config.h"
#include "pt2_sampler.h"
#include "pt2_visuals.h"
#include "pt2_textout.h"
#include "pt2_scopes.h"

static bool posJumpAssert, pBreakFlag, updateUIPositions, modHasBeenPlayed;
static int8_t pBreakPosition, oldRow, modPattern;
static uint8_t pattDelTime, setBPMFlag, lowMask = 0xFF, pattDelTime2, oldSpeed;
static int16_t modOrder, oldPattern, oldOrder;
static uint16_t modBPM, oldBPM;

static const int8_t vuMeterHeights[65] =
{
	 0,  0,  1,  2,  2,  3,  4,  5,
	 5,  6,  7,  8,  8,  9, 10, 11,
	11, 12, 13, 14, 14, 15, 16, 17,
	17, 18, 19, 20, 20, 21, 22, 23,
	23, 24, 25, 26, 26, 27, 28, 29,
	29, 30, 31, 32, 32, 33, 34, 35,
	35, 36, 37, 38, 38, 39, 40, 41,
	41, 42, 43, 44, 44, 45, 46, 47,
	47
};

static const uint8_t funkTable[16] = // EFx (FunkRepeat/InvertLoop)
{
	0x00, 0x05, 0x06, 0x07, 0x08, 0x0A, 0x0B, 0x0D,
	0x10, 0x13, 0x16, 0x1A, 0x20, 0x2B, 0x40, 0x80
};

void modSetSpeed(uint8_t speed)
{
	editor.modSpeed = speed;
	modEntry->currSpeed = speed;
	editor.modTick = 0;
}

void doStopIt(bool resetPlayMode)
{
	moduleChannel_t *c;
	uint8_t i;

	editor.songPlaying = false;

	resetCachedMixerPeriod();

	pattDelTime = 0;
	pattDelTime2 = 0;

	if (resetPlayMode)
	{
		editor.playMode = PLAY_MODE_NORMAL;
		editor.currMode = MODE_IDLE;

		pointerSetMode(POINTER_MODE_IDLE, DO_CARRY);
	}

	for (i = 0; i < AMIGA_VOICES; i++)
	{
		c = &modEntry->channels[i];
		c->n_wavecontrol = 0;
		c->n_glissfunk = 0;
		c->n_finetune = 0;
		c->n_loopcount = 0;
	}
}

void setPattern(int16_t pattern)
{
	if (pattern > MAX_PATTERNS-1)
		pattern = MAX_PATTERNS-1;

	modEntry->currPattern = modPattern = (int8_t)pattern;
}

void storeTempVariables(void) // this one is accessed in other files, so non-static
{
	oldBPM = modEntry->currBPM;
	oldRow = modEntry->currRow;
	oldOrder = modEntry->currOrder;
	oldSpeed = modEntry->currSpeed;
	oldPattern = modEntry->currPattern;
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

	editor.vuMeterVolumes[ch->n_chanindex] = vuMeterHeights[vol];
}

static void updateFunk(moduleChannel_t *ch)
{
	int8_t funkspeed;

	funkspeed = ch->n_glissfunk >> 4;
	if (funkspeed == 0)
		return;

	ch->n_funkoffset += funkTable[funkspeed];
	if (ch->n_funkoffset >= 128)
	{
		ch->n_funkoffset = 0;

		if (ch->n_loopstart != NULL && ch->n_wavestart != NULL) // SAFETY BUG FIX
		{
			if (++ch->n_wavestart >= ch->n_loopstart+ch->n_replen)
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

	if (editor.modTick != 0)
		return;

	if ((ch->n_cmd & 0xF) == 0)
	{
		ch->n_pattpos = modEntry->row;
	}
	else
	{
		if (ch->n_loopcount == 0)
		{
			ch->n_loopcount = ch->n_cmd & 0xF;
		}
		else
		{
			if (--ch->n_loopcount == 0)
				return;
		}

		pBreakPosition = ch->n_pattpos;
		pBreakFlag = 1;

		if (editor.isWAVRendering)
		{
			for (tempParam = pBreakPosition; tempParam <= modEntry->row; tempParam++)
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
	(void)ch; // this effect is *horrible* and never used, I'm not implementing it.
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

	updateSpectrumAnalyzer(ch->n_volume, ch->n_period);
	setVUMeterHeight(ch);
}

static void retrigNote(moduleChannel_t *ch)
{
	if ((ch->n_cmd & 0xF) > 0)
	{
		if (editor.modTick == 0 && (ch->n_note & 0xFFF) > 0)
				return;

		if (editor.modTick % (ch->n_cmd & 0xF) == 0)
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
	if (editor.modTick == 0)
	{
		ch->n_volume += ch->n_cmd & 0xF;
		if (ch->n_volume > 64)
			ch->n_volume = 64;
	}
}

static void volumeFineDown(moduleChannel_t *ch)
{
	if (editor.modTick == 0)
	{
		ch->n_volume -= ch->n_cmd & 0xF;
		if (ch->n_volume < 0)
			ch->n_volume = 0;
	}
}

static void noteCut(moduleChannel_t *ch)
{
	if (editor.modTick == (ch->n_cmd & 0xF))
		ch->n_volume = 0;
}

static void noteDelay(moduleChannel_t *ch)
{
	if (editor.modTick == (ch->n_cmd & 0xF) && (ch->n_note & 0xFFF) > 0)
		doRetrg(ch);
}

static void patternDelay(moduleChannel_t *ch)
{
	if (editor.modTick == 0 && pattDelTime2 == 0)
		pattDelTime = (ch->n_cmd & 0xF) + 1;
}

static void funkIt(moduleChannel_t *ch)
{
	if (editor.modTick == 0)
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
		editor.modTick = 0;

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

		pointerSetMode(POINTER_MODE_IDLE, DO_CARRY);
	}
}

static void arpeggio(moduleChannel_t *ch)
{
	uint8_t arpTick, arpNote;
	const int16_t *periods;

	assert(editor.modTick < 32);
	arpTick = arpTickTable[editor.modTick]; // 0, 1, 2

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
	setLEDFilter(!(ch->n_cmd & 1));
}

static void finePortaUp(moduleChannel_t *ch)
{
	if (editor.modTick == 0)
	{
		lowMask = 0xF;
		portaUp(ch);
	}
}

static void finePortaDown(moduleChannel_t *ch)
{
	if (editor.modTick == 0)
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

static void vibratoNoChange(moduleChannel_t *ch)
{
	uint8_t vibratoTemp;
	int16_t vibratoData;

	vibratoTemp = (ch->n_vibratopos / 4) & 31;
	vibratoData = ch->n_wavecontrol & 3;

	if (vibratoData == 0)
	{
		vibratoData = vibratoTable[vibratoTemp];
	}
	else
	{
		if (vibratoData == 1)
		{
			if (ch->n_vibratopos < 0)
				vibratoData = 255 - (vibratoTemp * 8);
			else
				vibratoData = vibratoTemp * 8;
		}
		else
		{
			vibratoData = 255;
		}
	}

	vibratoData = (vibratoData * (ch->n_vibratocmd & 0xF)) / 128;

	if (ch->n_vibratopos < 0)
		vibratoData = ch->n_period - vibratoData;
	else
		vibratoData = ch->n_period + vibratoData;

	paulaSetPeriod(ch->n_chanindex, vibratoData);

	ch->n_vibratopos += ((ch->n_vibratocmd >> 4) * 4);
}

static void vibrato(moduleChannel_t *ch)
{
	if ((ch->n_cmd & 0xFF) > 0)
	{
		if ((ch->n_cmd & 0x0F) > 0)
			ch->n_vibratocmd = (ch->n_vibratocmd & 0xF0) | (ch->n_cmd & 0x0F);

		if ((ch->n_cmd & 0xF0) > 0)
			ch->n_vibratocmd = (ch->n_cmd & 0xF0) | (ch->n_vibratocmd & 0x0F);
	}

	vibratoNoChange(ch);
}

static void tonePlusVolSlide(moduleChannel_t *ch)
{
	tonePortNoChange(ch);
	volumeSlide(ch);
}

static void vibratoPlusVolSlide(moduleChannel_t *ch)
{
	vibratoNoChange(ch);
	volumeSlide(ch);
}

static void tremolo(moduleChannel_t *ch)
{
	int8_t tremoloTemp;
	int16_t tremoloData;

	if ((ch->n_cmd & 0xFF) > 0)
	{
		if ((ch->n_cmd & 0x0F) > 0)
			ch->n_tremolocmd = (ch->n_tremolocmd & 0xF0) | (ch->n_cmd & 0x0F);

		if ((ch->n_cmd & 0xF0) > 0)
			ch->n_tremolocmd = (ch->n_cmd & 0xF0) | (ch->n_tremolocmd & 0x0F);
	}

	tremoloTemp = (ch->n_tremolopos / 4) & 31;
	tremoloData = (ch->n_wavecontrol >> 4) & 3;

	if (!tremoloData)
	{
		tremoloData = vibratoTable[tremoloTemp];
	}
	else
	{
		if (tremoloData == 1)
		{
			if (ch->n_vibratopos < 0) // PT bug, should've been n_tremolopos
				tremoloData = 255 - (tremoloTemp * 8);
			else
				tremoloData = tremoloTemp * 8;
		}
		else
		{
			tremoloData = 255;
		}
	}

	tremoloData = (tremoloData * (ch->n_tremolocmd & 0xF)) / 64;

	if (ch->n_tremolopos < 0)
	{
		tremoloData = ch->n_volume - tremoloData;
		if (tremoloData < 0)
			tremoloData = 0;
	}
	else
	{
		tremoloData = ch->n_volume + tremoloData;
		if (tremoloData > 64)
			tremoloData = 64;
	}

	paulaSetVolume(ch->n_chanindex, tremoloData);

	ch->n_tremolopos += (ch->n_tremolocmd >> 4) * 4;
}

static void sampleOffset(moduleChannel_t *ch)
{
	uint16_t newOffset;

	if ((ch->n_cmd & 0xFF) > 0)
		ch->n_sampleoffset = ch->n_cmd & 0xFF;

	newOffset = ch->n_sampleoffset << 7;

	if ((int16_t)newOffset < (int16_t)ch->n_length)
	{
		ch->n_length -= newOffset;
		ch->n_start += newOffset*2;
	}
	else
	{
		ch->n_length = 1;
	}
}

static void E_Commands(moduleChannel_t *ch)
{
	uint8_t cmd;

	cmd = (ch->n_cmd & 0xF0) >> 4;
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
	uint8_t effect;

	if (editor.muted[ch->n_chanindex])
		return;

	updateFunk(ch);

	effect = (ch->n_cmd & 0xF00) >> 8;

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
	uint8_t i;
	uint16_t note;

	note = ch->n_note & 0xFFF;
	for (i = 0; i < 37; i++)
	{
		// periodTable[36] = 0, so i=36 is safe
		if (note >= periodTable[i])
			break;
	}

	// BUG: yes it's 'safe' if i=37 because of padding at the end of period table
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
			updateSpectrumAnalyzer(ch->n_volume, ch->n_period);
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
		if (ch->n_chanindex == editor.metroChannel-1 && (modEntry->row % editor.metroSpeed) == 0)
		{
			note->sample = 0x1F;
			note->period = (((modEntry->row / editor.metroSpeed) % editor.metroSpeed) == 0) ? 160 : 214;
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

	note = modEntry->patterns[modPattern][(modEntry->row * AMIGA_VOICES) + ch->n_chanindex];
	checkMetronome(ch, &note);

	ch->n_note = note.period;
	ch->n_cmd = (note.command << 8) | note.param;

	if (note.sample >= 1 && note.sample <= 31) // SAFETY BUG FIX: don't handle sample-numbers >31
	{
		ch->n_samplenum = note.sample - 1;
		s = &modEntry->samples[ch->n_samplenum];

		ch->n_start = &modEntry->sampleData[s->offset];
		ch->n_finetune = s->fineTune;
		ch->n_volume = s->volume;
		ch->n_length = s->length / 2;
		ch->n_replen = s->loopLength / 2;

		if (s->loopStart > 0)
		{
			ch->n_loopstart = ch->n_start + s->loopStart;
			ch->n_wavestart = ch->n_loopstart;
			ch->n_length = (s->loopStart / 2) + ch->n_replen;
		}
		else
		{
			ch->n_loopstart = ch->n_start;
			ch->n_wavestart = ch->n_start;
		}

		// non-PT2 quirk
		if (ch->n_length == 0)
			ch->n_loopstart = ch->n_wavestart = &modEntry->sampleData[RESERVED_SAMPLE_OFFSET]; // dummy sample
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
	modEntry->row = pBreakPosition;
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
				modEntry->currRow = modEntry->row;

			return;
		}

		modOrder = (modOrder + 1) & 0x7F;
		if (modOrder >= modEntry->head.orderCount)
		{
			modOrder = 0;
			modHasBeenPlayed = true;

			if (config.compoMode) // stop song for music competitions playing
			{
				doStopIt(true);
				turnOffVoices();

				modEntry->currOrder = 0;
				modEntry->currRow = modEntry->row = 0;
				modEntry->currPattern = modPattern = (int8_t)modEntry->head.order[0];

				editor.currPatternDisp = &modEntry->currPattern;
				editor.currPosEdPattDisp = &modEntry->currPattern;
				editor.currPatternDisp = &modEntry->currPattern;
				editor.currPosEdPattDisp = &modEntry->currPattern;

				if (ui.posEdScreenShown)
					ui.updatePosEd = true;

				ui.updateSongPos = true;
				ui.updateSongPattern = true;
				ui.updateCurrPattText = true;
			}
		}

		modPattern = (int8_t)modEntry->head.order[modOrder];
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

	if (modBPM > 0)
		editor.musicTime += (65536 / modBPM); // for playback counter

	if (updateUIPositions)
	{
		updateUIPositions = false;

		if (!editor.isWAVRendering && !editor.isSMPRendering)
		{
			if (editor.playMode != PLAY_MODE_PATTERN)
			{
				modEntry->currOrder = modOrder;
				modEntry->currPattern = modPattern;

				patt = &modEntry->head.order[modOrder];
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
		modSetTempo(setBPMFlag);
		setBPMFlag = 0;
	}

	if (editor.isWAVRendering && editor.modTick == 0)
		editor.rowVisitTable[(modOrder * MOD_ROWS) + modEntry->row] = true;

	if (!editor.stepPlayEnabled)
		editor.modTick++;

	if (editor.modTick >= editor.modSpeed || editor.stepPlayEnabled)
	{
		editor.modTick = 0;

		if (pattDelTime2 == 0)
		{
			for (i = 0; i < AMIGA_VOICES; i++)
			{
				c = &modEntry->channels[i];

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
				checkEffects(&modEntry->channels[i]);
		}

		if (!editor.isWAVRendering && !editor.isSMPRendering)
		{
			modEntry->currRow = modEntry->row;
			ui.updatePatternData = true;
		}

		if (!editor.stepPlayBackwards)
		{
			modEntry->row++;
			modEntry->rowsCounter++;
		}

		if (pattDelTime > 0)
		{
			pattDelTime2 = pattDelTime;
			pattDelTime = 0;
		}

		if (pattDelTime2 > 0)
		{
			if (--pattDelTime2 > 0)
				modEntry->row--;
		}

		if (pBreakFlag)
		{
			modEntry->row = pBreakPosition;
			pBreakPosition = 0;
			pBreakFlag = false;
		}

		if (editor.blockMarkFlag)
			ui.updateStatusText = true;

		if (editor.stepPlayEnabled)
		{
			doStopIt(true);

			modEntry->currRow = modEntry->row & 0x3F;
			ui.updatePatternData = true;

			editor.stepPlayEnabled = false;
			editor.stepPlayBackwards = false;
			ui.updatePatternData = true;

			return true;
		}

		if (modEntry->row >= MOD_ROWS || posJumpAssert)
		{
			if (editor.isSMPRendering)
				modHasBeenPlayed = true;

			nextPosition();
		}

		if (editor.isWAVRendering && !pattDelTime2 && editor.rowVisitTable[(modOrder * MOD_ROWS) + modEntry->row])
			modHasBeenPlayed = true;
	}
	else
	{
		for (i = 0; i < AMIGA_VOICES; i++)
			checkEffects(&modEntry->channels[i]);

		if (posJumpAssert)
			nextPosition();
	}

	if ((editor.isSMPRendering || editor.isWAVRendering) && modHasBeenPlayed && editor.modTick == editor.modSpeed-1)
	{
		modHasBeenPlayed = false;
		return false;
	}

	return true;
}

void modSetPattern(uint8_t pattern)
{
	modPattern = pattern;
	modEntry->currPattern = modPattern;
	ui.updateCurrPattText = true;
}

void modSetPos(int16_t order, int16_t row)
{
	int16_t posEdPos;

	if (row != -1)
	{
		row = CLAMP(row, 0, 63);

		editor.modTick = 0;
		modEntry->row = (int8_t)row;
		modEntry->currRow = (int8_t)row;
	}

	if (order != -1)
	{
		if (order >= 0)
		{
			modOrder = order;
			modEntry->currOrder = order;
			ui.updateSongPos = true;

			if (editor.currMode == MODE_PLAY && editor.playMode == PLAY_MODE_NORMAL)
			{
				modPattern = (int8_t)modEntry->head.order[order];
				if (modPattern > MAX_PATTERNS-1)
					modPattern = MAX_PATTERNS-1;

				modEntry->currPattern = modPattern;
				ui.updateCurrPattText = true;
			}

			ui.updateSongPattern = true;
			editor.currPatternDisp = &modEntry->head.order[modOrder];

			posEdPos = modEntry->currOrder;
			if (posEdPos > modEntry->head.orderCount-1)
				posEdPos = modEntry->head.orderCount-1;

			editor.currPosEdPattDisp = &modEntry->head.order[posEdPos];

			if (ui.posEdScreenShown)
				ui.updatePosEd = true;
		}
	}

	ui.updatePatternData = true;

	if (editor.blockMarkFlag)
		ui.updateStatusText = true;
}

void modSetTempo(uint16_t bpm)
{
	uint32_t smpsPerTick;

	if (bpm < 32)
		return;

	const bool audioWasntLocked = !audio.locked;
	if (audioWasntLocked)
		lockAudio();

	modBPM = bpm;
	if (!editor.isSMPRendering && !editor.isWAVRendering)
	{
		modEntry->currBPM = bpm;
		ui.updateSongBPM = true;
	}

	bpm -= 32; // 32..255 -> 0..223

	if (editor.isSMPRendering)
		smpsPerTick = editor.pat2SmpHQ ? audio.bpmTab28kHz[bpm] : audio.bpmTab22kHz[bpm];
	else if (editor.isWAVRendering)
		smpsPerTick = audio.bpmTabMod2Wav[bpm];
	else
		smpsPerTick = audio.bpmTab[bpm];

	mixerSetSamplesPerTick(smpsPerTick);

	if (audioWasntLocked)
		unlockAudio();
}

void modStop(void)
{
	moduleChannel_t *ch;

	editor.songPlaying = false;
	turnOffVoices();

	for (uint8_t i = 0; i < AMIGA_VOICES; i++)
	{
		ch = &modEntry->channels[i];

		ch->n_wavecontrol = 0;
		ch->n_glissfunk = 0;
		ch->n_finetune = 0;
		ch->n_loopcount = 0;
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
	modEntry->row = startRow & 0x3F;
	modEntry->currRow  = modEntry->row;
	editor.modTick = 0;
	editor.playMode = PLAY_MODE_PATTERN;
	editor.currMode = MODE_PLAY;
	editor.didQuantize = false;

	if (!editor.stepPlayEnabled)
		pointerSetMode(POINTER_MODE_PLAY, DO_CARRY);

	editor.songPlaying = true;
	mixerClearSampleCounter();
}

void incPatt(void)
{
	if (++modPattern > MAX_PATTERNS-1)
		modPattern = 0;

	modEntry->currPattern = modPattern;

	ui.updatePatternData = true;
	ui.updateCurrPattText = true;
}

void decPatt(void)
{
	if (--modPattern < 0)
		modPattern = MAX_PATTERNS - 1;

	modEntry->currPattern = modPattern;

	ui.updatePatternData = true;
	ui.updateCurrPattText = true;
}

void modPlay(int16_t patt, int16_t order, int8_t row)
{
	uint8_t oldPlayMode, oldMode;

	doStopIt(false);
	turnOffVoices();
	mixerClearSampleCounter();

	if (row != -1)
	{
		if (row >= 0 && row <= 63)
		{
			modEntry->row = row;
			modEntry->currRow = row;
		}
	}
	else
	{
		modEntry->row = 0;
		modEntry->currRow = 0;
	}

	if (editor.playMode != PLAY_MODE_PATTERN)
	{
		if (modOrder >= modEntry->head.orderCount)
		{
			modOrder = 0;
			modEntry->currOrder = 0;
		}

		if (order >= 0 && order < modEntry->head.orderCount)
		{
			modOrder = order;
			modEntry->currOrder = order;
		}

		if (order >= modEntry->head.orderCount)
		{
			modOrder = 0;
			modEntry->currOrder = 0;
		}
	}

	if (patt >= 0 && patt <= MAX_PATTERNS-1)
		modEntry->currPattern = modPattern = (int8_t)patt;
	else
		modEntry->currPattern = modPattern = (int8_t)modEntry->head.order[modOrder];

	editor.currPatternDisp = &modEntry->head.order[modOrder];
	editor.currPosEdPattDisp = &modEntry->head.order[modOrder];

	oldPlayMode = editor.playMode;
	oldMode = editor.currMode;

	editor.playMode = oldPlayMode;
	editor.currMode = oldMode;

	editor.modTick = editor.modSpeed;
	modHasBeenPlayed = false;
	editor.songPlaying = true;
	editor.didQuantize = false;
	editor.musicTime = 0;

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

	if (modEntry != NULL)
	{
		memset(modEntry->head.order, 0, sizeof (modEntry->head.order));
		memset(modEntry->head.moduleTitle, 0, sizeof (modEntry->head.moduleTitle));

		editor.muted[0] = false;
		editor.muted[1] = false;
		editor.muted[2] = false;
		editor.muted[3] = false;

		editor.f6Pos = 0;
		editor.f7Pos = 16;
		editor.f8Pos = 32;
		editor.f9Pos = 48;
		editor.f10Pos = 63;

		editor.musicTime = 0;

		editor.metroFlag = false;
		editor.currSample = 0;
		editor.editMoveAdd = 1;
		editor.blockMarkFlag = false;
		editor.swapChannelFlag = false;

		modEntry->head.orderCount = 1;

		for (i = 0; i < MAX_PATTERNS; i++)
			memset(modEntry->patterns[i], 0, (MOD_ROWS * AMIGA_VOICES) * sizeof (note_t));

		for (i = 0; i < AMIGA_VOICES; i++)
		{
			ch = &modEntry->channels[i];

			ch->n_wavecontrol = 0;
			ch->n_glissfunk = 0;
			ch->n_finetune = 0;
			ch->n_loopcount = 0;
		}

		modSetPos(0, 0); // this also refreshes pattern data

		modEntry->currOrder = 0;
		modEntry->currPattern = 0;
		editor.currPatternDisp = &modEntry->head.order[0];
		editor.currPosEdPattDisp = &modEntry->head.order[0];

		modSetTempo(editor.initialTempo);
		modSetSpeed(editor.initialSpeed);

		setLEDFilter(false); // real PT doesn't do this there, but that's insane
		updateCurrSample();

		ui.updateSongSize = true;
		renderMuteButtons();
		updateWindowTitle(MOD_IS_MODIFIED);
	}
}

void clearSamples(void)
{
	moduleSample_t *s;

	if (modEntry == NULL)
		return;

	for (uint8_t i = 0; i < MOD_SAMPLES; i++)
	{
		s = &modEntry->samples[i];

		s->fineTune = 0;
		s->length = 0;
		s->loopLength = 2;
		s->loopStart = 0;
		s->volume = 0;

		memset(s->text, 0, sizeof (s->text));
	}

	memset(modEntry->sampleData, 0, (MOD_SAMPLES + 1) * MAX_SAMPLE_LEN);

	editor.currSample = 0;
	editor.keypadSampleOffset = 0;
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
	if (modEntry != NULL)
	{
		clearSamples();
		clearSong();
	}
}

void modFree(void)
{
	uint8_t i;

	if (modEntry == NULL)
		return; // not allocated

	const bool audioWasntLocked = !audio.locked;
	if (audioWasntLocked)
		lockAudio();

	turnOffVoices();

	for (i = 0; i < MAX_PATTERNS; i++)
	{
		if (modEntry->patterns[i] != NULL)
			free(modEntry->patterns[i]);
	}

	if (modEntry->sampleData != NULL)
		free(modEntry->sampleData);

	free(modEntry);
	modEntry = NULL;

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

	modEntry->row = 0;
	modEntry->currRow = 0;
	modEntry->rowsCounter = 0;

	memset(editor.rowVisitTable, 0, MOD_ORDERS * MOD_ROWS); // for MOD2WAV

	if (editor.isSMPRendering)
	{
		modPlay(DONT_SET_PATTERN, DONT_SET_ORDER, DONT_SET_ROW);
	}
	else
	{
		modEntry->currSpeed = 6;
		modEntry->currBPM = 125;
		modSetSpeed(6);
		modSetTempo(125);

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

	memset(modEntry->channels, 0, sizeof (modEntry->channels));
	for (uint8_t i = 0; i < AMIGA_VOICES; i++)
		modEntry->channels[i].n_chanindex = i;

	modOrder = oldOrder;
	modPattern = (int8_t)oldPattern;

	modEntry->row = oldRow;
	modEntry->currRow = oldRow;
	modEntry->currBPM = oldBPM;
	modEntry->currOrder = oldOrder;
	modEntry->currPattern = oldPattern;

	editor.currPosDisp = &modEntry->currOrder;
	editor.currEditPatternDisp = &modEntry->currPattern;
	editor.currPatternDisp = &modEntry->head.order[modEntry->currOrder];
	editor.currPosEdPattDisp = &modEntry->head.order[modEntry->currOrder];

	modSetSpeed(oldSpeed);
	modSetTempo(oldBPM);

	doStopIt(true);

	editor.modTick = 0;
	modHasBeenPlayed = false;
	audio.forceMixerOff = false;
}
