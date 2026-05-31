// sensors.h 💧 Заголовочный файл модуля датчиков влажности
#pragma once

#ifndef _SENSORS_h
#define _SENSORS_h

#include "arduino.h"

// 💧 Класс управления 8-канальным мультиплексором датчиков влажности
class HumiditySensors {
  unsigned long pm = 0;       // ⏱️ Предыдущее время
  unsigned long inter = 1000; // ⏱️ Интервал опроса

  // 🔌 Пины мультиплексора (3 бита адреса S0-S2)
  const byte S[3] = { 12, 13, 14 };
  // 📟 Пин АЦП (аналоговый вход)
  const byte Z = 33;

  int _low[8];     // 🔧 Минимальные значения АЦП (вода)
  int _high[8];    // 🔧 Максимальные значения АЦП (сухо)
  int _curr[8];    // 📟 Текущие значения АЦП
  int _border = 0; // 🔧 Дельта калибровки

  // 📟 Чтение АЦП с указанного канала мультиплексора
  int readSensor(const byte which);

public:
  // 🔌 Инициализация пинов мультиплексора
  void init() {
    pinMode(S[0], OUTPUT);
    pinMode(S[1], OUTPUT);
    pinMode(S[2], OUTPUT);
    pinMode(Z, INPUT);
    Serial.println("💧 Init Humidity Sensors");
  }

  // 🔧 Установить границы калибровки для канала
  void setLowHighValue(byte index, int lValue, int hValue) {
    _low[index] = lValue;
    _high[index] = hValue;
  }

  // 🔧 Установить дельту калибровки
  void setBorder(int value) {
    _border = value;
  }

  // 💧 Установить текущее значение как минимум (вода)
  int setLow(int index);

  // 🌵 Установить текущее значение как максимум (сухо)
  int setHigh(int index);

  // 📟 Опросить и сохранить текущее значение канала
  int setCurrent(int index);

  // 📟 Опросить все 8 каналов
  void setAll();

  // 🔧 Получить минимальное значение АЦП канала
  int getLow(int index) {
    return _low[index];
  }

  // 🔧 Получить максимальное значение АЦП канала
  int getHigh(int index) {
    return _high[index];
  }

  // 📟 Получить последнее измеренное значение АЦП
  int getCurrent(int index) {
    return _curr[index];
  }

  // 📊 Рассчитать процент влажности (0-100%) с учётом гистерезиса
  int Percent(int index);
};

#endif
