// 
// 
// 

#include "objects.h"

int64_t pumpStart = 0;

Config myConfig;
FileData data(&SD, "/configuration.dat", 'B', &myConfig, sizeof(myConfig));

char tstr[32] = "";
bool res = false;
FastBot bot(BOT_TOKEN);
RTC_DS3231 rtc;
HumiditySensors hs;
bool dropped = false;

bool rainNow = false;
bool nightNow = false;
byte oldMode[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };


int64_t getUnixTime() {
  if (bot.timeSynced()) {
    return bot.getUnix() + 3600 * 3;
  } else {
    DateTime now = rtc.now();
    return now.unixtime();
  }
}

void (*p_sendTelegramFunction)(String text);   // указатель на p_function


void attachSendFunction(void (*function)(String text)) { // передача указателя на функцию
  p_sendTelegramFunction = *function;
}


void sendTelegramStatus(String text)
{
  (*p_sendTelegramFunction)(text);
}


