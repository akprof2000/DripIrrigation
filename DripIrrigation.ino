#include "objects.h"
#include "telegram.h"
#include "init.h"
#include <esp_task_wdt.h>
#include "valves.h"
#include "irrigation.h"  // 🌱 чистая доменная логика решения о поливе
#include "log.h"
#include <GyverNTP.h>

// GyverDS3231 поддерживает работу с GyverNTP
#include <GyverDS3231Min.h>
GyverDS3231Min rtc;

#define WDT_TIMEOUT 300  // ⏱️ Таймаут watchdog: 300 секунд (5 минут)

// ⏱️ Таймер основного цикла проверки полива
unsigned long prevCheck = 0;
unsigned long intervalCheck = CHECK_INTERVAL;

// ============================================================
// 🚀 SETUP — Инициализация системы при включении питания
// ============================================================
void setup() {
#if LOG_LEVEL > LOG_LEVEL_NONE
  Serial.begin(115200);  // 🖥️ UART нужен только при включённом логировании
#endif
  delay(1000);

  // 🔌 Инициализация шины I2C (нужна клапанам PCF8574 и RTC)
  Wire.begin();
  // 🌱 Инициализация всех модулей системы
  systemInit();  // 📡 WiFi, EEPROM, SD, датчики
  botInit();     // 🤖 Telegram бот
  valvesInit();  // 🚰 Клапаны через PCF8574

  // 🕐 Инициализация RTC (часы реального времени)
  while (!rtc.begin()) {
    delay(1000);
    LOG_E("RTC не найден");
  }

  NTP.begin(3);  // 🌍 Запуск NTP с часовым поясом UTC+3 (Москва)

  // 🔗 Подключаем RTC к NTP для автоматической синхронизации
  NTP.attachRTC(rtc);


  // 🔌 Настройка пинов датчиков и выходов
  pinMode(LIGHT, INPUT);
  pinMode(RAIN, INPUT);
  pinMode(FILL, OUTPUT);

  digitalWrite(FILL, LOW);

  // 🐕 Настройка Watchdog Timer
  LOG_I("Настройка watchdog (%d с)", WDT_TIMEOUT);
  esp_task_wdt_config_t twdt_config = {
    .timeout_ms = WDT_TIMEOUT * 1000,
    .idle_core_mask = 1,  // 🖥️ Маска ядер
    .trigger_panic = true,
  };

  esp_task_wdt_deinit();
  esp_task_wdt_init(&twdt_config);
  esp_task_wdt_add(NULL);
}

// ============================================================
// 📊 Глобальные переменные для отслеживания состояния
// ============================================================
int64_t oldTime = 0;  // ⏱️ Предыдущая минута (для записи в CSV)

bool oldNMode = false;  // 🌙 Предыдущее состояние ночного режима
bool oldRMode = false;  // 🌧️ Предыдущее состояние дождя

int oldM = 0;  // 📅 Предыдущий месяц
int oldD = 0;  // 📅 Предыдущий день
int oldY = 0;  // 📅 Предыдущий год

File dataFile;  // 💾 Текущий файл CSV для записи
String fn;      // 📁 Имя текущего файла данных

// 💡 Мигание LED (индикация работы)
const unsigned long blinkInt = 300;
unsigned long prevBlink = 0;
bool blink = false;

// 🚰 Неблокирующий импульс заливки бака (пин FILL)
unsigned long fillStart = 0;
bool fillActive = false;

// ============================================================
// 🚰 Проверка и управление конкретным клапаном
// ============================================================
// 📨 Презентация: формируем текст уведомления по новому состоянию клапана
static void formatValveEvent(char* buf, size_t n, int i, uint8_t state, int p) {
  const char* title = myConfig.chanel[i].title;
  int border = myConfig.chanel[i].border;
  switch (state) {
    case VState::OpenByHum:
      snprintf(buf, n, "🚰 Клапан № %d (%s) открыт по порогу влажности (%d %%), текущая влажность %d %%", i + 1, title, border, p);
      break;
    case VState::CloseByHum:
      snprintf(buf, n, "⛔ Клапан № %d (%s) закрыт по порогу влажности (%d %%), текущая влажность %d %%", i + 1, title, border, p);
      break;
    case VState::Hysteresis:
      snprintf(buf, n, "➖ Клапан № %d (%s) находится в промежуточном состоянии (%d %%), текущая влажность %d %%", i + 1, title, border, p);
      break;
    case VState::ForcedOpen:
      snprintf(buf, n, "✅ Клапан № %d (%s) открыт по настройке, текущая влажность %d %%", i + 1, title, p);
      break;
    case VState::ForcedClose:
      snprintf(buf, n, "⛔ Клапан № %d (%s) закрыт по настройке, текущая влажность %d %%", i + 1, title, p);
      break;
    default:
      buf[0] = '\0';
      break;
  }
}

