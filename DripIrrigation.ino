#include "TM1637.h"
#include <EEPROM.h>

#define CLK 5  //pins definitions for TM1637 and can be changed to other ports
#define DIO 3
TM1637 tm1637(CLK, DIO);

int AirValue = 550;    // Максимальное значение сухого датчика
int WaterValue = 215;  // Минимальное значение погруженного датчика


const unsigned long WAIT = 1000;
const int DISP = 3;
byte DELTA = 2;



const int PinSens[] = { A2, A3, A4, A5 };
const int PinRele[] = { 8, 9, 10, 11 };
byte SensVal[] = { 0, 0, 0, 0 };
byte DefVal[] = { 50, 50, 50, 50 };
bool Enabled[] = { false, false, false, false };

unsigned long curr = 0;
unsigned long disp = 0;

bool btn_down[] = { false, false, false, false, false };
bool btn_clk[] = { false, false, false, false, false };


void setup() {
  // put your setup code here, to run once:
  // initialize digital pin LED_BUILTIN as an output.
  Serial.begin(9600);

  tm1637.init();
  tm1637.set(BRIGHT_TYPICAL);  //BRIGHT_TYPICAL = 2,BRIGHT_DARKEST = 0,BRIGHTEST = 7;
  pinMode(LED_BUILTIN, OUTPUT);
  for (int i = 0; i < 4; i++) {
    pinMode(PinSens[i], INPUT);
    pinMode(PinRele[i], OUTPUT);
    digitalWrite(PinRele[i], LOW);
  }

  digitalWrite(LED_BUILTIN, LOW);
  curr = millis();
  int val = analogRead(A0);

  bool blink = true;
  while (val >= 0 && val < 50) {
    delay(300);
    val = analogRead(A0);

    if (blink)
      digitalWrite(LED_BUILTIN, HIGH);
    else
      digitalWrite(LED_BUILTIN, LOW);
    if (curr + 3000 < millis())
      blink = !blink;
  }

  if (curr + 3000 < millis()) {
    EEPROM_int_write(0, AirValue);
    EEPROM_int_write(4, WaterValue);
    EEPROM.update(8, DefVal[0]);
    EEPROM.update(9, DefVal[1]);
    EEPROM.update(10, DefVal[2]);
    EEPROM.update(11, DefVal[3]);
    EEPROM.update(12, DELTA);
  }

  digitalWrite(LED_BUILTIN, HIGH);

  AirValue = EEPROM_int_read(0);
  WaterValue = EEPROM_int_read(4);
  DefVal[0] = EEPROM.read(8);
  DefVal[1] = EEPROM.read(9);
  DefVal[2] = EEPROM.read(10);
  DefVal[3] = EEPROM.read(11);
  DELTA = EEPROM.read(12);
}

// чтение
int EEPROM_int_read(int addr) {
  byte raw[2];
  for (byte i = 0; i < 2; i++) raw[i] = EEPROM.read(addr + i);
  int &num = (int &)raw;
  return num;
}

// запись
void EEPROM_int_write(int addr, int num) {
  byte raw[2];
  (int &)raw = num;
  for (byte i = 0; i < 2; i++) EEPROM.update(addr + i, raw[i]);
}


int8_t TimeDisp[] = { 1, 2, 3, 4 };

int delta_disp = 0;



int prev;
int ind = 0;

