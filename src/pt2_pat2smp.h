#pragma once

#include <stdint.h>

void pat2SmpDrawNote(void);
void pat2SmpDrawFinetune(void);
void pat2SmpDrawFrequency(void);
void pat2SmpDrawStartRow(void);
void pat2SmpDrawRows(void);
void pat2SmpCalculateFreq(void);
void pat2SmpNoteUp(void);
void pat2SmpNoteDown(void);
void pat2SmpSetFinetune(uint8_t finetune);
void pat2SmpFinetuneUp(void);
void pat2SmpFinetuneDown(void);
void pat2SmpStartRowUp(void);
void pat2SmpStartRowDown(void);
void pat2SmpRowsUp(void);
void pat2SmpRowsDown(void);
void pat2SmpRender(void);
