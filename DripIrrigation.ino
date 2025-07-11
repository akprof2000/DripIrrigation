
#include "objects.h"
#include "telegram.h"
#include "init.h"
#include <esp_task_wdt.h>
#include "valves.h"

#define WDT_TIMEOUT 300


unsigned long prevCheck = 0;
unsigned long intervalCheck = CHECK_INTERVAL;


void setup() {

  Serial.begin(115200);
  init();
  botInit();
  valves_init();

  pinMode(LIGHT, INPUT);
  pinMode(RAIN, INPUT);
  pinMode(FILL, OUTPUT);

  digitalWrite(FILL, LOW);
  Serial.println("Configuring WDT...");
  esp_task_wdt_config_t twdt_config = {
    .timeout_ms = WDT_TIMEOUT * 1000,
    .idle_core_mask = 1,  // Bitmask of all cores
    .trigger_panic = true,
  };

  esp_task_wdt_deinit();
  esp_task_wdt_init(&twdt_config);  // Init Watchdog timer
  esp_task_wdt_add(NULL);
}

int64_t oldTime = 0;

bool oldNMode = false;
bool oldRMode = false;


int oldM = 0;
int oldD = 0;
int oldY = 0;

File dataFile;
String fn;


const unsigned long blinkInt = 300;
unsigned long prevBlink = 0;
bool blink = false;



void checkValve(int i) {
  int p = hs.Percent(i);
  if (myConfig.chanel[i].mode == 0 || myConfig.chanel[i].mode == 3) {
    Serial.print("Current percent Humidity :");
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
    if (p < (b - d)) {
      if (oldMode[i] != 1) {
        oldMode[i] = 1;
        sendTelegramStatus("Клапан № " + String(i + 1) + " (" + myConfig.chanel[i].title + ")  открыт по порогу влажности (" + myConfig.chanel[i].border + " %), текущая влажность " + p + " %");
      }
      valve_open(i);
    } else if (p > (b + d)) {
      if (oldMode[i] != 2) {
        oldMode[i] = 2;
        sendTelegramStatus("Клапан № " + String(i + 1) + " (" + myConfig.chanel[i].title + ") закрыт по порогу влажности (" + myConfig.chanel[i].border + " %), текущая влажность " + p + " %");
      }
      valve_close(i);
    } else {
      if (oldMode[i] != 1 && oldMode[i] != 2 && oldMode[i] != 3) {
        oldMode[i] = 3;
        sendTelegramStatus("Клапан № " + String(i + 1) + " (" + myConfig.chanel[i].title + ") находиться в промежуточном состоянии (" + myConfig.chanel[i].border + " %), текущая влажность " + p + " %");
      }
    }
  } else {
    if (myConfig.chanel[i].mode == 1) {
      if (oldMode[i] != 10) {
        oldMode[i] = 10;
        sendTelegramStatus("Клапан № " + String(i + 1) + " (" + myConfig.chanel[i].title + ") открыт по настройке, текущая влажность " + p + " %");
      }
      valve_open(i);
    } else {
      if (oldMode[i] != 11) {
        oldMode[i] = 11;
        sendTelegramStatus("Клапан № " + String(i + 1) + " (" + myConfig.chanel[i].title + ") закрыт по настройке, текущая влажность " + p + " %");
      }
      valve_close(i);
    }
  }
}


