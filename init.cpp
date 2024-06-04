#include "init.h"
#include <WiFi.h>
#include <EEPROM.h>


#include "SimplePortal.h"
#include "objects.h"
#include "telegram.h"
#include <SD.h>




char SSID[32] = "";
char pass[32] = "";

wifi_mode_t mode = WIFI_AP;  // (1 WIFI_STA, 2 WIFI_AP)

byte init_config = 0;

unsigned long previousMillis = 0;
unsigned long interval = CHECK_WIFI_INTERVAL;

unsigned long lastGood = 0;

bool cd_card = true;
int OldR = 0;
void ReCheck() {
  int r = bot.tick();
  if (res) {
    bot.tickManual();  // Чтобы отметить сообщение прочитанным
    ESP.restart();
  }
  if (data.tick() == FD_WRITE) Serial.println("Data updated!");
  unsigned long currentMillis = millis();
  // if WiFi is down, try reconnecting
  if (currentMillis - previousMillis >= interval) {
    Serial.println("Check status wi-fi");
    if (r == 3 or r == 4) {
      OldR++;
    } else {
      OldR = 0;
      if (!dropped)
        lastGood = millis();
    }
    if (OldR > 0) {
      Serial.print("Current error status from telegram ");
      Serial.println(r);
    }
    previousMillis = currentMillis;
    if (WiFi.status() != WL_CONNECTED || (OldR >= 3 && !dropped)) {
      dropped = true;
      OldR = 0;
      Serial.println("Reconnecting to WiFi...");
      WiFi.disconnect();
      WiFi.reconnect();

    } else {
      if (dropped) {
        OldR = 0;
        dropped = false;
        reConnection(abs(long(millis() - lastGood)));
      }
    }
  }
}


void WiFiStationConnected(WiFiEvent_t event, WiFiEventInfo_t info) {
  Serial.println("Connected to AP successfully!");
}

void WiFiGotIP(WiFiEvent_t event, WiFiEventInfo_t info) {
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
}

void WiFiStationDisconnected(WiFiEvent_t event, WiFiEventInfo_t info) {
  Serial.println("Disconnected from WiFi access point");
  Serial.print("WiFi lost connection. Reason: ");
  Serial.println(info.wifi_sta_disconnected.reason);
  Serial.println("Trying to Reconnect");
  WiFi.begin(SSID, pass);
}


void init() {

  Serial.println("begin init");

  EEPROM.begin(4096);

  //WiFi.disconnect(true);

  delay(1000);

  WiFi.onEvent(WiFiStationConnected, WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_CONNECTED);
  WiFi.onEvent(WiFiGotIP, WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_GOT_IP);


  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(BUTTON, INPUT);
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

  EEPROM.get(0, init_config);

  Serial.println("");
  Serial.println(init_config);

  if (init_config == 0) {

    digitalWrite(LED_BUILTIN, HIGH);
    portalRun(180000);

    Serial.println(portalStatus());
    // статус: 0 error, 1 connect, 2 ap, 3 local, 4 exit, 5 timeout

    if (portalStatus() == SP_SUBMIT) {
      strcpy(SSID, portalCfg.SSID);
      strcpy(pass, portalCfg.pass);
      strcpy(tstr, portalCfg.tstr);
      mode = portalCfg.mode;

      Serial.println(SSID);
      Serial.println(pass);
      Serial.println(tstr);
      EEPROM.put(1, SSID);
      EEPROM.put(1 + 33, pass);
      EEPROM.put(1 + 33 + 33, tstr);
      EEPROM.put(1 + 33 + 33 + 33, mode);
      EEPROM.put(250, 0);
      EEPROM.commit();
      // забираем логин-пароль
      digitalWrite(LED_BUILTIN, LOW);
      Serial.println("");
      Serial.println("WiFi write info");
    }
  }

  EEPROM.get(1, SSID);
  EEPROM.get(1 + 33, pass);
  EEPROM.get(1 + 33 + 33, tstr);
  EEPROM.get(1 + 33 + 33 + 33, mode);

  Serial.println();
  Serial.println("******************************************************");
  Serial.print("Connecting to ");
  Serial.println(SSID);

  WiFi.setHostname("DripIrrigationEsp");
  WiFi.mode(mode);
  WiFi.begin(SSID, pass);

  int ind = 0;
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    ind++;
    if (ind > 30) {
      break;
    }
  }


  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("");
    Serial.println("WiFi not connected wait foe connect after");
  } else {
    Serial.println("");
    Serial.println("WiFi connected");
    Serial.println("IP address: ");
    Serial.println(WiFi.localIP());
  }

  if (init_config == 0) {
    init_config = 1;
    EEPROM.put(0, init_config);
    Serial.println("");
    Serial.println("WiFi write status");
    EEPROM.commit();
  }
  EEPROM.end();
  hs.init();

  while (!SD.begin(5)) {
    delay(1000);
    Serial.println("Card Mount Failed");
  }


  uint8_t cardType = SD.cardType();

  if (cardType == CARD_NONE) {
    Serial.println("No SD card attached");
    return;
  }

  Serial.print("SD Card Type: ");
  if (cardType == CARD_MMC) {
    Serial.println("MMC");
  } else if (cardType == CARD_SD) {
    Serial.println("SDSC");
  } else if (cardType == CARD_SDHC) {
    Serial.println("SDHC");
  } else {
    Serial.println("UNKNOWN");
  }

  uint64_t cardSize = SD.cardSize() / (1024 * 1024);
  Serial.printf("SD Card Size: %lluMB\n", cardSize);



  FDstat_t stat = data.read();

  switch (stat) {
    case FD_FS_ERR:
      Serial.println("FS Error");
      break;
    case FD_FILE_ERR:
      Serial.println("Error");
      break;
    case FD_WRITE:
      Serial.println("Data Write");
      break;
    case FD_ADD:
      Serial.println("Data Add");
      break;
    case FD_READ:
      Serial.println("Data Read");
      break;
    default:
      break;
  }

  Serial.println("Data read:");
  Serial.print("Run on rain ");
  Serial.println(myConfig.runOnRain);
  Serial.print("Run on night ");
  Serial.println(myConfig.runOnNight);
  Serial.print("Delta calibration ");
  Serial.println(myConfig.deltaCalibration);
  hs.setBorder(myConfig.deltaCalibration);
  Serial.print("Delta humidity ");
  Serial.println(myConfig.deltaHum);

  for (int i = 0; i < 8; i++) {
    Serial.print("Calibration data on ");
    Serial.print(i);
    Serial.print(" min value ");
    Serial.print(myConfig.chanel[i].minVal);
    Serial.print(" max value ");
    Serial.println(myConfig.chanel[i].maxVal);
    hs.setLowHighValue(i, myConfig.chanel[i].minVal, myConfig.chanel[i].maxVal);
  }


  while (!rtc.begin()) {
    delay(1000);
    Serial.println("Couldn't find RTC");
    Serial.flush();
  }
}