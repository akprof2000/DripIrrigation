// objects.h
#pragma once

#ifndef _SENSORS_h
#define _SENSORS_h

#if defined(ARDUINO) && ARDUINO >= 100
#include "arduino.h"
#else
#include "WProgram.h"
#endif

class HumiditySensors {
  unsigned long pm = 0;
  unsigned long inter = 1000;

  const byte _sensor = 33;  // where the multiplexer in/out port is connected
  // the multiplexer address select lines (A/B/C)
  const byte _addressA = 25;  // low-order bit
  const byte _addressB = 26;
  const byte _addressC = 27;  // high-order bit

  int _low[8];
  int _high[8];
  int _curr[8];
  int _border = 0;
  int readSensor(const byte which);
public:
  void init() {
    pinMode(_addressA, OUTPUT);
    pinMode(_addressB, OUTPUT);
    pinMode(_addressC, OUTPUT);
    Serial.println("Init Humidity Sensors");
  }
  void setLowHighValue(byte index, int lValue, int hValue) {
    _low[index] = lValue;
    _high[index] = hValue;
  }
  void setBorder(int value) {
    _border = value;
  }
  int setLow(int index);
  int setHigh(int index);
  int setCurrent(int index);
  void setAll();
  int getLow(int index) {
    return _low[index];
  }
  int getHigh(int index) {
    return _high[index];
  }
  int getCurrent(int index) {
    return _curr[index];
  }
  int Percent(int index);
};

#endif
