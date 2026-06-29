// objects.cpp 🌱💧 Определения глобальных объектов и переменных проекта
#include "objects.h"
#include "valves.h"  // 🚰 countValveOpen(), valveIsDraining() — для контроля засора фильтра
#include "log.h"

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
byte oldMode[NUM_CHANNELS] = { 0 };  // 🔢 все каналы в состояние VState::Init

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
  LOG_I("Датчик потока воды инициализирован (пин %d)", FLOW_SENSOR);
}

// ============================================================
// 🔄 Сброс счётчика текущей сессии полива
// ============================================================
// Вызывается при открытии первого клапана — начинаем новый замер
void flowResetSession() {
  flowPulseCount = 0;  // 📝 Сохраняем стартовое значение
  myConfig.pulses = 0;
  myConfig.flowSessionLiters = 0.0;                 // 💧 Обнуляем расход сессии
  LOG_I("Счётчик расхода сброшен — новая сессия полива");
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
    myConfig.flowSessionLiters = flowGetSessionLiters();  // обновляет myConfig.pulses = flowPulseCount
    // 💾 Пишем конфиг на SD только если реально текла вода (счётчик импульсов изменился).
    // Это покрывает и полив через клапаны, и пролив дренажа (spillage гоняет воду
    // без открытия канальных клапанов), но не плодит лишние перезаписи карты,
    // когда воды нет (все клапаны закрыты, насос выключен).
    static unsigned long lastSavedPulses = 0;
    if (myConfig.pulses != lastSavedPulses) {
      lastSavedPulses = myConfig.pulses;
      data.update();
    }
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
  LOG_I("Расход за сессию: %.3f л; всего: %.3f л",
        myConfig.flowSessionLiters, myConfig.flowTotalLiters);
}

// ============================================================
// 🧽 Контроль засора фильтра по скорости потока
// ============================================================
// Эталон скорости чистого фильтра хранится по числу открытых клапанов
// (myConfig.cleanFlowRate[openCount-1]). Гибрид: эталон поднимается
// авто-максимумом (чистый фильтр = самый быстрый поток) и сбрасывается
// кнопкой «фильтр прочищен». Засор = скорость < clogThresholdPercent% от эталона.
static uint8_t       fmCount = 0;             // число клапанов в текущем окне (0 = не мониторим)
static bool          fmMeasuring = false;     // фаза измерения (после settle)
static unsigned long fmPhaseStartMs = 0;      // старт фазы выхода на режим
static unsigned long fmMeasureStartMs = 0;    // старт окна измерения
static unsigned long fmMeasureStartPulses = 0;
static uint8_t       fmBadWindows = 0;        // подряд «плохих» окон (дебаунс)
static bool          fmAlertActive = false;   // активна ли тревога засора
static unsigned long fmLastAlertMs = 0;       // для rate-limit тревоги
static float         fmRate = 0.0;            // последняя измеренная скорость, л/мин
static bool          fmDirty = false;         // эталон изменился — нужно сохранить

float fmLastRate() { return fmRate; }
bool  fmIsClogged() { return fmAlertActive; }

float fmBaselineFor(uint8_t openCount) {
  if (openCount < 1 || openCount > NUM_CHANNELS) return 0.0;
  return myConfig.cleanFlowRate[openCount - 1];
}

bool flowMonitorNeedUpdate() {
  bool d = fmDirty;
  fmDirty = false;
  return d;
}

// 🧽 «Фильтр прочищен»: сбрасываем эталоны (пере-калибровка) и снимаем тревогу
void flowMonitorRecalibrate() {
  for (uint8_t i = 0; i < NUM_CHANNELS; i++) myConfig.cleanFlowRate[i] = 0.0;
  fmAlertActive = false;
  fmBadWindows = 0;
  fmDirty = true;
  data.updateNow();
  LOG_I("Эталон фильтра сброшен — калибровка на чистом фильтре");
}

