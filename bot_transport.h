// bot_transport.h 📡 Транспорт Telegram: отправка сообщений и рассылки.
// Низкоуровневая отправка с переподключением WiFi. Декларации dropCDCard/
// connectCDCard/sendStatus/botMonitor* остаются в telegram.h (вызываются из init).
#pragma once

#include <Arduino.h>

// 📨 Отправить текстовое сообщение пользователю.
//    Ошибка Telegram API (ok=false) — только лог, без повтора. Транспортная
//    ошибка при реально упавшем WiFi — одна короткая (≤3 с) попытка
//    переподключения и один повтор. kbRem — убрать клавиатуру.
void sendReconnectMessage(String text, String id, bool kbRem = false);