void parseButtons() {

  int val = analogRead(A0);


  if (abs(val - prev) < 10) {
    if (ind < 4)
      ind++;
  } else
    ind = 0;

  prev = val;

  if (ind < 3)
    return;

  /*
1 - 0 - 50
2 - 100 - 180
3 - 480 - 530
4 - 300 - 350
5 - 700 - 780
*/
  if (val >= 0 && val < 50) {
    btn_down[0] = true;
  } else {
    if (btn_down[0]) {
      btn_clk[0] = true;
    }

    btn_down[0] = false;
  }

  if (val > 100 && val < 180) {
    btn_down[1] = true;
  } else {
    if (btn_down[1]) {
      btn_clk[1] = true;
    }
    btn_down[1] = false;
  }

  if (val > 480 && val < 520) {
    btn_down[2] = true;
  } else {
    if (btn_down[2]) {
      btn_clk[2] = true;
    }
    btn_down[2] = false;
  }

  if (val > 300 && val < 350) {
    btn_down[3] = true;
  } else {
    if (btn_down[3]) {
      btn_clk[3] = true;
    }
    btn_down[3] = false;
  }

  if (val > 730 && val < 780) {
    btn_down[4] = true;
  } else {
    if (btn_down[4]) {
      btn_clk[4] = true;
    }
    btn_down[4] = false;
  }
}

bool fixed = false;

int mode = 0;
bool skip = false;

unsigned long wait_btn = 0;

bool income = false;

