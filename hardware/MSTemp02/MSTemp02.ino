#include <DHT.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <SPI.h>
#include <Ethernet.h>
#include <EEPROM.h>

// ======== CONFIG PADRÃO (usado se EEPROM vazia) ========
#define DEFAULT_USE_STATIC_IP  1  // 1 = IP fixo, 0 = DHCP
const IPAddress DEFAULT_IP   (172, 17, 240, 253);
const IPAddress DEFAULT_DNS  (172, 17, 240,  2);
const IPAddress DEFAULT_GW   (172, 17, 240,  1);
const IPAddress DEFAULT_MASK (255, 255, 252, 0);

// ======== Pinos ========
#define DHTPIN 6
#define DHTTYPE DHT22
const int PIN_BUZZER  = A11;
const int PIN_BUZZ_G  = A8;
const int PIN_LED     = 7;

// ======== Objetos ========
DHT dht(DHTPIN, DHTTYPE);
LiquidCrystal_I2C lcd(0x27, 16, 2);

// MAC: altere p/ ser único na rede
byte mac[] = {0xDE,0xAD,0xBE,0xEF,0xFE,0xED};
EthernetServer server(8081);

// ======== Amostragem ========
unsigned long lastSampleMs = 0;
const unsigned long sampleIntervalMs = 2500;
unsigned long dhtWarmupUntil = 0;
float lastTemp = NAN, lastHum = NAN;

// ======== Alertas ========
const float THRESH_C = 30.0;
bool wasBelowThreshold = true;

// ======== Tela ========
unsigned long ipSplashStartMs = 0;
bool ipSplashDone = false;
const unsigned long IP_SPLASH_MS = 10000;

// ======== Config persistente (EEPROM) ========
struct NetConfig {
  uint8_t magic;       // 0x42
  uint8_t use_static;  // 0/1
  uint8_t ip[4];
  uint8_t dns[4];
  uint8_t gw[4];
  uint8_t mask[4];
  uint8_t checksum;    // soma simples de bytes (exceto checksum)
};
const uint8_t CFG_MAGIC = 0x42;
const int EEPROM_ADDR = 0;

NetConfig cfg;

uint8_t calcChecksum(const NetConfig &c) {
  const uint8_t *p = (const uint8_t*)&c;
  uint16_t s = 0;
  for (size_t i = 0; i < sizeof(NetConfig)-1; i++) s += p[i];
  return (uint8_t)(s & 0xFF);
}

void loadDefaults(NetConfig &c) {
  c.magic = CFG_MAGIC;
  c.use_static = DEFAULT_USE_STATIC_IP ? 1 : 0;
  for (int i=0;i<4;i++) {
    c.ip[i]   = DEFAULT_IP[i];
    c.dns[i]  = DEFAULT_DNS[i];
    c.gw[i]   = DEFAULT_GW[i];
    c.mask[i] = DEFAULT_MASK[i];
  }
  c.checksum = calcChecksum(c);
}

bool loadConfig(NetConfig &c) {
  EEPROM.get(EEPROM_ADDR, c);
  if (c.magic != CFG_MAGIC) return false;
  if (c.checksum != calcChecksum(c)) return false;
  return true;
}

void saveConfig(const NetConfig &c) {
  EEPROM.put(EEPROM_ADDR, c);
}

void applyNetworkFromConfig() {
  if (cfg.use_static) {
    IPAddress ip  (cfg.ip[0],  cfg.ip[1],  cfg.ip[2],  cfg.ip[3]);
    IPAddress dns (cfg.dns[0], cfg.dns[1], cfg.dns[2], cfg.dns[3]);
    IPAddress gw  (cfg.gw[0],  cfg.gw[1],  cfg.gw[2],  cfg.gw[3]);
    IPAddress mask(cfg.mask[0],cfg.mask[1],cfg.mask[2],cfg.mask[3]);
    Ethernet.begin(mac, ip, dns, gw, mask);
  } else {
    if (Ethernet.begin(mac) == 0) {
      // fallback simples caso DHCP falhe
      IPAddress ip(192,168,1,177), dns(192,168,1,1), gw(192,168,1,1), mask(255,255,255,0);
      Ethernet.begin(mac, ip, dns, gw, mask);
    }
  }
  delay(500);
  server.begin(); // garante que o server está ativo após reconfig
}

