// 
// 
// 

#include "objects.h"
#include <SD.h>


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




