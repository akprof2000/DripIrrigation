// log.h 📝 Лёгкий логгер с уровнями. Логи ниже LOG_LEVEL вырезаются компилятором
// (не занимают флеш), что важно при почти полном разделе.
//
// Уровни по смыслу события:
//   LOG_E — ошибки (сбой записи, нет RTC/SD, не открыть файл)
//   LOG_W — предупреждения/восстанавливаемое (потеря связи, засор, сброс настроек)
//   LOG_I — нормальные события (полив, день/ночь, старт, восстановление связи)
//   LOG_D — подробная отладка (значения АЦП, влажность по каналам, EEPROM)
#pragma once

#include <Arduino.h>

#define LOG_LEVEL_NONE  0
#define LOG_LEVEL_ERROR 1
#define LOG_LEVEL_WARN  2
#define LOG_LEVEL_INFO  3
#define LOG_LEVEL_DEBUG 4

// 🎚️ Порог компиляции: можно переопределить в secrets.h/build-флаге.
//    NONE: логи полностью вырезаны компилятором (нет Serial-вывода, меньше флеш).
//    Для отладки на стенде поставь LOG_LEVEL_DEBUG/INFO и пересобери.
#ifndef LOG_LEVEL
#define LOG_LEVEL LOG_LEVEL_NONE  
#endif

// 📤 Печать одной строки лога: [чч:мм:сс] <тег> сообщение
void logPrintf(char level, const char* fmt, ...);

#if LOG_LEVEL >= LOG_LEVEL_ERROR
#define LOG_E(...) logPrintf('E', __VA_ARGS__)
#else
#define LOG_E(...) do {} while (0)
#endif

#if LOG_LEVEL >= LOG_LEVEL_WARN
#define LOG_W(...) logPrintf('W', __VA_ARGS__)
#else
#define LOG_W(...) do {} while (0)
#endif

#if LOG_LEVEL >= LOG_LEVEL_INFO
#define LOG_I(...) logPrintf('I', __VA_ARGS__)
#else
#define LOG_I(...) do {} while (0)
#endif

#if LOG_LEVEL >= LOG_LEVEL_DEBUG
#define LOG_D(...) logPrintf('D', __VA_ARGS__)
#else
#define LOG_D(...) do {} while (0)
#endif
