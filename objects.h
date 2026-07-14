// objects.h 🌱💧 Главный заголовочный файл проекта капельного полива
#pragma once

#ifndef _OBJECTS_h
#define _OBJECTS_h

#include <Arduino.h>

#include "sensors.h"  // 🔢 содержит NUM_CHANNELS
#include <SD.h>
#include <FileData.h>
#include <FastBot2.h>  // 🤖 Библиотека Telegram бота версии 2
#include <GyverNTP.h>

// ⚙️ Режимы работы канала — значение поля HumCalibration::mode
namespace Mode {
  constexpr uint8_t Auto = 0;        // 🤖 автоматический по влажности
  constexpr uint8_t AlwaysOn = 1;    // ✅ постоянно открыт
  constexpr uint8_t AlwaysOff = 2;   // ⛔ постоянно закрыт
  constexpr uint8_t Greenhouse = 3;  // 🏠 авто (парник — работает и при дожде)
}

// 🚰 Состояния отслеживания клапана — значение oldMode[i]
namespace VState {
  constexpr uint8_t Init = 0;         // начальное состояние
  constexpr uint8_t OpenByHum = 1;    // открыт по порогу влажности
  constexpr uint8_t CloseByHum = 2;   // закрыт по порогу влажности
  constexpr uint8_t Hysteresis = 3;   // в зоне гистерезиса (без действия)
  constexpr uint8_t ForcedOpen = 10;  // открыт принудительно (ручной режим)
  constexpr uint8_t ForcedClose = 11; // закрыт принудительно
}

// 🌦️ Результат оценки погодных условий за цикл (используется в основном цикле)
// Определён в заголовке намеренно: иначе авто-генерация прототипов в .ino
// вставит прототип checkWeather() выше определения типа.
struct Weather {
  bool blocked = false;     // ⛔ полив заблокирован (ночь и/или дождь)
  bool nightBlock = false;  // 🌙 заблокировано из-за ночи
  bool rainBlock = false;   // 🌧️ заблокировано из-за дождя
};

#include "secrets.h"  // 🔐 BOT_TOKEN (не коммитится)
#include "pins.h"     // 📌 карта пинов ESP32
#include "timing.h"   // ⏱️ интервалы, таймауты, параметры алгоритмов

// 🌱 Структура калибровки одного канала датчика влажности почвы
struct HumCalibration {
  char title[90] = "🌱 Растение";  // 📝 Название растения/канала
  byte border = 60;                // 🎯 Порог срабатывания (%)
  byte mode = 0;                   // 🚰 Режим работы: 0=авто, 1=вкл, 2=выкл, 3=авто(парник)
  uint16_t minVal = 1024;          // 🔧 Минимальное АЦП (вода)
  uint16_t maxVal = 1024;          // 🔧 Максимальное АЦП (сухо)
};

// 🔐 Сигнатура и версия структуры Config (защита от чтения чужого/устаревшего файла)
#define CONFIG_MAGIC   0xD12C0FFEu
#define CONFIG_VERSION 3
#define EEPROM_VER_ADDR 240   // 📍 адрес в EEPROM для версии настроек (uint16_t), для авто-сброса

