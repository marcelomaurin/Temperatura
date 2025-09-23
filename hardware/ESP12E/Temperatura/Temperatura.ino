/*******************************************************
 * ESP8266 WiFi Setup + DHT11 + mDNS (srvTemp.local)
 * Patch: DHT em GPIO4 (D2), debug de boot, pequenos yields
 *******************************************************/
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <DNSServer.h>
#include <EEPROM.h>
#include <DHT.h>
#include <ESP8266mDNS.h>

// ================== CONFIG GERAL ==================
#define EEPROM_SIZE       256
#define EEPROM_MAGIC_ADDR 0
#define EEPROM_DATA_ADDR  4
#define MAGIC_VALUE       0x42A1C0DE

#define CONNECT_TIMEOUT_MS 20000

const byte DNS_PORT = 53;
DNSServer dnsServer;
ESP8266WebServer server(80);

// ================== DHT11 ==================
// ⚠️ MOVIDO para GPIO4 (D2) para não usar pino de boot (GPIO2/D4)
#define DHT_PIN   4     // GPIO4 (D2)
#define DHT_TYPE  DHT11
DHT dht(DHT_PIN, DHT_TYPE);

struct DhtReading {
  float t;
  float h;
  uint32_t ts;
};
DhtReading gDht = {NAN, NAN, 0};
uint32_t lastDhtSampleMs = 0;
const uint32_t DHT_PERIOD_MS = 3000;

// ================== REDE ==================
struct WifiCreds {
  char ssid[32];
  char pass[64];
};
WifiCreds gCreds;

String apSsid;
IPAddress apIP(192,168,4,1);
IPAddress apGW(192,168,4,1);
IPAddress apMASK(255,255,255,0);

bool haveCreds = false;
bool staConnected = false;

const char* MDNS_HOST = "srvtemp";  // => srvtemp.local

// ================== PERSISTÊNCIA ==================
void saveCredsToEEPROM(const WifiCreds &c){
  EEPROM.begin(EEPROM_SIZE);
  uint32_t magic = MAGIC_VALUE;
  EEPROM.put(EEPROM_MAGIC_ADDR, magic);
  EEPROM.put(EEPROM_DATA_ADDR, c);
  EEPROM.commit();
  EEPROM.end();
}

bool loadCredsFromEEPROM(WifiCreds &out){
  EEPROM.begin(EEPROM_SIZE);
  uint32_t magic=0;
  EEPROM.get(EEPROM_MAGIC_ADDR, magic);
  if(magic != MAGIC_VALUE){ EEPROM.end(); return false; }
  EEPROM.get(EEPROM_DATA_ADDR, out);
  EEPROM.end();
  return (out.ssid[0] != '\0');
}

void clearCreds(){
  EEPROM.begin(EEPROM_SIZE);
  uint32_t zero=0;
  EEPROM.put(EEPROM_MAGIC_ADDR, zero);
  WifiCreds blank = {};
  EEPROM.put(EEPROM_DATA_ADDR, blank);
  EEPROM.commit();
  EEPROM.end();
}

// ================== HTML (igual ao teu) ==================
const char PAGE_INDEX[] PROGMEM = R"HTML(
<!-- (mesmo HTML que você colou, sem mudanças) -->
)HTML";

// ================== mDNS ==================
void startMDNS() {
  if (MDNS.isRunning()) { MDNS.end(); delay(10); }

  bool ok = false;
  if (WiFi.status() == WL_CONNECTED) {
    ok = MDNS.begin(MDNS_HOST);                 // IP da STA
  } else {
    ok = MDNS.begin(MDNS_HOST, WiFi.softAPIP()); // IP do AP
  }

  if (ok) {
    MDNS.addService("http", "tcp", 80);
    MDNS.addServiceTxt("http", "tcp", "path", "/");
    Serial.println(F("[mDNS] Anunciado como srvtemp.local (http._tcp)."));
  } else {
    Serial.println(F("[mDNS] Falha ao iniciar mDNS."));
  }
}

// ================== REDE / AP ==================
void startAP(){
  WiFi.mode(WIFI_AP);
  delay(50); yield();                 // pequeno respiro após mudar modo
  WiFi.softAPConfig(apIP, apGW, apMASK);

  apSsid = "srvTemp";                 // SSID fixo
  WiFi.softAP(apSsid.c_str());
  delay(50); yield();

  dnsServer.start(DNS_PORT, "*", apIP); // portal cativo

  Serial.println(F("[AP] Portal de configuração ativo."));
  Serial.print (F("[AP] SSID: ")); Serial.println(apSsid);
  Serial.print (F("[AP] IP:   ")); Serial.println(WiFi.softAPIP());

  startMDNS(); // mDNS no AP (quando cliente suportar)
}

