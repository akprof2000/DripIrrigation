#include <PCF8574.h>
#include "objects.h"
#include "telegram.h"
#include "init.h"
#include <esp_task_wdt.h>

#define WDT_TIMEOUT 60

PCF8574 pcf8574(0x20);

unsigned long prevCheck = 0;
unsigned long intervalCheck = CHECK_INTERVAL;


void setup() {
  Serial.begin(115200);
  init();
  botInit();
  pcf8574.pinMode(P0, OUTPUT);
  pcf8574.pinMode(P1, OUTPUT);
  pcf8574.pinMode(P2, OUTPUT);
  pcf8574.pinMode(P3, OUTPUT);
  pcf8574.pinMode(P4, OUTPUT);
  pcf8574.pinMode(P5, OUTPUT);
  pcf8574.pinMode(P6, OUTPUT);
  pcf8574.pinMode(P7, OUTPUT);

  if (pcf8574.begin() == 1) {
    Serial.println("Valve is ok");
  } else {
    Serial.println("Valve is Error");
  }
  pinMode(LIGHT, INPUT);
  pinMode(RAIN, INPUT);

  esp_task_wdt_init(WDT_TIMEOUT, true);  // Init Watchdog timer
  esp_task_wdt_add(NULL);
}

int64_t oldTime = 0;


bool oldNMode = false;
bool oldRMode = false;
bool sRm = false;
bool sNm = false;

int oldM = 0;
int oldD = 0;
int oldY = 0;

File dataFile;

void loop() {
  bot.tick();
  if (res) {
    bot.tickManual();  // Чтобы отметить сообщение прочитанным
    ESP.restart();
  }
  ReCheck();
  unsigned long currentMillis = millis();
  if (currentMillis - prevCheck >= intervalCheck) {
    prevCheck = currentMillis;
    int64_t curr = getUnixTime();
    if (oldTime < (int64_t(curr / 60))) {
      FB_Time t(curr, 0);
      if (oldY < t.year || oldM < t.month || oldD < t.day) {
        String fn = "/" + String(t.year) + "/" + IntWith2Zero(t.month) + "/" + IntWith2Zero(t.day) + ".csv";
        SD.mkdir("/" + String(t.year));
        SD.mkdir("/" + String(t.year) + "/" + IntWith2Zero(t.month));

        Serial.print("Create file ");
        Serial.println(fn);
        if (oldY == 0 && oldM == 0 && oldD == 0) {
          Serial.println("Run files after restart");
        } else {
          dataFile.flush();
          dataFile.close();
        }
        if (SD.exists(fn)) {
          dataFile = SD.open(fn, FILE_APPEND);
        } else {
          dataFile = SD.open(fn, FILE_WRITE);
          dataFile.println("UnixTime,DateTime,Index,Title,Humidity,Valve,Border,Night,Rain");
        }
        dataFile.flush();
      }
      oldY = t.year;
      oldM = t.month;
      oldD = t.day;

      oldTime = int64_t(curr / 60);
      if (oldTime % (60 * 24) == 0) {
        Serial.println(F("Check time status"));
        timeFixed();
      }
      Serial.println(F("Check status"));
      hs.setAll();

      int night = digitalRead(LIGHT);
      bool blocked = false;
      if (night == HIGH) {
        Serial.println(F("Night"));
        if (!sNm) {
          sNm = true;
          nightNow = true;
          sendStatus("Наступила ночь");
        }
        if (!myConfig.runOnNight) {
          blocked = true;
          if (!oldNMode) {
            Serial.println(F("Set night low power"));
            oldNMode = true;
            sendStatus("Переход в энергосберегающее состояние!");
          }
        }
      } else {
        if (sNm) {
          sNm = false;
          nightNow = false;
          sendStatus("Наступил день");
        }
        if (oldNMode) {
          Serial.println(F("Set night high power"));
          oldNMode = false;
          sendStatus("Восстановление работы после ночного режима!");
        }
      }

      int rain_t = digitalRead(RAIN);
      if (rain_t == LOW) {
        Serial.println(F("Rain"));
        if (!sRm) {
          sendStatus("Пошел сильный дождь");
          sRm = true;
          rainNow = true;
        }
        if (!myConfig.runOnRain) {
          blocked = true;
          if (!oldRMode) {
            Serial.println(F("Set rain low power"));
            oldNMode = true;
            sendStatus("Идет дождь отключаем полив!");
          }
        }
      } else {
        if (sRm) {
          sRm = false;
          rainNow = false;
          sendStatus("Влажность после дождя достигла нормы");
        }
        if (oldRMode) {
          Serial.println(F("Set rain high power"));
          oldRMode = false;
          sendStatus("Восстановление работы после дождя!");
        }
      }



      if (!blocked) {
        for (int i = 0; i < 8; i++) {
          int p = hs.Percent(i);
          if (myConfig.chanel[i].mode == 0) {
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
                sendStatus("Клапан № " + String(i + 1) + " (" + myConfig.chanel[i].title + ")  открыт по порогу влажности, текущая влажность " + p + " %");
              }
              pcf8574.digitalWrite(i, LOW);
            } else if (p > (b + d)) {
              if (oldMode[i] != 2) {
                oldMode[i] = 2;
                sendStatus("Клапан № " + String(i + 1) + " (" + myConfig.chanel[i].title + ") закрыт по порогу влажности, текущая влажность " + p + " %");
              }
              pcf8574.digitalWrite(i, HIGH);
            }
          } else {
            if (myConfig.chanel[i].mode == 1) {
              if (oldMode[i] != 10) {
                oldMode[i] = 10;
                sendStatus("Клапан № " + String(i + 1) + " (" + myConfig.chanel[i].title + ") открыт по настройке, текущая влажность " + p + " %");
              }
              pcf8574.digitalWrite(i, LOW);
            } else {
              if (oldMode[i] != 11) {
                oldMode[i] = 11;
                sendStatus("Клапан № " + String(i + 1) + " (" + myConfig.chanel[i].title + ") закрыт по настройке, текущая влажность " + p + " %");
              }
              pcf8574.digitalWrite(i, HIGH);
            }
          }
        }
      } else {
        for (int i = 0; i < 8; i++) {
          pcf8574.digitalWrite(i, HIGH);
          if (oldMode[i] != 11) {
            oldMode[i] = 11;
            sendStatus("Клапан № " + String(i + 1) + " (" + myConfig.chanel[i].title + ") закрыт");
          }
        }
      }
      Serial.println("Write file data");
      for (int i = 0; i < 8; i++) {
        String row = String(curr) + ","
                     + t.dateString() + " " + t.timeString() + ","
                     + String(i + 1) + ","
                     + String(myConfig.chanel[i].title) + ","
                     + String(hs.Percent(i)) + ","
                     + String((oldMode[i] == 11 || oldMode[i] == 2) ? 0 : 1) + ","
                     + String(myConfig.chanel[i].border) + ","
                     + String(sNm ? 1 : 0) + ","
                     + String(sRm ? 1 : 0);
        Serial.println(row);
        dataFile.println(row);
        dataFile.flush();
      }
    }
  }

  esp_task_wdt_reset();  // Reset watchdog timer
}