void loop() {


  //Serial.println(mode);
  parseButtons();

  if (mode == 0) {
    if (btn_clk[0]) {
      fixed = !fixed;
      if (fixed)
        disp--;
      btn_clk[0] = false;
      skip = true;
      delta_disp = 0;
    }

    if (btn_clk[4] && fixed) {
      btn_clk[4] = false;
      skip = true;
      delta_disp = 0;
      mode = 2;
    }

    if (btn_clk[1]) {
      disp--;
      if (disp < 0) disp = 3;
      if (!fixed) {
        disp--;
        if (disp < 0) disp = 3;
      }

      btn_clk[1] = false;
      skip = true;
      delta_disp = 0;
    }

    if (btn_clk[3]) {
      if (fixed)
        disp++;
      if (disp > 3) disp = 0;
      btn_clk[3] = false;
      skip = true;
      delta_disp = 0;
    }

    if (fixed) {
      if (btn_down[2]) {
        if (wait_btn + 3000 < millis()) {
          mode = 1;
          disp = 0;
          skip = true;
          income = true;
          btn_clk[0] = false;
          btn_clk[1] = false;
          btn_clk[2] = false;
          btn_clk[3] = false;
          btn_clk[4] = false;
        }
      } else {
        wait_btn = millis();
      }
    }
  }


  if (mode == 1) {
    if (btn_clk[4]) {
      btn_clk[4] = false;
      mode = 0;
      skip = true;
      EEPROM_int_write(0, AirValue);
      EEPROM_int_write(4, WaterValue);
      EEPROM_int_write(12, DELTA);
    }
    if (btn_clk[1]) {
      disp--;
      if (disp < 0) disp = 2;
      btn_clk[1] = false;
      skip = true;
    }

    if (btn_clk[3]) {
      disp++;
      if (disp > 2) disp = 0;
      btn_clk[3] = false;
      skip = true;
    }

    if (btn_clk[0]) {
      if (disp == 0) {
        AirValue--;
        if (AirValue < 0)
          AirValue = 1024;
      }
      if (disp == 1) {
        WaterValue--;
        if (WaterValue < 0)
          WaterValue = 1024;
      }
      if (disp == 2) {
        DELTA--;
        if (DELTA < 0)
          DELTA = 10;
      }

      btn_clk[0] = false;
      skip = true;
    }

    if (btn_clk[2]) {
      if (!income) {
        if (disp == 0) {
          AirValue++;
          if (AirValue > 1024)
            AirValue = 0;
        }
        if (disp == 1) {
          WaterValue++;
          if (WaterValue > 1024)
            WaterValue = 0;
        }
        if (disp == 2) {
          DELTA++;
          if (DELTA > 10)
            DELTA = 0;
        }
        skip = true;
      }
      btn_clk[2] = false;
      income = false;
    }
  }

  if (mode == 2) {
    if (btn_clk[0]) {
      btn_clk[0] = false;
      mode = 0;
      skip = true;
    }

    if (btn_clk[1]) {
      disp--;
      if (disp < 0) disp = 3;
      btn_clk[1] = false;
      skip = true;
    }

    if (btn_clk[3]) {
      disp++;
      if (disp > 3) disp = 0;
      btn_clk[3] = false;
      skip = true;
    }

    if (btn_clk[4]) {
      skip = true;
      btn_clk[4] = false;
      delta_disp = 0;
      mode = 3;
    }
  }

  if (mode == 3) {
    if (btn_clk[0]) {
      DefVal[disp]--;
      if (DefVal[disp] < 1)
        DefVal[disp] = 99;
      btn_clk[0] = false;
      skip = true;
    }

    if (btn_clk[2]) {
      DefVal[disp]++;
      if (DefVal[disp] > 99)
        DefVal[disp] = 0;

      btn_clk[2] = false;
      skip = true;
    }

    if (btn_clk[4]) {
      btn_clk[4] = false;
      skip = true;
      delta_disp = 0;
      EEPROM.write(8 + disp, DefVal[disp]);

      mode = 2;
    }
  }

  if (mode == 3) {
    if (skip) {
      skip = false;
      tm1637.point(false);
      //tm1637.display(TimeDisp);
      tm1637.display(0, disp + 1);
      tm1637.display(1, 13);
      tm1637.display(2, DefVal[disp] / 10);
      tm1637.display(3, DefVal[disp] % 10);
    }
  } else if (mode == 2) {
    if (skip) {
      skip = false;
      tm1637.point(false);
      //tm1637.display(TimeDisp);
      tm1637.display(0, disp + 1);
      tm1637.display(1, 10);
      tm1637.display(2, DefVal[disp] / 10);
      tm1637.display(3, DefVal[disp] % 10);
    }
  } else if (mode == 1) {
    if (skip) {
      skip = false;
      tm1637.point(false);
      //tm1637.display(TimeDisp);
      if (disp == 0) {
        tm1637.display(0, AirValue / 1000);
        tm1637.display(1, AirValue % 1000 / 100);
        tm1637.display(2, AirValue % 100 / 10);
        tm1637.display(3, AirValue % 10);
      }
      if (disp == 1) {
        tm1637.display(0, WaterValue / 1000);
        tm1637.display(1, WaterValue % 1000 / 100);
        tm1637.display(2, WaterValue % 100 / 10);
        tm1637.display(3, WaterValue % 10);
      }
      if (disp == 2) {
        tm1637.display(0, 0);
        tm1637.display(1, 0);
        tm1637.display(2, DELTA / 10);
        tm1637.display(3, DELTA % 10);
      }
    }
  }




  if (millis() > curr + WAIT || skip) {
    skip = false;

    //Serial.println(analogRead(A0));
    for (int i = 0; i < 4; i++) {
      int read = analogRead(PinSens[i]);
      SensVal[i] = map(read, AirValue, WaterValue, 0, 100);

      if (Enabled[i]) {
        if (DefVal[i] + DELTA < SensVal[i]) Enabled[i] = false;
      } else {
        if (DefVal[i] - DELTA > SensVal[i]) Enabled[i] = true;
      }

      if (Enabled[i]) {
        digitalWrite(PinRele[i], HIGH);
      } else {
        digitalWrite(PinRele[i], LOW);
      }
    }

    if (mode == 0) {
      if (delta_disp == 0) {
        if (disp > 3) disp = 0;
        if (disp < 0) disp = 3;
        tm1637.point(Enabled[disp]);
        //tm1637.display(TimeDisp);

        tm1637.display(0, disp + 1);
        if (fixed)
          tm1637.display(1, 15);
        else
          tm1637.display(1, 0x7f);
        tm1637.display(2, (int8_t)(SensVal[disp] / 10));
        tm1637.display(3, (int8_t)(SensVal[disp] % 10));



        if (!fixed) {
          disp++;
        }
      }

      delta_disp++;
      if (delta_disp >= DISP) delta_disp = 0;
    }
    curr = millis();
  }
}
