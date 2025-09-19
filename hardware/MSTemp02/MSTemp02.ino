#include <DHT.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <SPI.h>
#include <Ethernet.h>
#include <EEPROM.h>
#include <SD.h>
#include <EthernetUdp.h>
#include <Dns.h>   // <-- ADD

// ======== DEFAULTS (EEPROM ausente) ========
#define DEFAULT_USE_STATIC_IP  1
const IPAddress DEFAULT_IP   (172, 17, 240, 253);
const IPAddress DEFAULT_DNS  (172, 17, 240,   2);
const IPAddress DEFAULT_GW   (172, 17, 240,   1);
const IPAddress DEFAULT_MASK (255, 255, 252,  0);
const uint8_t  DEFAULT_MAC[6] = {0xDE,0xAD,0xBE,0xEF,0xFE,0xED};

// ======== Pinos ========
#define DHTPIN 6
#define DHTTYPE DHT22
const int PIN_BUZZER  = A11;
const int PIN_BUZZ_G  = A8;
const int PIN_LED     = 7;
const int PIN_CS_ETH  = 10;
const int PIN_CS_SD   = 4;

// ======== Objetos ========
DHT dht(DHTPIN, DHTTYPE);
LiquidCrystal_I2C lcd(0x27, 16, 2);
EthernetServer server(8081);
EthernetUDP Udp;

// ======== NTP ========
const char* NTP_SERVER = "a.st1.ntp.br"; // NTP Brasil
const unsigned int NTP_LOCAL_PORT = 8888;
const unsigned long NTP_INTERVAL_MS = 10UL * 60UL * 1000UL; // re-sync a cada 10 min
unsigned long lastNtpSyncMs = 0;
unsigned long epochAtLastSync = 0;  // epoch UTC
unsigned long msAtLastSync = 0;

// São Paulo UTC-3; vamos formatar no browser, então guardamos UTC aqui.

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
  uint8_t mac[6];
  uint8_t checksum;    // soma simples (exceto checksum)
};
const uint8_t CFG_MAGIC = 0x42;
const int EEPROM_ADDR = 0;

NetConfig cfg;

// ======== SD / Log ========
bool sdAvailable = false;
char currentMonthFile[20] = {0}; // e.g. "L202509.CSV"
volatile bool sdBusy = false;    // <-- trava escrita durante export/export/clear



bool resolveHostname(const char* host, IPAddress& outIP) {
  DNSClient dns;
  dns.begin(Ethernet.dnsServerIP());           // usa o DNS atual (estático ou DHCP)
  int rc = dns.getHostByName(host, outIP);     // 1 = sucesso
  return (rc == 1);
}

// ---------- Utils: EEPROM ----------
uint8_t calcChecksum(const NetConfig &c){
  const uint8_t *p = (const uint8_t*)&c;
  uint16_t s=0;
  for(size_t i=0;i<sizeof(NetConfig)-1;i++) s+=p[i];
  return (uint8_t)(s & 0xFF);
}
void loadDefaults(NetConfig &c){
  c.magic = CFG_MAGIC;
  c.use_static = DEFAULT_USE_STATIC_IP ? 1 : 0;
  for(int i=0;i<4;i++){
    c.ip[i]=DEFAULT_IP[i]; c.dns[i]=DEFAULT_DNS[i];
    c.gw[i]=DEFAULT_GW[i]; c.mask[i]=DEFAULT_MASK[i];
  }
  for(int i=0;i<6;i++) c.mac[i]=DEFAULT_MAC[i];
  c.checksum = calcChecksum(c);
}
bool loadConfig(NetConfig &c){
  EEPROM.get(EEPROM_ADDR, c);
  if(c.magic!=CFG_MAGIC) return false;
  if(c.checksum!=calcChecksum(c)) return false;
  return true;
}
void saveConfig(const NetConfig &c){ EEPROM.put(EEPROM_ADDR, c); }

