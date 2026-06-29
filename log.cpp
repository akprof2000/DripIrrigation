// log.cpp 📝 Реализация логгера
#include "log.h"
#include <stdarg.h>

// При LOG_LEVEL_NONE реализация (и любые ссылки на Serial) не компилируются —
// UART не задействуется вовсе, прошивка меньше.
#if LOG_LEVEL > LOG_LEVEL_NONE

// 🏷️ Тег уровня (эмодзи + метка) — для читаемости в мониторе порта
static const char* levelTag(char level) {
  switch (level) {
    case 'E': return "❌ ERR ";
    case 'W': return "⚠️ WARN";
    case 'I': return "ℹ️ INFO";
    case 'D': return "🔧 DBG ";
    default:  return "   ?   ";
  }
}

void logPrintf(char level, const char* fmt, ...) {
  // ⏱️ Аптайм в формате чч:мм:сс
  unsigned long s = millis() / 1000;
  char head[40];
  snprintf(head, sizeof(head), "[%02lu:%02lu:%02lu] %s  ",
           s / 3600, (s / 60) % 60, s % 60, levelTag(level));
  Serial.print(head);

  // 💬 Тело сообщения
  char buf[256];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  Serial.println(buf);
}

#endif  // LOG_LEVEL > LOG_LEVEL_NONE
