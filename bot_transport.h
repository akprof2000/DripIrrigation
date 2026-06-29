// bot_transport.h 📡 Транспорт Telegram: отправка сообщений и рассылки.
// Низкоуровневая отправка с переподключением WiFi. Декларации dropCDCard/
// connectCDCard/sendStatus/botMonitor* остаются в telegram.h (вызываются из init).
#pragma once

#include <Arduino.h>

// 📨 Отправить текстовое сообщение пользователю с повторными попытками.
//    При ошибке связи с Telegram переподключает WiFi. kbRem — убрать клавиатуру.
void sendReconnectMessage(String text, String id, bool kbRem = false);
