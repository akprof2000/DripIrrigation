// valves.cpp 🚰 Модуль управления клапанами через PCF8574
#include "valves.h"
#include <Wire.h>
#include <PCF8574.h>
#include "objects.h"
#include "log.h"

// 🔌 Экземпляр расширителя портов PCF8574 (адрес 0x20)
PCF8574 pcf8574(0x20);

// 🚰 Флаг открытия клапана (для насоса)
bool isOpen = false;

// ⛔ Состояния клапанов (true = закрыт)
bool isClose[NUM_CHANNELS] = { true, true, true, true, true, true, true, true };

// 🔄 Флаг необходимости сохранения конфигурации
bool needValveUpdate = false;

// ============================================================
// 🚰 Проверить и сбросить флаг открытия клапана
// ============================================================
bool valveOpened() {
  bool opn = isOpen;
  isOpen = false;
  return opn;
}

// ============================================================
// 🔌 Инициализация PCF8574 и всех пинов как OUTPUT
// ============================================================
void valvesInit() {
  pcf8574.pinMode(P0, OUTPUT);
  pcf8574.pinMode(P1, OUTPUT);
  pcf8574.pinMode(P2, OUTPUT);
  pcf8574.pinMode(P3, OUTPUT);
  pcf8574.pinMode(P4, OUTPUT);
  pcf8574.pinMode(P5, OUTPUT);
  pcf8574.pinMode(P6, OUTPUT);
  pcf8574.pinMode(P7, OUTPUT);

  if (pcf8574.begin() == 1) {
    LOG_I("Клапаны (PCF8574) инициализированы");
  } else {
    LOG_E("PCF8574 не отвечает — клапаны недоступны");
  }
}

// ============================================================
// 🗑️ Пролив дренажа: наполнение + слив
// ============================================================
// 🗑️ Неблокирующий пролив: spillage() запускает, spillageTick() завершает по таймеру
static bool drainActive = false;
static unsigned long drainStart = 0;

void spillage() {
  if (drainActive) return;  // 🔁 уже идёт — не перезапускаем
  LOG_I("Запуск пролива дренажа");
  sendTelegramStatus("🗑️ Запуск пролива дренажа");
  digitalWrite(DRAIN, HIGH);
  digitalWrite(PUMP, HIGH);
  drainStart = millis();
  drainActive = true;
}

// 🗑️ Идёт ли сейчас пролив дренажа
bool valveIsDraining() {
  return drainActive;
}

// ⏱️ Завершение пролива по истечении DRAIN_TIMEOUT — вызывать в каждом цикле loop()
void spillageTick() {
  if (drainActive && (millis() - drainStart >= DRAIN_TIMEOUT)) {
    digitalWrite(DRAIN, LOW);
    drainActive = false;
    stopPumpIfNeed();
    sendTelegramStatus("✅ Пролив дренажа завершён");
    LOG_I("Пролив дренажа завершён");
  }
}

// ============================================================
// 🚰 Открыть клапан по индексу
// ============================================================
void valveOpen(int index) {
  pcf8574.digitalWrite(index, LOW);  // 🔌 LOW = открыть (инверсная логика реле)
  isOpen = true;

  // ⚡ Если клапан был закрыт — включаем насос
  if (isClose[index]) {
    LOG_D("Клапан %d открыт, насос ВКЛ", index + 1);
    digitalWrite(PUMP, HIGH);
    pumpStart = getDateTime().getUnix();

    // 💧 Проверяем, был ли кто-то из клапанов уже открыт (для сброса счётчика расхода)
    bool anyOpen = false;
    for (int i = 0; i < NUM_CHANNELS; i++) {
      if (!isClose[i]) {
        anyOpen = true;
        break;
      }
    }
    // 🔄 Если до этого все клапаны были закрыты — начинаем новую сессию замера расхода воды
    if (!anyOpen) {
      flowResetSession();
    }
  }
  isClose[index] = false;

  // 🗑️ Если все клапаны были закрыты долго — делаем пролив
  if (myConfig.utimeAllClosed != 0) {
    unsigned long ut = getDateTime().getUnix();
    if (ut - myConfig.utimeAllClosed > TIMEOUT_WAIT) {
      spillage();
    }
    myConfig.utimeAllClosed = 0;
    needValveUpdate = true;
  }
}

// ============================================================
// ⛔ Закрыть клапан по индексу
// ============================================================
void valveClose(int index) {
  if (!isClose[index]) LOG_D("Клапан %d закрыт", index + 1);
  pcf8574.digitalWrite(index, HIGH);  // 🔌 HIGH = закрыть
  isClose[index] = true;

  if (pumpStart == 0) {
    stopPumpIfNeed();
  }


  // ⏱️ Проверяем, все ли клапаны закрыты
  bool allCls = true;
  for (int i = 0; i < NUM_CHANNELS; i++) {
    if (!isClose[i]) {
      allCls = false;
      break;
    }
  }

  // ⏱️ Фиксируем время закрытия всех клапанов (для пролива)
  if (allCls) {
    if (myConfig.utimeAllClosed == 0) {
      myConfig.utimeAllClosed = getDateTime().getUnix();
      needValveUpdate = true;

      // 💧 Все клапаны закрыты — фиксируем расход воды за сессию
      flowAddToTotal();
    }
  }
}


void stopPumpIfNeed() {

  if (drainActive) return;  // 🗑️ во время пролива дренажа насос держим включённым

  // 💪 Насос повышения давления: при пороге N=1..8 держим насос включённым,
  // пока открыто N и более клапанов; иначе (или N=9 — выкл) — останавливаем.
  uint8_t n = myConfig.boostPumpValves;
  bool on = (n >= 1 && n <= NUM_CHANNELS && countValveOpen() >= n);
  digitalWrite(PUMP, on ? HIGH : LOW);

  // 📝 Логируем только смену состояния насоса (без спама каждый цикл)
  static int8_t lastPump = -1;
  if ((int8_t)on != lastPump) {
    lastPump = on;
    LOG_D("Насос: %s (открыто %d кл., порог %d)", on ? "ВКЛ" : "ВЫКЛ", countValveOpen(), n);
  }
}

int countValveOpen() {
  int cnt = 0;
  for (int i = 0; i < NUM_CHANNELS; i++) {
    if (isClose[i] == false)
      cnt++;
  }
  return cnt;
}

// ============================================================
// 🔄 Проверить необходимость сохранения конфигурации
// ============================================================
bool valveNeedUpdate() {
  bool nu = needValveUpdate;
  needValveUpdate = false;
  return nu;
}