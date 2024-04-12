
#include "objects.h"
#include "telegram.h"
#include "init.h"
#include <esp_task_wdt.h>

#define WDT_TIMEOUT 60

void setup() {  
  Serial.begin(9600);
  init();
  botInit();
  esp_task_wdt_init(WDT_TIMEOUT, true);  // Init Watchdog timer
}

void loop() {
  bot.tick();
  if (res) {
    bot.tickManual();  // Чтобы отметить сообщение прочитанным
    ESP.restart();
  }
  ReCheck();
  esp_task_wdt_reset();  // Reset watchdog timer
}
