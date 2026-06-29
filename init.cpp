// init.cpp 🌱💧 Инициализация системы капельного полива
#include "init.h"
#include <WiFi.h>
#include <EEPROM.h>

#include "SimplePortal.h"
#include "objects.h"
#include "telegram.h"
#include "log.h"
#include <SD.h>

// 📡 WiFi настройки
char SSID[32] = "";
char pass[32] = "";

wifi_mode_t mode = WIFI_AP;  // 📡 1=WIFI_STA, 2=WIFI_AP

byte init_config = 0;  // ⚙️ Флаг первичной инициализации (0=нужна настройка)

// ⏱️ Таймеры для проверки WiFi
unsigned long previousMillis = 0;
unsigned long interval = CHECK_WIFI_INTERVAL;

unsigned long previousMillisSmall = 0;
unsigned long intervalSmall = CHECK_WIFI_INTERVAL_SMALL;

unsigned long lastGood = 0;  // ⏱️ Время последнего удачного соединения

bool cd_card = true;  // 💾 Флаг наличия SD-карты

// ============================================================
// 🔄 Периодическая проверка WiFi и обработка Telegram
// ============================================================
void ReCheck() {
  unsigned long currentMillis = millis();

  if (currentMillis - previousMillisSmall >= intervalSmall) {
    previousMillisSmall = currentMillis;

    // 🤖 Вызываем тикер FastBot2 (обработка входящих сообщений)
    bool gotUpdate = bot.tick();
    // 🔌 Контроль связи с Telegram (детект разрыва/восстановления)
    botMonitorTick(gotUpdate);

    // 🔄 Если установлен флаг перезагрузки — выполняем tickManual и перезагружаем
    if (res) {
      bot.tickManual();  // 📤 Чтобы отметить сообщение прочитанным
      ESP.restart();
    }

    // 🆘 Проверяем наличие ошибок соединения с Telegram

    // 📡 Если WiFi не работает или есть ошибка Telegram — пытаемся переподключиться
    // 📡 Неблокирующий контроль WiFi:
    //   • восстановление соединения обнаруживаем сразу (на каждом малом цикле);
    //   • саму попытку переподключения делаем не чаще раза в interval — без busy-wait.
    if (WiFi.status() == WL_CONNECTED) {
      if (dropped) {
        dropped = false;
        LOG_I("WiFi восстановлен");  // 📨 уведомление шлёт botMonitorTick (единый источник)
      }
      lastGood = currentMillis;
    } else {
      if (!dropped) {
        dropped = true;
        LOG_W("WiFi потерян");
      }
      // 🔄 Попытка переподключения по таймеру (WiFi.reconnect() не блокирует)
      if (currentMillis - previousMillis >= interval) {
        previousMillis = currentMillis;
        LOG_I("Переподключение к WiFi...");
        WiFi.reconnect();
      }
    }
  }

  // 💾 Проверяем необходимость сохранения конфигурации на SD
  if (data.tick() == FD_WRITE) LOG_D("Конфиг сохранён на SD");
}

// ============================================================
// 📡 Обработчики событий WiFi
// ============================================================
void WiFiStationConnected(WiFiEvent_t event, WiFiEventInfo_t info) {
  LOG_I("Подключено к точке доступа");
}

void WiFiGotIP(WiFiEvent_t event, WiFiEventInfo_t info) {
  LOG_I("WiFi подключён, IP: %s", WiFi.localIP().toString().c_str());
}

void WiFiStationDisconnected(WiFiEvent_t event, WiFiEventInfo_t info) {
  LOG_W("WiFi отключён (причина %d) — переподключение", info.wifi_sta_disconnected.reason);
  WiFi.begin(SSID, pass);
}