// 🚰 Оболочка: получает чистое решение и применяет его (железо + уведомление)
void checkValve(int i) {
  int p = hs.Percent(i);  // 💧 Текущая влажность в процентах
  uint8_t mode = myConfig.chanel[i].mode;

  // 🖨️ Отладочный вывод порогов (только в авто/парнике, как и раньше)
  if (mode == Mode::Auto || mode == Mode::Greenhouse) {
    int b = myConfig.chanel[i].border;
    int d = myConfig.deltaHum;
    LOG_D("Канал %d: влажность %d%%, порог %d ±%d (%d..%d)",
          i, p, b, d, b - d, b + d);
  }

  // 🧠 Чистое решение (без побочных эффектов)
  ValveDecision dec = decideValve(mode, p, myConfig.chanel[i].border, myConfig.deltaHum, oldMode[i]);

  // ⚙️ Применяем действие к железу
  if (dec.action == ValveAction::Open) valveOpen(i);
  else if (dec.action == ValveAction::Close) valveClose(i);
  // ValveAction::Hold — ничего не делаем

  // 📨 Уведомление об изменении (заодно фиксируем новое состояние)
  if (dec.notify) {
    oldMode[i] = dec.newState;
    char msg[256];
    formatValveEvent(msg, sizeof(msg), i, dec.newState, p);
    LOG_I("%s", msg);
    sendTelegramStatus(msg);
  }
}

// ============================================================
// 💡 Мигание встроенным LED (индикация работы системы)
// ============================================================
static void handleBlink() {
  unsigned long now = millis();
  if (now - prevBlink >= blinkInt) {
    prevBlink = now;
    digitalWrite(LED_BUILTIN, blink ? HIGH : LOW);
    blink = !blink;
  }
}

// ============================================================
// 📁 Создание нового CSV-файла при наступлении нового дня
// ============================================================
static void rotateLogFileIfNewDay(Datime t) {
  if (oldY < t.year || oldM < t.month || oldD < t.day) {
    fn = "/" + String(t.year) + "/" + IntWith2Zero(t.month) + "/" + IntWith2Zero(t.day) + ".csv";
    SD.mkdir("/" + String(t.year));
    SD.mkdir("/" + String(t.year) + "/" + IntWith2Zero(t.month));

    if (!SD.exists(fn)) {
      LOG_I("Создаю файл данных: %s", fn.c_str());
      dataFile = SD.open(fn, FILE_WRITE);
      dataFile.println("UnixTime,DateTime,Index,Title,Humidity,Valve,Border,Night,Rain");
      dataFile.close();
    }
    oldY = t.year;
    oldM = t.month;
    oldD = t.day;
  }
}

// ============================================================
// 🚰 Запуск/завершение импульса заливки бака (без блокировки)
// ============================================================
static void startTankFill() {
  if (fillActive) return;  // 🔁 импульс уже идёт
  LOG_I("Старт заливки бака");
  digitalWrite(FILL, HIGH);
  fillStart = millis();
  fillActive = true;
  sendTelegramStatus("🚰 Старт заливки бака!");
}

static void handleTankFill() {
  if (fillActive && millis() - fillStart >= FILLING_WAIT) {
    digitalWrite(FILL, LOW);
    fillActive = false;
  }
}

