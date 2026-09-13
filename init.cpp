// init.cpp 🌱💧 Инициализация системы капельного полива
#include "init.h"
#include <WiFi.h>
#include <EEPROM.h>

#include "SimplePortal.h"
#include "objects.h"
#include "telegram.h"
#include "valves.h"
#include "log.h"
#include "faults.h"
#include <SD.h>

// 💾 Монтирование SD: сколько раз пробуем и с какой паузой. Ограниченное
//    число попыток вместо бесконечного ожидания — загрузка обязана дойти
//    до бота, иначе об аварии некому сообщить.
#define SD_INIT_ATTEMPTS  3
#define SD_INIT_RETRY_MS  300

// 📡 WiFi настройки (размеры — из SimplePortal.h: SSID ≤32, пароль WPA2 ≤63)
char SSID[SP_SSID_LEN] = "";
char pass[SP_PASS_LEN] = "";

// 💾 Раскладка EEPROM для WiFi.
//    Старая: SSID[32]@1, pass[32]@34, tstr[32]@67, mode@100 — пароль упирался в tstr,
//    поэтому расширить его на месте нельзя. Новые SSID/пароль живут в отдельной
//    области после списка пользователей (250..~383); tstr и mode остаются на
//    прежних адресах, токен (110), offset апдейтов (180), версия (240) не тронуты.
//    При первом старте после обновления старые значения переносятся в новую область.
#define EEPROM_TSTR_ADDR  (1 + 33 + 33)
#define EEPROM_MODE_ADDR  (1 + 33 + 33 + 33)
#define EEPROM_OLD_SSID_ADDR 1
#define EEPROM_OLD_PASS_ADDR (1 + 33)
#define EEPROM_WIFI_ADDR  512                             // 📍 начало новой области
#define EEPROM_SSID_ADDR  EEPROM_WIFI_ADDR                // SSID[33]  512..544
#define EEPROM_PASS_ADDR  (EEPROM_WIFI_ADDR + SP_SSID_LEN) // pass[65]  545..609

