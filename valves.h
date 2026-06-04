// valves.h 🚰 Заголовочный файл модуля управления клапанами
#pragma once

#ifndef _VALVES_h
#define _VALVES_h

#include "arduino.h"

// 🔌 Инициализация расширителя портов PCF8574
void valves_init();

// 🚰 Проверить, открывался ли клапан (сбрасывает флаг)
bool valve_opened();

// 🗑️ Пролив дренажа (наполнение + слив)
void spillage();

// 🚰 Открыть клапан по индексу
void valve_open(int index);

// ⛔ Закрыть клапан по индексу
void valve_close(int index);

// 🔄 Проверить необходимость сохранения времени закрытия
bool valve_needUpdate();

int countValveOpen();
void stopPupmIfNeed();
#endif
