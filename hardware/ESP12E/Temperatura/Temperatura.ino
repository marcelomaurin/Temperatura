/*******************************************************
 * ESP8266 (Generic) - WiFi + DHT11 + HTTP + Config + Sidebar + Fallback por tentativa
 * Comportamento:
 * 1) Tenta conectar com SSID/Senha da EEPROM (primário).
 * 2) Se falhar (timeout), próxima tentativa usa DEFAULT_SSID/DEFAULT_PASS.
 * 3) Se o default também falhar, alterna de volta ao primário, e assim por diante.
 *
 * Rotas:
 * - "/"           : Dashboard (auto-refresh T/H)
 * - "/config"     : Form p/ SSID/Senha (EEPROM)
 * - "/dht"        : JSON {temperature, humidity, ok}
 * - "/ws/coleta"  : JSON {device, ip, ssid, using_default, ok, temperature, humidity, ts_ms}
 * - "/ws/savewifi": POST salva SSID/Senha (EEPROM) e reconecta
 * - "/ws/reboot"  : Reinicia o ESP
 *******************************************************/
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <EEPROM.h>
#include <DHT.h>

// ======== Credenciais ========
// Defaults (fallback quando não conecta com EEPROM)
#define DEFAULT_SSID "maurinsrv"
#define DEFAULT_PASS "1425361425"

// ======== DHT11 ========
#define DHTPIN   4        // GPIO4
#define DHTTYPE  DHT11
DHT dht(DHTPIN, DHTTYPE);

// ======== HTTP ========
ESP8266WebServer server(80);

// ======== EEPROM Layout ========
#define EEPROM_SIZE   256
struct WifiStore {
  uint32_t magic;
  char ssid[33];   // 32 + '\0'
  char pass[65];   // 64 + '\0'
};
const uint32_t WIFI_MAGIC = 0x42A1C0DE;

// ======== WiFi State Machine ========
static const uint32_t WIFI_FIRST_TIMEOUT_MS   = 8000;
static const uint32_t WIFI_MAX_TIMEOUT_MS     = 20000;
static const uint32_t WIFI_RETRY_COOLDOWN_MS  = 15000;
static const uint8_t  WIFI_TX_PWR_DBM         = 10;

#define LED_ON()  digitalWrite(LED_BUILTIN, LOW)
#define LED_OFF() digitalWrite(LED_BUILTIN, HIGH)
#define LED_TG()  digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN))

enum WifiConnState { WIFI_IDLE, WIFI_CONNECTING, WIFI_CONNECTED, WIFI_BACKOFF };
WifiConnState wifiConnState = WIFI_IDLE;

uint32_t connectStartedAt = 0;
uint32_t nextRetryAt      = 0;
uint32_t connectTimeoutMs = WIFI_FIRST_TIMEOUT_MS;

// Credenciais primárias (EEPROM) e alvo atual
String pri_ssid = DEFAULT_SSID;
String pri_pass = DEFAULT_PASS;
String g_ssid   = DEFAULT_SSID;   // alvo atual
String g_pass   = DEFAULT_PASS;
bool   usingDefault = false;       // true quando usando DEFAULT_*

// ======== DHT Reading ========
struct DhtReading { float t; float h; bool ok; };
DhtReading lastDht = {NAN, NAN, false};

// ======== Utils ========
inline void safeYield() { delay(0); }

void printResetInfo() {
  Serial.println();
  Serial.println(F("===== Reset Info ====="));
  Serial.print(F("Reason: ")); Serial.println(ESP.getResetReason());
  Serial.print(F("Info:   ")); Serial.println(ESP.getResetInfo());
  Serial.println(F("======================"));
}

bool eepromLoadWifi(String &ssid, String &pass) {
  WifiStore ws;
  EEPROM.get(0, ws);
  if (ws.magic != WIFI_MAGIC) return false;
  if (ws.ssid[0] == '\0')     return false;
  ssid = String(ws.ssid);
  pass = String(ws.pass);
  return true;
}

bool eepromSaveWifi(const String &ssid, const String &pass) {
  WifiStore ws;
  ws.magic = WIFI_MAGIC;
  ssid.substring(0, 32).toCharArray(ws.ssid, sizeof(ws.ssid));
  pass.substring(0, 64).toCharArray(ws.pass, sizeof(ws.pass));
  EEPROM.put(0, ws);
  return EEPROM.commit();
}

