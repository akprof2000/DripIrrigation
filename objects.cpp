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

// ============================================================
// 💧 Переменные для замера расхода воды через датчик потока
// ============================================================
// 🔧 Калибровочный коэффициент: импульсов на 1 литр
// Для YF-S201 ≈ 450 импульсов/литр, для YF-S402 ≈ 980 импульсов/литр
#define FLOW_PULSES_PER_LITER 450.0

// 🔄 Счётчик импульсов датчика потока (volatile — обновляется в прерывании)
volatile unsigned long flowPulseCount = 0;


// ============================================================
// ⚡ ISR: Обработчик прерывания от датчика потока воды
// ============================================================
// Вызывается при каждом импульсе на пине FLOW_SENSOR (27)
void IRAM_ATTR onFlowPulse() {
  flowPulseCount+=1;  // 🔄 Увеличиваем счётчик импульсов
}

// ============================================================
// 🔌 Инициализация датчика потока воды
// ============================================================
void flowInit() {
  pinMode(FLOW_SENSOR, INPUT_PULLUP);  // 🔌 Настраиваем пин как вход с подтяжкой
  
  flowPulseCount = myConfig.pulses;
  // ⚡ Подключаем прерывание по спадающему фронту (FALLING) — датчик замыкает на GND
  attachInterrupt(digitalPinToInterrupt(FLOW_SENSOR), onFlowPulse, FALLING);
  Serial.println("💧 Датчик потока воды инициализирован на пине 27");
}

// ============================================================
// 🔄 Сброс счётчика текущей сессии полива
// ============================================================
// Вызывается при открытии первого клапана — начинаем новый замер
void flowResetSession() {
  flowPulseCount = 0;  // 📝 Сохраняем стартовое значение
  myConfig.pulses = 0;
  myConfig.flowSessionLiters = 0.0;                 // 💧 Обнуляем расход сессии
  Serial.println("🔄 Счётчик расхода воды сброшен — начата новая сессия полива");
  data.updateNow();
}

// ============================================================
// 💧 Получить расход воды за текущую сессию полива
// ============================================================
float flowGetSessionLiters() {
  // 📟 Считаем разницу импульсов с момента сброса и переводим в литры
  myConfig.pulses = flowPulseCount;
  return myConfig.pulses / FLOW_PULSES_PER_LITER;
}

// ============================================================
// 📊 Получить общий накопленный расход воды
// ============================================================
float flowGetTotalLiters() {
  return myConfig.flowTotalLiters;
}

void flowGetSessionLitersTick()
{
    myConfig.flowSessionLiters = flowGetSessionLiters();
    data.update();
}

void clearDataFlow()
{
  myConfig.flowSessionLiters = 0;
  myConfig.flowTotalLiters = 0;
  myConfig.pulses = 0;
  flowPulseCount = 0;
  data.updateNow();
}


// ============================================================
// ➕ Перенести расход текущей сессии в общий счётчик
// ============================================================
// Вызывается при закрытии всех клапанов — фиксируем расход сессии
void flowAddToTotal() {
  myConfig.flowSessionLiters = flowGetSessionLiters();  // 💧 Фиксируем литры за сессию
  myConfig.flowTotalLiters += myConfig.flowSessionLiters;        // ➕ Добавляем к общему расходу
  Serial.print("💧 Расход воды за сессию: ");
  Serial.print(myConfig.flowSessionLiters, 3);
  Serial.println(" л");
  Serial.print("📊 Общий расход воды: ");
  Serial.print(myConfig.flowTotalLiters, 3);
  Serial.println(" л");
}

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