// sensors.h 💧 Заголовочный файл модуля датчиков влажности
#pragma once

#ifndef _SENSORS_h
#define _SENSORS_h

#include <Arduino.h>
#include "log.h"

// 🔢 Количество каналов влажности/клапанов — единая точка правды вместо литерала 8.
// Определено здесь (самый нижний заголовок), поэтому видно во всех модулях.
constexpr uint8_t NUM_CHANNELS = 8;

// 💧 Класс управления 8-канальным мультиплексором датчиков влажности
class HumiditySensors {
  unsigned long pm = 0;       // ⏱️ Предыдущее время
  unsigned long inter = 1000; // ⏱️ Интервал опроса

  // 🔌 Пины мультиплексора (3 бита адреса S0-S2)
  const byte S[3] = { 12, 13, 14 };
  // 📟 Пин АЦП (аналоговый вход)
  const byte Z = 33;

  int _low[NUM_CHANNELS];     // 🔧 Минимальные значения АЦП (вода)
  int _high[NUM_CHANNELS];    // 🔧 Максимальные значения АЦП (сухо)
  int _curr[NUM_CHANNELS];    // 📟 Текущие значения АЦП
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
    LOG_I("Датчики влажности инициализированы");
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