// ======== WiFi: State Machine ========
void beginConnectTo(const String& ssid, const String& pass, bool markDefault) {
  g_ssid = ssid;
  g_pass = pass;
  usingDefault = markDefault;

  wifiConnState    = WIFI_CONNECTING;
  connectStartedAt = millis();

  Serial.print(F("[WiFi] Conectando a ")); Serial.println(g_ssid);
  WiFi.begin(g_ssid.c_str(), g_pass.c_str());
  LED_ON();
}

void wifiInitIfNeeded() {
  if (wifiConnState != WIFI_IDLE) return;

  WiFi.mode(WIFI_STA);
  WiFi.persistent(false);
  WiFi.setAutoReconnect(false);
  WiFi.setSleepMode(WIFI_MODEM_SLEEP);
  WiFi.setOutputPower(WIFI_TX_PWR_DBM);
  WiFi.hostname("ESP8266-maurinsrv");

  connectTimeoutMs = WIFI_FIRST_TIMEOUT_MS;
  // Primeira tentativa: sempre PRIMÁRIA (EEPROM se existir)
  beginConnectTo(pri_ssid, pri_pass, false);
}

void wifiStateMachine() {
  switch (wifiConnState) {
    case WIFI_IDLE:
      wifiInitIfNeeded();
      break;

    case WIFI_CONNECTING: {
      static uint32_t lastBlink = 0;
      if (millis() - lastBlink >= 150) { lastBlink = millis(); LED_TG(); }

      if (WiFi.status() == WL_CONNECTED) {
        wifiConnState = WIFI_CONNECTED;
        LED_OFF();
        Serial.println(F("[WiFi] Conectado!"));
        Serial.print(F("[WiFi] SSID: ")); Serial.println(g_ssid);
        Serial.print(F("[WiFi] IP: "));   Serial.println(WiFi.localIP());
        Serial.print(F("[WiFi] RSSI: ")); Serial.print(WiFi.RSSI()); Serial.println(F(" dBm"));
        break;
      }

      if (millis() - connectStartedAt >= connectTimeoutMs) {
        // Falha: agenda backoff e alterna alvo (EEPROM <-> DEFAULT)
        wifiConnState = WIFI_BACKOFF;
        nextRetryAt   = millis() + WIFI_RETRY_COOLDOWN_MS;
        connectTimeoutMs = min(connectTimeoutMs * 2, WIFI_MAX_TIMEOUT_MS);
        LED_OFF();
        Serial.println(F("[WiFi] Timeout. Alternando alvo na próxima tentativa."));
        WiFi.disconnect();

        // Alterna o alvo: se falhou com EEPROM, tenta DEFAULT; se falhou com DEFAULT, volta para EEPROM
        if (!usingDefault) {
          // Falhou com PRIMÁRIA -> vai para DEFAULT
          g_ssid = DEFAULT_SSID; g_pass = DEFAULT_PASS; usingDefault = true;
        } else {
          // Falhou com DEFAULT -> volta para PRIMÁRIA
          g_ssid = pri_ssid; g_pass = pri_pass; usingDefault = false;
        }
      }
      break;
    }

    case WIFI_CONNECTED: {
      static uint32_t lastChk = 0;
      if (millis() - lastChk >= 2000) {
        lastChk = millis();
        if (WiFi.status() != WL_CONNECTED) {
          Serial.println(F("[WiFi] Perdeu conexão. Retentando..."));
          wifiConnState = WIFI_BACKOFF;
          nextRetryAt   = millis() + 1000;
          WiFi.disconnect();
          LED_OFF();
          // Mantém o alvo atual; troca somente se a nova tentativa expirar
        }
      }
      break;
    }

    case WIFI_BACKOFF:
      if ((int32_t)(millis() - nextRetryAt) >= 0) {
        Serial.print(F("[WiFi] Nova tentativa (SSID=")); Serial.print(g_ssid);
        Serial.print(F(", timeout=")); Serial.print(connectTimeoutMs); Serial.println(F(" ms)..."));
        beginConnectTo(g_ssid, g_pass, usingDefault);
      }
      break;
  }
}

// ======== DHT ========
void readDhtIfDue() {
  static uint32_t lastRead = 0;
  const uint32_t PERIOD_MS = 3000;
  if (millis() - lastRead < PERIOD_MS) return;
  lastRead = millis();

  float h = dht.readHumidity();
  float t = dht.readTemperature();
  if (isnan(h) || isnan(t)) {
    lastDht.ok = false;
    Serial.println(F("[DHT] Falha na leitura."));
  } else {
    lastDht = {t, h, true};
    Serial.printf("[DHT] T=%.1f °C  H=%.1f %%\n", t, h);
  }
}