// ============================================================
// 🌦️ Оценка погоды (дождь/ночь) + связанные уведомления
// ============================================================
static Weather checkWeather() {
  Weather w;

  // 🌧️ Проверка дождя
  if (digitalRead(RAIN) == LOW) {
    LOG_D("Дождь");
    if (!rainNow) {
      sendTelegramStatus("🌧️ Пошёл сильный дождь");
      rainNow = true;
    }
    if (!myConfig.runOnRain) {
      w.blocked = true;
      w.rainBlock = true;
      if (!oldRMode) {
        LOG_I("Дождь — отключаю полив");
        oldRMode = true;
        sendTelegramStatus("🌧️ Идёт дождь — отключаем полив!");
      }
    }
  } else {
    if (rainNow) {
      rainNow = false;
      sendTelegramStatus("☀️ Влажность после дождя достигла нормы");
    }
    if (oldRMode) {
      LOG_I("Дождь закончился — возобновляю полив");
      oldRMode = false;
      sendTelegramStatus("☀️ Восстановление работы после дождя!");
    }
  }

  // 🌙 Проверка ночи
  if (digitalRead(LIGHT) == HIGH) {
    LOG_D("Ночь");
    if (!nightNow) {
      nightNow = true;
      sendTelegramStatus("🌙 Наступила ночь");
    }
    if (!myConfig.runOnNight) {
      w.blocked = true;
      w.nightBlock = true;
      if (!oldNMode) {
        LOG_I("Ночь — энергосбережение");
        oldNMode = true;
        sendTelegramStatus("🌙 Переход в энергосберегающее состояние!");
        if (valveOpened() == true) {
          startTankFill();  // ⏱️ неблокирующий импульс заливки бака
        }
      }
    }
  } else {
    LOG_D("День");
    if (nightNow) {
      nightNow = false;
      sendTelegramStatus("☀️ Наступил день");
    }
    if (oldNMode) {
      LOG_I("День — возобновляю работу");
      oldNMode = false;
      sendTelegramStatus("☀️ Восстановление работы после ночного режима!");
      if (rainNow && !myConfig.runOnRain)
        sendTelegramStatus("🌧️ Погодная обстановка не достигла нормы!");
    }
  }

  return w;
}

// ============================================================
// 🚰 Управление всеми клапанами с учётом погодных блокировок
// ============================================================
static void controlValves(const Weather& w) {
  if (!w.blocked) {
    for (int i = 0; i < NUM_CHANNELS; i++) {
      checkValve(i);
    }
    return;
  }

  for (int i = 0; i < NUM_CHANNELS; i++) {
    // 🏠 В режиме парника или принудительно включён — поливаем даже при дожде (но не ночью)
    if (!w.nightBlock && w.rainBlock && (myConfig.chanel[i].mode == Mode::Greenhouse || myConfig.chanel[i].mode == Mode::AlwaysOn)) {
      checkValve(i);
    } else {
      valveClose(i);
      if (oldMode[i] != VState::ForcedClose) {
        oldMode[i] = VState::ForcedClose;
        char msg[160];
        snprintf(msg, sizeof(msg), "⛔ Клапан № %d (%s) закрыт", i + 1, myConfig.chanel[i].title);
        sendTelegramStatus(msg);
      }
    }
  }
}

// ============================================================
// 💾 Запись текущего состояния всех каналов в CSV
// ============================================================
static void logToCsv(Datime t, uint32_t curr) {
  LOG_D("Запись данных в CSV");
  dataFile = SD.open(fn, FILE_APPEND);
  if (dataFile) {
    for (int i = 0; i < NUM_CHANNELS; i++) {
      String row = String(curr) + ","
                   + t.toString(' ') + ","
                   + String(i + 1) + ","
                   + String(myConfig.chanel[i].title) + ","
                   + String(hs.Percent(i)) + ","
                   + String((oldMode[i] == VState::ForcedClose || oldMode[i] == VState::CloseByHum) ? 0 : 1) + ","
                   + String(myConfig.chanel[i].border) + ","
                   + String(nightNow ? 1 : 0) + ","
                   + String(rainNow ? 1 : 0);

      LOG_D("%s", row.c_str());
      dataFile.println(row);
    }
  } else {
    sendTelegramStatus("❌ Ошибка записи в файл: " + fn);
    LOG_E("Не открыть файл для записи: %s", fn.c_str());
  }
  dataFile.close();
}