void loop() {
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

  ReCheck();
  unsigned long currentMillis = millis();
  if (currentMillis - prevCheck >= intervalCheck) {
    prevCheck = currentMillis;
    int64_t curr = getUnixTime();
    if (oldTime < (int64_t(curr / 60))) {
      FB_Time t(curr, 0);
      if (oldY < t.year || oldM < t.month || oldD < t.day) {
        fn = "/" + String(t.year) + "/" + IntWith2Zero(t.month) + "/" + IntWith2Zero(t.day) + ".csv";
        SD.mkdir("/" + String(t.year));
        SD.mkdir("/" + String(t.year) + "/" + IntWith2Zero(t.month));

        Serial.print("Check file ");
        Serial.println(fn);
        if (!SD.exists(fn)) {
          Serial.print("file not found create new file: ");
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
      if (oldTime % (60 * 24) == 0) {
        Serial.println(F("Check time status"));
        timeFixed();
      }
      Serial.println(F("Check status"));
      hs.setAll();


      bool blocked = false;
      bool nightW = false;
      bool rainW = false;


      int rain_t = digitalRead(RAIN);
      if (rain_t == LOW) {
        Serial.println(F("Rain"));
        if (!rainNow) {
          sendTelegramStatus("Пошел сильный дождь");
          rainNow = true;
        }
        if (!myConfig.runOnRain) {
          blocked = true;
          rainW = true;
          if (!oldRMode) {
            Serial.println(F("Set rain low power"));
            oldRMode = true;
            sendTelegramStatus("Идет дождь отключаем полив!");
          }
        }
      } else {
        if (rainNow) {
          rainNow = false;
          sendTelegramStatus("Влажность после дождя достигла нормы");
        }
        if (oldRMode) {
          Serial.println(F("Set rain high power"));
          oldRMode = false;
          sendTelegramStatus("Восстановление работы после дождя!");
        }
      }

      int night = digitalRead(LIGHT);
      if (night == HIGH) {
        Serial.println(F("Night"));
        if (!nightNow) {
          nightNow = true;
          sendTelegramStatus("Наступила ночь");
        }
        if (!myConfig.runOnNight) {
          blocked = true;
          nightW = true;
          if (!oldNMode) {
            Serial.println(F("Set night low power"));
            oldNMode = true;
            sendTelegramStatus("Переход в энергосберегающее состояние!");
            if (valve_opened() == true) {
              Serial.println(F("Set filling signal"));
              digitalWrite(FILL, HIGH);
              delay(FILLING_WAIT);
              digitalWrite(FILL, LOW);
              sendTelegramStatus("Старт заливки бака!");
            }
          }
        }
      } else {
        Serial.println(F("Day"));
        if (nightNow) {
          nightNow = false;
          sendTelegramStatus("Наступил день");
        }
        if (oldNMode) {
          Serial.println(F("Set night high power"));
          oldNMode = false;
          sendTelegramStatus("Восстановление работы после ночного режима!");
          if (rainNow && !myConfig.runOnRain)
              sendTelegramStatus("Погодная обстановка не достигла нормы!");
        }
      }


      if (!blocked) {
        hs.setAll();
        for (int i = 0; i < 8; i++) {
          checkValve(i);
        }
      } else {
        for (int i = 0; i < 8; i++) {
          if (!nightW && rainW && myConfig.chanel[i].mode == 3) {
            checkValve(i);
          } else {
            valve_close(i);
            if (oldMode[i] != 11) {
              oldMode[i] = 11;
              sendTelegramStatus("Клапан № " + String(i + 1) + " (" + myConfig.chanel[i].title + ") закрыт");
            }
          }
        }
      }

      Serial.println("Write file data");
      dataFile = SD.open(fn, FILE_APPEND);
      if (dataFile) {
        for (int i = 0; i < 8; i++) {
          String row = String(curr) + ","
                       + t.dateString() + " " + t.timeString() + ","
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
        sendTelegramStatus("Ошибка записи в файл: " + fn);
        Serial.print("Can not open file to write: ");
        Serial.println(fn);
      }
      dataFile.close();
    }
    if (pumpStart != 0) {
      if (curr - pumpStart > PUMP_TIMEOUT) {
        pumpStart = 0;
        digitalWrite(PUMP, LOW);
      }
    }
    bool duv = valve_needUpdate();
    bool dut = telegram_needUpdate();

    if (duv || dut) {
      data.update();
    }
    esp_task_wdt_reset();  // Reset watchdog timer
  }
}