// 📊 Обработка одного измерения скорости при cnt открытых клапанах
static void fmProcessSample(uint8_t cnt, float rate) {
  fmRate = rate;
  float baseline = myConfig.cleanFlowRate[cnt - 1];
  LOG_D("Поток: %.2f л/мин при %d кл. (эталон %.2f)", rate, cnt, baseline);

  // 📈 Авто-обучение: чистый фильтр = самый быстрый поток → поднимаем эталон
  if (rate > baseline) {
    myConfig.cleanFlowRate[cnt - 1] = rate;
    fmDirty = true;
    fmBadWindows = 0;
    LOG_D("Новый эталон потока (%d кл.): %.2f л/мин", cnt, rate);
    return;
  }
  if (baseline <= 0.0) return;  // эталона ещё нет — нечего сравнивать

  float ratioPct = rate / baseline * 100.0;
  if (ratioPct < myConfig.clogThresholdPercent) {
    if (fmBadWindows < 255) fmBadWindows++;
    LOG_W("Низкий поток: %.2f л/мин (%d%% от эталона), окно %d/%d",
          rate, (int)ratioPct, fmBadWindows, FM_BAD_WINDOWS);
    if (fmBadWindows >= FM_BAD_WINDOWS) {
      unsigned long now = millis();
      if (!fmAlertActive || (now - fmLastAlertMs > FM_ALERT_REPEAT_MS)) {
        fmAlertActive = true;
        fmLastAlertMs = now;
        LOG_W("ЗАСОР ФИЛЬТРА подтверждён: %.2f л/мин при норме %.2f (%d кл.)",
              rate, baseline, cnt);
        sendTelegramStatus("🧽 Похоже, засорился фильтр! Поток " + String(rate, 2)
          + " л/мин при норме " + String(baseline, 2) + " л/мин ("
          + String((int)ratioPct) + "% от эталона), открыто клапанов: "
          + String(cnt) + ". Прочистите фильтр.");
      }
    }
  } else {
    fmBadWindows = 0;
    if (fmAlertActive) {
      fmAlertActive = false;
      LOG_I("Поток фильтра восстановился: %.2f л/мин", rate);
      sendTelegramStatus("✅ Поток воды восстановился — фильтр в норме.");
    }
  }
}

// ⏱️ Тик контроля фильтра — вызывать в каждом loop()
void flowMonitorTick() {
  if (!myConfig.flowMonitorEnabled) { fmCount = 0; fmMeasuring = false; return; }

  uint8_t cnt = countValveOpen();
  // ✅ Качественное окно: есть полив, не идёт пролив дренажа и заливка бака
  bool qualified = (cnt > 0) && !valveIsDraining() && !fillActive;
  if (!qualified) { fmCount = 0; fmMeasuring = false; return; }

  unsigned long now = millis();

  // 🔄 Сменилась конфигурация — перезапуск с фазы выхода на режим
  if (cnt != fmCount) {
    fmCount = cnt;
    fmMeasuring = false;
    fmPhaseStartMs = now;
    return;
  }

  // ⏳ Фаза выхода на режим (даём потоку устаканиться, уходит воздух)
  if (!fmMeasuring) {
    if (now - fmPhaseStartMs >= FM_SETTLE_MS) {
      fmMeasuring = true;
      fmMeasureStartMs = now;
      fmMeasureStartPulses = flowPulseCount;
    }
    return;
  }

  // 📟 Фаза измерения — ждём окно FM_WINDOW_MS
  if (now - fmMeasureStartMs < FM_WINDOW_MS) return;

  unsigned long dPulses = flowPulseCount - fmMeasureStartPulses;
  unsigned long dt = now - fmMeasureStartMs;
  // 🔁 Следующее окно стартует сразу — непрерывный мониторинг
  fmMeasureStartMs = now;
  fmMeasureStartPulses = flowPulseCount;

  // 💧 Почти нет потока при открытых клапанах — возможно пустой бак/сухой ход,
  // это не «засор», отдельный случай — пропускаем (без ложной тревоги)
  if (dPulses < FM_MIN_PULSES) {
    LOG_W("Почти нет потока (%lu имп.) при %d открытых клапанах — пустой бак/сухой ход?",
          dPulses, cnt);
    return;
  }

  float liters = dPulses / FLOW_PULSES_PER_LITER;
  float minutes = dt / 60000.0;
  if (minutes <= 0.0) return;
  fmProcessSample(cnt, liters / minutes);
}

// 📟 Получить актуальную дату и время (синхронизация NTP + RTC)
// Время между синхронизациями ведётся локально (millis + RTC), поэтому
// принудительный сетевой запрос делаем не чаще раза в NTP_SYNC_INTERVAL,
// а не при каждом вызове (раньше дёргали сеть на каждом открытии клапана).
Datime getDateTime() {
  static uint32_t lastSync = 0;
  uint32_t nowMs = millis();
  if (lastSync == 0 || nowMs - lastSync >= NTP_SYNC_INTERVAL) {
    lastSync = nowMs;
    NTP.updateNow();      // 🔄 Принудительная синхронизация по NTP (раз в час)
  }
  Datime now = NTP;       // 📟 Локальное время (millis + RTC) — без сети
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