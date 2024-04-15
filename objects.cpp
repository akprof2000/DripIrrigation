// 
// 
// 

#include "objects.h"

Config myConfig;
FileData data(&SD, "/configuration.dat", 'B', &myConfig, sizeof(myConfig));

char tstr[32] = "";
bool res = false;
FastBot bot(BOT_TOKEN);
RTC_DS3231 rtc;





