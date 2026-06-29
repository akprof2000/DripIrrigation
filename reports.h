// reports.h 📊 Генерация отчётов/графиков из CSV на SD + рекурсивное удаление
#pragma once

#include <Arduino.h>
#include <SD.h>  // тип File

// 🗑️ Рекурсивное удаление папки со всем содержимым
void rm(File dir, String tempPath);

// 📊 График за период дней (несколько CSV-файлов) → отправка в чат
void fileToGrafPeriod(int period, String msgID);

// 📊 График за один день (один CSV-файл) → отправка в чат
void fileToGraf(String fn, String msgID);
