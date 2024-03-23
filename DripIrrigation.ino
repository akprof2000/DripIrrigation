
#include "objects.h"
#include "telegram.h"



void setup() {
  Serial.begin(9600);
  init();
  botInit();
}

void loop() {
  bot.tick();
  if (res) {
    bot.tickManual();  // Чтобы отметить сообщение прочитанным
    ESP.restart();
  }
}
