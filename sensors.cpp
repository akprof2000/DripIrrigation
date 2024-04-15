//
//
//
#include "sensors.h"


int HumiditySensors::readSensor(const byte which) {
  // select correct MUX channel
  digitalWrite(_addressA, (which & 1) ? HIGH : LOW);  // low-order bit
  digitalWrite(_addressB, (which & 2) ? HIGH : LOW);
  digitalWrite(_addressC, (which & 4) ? HIGH : LOW);  // high-order bit
  // now read the sensor
  return analogRead(_sensor);
}  // end of readSensor


int HumiditySensors::setLow(int index) {
  _low[index] = _curr[index] - _border;
  return _low[index];
}

int HumiditySensors::setHigh(int index) {
  _high[index] = _curr[index] + _border;
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
  int val = readSensor(index);
  return(map(val, _high[index], _low[index], 0, 100));
}
