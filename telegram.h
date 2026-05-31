// telegram.h 🤖💬 Заголовочный файл модуля Telegram бота (FastBot2)
#pragma GCC system_header

#ifndef _TELEGRAM_h
#define _TELEGRAM_h

#include <arduino.h>

// 🚀 Инициализация Telegram бота (FastBot2): подключение обработчика, загрузка пользователей
void botInit();

// 📡 Отправить уведомление о восстановлении соединения (если отключение > 5 минут)
void reConnection(unsigned long time);

// 💾 Отправить уведомление об отключении SD-карты всем пользователям
void dropCDCard();

// 💾 Отправить уведомление о подключении SD-карты всем пользователям
void connectCDCard();

// 📝 Форматировать число с ведущим нулём (для дат: 1 -> "01")
String IntWith2Zero(int data);

// 📨 Отправить статусное сообщение всем подписанным пользователям
void sendStatus(String text);

// 🔄 Проверить необходимость обновления конфигурации на SD-карте
bool telegram_needUpdate();

#endif
