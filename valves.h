// valves.h 🚰 Заголовочный файл модуля управления клапанами
#pragma once

#ifndef _VALVES_h
#define _VALVES_h

#include <Arduino.h>

// 🔌 Инициализация расширителя портов PCF8574
void valvesInit();

// 🚰 Проверить, открывался ли клапан (сбрасывает флаг)
bool valveOpened();

// 🗑️ Запуск пролива дренажа (неблокирующий — старт)
void spillage();

// ⏱️ Тик пролива дренажа: завершает пролив по таймеру. Вызывать в каждом loop()
void spillageTick();

// 🗑️ Идёт ли сейчас пролив дренажа (для контроля засора фильтра)
bool valveIsDraining();

// 🚰 Открыть клапан по индексу
void valveOpen(int index);

// ⛔ Закрыть клапан по индексу
void valveClose(int index);

// 🛑 Погасить все нагрузки: клапаны через PCF8574 (по возможности), насос, дренаж,
//    налив — напрямую по пинам. Вызывать перед ESP.restart() и в аварийном режиме.
void loadsOff();

// 🔄 Проверить необходимость сохранения времени закрытия
bool valveNeedUpdate();

int countValveOpen();
void stopPumpIfNeed();

// 💪 Включён ли сейчас нагнетательный насос (фактическое состояние пина реле)
bool pumpIsOn();
#endif
