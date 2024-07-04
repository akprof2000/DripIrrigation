

#ifndef _SimplePortal_h
#define _SimplePortal_h



#define SP_AP_NAME "ESPCfg_DripIrrigation"     // название точки
#define SP_AP_IP 192,168,1,1        // IP точки


#include <DNSServer.h>
#include <WiFi.h>
#include <WebServer.h>

#define SP_ERROR 0
#define SP_SUBMIT 1
#define SP_SWITCH_AP 2
#define SP_SWITCH_LOCAL 3
#define SP_EXIT 4
#define SP_TIMEOUT 5

struct PortalCfg {
  char SSID[32] = "";
  char pass[32] = "";
  char tstr[32] = "";
  wifi_mode_t mode = WIFI_AP;    // (1 WIFI_STA, 2 WIFI_AP)
};
extern PortalCfg portalCfg;

void portalStart();     // запустить портал
void portalStop();      // остановить портал
bool portalTick();      // вызывать в цикле
void portalRun(uint32_t prd = 60000);   // блокирующий вызов
byte portalStatus();    // статус: 1 connect, 2 ap, 3 local, 4 exit, 5 timeout

void SP_handleConnect();
void SP_handleAP();
void SP_handleLocal();
void SP_handleExit();
#endif