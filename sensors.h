// objects.h
#pragma once

#ifndef _SENSORS_h
#define _SENSORS_h

#include "arduino.h"

class HumiditySensors {
  unsigned long pm = 0;
  unsigned long inter = 1000;

const byte S[3] = { 12, 13, 14 };
const byte Z = 33;

  int _low[8];
  int _high[8];
  int _curr[8];
  int _border = 0;
  int readSensor(const byte which);
public:
  void init() {
    pinMode(S[0], OUTPUT);
    pinMode(S[1], OUTPUT);
    pinMode(S[2], OUTPUT);
    pinMode(Z, INPUT);
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