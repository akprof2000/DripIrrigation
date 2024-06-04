// valves.h
#pragma once

#ifndef _VALVES_h
#define _VALVES_h

#if defined(ARDUINO) && ARDUINO >= 100
#include "arduino.h"
#else
#include "WProgram.h"
#endif



void valves_init();

bool valve_opened();

void valve_open(int index);
void valve_close(int index);

#endif
