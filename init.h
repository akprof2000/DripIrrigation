// objects.h
#pragma once

#ifndef _INIT_h
#define _INIT_h

#if defined(ARDUINO) && ARDUINO >= 100
#include "arduino.h"
#else
#include "WProgram.h"
#endif

void init();

void ReCheck();

#endif
