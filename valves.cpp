// valves.cpp 🚰 Модуль управления клапанами через PCF8574
#include "valves.h"
#include <Wire.h>
#include <PCF8574.h>
#include "objects.h"

// 🔌 Экземпляр расширителя портов PCF8574 (адрес 0x20)
PCF8574 pcf8574(0x20);

// 🚰 Флаг открытия клапана (для насоса)
bool isOpen = false;

// ⛔ Состояния клапанов (true = закрыт)
bool isClose[8] = { true, true, true, true, true, true, true, true };

// 🔄 Флаг необходимости сохранения конфигурации
bool needValveUpdate = false;

// ============================================================
// 🚰 Проверить и сбросить флаг открытия клапана
// ============================================================
bool valve_opened() {
  bool opn = isOpen;
  isOpen = false;
  return opn;
}

// ============================================================
// 🔌 Инициализация PCF8574 и всех пинов как OUTPUT
// ============================================================
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
    Serial.println("✅ Valve is ok");
  } else {
    Serial.println("❌ Valve is Error");
  }
}

// ============================================================
// 🗑️ Пролив дренажа: наполнение + слив
// ============================================================
void spillage() {
  Serial.println("🗑️ Filling drainage");
  sendTelegramStatus("🗑️ Запуск пролива дренажа");
  digitalWrite(DRAIN, HIGH);
  digitalWrite(PUMP, HIGH);
  delay(DRAIN_TIMEOUT);
  digitalWrite(DRAIN, LOW);
  digitalWrite(PUMP, LOW);
  sendTelegramStatus("✅ Пролив дренажа завершён");
  Serial.println("✅ End filling drainage");
}

// ============================================================
// 🚰 Открыть клапан по индексу
// ============================================================
void valve_open(int index) {
  pcf8574.digitalWrite(index, LOW);  // 🔌 LOW = открыть (инверсная логика реле)
  isOpen = true;

  // ⚡ Если клапан был закрыт — включаем насос
  if (isClose[index]) {
    digitalWrite(PUMP, HIGH);
    pumpStart = getDateTime().getUnix();

    // 💧 Проверяем, был ли кто-то из клапанов уже открыт (для сброса счётчика расхода)
    bool anyOpen = false;
    for (int i = 0; i < 8; i++) {
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
void valve_close(int index) {
  pcf8574.digitalWrite(index, HIGH);  // 🔌 HIGH = закрыть
  isClose[index] = true;

  // ⏱️ Проверяем, все ли клапаны закрыты
  bool allCls = true;
  for (int i = 0; i < 8; i++) {
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

// ============================================================
// 🔄 Проверить необходимость сохранения конфигурации
// ============================================================
bool valve_needUpdate() {
  bool nu = needValveUpdate;
  needValveUpdate = false;
  return nu;
}