void printIPToSerial() {
  Serial.print(F("IP: ")); Serial.print(Ethernet.localIP());
  Serial.print(F(" | GW: ")); Serial.print(Ethernet.gatewayIP());
  Serial.print(F(" | DNS: ")); Serial.print(Ethernet.dnsServerIP());
  Serial.print(F(" | Mask: ")); Serial.println(Ethernet.subnetMask());
  Serial.println(F("HTTP na porta 8081"));
}

void printIPLine() {
  IPAddress ip = Ethernet.localIP();
  char line[17];
  snprintf(line, sizeof(line), "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
  lcd.setCursor(0, 0);
  lcd.print("                ");
  lcd.setCursor(0, 0);
  lcd.print(line);
}

void printTHLine() {
  char line[17];
  char tbuf[8], hbuf[8];
  dtostrf(lastTemp, 4, 1, tbuf);
  dtostrf(lastHum,  4, 1, hbuf);
  snprintf(line, sizeof(line), "T:%sC H:%s%%", tbuf, hbuf);
  lcd.setCursor(0, 1);
  lcd.print("                ");
  lcd.setCursor(0, 1);
  lcd.print(line);
}

void beep(uint16_t freq, uint16_t durMs) {
  tone(PIN_BUZZER, freq, durMs);
  delay(durMs + 5);
  noTone(PIN_BUZZER);
}

void startupChime() {
  beep(1200, 120);
  beep(1600, 120);
  beep(2000, 160);
}

// ---- Utils HTTP/IP ----
bool parseIp(const String &s, uint8_t out[4]) {
  int p1 = s.indexOf('.');
  int p2 = s.indexOf('.', p1+1);
  int p3 = s.indexOf('.', p2+1);
  if (p1<0||p2<0||p3<0) return false;
  long a = s.substring(0, p1).toInt();
  long b = s.substring(p1+1, p2).toInt();
  long c = s.substring(p2+1, p3).toInt();
  long d = s.substring(p3+1).toInt();
  if (a<0||a>255||b<0||b>255||c<0||c>255||d<0||d>255) return false;
  out[0]=a; out[1]=b; out[2]=c; out[3]=d; return true;
}

String getQueryParam(const String &query, const String &key) {
  // procura "key=" e lê até '&' ou fim
  String pat = key + "=";
  int i = query.indexOf(pat);
  if (i < 0) return "";
  int j = query.indexOf('&', i + pat.length());
  if (j < 0) j = query.length();
  return query.substring(i + pat.length(), j);
}

void sendHtmlHeader(EthernetClient &client) {
  client.println(F("HTTP/1.1 200 OK"));
  client.println(F("Content-Type: text/html; charset=utf-8"));
  client.println(F("Connection: close"));
  client.println();
}

void sendJsonHeader(EthernetClient &client) {
  client.println(F("HTTP/1.1 200 OK"));
  client.println(F("Content-Type: application/json; charset=utf-8"));
  client.println(F("Access-Control-Allow-Origin: *"));
  client.println(F("Connection: close"));
  client.println();
}

void sendNotFound(EthernetClient &client) {
  client.println(F("HTTP/1.1 404 Not Found"));
  client.println(F("Content-Type: text/plain; charset=utf-8"));
  client.println(F("Connection: close"));
  client.println();
  client.println(F("404"));
}

void handleRootPage(EthernetClient &client, const String &query) {
  bool changed = false;
  NetConfig newCfg = cfg;

  if (query.length() > 0) {
    String v_static = getQueryParam(query, "use_static");
    String v_ip     = getQueryParam(query, "ip");
    String v_dns    = getQueryParam(query, "dns");
    String v_gw     = getQueryParam(query, "gw");
    String v_mask   = getQueryParam(query, "mask");

    if (v_static.length()) {
      newCfg.use_static = (uint8_t)(v_static.toInt() ? 1 : 0);
      changed = true;
    }
    uint8_t tmp[4];
    if (v_ip.length()   && parseIp(v_ip,   tmp)) { memcpy(newCfg.ip,   tmp, 4); changed = true; }
    if (v_dns.length()  && parseIp(v_dns,  tmp)) { memcpy(newCfg.dns,  tmp, 4); changed = true; }
    if (v_gw.length()   && parseIp(v_gw,   tmp)) { memcpy(newCfg.gw,   tmp, 4); changed = true; }
    if (v_mask.length() && parseIp(v_mask, tmp)) { memcpy(newCfg.mask, tmp, 4); changed = true; }

    if (changed) {
      newCfg.magic = CFG_MAGIC;
      newCfg.checksum = calcChecksum(newCfg);
      cfg = newCfg;
      saveConfig(cfg);
      applyNetworkFromConfig(); // aplica imediatamente
    }
  }

  // Página HTML simples com form (GET)
  sendHtmlHeader(client);
  client.println(F("<!doctype html><html><head><meta charset='utf-8'><title>Monitor Temperatura</title>"
                   "<meta name='viewport' content='width=device-width,initial-scale=1'>"
                   "<style>body{font-family:system-ui;margin:20px}input{padding:6px;margin:4px 0}label{display:block;margin-top:8px}"
                   "button{padding:8px 14px} .card{border:1px solid #ddd;padding:14px;border-radius:8px;max-width:420px}"
                   ".row{display:flex;gap:12px} .row>div{flex:1}</style></head><body>"));
  client.println(F("<h2>Equipamento de Monitoramento</h2>"));

  // Status T/H
  client.print(F("<div class='card'><h3>Leitura atual</h3><p>Temperatura: "));
  if (isnan(lastTemp)) client.print(F("--"));
  else { client.print(lastTemp,1); client.print(F(" &deg;C")); }
  client.print(F("<br>Umidade: "));
  if (isnan(lastHum)) client.print(F("--"));
  else { client.print(lastHum,1); client.print(F("% RH")); }
  client.println(F("</p></div><br>"));

  // Form de configuração
  client.println(F("<div class='card'><h3>Configura&ccedil;&atilde;o de Rede</h3>"
                   "<form method='GET' action='/'>"));

  client.print(F("<label>USE_STATIC_IP (0 ou 1): <input name='use_static' value='"));
  client.print(cfg.use_static ? 1 : 0);
  client.println(F("'></label>"));

  auto printIPField = [&](const char* name, const uint8_t a[4]){
    client.print(F("<label>"));
    client.print(name);
    client.print(F(": <input name='"));
    client.print(name);
    client.print(F("' value='"));
    client.print(a[0]); client.print('.');
    client.print(a[1]); client.print('.');
    client.print(a[2]); client.print('.');
    client.print(a[3]);
    client.println(F("'></label>"));
  };

  printIPField("ip",   cfg.ip);
  printIPField("dns",  cfg.dns);
  printIPField("gw",   cfg.gw);
  printIPField("mask", cfg.mask);

  client.println(F("<button type='submit'>Salvar & Aplicar</button>"));
  client.println(F("</form></div><p style='color:#555'>Acesse JSON em <code>/ws/temperatura</code>.</p>"));

  // Info de serviço
  client.print(F("<p>Rodando em "));
  IPAddress ip = Ethernet.localIP();
  client.print(ip);
  client.println(F(":8081</p>"));

  client.println(F("</body></html>"));
}

void handleJson(EthernetClient &client) {
  sendJsonHeader(client);
  if (isnan(lastTemp) || isnan(lastHum)) {
    client.println(F("{\"temperature\":null,\"humidity\":null,"
                     "\"unit_temp\":\"C\",\"unit_humidity\":\"%RH\",\"status\":\"warming_up\"}"));
  } else {
    client.print(F("{\"temperature\":")); client.print(lastTemp, 2);
    client.print(F(",\"humidity\":"));    client.print(lastHum, 2);
    client.print(F(",\"unit_temp\":\"C\",\"unit_humidity\":\"%RH\"}"));
    client.println();
  }
}

void handleHttp(EthernetClient &client) {
  // espera request
  unsigned long t0 = millis();
  while (client.connected() && !client.available() && millis() - t0 < 1000) {}
  if (!client.available()) return;

  // primeira linha: "GET /path?query HTTP/1.1"
  String reqLine = client.readStringUntil('\r');
  client.read(); // '\n'

  // cabeçalhos (descarta)
  while (client.connected()) {
    String h = client.readStringUntil('\n');
    if (h == "\r" || h.length() == 1) break;
  }

  // extrai path e query
  int sp1 = reqLine.indexOf(' ');
  int sp2 = reqLine.indexOf(' ', sp1+1);
  String url = reqLine.substring(sp1+1, sp2);  // /..., possivelmente com ?...
  String path = url;
  String query = "";
  int q = url.indexOf('?');
  if (q >= 0) {
    path = url.substring(0, q);
    query = url.substring(q+1);
  }

  if (path == "/" || path == "/index.html") {
    handleRootPage(client, query);
  } else if (path == "/ws/temperatura") {
    handleJson(client);
  } else {
    sendNotFound(client);
  }
}

void setup() {
  Serial.begin(115200);

  // Buzzer/LED/DHT
  pinMode(PIN_BUZZ_G, OUTPUT); digitalWrite(PIN_BUZZ_G, LOW);
  pinMode(PIN_BUZZER, OUTPUT); noTone(PIN_BUZZER);
  pinMode(PIN_LED, OUTPUT); digitalWrite(PIN_LED, LOW);
  pinMode(DHTPIN, INPUT_PULLUP);
  dht.begin();
  dhtWarmupUntil = millis() + 2000;

  // LCD
  lcd.init(); lcd.backlight(); lcd.clear();
  lcd.setCursor(0,0); lcd.print(F("Inicializando..."));

  // SPI / Ethernet (Mega + Shield)
  pinMode(53, OUTPUT); digitalWrite(53, HIGH); // SS do Mega
  pinMode(10, OUTPUT);                         // CS Ethernet
  pinMode(4, OUTPUT);  digitalWrite(4, HIGH);  // desabilita SD do shield

  // Carrega config
  if (!loadConfig(cfg)) {
    loadDefaults(cfg);
    saveConfig(cfg);
  }

  // Aplica rede
  applyNetworkFromConfig();
  server.begin();

  Serial.println(F("Servidor HTTP iniciado"));
  printIPToSerial();

  // Splash: só IP por 10 s
  lcd.clear();
  printIPLine();
  lcd.setCursor(0,1); lcd.print("                ");
  ipSplashStartMs = millis();
  ipSplashDone = false;

  startupChime();
}

void sampleSensorIfNeeded() {
  unsigned long now = millis();
  if (now < dhtWarmupUntil) return;
  if (now - lastSampleMs < sampleIntervalMs) return;
  lastSampleMs = now;

  digitalWrite(PIN_LED, HIGH);

  float t = dht.readTemperature();
  float h = dht.readHumidity();

  if (!isnan(t) && !isnan(h)) {
    lastTemp = t; lastHum = h;

    Serial.print(F("Temperatura: ")); Serial.println(lastTemp, 2);
    Serial.print(F("Humidade: "));    Serial.println(lastHum, 2);

    if (wasBelowThreshold && lastTemp >= THRESH_C) {
      beep(1800, 180);
      wasBelowThreshold = false;
    } else if (lastTemp < THRESH_C) {
      wasBelowThreshold = true;
    }

    if (ipSplashDone) {
      printIPLine();
      printTHLine();
    }
  } else {
    Serial.println(F("Falha leitura DHT22"));
  }

  delay(60);
  digitalWrite(PIN_LED, LOW);
}

void loop() {
  // controla splash IP
  if (!ipSplashDone && (millis() - ipSplashStartMs >= IP_SPLASH_MS)) {
    ipSplashDone = true;
    printIPLine();
    if (!isnan(lastTemp) && !isnan(lastHum)) {
      printTHLine();
    } else {
      lcd.setCursor(0,1);
      lcd.print("Aguardando DHT ");
    }
  }

  sampleSensorIfNeeded();

  EthernetClient client = server.available();
  if (client) {
    handleHttp(client);
    delay(1);
    client.stop();
  }
}