// ============================================================
// ⏱️ Защита насоса: автоотключение по таймауту работы
// ============================================================
static void handlePumpTimeout() {
  // ⏱️ Считаем по millis() — защита не зависит от синхронизации NTP/RTC.
  if (pumpStart != 0 && (millis() - pumpStart > (unsigned long)PUMP_TIMEOUT * 1000UL)) {
    LOG_W("Насос: завершён пусковой режим по таймауту (%d с)", PUMP_TIMEOUT);
    pumpStart = 0;
    stopPumpIfNeed();  // 💪 решает по boostPumpValves: оставить ВКЛ или выключить
  }
}

// ============================================================
// 🔧 Авто-калибровка границ датчиков влажности
// ============================================================
// Если показание устойчиво выходит за границу, заданную при калибровке (почва
// оказалась влажнее «воды» или суше «сухого»), граница отодвигается до этого
// значения с запасом в дельту калибровки. Границы только расширяются.
//
// 🛡️ Разовые выбросы АЦП игнорируются: граница двигается, только когда
// превышение держится HUM_AUTOCAL_CONFIRM минутных замеров подряд (см. sensors.h).
//
// ⛔ Работает ТОЛЬКО в режиме измерения: пока идёт диалог калибровки или поиска
// датчика, датчик держат в воде / на воздухе — такие показания нельзя принимать
// за рабочие, иначе авто-калибровка «выучит» калибровочные экстремумы.
static bool calibBoundsDirty = false;

static void autoCalibrateBounds() {
  if (calibrationInProgress()) return;

  // ⚡ «Без задержки» — двигаем границу по первому же отсчёту за ней;
  //    иначе ждём подтверждения HUM_AUTOCAL_CONFIRM замеров подряд.
  uint8_t confirm = myConfig.autoCalNoDelay ? 1 : HUM_AUTOCAL_CONFIRM;

  for (int i = 0; i < NUM_CHANNELS; i++) {
    if (hs.autoExtend(i, confirm)) {
      myConfig.chanel[i].minVal = hs.getLow(i);
      myConfig.chanel[i].maxVal = hs.getHigh(i);
      calibBoundsDirty = true;
      LOG_I("Канал %d: границы расширены до [%d; %d] (АЦП %d)",
            i + 1, hs.getLow(i), hs.getHigh(i), hs.getCurrent(i));
    }
  }
}

// ============================================================
// 💾 Сохранение конфигурации, если есть незаписанные изменения
// ============================================================
static void saveConfigIfDirty() {
  bool duv = valveNeedUpdate();
  bool dut = telegramNeedUpdate();
  bool duf = flowMonitorNeedUpdate();  // 🧽 обновился эталон фильтра
  bool duc = calibBoundsDirty;         // 🔧 авто-расширены границы калибровки
  calibBoundsDirty = false;
  if (duv || dut || duf || duc) {
    data.update();
  }
}

// ============================================================
// 🔄 ГЛАВНЫЙ ЦИКЛ LOOP
// ============================================================
void loop() {
  handleBlink();      // 💡 индикация работы
  handleTankFill();   // ⏱️ неблокирующий импульс заливки бака
  spillageTick();     // ⏱️ неблокирующий пролив дренажа
  handlePumpTimeout(); // ⏱️ завершение пускового режима насоса (каждый loop, по millis)
  flowMonitorTick();  // 🧽 контроль засора фильтра по скорости потока
  ReCheck();          // 🤖 Telegram + проверка WiFi

  unsigned long now = millis();
  if (now - prevCheck < intervalCheck) return;  // ⏱️ ещё не пора — выходим
  prevCheck = now;

  Datime t = getDateTime();
  uint32_t curr = t.getUnix();

  // 📅 Основная логика полива и логирование — раз в минуту
  if (oldTime < int64_t(curr / 60)) {
    flowGetSessionLitersTick();
    rotateLogFileIfNewDay(t);
    oldTime = int64_t(curr / 60);

    LOG_D("Проверка состояния");
    hs.setAll();          // 💧 Опрос всех датчиков влажности
    autoCalibrateBounds(); // 🔧 расширить границы, если показания вышли за диапазон

    Weather w = checkWeather();
    controlValves(w);
    logToCsv(t, curr);
  }

  saveConfigIfDirty();      // 💾 отложенное сохранение конфига
  esp_task_wdt_reset();     // 🐕 сброс watchdog
}
