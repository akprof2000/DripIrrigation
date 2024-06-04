// objects.h
#pragma once

#ifndef _OBJECTS_h
#define _OBJECTS_h

#if defined(ARDUINO) && ARDUINO >= 100
#include "arduino.h"
#else
#include "WProgram.h"
#endif


#include "sensors.h"
#include <RTClib.h>
#include <SPIFFS.h>
#include <FileData.h>
#include <FastBot.h>


#define BOT_TOKEN "XXXXXXXXXXXXXX:XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX"
#define PIN_SPI_CS 5
#define CHECK_WIFI_INTERVAL 30000
#define CHECK_INTERVAL 10000
#define CHECK_LIGHT true
#define CHECK_RAIN true
#define FILLING_WAIT 300



const int LED_BUILTIN = 2;
const int BUTTON = 16;
const int LIGHT = 4;
const int RAIN = 15;
const int FILL = 17;


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

extern byte oldMode[8];

#endif
