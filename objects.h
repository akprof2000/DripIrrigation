// objects.h
#pragma once

#ifndef _OBJECTS_h
#define _OBJECTS_h

#if defined(ARDUINO) && ARDUINO >= 100
#include "arduino.h"
#else
#include "WProgram.h"
#endif

#include <RTClib.h>
#include <FastBot.h>
#include <FileData.h>
#include <SD.h>

#define BOT_TOKEN "REMOVED_BOT_TOKEN_XXXXXXXXXXXXXXXXXXXXXXXXXXXX"
#define PIN_SPI_CS 5
#define CHECK_WIFI_INTERVAL 30000
#define CHECK_LIGHT true
#define CHECK_RAIN true




const int LED_BUILTIN = 2;
const int BUTTON = 16;
const int LIGHT = 4;
const int RAIN = 17;


struct HumCalibr {
  uint16_t minVal = 1024;
  uint16_t maxVal = 1024;
};


struct Config {
  bool runOnRain = true;
  bool runOnNight = false;
  int deltaCalibr = 30;
  int deltaHum = 5;
  HumCalibr calibr[8];
};

extern Config myConfig;
extern FileData data;

extern char tstr[32];
extern bool res;
extern FastBot bot;
extern RTC_DS3231 rtc;

#endif