// 🔁 Перенос WiFi-настроек из старой раскладки, если новая область ещё пуста
static void migrateWifiEeprom() {
  char probe[SP_SSID_LEN];
  EEPROM.get(EEPROM_SSID_ADDR, probe);
  bool newEmpty = (probe[0] == '\0' || (uint8_t)probe[0] == 0xFF);
  if (!newEmpty) return;

  char oldSsid[32], oldPass[32];
  EEPROM.get(EEPROM_OLD_SSID_ADDR, oldSsid);
  EEPROM.get(EEPROM_OLD_PASS_ADDR, oldPass);
  oldSsid[sizeof(oldSsid) - 1] = '\0';
  oldPass[sizeof(oldPass) - 1] = '\0';
  if (oldSsid[0] == '\0' || (uint8_t)oldSsid[0] == 0xFF) return;  // нечего переносить

  char nSsid[SP_SSID_LEN] = "";
  char nPass[SP_PASS_LEN] = "";
  strncpy(nSsid, oldSsid, SP_SSID_LEN - 1);
  strncpy(nPass, oldPass, SP_PASS_LEN - 1);
  EEPROM.put(EEPROM_SSID_ADDR, nSsid);
  EEPROM.put(EEPROM_PASS_ADDR, nPass);
  EEPROM.commit();
  LOG_I("WiFi-настройки перенесены в новую область EEPROM (SSID=%s)", nSsid);
}

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

    // 🔄 Если установлен флаг перезагрузки — подтверждаем очередь и перезагружаем
    if (res) {
      telegramSaveUpdateOffset();  // 📮 позиция очереди переживёт перезагрузку
      bot.tickManual();            // 📤 отметить сообщение прочитанным на сервере
      loadsOff();   // 🛑 клапаны и насос — выключить до перезагрузки
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
  pinMode(BUTTON, INPUT_PULLDOWN);   // нажата = HIGH; без подтяжки вход плавал и мог стереть настройки
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

  // 🤖 Токен бота: сперва из EEPROM, иначе — скомпилированный в secrets.h.
  //    Такой порядок делает прошивку универсальной (готовый бинарник можно
  //    публиковать без секретов) и при этом не ломает сборки со своим secrets.h.
  EEPROM.get(EEPROM_TOKEN_ADDR, botToken);
  botToken[BOT_TOKEN_LEN - 1] = '\0';
  if (botTokenValid(botToken)) {
    LOG_I("Токен бота загружен из EEPROM");
  } else {
    strncpy(botToken, BOT_TOKEN, BOT_TOKEN_LEN - 1);
    botToken[BOT_TOKEN_LEN - 1] = '\0';
    if (botTokenValid(botToken)) LOG_I("Токен бота взят из secrets.h");
  }
  portalTokenKnown = botTokenValid(botToken);  // 💬 подсказки в форме портала

  // 💾 Перенос WiFi-настроек из старой раскладки EEPROM (однократно)
  migrateWifiEeprom();

  // 🆕 Первичная настройка через WiFi портал
  if (init_config == 0) {
    digitalWrite(LED_BUILTIN, HIGH);
    portalRun(180000);  // ⏱️ 3 минуты на настройку

    LOG_D("Статус портала: %d", portalStatus());
    // 📡 статус: 0 error, 1 connect, 2 ap, 3 local, 4 exit, 5 timeout

    if (portalStatus() == SP_SUBMIT) {
      strncpy(SSID, portalCfg.SSID, sizeof(SSID) - 1);
      SSID[sizeof(SSID) - 1] = '\0';
      strncpy(pass, portalCfg.pass, sizeof(pass) - 1);
      pass[sizeof(pass) - 1] = '\0';
      strncpy(tstr, portalCfg.tstr, sizeof(tstr) - 1);
      tstr[sizeof(tstr) - 1] = '\0';
      mode = portalCfg.mode;

      LOG_D("Портал: SSID=%s сохранён", SSID);
      EEPROM.put(EEPROM_SSID_ADDR, SSID);
      EEPROM.put(EEPROM_PASS_ADDR, pass);
      EEPROM.put(EEPROM_TSTR_ADDR, tstr);
      EEPROM.put(EEPROM_MODE_ADDR, mode);
      EEPROM.put(250, 0);
      // 🤖 Токен: пустое поле означает «оставить прежний»
      if (botTokenValid(portalCfg.token)) {
        strncpy(botToken, portalCfg.token, BOT_TOKEN_LEN - 1);
        botToken[BOT_TOKEN_LEN - 1] = '\0';
        EEPROM.put(EEPROM_TOKEN_ADDR, botToken);
        LOG_I("Токен бота сохранён в EEPROM");
      }
      EEPROM.commit();
      // 💾 Сохраняем логин-пароль
      digitalWrite(LED_BUILTIN, LOW);
      LOG_D("Настройки WiFi записаны в EEPROM");
    }
  }

  // 📖 Читаем сохранённые настройки WiFi из EEPROM
  EEPROM.get(EEPROM_SSID_ADDR, SSID);
  EEPROM.get(EEPROM_PASS_ADDR, pass);
  EEPROM.get(EEPROM_TSTR_ADDR, tstr);
  EEPROM.get(EEPROM_MODE_ADDR, mode);
  SSID[sizeof(SSID) - 1] = '\0';  // 🛡️ гарантия терминатора при мусоре в EEPROM
  pass[sizeof(pass) - 1] = '\0';
  tstr[sizeof(tstr) - 1] = '\0';

  // 🤖 Токена нет ни в EEPROM, ни в secrets.h (типичный случай — прошивка,
  //    собранная в CI без секретов). Поднимаем портал ТОЛЬКО чтобы принять токен:
  //    настройки WiFi, список пользователей и калибровки остаются нетронутыми,
  //    и после ввода система продолжает загрузку с того же места.
  if (!botTokenValid(botToken)) {
    LOG_W("Токен бота не задан — портал для ввода токена (настройки сохраняются)");
    digitalWrite(LED_BUILTIN, HIGH);
    portalRun(180000);  // ⏱️ 3 минуты на ввод

    if (portalStatus() == SP_SUBMIT) {
      if (botTokenValid(portalCfg.token)) {
        strncpy(botToken, portalCfg.token, BOT_TOKEN_LEN - 1);
        botToken[BOT_TOKEN_LEN - 1] = '\0';
        EEPROM.put(EEPROM_TOKEN_ADDR, botToken);
        LOG_I("Токен бота принят и сохранён");
      }
      // 📡 Сеть меняем, только если её реально ввели — иначе работаем на прежней
      if (strlen(portalCfg.SSID)) {
        strncpy(SSID, portalCfg.SSID, sizeof(SSID) - 1);
        SSID[sizeof(SSID) - 1] = '\0';
        strncpy(pass, portalCfg.pass, sizeof(pass) - 1);
        pass[sizeof(pass) - 1] = '\0';
        mode = portalCfg.mode;
        EEPROM.put(EEPROM_SSID_ADDR, SSID);
        EEPROM.put(EEPROM_PASS_ADDR, pass);
        EEPROM.put(EEPROM_MODE_ADDR, mode);
        LOG_I("Заодно обновлены настройки WiFi: %s", SSID);
      }
      // 🔐 Кодовое слово обновляем (оно показано на странице). Список
      //    пользователей НЕ трогаем — уже зарегистрированные останутся.
      strncpy(tstr, portalCfg.tstr, sizeof(tstr) - 1);
      tstr[sizeof(tstr) - 1] = '\0';
      EEPROM.put(EEPROM_TSTR_ADDR, tstr);
      EEPROM.commit();
    } else {
      LOG_W("Токен так и не введён — бот не запустится, полив продолжит работать");
    }
    digitalWrite(LED_BUILTIN, LOW);
  }

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



  // 💾 Инициализация SD-карты — неблокирующая.
  //    Раньше здесь был бесконечный `while (!SD.begin(5))`: без карты прошивка
  //    зависала навсегда ещё до старта бота, и увидеть причину можно было только
  //    по кабелю. Теперь делаем ограниченное число попыток и идём дальше —
  //    неисправность попадёт в чат, а бот перейдёт в аварийный режим.
  bool sdOk = false;
  for (int attempt = 1; attempt <= SD_INIT_ATTEMPTS; attempt++) {
    if (SD.begin(5)) {
      sdOk = true;
      break;
    }
    LOG_E("SD: монтирование не удалось (попытка %d из %d)", attempt, SD_INIT_ATTEMPTS);
    delay(SD_INIT_RETRY_MS);
  }

  uint8_t cardType = sdOk ? SD.cardType() : CARD_NONE;
  cd_card = sdOk && cardType != CARD_NONE;

  if (!sdOk) {
    hwSetFault(HW_SD, "не удалось смонтировать");
  } else if (cardType == CARD_NONE) {
    hwSetFault(HW_SD, "карта не вставлена");
  } else {
    const char* ct = (cardType == CARD_MMC)  ? "MMC"
                   : (cardType == CARD_SD)   ? "SDSC"
                   : (cardType == CARD_SDHC) ? "SDHC"
                                             : "UNKNOWN";
    uint64_t cardSize = SD.cardSize() / (1024 * 1024);
    LOG_I("SD-карта: %s, %llu МБ", ct, cardSize);
  }

  // 📖 Читаем конфигурацию с SD-карты.
  // addWithoutWipe: если структура Config выросла (добавлено поле в конец), старые
  // поля дочитываются по прежним смещениям, а новые получают значение по умолчанию —
  // настройки переживают обновление прошивки без сброса.
  //
  // Без карты читать нечего: остаёмся на значениях по умолчанию. Раньше в этом
  // месте стоял `return`, и молча пропускались flowInit() и раскладка калибровок
  // по каналам — то есть система доходила до loop() полунастроенной.
  if (cd_card) {
    data.addWithoutWipe(true);
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
  } else {
    LOG_W("Конфиг не прочитан (нет SD) — значения по умолчанию");
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