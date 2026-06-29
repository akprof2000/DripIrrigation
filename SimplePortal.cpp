// SimplePortal.cpp 📡 Модуль WiFi конфигурационного портала
#include "SimplePortal.h"
#include "log.h"

static DNSServer _SP_dnsServer;
static WebServer _SP_server(80);

// 🌐 HTML страница конфигурации WiFi (адаптивная тёмная карточка)
String SP_connect_page = R"rawliteral(
<!DOCTYPE html><html lang="ru"><head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Настройка полива</title>
<style>
:root{--card:#1e293b;--accent:#22c55e;--blue:#3b82f6;--text:#e2e8f0;--muted:#94a3b8;--input:#334155}
*{box-sizing:border-box}
body{margin:0;min-height:100vh;display:flex;align-items:center;justify-content:center;
font-family:-apple-system,Segoe UI,Roboto,Arial,sans-serif;
background:linear-gradient(135deg,#0f172a,#1e3a5f);color:var(--text);padding:16px}
.card{background:var(--card);border-radius:16px;box-shadow:0 12px 40px rgba(0,0,0,.45);max-width:430px;width:100%;padding:26px}
h1{font-size:21px;margin:0 0 4px;text-align:center}
.sub{color:var(--muted);font-size:13px;text-align:center;margin-bottom:18px}
label{display:block;font-size:13px;color:var(--muted);margin:14px 0 6px}
select,input[type=text],input[type=password]{width:100%;padding:12px 14px;font-size:16px;
border:1px solid var(--input);border-radius:10px;background:var(--input);color:var(--text);outline:none}
select:focus,input:focus{border-color:var(--blue)}
.pwd{position:relative}.pwd input{padding-right:46px}
.pwd .eye{position:absolute;right:6px;top:6px;width:36px;height:36px;background:none;border:none;color:var(--muted);font-size:18px;cursor:pointer}
.btn{width:100%;padding:14px;font-size:16px;font-weight:600;border:none;border-radius:10px;color:#fff;cursor:pointer;margin-top:18px}
.btn-primary{background:var(--accent)}.btn-primary:active{filter:brightness(.9)}
.btn-ghost{background:transparent;border:1px solid var(--input);color:var(--muted);margin-top:10px}
.code{display:flex;align-items:center;gap:8px;background:var(--input);border-radius:10px;padding:10px 12px;margin-top:6px}
.code code{font-size:15px;letter-spacing:.5px;word-break:break-all;flex:1}
.code .cp{background:var(--blue);border:none;color:#fff;border-radius:8px;padding:9px 12px;cursor:pointer;font-size:14px}
.hint{font-size:12px;color:var(--muted);margin-top:8px;line-height:1.5}
.divider{height:1px;background:var(--input);margin:22px 0}
</style></head><body>
<div class="card">
<h1>🌱 Капельный полив</h1>
<div class="sub">Подключение к Wi-Fi сети</div>
<form action="/connect" method="POST">
<label>📡 Выберите сеть</label>
<select name="ssid" id="ssid">{BoxItems}</select>
<label>✏️ Или имя сети вручную (для скрытых)</label>
<input type="text" name="ssid_manual" placeholder="Необязательно" autocomplete="off">
<label>🔑 Пароль</label>
<div class="pwd">
<input type="password" name="pass" id="pass" placeholder="Пароль сети" autocomplete="off">
<button type="button" class="eye" onclick="tog()">👁</button>
</div>
<button class="btn btn-primary" type="submit">🔗 Подключить</button>
</form>
<div class="divider"></div>
<label>🤖 Кодовое слово для Telegram</label>
<div class="code"><code id="uid">{textTelegramConnect}</code>
<button type="button" class="cp" onclick="cp(this)">📋</button></div>
<div class="hint">При первом запуске бота отправьте это слово в чат — он распознает вас как администратора системы.</div>
<form action="/exit" method="POST"><button class="btn btn-ghost" type="submit">🚪 Выход</button></form>
</div>
<script>
function tog(){var p=document.getElementById('pass');p.type=p.type==='password'?'text':'password'}
function cp(b){var t=document.getElementById('uid').innerText;if(navigator.clipboard){navigator.clipboard.writeText(t)}b.textContent='✅';setTimeout(function(){b.textContent='📋'},1200)}
</script>
</body></html>)rawliteral";

static bool _SP_started = false;
static byte _SP_status = 0;
PortalCfg portalCfg;

const int MAX_UID = 30;

// 🔐 Генерация случайного кодового слова для регистрации
const String generateUID() {
  const char possible[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
  String uid = "";
  for (int i = 0; i < MAX_UID; i++) {
    int r = random(0, strlen(possible));
    uid += possible[r];
  }
  return uid;
}

String t_str;

// 📡 Обработчик подключения к WiFi
void spHandleConnect() {
  // ✏️ Ручной ввод (для скрытых сетей) имеет приоритет над выбором из списка
  String ssid = _SP_server.arg("ssid_manual");
  ssid.trim();
  if (ssid.length() == 0) ssid = _SP_server.arg("ssid");

  strncpy(portalCfg.SSID, ssid.c_str(), sizeof(portalCfg.SSID) - 1);
  portalCfg.SSID[sizeof(portalCfg.SSID) - 1] = '\0';
  strncpy(portalCfg.pass, _SP_server.arg("pass").c_str(), sizeof(portalCfg.pass) - 1);
  portalCfg.pass[sizeof(portalCfg.pass) - 1] = '\0';
  strncpy(portalCfg.tstr, t_str.c_str(), sizeof(portalCfg.tstr) - 1);
  portalCfg.tstr[sizeof(portalCfg.tstr) - 1] = '\0';

  portalCfg.mode = WIFI_STA;
  _SP_status = 1;
}

// 🚪 Обработчик выхода из портала
void spHandleExit() {
  _SP_status = 4;
}

// 🚀 Запуск точки доступа и DNS сервера
void portalStart() {
  WiFi.softAPdisconnect();
  WiFi.disconnect();
  WiFi.mode(WIFI_AP);
  WiFi.begin(SP_AP_NAME);
  delay(1000);
  String wifi_name = String(SP_AP_NAME) + "_" + WiFi.macAddress();
  wifi_name.replace(":", "");
  LOG_I("Точка доступа: %s", wifi_name.c_str());
  WiFi.softAPdisconnect();
  WiFi.disconnect();

  WiFi.softAPdisconnect();
  WiFi.disconnect();
  IPAddress apIP(SP_AP_IP);
  IPAddress subnet(255, 255, 255, 0);
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(apIP, apIP, subnet);

  WiFi.softAP(wifi_name);

  _SP_dnsServer.start(53, "*", apIP);

  _SP_server.onNotFound([]() {
    _SP_server.send(200, "text/html", SP_connect_page);
  });
  _SP_server.on("/connect", HTTP_POST, spHandleConnect);
  _SP_server.on("/exit", HTTP_POST, spHandleExit);
  _SP_server.begin();
  _SP_started = true;
  _SP_status = 0;
}

// 🛑 Остановка портала
void portalStop() {
  WiFi.softAPdisconnect();
  _SP_server.stop();
  _SP_dnsServer.stop();
  _SP_started = false;
}

// 🔄 Неблокирующий тикер портала
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

// ⏱️ Блокирующий запуск портала с таймаутом
void portalRun(uint32_t prd) {
  uint32_t tmr = millis();
  LOG_I("WiFi: сканирование сетей...");

  // 📡 Сканирование доступных WiFi сетей
  int n = WiFi.scanNetworks();
  String data = String("");
  if (n == 0) {
    LOG_W("WiFi: сети не найдены");
    data = "<option value=\"\">❌ Сети не найдены</option>";
  } else {
    LOG_I("WiFi: найдено сетей: %d", n);
    for (int i = 0; i < n; ++i) {
      String s_name = WiFi.SSID(i).c_str();
      // 📶 индикатор уровня сигнала (4 деления) + 🔒 признак шифрования
      int rssi = WiFi.RSSI(i);
      bool enc = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
      int q = rssi >= -55 ? 4 : rssi >= -65 ? 3 : rssi >= -75 ? 2 : rssi >= -85 ? 1 : 0;
      String bars = "";
      for (int b = 0; b < 4; b++) bars += (b < q) ? "▮" : "▯";
      // value — точное имя сети (для подключения), текст — с иконками
      data += "<option value=\"" + s_name + "\">" + (enc ? "🔒 " : "🔓 ") + s_name + "  " + bars + "</option>\n";
      LOG_D("  %2d. %-20s %d dBm ch%d %s", i + 1, s_name.c_str(), rssi, (int)WiFi.channel(i), enc ? "secured" : "open");
      delay(10);
    }
  }
  WiFi.scanDelete();

  // 🔐 Генерируем кодовое слово и обновляем HTML
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

// 📊 Получить статус портала
byte portalStatus() {
  return _SP_status;
}
