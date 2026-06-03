// logger.cpp 📝 Модуль логирования (обертка над Serial)
#include "logger.h"

Logger logger;

void Logger::begin(unsigned long baud) {
    Serial.begin(baud);
}

void Logger::print(const char* str) { Serial.print(str); }
void Logger::print(const String& str) { Serial.print(str); }
void Logger::print(char c) { Serial.print(c); }
void Logger::print(int val) { Serial.print(val); }
void Logger::print(unsigned int val) { Serial.print(val); }
void Logger::print(long val) { Serial.print(val); }
void Logger::print(unsigned long val) { Serial.print(val); }
void Logger::print(int64_t val) { Serial.print((long long)val); }
void Logger::print(uint64_t val) { Serial.print((unsigned long long)val); }
void Logger::print(float val) { Serial.print(val); }
void Logger::print(double val) { Serial.print(val); }
void Logger::print(const __FlashStringHelper* val) { Serial.print(val); }

void Logger::println() { Serial.println(); }
void Logger::println(const char* str) { Serial.println(str); }
void Logger::println(const String& str) { Serial.println(str); }
void Logger::println(char c) { Serial.println(c); }
void Logger::println(int val) { Serial.println(val); }
void Logger::println(unsigned int val) { Serial.println(val); }
void Logger::println(long val) { Serial.println(val); }
void Logger::println(unsigned long val) { Serial.println(val); }
void Logger::println(int64_t val) { Serial.println((long long)val); }
void Logger::println(uint64_t val) { Serial.println((unsigned long long)val); }
void Logger::println(float val) { Serial.println(val); }
void Logger::println(double val) { Serial.println(val); }
void Logger::println(const __FlashStringHelper* val) { Serial.println(val); }

void Logger::printf(const char* format, ...) {
    char buf[512];
    va_list args;
    va_start(args, format);
    vsnprintf(buf, sizeof(buf), format, args);
    va_end(args);
    Serial.print(buf);
}

void Logger::flush() { Serial.flush(); }
