// botutil.h 🧰 Мелкие строковые утилиты (без зависимостей от бота/железа)
#pragma once

#include <Arduino.h>

// 🔢 Проверка строки на числовое значение (целое или с точкой)
bool isNumeric(String str);

// ✂️ Извлечение подстроки по разделителю (CSV-парсер)
String getValue(String data, char separator, int index);

// 📝 Форматирование числа с ведущим нулём (1 -> "01"), для дат
String IntWith2Zero(int data);
