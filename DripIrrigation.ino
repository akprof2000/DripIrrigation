#include "objects.h"
#include "telegram.h"
#include "init.h"
#include <esp_task_wdt.h>
#include "valves.h"
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
  Serial.begin(115200);
  delay(1000);

  // 🕐 Инициализация RTC (часы реального времени)
  Wire.begin();
  while (!rtc.begin()) {
    delay(1000);
    Serial.println("❌ Couldn't find RTC");
    Serial.flush();
  }

  NTP.begin(3);  // 🌍 Запуск NTP с часовым поясом UTC+3 (Москва)

  // 🔗 Подключаем RTC к NTP для автоматической синхронизации
  NTP.attachRTC(rtc);

  // 🌱 Инициализация всех модулей системы
  init();        // 📡 WiFi, EEPROM, SD, датчики
  botInit();     // 🤖 Telegram бот
  valves_init(); // 🚰 Клапаны через PCF8574

  // 🔌 Настройка пинов датчиков и выходов
  pinMode(LIGHT, INPUT);
  pinMode(RAIN, INPUT);
  pinMode(FILL, OUTPUT);

  digitalWrite(FILL, LOW);

  // 🐕 Настройка Watchdog Timer
  Serial.println("🐕 Configuring WDT...");
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
int64_t oldTime = 0;      // ⏱️ Предыдущая минута (для записи в CSV)

bool oldNMode = false;    // 🌙 Предыдущее состояние ночного режима
bool oldRMode = false;    // 🌧️ Предыдущее состояние дождя

int oldM = 0;             // 📅 Предыдущий месяц
int oldD = 0;             // 📅 Предыдущий день
int oldY = 0;             // 📅 Предыдущий год

File dataFile;            // 💾 Текущий файл CSV для записи
String fn;                // 📁 Имя текущего файла данных

// 💡 Мигание LED (индикация работы)
const unsigned long blinkInt = 300;
unsigned long prevBlink = 0;
bool blink = false;

// ============================================================
// 🚰 Проверка и управление конкретным клапаном
// ============================================================
void checkValve(int i) {
  int p = hs.Percent(i);  // 💧 Текущая влажность в процентах

  // 🤖 Автоматический режим (0) или режим парника (3)
  if (myConfig.chanel[i].mode == 0 || myConfig.chanel[i].mode == 3) {
    Serial.print("💧 Current percent Humidity :");
    Serial.print(p);
    Serial.print(" for ");
    Serial.print(i);
    int b = myConfig.chanel[i].border;
    int d = myConfig.deltaHum;
    Serial.print(" border ");
    Serial.print(b);
    Serial.print(" with delta ");
    Serial.print(d);
    Serial.print(" from ");
    Serial.print(b - d);
    Serial.print(" to ");
    Serial.println(b + d);

    // 🚰 Влажность ниже порога — открываем клапан
    if (p < (b - d)) {
      if (oldMode[i] != 1) {
        oldMode[i] = 1;
        sendTelegramStatus("🚰 Клапан № " + String(i + 1) + " (" + myConfig.chanel[i].title + ") открыт по порогу влажности (" + myConfig.chanel[i].border + " %), текущая влажность " + p + " %");
      }
      valve_open(i);
    }
    // ⛔ Влажность выше порога — закрываем клапан
    else if (p > (b + d)) {
      if (oldMode[i] != 2) {
        oldMode[i] = 2;
        sendTelegramStatus("⛔ Клапан № " + String(i + 1) + " (" + myConfig.chanel[i].title + ") закрыт по порогу влажности (" + myConfig.chanel[i].border + " %), текущая влажность " + p + " %");
      }
      valve_close(i);
    }
    // ➖ Влажность в пределах гистерезиса — промежуточное состояние
    else {
      if (oldMode[i] != 1 && oldMode[i] != 2 && oldMode[i] != 3) {
        oldMode[i] = 3;
        sendTelegramStatus("➖ Клапан № " + String(i + 1) + " (" + myConfig.chanel[i].title + ") находится в промежуточном состоянии (" + myConfig.chanel[i].border + " %), текущая влажность " + p + " %");
      }
    }
  }
  // ✅ Ручной режим: постоянно открыт (1) или закрыт (2)
  else {
    if (myConfig.chanel[i].mode == 1) {
      if (oldMode[i] != 10) {
        oldMode[i] = 10;
        sendTelegramStatus("✅ Клапан № " + String(i + 1) + " (" + myConfig.chanel[i].title + ") открыт по настройке, текущая влажность " + p + " %");
      }
      valve_open(i);
    } else {
      if (oldMode[i] != 11) {
        oldMode[i] = 11;
        sendTelegramStatus("⛔ Клапан № " + String(i + 1) + " (" + myConfig.chanel[i].title + ") закрыт по настройке, текущая влажность " + p + " %");
      }
      valve_close(i);
    }
  }
}

