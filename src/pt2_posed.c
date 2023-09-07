#include <stdint.h>
#include <stdbool.h>
#include "pt2_textout.h"
#include "pt2_visuals.h"
#include "pt2_helpers.h"
#include "pt2_replayer.h"
#include "pt2_bmp.h"
#include "pt2_mouse.h"
#include "pt2_edit.h"
#include "pt2_textedit.h"
#include "pt2_posed.h"

static char posEdNames[MAX_PATTERNS][16];

void posEdScrollDown(void)
{
	const uint16_t scrollAmount = mouse.rightButtonPressed ? 10 : 1;

	if (song->currPos < song->header.songLength-1)
	{
		int16_t pos = song->currPos + scrollAmount;
		if (pos > song->header.songLength-1)
			pos = song->header.songLength-1;

		modSetPos(pos, DONT_SET_ROW);
	}
}

void posEdScrollUp(void)
{
	const uint16_t scrollAmount = mouse.rightButtonPressed ? 10 : 1;

	if (song->currPos > 0)
	{
		int16_t pos = song->currPos - scrollAmount;
		if (pos < 0)
			pos = 0;

		modSetPos(pos, DONT_SET_ROW);
	}
}

void posEdPageUp(void)
{
	const uint16_t scrollAmount = POSED_LIST_SIZE;

	if (song->currPos > 0)
	{
		int16_t pos = song->currPos - scrollAmount;
		if (pos < 0)
			pos = 0;

		modSetPos(pos, DONT_SET_ROW);
	}
}

void posEdPageDown(void)
{
	const uint16_t scrollAmount = POSED_LIST_SIZE;

	if (song->currPos < song->header.songLength-1)
	{
		int16_t pos = song->currPos + scrollAmount;
		if (pos > song->header.songLength-1)
			pos = song->header.songLength-1;

		modSetPos(pos, DONT_SET_ROW);
	}
}

void posEdScrollToTop(void)
{
	if (song->currPos > 0)
		modSetPos(0, DONT_SET_ROW);
}

void posEdScrollToBottom(void)
{
	if (song->currPos < song->header.songLength-1)
		modSetPos(song->header.songLength-1, DONT_SET_ROW);
}

void posEdClearNames(void)
{
	memset(posEdNames, 0, sizeof (posEdNames));

	if (ui.posEdScreenShown)
		ui.updatePosEd = true;
}

void posEdEditName(void)
{
	if (editor.currMode == MODE_IDLE || editor.currMode == MODE_EDIT)
	{
		const uint8_t pattern = (uint8_t)song->header.patternTable[song->currPos & 0x7F];

		textEdit.textStartPtr = posEdNames[pattern];
		textEdit.textEndPtr = posEdNames[pattern] + 15;
		textEdit.numBlocks = 15;
		textEdit.cursorStartX = 184;
		textEdit.cursorStartY = 58;
		textEdit.scrollable = true;
		enterTextEditMode(PTB_PE_PATTNAME);
	}
}

void posEdInsert(void)
{
	if ((editor.currMode == MODE_IDLE || editor.currMode == MODE_EDIT) && song->header.songLength < 128)
	{
		for (int32_t i = 0; i < 127-song->currPos; i++)
			song->header.patternTable[127-i] = song->header.patternTable[(127-i)-1];
		song->header.patternTable[song->currPos] = 0;

		song->header.songLength++;
		if (song->currPos > song->header.songLength-1)
			editor.currPosEdPattDisp = &song->header.patternTable[song->header.songLength-1];

		updateWindowTitle(MOD_IS_MODIFIED);

		ui.updateSongSize = true;
		ui.updateSongLength = true;
		ui.updateSongPattern = true;

		if (ui.posEdScreenShown)
			ui.updatePosEd = true;
	}
}

void posEdDelete(void)
{
	if ((editor.currMode == MODE_IDLE || editor.currMode == MODE_EDIT) && song->header.songLength > 1)
	{
		for (int32_t i = 0; i < 128-song->currPos; i++)
			song->header.patternTable[song->currPos+i] = song->header.patternTable[song->currPos+i+1];
		song->header.patternTable[127] = 0;

		song->header.songLength--;
		if (song->currPos > song->header.songLength-1)
			editor.currPosEdPattDisp = &song->header.patternTable[song->header.songLength-1];

		updateWindowTitle(MOD_IS_MODIFIED);

		ui.updateSongSize = true;
		ui.updateSongLength = true;
		ui.updateSongPattern = true;

		if (ui.posEdScreenShown)
			ui.updatePosEd = true;
	}
}

void posEdToggle(void)
{
	ui.posEdScreenShown ^= 1;
	if (ui.posEdScreenShown)
	{
		renderPosEdScreen();
		ui.updatePosEd = true;
	}
	else
	{
		displayMainScreen();
	}
}

void renderPosEdScreen(void)
{
	blit32(120, 0, 200, 99, posEdBMP);
	ui.updatePosEd = true;
}

static void drawPosEdName(int32_t entry, int32_t y, bool selected)
{
	const uint8_t pattern = (uint8_t)song->header.patternTable[entry & 0x7F];
	for (int32_t i = 0; i < 15; i++)
	{
		const int32_t x = 184 + (i * FONT_CHAR_W);

		char ch;
		
		if (ui.editTextFlag)
			ch = posEdNames[pattern][textEdit.scrollOffset + i];
		else
			ch = posEdNames[pattern][i];

		if (ch == '\0')
			break;

		if (selected)
			charOut(x, y, ch, video.palette[PAL_GENTXT]);
		else
			charOut(x, y, ch, video.palette[PAL_QADSCP]);
	}
}

void updatePosEd(void)
{
	if (!ui.updatePosEd || !ui.posEdScreenShown || ui.askBoxShown)
		return;

	ui.updatePosEd = false;

	// clear entries first
	fillRect(128, 23, (22*FONT_CHAR_W)-1, ((FONT_CHAR_H+1)*5)-1, video.palette[PAL_BACKGRD]);
	fillRect(128, 53, (22*FONT_CHAR_W)-1, FONT_CHAR_H, video.palette[PAL_GENBKG]);
	fillRect(128, 59, (22*FONT_CHAR_W)-1, ((FONT_CHAR_H+1)*6)-1, video.palette[PAL_BACKGRD]);

	for (int32_t i = 0; i < POSED_LIST_SIZE; i++)
	{
		const int32_t entry = song->currPos - (5 - i);
		if (entry < 0 || entry >= song->header.songLength)
			continue;

		const int32_t pattern = song->header.patternTable[entry];
		const int32_t yPos = 23 + (i * (FONT_CHAR_H+1));
		const bool selected = (i == 5);
		const uint32_t textColor = selected ? video.palette[PAL_GENTXT] : video.palette[PAL_QADSCP];

		printThreeDecimals(128, yPos, entry, textColor);
		printTwoDecimals(160, yPos, selected ? *editor.currPosEdPattDisp : pattern, textColor);
		drawPosEdName(entry, yPos, selected);
	}

	// kludge to fix bottom part of text edit marker
	if (ui.editTextFlag && (textEdit.object == PTB_PE_PATT || textEdit.object == PTB_PE_PATTNAME))
		renderTextEditCursor();
}
