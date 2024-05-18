//
//
//
#include "valves.h"
#include <PCF8574.h>

PCF8574 pcf8574(0x20);

void valves_init()
{
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


void valve_open(int index){
  pcf8574.digitalWrite(index, LOW);
}

void valve_close(int index){
  pcf8574.digitalWrite(index, HIGH);
}