bool tryConnectSTA(const WifiCreds &c){
  Serial.print(F("[STA] Conectando a SSID: "));
  Serial.println(c.ssid);

  WiFi.mode(WIFI_STA);
  delay(50); yield();
  WiFi.begin(c.ssid, c.pass);

  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && (millis()-t0) < CONNECT_TIMEOUT_MS){
    delay(200);
    yield();                        // garante WDT feliz
    Serial.print('.');
  }
  Serial.println();

  if(WiFi.status() == WL_CONNECTED){
    Serial.print(F("[STA] Conectado! IP: "));
    Serial.println(WiFi.localIP());
    startMDNS();                    // atualiza mDNS para IP da STA
    return true;
  }else{
    Serial.println(F("[STA] Falhou a conexão."));
    return false;
  }
}

// ================== DHT: Leitura periódica ==================
void sampleDHT(){
  if(millis() - lastDhtSampleMs < DHT_PERIOD_MS) return;
  lastDhtSampleMs = millis();

  float h = dht.readHumidity();
  float t = dht.readTemperature(); // °C

  if (isnan(h) || isnan(t)) {
    Serial.println(F("[DHT] Falha de leitura (NaN)."));
    return;
  }
  gDht.h = h;
  gDht.t = t;
  gDht.ts = millis();

  Serial.print(F("[DHT] T=")); Serial.print(t);
  Serial.print(F("C  H=")); Serial.print(h);
  Serial.println(F("%"));
}

// ================== AUX: parsing body x-www-form-urlencoded (igual ao teu) ==================
bool parseFormUrlEncoded(const String& body, String& ssidOut, String& passOut){
  int p1 = body.indexOf("ssid=");
  int p2 = body.indexOf("&pass=");
  if(p1 < 0 || p2 < 0) return false;
  String v1 = body.substring(p1+5, p2);
  String v2 = body.substring(p2+6);
  v1.replace("+"," "); v2.replace("+"," ");
  auto pct = [](String s){
    String r; r.reserve(s.length());
    for (size_t i=0;i<s.length();){
      if (s[i]=='%' && i+2<s.length()){
        char h1=s[i+1], h2=s[i+2];
        int hi = isdigit(h1)? h1-'0' : (toupper(h1)-'A'+10);
        int lo = isdigit(h2)? h2-'0' : (toupper(h2)-'A'+10);
        char c = (char)((hi<<4)|lo);
        r += c; i+=3;
      } else {
        r += s[i++];
      }
    }
    return r;
  };
  ssidOut = pct(v1);
  passOut = pct(v2);
  return true;
}

// ================== ROTAS (iguais às tuas) ==================
void handleRoot(){
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/html; charset=utf-8", "");
  WiFiClient client = server.client();
  client.write_P(PAGE_INDEX, strlen_P(PAGE_INDEX));
}

void handleStatus(){
  uint8 wifiMode = WiFi.getMode();
  String mode = (wifiMode==WIFI_STA)?"STA":(wifiMode==WIFI_AP)?"AP":(wifiMode==WIFI_AP_STA)?"AP+STA":"DESCONHECIDO";

  bool connected = (WiFi.status()==WL_CONNECTED);
  IPAddress ip   = connected ? WiFi.localIP() : WiFi.softAPIP();
  String ipStr   = ip.toString();

  String host = String(MDNS_HOST) + ".local";
  String url  = String("http://") + (connected ? host : ipStr) + "/";

  String ssid = connected ? WiFi.SSID() : "";
  long rssi   = connected ? WiFi.RSSI() : 0;

  String out = "{";
  out += "\"mode\":\""+mode+"\",";
  out += "\"ip\":\""+ipStr+"\",";
  out += "\"host\":\""+host+"\",";
  out += "\"url\":\""+url+"\",";
  out += "\"ssid\":\""+ssid+"\",";
  out += "\"rssi\":";
  out += connected ? String(rssi) : "null";
  out += "}";
  server.send(200, "application/json; charset=utf-8", out);
}

void handleScan(){
  int n = WiFi.scanNetworks();
  String out = "[";
  for(int i=0;i<n;i++){
    if(i) out += ",";
    out += "{";
    out += "\"ssid\":\""+ String(WiFi.SSID(i)) +"\",";
    out += "\"rssi\":"+ String(WiFi.RSSI(i)) +",";
    out += "\"chan\":"+ String(WiFi.channel(i)) +",";
    out += "\"open\":"+ String(WiFi.encryptionType(i)==ENC_TYPE_NONE ? "true":"false");
    out += "}";
  }
  out += "]";
  server.send(200, "application/json; charset=utf-8", out);
}

