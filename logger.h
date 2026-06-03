// logger.h 📝 Заголовочный файл модуля логирования
#pragma once
#ifndef _LOGGER_h
#define _LOGGER_h

#include "arduino.h"

class Logger {
public:
    static void begin(unsigned long baud = 115200);

    static void print(const char* str);
    static void print(const String& str);
    static void print(char c);
    static void print(int val);
    static void print(unsigned int val);
    static void print(long val);
    static void print(unsigned long val);
    static void print(int64_t val);
    static void print(uint64_t val);
    static void print(float val);
    static void print(double val);
    static void print(const __FlashStringHelper* val);

    static void println();
    static void println(const char* str);
    static void println(const String& str);
    static void println(char c);
    static void println(int val);
    static void println(unsigned int val);
    static void println(long val);
    static void println(unsigned long val);
    static void println(int64_t val);
    static void println(uint64_t val);
    static void println(float val);
    static void println(double val);
    static void println(const __FlashStringHelper* val);

    static void printf(const char* format, ...);
    static void flush();
};

extern Logger logger;

#endif
