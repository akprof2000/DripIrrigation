#include "init.h"
#include <WiFi.h>
#include <EEPROM.h>
#include "SimplePortal.h"
#include "objects.h"
#include "telegram.h"


char SSID[32] = "";
char pass[32] = "";


wifi_mode_t mode = WIFI_AP;  // (1 WIFI_STA, 2 WIFI_AP)

const int LED_BUILTIN = 2;
const int BUTTON = 23;

byte init_config = 0;

unsigned long previousMillis = 0;
unsigned long interval = 30000;

bool droped = false;

void ReCheck() {
  unsigned long currentMillis = millis();
  // if WiFi is down, try reconnecting
  if (currentMillis - previousMillis >= interval) {
    if (WiFi.status() != WL_CONNECTED) {
      droped = true;
      Serial.println("Reconnecting to WiFi...");
      WiFi.disconnect();
      WiFi.reconnect();
      previousMillis = currentMillis;
    } else {
      if (droped) {
        droped = false;
        reConnection();
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
  EEPROM.begin(4096);

  WiFi.disconnect(true);

  delay(1000);
  /*
  WiFi.onEvent(WiFiStationConnected, WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_CONNECTED);
  WiFi.onEvent(WiFiGotIP, WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_GOT_IP);
  WiFi.onEvent(WiFiStationDisconnected, WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
*/

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
}