// ============================================================
// 🚀 Главная функция инициализации системы
// ============================================================
void systemInit() {
  LOG_I("Инициализация системы");

  // 📡 Подключаем функцию отправки статуса
  attachSendFunction(sendStatus);

  EEPROM.begin(4096);
  delay(1000);

  // 📡 Регистрируем обработчики событий WiFi
  WiFi.onEvent(WiFiStationConnected, WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_CONNECTED);
  WiFi.onEvent(WiFiGotIP, WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_GOT_IP);

  // 🔌 Настройка пинов
  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(DRAIN, OUTPUT);
  pinMode(BUTTON, INPUT);
  digitalWrite(DRAIN, LOW);
  pinMode(PUMP, OUTPUT);
  digitalWrite(PUMP, LOW);

  // 💡 Мигание LED при старте + проверка кнопки сброса
  int state = HIGH;
  bool nClear = false;
  for (int i = 0; i < 3000; i++) {
    int buttonState = digitalRead(BUTTON);
    delay(1);
    if (i % 100 == 0) {
      if (state == HIGH) {
        state = LOW;
      } else {
        state = HIGH;
      }
      digitalWrite(LED_BUILTIN, state);
    }
    if (buttonState == HIGH) {
      int rep = 0;
      while (buttonState == HIGH) {
        digitalWrite(LED_BUILTIN, HIGH);
        buttonState = digitalRead(BUTTON);
        delay(100);
        rep++;
        if (rep > 50) {
          nClear = true;
        }
        if (nClear)
          break;
      }
    }
    if (nClear)
      break;
  }

  // 🔄 Сброс настроек при удержании кнопки > 5 сек
  if (nClear) {
    for (int i = 0; i < 6; i++) {
      delay(300);
      if (state == HIGH) {
        state = LOW;
      } else {
        state = HIGH;
      }
      digitalWrite(LED_BUILTIN, state);
    }
    init_config = 0;
    EEPROM.put(0, init_config);
  }

  digitalWrite(LED_BUILTIN, LOW);

  // 📖 Читаем флаг инициализации из EEPROM
  EEPROM.get(0, init_config);

  // ⚙️ Авто-сброс при смене версии конфигурации: если сохранённая в EEPROM версия
  //    не совпадает с прошивкой — выполняем тот же сброс, что и по кнопке (запуск
  //    WiFi-портала + обнуление списка пользователей), без нажатия кнопки.
  uint16_t eepromVer = 0;
  EEPROM.get(EEPROM_VER_ADDR, eepromVer);
  if (eepromVer != CONFIG_VERSION) {
    LOG_W("Версия конфига изменилась (%u -> %d) — авто-сброс к базовым настройкам",
          eepromVer, CONFIG_VERSION);
    init_config = 0;                              // → запустится портал настройки WiFi
    EEPROM.put(0, init_config);
    EEPROM.put(250, (int)0);                      // обнулить список пользователей в EEPROM
    EEPROM.put(EEPROM_VER_ADDR, (uint16_t)CONFIG_VERSION);
    EEPROM.commit();
  }

  LOG_D("init_config = %d", init_config);

  // 🆕 Первичная настройка через WiFi портал
  if (init_config == 0) {
    digitalWrite(LED_BUILTIN, HIGH);
    portalRun(180000);  // ⏱️ 3 минуты на настройку

    LOG_D("Статус портала: %d", portalStatus());
    // 📡 статус: 0 error, 1 connect, 2 ap, 3 local, 4 exit, 5 timeout

    if (portalStatus() == SP_SUBMIT) {
      strcpy(SSID, portalCfg.SSID);
      strcpy(pass, portalCfg.pass);
      strcpy(tstr, portalCfg.tstr);
      mode = portalCfg.mode;

      LOG_D("Портал: SSID=%s сохранён", SSID);
      EEPROM.put(1, SSID);
      EEPROM.put(1 + 33, pass);
      EEPROM.put(1 + 33 + 33, tstr);
      EEPROM.put(1 + 33 + 33 + 33, mode);
      EEPROM.put(250, 0);
      EEPROM.commit();
      // 💾 Сохраняем логин-пароль
      digitalWrite(LED_BUILTIN, LOW);
      LOG_D("Настройки WiFi записаны в EEPROM");
    }
  }

  // 📖 Читаем сохранённые настройки WiFi из EEPROM
  EEPROM.get(1, SSID);
  EEPROM.get(1 + 33, pass);
  EEPROM.get(1 + 33 + 33, tstr);
  EEPROM.get(1 + 33 + 33 + 33, mode);

  LOG_I("Подключение к WiFi: %s", SSID);

  WiFi.setHostname("DripIrrigationEsp");
  WiFi.mode(mode);
  WiFi.begin(SSID, pass);

  // ⏱️ Ожидание подключения к WiFi (макс 15 сек)
  int ind = 0;
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    ind++;
    if (ind > 30) {
      break;
    }
  }

  if (WiFi.status() != WL_CONNECTED) {
    LOG_W("WiFi не подключён — продолжим попытки в фоне");
  } else {
    LOG_I("WiFi подключён, IP: %s", WiFi.localIP().toString().c_str());

    if (init_config == 0) {
      init_config = 1;
      EEPROM.put(0, init_config);
      LOG_D("Флаг init_config сохранён");
      EEPROM.commit();
    }
  }

  EEPROM.end();

  // 💧 Инициализация датчиков влажности
  hs.init();



  // 💾 Инициализация SD-карты
  while (!SD.begin(5)) {
    delay(1000);
    LOG_E("SD: монтирование не удалось — повтор");
  }

  uint8_t cardType = SD.cardType();

  if (cardType == CARD_NONE) {
    LOG_E("SD-карта не подключена");
    return;
  }

  const char* ct = (cardType == CARD_MMC)  ? "MMC"
                 : (cardType == CARD_SD)   ? "SDSC"
                 : (cardType == CARD_SDHC) ? "SDHC"
                                           : "UNKNOWN";
  uint64_t cardSize = SD.cardSize() / (1024 * 1024);
  LOG_I("SD-карта: %s, %llu МБ", ct, cardSize);

  // 📖 Читаем конфигурацию с SD-карты
  FDstat_t stat = data.read();

  switch (stat) {
    case FD_FS_ERR:   LOG_E("Конфиг: ошибка файловой системы"); break;
    case FD_FILE_ERR: LOG_E("Конфиг: ошибка файла"); break;
    case FD_WRITE:    LOG_D("Конфиг: записан"); break;
    case FD_ADD:      LOG_D("Конфиг: создан файл по умолчанию"); break;
    case FD_READ:     LOG_D("Конфиг: прочитан с SD"); break;
    default: break;
  }

  // 🔐 Проверка сигнатуры/версии: если файл чужой/устаревший — сбрасываем в дефолты,
  // а не работаем на «мусоре». FileData уже защищает по размеру, это — доп. страховка.
  if (myConfig.magic != CONFIG_MAGIC || myConfig.version != CONFIG_VERSION) {
    LOG_W("Config magic/version не совпал — сброс настроек в значения по умолчанию");
    myConfig = Config();      // свежие дефолты (magic/version проставляются конструктором)
    data.updateNow();
  }

    // 💧 Инициализация датчика потока воды (пин 27)
  flowInit();

  // 📋 Применяем и выводим прочитанную конфигурацию
  LOG_I("Конфиг: дождь=%d ночь=%d dCal=%d dHum=%d boost=%d clog=%d%% tgTout=%dс",
        myConfig.runOnRain, myConfig.runOnNight, myConfig.deltaCalibration,
        myConfig.deltaHum, myConfig.boostPumpValves, myConfig.clogThresholdPercent,
        myConfig.tgTimeoutSec);
  hs.setBorder(myConfig.deltaCalibration);

  for (int i = 0; i < NUM_CHANNELS; i++) {
    LOG_D("  канал %d: min=%d max=%d порог=%d%% режим=%d", i,
          myConfig.chanel[i].minVal, myConfig.chanel[i].maxVal,
          myConfig.chanel[i].border, myConfig.chanel[i].mode);
    hs.setLowHighValue(i, myConfig.chanel[i].minVal, myConfig.chanel[i].maxVal);
  }
}