void handleSave(){
  Serial.println(F("[HTTP] POST /save recebido"));
  if(server.method() != HTTP_POST){
    Serial.println(F("[HTTP] Metodo != POST"));
    server.send(405, "application/json", "{\"ok\":false,\"msg\":\"Método inválido\"}");
    return;
  }

  Serial.printf("[HTTP] args=%d, contentType=%s\n",
                server.args(), server.header("Content-Type").c_str());
  for (int i = 0; i < server.args(); i++) {
    Serial.print(" - "); Serial.print(server.argName(i)); Serial.print(" = ");
    Serial.println(server.arg(i));
  }

  String ssid = server.arg("ssid");
  String pass = server.arg("pass");

  if (ssid.length()==0 && pass.length()==0 && server.hasArg("plain")){
    String body = server.arg("plain");
    Serial.print(F("[HTTP] plain body: ")); Serial.println(body);
    String s2,p2;
    if (parseFormUrlEncoded(body, s2, p2)){
      ssid = s2; pass = p2;
      Serial.println(F("[HTTP] parseFormUrlEncoded OK (fallback)"));
    }
  }

  ssid.trim();
  if(ssid.length()==0){
    Serial.println(F("[HTTP] SSID vazio"));
    server.send(400, "application/json", "{\"ok\":false,\"msg\":\"SSID vazio\"}");
    return;
  }
  if(ssid.length() >= sizeof(gCreds.ssid) || pass.length() >= sizeof(gCreds.pass)){
    Serial.println(F("[HTTP] Dados muito longos"));
    server.send(400, "application/json", "{\"ok\":false,\"msg\":\"Dados muito longos\"}");
    return;
  }

  memset(&gCreds, 0, sizeof(gCreds));
  ssid.toCharArray(gCreds.ssid, sizeof(gCreds.ssid));
  pass.toCharArray(gCreds.pass, sizeof(gCreds.pass));

  Serial.print(F("[HTTP] Salvando SSID: ")); Serial.println(gCreds.ssid);
  saveCredsToEEPROM(gCreds);
  haveCreds = true;

  server.send(200, "application/json; charset=utf-8", "{\"ok\":true,\"msg\":\"Salvo. Tentando conectar...\"}");
  delay(500);

  if(tryConnectSTA(gCreds)){
    staConnected = true;
  }else{
    startAP();
  }
}

void handleClear(){
  if(server.method() != HTTP_POST){
    server.send(405, "application/json", "{\"ok\":false}");
    return;
  }
  clearCreds();
  haveCreds = false;
  server.send(200, "application/json; charset=utf-8", "{\"ok\":true}");
  delay(300);
  startAP();
}

void handleDht(){
  sampleDHT();
  String out = "{";
  if(!isnan(gDht.t)) { out += "\"temp\":" + String(gDht.t, 1); } else { out += "\"temp\":null"; }
  out += ",";
  if(!isnan(gDht.h)) { out += "\"hum\":"  + String(gDht.h, 1); } else { out += "\"hum\":null"; }
  out += ",";
  out += "\"ts\":" + String(gDht.ts);
  out += "}";
  server.send(200, "application/json; charset=utf-8", out);
}

void notFound(){
  server.sendHeader("Location", String("http://") + apIP.toString() + "/", true);
  server.send(302, "text/plain", "");
}

// ================== SETUP / LOOP ==================
void setup(){
  // 🔎 Use 74880 enquanto depura (mostra bootloader). Depois pode voltar a 115200.
  Serial.begin(115200);
  delay(50);
  Serial.println();
  Serial.print(F("[BOOT] Reset reason: "));
  Serial.println(ESP.getResetReason());

  dht.begin();

  haveCreds = loadCredsFromEEPROM(gCreds);

  if(haveCreds){
    Serial.print(F("[BOOT] Credenciais encontradas para SSID: "));
    Serial.println(gCreds.ssid);
    if(tryConnectSTA(gCreds)){
      staConnected = true;
    }else{
      startAP();
    }
  }else{
    Serial.println(F("[BOOT] Nenhuma credencial salva."));
    startAP();
  }

  server.on("/",           HTTP_GET,  handleRoot);
  server.on("/status",     HTTP_GET,  handleStatus);
  server.on("/scan",       HTTP_GET,  handleScan);
  server.on("/save",       HTTP_POST, handleSave);
  server.on("/clear",      HTTP_POST, handleClear);
  server.on("/dht",        HTTP_GET,  handleDht);
  server.onNotFound(notFound);
  server.begin();

  Serial.println(F("[HTTP] Servidor iniciado na porta 80"));
}

void loop(){
  dnsServer.processNextRequest();
  server.handleClient();
  sampleDHT();
  MDNS.update();
}
