// SimplePortal.h 📡 Заголовочный файл WiFi конфигурационного портала
#ifndef _SimplePortal_h
#define _SimplePortal_h

#define SP_AP_NAME "ESPCfg_DripIrrigation"  // 📡 Название точки доступа
#define SP_AP_IP 192,168,1,1                // 🌐 IP точки доступа

#include <DNSServer.h>
#include <WiFi.h>
#include <WebServer.h>

// 📡 Статусы портала
#define SP_ERROR 0
#define SP_SUBMIT 1
#define SP_SWITCH_AP 2
#define SP_SWITCH_LOCAL 3
#define SP_EXIT 4
#define SP_TIMEOUT 5

// ⚙️ Структура конфигурации, получаемая из портала
struct PortalCfg {
  char SSID[32] = "";           // 📡 Имя WiFi сети
  char pass[32] = "";           // 🔐 Пароль WiFi
  char tstr[32] = "";           // 🔐 Кодовое слово Telegram
  wifi_mode_t mode = WIFI_AP;   // 📡 Режим: 1=WIFI_STA, 2=WIFI_AP
};
extern PortalCfg portalCfg;

// 🚀 Запустить портал
void portalStart();
// 🛑 Остановить портал
void portalStop();
// 🔄 Вызвать в цикле (неблокирующий)
bool portalTick();
// ⏱️ Блокирующий вызов с таймаутом (мс)
void portalRun(uint32_t prd = 60000);
// 📊 Получить статус портала
byte portalStatus();

// 📡 Обработчики HTTP
void SP_handleConnect();
void SP_handleAP();
void SP_handleLocal();
void SP_handleExit();

#endif
