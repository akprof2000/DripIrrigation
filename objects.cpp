// objects.cpp 🌱💧 Определения глобальных объектов и переменных проекта
#include "objects.h"

// ⏱️ Unix-время запуска насоса (для защиты от перегрева)
int64_t pumpStart = 0;

// ⚙️ Глобальная конфигурация системы полива (хранится на SD-карте)
Config myConfig;

// 💾 Менеджер файловой конфигурации: путь /configuration.dat, тип 'B' (бинарный)
FileData data(&SD, "/configuration.dat", 'B', &myConfig, sizeof(myConfig));

// 🔐 Кодовое слово для первичной регистрации администратора (генерируется в портале)
char tstr[32] = "";

// 🔄 Флаг запроса на перезагрузку ESP (устанавливается через Telegram)
bool res = false;

// 🤖 Экземпляр Telegram бота (FastBot2)
FastBot2 bot;

// 🆘 Флаг ошибки соединения с серверами Telegram (3 или 4 из tick())
bool botHasError = false;

// 💧 Менеджер 8-канальных датчиков влажности почвы
HumiditySensors hs;

// 📡 Флаг потери WiFi соединения
bool dropped = false;

// 🌧️ Флаг обнаружения дождя
bool rainNow = false;

// 🌙 Флаг ночного времени
bool nightNow = false;

// 🚰 Предыдущие состояния клапанов (для отслеживания изменений и отправки уведомлений)
byte oldMode[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };

// 📡 Указатель на функцию отправки статуса в Telegram
void (*p_sendTelegramFunction)(String text);

// 📟 Получить актуальную дату и время (синхронизация NTP + RTC)
Datime getDateTime() {
  NTP.updateNow();        // 🔄 Принудительное обновление времени по NTP
  Datime now = NTP;       // 📟 Получение текущего времени
  return now;
}

// 📡 Подключить внешнюю функцию отправки статуса в Telegram
void attachSendFunction(void (*function)(String text)) {
  p_sendTelegramFunction = function;
}

// 📨 Отправить статусное сообщение через подключённую функцию
void sendTelegramStatus(String text) {
  (*p_sendTelegramFunction)(text);
}
