// telegram.h 🤖💬 Заголовочный файл модуля Telegram бота (FastBot2)
#ifndef _TELEGRAM_h
#define _TELEGRAM_h

#include <Arduino.h>
#include "botutil.h"  // 🧰 IntWith2Zero и др. (исторически объявлялись здесь)

// 🚀 Инициализация Telegram бота (FastBot2): подключение обработчика, загрузка пользователей
void botInit();

// 💾 Отправить уведомление об отключении SD-карты всем пользователям
void dropCDCard();

// 💾 Отправить уведомление о подключении SD-карты всем пользователям
void connectCDCard();

// 📨 Отправить статусное сообщение всем подписанным пользователям
void sendStatus(String text);

// 🔌 Контроль связи с Telegram: подключить детект (в botInit)
void botMonitorAttach();
// 🔌 Тик контроля связи (в ReCheck). gotUpdate = результат bot.tick()
void botMonitorTick(bool gotUpdate);

// 🔄 Проверить необходимость обновления конфигурации на SD-карте
bool telegramNeedUpdate();

#endif
