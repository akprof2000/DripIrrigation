// HumidityControl.h
#pragma GCC system_header

#ifndef _TELEGRAM_h
#define _TELEGRAM_h

#include <arduino.h>


void botInit();
void reConnection();
void dropCDCard();
void connectCDCard();

int64_t getUnixTime();

void timeFixed();
String IntWith2Zero(int data);
void sendStatus(String text);

#endif