// objects.h
#pragma once

#ifndef _OBJECTS_h
#define _OBJECTS_h

#if defined(ARDUINO) && ARDUINO >= 100
#include "arduino.h"
#else
#include "WProgram.h"
#endif

#include <FastBot.h>

#define BOT_TOKEN "REMOVED_BOT_TOKEN_XXXXXXXXXXXXXXXXXXXXXXXXXXXX"

extern char tstr[32];
extern bool res;
extern FastBot bot;

#endif




