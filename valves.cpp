//
//
//
#include "valves.h"
#include <PCF8574.h>
#include "objects.h"

PCF8574 pcf8574(0x20);

bool isOpen = false;

bool isClose[8] = { true, true, true, true, true, true, true, true };

bool needValveUpdate = false;

bool valve_opened() {
  bool opn = isOpen;
  isOpen = false;
  return opn;
}

void valves_init() {
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
}

void spillage() {
  Serial.println("Filling drenage");
  sendTelegramStatus("Запуск пролива дренажа");
  digitalWrite(DRAIN, HIGH);
  digitalWrite(PUMP, HIGH);
  delay(DRAIN_TIMEOUT);
  digitalWrite(DRAIN, LOW);
  digitalWrite(PUMP, LOW);
  sendTelegramStatus("Пролива дренажа завершон");
  Serial.println("End filling drenage");
}

void valve_open(int index) {
  pcf8574.digitalWrite(index, LOW);
  isOpen = true;

  if (isClose[index]) {
    digitalWrite(PUMP, HIGH);
    pumpStart = getUnixTime();
  }
  isClose[index] = false;
  if (myConfig.utimeAllClosed != 0) {
    unsigned long ut = getUnixTime();
    if (ut - myConfig.utimeAllClosed > TIMEOUT_WAIT) {
      spillage();
    }
    myConfig.utimeAllClosed = 0;
    needValveUpdate = true;
  }
}

void valve_close(int index) {
  pcf8574.digitalWrite(index, HIGH);
  isClose[index] = true;
  bool allCls = true;
  for (int i = 0; i < 8; i++) {
    if (!isClose[i]) {
      allCls = false;
      break;
    }
  }

  if (allCls) {
    if (myConfig.utimeAllClosed == 0) {
      myConfig.utimeAllClosed = getUnixTime();
      needValveUpdate = true;
    }
  }
}

bool valve_needUpdate() {
  bool nu = needValveUpdate;
  needValveUpdate = false;
  return nu;
}
