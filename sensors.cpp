//
//
//
#include "sensors.h"


int HumiditySensors::readSensor(const byte index) {
 
   for (byte j = 0; j < 3; j++) {
      if (index & (1 << j))
        digitalWrite(S[j], HIGH);
      else
        digitalWrite(S[j], LOW);
    }
    delay(100);
    return analogRead(Z);
 }  // end of readSensor


int HumiditySensors::setLow(int index) {
  _low[index] = _curr[index];
  return _low[index];
}

int HumiditySensors::setHigh(int index) {
  _high[index] = _curr[index];
  return _high[index];
}

int HumiditySensors::setCurrent(int index) {
  _curr[index] = readSensor(index);
  return _curr[index];
}

void HumiditySensors::setAll() {
  for (int i = 0; i < 8; i++) {
    int val = setCurrent(i);
    Serial.print("value sensor ");
    Serial.print(i);
    Serial.print(": ");
    Serial.println(val);
  }
}

int HumiditySensors::Percent(int index){
  int val = getCurrent(index);
  return(map(val, _high[index] + _border, _low[index] - _border, 0, 100));
}