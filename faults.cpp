// faults.cpp 🩺 Реестр аппаратных неисправностей
#include "faults.h"
#include "log.h"

// 📏 Длина причины. Хватает на «не отвечает на шине I2C» с запасом; фиксированный
//    буфер вместо String — чтобы реестр не зависел от кучи и был доступен на
//    самых ранних этапах загрузки.
#define HW_DETAIL_LEN 48

struct HwEntry {
  const char* name;   // 🏷️ как узел называется в чате
  const char* shortName;  // 🏷️ краткое имя для заголовка меню
  bool faulty;
  char detail[HW_DETAIL_LEN];
};

// ⚠️ Порядок записей обязан совпадать с порядком в enum HwId.
static HwEntry g_hw[HW_COUNT] = {
  { "Часы RTC (DS3231)",        "RTC",      false, "" },
  { "SD-карта",                 "SD-карта", false, "" },
  { "Плата клапанов (PCF8574)", "клапаны",  false, "" },
};

static bool g_reportSent = false;

void hwSetFault(HwId id, const char* detail) {
  if (id >= HW_COUNT) return;

  HwEntry& e = g_hw[id];
  e.faulty = true;
  strncpy(e.detail, detail ? detail : "", HW_DETAIL_LEN - 1);
  e.detail[HW_DETAIL_LEN - 1] = '\0';

  // 🔁 Узел снова сломался — отчёт нужно отправить заново.
  g_reportSent = false;

  LOG_E("Авария: %s — %s", e.name, e.detail);
}

void hwSetOk(HwId id) {
  if (id >= HW_COUNT) return;
  if (!g_hw[id].faulty) return;

  g_hw[id].faulty = false;
  g_hw[id].detail[0] = '\0';
  LOG_I("Восстановлено: %s", g_hw[id].name);
}

bool hwFaulty(HwId id) {
  return id < HW_COUNT && g_hw[id].faulty;
}

bool hwHasFault() {
  for (uint8_t i = 0; i < HW_COUNT; i++) {
    if (g_hw[i].faulty) return true;
  }
  return false;
}

String hwFaultReport() {
  String s = F("🆘 <b>Авария оборудования</b>\n\n");

  for (uint8_t i = 0; i < HW_COUNT; i++) {
    if (!g_hw[i].faulty) continue;
    s += F("❌ <b>");
    s += g_hw[i].name;
    s += F("</b>");
    if (g_hw[i].detail[0]) {
      s += F(": ");
      s += g_hw[i].detail;
    }
    s += '\n';
  }

  s += F("\n💧 Полив остановлен: работать с неисправным оборудованием опасно.\n");
  s += F("🔌 Проверьте подключение и нажмите «Перезагрузка».");
  return s;
}

String hwFaultShort() {
  String s;
  for (uint8_t i = 0; i < HW_COUNT; i++) {
    if (!g_hw[i].faulty) continue;
    if (s.length()) s += F(", ");
    s += g_hw[i].shortName;
  }
  return s;
}

bool hwReportSent() {
  return g_reportSent;
}

void hwMarkReportSent() {
  g_reportSent = true;
}
