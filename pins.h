// pins.h 📌 Назначение пинов ESP32 (аппаратная карта проекта)
#pragma once

#define PIN_SPI_CS 5            // 💾 CS SD-карты

const int LED_BUILTIN = 2;     // 💡 Встроенный светодиод
const int BUTTON = 16;         // 🔘 Кнопка сброса настроек
const int LIGHT = 4;           // ☀️ Датчик освещённости (день/ночь)
const int RAIN = 15;           // 🌧️ Датчик дождя
const int FILL = 17;           // 🚰 Клапан наполнения бака
const int DRAIN = 25;          // 🗑️ Клапан слива (дренаж)
const int PUMP = 26;           // ⚡ Реле насоса
const int FLOW_SENSOR = 27;    // 💧 Датчик потока воды (расходомер)