// ======== HTML/CSS (Sidebar + Layout) ========
String htmlHeaderCss() {
  return
R"HTML(<style>
*{box-sizing:border-box} body{margin:0;font-family:Segoe UI,Roboto,Arial,sans-serif;background:#f3f4f6;color:#111}
.top{background:#0b63b6;color:#fff;padding:14px 18px;font-weight:600}
.layout{display:flex;min-height:100vh}
.side{width:220px;background:#0e1f33;color:#e5e7eb;position:sticky;top:0;height:100vh;padding:14px 10px}
.side .brand{font-weight:700;margin:6px 8px 12px 8px;color:#fff}
.side a{display:block;color:#cbd5e1;text-decoration:none;padding:10px 12px;border-radius:8px;margin:4px 6px}
.side a:hover{background:#132a4a;color:#fff}
.side a.active{background:#0b63b6;color:#fff}
.badge{display:inline-block;font-size:11px;padding:2px 6px;border-radius:999px;margin-left:8px;background:#eee;color:#333}
.badge.default{background:#f59e0b;color:#111}
.main{flex:1;padding:20px}
.wrap{max-width:860px;margin:0 auto}
.card{background:#fff;border-radius:12px;box-shadow:0 6px 18px rgba(0,0,0,.06);padding:18px;margin:12px 0}
.grid{display:grid;grid-template-columns:1fr 1fr;gap:12px}
.k{font-size:14px;color:#6b7280;text-transform:uppercase;letter-spacing:.08em}
.v{font-size:28px;font-weight:700;margin-top:6px}
.foot{margin-top:16px;font-size:12px;color:#6b7280}
.ok{display:inline-block;margin-left:8px;width:10px;height:10px;border-radius:50%;background:#10b981}
.bad{background:#ef4444}
button{border:0;border-radius:10px;padding:10px 14px;cursor:pointer}
.btn{background:#0b63b6;color:#fff}
input{width:100%;padding:10px;border:1px solid #d1d5db;border-radius:10px;margin-top:4px;font-size:16px}
label{display:block;font-size:14px;color:#374151;margin-top:8px}
pre{white-space:pre-wrap;word-break:break-word;background:#f9fafb;border-radius:10px;padding:12px}
@media(max-width:860px){.layout{flex-direction:column}.side{width:100%;height:auto;display:flex;gap:6px}}
</style>)HTML";
}

String htmlSidebar(const String& active) {
  String aHome   = (active=="home")   ? "active" : "";
  String aCfg    = (active=="config") ? "active" : "";
  String s =
    String(F("<div class='side'>"
             "<div class='brand'>ESP8266</div>"
             "<a class='")) + aHome + F("' href='/'>Início</a>"
             "<a class='") + aCfg + F("' href='/config'>Configuração</a>"
             "<a href='/ws/coleta' target='_blank'>API /ws/coleta</a>"
             "<a href='/ws/reboot'>Reiniciar</a>"
             "</div>");
  return s;
}

// Dashboard
String htmlPage() {
  String ip = WiFi.isConnected() ? WiFi.localIP().toString() : String("-");
  String ss = g_ssid;
  String badge = usingDefault ? "<span class='badge default'>default</span>" : "";
  String s =
    String(F("<!doctype html><html><head><meta charset='utf-8'>"
             "<meta name='viewport' content='width=device-width,initial-scale=1'>"
             "<title>ESP8266 - Temperatura & Umidade</title>"))
  + htmlHeaderCss() +
  F("</head><body>"
    "<div class='top'>ESP8266 • Temperatura & Umidade</div>"
    "<div class='layout'>")
  + htmlSidebar("home") +
  F("<div class='main'><div class='wrap'>"
      "<div class='card'>"
        "<div class='k'>SSID atual</div><div class='v'>")
  + ss + badge +
  F("</div><div class='grid' style='margin-top:12px'>"
        "<div><div class='k'>Temperatura</div><div id='temp' class='v'>--</div></div>"
        "<div><div class='k'>Umidade</div><div id='hum' class='v'>--</div></div>"
      "</div>"
      "<div class='foot'>IP: <span id='ip'>")
  + ip +
  F("</span><span id='status' class='ok'></span>"
    "<span id='time' style='margin-left:12px'></span></div>"
    "<div style='margin-top:10px'><button class='btn' onclick='refreshNow()'>Atualizar</button></div>"
    "</div>"
    "<div class='card'><div class='k'>Web API (JSON)</div><pre id='apiOut'>GET /ws/coleta → aguarde...</pre></div>"
    "</div></div></div>"
    "<script>"
    "function setVals(d){const ok=d&&d.ok;"
      "document.getElementById('temp').textContent=ok?(d.temperature.toFixed(1)+' °C'):'--';"
      "document.getElementById('hum').textContent =ok?(d.humidity.toFixed(1)+' %') :'--';"
      "document.getElementById('status').className=ok?'ok':'ok bad';"
      "document.getElementById('time').textContent=new Date().toLocaleTimeString();}"
    "async function refreshNow(){try{const r=await fetch('/dht',{cache:'no-store'});"
      "const j=await r.json();setVals(j);}catch(e){setVals({ok:false});}}"
    "async function refreshApi(){try{const r=await fetch('/ws/coleta',{cache:'no-store'});"
      "const txt=await r.text();document.getElementById('apiOut').textContent=txt;}catch(e){"
      "document.getElementById('apiOut').textContent='Erro ao chamar /ws/coleta';}}"
    "refreshNow();setInterval(refreshNow,2000);refreshApi();setInterval(refreshApi,4000);"
    "</script>"
    "</body></html>");
  return s;
}

// Config
String htmlConfigPage() {
  String curS = pri_ssid;
  String s =
    String(F("<!doctype html><html><head><meta charset='utf-8'>"
             "<meta name='viewport' content='width=device-width,initial-scale=1'>"
             "<title>Configuração de Wi-Fi</title>"))
  + htmlHeaderCss() +
  F("</head><body>"
    "<div class='top'>ESP8266 • Configuração</div>"
    "<div class='layout'>")
  + htmlSidebar("config") +
  F("<div class='main'><div class='wrap'>"
      "<div class='card'>"
        "<form method='POST' action='/ws/savewifi'>"
          "<label>SSID (até 32 chars)</label>"
          "<input type='text' name='ssid' maxlength='32' required value='")
  + curS +
  F("'>"
    "<label>Senha (até 64 chars)</label>"
    "<input type='password' name='pass' maxlength='64' placeholder='••••••••'>"
    "<button class='btn' type='submit'>Salvar</button>"
    "<div class='foot'>Após salvar, a conexão será atualizada. Se necessário, use <a href='/ws/reboot'>Reiniciar</a>.</div>"
    "</form>"
    "</div></div></div></div></body></html>");
  return s;
}

// ======== HTTP Handlers ========
void sendJson(const String& body) {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate");
  server.send(200, "application/json; charset=utf-8", body);
}

void handleRoot() {
  server.send(200, "text/html; charset=utf-8", htmlPage());
}

void handleConfig() {
  server.send(200, "text/html; charset=utf-8", htmlConfigPage());
}

void handleDhtJson() {
  String out = "{";
  out += "\"temperature\":" + String(lastDht.ok ? String(lastDht.t, 1) : "null") + ",";
  out += "\"humidity\":"    + String(lastDht.ok ? String(lastDht.h, 1) : "null") + ",";
  out += "\"ok\":"          + String(lastDht.ok ? "true" : "false");
  out += "}";
  sendJson(out);
}

void handleWsColeta() {
  const bool connected = WiFi.status() == WL_CONNECTED;
  String ip  = connected ? WiFi.localIP().toString() : String("");
  String hst = WiFi.hostname();
  String out = "{";
  out += "\"device\":\"" + hst + "\",";
  out += "\"ip\":" + (connected ? ("\"" + ip + "\"") : "null") + ",";
  out += "\"ssid\":\"" + g_ssid + "\",";
  out += "\"using_default\":" + String(usingDefault ? "true" : "false") + ",";
  out += "\"ok\":" + String(lastDht.ok ? "true" : "false") + ",";
  out += "\"temperature\":" + String(lastDht.ok ? String(lastDht.t, 1) : "null") + ",";
  out += "\"humidity\":"    + String(lastDht.ok ? String(lastDht.h, 1) : "null") + ",";
  out += "\"ts_ms\":" + String(millis());
  out += "}";
  sendJson(out);
}

void handleSaveWifi() {
  String ssid = server.hasArg("ssid") ? server.arg("ssid") : "";
  String pass = server.hasArg("pass") ? server.arg("pass") : "";
  ssid.trim(); pass.trim();

  if (ssid.length() == 0 || ssid.length() > 32 || pass.length() > 64) {
    server.send(400, "text/plain; charset=utf-8", "SSID/senha invalidos.");
    return;
  }

  if (eepromSaveWifi(ssid, pass)) {
    pri_ssid = ssid; pri_pass = pass;            // atualiza rede PRIMÁRIA
    usingDefault = false;                         // preferir primária
    WiFi.disconnect();
    wifiConnState = WIFI_BACKOFF;
    connectTimeoutMs = WIFI_FIRST_TIMEOUT_MS;    // reseta janela
    nextRetryAt = millis();

    String html =
      String(F("<!doctype html><html><head><meta charset='utf-8'>"
               "<meta name='viewport' content='width=device-width,initial-scale=1'>"
               "<title>Wi-Fi salvo</title>")) +
      htmlHeaderCss() +
      F("</head><body><div class='top'>ESP8266 • Configuração</div>"
        "<div class='layout'>") +
      htmlSidebar("config") +
      F("<div class='main'><div class='wrap'>"
        "<div class='card'>✅ Credenciais salvas na EEPROM.<br>SSID: <b>") + ssid +
      F("</b><br>Reconectando..."
        "<div style='margin-top:10px'><a class='btn' href='/'>Voltar</a> "
        "<a class='btn' style='margin-left:8px' href='/ws/reboot'>Reiniciar</a></div>"
        "</div></div></div></div></body></html>");
    server.send(200, "text/html; charset=utf-8", html);
  } else {
    server.send(500, "text/plain; charset=utf-8", "Falha ao gravar EEPROM.");
  }
}

void handleReboot() {
  server.send(200, "text/plain; charset=utf-8", "Reiniciando...");
  server.client().flush();
  delay(100);
  ESP.restart();
}

void handleNotFound() {
  if (server.uri().startsWith("/ws/") || server.uri().startsWith("/dht")) {
    server.send(404, "application/json", "{\"error\":\"not found\"}");
  } else {
    handleRoot();
  }
}

// ======== Setup / Loop ========
void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  LED_OFF();

  Serial.begin(115200);
  delay(10);
  Serial.println(F("=== ESP8266 WiFi + DHT11 + HTTP + Config + Fallback por tentativa ==="));
  printResetInfo();

  EEPROM.begin(EEPROM_SIZE);
  String s, p;
  if (eepromLoadWifi(s, p)) {
    pri_ssid = s; pri_pass = p;
    Serial.print(F("[EEPROM] SSID carregado: ")); Serial.println(pri_ssid);
  } else {
    Serial.println(F("[EEPROM] Sem credenciais válidas. Usando DEFAULT_* como primário."));
    pri_ssid = DEFAULT_SSID;
    pri_pass = DEFAULT_PASS;
  }

  dht.begin();

  // Inicializa WiFi (primeiro com PRIMÁRIA)
  wifiInitIfNeeded();

  // Rotas HTTP
  server.on("/",             handleRoot);
  server.on("/config",       handleConfig);
  server.on("/dht",          handleDhtJson);
  server.on("/ws/coleta",    HTTP_GET, handleWsColeta);
  server.on("/ws/savewifi",  HTTP_POST, handleSaveWifi);
  server.on("/ws/reboot",    handleReboot);
  server.onNotFound(handleNotFound);

  server.begin();
  Serial.println(F("[HTTP] Servidor iniciado na porta 80"));
}

void loop() {
  wifiStateMachine();
  readDhtIfDue();

  server.handleClient();

  static uint32_t last = 0;
  if (millis() - last >= 5000) {
    last = millis();
    wl_status_t st = WiFi.status();
    Serial.print(F("[WiFi] Status=")); Serial.print(st);
    Serial.print(F(" SSID=")); Serial.print(g_ssid);
    if (usingDefault) Serial.print(F(" (DEFAULT)"));
    if (st == WL_CONNECTED) {
      Serial.print(F(" IP="));   Serial.print(WiFi.localIP());
      Serial.print(F(" RSSI=")); Serial.print(WiFi.RSSI()); Serial.print(F(" dBm"));
    }
    if (lastDht.ok) {
      Serial.printf(" | T=%.1f°C H=%.1f%%", lastDht.t, lastDht.h);
    }
    Serial.println();
  }

  safeYield();
}
