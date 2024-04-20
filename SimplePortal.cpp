#include "SimplePortal.h"
static DNSServer _SP_dnsServer;
#ifdef ESP8266
static ESP8266WebServer _SP_server(80);
#else
static WebServer _SP_server(80);
#endif

String SP_connect_page = R"rawliteral(
<!DOCTYPE HTML><html><head>
<meta name="viewport" content="width=device-width, initial-scale=1">
</head><body>
<style type="text/css">
    select, input[type="text"] {margin-bottom:8px;font-size:20px;min-width:80%;max-width:80%;width:80%; -webkit-box-sizing: border-box;  -moz-box-sizing: border-box;  box-sizing: border-box;}
    input[type="submit"] {width:180px; height:60px;margin-bottom:8px;font-size:20px;}
</style>
<center>
<h3>WiFi настройка</h3>
<form action="/connect" method="POST">
    <select name="ssid" placeholder="SSID">
          {BoxItems}
    </select></br>
    <input type="text" name="pass" placeholder="Pass">
    <input type="submit" value="Подключить">
</form>
<h3>Telegram кодовое слово для регистрации администратора</h3>
<h4>{textTelegramConnect}</h4>
<p>При активации бота в Telegram потребуется ввести данное кодовое слово, чтобы бот воспринял вас как официального администратора</p>
<form action="/exit" method="POST">
    <input type="submit" value="Выход">
</form>
</center>
</body></html>)rawliteral";

static bool _SP_started = false;
static byte _SP_status = 0;
PortalCfg portalCfg;

const int MAX_UID = 30;

const String  generateUID(){
  /* Change to allowable characters */
  const char possible[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
  String uid="";
  for(int i = 0; i < MAX_UID; i++){
    int r = random(0, strlen(possible));
    uid += possible[r];
  }
  return uid;
}

String t_str; 

void SP_handleConnect() {
  strcpy(portalCfg.SSID, _SP_server.arg("ssid").c_str());
  strcpy(portalCfg.pass, _SP_server.arg("pass").c_str());
  strcpy(portalCfg.tstr, t_str.c_str());
  portalCfg.mode = WIFI_STA;
  _SP_status = 1;
}

void SP_handleExit() {
  _SP_status = 4;
}

void portalStart() {
  WiFi.softAPdisconnect();
  WiFi.disconnect();
  IPAddress apIP(SP_AP_IP);
  IPAddress subnet(255, 255, 255, 0);
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(apIP, apIP, subnet);
  WiFi.softAP(SP_AP_NAME);
  
  _SP_dnsServer.start(53, "*", apIP);

  _SP_server.onNotFound([]() {
    _SP_server.send(200, "text/html", SP_connect_page);
  });
  _SP_server.on("/connect", HTTP_POST, SP_handleConnect);
  _SP_server.on("/exit", HTTP_POST, SP_handleExit);
  _SP_server.begin();
  _SP_started = true;
  _SP_status = 0;
}

void portalStop() {
  WiFi.softAPdisconnect();
  _SP_server.stop();
  _SP_dnsServer.stop();
  _SP_started = false;
}

bool portalTick() {
  if (_SP_started) {
    _SP_dnsServer.processNextRequest();
    _SP_server.handleClient();
    yield();
    if (_SP_status) {
      portalStop();
      return 1;
    }
  }
  return 0;
}

void portalRun(uint32_t prd) {
  uint32_t tmr = millis();
  Serial.println("Scan start");

  // WiFi.scanNetworks will return the number of networks found.
  int n = WiFi.scanNetworks();
  Serial.println("Scan done");
  String data = String("");
  if (n == 0) {
    data = "<option value=\"-\">Нет сетей</option>";
  } else {
    Serial.print(n);
    Serial.println(" networks found");
    Serial.println("Nr | SSID                             | RSSI | CH | Encryption");
    for (int i = 0; i < n; ++i) {
      String s_name =  WiFi.SSID(i).c_str();
      data += String("<option value=\""+s_name+"\">"+s_name+"</option>\n");
      // Print SSID and RSSI for each network found
      Serial.printf("%2d", i + 1);
      Serial.print(" | ");
      Serial.printf("%-32.32s", WiFi.SSID(i).c_str());
      Serial.print(" | ");
      Serial.printf("%4d", WiFi.RSSI(i));
      Serial.print(" | ");
      Serial.printf("%2d", WiFi.channel(i));
      Serial.print(" | ");
      switch (WiFi.encryptionType(i)) {
        case WIFI_AUTH_OPEN:
          Serial.print("open");
          break;
        case WIFI_AUTH_WEP:
          Serial.print("WEP");
          break;
        case WIFI_AUTH_WPA_PSK:
          Serial.print("WPA");
          break;
        case WIFI_AUTH_WPA2_PSK:
          Serial.print("WPA2");
          break;
        case WIFI_AUTH_WPA_WPA2_PSK:
          Serial.print("WPA+WPA2");
          break;
        case WIFI_AUTH_WPA2_ENTERPRISE:
          Serial.print("WPA2-EAP");
          break;
        case WIFI_AUTH_WPA3_PSK:
          Serial.print("WPA3");
          break;
        case WIFI_AUTH_WPA2_WPA3_PSK:
          Serial.print("WPA2+WPA3");
          break;
        case WIFI_AUTH_WAPI_PSK:
          Serial.print("WAPI");
          break;
        default:
          Serial.print("unknown");
      }
      Serial.println();
      delay(10);
    }
  }
  Serial.println("");
   // Delete the scan result to free memory for code below.
  WiFi.scanDelete();

  t_str = generateUID();
  SP_connect_page.replace("{BoxItems}", data);
  SP_connect_page.replace("{textTelegramConnect}", t_str);
  

  portalStart();
  while (!portalTick()) {
    if (millis() - tmr > prd) {
      _SP_status = 5;
      portalStop();
      break;
    }
    yield();
  }
}

byte portalStatus() {
  return _SP_status;
}