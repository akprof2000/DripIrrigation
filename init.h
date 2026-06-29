// init.h 🌱💧 Заголовочный файл модуля инициализации
#pragma once

#ifndef _INIT_h
#define _INIT_h

#include <Arduino.h>

// 🚀 Главная функция инициализации системы
// (переименована с init() — у ядра Arduino есть своя функция init(), не затеняем её)
void systemInit();

// 🔄 Периодическая проверка WiFi и обработка Telegram
void ReCheck();

#endif
