// sensors.h 💧 Заголовочный файл модуля датчиков влажности
#pragma once

#ifndef _SENSORS_h
#define _SENSORS_h

#include <Arduino.h>
#include "log.h"
#include "timing.h"  // ⏱️ HUM_AUTOCAL_CONFIRM

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

  // 🔧 Авто-калибровка границ: счётчики подряд идущих замеров за границей и
  // «кандидат» — САМОЕ МЯГКОЕ (наименее экстремальное) значение за это окно.
  // Берём именно мягкое, чтобы одиночный выброс внутри окна не утянул границу.
  uint8_t _loCnt[NUM_CHANNELS] = { 0 };   // замеров подряд ниже _low (влажнее воды)
  uint8_t _hiCnt[NUM_CHANNELS] = { 0 };   // замеров подряд выше _high (суше сухого)
  int     _loCand[NUM_CHANNELS] = { 0 };  // кандидат нижней границы (максимум из превышений)
  int     _hiCand[NUM_CHANNELS] = { 0 };  // кандидат верхней границы (минимум из превышений)

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

  // 🔧 Установить границы калибровки для канала (сбрасывает авто-калибровку:
  // старые счётчики превышений относятся к прежним границам)
  void setLowHighValue(byte index, int lValue, int hValue) {
    _low[index] = lValue;
    _high[index] = hValue;
    _loCnt[index] = 0;
    _hiCnt[index] = 0;
  }

  // 🔧 Авто-расширение границ по фактическому показанию.
  //
  // Если значение вышло за границу, заданную при калибровке, граница
  // отодвигается до этого значения с запасом «дельта калибровки» (_border).
  // Границы только расширяются, сужения нет.
  //
  // 🛡️ Защита от разовых выбросов: граница двигается не сразу, а только когда
  // превышение держится confirmNeeded замеров подряд (замер раз в минуту).
  // Один сбойный отсчёт обнуляет счётчик и ничего не портит. Внутри окна
  // подтверждения берём САМОЕ МЯГКОЕ значение (ближайшее к старой границе), а не
  // самое экстремальное — иначе выброс внутри окна всё равно утянул бы границу.
  //
  // confirmNeeded = 1 — режим «без задержки»: граница двигается по первому же
  // отсчёту за границей (быстро, но выбросы не фильтруются).
  //
  // Значения зажаты в 0..4095: в Config границы хранятся как uint16_t, и
  // отрицательное значение обернулось бы в огромное.
  //
  // Возвращает true, если границы изменились (нужно сохранить конфиг).
  bool autoExtend(int index, uint8_t confirmNeeded) {
    if (confirmNeeded < 1) confirmNeeded = 1;
    bool changed = false;
    int val = _curr[index];

    // 💧 Влажнее калибровочной «воды»
    if (val < _low[index]) {
      // кандидат = максимум из превышений (наименее влажный отсчёт окна)
      if (_loCnt[index] == 0 || val > _loCand[index]) _loCand[index] = val;
      if (_loCnt[index] < 255) _loCnt[index]++;
      if (_loCnt[index] >= confirmNeeded) {
        int nl = _loCand[index] - _border;
        _low[index] = (nl < 0) ? 0 : nl;
        _loCnt[index] = 0;
        changed = true;
      }
    } else {
      _loCnt[index] = 0;  // 🔄 отсчёт вернулся в норму — выброс не подтвердился
    }

    // 🌵 Суше калибровочного «сухого»
    if (val > _high[index]) {
      // кандидат = минимум из превышений (наименее сухой отсчёт окна)
      if (_hiCnt[index] == 0 || val < _hiCand[index]) _hiCand[index] = val;
      if (_hiCnt[index] < 255) _hiCnt[index]++;
      if (_hiCnt[index] >= confirmNeeded) {
        int nh = _hiCand[index] + _border;
        _high[index] = (nh > 4095) ? 4095 : nh;
        _hiCnt[index] = 0;
        changed = true;
      }
    } else {
      _hiCnt[index] = 0;
    }

    return changed;
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
