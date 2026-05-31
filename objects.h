// objects.h 🌱💧 Главный заголовочный файл проекта капельного полива
#pragma once

#ifndef _OBJECTS_h
#define _OBJECTS_h

#include "arduino.h"

#include "sensors.h"
#include <SD.h>
#include <FileData.h>
#include <FastBot2.h>  // 🤖 Библиотека Telegram бота версии 2
#include <GyverNTP.h>

// 🔐 Токен Telegram бота (получен у 🤖 BotFather)
#define BOT_TOKEN "REMOVED_BOT_TOKEN_XXXXXXXXXXXXXXXXXXXXXXXXXXXX"
#define PIN_SPI_CS 5
#define CHECK_WIFI_INTERVAL_SMALL 3000 // ⏱️ миллисекунды (малый интервал проверки)
#define CHECK_WIFI_INTERVAL 30000      // ⏱️ миллисекунды (основной интервал проверки)
#define CHECK_INTERVAL 10000           // ⏱️ миллисекунды (интервал цикла полива)
#define CHECK_LIGHT true
#define CHECK_RAIN true
#define FILLING_WAIT 3000    // ⏱️ миллисекунды (время наполнения бака)
#define TIMEOUT_WAIT 18000   // ⏱️ секунды (таймаут ожидания закрытия клапанов)
#define DRAIN_TIMEOUT 12000  // ⏱️ миллисекунды (время пролива дренажа)
#define PUMP_TIMEOUT 30      // ⏱️ секунды (максимальное время работы насоса)

// 📌 Назначение пинов ESP32
const int LED_BUILTIN = 2;  // 💡 Встроенный светодиод
const int BUTTON = 16;        // 🔘 Кнопка сброса настроек
const int LIGHT = 4;          // ☀️ Датчик освещённости (день/ночь)
const int RAIN = 15;          // 🌧️ Датчик дождя
const int FILL = 17;          // 🚰 Клапан наполнения бака
const int DRAIN = 25;         // 🗑️ Клапан слива (дренаж)
const int PUMP = 26;          // ⚡ Реле насоса

// 🌱 Структура калибровки одного канала датчика влажности почвы
struct HumCalibration {
  char title[90] = "🌱 Растение";  // 📝 Название растения/канала
  byte border = 60;                // 🎯 Порог срабатывания (%)
  byte mode = 0;                   // 🚰 Режим работы: 0=авто, 1=вкл, 2=выкл, 3=авто(парник)
  uint16_t minVal = 1024;          // 🔧 Минимальное АЦП (вода)
  uint16_t maxVal = 1024;          // 🔧 Максимальное АЦП (сухо)
};

// ⚙️ Основная структура конфигурации системы полива
struct Config {
  bool runOnRain = true;            // 🌧️ Разрешить работу во время дождя
  bool runOnNight = false;          // 🌙 Разрешить работу ночью
  int deltaCalibration = 30;      // 🔧 Дельта калибровки (АЦП)
  int deltaHum = 5;                 // 💧 Дельта влажности (% гистерезис)
  HumCalibration chanel[8];         // 🌱 8 каналов датчиков/клапанов
  unsigned long utimeAllClosed = 0; // ⏱️ Unix-время закрытия всех клапанов
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
extern int64_t pumpStart;     // ⏱️ Unix-время запуска насоса

extern byte oldMode[8];       // 🚰 Предыдущие состояния клапанов (для отслеживания изменений)

// 📟 Получить актуальную дату и время (синхронизация NTP + RTC)
Datime getDateTime();

// 📡 Подключить внешнюю функцию отправки статуса в Telegram
void attachSendFunction(void (*function)(String text));

// 📨 Отправить статусное сообщение всем подписанным пользователям Telegram
void sendTelegramStatus(String text);

#endif
