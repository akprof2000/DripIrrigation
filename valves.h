// valves.h
#pragma once

#ifndef _VALVES_h
#define _VALVES_h

#include "arduino.h"




void valves_init();

bool valve_opened();

void valve_open(int index);
void valve_close(int index);
bool valve_needUpdate();

#endif