// ⚙️ Основная структура конфигурации системы полива
struct Config {
  uint32_t magic = CONFIG_MAGIC;     // 🔐 метка «это наш конфиг»
  uint16_t version = CONFIG_VERSION; // 🔢 версия структуры (растёт при изменении полей)
  bool runOnRain = true;            // 🌧️ Разрешить работу во время дождя
  bool runOnNight = false;          // 🌙 Разрешить работу ночью
  int deltaCalibration = 15;      // 🔧 Дельта калибровки (АЦП) — совпадает со «Сбросом настроек» в боте
  int deltaHum = 5;                 // 💧 Дельта влажности (% гистерезис)
  uint8_t boostPumpValves = 9;      // 💪 Порог насоса повышения давления: включать при ≥ N открытых клапанах (1..8), 9 = никогда
  HumCalibration chanel[NUM_CHANNELS]; // 🌱 каналы датчиков/клапанов
  unsigned long utimeAllClosed = 0; // ⏱️ Unix-время закрытия всех клапанов
  float flowSessionLiters = 0.0; // 💧 Расход воды за текущую сессию полива (литры)
  float flowTotalLiters = 0.0;// 📊 Общий накопленный расход воды с момента включения (литры)
  unsigned long  pulses = 0; // импульсов за смену
  // 🧽 Контроль засора фильтра
  float   cleanFlowPerValve = 0.0; // 🧽 эталон скорости одного открытого клапана, л/мин (0 = не откалибровано)
  float   cleanFlowDrain = 0.0;    // 🧽 эталон добавки потока от пролива дренажа, л/мин
  uint8_t clogThresholdPercent = 50;           // 🚨 порог тревоги: % от эталона (50% = «в 2 раза медленнее»)
  bool    flowMonitorEnabled = true;           // 🧽 включён ли контроль засора фильтра
  uint16_t tgTimeoutSec = TG_TIMEOUT_DEFAULT;  // 🔌 таймаут потери связи с Telegram, секунды
  // ⚠️ Новые поля добавлять ТОЛЬКО в конец структуры: FileData работает в режиме
  // addWithoutWipe() — при росте структуры он дочитывает старые поля по их
  // прежним смещениям, а новые оставляет со значением по умолчанию. Так настройки
  // переживают обновление прошивки без сброса (и без бампа CONFIG_VERSION, который
  // запустил бы полный factory-reset с порталом WiFi и очисткой пользователей).
  bool    autoCalNoDelay = false;              // 🔧 авто-калибровка границ без задержки (без подтверждения 3 замерами)
};

extern Config myConfig;       // ⚙️ Глобальная конфигурация системы
extern FileData data;         // 💾 Менеджер хранения конфигурации на SD-карте

extern char tstr[32];         // 🔐 Кодовое слово для первичной регистрации админа
extern bool res;              // 🔄 Флаг запроса на перезагрузку ESP
extern FastBot2 bot;          // 🤖 Экземпляр Telegram бота (FastBot2)
extern bool botHasError;      // 🆘 Флаг ошибки соединения с серверами Telegram

extern HumiditySensors hs;    // 💧 Менеджер 8-канальных датчиков влажности
extern bool dropped;          // 📡 Флаг потери WiFi соединения
extern bool rainNow;          // 🌧️ Флаг обнаружения дождя
extern bool nightNow;         // 🌙 Флаг ночного времени
extern unsigned long pumpStart;  // ⏱️ millis() запуска насоса (0 = не в пусковом режиме)

extern byte oldMode[NUM_CHANNELS]; // 🚰 Предыдущие состояния клапанов (для отслеживания изменений)
extern bool fillActive;       // 🚰 идёт неблокирующий импульс заливки бака (определён в .ino)

// 💧 Переменные для замера расхода воды через датчик потока
extern volatile unsigned long flowPulseCount;   // 🔄 Счётчик импульсов датчика потока (volatile — обновляется в ISR)
extern unsigned long flowLastSessionPulses;     // 📝 Импульсы за предыдущую сессию

// 💧 Функции для работы с датчиком потока воды
void flowInit();                // 🔌 Инициализация датчика потока (пин + прерывание)
void flowResetSession();        // 🔄 Сброс счётчика текущей сессии
float flowGetSessionLiters(); // 💧 Получить расход за текущую сессию
float flowGetTotalLiters();   // 📊 Получить общий расход
void flowAddToTotal();        // ➕ Перенести расход сессии в общий счётчик
void flowGetSessionLitersTick();     // ⏱️ обработка литров по таймеру
void clearDataFlow();

// 🧽 Контроль засора фильтра по скорости потока
void  flowMonitorTick();          // ⏱️ вызывать в каждом loop()
void  flowMonitorRecalibrate();   // 🧽 «фильтр прочищен»: сброс эталонов и тревоги
bool  flowMonitorNeedUpdate();   // 💾 нужно ли сохранить обновлённый эталон
float fmLastRate();               // 📟 последняя измеренная скорость (л/мин)
float fmBaselineFor(uint8_t openCount);  // 📊 эталон для числа открытых клапанов
bool  fmIsClogged();              // 🚨 активна ли тревога засора

// 📟 Получить актуальную дату и время (синхронизация NTP + RTC)
Datime getDateTime();

// 📡 Подключить внешнюю функцию отправки статуса в Telegram
void attachSendFunction(void (*function)(String text));

// 📨 Отправить статусное сообщение всем подписанным пользователям Telegram
void sendTelegramStatus(String text);

#endif