// ============================================================
// 🔄 ГЛАВНЫЙ ЦИКЛ LOOP
// ============================================================
void loop() {
  // 💡 Мигание LED (индикация работы системы)
  unsigned long currBlink = millis();
  if (currBlink - prevBlink >= blinkInt) {
    prevBlink = currBlink;
    if (blink) {
      digitalWrite(LED_BUILTIN, HIGH);
    } else {
      digitalWrite(LED_BUILTIN, LOW);
    }
    blink = !blink;
  }

  // 🤖 Обработка Telegram и проверка WiFi
  ReCheck();

  unsigned long currentMillis = millis();
  if (currentMillis - prevCheck >= intervalCheck) {
    flowGetSessionLitersTick();
    prevCheck = currentMillis;
    Datime t = getDateTime();
    uint32_t curr = t.getUnix();

    // 📅 Проверяем, началась ли новая минута
    if (oldTime < (int64_t(curr / 60))) {
      // 📁 Проверяем, начался ли новый день — создаём новый файл CSV
      if (oldY < t.year || oldM < t.month || oldD < t.day) {
        fn = "/" + String(t.year) + "/" + IntWith2Zero(t.month) + "/" + IntWith2Zero(t.day) + ".csv";
        SD.mkdir("/" + String(t.year));
        SD.mkdir("/" + String(t.year) + "/" + IntWith2Zero(t.month));

        Serial.print("📁 Check file ");
        Serial.println(fn);
        if (!SD.exists(fn)) {
          Serial.print("📁 file not found — create new file: ");
          Serial.println(fn);
          dataFile = SD.open(fn, FILE_WRITE);
          dataFile.println("UnixTime,DateTime,Index,Title,Humidity,Valve,Border,Night,Rain");
          dataFile.close();
        }
        oldY = t.year;
        oldM = t.month;
        oldD = t.day;
      }

      oldTime = int64_t(curr / 60);

      Serial.println(F("📊 Check status"));
      hs.setAll();  // 💧 Опрос всех датчиков влажности

      // 🌧️🌙 Проверка погодных условий
      bool blocked = false;   // ⛔ Блокировка полива
      bool nightW = false;    // 🌙 Флаг ночной блокировки
      bool rainW = false;     // 🌧️ Флаг дождевой блокировки

      // 🌧️ Проверка дождя
      int rain_t = digitalRead(RAIN);
      if (rain_t == LOW) {
        Serial.println(F("🌧️ Rain"));
        if (!rainNow) {
          sendTelegramStatus("🌧️ Пошёл сильный дождь");
          rainNow = true;
        }
        if (!myConfig.runOnRain) {
          blocked = true;
          rainW = true;
          if (!oldRMode) {
            Serial.println(F("🌧️ Set rain low power"));
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
          Serial.println(F("☀️ Set rain high power"));
          oldRMode = false;
          sendTelegramStatus("☀️ Восстановление работы после дождя!");
        }
      }

      // 🌙 Проверка ночи
      int night = digitalRead(LIGHT);
      if (night == HIGH) {
        Serial.println(F("🌙 Night"));
        if (!nightNow) {
          nightNow = true;
          sendTelegramStatus("🌙 Наступила ночь");
        }
        if (!myConfig.runOnNight) {
          blocked = true;
          nightW = true;
          if (!oldNMode) {
            Serial.println(F("🌙 Set night low power"));
            oldNMode = true;
            sendTelegramStatus("🌙 Переход в энергосберегающее состояние!");
            if (valve_opened() == true) {
              Serial.println(F("🚰 Set filling signal"));
              digitalWrite(FILL, HIGH);
              delay(FILLING_WAIT);
              digitalWrite(FILL, LOW);
              sendTelegramStatus("🚰 Старт заливки бака!");
            }
          }
        }
      } else {
        Serial.println(F("☀️ Day"));
        if (nightNow) {
          nightNow = false;
          sendTelegramStatus("☀️ Наступил день");
        }
        if (oldNMode) {
          Serial.println(F("☀️ Set night high power"));
          oldNMode = false;
          sendTelegramStatus("☀️ Восстановление работы после ночного режима!");
          if (rainNow && !myConfig.runOnRain)
            sendTelegramStatus("🌧️ Погодная обстановка не достигла нормы!");
        }
      }

      // 🚰 Управление клапанами в зависимости от блокировок
      if (!blocked) {
        hs.setAll();
        for (int i = 0; i < 8; i++) {
          checkValve(i);
        }
      } else {
        for (int i = 0; i < 8; i++) {
          // 🏠 В режиме парника (3) или принудительно включён (1) — проверяем даже при дожде
          if (!nightW && rainW && (myConfig.chanel[i].mode == 3 || myConfig.chanel[i].mode == 1)) {
            checkValve(i);
          } else {
            valve_close(i);
            if (oldMode[i] != 11) {
              oldMode[i] = 11;
              sendTelegramStatus("⛔ Клапан № " + String(i + 1) + " (" + myConfig.chanel[i].title + ") закрыт");
            }
          }
        }
      }

      // 💾 Запись данных в CSV файл
      Serial.println("💾 Write file data");
      dataFile = SD.open(fn, FILE_APPEND);
      if (dataFile) {
        for (int i = 0; i < 8; i++) {
          String row = String(curr) + ","
                       + t.toString(' ') + ","
                       + String(i + 1) + ","
                       + String(myConfig.chanel[i].title) + ","
                       + String(hs.Percent(i)) + ","
                       + String((oldMode[i] == 11 || oldMode[i] == 2) ? 0 : 1) + ","
                       + String(myConfig.chanel[i].border) + ","
                       + String(nightNow ? 1 : 0) + ","
                       + String(rainNow ? 1 : 0);

          Serial.println(row);
          dataFile.println(row);
        }
      } else {
        sendTelegramStatus("❌ Ошибка записи в файл: " + fn);
        Serial.print("❌ Can not open file to write: ");
        Serial.println(fn);
      }
      dataFile.close();
    }

    // ⏱️ Защита насоса: автоматическое отключение по таймауту
    if (pumpStart != 0) {
      if (curr - pumpStart > PUMP_TIMEOUT) {
        pumpStart = 0;
        digitalWrite(PUMP, LOW);
      }
    }

    // 💾 Проверяем необходимость сохранения конфигурации
    bool duv = valve_needUpdate();
    bool dut = telegram_needUpdate();

    if (duv || dut) {
      data.update();
    }

    // 🐕 Сброс Watchdog Timer
    esp_task_wdt_reset();
  }
}
