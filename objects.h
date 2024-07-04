// objects.h
#pragma once

#ifndef _OBJECTS_h
#define _OBJECTS_h

#include "arduino.h"


#include "sensors.h"
#include <RTClib.h>
#include <SD.h>
#include <FileData.h>
#include <FastBot.h>


#define BOT_TOKEN "XXXXXXXXXXXXXX:XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX"
#define PIN_SPI_CS 5
#define CHECK_WIFI_INTERVAL_SMALL 3000 //miliseconds
#define CHECK_WIFI_INTERVAL 30000 //miliseconds
#define CHECK_INTERVAL 10000 //miliseconds
#define CHECK_LIGHT true
#define CHECK_RAIN true
#define FILLING_WAIT 3000 //miliseconds
#define TIMEOUT_WAIT 18000 //seconds
#define DRAIN_TIMEOUT 9000 //miliseconds
#define PUMP_TIMEOUT 60 //seconds



const int LED_BUILTIN = 2;
const int BUTTON = 16;
const int LIGHT = 4;
const int RAIN = 15;
const int FILL = 17;
const int DRAIN = 25;
const int PUMP = 26;


struct HumCalibration {
  char title[90] = "Растение";
  byte border = 60;
  byte mode = 0;
  uint16_t minVal = 1024;
  uint16_t maxVal = 1024;
};


struct Config {
  bool runOnRain = true;
  bool runOnNight = false;
  int deltaCalibration = 30;
  int deltaHum = 5;
  HumCalibration chanel[8];
  unsigned long utimeAllClosed = 0;
};

extern Config myConfig;
extern FileData data;

extern char tstr[32];
extern bool res;
extern FastBot bot;
extern RTC_DS3231 rtc;

extern HumiditySensors hs;
extern bool dropped;
extern bool rainNow;
extern bool nightNow;
extern int64_t pumpStart;

extern byte oldMode[8];

int64_t getUnixTime();

void attachSendFunction(void (*function)(String text));


void sendTelegramStatus(String text);

#endif
