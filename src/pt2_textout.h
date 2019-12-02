#pragma once

#include <stdint.h>
#include <stdbool.h>

void charOut(uint32_t *frameBuffer, uint32_t xPos, uint32_t yPos, char ch, uint32_t color);
void charOutBg(uint32_t *frameBuffer, uint32_t xPos, uint32_t yPos, char ch, uint32_t fgColor, uint32_t bgColor);
void charOutBig(uint32_t *frameBuffer, uint32_t xPos, uint32_t yPos, char ch, uint32_t color);
void charOutBigBg(uint32_t *frameBuffer, uint32_t xPos, uint32_t yPos, char ch, uint32_t fgColor, uint32_t bgColor);
void textOut(uint32_t *frameBuffer, uint32_t xPos, uint32_t yPos, const char *text, uint32_t color);
void textOutTight(uint32_t *frameBuffer, uint32_t xPos, uint32_t yPos, const char *text, uint32_t color);
void textOutBg(uint32_t *frameBuffer, uint32_t xPos, uint32_t yPos, const char *text, uint32_t fgColor, uint32_t bgColor);
void textOutBig(uint32_t *frameBuffer, uint32_t xPos, uint32_t yPos, const char *text, uint32_t color);
void textOutBigBg(uint32_t *frameBuffer, uint32_t xPos, uint32_t yPos, const char *text, uint32_t fgColor, uint32_t bgColor);
void printOneHex(uint32_t *frameBuffer, uint32_t x, uint32_t y, uint32_t value, uint32_t fontColor);
void printTwoHex(uint32_t *frameBuffer, uint32_t x, uint32_t y, uint32_t value, uint32_t fontColor);
void printThreeHex(uint32_t *frameBuffer, uint32_t x, uint32_t y, uint32_t value, uint32_t fontColor);
void printFourHex(uint32_t *frameBuffer, uint32_t x, uint32_t y, uint32_t value, uint32_t fontColor);
void printFiveHex(uint32_t *frameBuffer, uint32_t x, uint32_t y, uint32_t value, uint32_t fontColor);
void printOneHexBig(uint32_t *frameBuffer, uint32_t x, uint32_t y, uint32_t value, uint32_t fontColor);
void printTwoHexBig(uint32_t *frameBuffer, uint32_t x, uint32_t y, uint32_t value, uint32_t fontColor);
void printSixDecimals(uint32_t *frameBuffer, uint32_t x, uint32_t y, uint32_t value, uint32_t fontColor);
void printTwoDecimals(uint32_t *frameBuffer, uint32_t x, uint32_t y, uint32_t value, uint32_t fontColor);
void printFourDecimals(uint32_t *frameBuffer, uint32_t x, uint32_t y, uint32_t value, uint32_t fontColor);
void printFiveDecimals(uint32_t *frameBuffer, uint32_t x, uint32_t y, uint32_t value, uint32_t fontColor);
void printThreeDecimals(uint32_t *frameBuffer, uint32_t x, uint32_t y, uint32_t value, uint32_t fontColor);
void printTwoDecimalsBig(uint32_t *frameBuffer, uint32_t x, uint32_t y, uint32_t value, uint32_t fontColor);
void printOneHexBg(uint32_t *frameBuffer, uint32_t x, uint32_t y, uint32_t value, uint32_t fontColor, uint32_t backColor);
void printTwoHexBg(uint32_t *frameBuffer, uint32_t x, uint32_t y, uint32_t value, uint32_t fontColor, uint32_t backColor);
void printThreeHexBg(uint32_t *frameBuffer, uint32_t x, uint32_t y, uint32_t value, uint32_t fontColor, uint32_t backColor);
void printFourHexBg(uint32_t *frameBuffer, uint32_t x, uint32_t y, uint32_t value, uint32_t fontColor, uint32_t backColor);
void printFiveHexBg(uint32_t *frameBuffer, uint32_t x, uint32_t y, uint32_t value, uint32_t fontColor, uint32_t backColor);
void printOneHexBigBg(uint32_t *frameBuffer, uint32_t x, uint32_t y, uint32_t value, uint32_t fontColor, uint32_t backColor);
void printTwoHexBigBg(uint32_t *frameBuffer, uint32_t x, uint32_t y, uint32_t value, uint32_t fontColor, uint32_t backColor);
void printSixDecimalsBg(uint32_t *frameBuffer, uint32_t x, uint32_t y, uint32_t value, uint32_t fontColor, uint32_t backColor);
void printTwoDecimalsBg(uint32_t *frameBuffer, uint32_t x, uint32_t y, uint32_t value, uint32_t fontColor, uint32_t backColor);
void printFourDecimalsBg(uint32_t *frameBuffer, uint32_t x, uint32_t y, uint32_t value, uint32_t fontColor, uint32_t backColor);
void printFiveDecimalsBg(uint32_t *frameBuffer, uint32_t x, uint32_t y, uint32_t value, uint32_t fontColor, uint32_t backColor);
void printThreeDecimalsBg(uint32_t *frameBuffer, uint32_t x, uint32_t y, uint32_t value, uint32_t fontColor, uint32_t backColor);
void printTwoDecimalsBigBg(uint32_t *frameBuffer, uint32_t x, uint32_t y, uint32_t value, uint32_t fontColor, uint32_t backColor);
void setPrevStatusMessage(void);
void setStatusMessage(const char *msg, bool carry);
void displayMsg(const char *msg);
void displayErrorMsg(const char *msg);
