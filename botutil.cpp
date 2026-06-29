// botutil.cpp 🧰 Реализация строковых утилит
#include "botutil.h"

// 🔢 Проверка строки на числовое значение (целое или с точкой)
bool isNumeric(String str) {
  unsigned int stringLength = str.length();

  if (stringLength == 0) {
    return false;
  }

  boolean seenDecimal = false;

  for (unsigned int i = 0; i < stringLength; ++i) {
    if (isDigit(str.charAt(i))) {
      continue;
    }

    if (str.charAt(i) == '.') {
      if (seenDecimal) {
        return false;
      }
      seenDecimal = true;
      continue;
    }
    return false;
  }
  return true;
}

// ✂️ Извлечение подстроки по разделителю (CSV-парсер)
String getValue(String data, char separator, int index) {
  int found = 0;
  int strIndex[] = { 0, -1 };
  int maxIndex = data.length() - 1;

  for (int i = 0; i <= maxIndex && found <= index; i++) {
    if (data.charAt(i) == separator || i == maxIndex) {
      found++;
      strIndex[0] = strIndex[1] + 1;
      strIndex[1] = (i == maxIndex) ? i + 1 : i;
    }
  }

  return found > index ? data.substring(strIndex[0], strIndex[1]) : "";
}

// 📝 Форматирование числа с ведущим нулём (для дат)
String IntWith2Zero(int data) {
  String s = String(data);
  if (data < 10) { s = String("0") + s; }
  return s;
}