// ---------- Utils: IP/MAC ----------
String getQueryParam(const String& q, const String& k){
  String pat=k+"="; int i=q.indexOf(pat); if(i<0) return "";
  int j=q.indexOf('&', i+pat.length()); if(j<0) j=q.length();
  String v=q.substring(i+pat.length(), j);
  v.replace("+"," "); return v;
}
bool parseIp(const String &s, uint8_t out[4]){
  int p1=s.indexOf('.'); int p2=s.indexOf('.',p1+1); int p3=s.indexOf('.',p2+1);
  if(p1<0||p2<0||p3<0) return false;
  long a=s.substring(0,p1).toInt(), b=s.substring(p1+1,p2).toInt();
  long c=s.substring(p2+1,p3).toInt(), d=s.substring(p3+1).toInt();
  if(a<0||a>255||b<0||b>255||c<0||c>255||d<0||d>255) return false;
  out[0]=a; out[1]=b; out[2]=c; out[3]=d; return true;
}
int hexVal(char c){
  if(c>='0'&&c<='9') return c-'0';
  c|=0x20; if(c>='a'&&c<='f') return 10+(c-'a'); return -1;
}
bool parseMac(const String& s, uint8_t out[6]){
  int n=0; int hi=-1,lo=-1; int i=0,len=s.length();
  while(i<len && n<6){
    while(i<len && (s[i]==':'||s[i]=='-'||s[i]==' ')) i++;
    if(i>=len) break;
    if(i<len){ hi=hexVal(s[i++]); } else return false;
    if(i<len){ lo=hexVal(s[i++]); } else return false;
    if(hi<0||lo<0) return false;
    out[n++] = (uint8_t)((hi<<4)|lo);
  }
  return n==6;
}
void macToString(const uint8_t mac[6], char* buf, size_t sz){
  snprintf(buf, sz, "%02X:%02X:%02X:%02X:%02X:%02X",
    mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
}

// --- Helpers: tamanho fixo em KB ---
String humanKB(unsigned long long bytes) {
  unsigned long kb = (unsigned long)((bytes + 1023ULL) / 1024ULL); // arredonda pra cima
  char buf[32];
  snprintf(buf, sizeof(buf), "%lu KB", kb);
  return String(buf);
}

// --- Helpers de tamanho legível ---
String humanSize(unsigned long long bytes) {
  char buf[32];
  if (bytes < 1024ULL) { snprintf(buf, sizeof(buf), "%llu B", bytes); return String(buf); }
  double kb = bytes / 1024.0;
  if (kb < 1024.0)     { snprintf(buf, sizeof(buf), "%.1f KB", kb); return String(buf); }
  double mb = kb / 1024.0;
  if (mb < 1024.0)     { snprintf(buf, sizeof(buf), "%.1f MB", mb); return String(buf); }
  double gb = mb / 1024.0;
  snprintf(buf, sizeof(buf), "%.2f GB", gb);
  return String(buf);
}

void printSdStatusHtml(EthernetClient &client) {
  client.println(F("<hr class='my-4'>"));
  if (!sdAvailable) {
    client.println(F(
      "<div class='d-flex align-items-center mb-2'>"
        "<h6 class='mb-0 me-2'>Armazenamento SD</h6>"
        "<span class='badge bg-danger'>Indispon&iacute;vel</span>"
      "</div>"
      "<div class='alert alert-warning mb-0'>Cart&atilde;o SD n&atilde;o detectado.</div>"
    ));
    return;
  }

  // Cabeçalho com status "Disponível"
  client.println(F(
    "<div class='d-flex align-items-center mb-2'>"
      "<h6 class='mb-0 me-2'>Armazenamento SD</h6>"
      "<span class='badge bg-success'>Dispon&iacute;vel</span>"
    "</div>"
  ));

  // Handoff SPI: garante SD ativo e Ethernet deselecionado
  digitalWrite(PIN_CS_ETH, HIGH);

  File root = SD.open("/");
  if (!root) {
    client.println(F("<div class='alert alert-danger mb-0'>Falha ao abrir a raiz do SD.</div>"));
    digitalWrite(PIN_CS_SD, HIGH);
    return;
  }

  unsigned long long totalBytes = 0;
  const int MAX_ROWS = 50;
  int shown = 0, totalFiles = 0;

  client.println(F(
    "<div class='table-responsive'>"
      "<table class='table table-sm align-middle mb-2'>"
        "<thead><tr><th>Arquivo</th><th style='width:160px' class='text-end'>Tamanho</th></tr></thead>"
        "<tbody>"
  ));

  File entry = root.openNextFile();
  while (entry) {
    if (!entry.isDirectory()) {
      unsigned long sz = entry.size();
      totalBytes += (unsigned long long)sz;
      totalFiles++;
      if (shown < MAX_ROWS) {
        client.print(F("<tr><td><code>"));
        client.print(entry.name());
        client.print(F("</code></td><td class='text-end'>"));
        client.print(humanKB(sz));            // mostrar em KB
        client.println(F("</td></tr>"));
        shown++;
      }
    }
    entry.close();
    entry = root.openNextFile();
  }
  root.close();

  if (totalFiles == 0) {
    client.println(F("<tr><td colspan='2' class='text-muted'>Sem arquivos no SD.</td></tr>"));
  }
  if (totalFiles > shown) {
    client.print(F("<tr><td colspan='2' class='text-muted'>(+"));
    client.print(totalFiles - shown);
    client.println(F(" restantes)</td></tr>"));
  }

  client.println(F("</tbody></table></div>"));

  // Resumo exatamente no formato pedido
  client.print(F("<div class='small text-muted'>Arquivos: "));
  client.print(totalFiles);
  client.print(F(" &middot; Ocupa&ccedil;&atilde;o total: "));
  client.print(humanKB(totalBytes));          // total em KB
  client.println(F("</div>"));

  // Libera CS do SD
  digitalWrite(PIN_CS_SD, HIGH);
}




// ---------- Rede ----------
void applyNetworkFromConfig(){
  // linhas de controle SPI
  pinMode(53, OUTPUT); digitalWrite(53, HIGH); // SS
  pinMode(PIN_CS_ETH, OUTPUT);
  pinMode(PIN_CS_SD,  OUTPUT);  digitalWrite(PIN_CS_SD, HIGH); // SD desabilitado por padrão

  // MAC
  uint8_t macLocal[6]; for(int i=0;i<6;i++) macLocal[i]=cfg.mac[i];

  if (cfg.use_static) {
    IPAddress ip  (cfg.ip[0],  cfg.ip[1],  cfg.ip[2],  cfg.ip[3]);
    IPAddress dns (cfg.dns[0], cfg.dns[1], cfg.dns[2], cfg.dns[3]);
    IPAddress gw  (cfg.gw[0],  cfg.gw[1],  cfg.gw[2],  cfg.gw[3]);
    IPAddress msk (cfg.mask[0],cfg.mask[1],cfg.mask[2],cfg.mask[3]);
    Ethernet.begin(macLocal, ip, dns, gw, msk);
  } else {
    if (Ethernet.begin(macLocal) == 0) {
      // Fallback solicitado
      Ethernet.begin(macLocal, DEFAULT_IP, DEFAULT_DNS, DEFAULT_GW, DEFAULT_MASK);
    }
  }
  delay(500);
  server.begin();

  // UDP para NTP
  Udp.begin(NTP_LOCAL_PORT);
}

// ---------- NTP ----------
unsigned long getEpochUTC(){
  // Caso não tenhamos sincronizado ainda, devolve estimativa por millis
  if (epochAtLastSync == 0) return 0UL;
  unsigned long secs = (millis() - msAtLastSync)/1000UL;
  return epochAtLastSync + secs;
}

void sendNTPpacket(IPAddress& address) {
  const int NTP_PACKET_SIZE = 48;
  byte packetBuffer[NTP_PACKET_SIZE];
  memset(packetBuffer, 0, NTP_PACKET_SIZE);
  packetBuffer[0] = 0b11100011; // LI, Version, Mode
  packetBuffer[1] = 0;   // Stratum
  packetBuffer[2] = 6;   // Poll
  packetBuffer[3] = 0xEC;// Precision
  Udp.beginPacket(address, 123);
  Udp.write(packetBuffer, NTP_PACKET_SIZE);
  Udp.endPacket();
}

void ntpSyncNow(){
  IPAddress timeServerIP;

  // tenta resolver via DNS; se falhar, você pode colocar um fallback de IP fixo
  if (!resolveHostname(NTP_SERVER, timeServerIP)) {
    // Fallback opcional (um IP do pool/servidor NTP)
    // timeServerIP = IPAddress(200,160,7,186); // exemplo
  }

  if (timeServerIP != IPAddress(0,0,0,0)) {
    sendNTPpacket(timeServerIP);
    delay(800);
    int size = Udp.parsePacket();
    if (size >= 48) {
      byte buf[48];
      Udp.read(buf, 48);
      unsigned long secsSince1900 = (unsigned long)buf[40] << 24 |
                                    (unsigned long)buf[41] << 16 |
                                    (unsigned long)buf[42] << 8  |
                                    (unsigned long)buf[43];
      const unsigned long seventyYears = 2208988800UL;
      unsigned long epochUTC = secsSince1900 - seventyYears;
      epochAtLastSync = epochUTC;
      msAtLastSync    = millis();
    }
  }

  lastNtpSyncMs = millis();
}

// Constrói nome 8.3 a partir de YYYYMM -> "L202509.CSV"
void buildMonthFilename(int yyyymm, char* out, size_t sz) {
  snprintf(out, sz, "L%06d.CSV", yyyymm);
}


// Pega YYYYMM (UTC) do epoch atual
int currentYYYYMM(unsigned long epochUTC){
  if (epochUTC == 0) return 0;
  // Conversão simples epoch->ano/mes (mesma lógica já usada em updateMonthFileName)
  unsigned long days = epochUTC / 86400UL;
  int y = 1970;
  unsigned long d = days;
  while (true) {
    bool leap = (y%4==0 && (y%100!=0 || y%400==0));
    unsigned long diy = leap ? 366 : 365;
    if (d >= diy) { d -= diy; y++; } else break;
  }
  int md[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
  bool leap = (y%4==0 && (y%100!=0 || y%400==0));
  if (leap) md[1]=29;
  int m=0;
  while (m<12 && d >= (unsigned long)md[m]) { d -= md[m]; m++; }
  return y*100 + (m+1); // YYYYMM
}


// ---------- LCD ----------
void printIPToSerial(){
  Serial.print(F("IP: ")); Serial.print(Ethernet.localIP());
  Serial.print(F(" | GW: ")); Serial.print(Ethernet.gatewayIP());
  Serial.print(F(" | DNS: ")); Serial.print(Ethernet.dnsServerIP());
  Serial.print(F(" | Mask: ")); Serial.println(Ethernet.subnetMask());
  Serial.println(F("HTTP na porta 8081"));
}
void printIPLine(){
  IPAddress ip = Ethernet.localIP();
  char line[17];
  snprintf(line,sizeof(line),"%u.%u.%u.%u",ip[0],ip[1],ip[2],ip[3]);
  lcd.setCursor(0,0); lcd.print("                ");
  lcd.setCursor(0,0); lcd.print(line);
}
void printTHLine(){
  char line[17]; char tbuf[8], hbuf[8];
  dtostrf(lastTemp,4,1,tbuf); dtostrf(lastHum,4,1,hbuf);
  snprintf(line,sizeof(line),"T:%sC H:%s%%",tbuf,hbuf);
  lcd.setCursor(0,1); lcd.print("                ");
  lcd.setCursor(0,1); lcd.print(line);
}

// ---------- Buzzer ----------
void beep(uint16_t f,uint16_t ms){ tone(PIN_BUZZER,f,ms); delay(ms+5); noTone(PIN_BUZZER); }
void startupChime(){ beep(1200,120); beep(1600,120); beep(2000,160); }

// Atualiza currentMonthFile a partir do epoch -> "LYYYYMM.CSV"
void updateMonthFileName(unsigned long epochUTC){
  if (epochUTC == 0) { strcpy(currentMonthFile, "LOG_BOOT.CSV"); return; }
  unsigned long days = epochUTC / 86400UL;
  int y = 1970; unsigned long d = days;
  while (true) {
    bool leap = (y%4==0 && (y%100!=0 || y%400==0));
    unsigned long diy = leap ? 366 : 365;
    if (d >= diy) { d -= diy; y++; } else break;
  }
  int md[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
  bool leap = (y%4==0 && (y%100!=0 || y%400==0));
  if (leap) md[1]=29;
  int m=0; while (m<12 && d >= (unsigned long)md[m]) { d -= md[m]; m++; }
  snprintf(currentMonthFile, sizeof(currentMonthFile), "L%04d%02d.CSV", y, m+1);
}


void sdInit(){
  // desabilita Ethernet durante SD.begin para evitar conflito
  digitalWrite(PIN_CS_ETH, HIGH);
  sdAvailable = SD.begin(PIN_CS_SD);
  if (!sdAvailable) {
    Serial.println(F("SD nao detectado. Historico desativado."));
  } else {
    Serial.println(F("SD OK. Historico ativado."));
  }
}

void sdAppendLog(float t, float h){
  if (!sdAvailable) return;
  if (sdBusy) return;                 // <-- não grava durante export

  // 1) Garante que o Ethernet NÃO está selecionado no SPI
  digitalWrite(PIN_CS_ETH, HIGH);   // desabilita W5100/W5500
  // (a lib SD controla PIN_CS_SD durante a operação)

  // 2) Escolhe nome de arquivo: se ainda sem NTP, usa LOG_BOOT.csv
  unsigned long epoch = getEpochUTC();
  char fname[20];
  if (epoch == 0) {
    // ainda sem hora válida: grava provisoriamente aqui
    strcpy(fname, "LOG_BOOT.csv");
  } else {
    updateMonthFileName(epoch);
    strncpy(fname, currentMonthFile, sizeof(fname));
    fname[sizeof(fname)-1] = '\0';
  }

  // 3) Abre para append
  bool newFile = !SD.exists(fname);
  File f = SD.open(fname, FILE_WRITE);
  if (!f) {
    // debug útil para ver no Serial
    Serial.print(F("[SD] Falha ao abrir ")); Serial.println(fname);
    digitalWrite(PIN_CS_SD, HIGH);   // garante SD solto
    return;
  }

  // 4) (Opcional) escreve cabeçalho se arquivo novo
  if (newFile) {
    f.println(F("epoch,temperature,humidity"));
  }

  // 5) Escreve linha CSV
  f.print(epoch); f.print(',');
  f.print(t, 2);  f.print(',');
  f.println(h, 2);
  f.flush();
  f.close();

  // 6) Libera SD (boa prática quando voltar a usar Ethernet)
  digitalWrite(PIN_CS_SD, HIGH);

  // 7) Debug
  Serial.print(F("[SD] Append ")); Serial.print(fname);
  Serial.print(F(" -> ")); Serial.print(t,2);
  Serial.print(F("C, ")); Serial.print(h,2);
  Serial.println(F("%"));
}


// Itera linhas do CSV e manda JSON filtrado
void streamCsvAsJson(EthernetClient &client, unsigned long minEpoch, unsigned long maxEpoch,
                     int yearMonth /* yyyymm or -1 for current+prev hours */) {
  if (!sdAvailable) { client.println(F("[]")); return; }

  // ---- SPI handoff: desabilita Ethernet p/ operar SD ----
  digitalWrite(PIN_CS_ETH, HIGH);           // Ethernet inativo
  // (SD.begin já configurou PIN_CS_SD; a lib SD controla o CS, mas garantimos no fim)

  bool first = true;
  client.print('[');

  auto streamFile = [&](const char* fname){
    File f = SD.open(fname, FILE_READ);
    if (!f) return;
    String line;
    while (f.available()) {
      char c = f.read();
      if (c=='\n' || c=='\r') {
        if (line.length()>0) {
          int p1 = line.indexOf(',');
          int p2 = line.indexOf(',', p1+1);
          if (p1>0 && p2>p1) {
            unsigned long e = line.substring(0,p1).toInt();
            if ((minEpoch==0 || e>=minEpoch) && (maxEpoch==0 || e<=maxEpoch)) {
              String st = line.substring(p1+1, p2);
              String sh = line.substring(p2+1);
              if (!first) client.print(',');
              client.print(F("{\"epoch\":")); client.print(e);
              client.print(F(",\"temperature\":")); client.print(st);
              client.print(F(",\"humidity\":")); client.print(sh);
              client.print('}');
              first=false;
            }
          }
          line = "";
        }
      } else {
        line += c;
      }
    }
    f.close();
  };

  if (yearMonth > 0) {
    char fname[20];
    // antes: "LOG_%06d.csv"
    snprintf(fname, sizeof(fname), "L%06d.CSV", yearMonth);

    streamFile(fname);
  } else {
    unsigned long nowEpoch = getEpochUTC();
    updateMonthFileName(nowEpoch);
    char cur[20]; strcpy(cur, currentMonthFile);

    unsigned long prevEpoch = nowEpoch - 31UL*86400UL;
    updateMonthFileName(prevEpoch);
    char prev[20]; strcpy(prev, currentMonthFile);

    updateMonthFileName(nowEpoch);

    streamFile(prev);
    streamFile(cur);
  }

  client.println(']');

  // ---- SPI handoff: reabilita caminho do Ethernet ----
  digitalWrite(PIN_CS_SD, HIGH);           // SD inativo
  // (Ethernet lib vai baixar PIN_CS_ETH quando precisar)
}


// ---------- Páginas ----------
void sendHtmlHeader(EthernetClient &c){
  c.println(F("HTTP/1.1 200 OK"));
  c.println(F("Content-Type: text/html; charset=utf-8"));
  c.println(F("Connection: close"));
  c.println();
}
void sendJsonHeader(EthernetClient &c){
  c.println(F("HTTP/1.1 200 OK"));
  c.println(F("Content-Type: application/json; charset=utf-8"));
  c.println(F("Access-Control-Allow-Origin: *"));
  c.println(F("Connection: close"));
  c.println();
}
void sendNotFound(EthernetClient &c){
  c.println(F("HTTP/1.1 404 Not Found"));
  c.println(F("Content-Type: text/plain; charset=utf-8"));
  c.println(F("Connection: close"));
  c.println(); c.println(F("404"));
}


void handleRootPage(EthernetClient &client, const String &query){
  bool changed=false; NetConfig newCfg=cfg;

  // ---- Parse de parâmetros (salva em EEPROM e reconfigura rede) ----
  if(query.length()>0){
    String v_mode  = getQueryParam(query,"mode"); // dhcp | static
    String v_stat  = getQueryParam(query,"use_static");
    if (v_mode.length()){
      if (v_mode=="dhcp")  { newCfg.use_static=0; changed=true; }
      if (v_mode=="static"){ newCfg.use_static=1; changed=true; }
    } else if (v_stat.length()){
      newCfg.use_static = (uint8_t)(v_stat.toInt()?1:0); changed=true;
    }
    String v_ip   = getQueryParam(query,"ip");
    String v_dns  = getQueryParam(query,"dns");
    String v_gw   = getQueryParam(query,"gw");
    String v_mask = getQueryParam(query,"mask");
    uint8_t tmp4[4];
    if(v_ip.length()   && parseIp(v_ip,tmp4))   { memcpy(newCfg.ip,tmp4,4);   changed=true; }
    if(v_dns.length()  && parseIp(v_dns,tmp4))  { memcpy(newCfg.dns,tmp4,4);  changed=true; }
    if(v_gw.length()   && parseIp(v_gw,tmp4))   { memcpy(newCfg.gw,tmp4,4);   changed=true; }
    if(v_mask.length() && parseIp(v_mask,tmp4)) { memcpy(newCfg.mask,tmp4,4); changed=true; }
    String v_mac = getQueryParam(query,"mac");
    uint8_t tmp6[6];
    if(v_mac.length() && parseMac(v_mac,tmp6)) { memcpy(newCfg.mac,tmp6,6); changed=true; }

    if(changed){
      newCfg.magic=CFG_MAGIC; newCfg.checksum=calcChecksum(newCfg);
      cfg=newCfg; saveConfig(cfg);
      applyNetworkFromConfig(); // reconfig net
    }
  }

  // ---- HTML HEAD ----
  sendHtmlHeader(client);
  client.println(F(
    "<!doctype html><html lang='pt-br'><head><meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width, initial-scale=1'>"
    "<title>Monitor de Temperatura</title>"
    "<link href='https://cdn.jsdelivr.net/npm/bootstrap@5.3.3/dist/css/bootstrap.min.css' rel='stylesheet'>"
    "<style>"
      "body{background:#f6f7fb}"
      ".navbar{background:#0d6efd}.navbar-brand{color:#fff!important;font-weight:600}"
      ".sidebar{min-height:100vh;background:#fff;border-right:1px solid #e5e7eb}"
      ".sidebar .nav-link{color:#0d6efd;font-weight:500}"
      ".sidebar .nav-link.active{background:#e7f1ff;border-radius:.5rem}"
      ".card{border-radius:.75rem}"
    "</style></head><body>"
  ));

  // ---- NAVBAR ----
  client.println(F(
    "<nav class='navbar navbar-expand-lg'><div class='container-fluid'>"
      "<a class='navbar-brand' href='#'>Equipamento de Monitoramento</a>"
    "</div></nav>"
    "<div class='container-fluid'><div class='row'>"
  ));

  // ---- SIDEBAR (uma única vez) ----
  client.println(F(
    "<aside class='col-12 col-md-3 col-lg-2 p-3 sidebar'>"
      "<ul class='nav nav-pills flex-column'>"
        "<li class='nav-item'><a class='nav-link active' href='/?#status'>Status</a></li>"
        "<li class='nav-item'><a class='nav-link' href='/?#rede'>Rede</a></li>"
        "<li class='nav-item'><a class='nav-link' href='/historico'>Hist&oacute;rico</a></li>"
        "<li class='nav-item'><a class='nav-link' href='/export'>Exportar</a></li>"
        "<li class='nav-item'><a class='nav-link' href='/ws/temperatura' target='_blank'>JSON</a></li>"
      "</ul>"
    "</aside>"
  ));

  // ---- MAIN ----
  client.println(F("<main class='col-12 col-md-9 col-lg-10 p-4'>"));

  // ===== Status =====
  client.println(F(
    "<section id='status' class='mb-4'>"
      "<div class='card shadow-sm'><div class='card-body'>"
        "<h5 class='card-title mb-3'>Status</h5>"
        "<div class='row g-3'>"
          "<div class='col-12 col-md-6'>"
            "<div class='p-3 bg-light rounded'>"
              "<div class='text-muted small'>Temperatura</div>"
              "<div class='display-6' id='temp_val'>-- &deg;C</div>"
            "</div>"
          "</div>"
          "<div class='col-12 col-md-6'>"
            "<div class='p-3 bg-light rounded'>"
              "<div class='text-muted small'>Umidade</div>"
              "<div class='display-6' id='hum_val'>-- %</div>"
            "</div>"
          "</div>"
        "</div>"
        "<div class='mt-3 text-muted'>JSON em <code>/ws/temperatura</code>.</div>"
      "</div></div>"
    "</section>"
  ));

  // ---- Bloco: Armazenamento SD (logo após Status) ----
  printSdStatusHtml(client);

  // ===== Rede =====
  char macStr[18]; macToString(cfg.mac, macStr, sizeof(macStr));
  client.println(F("<section id='rede' class='mt-4'><div class='card shadow-sm'><div class='card-body'>"
    "<h5 class='card-title mb-3'>Configura&ccedil;&atilde;o de Rede</h5>"
    "<form method='GET' action='/'>"));

  client.println(F(
    "<div class='mb-3'>"
      "<label class='form-label'>Modo de Endere&ccedil;amento</label>"
      "<div class='form-check'>"
        "<input class='form-check-input' type='radio' name='mode' id='mode_dhcp' value='dhcp'>"
        "<label class='form-check-label' for='mode_dhcp'>DHCP (autom&aacute;tico)</label>"
      "</div>"
      "<div class='form-check'>"
        "<input class='form-check-input' type='radio' name='mode' id='mode_static' value='static'>"
        "<label class='form-check-label' for='mode_static'>Est&aacute;tico (manual)</label>"
      "</div>"
    "</div>"
  ));

  client.print(F("<div class='mb-3'><label class='form-label'>MAC (AA:BB:CC:DD:EE:FF)</label>"
                 "<input class='form-control' name='mac' value='"));
  client.print(macStr);
  client.println(F("'></div>"));

  auto ipField=[&](const char* label,const char* name,const uint8_t a[4]){
    client.print(F("<div class='mb-3 ipset'><label class='form-label'>"));
    client.print(label);
    client.print(F("</label><input class='form-control' name='"));
    client.print(name);
    client.print(F("' value='"));
    client.print(a[0]); client.print('.');
    client.print(a[1]); client.print('.');
    client.print(a[2]); client.print('.');
    client.print(a[3]);
    client.println(F("'></div>"));
  };
  ipField("IP",     "ip",   cfg.ip);
  ipField("DNS",    "dns",  cfg.dns);
  ipField("Gateway","gw",   cfg.gw);
  ipField("Mask",   "mask", cfg.mask);

  client.println(F(
    "<button class='btn btn-primary' type='submit'>Salvar & Aplicar</button>"
    "</form>"
    "<div class='mt-3 text-muted small'>Se DHCP estiver ativo e falhar, o equipamento usar&aacute; 172.17.240.253 automaticamente.</div>"
    "</div></div></section>"
  ));

  // Rodapé
  client.print(F("<div class='text-muted mt-4'>Rodando em "));
  IPAddress ip = Ethernet.localIP(); client.print(ip);
  client.println(F(":8081</div>"));

  // Fecha MAIN + LAYOUT e injeta scripts
  client.println(F(
    "</main></div></div>"
    "<script src='https://cdn.jsdelivr.net/npm/bootstrap@5.3.3/dist/js/bootstrap.bundle.min.js'></script>"
    "<script>"
      "const isDhcp = "
  ));
  client.print(cfg.use_static ? F("false") : F("true"));
  client.println(F(";"
      "document.getElementById('mode_dhcp').checked = isDhcp;"
      "document.getElementById('mode_static').checked = !isDhcp;"
      "function toggleFields(){const dh=document.getElementById('mode_dhcp').checked;"
        "document.querySelectorAll('.ipset input').forEach(el=>el.disabled=dh);}"
      "document.getElementById('mode_dhcp').addEventListener('change',toggleFields);"
      "document.getElementById('mode_static').addEventListener('change',toggleFields);"
      "toggleFields();"
      "async function refreshTH(){try{const r=await fetch('/ws/temperatura',{cache:'no-store'});"
        "if(!r.ok)return;const j=await r.json();"
        "document.getElementById('temp_val').innerHTML=(j.temperature==null?'--':j.temperature.toFixed(1))+' &deg;C';"
        "document.getElementById('hum_val').innerHTML=(j.humidity==null?'--':j.humidity.toFixed(1))+' %';}catch(e){}}"
      "refreshTH();setInterval(refreshTH,3000);"
    "</script></body></html>"
  ));
}



void handleHistoricoPage(EthernetClient &client){
  sendHtmlHeader(client);
  client.println(F(
    "<!doctype html><html lang='pt-br'><head><meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width, initial-scale=1'>"
    "<title>Hist&oacute;rico</title>"
    "<link href='https://cdn.jsdelivr.net/npm/bootstrap@5.3.3/dist/css/bootstrap.min.css' rel='stylesheet'>"
    "<script src='https://cdn.jsdelivr.net/npm/chart.js@4.4.1'></script>"
    "<style>"
      "body{background:#f6f7fb}"
      ".navbar{background:#0d6efd}.navbar-brand{color:#fff!important;font-weight:600}"
      ".sidebar{min-height:100vh;background:#fff;border-right:1px solid #e5e7eb}"
      ".sidebar .nav-link{color:#0d6efd;font-weight:500}"
      ".sidebar .nav-link.active{background:#e7f1ff;border-radius:.5rem}"
      ".card{border-radius:.75rem}"
      ".graph-box{height:400px;}"
    "</style></head><body>"));

  // Navbar
  client.println(F(
    "<nav class='navbar navbar-expand-lg'><div class='container-fluid'>"
      "<a class='navbar-brand' href='#'>Equipamento de Monitoramento</a>"
    "</div></nav>"
    "<div class='container-fluid'><div class='row'>"));

  // Sidebar (mesma do Status)
  client.println(F(
    "<aside class='col-12 col-md-3 col-lg-2 p-3 sidebar'>"
      "<ul class='nav nav-pills flex-column'>"
        "<li class='nav-item'><a class='nav-link' href='/?#status'>Status</a></li>"
        "<li class='nav-item'><a class='nav-link' href='/?#rede'>Rede</a></li>"
        "<li class='nav-item'><a class='nav-link active' href='/historico'>Hist&oacute;rico</a></li>"
        "<li class='nav-item'><a class='nav-link' href='/export'>Exportar</a></li>"
        "<li class='nav-item'><a class='nav-link' href='/ws/temperatura' target='_blank'>JSON</a></li>"
      "</ul>"
    "</aside>"));

  // Main
  client.println(F("<main class='col-12 col-md-9 col-lg-10 p-4'>"
    "<h3 class='mb-3'>Hist&oacute;rico</h3>"));

  if (!sdAvailable) {
    client.println(F(
      "<div class='alert alert-warning'>Cart&atilde;o SD n&atilde;o detectado. "
      "Os gr&aacute;ficos ser&atilde;o exibidos vazios at&eacute; que o SD seja inserido e o log seja gravado.</div>"
    ));
  }

  client.println(F(
    "<div class='row g-3 mb-3'>"
      "<div class='col-12 col-md-3'><input id='yyyymm' class='form-control' placeholder='YYYY-MM'></div>"
      "<div class='col-12 col-md-3'><button id='btnMes' class='btn btn-primary w-100'>Carregar M&ecirc;s</button></div>"
    "</div>"

    "<div class='card mb-3'><div class='card-body'>"
      "<h5>Temperatura x Tempo (24h)</h5>"
      "<div class='graph-box'><canvas id='chartT'></canvas></div>"
    "</div></div>"

    "<div class='card'><div class='card-body'>"
      "<h5>Umidade x Tempo (24h)</h5>"
      "<div class='graph-box'><canvas id='chartH'></canvas></div>"
    "</div></div>"

    "<p class='text-muted mt-3'>Hor&aacute;rio exibido em <code>America/Sao_Paulo</code>.</p>"
    "</main></div></div>" // fecha main/row/container
  ));

  // Scripts
  client.println(F(
    "<script src='https://cdn.jsdelivr.net/npm/bootstrap@5.3.3/dist/js/bootstrap.bundle.min.js'></script>"
    "<script>"
    "let tz='America/Sao_Paulo';"
    "const fmt = ts => new Date(ts*1000).toLocaleString('pt-BR',{timeZone:tz});"
    "const toSeries = arr => ({labels: arr.map(x=>fmt(x.epoch)), t: arr.map(x=>x.temperature), h: arr.map(x=>x.humidity)});"

    "const chartOptions={"
      "responsive:true,maintainAspectRatio:false,"
      "interaction:{mode:'index',intersect:false},"
      "scales:{x:{type:'category'}, y:{beginAtZero:false}},"
      "elements:{point:{radius:2}},plugins:{legend:{display:true}}"
    "};"

    "const gT=new Chart(document.getElementById('chartT'),{type:'line',"
      "data:{labels:[],datasets:[{label:'Temperatura (°C)',borderColor:'#dc3545',backgroundColor:'rgba(220,53,69,0.2)',data:[]}]},"
      "options:chartOptions});"

    "const gH=new Chart(document.getElementById('chartH'),{type:'line',"
      "data:{labels:[],datasets:[{label:'Umidade (%RH)',borderColor:'#0d6efd',backgroundColor:'rgba(13,110,253,0.2)',data:[]}]},"
      "options:chartOptions});"

    "async function load24h(){"
      "try{const r=await fetch('/ws/log?hours=24',{cache:'no-store'}); if(!r.ok)return;"
          "const j=await r.json(); const d=toSeries(j);"
          "gT.data.labels=d.labels; gT.data.datasets[0].data=d.t; gT.update();"
          "gH.data.labels=d.labels; gH.data.datasets[0].data=d.h; gH.update();"
      "}catch(e){}}"

    "document.getElementById('btnMes').addEventListener('click',async()=>{"
      "const y=document.getElementById('yyyymm').value.trim();"
      "if(!/^\\d{4}-\\d{2}$/.test(y)){alert('Use YYYY-MM');return;}"
      "const ym=y.replace('-','');"
      "try{const r=await fetch('/ws/log?yyyymm='+ym,{cache:'no-store'});"
          "if(!r.ok){alert('Sem dados para o m&ecirc;s.');return;}"
          "const j=await r.json(); const d=toSeries(j);"
          "gT.data.labels=d.labels; gT.data.datasets[0].data=d.t; gT.update();"
          "gH.data.labels=d.labels; gH.data.datasets[0].data=d.h; gH.update();"
      "}catch(e){}});"

    "load24h();"
    "</script></body></html>"
  ));
}


void handleExportPage(EthernetClient &client){
  sendHtmlHeader(client);
  client.println(F(
    "<!doctype html><html lang='pt-br'><head><meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width, initial-scale=1'>"
    "<title>Exportar / Limpar</title>"
    "<link href='https://cdn.jsdelivr.net/npm/bootstrap@5.3.3/dist/css/bootstrap.min.css' rel='stylesheet'>"
    "<style>"
      "body{background:#f6f7fb}"
      ".navbar{background:#0d6efd}.navbar-brand{color:#fff!important;font-weight:600}"
      ".sidebar{min-height:100vh;background:#fff;border-right:1px solid #e5e7eb}"
      ".sidebar .nav-link{color:#0d6efd;font-weight:500}"
      ".sidebar .nav-link.active{background:#e7f1ff;border-radius:.5rem}"
      ".card{border-radius:.75rem}"
    "</style></head><body>"));

  // Navbar
  client.println(F(
    "<nav class='navbar navbar-expand-lg'><div class='container-fluid'>"
      "<a class='navbar-brand' href='#'>Equipamento de Monitoramento</a>"
    "</div></nav>"
    "<div class='container-fluid'><div class='row'>"));

  // Sidebar (igual ao Status)
  client.println(F(
    "<aside class='col-12 col-md-3 col-lg-2 p-3 sidebar'>"
      "<ul class='nav nav-pills flex-column'>"
        "<li class='nav-item'><a class='nav-link' href='/?#status'>Status</a></li>"
        "<li class='nav-item'><a class='nav-link' href='/?#rede'>Rede</a></li>"
        "<li class='nav-item'><a class='nav-link' href='/historico'>Hist&oacute;rico</a></li>"
        "<li class='nav-item'><a class='nav-link active' href='/export'>Exportar</a></li>"
        "<li class='nav-item'><a class='nav-link' href='/ws/temperatura' target='_blank'>JSON</a></li>"
      "</ul>"
    "</aside>"));

  // Main
  client.println(F("<main class='col-12 col-md-9 col-lg-10 p-4'>"
    "<h3 class='mb-3'>Exportar / Limpar SD</h3>"));

  if (!sdAvailable) {
    client.println(F("<div class='alert alert-warning'>Cart&atilde;o SD n&atilde;o detectado. "
                     "Exportar e Limpar n&atilde;o est&atilde;o dispon&iacute;veis.</div>"));
  }

  client.println(F("<p class='text-muted'>Os arquivos seguem o padr&atilde;o <code>LYYYYMM.CSV</code> (por m&ecirc;s).</p>"));

  client.println(F(
    "<div class='card'><div class='card-body'>"
      "<div class='row g-3 align-items-end'>"
        "<div class='col-12 col-md-4'>"
          "<label class='form-label'>Ano e m&ecirc;s (YYYY-MM)</label>"
          "<input id='ym' type='month' class='form-control'>"
          "<div class='form-text'>O arquivo alvo ser&aacute; <code>LYYYYMM.CSV</code>.</div>"
        "</div>"
        "<div class='col-12 col-md-3'>"
          "<a id='btnDown' class='btn btn-success w-100'>Baixar CSV</a>"
        "</div>"
        "<div class='col-12 col-md-3'>"
          "<a id='btnClr' class='btn btn-danger w-100'>Limpar SD (m&ecirc;s)</a>"
        "</div>"
        "<div class='col-12 col-md-2'>"
          "<a class='btn btn-secondary w-100' href='/historico'>Hist&oacute;rico</a>"
        "</div>"
      "</div>"
      "<div class='mt-3 small text-muted'>Arquivo calculado: <code id='fnPreview'>—</code></div>"
    "</div></div>"));

  client.println(F(
    "</main></div></div>"
    "<script src='https://cdn.jsdelivr.net/npm/bootstrap@5.3.3/dist/js/bootstrap.bundle.min.js'></script>"
    "<script>"
      "const sdOK = ")); client.print(sdAvailable ? F("true") : F("false")); client.println(F(";"

      // Preenche com o mês atual (YYYY-MM) no input type=month
      "function ymNow(){const d=new Date();const m=('0'+(d.getMonth()+1)).slice(-2);return d.getFullYear()+'-'+m;}"
      "const inp=document.getElementById('ym');"
      "inp.value = ymNow();"

      // Converte YYYY-MM -> yyyymm e mostra prévia LYYYYMM.CSV
      "function yyyymm(){"
        "const v=inp.value.trim(); if(!/^\\d{4}-\\d{2}$/.test(v)) return null;"
        "return v.replace('-','');"
      "}"
      "function updatePreview(){"
        "const ym=yyyymm();"
        "document.getElementById('fnPreview').textContent = ym?('L'+ym+'.CSV'):'—';"
      "}"
      "updatePreview();"
      "inp.addEventListener('change',updatePreview);"

      // Botões
      "document.getElementById('btnDown').addEventListener('click',()=>{"
        "if(!sdOK){alert('SD indispon&iacute;vel');return;}"
        "const ym=yyyymm(); if(!ym){alert('Informe YYYY-MM.');return;}"
        "window.location.href = '/ws/csv?yyyymm='+ym;"
      "});"
      "document.getElementById('btnClr').addEventListener('click',()=>{"
        "if(!sdOK){alert('SD indispon&iacute;vel');return;}"
        "const ym=yyyymm(); if(!ym){alert('Informe YYYY-MM.');return;}"
        "if(!confirm('Apagar o arquivo L'+ym+'.CSV?')) return;"
        "window.location.href = '/ws/clear?yyyymm='+ym;"
      "});"
    "</script></body></html>"
  ));
}


void handleCsvDownload(EthernetClient &client, const String &query) {
  // 1) Verifica SD
  if (!sdAvailable) {
    sendHtmlHeader(client);
    client.println(F("<!doctype html><html><head><meta charset='utf-8'><title>Exportar CSV</title></head><body>"));
    client.println(F("<p style='padding:1rem'>SD n&atilde;o dispon&iacute;vel.</p></body></html>"));
    return;
  }

  // 2) Resolve nome do arquivo (8.3): "LYYYYMM.CSV"
  String v_ym = getQueryParam(query, "yyyymm");
  char fname[20];

  if (v_ym.length() == 6) {
    int yyyymm = v_ym.toInt();               // ex: 202509
    buildMonthFilename(yyyymm, fname, sizeof(fname));  // -> "L202509.CSV"
  } else {
    int yyyymm = currentYYYYMM(getEpochUTC());
    if (yyyymm == 0) {
      // Sem epoch válido ainda (NTP não sincronizado)
      sendHtmlHeader(client);
      client.println(F("<!doctype html><html><head><meta charset='utf-8'><title>Exportar CSV</title></head><body>"));
      client.println(F("<p style='padding:1rem'>Sem data/hora v&aacute;lida (NTP). N&atilde;o h&aacute; m&ecirc;s atual para exportar.</p></body></html>"));
      return;
    }
    buildMonthFilename(yyyymm, fname, sizeof(fname));
  }

  // 3) Se não existir, informa de forma amigável (não 404)
  if (!SD.exists(fname)) {
    sendHtmlHeader(client);
    client.print(F("<!doctype html><html><head><meta charset='utf-8'><title>Exportar CSV</title></head><body>"));
    client.print(F("<p style='padding:1rem'>Arquivo inexistente para o per&iacute;odo solicitado: <code>"));
    client.print(fname);
    client.println(F("</code></p></body></html>"));
    return;
  }

  // 4) Bloqueia escrita no log durante export e faz handoff do SPI
  sdBusy = true;                  // impede sdAppendLog
  digitalWrite(PIN_CS_ETH, HIGH); // garante W5100/W5500 dessel.

  File f = SD.open(fname, FILE_READ);
  if (!f) {
    sdBusy = false;
    digitalWrite(PIN_CS_SD, HIGH);
    sendHtmlHeader(client);
    client.println(F("<!doctype html><html><head><meta charset='utf-8'><title>Exportar CSV</title></head><body>"));
    client.println(F("<p style='padding:1rem'>Falha ao abrir o arquivo no SD.</p></body></html>"));
    return;
  }

  // 5) Cabeçalhos de download
  client.println(F("HTTP/1.1 200 OK"));
  client.println(F("Content-Type: text/csv; charset=utf-8"));
  client.print (F("Content-Disposition: attachment; filename=\""));
  client.print(fname);
  client.println(F("\""));
  client.println(F("Connection: close"));
  client.println();

  // 6) Stream do arquivo
  const size_t BUFSZ = 512;
  uint8_t buf[BUFSZ];
  while (f.available()) {
    int n = f.read(buf, BUFSZ);
    if (n > 0) client.write(buf, n);
  }
  f.close();

  // 7) Libera SPI e destrava SD
  digitalWrite(PIN_CS_SD, HIGH);
  sdBusy = false;
}



void handleCsvClear(EthernetClient &client, const String &query){
  if (!sdAvailable) { sendHtmlHeader(client); client.println(F("<p style='padding:1rem'>SD nao disponivel.</p>")); return; }

  String v_ym = getQueryParam(query, "yyyymm");
  char fname[20];
  if (v_ym.length() == 6) {
    int yyyymm = v_ym.toInt();
    buildMonthFilename(yyyymm, fname, sizeof(fname));
  } else {
    int yyyymm = currentYYYYMM(getEpochUTC());
    if (yyyymm == 0) { sendNotFound(client); return; }
    buildMonthFilename(yyyymm, fname, sizeof(fname));
  }

  sdBusy = true;
  digitalWrite(PIN_CS_ETH, HIGH);

  bool ok = SD.exists(fname) ? SD.remove(fname) : false;

  digitalWrite(PIN_CS_SD, HIGH);
  sdBusy = false;

  sendHtmlHeader(client);
  client.println(F("<!doctype html><html><head><meta charset='utf-8'><title>Limpar SD</title>"
                   "<link href='https://cdn.jsdelivr.net/npm/bootstrap@5.3.3/dist/css/bootstrap.min.css' rel='stylesheet'></head><body class='p-3'><div class='container'>"));
  client.print(F("<h4>Limpar SD - <code>")); client.print(fname); client.println(F("</code></h4>"));
  if (ok) client.println(F("<div class='alert alert-success'>Arquivo removido.</div>"));
  else    client.println(F("<div class='alert alert-warning'>Arquivo n&atilde;o existia.</div>"));
  client.println(F("<a class='btn btn-secondary' href='/export'>Voltar</a></div></body></html>"));
}



void handleJsonNow(EthernetClient &client){
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

void handleJsonLog(EthernetClient &client, const String &query){
  if (!sdAvailable) { sendNotFound(client); return; }
  sendJsonHeader(client);

  if (!sdAvailable) {                     // <-- sem SD, devolve array vazio
    client.println(F("[]"));
    return;
  }

  String v_hours = getQueryParam(query, "hours");
  String v_ym    = getQueryParam(query, "yyyymm");

  if (v_hours.length()) {
    unsigned long hrs = (unsigned long)v_hours.toInt();
    if (hrs == 0) hrs = 24;
    unsigned long nowEpoch = getEpochUTC();
    unsigned long minEpoch = (nowEpoch>hrs*3600UL) ? nowEpoch - hrs*3600UL : 0;
    streamCsvAsJson(client, minEpoch, 0, -1);
    return;
  }
  if (v_ym.length() == 6) {
    int yyyymm = v_ym.toInt(); // ex 202509
    streamCsvAsJson(client, 0, 0, yyyymm);
    return;
  }
  // default: 24h
  unsigned long nowEpoch = getEpochUTC();
  unsigned long minEpoch = (nowEpoch>24UL*3600UL) ? nowEpoch - 24UL*3600UL : 0;
  streamCsvAsJson(client, minEpoch, 0, -1);
}

// ---------- HTTP routing ----------
void handleHttp(EthernetClient &client){
  unsigned long t0=millis();
  while(client.connected() && !client.available() && millis()-t0<1000) {}
  if(!client.available()) return;

  String reqLine = client.readStringUntil('\r'); client.read(); // \n
  while(client.connected()){
    String h = client.readStringUntil('\n');
    if(h=="\r" || h.length()==1) break;
  }
  int sp1=reqLine.indexOf(' '), sp2=reqLine.indexOf(' ',sp1+1);
  String url=reqLine.substring(sp1+1,sp2);
  String path=url, query=""; int q=url.indexOf('?');
  if(q>=0){ path=url.substring(0,q); query=url.substring(q+1); }

  if(path=="/" || path=="/index.html") handleRootPage(client, query);
  else if(path=="/ws/temperatura")     handleJsonNow(client);
  else if(path=="/ws/log")             handleJsonLog(client, query);
  else if(path=="/historico")          handleHistoricoPage(client);
  else if(path=="/export")             handleExportPage(client);           // <-- NOVO
  else if(path=="/ws/csv")             handleCsvDownload(client, query);   // <-- NOVO
  else if(path=="/ws/clear")           handleCsvClear(client, query);      // <-- NOVO
  else sendNotFound(client);

}

// ======== Setup/Loop ========
void setup(){
  Serial.begin(115200);

  // Buzzer/LED/DHT
  pinMode(PIN_BUZZ_G, OUTPUT); digitalWrite(PIN_BUZZ_G, LOW);
  pinMode(PIN_BUZZER, OUTPUT); noTone(PIN_BUZZER);
  pinMode(PIN_LED, OUTPUT); digitalWrite(PIN_LED, LOW);
  pinMode(DHTPIN, INPUT_PULLUP);
  dht.begin(); dhtWarmupUntil = millis() + 2000;

  // LCD
  lcd.init(); lcd.backlight(); lcd.clear();
  lcd.setCursor(0,0); lcd.print(F("Inicializando..."));

  // Config
  if(!loadConfig(cfg)){ loadDefaults(cfg); saveConfig(cfg); }

  // Rede
  applyNetworkFromConfig();
  Serial.println(F("Servidor HTTP iniciado")); printIPToSerial();

  // UDP/NTP inicial
  ntpSyncNow();

  // SD
  sdInit();

  // Splash IP
  lcd.clear(); printIPLine();
  lcd.setCursor(0,1); lcd.print("                ");
  ipSplashStartMs = millis(); ipSplashDone=false;

  // Som inicial
  startupChime();
}

void sampleSensorIfNeeded(){
  unsigned long now=millis();
  if(now<dhtWarmupUntil) return;
  if(now - lastSampleMs < sampleIntervalMs) return;
  lastSampleMs = now;

  digitalWrite(PIN_LED, HIGH);

  float t=dht.readTemperature(), h=dht.readHumidity();
  if(!isnan(t) && !isnan(h)){
    lastTemp=t; lastHum=h;
    if(wasBelowThreshold && lastTemp>=THRESH_C){ beep(1800,180); wasBelowThreshold=false; }
    else if(lastTemp<THRESH_C){ wasBelowThreshold=true; }

    if(sdAvailable){
      sdAppendLog(lastTemp, lastHum);
    }
    if(ipSplashDone){ printIPLine(); printTHLine(); }
  }
  delay(60);
  digitalWrite(PIN_LED, LOW);
}

void loop(){
  // splash
  if(!ipSplashDone && (millis()-ipSplashStartMs>=IP_SPLASH_MS)){
    ipSplashDone=true; printIPLine();
    if(!isnan(lastTemp) && !isnan(lastHum)) printTHLine();
    else { lcd.setCursor(0,1); lcd.print("Aguardando DHT "); }
  }

  // NTP re-sync
  if (millis() - lastNtpSyncMs > NTP_INTERVAL_MS) ntpSyncNow();

  sampleSensorIfNeeded();

  // HTTP
  EthernetClient client = server.available();
  if(client){ handleHttp(client); delay(1); client.stop(); }
}
