/*
  Firmware "mega temp02" - Monitor T/H c/ LCD, SD, HTTP, NTP
  Serial Verbose: imprime tudo que importa pra diagnosticar.

  ► Principais endpoints:
    /                 -> Dashboard (status/rede)
    /historico        -> Gráficos (Chart.js) por mês
    /export           -> Download CSV (por mês) e limpar arquivo do mês
    /ws/temperatura   -> JSON com leitura atual
    /ws/log           -> JSON histórico (param: yyyymm=YYYYMM | hours=24)
    /ws/csv           -> DOWNLOAD CSV (param: yyyymm=YYYYMM) [se usar handleCsvDownload]
    /ws/clear         -> Remove CSV do mês (param: yyyymm=YYYYMM)
    /calibracao       -> Página de calibração (direta e 2 pontos)
    /ws/calib         -> API calibração

  ► Logs na Serial (115200) com prefixos:
    [CFG], [NET], [NTP], [SD], [HTTP], [LOG], [DHT], [CAL], [LCD]
*/

#define SERIAL_VERBOSE 1


#include <Wire.h>
#include <SPI.h>
#include <Ethernet.h>
#include <EEPROM.h>
#include <EthernetUdp.h>
#include <Dns.h>
#include <math.h>
#include <LiquidCrystal_I2C.h>
#include <DHT.h>
#include <SdFat.h>

SdFat    SD;
SdCard   g_sdCard;
FsVolume g_sdVol;

// ======== DEFAULTS (EEPROM ausente) ========
#define DEFAULT_USE_STATIC_IP  0
const IPAddress DEFAULT_IP   (172, 17, 240, 254);
const IPAddress DEFAULT_DNS  (172, 17, 240,   2);
const IPAddress DEFAULT_GW   (172, 17, 240,   1);
const IPAddress DEFAULT_MASK (255, 255, 252,  0);
//const uint8_t  DEFAULT_MAC[6] = {0xDE,0xAD,0xBE,0xEF,0xFE,0xED};
const uint8_t  DEFAULT_MAC[6] = {0x5C,0xCF,0x7F,0x84,0x56,0x56};

// ======== DHCP robusto ========
const uint8_t      DHCP_RETRIES          = 3;      // quantas tentativas de DHCP
const unsigned long DHCP_WAIT_LINK_MS    = 8000;   // espera por link antes do DHCP
const unsigned long DHCP_RETRY_DELAY_MS  = 2000;   // pausa entre tentativas


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
EthernetServer server(80);
EthernetUDP Udp;

// ======== NTP ========
const char* NTP_SERVER = "a.st1.ntp.br";
const unsigned int NTP_LOCAL_PORT = 8888;
const unsigned long NTP_INTERVAL_MS = 10UL * 60UL * 1000UL;
unsigned long lastNtpSyncMs = 0;
unsigned long epochAtLastSync = 0;  // UTC
unsigned long msAtLastSync = 0;

// ======== Amostragem ========
unsigned long lastSampleMs = 0;
const unsigned long sampleIntervalMs = 2500;
unsigned long dhtWarmupUntil = 0;
float lastTemp = NAN, lastHum = NAN;

// ======== Alertas ========
const float THRESH_C = 30.0;
bool wasBelowThreshold = true;

// ======== LCD ========
unsigned long ipSplashStartMs = 0;
bool ipSplashDone = false;
const unsigned long IP_SPLASH_MS = 2000;

// ======== SD / Log ========
bool sdAvailable = false;
char currentMonthFile[20] = {0}; // "L202509.CSV"
volatile bool sdBusy = false;
float lastLoggedTemp = NAN;
float lastLoggedHum  = NAN;

// ======== Config persistente (EEPROM) ========
struct NetConfig {
  uint8_t  magic;       // 0x42
  uint8_t  use_static;  // 0/1
  uint8_t  ip[4];
  uint8_t  dns[4];
  uint8_t  gw[4];
  uint8_t  mask[4];
  uint8_t  mac[6];
  // Calibração: y_cal = k * y_raw + a
  float    kTemp; float aTemp;
  float    kHum;  float aHum;
  uint8_t  checksum; // soma simples
};
const uint8_t CFG_MAGIC = 0x42;
const int EEPROM_ADDR = 0;
NetConfig cfg;

void streamCsvAsJson(EthernetClient &client,
                     unsigned long minEpoch,
                     unsigned long maxEpoch,
                     int yearMonth,
                     bool debugLogs = false);


// ======== Helpers ========
static inline float applyCalTemp(float raw){ return isnan(raw) ? raw : (cfg.kTemp*raw + cfg.aTemp); }
static inline float applyCalHum (float raw){ return isnan(raw) ? raw : (cfg.kHum *raw + cfg.aHum ); }

static inline void macToString(const uint8_t mac[6], char* buf, size_t sz){
  snprintf(buf, sz, "%02X:%02X:%02X:%02X:%02X:%02X",
    mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
}

static inline String ipToString(IPAddress ip){
  char s[24]; snprintf(s,sizeof(s),"%u.%u.%u.%u",ip[0],ip[1],ip[2],ip[3]); return String(s);
}

uint8_t calcChecksum(const NetConfig &c){
  const uint8_t *p = (const uint8_t*)&c;
  uint16_t s=0;
  for(size_t i=0;i<sizeof(NetConfig)-1;i++) s+=p[i];
  return (uint8_t)(s & 0xFF);
}

// Itera linhas do CSV e envia JSON filtrado.
// minEpoch/maxEpoch: filtro por epoch (0 = sem filtro no lado).
// yearMonth: yyyymm para ler exatamente LYYYYMM.CSV; se -1, tenta mês atual e anterior.
// debugLogs: imprime métricas no Serial.
void streamCsvAsJson(EthernetClient &client,
                     unsigned long minEpoch,
                     unsigned long maxEpoch,
                     int yearMonth,
                     bool debugLogs) {
  unsigned long t0 = millis();
  if (!sdAvailable) { client.println(F("[]")); return; }

  // ---- SPI handoff: desabilita Ethernet p/ operar SD ----
  digitalWrite(PIN_CS_ETH, HIGH);

  bool first = true;
  client.print('[');

  // Métricas de debug
  unsigned long filesTried = 0, filesOpened = 0;
  unsigned long linesTotal = 0, linesParsed = 0, linesEmitted = 0;

  auto streamFile = [&](const char* fname){
    if (!SD.exists(fname)) {
      if (debugLogs) { Serial.print(F("[WS/LOG] arquivo inexistente: ")); Serial.println(fname); }
      return;
    }
    filesTried++;
    File f = SD.open(fname, FILE_READ);
    if (!f) {
      if (debugLogs) { Serial.print(F("[WS/LOG] falha ao abrir: ")); Serial.println(fname); }
      return;
    }
    filesOpened++;

    String line;
    while (f.available()) {
      char c = f.read();
      if (c=='\n' || c=='\r') {
        if (line.length()>0) {
          linesTotal++;
          if (!line.startsWith(F("epoch,"))) {
            int p1 = line.indexOf(',');
            int p2 = line.indexOf(',', p1+1);
            if (p1>0 && p2>p1) {
              unsigned long e = (unsigned long) line.substring(0,p1).toInt();
              if (e != 0 && (minEpoch==0 || e>=minEpoch) && (maxEpoch==0 || e<=maxEpoch)) {
                String st = line.substring(p1+1, p2);
                String sh = line.substring(p2+1);
                if (!first) client.print(',');
                client.print(F("{\"epoch\":")); client.print(e);
                client.print(F(",\"temperature\": ")); client.print(st);
                client.print(F(",\"humidity\": ")); client.print(sh);
                client.print('}');
                first=false;
                linesEmitted++;
              }
              linesParsed++;
            }
          }
        }
        line = "";
      } else {
        line += c;
      }
    }
    f.close();
  };

  if (yearMonth > 0) {
    char fname[20];
    formatMonthFilenameFromYYYYMM((uint32_t)yearMonth, fname, sizeof(fname));
    if (debugLogs) { Serial.print(F("[WS/LOG] modo yyyymm, arquivo=")); Serial.println(fname); }
    streamFile(fname);
  } else {
    // Lê mês atual + anterior
    unsigned long nowEpoch = getEpochUTC();
    updateMonthFileName(nowEpoch);
    char cur[20]; strncpy(cur, currentMonthFile, sizeof(cur)); cur[sizeof(cur)-1] = '\0';

    unsigned long prevEpoch = (nowEpoch > 31UL*86400UL) ? (nowEpoch - 31UL*86400UL) : 0UL;
    updateMonthFileName(prevEpoch);
    char prev[20]; strncpy(prev, currentMonthFile, sizeof(prev)); prev[sizeof(prev)-1] = '\0';

    // restaura nome do mês atual
    updateMonthFileName(nowEpoch);

    if (debugLogs) {
      Serial.print(F("[WS/LOG] lendo prev=")); Serial.print(prev);
      Serial.print(F(" cur=")); Serial.println(cur);
      Serial.print(F("[WS/LOG] filtro minEpoch=")); Serial.print(minEpoch);
      Serial.print(F(" maxEpoch=")); Serial.println(maxEpoch);
    }

    streamFile(prev);
    streamFile(cur);
  }

  client.println(']');

  // ---- solta o SD ----
  digitalWrite(PIN_CS_SD, HIGH);

  if (debugLogs) {
    Serial.print(F("[WS/LOG] filesTried="));  Serial.print(filesTried);
    Serial.print(F(" opened="));              Serial.print(filesOpened);
    Serial.print(F(" linesTotal="));          Serial.print(linesTotal);
    Serial.print(F(" linesParsed="));         Serial.print(linesParsed);
    Serial.print(F(" linesEmitted="));        Serial.print(linesEmitted);
    Serial.print(F(" ms="));                  Serial.println(millis()-t0);
  }
}


void loadDefaults(NetConfig &c){
  c.magic = CFG_MAGIC;
  c.use_static = DEFAULT_USE_STATIC_IP ? 1 : 0;
  for(int i=0;i<4;i++){
    c.ip[i]=DEFAULT_IP[i]; c.dns[i]=DEFAULT_DNS[i];
    c.gw[i]=DEFAULT_GW[i]; c.mask[i]=DEFAULT_MASK[i];
  }
  for(int i=0;i<6;i++) c.mac[i]=DEFAULT_MAC[i];
  c.kTemp=1.0f; c.aTemp=0.0f;
  c.kHum =1.0f; c.aHum =0.0f;
  c.checksum = calcChecksum(c);
}

bool loadConfig(NetConfig &c){
  EEPROM.get(EEPROM_ADDR, c);
  if(c.magic!=CFG_MAGIC) return false;
  if(c.checksum!=calcChecksum(c)) return false;
  if (!isfinite(c.kTemp)) c.kTemp=1.0f;
  if (!isfinite(c.aTemp)) c.aTemp=0.0f;
  if (!isfinite(c.kHum )) c.kHum =1.0f;
  if (!isfinite(c.aHum )) c.aHum =0.0f;
  return true;
}
void saveConfig(const NetConfig &c){ EEPROM.put(EEPROM_ADDR, c); }

String getQueryParam(const String& q, const String& k){
  String pat=k+"="; int i=q.indexOf(pat); if(i<0) return "";
  int j=q.indexOf('&', i+pat.length()); if(j<0) j=q.length();
  String v=q.substring(i+pat.length(), j); v.replace("+"," "); return v;
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

static inline String humanKB(unsigned long long bytes) {
  unsigned long kb = (unsigned long)((bytes + 1023ULL) / 1024ULL);
  char buf[32];
  snprintf(buf, sizeof(buf), "%lu KB", kb);
  return String(buf);
}

bool resolveHostname(const char* host, IPAddress& outIP) {
  IPAddress dnsIP = Ethernet.dnsServerIP();
  if (dnsIP == IPAddress(0,0,0,0)) {
    dnsIP = IPAddress(DEFAULT_DNS[0], DEFAULT_DNS[1], DEFAULT_DNS[2], DEFAULT_DNS[3]);
  }
  DNSClient dns; dns.begin(dnsIP);
  int rc = dns.getHostByName(host, outIP);
  #if SERIAL_VERBOSE
    Serial.print(F("[NET] DNS ")); Serial.print(ipToString(dnsIP));
    Serial.print(F(" resolver '")); Serial.print(host);
    Serial.print(F("' -> rc=")); Serial.println(rc);
  #endif
  return (rc == 1);
}

// ======== SD Capacidade ========
bool getSdCapacityAndFree(uint64_t &totalBytes, uint64_t &freeBytes) {
  if (!sdAvailable) return false;
  digitalWrite(PIN_CS_ETH, HIGH);
  if (!g_sdCard.begin(SdSpiConfig(PIN_CS_SD, DEDICATED_SPI, SD_SCK_MHZ(25)))) {
    digitalWrite(PIN_CS_SD, HIGH);
    return false;
  }
  if (!g_sdVol.begin(&g_sdCard)) {
    digitalWrite(PIN_CS_SD, HIGH);
    return false;
  }
  const uint32_t sectorsPerCluster = g_sdVol.sectorsPerCluster();
  const uint32_t clusterCount      = g_sdVol.clusterCount();
  totalBytes = (uint64_t)clusterCount * (uint64_t)sectorsPerCluster * 512ULL;
  const uint32_t freeClusters = g_sdVol.freeClusterCount();
  freeBytes = (uint64_t)freeClusters * (uint64_t)sectorsPerCluster * 512ULL;
  digitalWrite(PIN_CS_SD, HIGH);
  return true;
}

// ======== Tempo ========
unsigned long getEpochUTC(){
  if (epochAtLastSync == 0) return 0UL;
  unsigned long secs = (millis() - msAtLastSync)/1000UL;
  return epochAtLastSync + secs;
}

void sendNTPpacket(IPAddress& address) {
  const int NTP_PACKET_SIZE = 48;
  byte packetBuffer[NTP_PACKET_SIZE];
  memset(packetBuffer, 0, NTP_PACKET_SIZE);
  packetBuffer[0] = 0b11100011;
  Udp.beginPacket(address, 123);
  Udp.write(packetBuffer, NTP_PACKET_SIZE);
  Udp.endPacket();
}

void ntpSyncNow(){
  IPAddress timeServerIP(0,0,0,0);
  if (!resolveHostname(NTP_SERVER, timeServerIP)) {
    #if SERIAL_VERBOSE
      Serial.println(F("[NTP] Falha no DNS para servidor NTP (usando DNS fallback)."));
    #endif
  }
  if (timeServerIP != IPAddress(0,0,0,0)) {
    #if SERIAL_VERBOSE
      Serial.print(F("[NTP] Enviando req p/ ")); Serial.println(ipToString(timeServerIP));
    #endif
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
      #if SERIAL_VERBOSE
        Serial.print(F("[NTP] Sync OK. epoch=")); Serial.println(epochUTC);
      #endif
    } else {
      #if SERIAL_VERBOSE
        Serial.println(F("[NTP] Resposta inválida / timeout."));
      #endif
    }
  } else {
    #if SERIAL_VERBOSE
      Serial.println(F("[NTP] IP do servidor NTP não resolvido."));
    #endif
  }
  lastNtpSyncMs = millis();
}

uint32_t currentYYYYMM(unsigned long epochUTC){
  if (epochUTC == 0) return 0UL;
  unsigned long days = epochUTC / 86400UL;
  int y = 1970; unsigned long d = days;
  while (true) {
    bool leap = (y%4==0 && (y%100!=0 || y%400==0));
    unsigned long diy = leap ? 366 : 365;
    if (d >= diy) { d -= diy; y++; } else break;
  }
  int md[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
  bool leap = (y%4==0 && (y%100!=0 || y%400==0));
  if (leap) md[1] = 29;
  int m = 0;
  while (m<12 && d >= (unsigned long)md[m]) { d -= md[m]; m++; }
  return (uint32_t)y*100UL + (uint32_t)(m+1);
}

static inline void formatMonthFilenameFromYM(uint16_t year, uint8_t month, char* out, size_t sz) {
  if (month < 1)  month = 1;
  if (month > 12) month = 12;
  snprintf(out, sz, "L%04u%02u.CSV", (unsigned)year, (unsigned)month);
}
static inline void formatMonthFilenameFromYYYYMM(uint32_t yyyymm, char* out, size_t sz) {
  uint16_t year  = (uint16_t)(yyyymm / 100U);
  uint8_t  month = (uint8_t)(yyyymm % 100U);
  formatMonthFilenameFromYM(year, month, out, sz);
}

bool updateMonthFileName(unsigned long epochUTC){
  if (epochUTC == 0) return false;
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
  return true;
}

// ======== LCD ========
void printIPToSerial(){
  Serial.print(F("[NET] IP: ")); Serial.print(Ethernet.localIP());
  Serial.print(F(" | GW: ")); Serial.print(Ethernet.gatewayIP());
  Serial.print(F(" | DNS: ")); Serial.print(Ethernet.dnsServerIP());
  Serial.print(F(" | Mask: ")); Serial.println(Ethernet.subnetMask());
  Serial.println(F("[HTTP] Servidor na porta 80"));
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

// ======== Buzzer ========
void beep(uint16_t f,uint16_t ms){ tone(PIN_BUZZER,f,ms); delay(ms+5); noTone(PIN_BUZZER); }
void startupChime(){ beep(1200,120); beep(1600,120); beep(2000,160); }

// ======== SD ========
void sdInit(){
  digitalWrite(PIN_CS_ETH, HIGH);
  sdAvailable = SD.begin(PIN_CS_SD);
  #if SERIAL_VERBOSE
    if (!sdAvailable) Serial.println(F("[SD ] Nao detectado. Historico DESATIVADO."));
    else              Serial.println(F("[SD ] OK. Historico ATIVADO."));
  #endif
}

bool sdAppendLog(float t, float h) {
  if (!sdAvailable) return false;
  if (sdBusy) {
    #if SERIAL_VERBOSE
      Serial.println(F("[SD ] Ignorado append: SD ocupado."));
    #endif
    return false;
  }
  sdBusy = true;

  bool firstLog = isnan(lastLoggedTemp) || isnan(lastLoggedHum);
  bool dt = !firstLog && (fabsf(t - lastLoggedTemp) >= 1.0f);
  bool dh = !firstLog && (fabsf(h - lastLoggedHum ) >= 1.0f);
  if (!firstLog && !dt && !dh) {
    sdBusy=false;
    #if SERIAL_VERBOSE
      Serial.println(F("[LOG] Mudanca < 1.0 -> nao grava."));
    #endif
    return false;
  }

  unsigned long epoch = getEpochUTC();
  if (epoch == 0) {
    sdBusy=false;
    #if SERIAL_VERBOSE
      Serial.println(F("[LOG] Sem epoch (NTP) -> nao grava."));
    #endif
    return false;
  }

  digitalWrite(PIN_CS_ETH, HIGH);

  if (!updateMonthFileName(epoch)) { sdBusy=false; return false; }
  char fname[20];
  strncpy(fname, currentMonthFile, sizeof(fname));
  fname[sizeof(fname)-1] = '\0';

  bool newFile = !SD.exists(fname);
  File f = SD.open(fname, FILE_WRITE);
  if (!f) {
    Serial.print(F("[SD ] Falha ao abrir ")); Serial.println(fname);
    digitalWrite(PIN_CS_SD, HIGH);
    sdBusy=false;
    return false;
  }

  if (newFile) f.println(F("epoch,temperature,humidity"));
  f.print(epoch); f.print(','); f.print(t, 2); f.print(','); f.println(h, 2);
  f.flush();
  f.close();

  lastLoggedTemp = t;
  lastLoggedHum  = h;

  digitalWrite(PIN_CS_SD, HIGH);
  sdBusy = false;

  #if SERIAL_VERBOSE
    Serial.print(F("[LOG] + ")); Serial.print(fname);
    Serial.print(F(" -> ")); Serial.print(t,2);
    Serial.print(F("C, ")); Serial.print(h,2);
    Serial.println(F("%"));
  #endif
  return true;
}

// ======== HTTP util ========
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

// --- humanSize / printSdStatusHtml ---
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
  client.println(F(
    "<div class='d-flex align-items-center mb-2'>"
      "<h6 class='mb-0 me-2'>Armazenamento SD</h6>"
      "<span class='badge bg-success'>Dispon&iacute;vel</span>"
    "</div>"
  ));

  digitalWrite(PIN_CS_ETH, HIGH);
  File root = SD.open("/");
  if (!root) {
    client.println(F("<div class='alert alert-danger mb-0'>Falha ao abrir a raiz do SD.</div>"));
    digitalWrite(PIN_CS_SD, HIGH);
    return;
  }

  unsigned long long totalBytesUsed = 0;
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
      totalBytesUsed += (unsigned long long)sz;
      totalFiles++;

      if (shown < MAX_ROWS) {
        char nm[64];
        entry.getName(nm, sizeof(nm));
        client.print(F("<tr><td><code>"));
        client.print(nm);
        client.print(F("</code></td><td class='text-end'>"));
        client.print(humanKB(sz));
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

  uint64_t capBytes = 0, freeBytes = 0;
  bool okSpace = getSdCapacityAndFree(capBytes, freeBytes);

  client.print(F("<div class='small text-muted'>Arquivos: "));
  client.print(totalFiles);
  client.print(F(" &middot; Ocupa&ccedil;&atilde;o total: "));
  client.print(humanKB(totalBytesUsed));
  client.println(F("</div>"));

  client.print(F("<div class='small text-muted'>Espa&ccedil;o livre: "));
  if (okSpace) client.print(humanKB(freeBytes));
  else         client.print(F("N/D"));
  client.println(F("</div>"));

  digitalWrite(PIN_CS_SD, HIGH);
}


// Espera pelo link físico da interface (se a lib suportar), senão retorna true
static bool waitForLink(unsigned long timeoutMs) {
  unsigned long t0 = millis();
  #if defined(ETHERNET_H) && defined(LINKON) // algumas variantes expõem linkStatus()
    while (millis() - t0 < timeoutMs) {
      auto st = Ethernet.linkStatus();
      if (st == LinkON) return true;
      delay(200);
    }
    return false;
  #else
    (void)timeoutMs;
    return true; // sem suporte a linkStatus -> segue o baile
  #endif
}


void applyNetworkFromConfig(){
  // Garante CS em HIGH
  #if defined(SS)
    pinMode(SS, OUTPUT); digitalWrite(SS, HIGH);
  #else
    pinMode(53, OUTPUT); digitalWrite(53, HIGH);
  #endif
  pinMode(PIN_CS_ETH, OUTPUT);
  pinMode(PIN_CS_SD,  OUTPUT);
  digitalWrite(PIN_CS_ETH, HIGH);
  digitalWrite(PIN_CS_SD,  HIGH);

  // MAC
  uint8_t macLocal[6];
  for (int i=0;i<6;i++) macLocal[i]=cfg.mac[i];
  char macStr[18]; macToString(macLocal, macStr, sizeof(macStr));

  #if SERIAL_VERBOSE
    Serial.print(F("[NET] MAC=")); Serial.println(macStr);
    Serial.print(F("[NET] Modo=")); Serial.println(cfg.use_static ? F("STATIC") : F("DHCP"));
  #endif

  // IPs estáticos prontos para fallback
  IPAddress ipS  (cfg.ip[0],  cfg.ip[1],  cfg.ip[2],  cfg.ip[3]);
  IPAddress dnsS (cfg.dns[0], cfg.dns[1], cfg.dns[2], cfg.dns[3]);
  IPAddress gwS  (cfg.gw[0],  cfg.gw[1],  cfg.gw[2],  cfg.gw[3]);
  IPAddress mskS (cfg.mask[0],cfg.mask[1],cfg.mask[2],cfg.mask[3]);

  if (cfg.use_static) {
    // ===== MODO ESTÁTICO =====
    #if SERIAL_VERBOSE
      Serial.print(F("[NET] STATIC IP=")); Serial.print(ipS);
      Serial.print(F(" GW="));            Serial.print(gwS);
      Serial.print(F(" MASK="));          Serial.print(mskS);
      Serial.print(F(" DNS="));           Serial.println(dnsS);
    #endif
    Ethernet.begin(macLocal, ipS, dnsS, gwS, mskS);
  } else {
    // ===== MODO DHCP (robusto) =====
    #if SERIAL_VERBOSE
      Serial.print(F("[NET] Aguardando link (ms)=")); Serial.println(DHCP_WAIT_LINK_MS);
    #endif
    (void)waitForLink(DHCP_WAIT_LINK_MS);

    bool ok = false;
    for (uint8_t attempt = 1; attempt <= DHCP_RETRIES; ++attempt) {
      #if SERIAL_VERBOSE
        Serial.print(F("[NET] Tentando DHCP (")); Serial.print(attempt);
        Serial.print(F("/")); Serial.print(DHCP_RETRIES); Serial.println(F(")..."));
      #endif
      if (Ethernet.begin(macLocal) != 0) { ok = true; break; }
      #if SERIAL_VERBOSE
        Serial.print(F("[NET] DHCP falhou; aguardando ")); Serial.print(DHCP_RETRY_DELAY_MS);
        Serial.println(F(" ms e tentando novamente..."));
      #endif
      delay(DHCP_RETRY_DELAY_MS);
    }

    if (!ok) {
      #if SERIAL_VERBOSE
        Serial.println(F("[NET] DHCP esgotado -> fallback para estático (DEFAULT_*)"));
      #endif
      // Fallback final para os DEFAULT_* (independe do que está salvo na EEPROM)
      Ethernet.begin(
        macLocal,
        IPAddress(DEFAULT_IP[0],   DEFAULT_IP[1],   DEFAULT_IP[2],   DEFAULT_IP[3]),
        IPAddress(DEFAULT_DNS[0],  DEFAULT_DNS[1],  DEFAULT_DNS[2],  DEFAULT_DNS[3]),
        IPAddress(DEFAULT_GW[0],   DEFAULT_GW[1],   DEFAULT_GW[2],   DEFAULT_GW[3]),
        IPAddress(DEFAULT_MASK[0], DEFAULT_MASK[1], DEFAULT_MASK[2], DEFAULT_MASK[3])
      );
    } else {
      #if SERIAL_VERBOSE
        Serial.println(F("[NET] DHCP OK."));
      #endif
    }
  }

  delay(500);
  server.begin();
  Udp.begin(NTP_LOCAL_PORT);

  printIPToSerial();

  // DNS “failsafe”: se o DNS vier zerado do DHCP, aplica o default
  IPAddress dnsIP = Ethernet.dnsServerIP();
  if (dnsIP == IPAddress(0,0,0,0)) {
    #if SERIAL_VERBOSE
      Serial.println(F("[NET] DHCP sem DNS -> aplicando DEFAULT_DNS."));
    #endif
    // A lib Ethernet não tem setter para DNS depois de begin(); apenas logamos.
    // O resolveHostname() já faz fallback para DEFAULT_DNS se o atual vier 0.0.0.0
  }
}


// ======== Páginas e endpoints ========
void handleRootPage(EthernetClient &client, const String &query);
void handleHistoricoPage(EthernetClient &client);
void handleExportPage(EthernetClient &client);
void handleCalibracaoPage(EthernetClient &client, const String &query);
void handleSetCalibracao(EthernetClient &client, const String &query);
void handleJsonNow(EthernetClient &client);
void handleJsonLog(EthernetClient &client, const String &query);

// ======== LCD progress helper ========
#ifndef LCD_PROGRESS_HELPER_DEFINED
#define LCD_PROGRESS_HELPER_DEFINED
static void lcdShowProgress(const char* label, uint32_t done, uint32_t total) {
  if (total == 0) total = 1;
  const uint8_t cols = 16;
  uint8_t filled = (uint32_t)((uint64_t)done * cols / total);
  if (filled > cols) filled = cols;
  char l0[17]; memset(l0, ' ', 16); l0[16] = '\0';
  size_t n = strnlen(label, 16);
  memcpy(l0, label, n);
  lcd.setCursor(0, 0); lcd.print(l0);
  char bar[17];
  for (uint8_t i = 0; i < cols; i++) bar[i] = (i < filled ? '#' : '-');
  bar[16] = '\0';
  uint8_t pct = (uint8_t)((uint64_t)done * 100 / total);
  lcd.setCursor(0, 1);  lcd.print(bar);
  char pbuf[5]; snprintf(pbuf, sizeof(pbuf), "%3u%%", pct);
  lcd.setCursor(12, 1); lcd.print(pbuf);
}
#endif

// ======== streamMonthCsvAsJson ========
void streamMonthCsvAsJson(EthernetClient &client, uint32_t yyyymm) {
  char fname[20];
  formatMonthFilenameFromYYYYMM(yyyymm, fname, sizeof(fname));
  Serial.print(F("[LOG] streamMonthCsvAsJson yyyymm="));
  Serial.print(yyyymm);
  Serial.print(F(" arquivo="));
  Serial.println(fname);

  if (sdBusy) {
    Serial.println(F("[LOG] SD ocupado por outra operacao. Retornando []."));
    client.println(F("[]"));
    return;
  }
  sdBusy = true;

  digitalWrite(PIN_CS_ETH, HIGH);

  if (!SD.exists(fname)) {
    Serial.println(F("[LOG] Arquivo nao existe. Retornando []."));
    client.println(F("[]"));
    sdBusy = false;
    digitalWrite(PIN_CS_SD, HIGH);
    #ifdef LCD_PROGRESS_HELPER_DEFINED
      lcdShowProgress("Arquivo AUSENTE", 1, 1);
      delay(600);
      printIPLine(); if (!isnan(lastTemp)&&!isnan(lastHum)) printTHLine();
    #endif
    return;
  }

  File f = SD.open(fname, FILE_READ);
  if (!f) {
    Serial.println(F("[LOG] Falha ao abrir (FILE_READ). Retornando []."));
    client.println(F("[]"));
    sdBusy = false;
    digitalWrite(PIN_CS_SD, HIGH);
    #ifdef LCD_PROGRESS_HELPER_DEFINED
      lcdShowProgress("Falha ao abrir", 1, 1);
      delay(600);
      printIPLine(); if (!isnan(lastTemp)&&!isnan(lastHum)) printTHLine();
    #endif
    return;
  }

  f.seek(0);
  const uint32_t totalSize = (uint32_t)f.size();
  uint32_t bytesRead = 0;

  #ifdef LCD_PROGRESS_HELPER_DEFINED
    char label[17]; label[16] = '\0';
    {
      const char* pref = "Enviando ";
      size_t lp = strlen(pref), lf = strlen(fname);
      if (lp + lf <= 16) snprintf(label, sizeof(label), "%s%s", pref, fname);
      else snprintf(label, sizeof(label), "%s", fname);
    }
    lcdShowProgress(label, 0, totalSize);
    unsigned long lastLcdMs = 0;
  #endif

  Serial.println(F("[LOG] Lendo/streaming CSV..."));
  client.print('[');
  bool first = true;

  const size_t BUFSZ = 256;
  uint8_t buf[BUFSZ];
  String line;

  unsigned long lastProgressMs = millis();
  const unsigned long MAX_IDLE_MS  = 2000;
  const unsigned long MAX_TOTAL_MS = 120000;
  unsigned long startMs = millis();

  while (bytesRead < totalSize) {
    size_t toRead = totalSize - bytesRead;
    if (toRead > BUFSZ) toRead = BUFSZ;

    int n = f.read(buf, (int)toRead);
    if (n == 0 && bytesRead == 0) {
      Serial.println(F("[LOG] Primeiro read=0; seek(0) e tentar novamente..."));
      f.seek(0);
      n = f.read(buf, (int)toRead);
    }

    if (n > 0) {
      bytesRead += (uint32_t)n;
      lastProgressMs = millis();

      #ifdef LCD_PROGRESS_HELPER_DEFINED
        if (lastProgressMs - lastLcdMs >= 150) {
          lcdShowProgress(label, bytesRead, totalSize);
          lastLcdMs = lastProgressMs;
        }
      #endif

      for (int i = 0; i < n; i++) {
        char c = (char)buf[i];
        if (c == '\n' || c == '\r') {
          if (line.length() > 0) {
            if (!line.startsWith(F("epoch,"))) {
              int p1 = line.indexOf(',');
              int p2 = line.indexOf(',', p1 + 1);
              if (p1 > 0 && p2 > p1) {
                unsigned long e = line.substring(0, p1).toInt();
                String st = line.substring(p1 + 1, p2);
                String sh = line.substring(p2 + 1);
                if (!first) client.print(',');
                client.print(F("{\"epoch\":")); client.print(e);
                client.print(F(",\"temperature\":")); client.print(st);
                client.print(F(",\"humidity\":")); client.print(sh);
                client.print('}');
                first = false;
              }
            }
          }
          line = "";
        } else {
          line += c;
        }
      }
    } else {
      delay(2);
      if (millis() - lastProgressMs > MAX_IDLE_MS) {
        Serial.println(F("[LOG] Idle timeout lendo SD — abortando."));
        break;
      }
    }
    if (millis() - startMs > MAX_TOTAL_MS) {
      Serial.println(F("[LOG] Max total time — abortando."));
      break;
    }
  }

  if (line.length() > 0 && !line.startsWith(F("epoch,"))) {
    int p1 = line.indexOf(',');
    int p2 = line.indexOf(',', p1 + 1);
    if (p1 > 0 && p2 > p1) {
      unsigned long e = line.substring(0, p1).toInt();
      String st = line.substring(p1 + 1, p2);
      String sh = line.substring(p2 + 1);
      if (!first) client.print(',');
      client.print(F("{\"epoch\":")); client.print(e);
      client.print(F(",\"temperature\":")); client.print(st);
      client.print(F(",\"humidity\":")); client.print(sh);
      client.print('}');
      first = false;
    }
  }

  f.close();
  client.println(']');

  #ifdef LCD_PROGRESS_HELPER_DEFINED
    lcdShowProgress(label, bytesRead, totalSize);
  #endif
  digitalWrite(PIN_CS_SD, HIGH);
  sdBusy = false;

  Serial.print(F("[LOG] Streaming OK. bytesRead="));
  Serial.print(bytesRead);
  Serial.print(F("/"));
  Serial.println(totalSize);

  #ifdef LCD_PROGRESS_HELPER_DEFINED
    delay(600);
    printIPLine();
    if (!isnan(lastTemp) && !isnan(lastHum)) printTHLine();
    else { lcd.setCursor(0,1); lcd.print("Aguardando DHT "); }
  #endif
}

// ======== Páginas/Handlers principais ========
void handleRootPage(EthernetClient &client, const String &query){
  bool changed=false; NetConfig newCfg=cfg;
  if(query.length()>0){
    String v_mode  = getQueryParam(query,"mode");
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
      Serial.println(F("[CFG] Configuracao salva. Aplicando rede..."));
      applyNetworkFromConfig();
    }
  }

  // ===== HTML com menu responsivo e painel T/H =====
  sendHtmlHeader(client);
  client.println(F("<!doctype html><html lang='pt-br'>"));
  client.println(F("<head>"
    "<meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width, initial-scale=1'>"
    "<title>mega temp02 • Dashboard</title>"
    "<link href='https://cdn.jsdelivr.net/npm/bootstrap@5.3.3/dist/css/bootstrap.min.css' rel='stylesheet'>"
    "<style>"
      "body{padding-top:56px}"
      "code{font-size:.95em}"
      ".metric{font-size:2.4rem;font-weight:700;line-height:1}"
      ".metric-sub{font-size:.95rem;color:#6c757d}"
      ".mono{font-family:ui-monospace,SFMono-Regular,Menlo,Monaco,Consolas,'Liberation Mono','Courier New',monospace}"
    "</style>"
  "</head>"));

  client.println(F("<body>"
    "<nav class='navbar navbar-expand-lg navbar-dark bg-dark fixed-top'>"
      "<div class='container-fluid'>"
        "<a class='navbar-brand' href='/'>mega temp02</a>"
        "<button class='navbar-toggler' type='button' data-bs-toggle='collapse' data-bs-target='#navbars' aria-controls='navbars' aria-expanded='false' aria-label='Toggle navigation'>"
          "<span class='navbar-toggler-icon'></span>"
        "</button>"
        "<div class='collapse navbar-collapse' id='navbars'>"
          "<ul class='navbar-nav me-auto mb-2 mb-lg-0'>"
            "<li class='nav-item'><a class='nav-link active' aria-current='page' href='/'>Dashboard</a></li>"
            "<li class='nav-item'><a class='nav-link' href='/historico'>Hist&oacute;rico</a></li>"
            "<li class='nav-item'><a class='nav-link' href='/export'>Exportar/Limpar</a></li>"
            "<li class='nav-item'><a class='nav-link' href='/calibracao'>Calibra&ccedil;&atilde;o</a></li>"
          "</ul>"
          "<div class='d-flex'>"
            "<a class='btn btn-outline-light btn-sm me-2' href='/ws/temperatura' target='_blank'>/ws/temperatura</a>"
            "<a class='btn btn-outline-light btn-sm' href='/ws/log?hours=24' target='_blank'>/ws/log?hours=24</a>"
          "</div>"
        "</div>"
      "</div>"
    "</nav>"));

  client.println(F("<main class='container'>"));

  // Painel de métricas
  client.print(F(
    "<div class='row g-3'>"
      "<div class='col-12 col-md-6'>"
        "<div class='card shadow-sm'>"
          "<div class='card-body'>"
            "<div class='d-flex justify-content-between align-items-center'>"
              "<div>"
                "<div class='metric' id='mTemp'>"
  ));
  // valor inicial (snapshot do último lido)
  if (!isnan(lastTemp)) {
    char tb[16]; dtostrf(lastTemp, 0, 2, tb);
    client.print(tb);
  } else {
    client.print(F("--"));
  }
  client.println(F(" &deg;C</div>"
                "<div class='metric-sub'>Temperatura atual</div>"
              "</div>"
              "<span class='badge text-bg-secondary'>DHT22</span>"
            "</div>"
          "</div>"
        "</div>"
      "</div>"));

  client.print(F(
      "<div class='col-12 col-md-6'>"
        "<div class='card shadow-sm'>"
          "<div class='card-body'>"
            "<div class='d-flex justify-content-between align-items-center'>"
              "<div>"
                "<div class='metric' id='mHum'>"
  ));
  if (!isnan(lastHum)) {
    char hb[16]; dtostrf(lastHum, 0, 2, hb);
    client.print(hb);
  } else {
    client.print(F("--"));
  }
  client.println(F(" %</div>"
                "<div class='metric-sub'>Umidade relativa</div>"
              "</div>"
              "<span class='badge text-bg-info'>Ambiente</span>"
            "</div>"
          "</div>"
        "</div>"
      "</div>"
    "</div>"));

  // Bloco rede
  IPAddress ip = Ethernet.localIP();
  IPAddress gw = Ethernet.gatewayIP();
  IPAddress ms = Ethernet.subnetMask();
  IPAddress dn = Ethernet.dnsServerIP();
  char ipS[24]; snprintf(ipS,sizeof(ipS),"%u.%u.%u.%u",ip[0],ip[1],ip[2],ip[3]);
  char gwS[24]; snprintf(gwS,sizeof(gwS),"%u.%u.%u.%u",gw[0],gw[1],gw[2],gw[3]);
  char msS[24]; snprintf(msS,sizeof(msS),"%u.%u.%u.%u",ms[0],ms[1],ms[2],ms[3]);
  char dnS[24]; snprintf(dnS,sizeof(dnS),"%u.%u.%u.%u",dn[0],dn[1],dn[2],dn[3]);
  char macStr[18]; macToString(cfg.mac, macStr, sizeof(macStr));

  client.print(F(
    "<div class='row g-3 mt-1'>"
      "<div class='col-12'>"
        "<div class='card shadow-sm'>"
          "<div class='card-header'>Rede</div>"
          "<div class='card-body'>"
            "<div class='row'>"
              "<div class='col-12 col-md-6'>"
                "<dl class='row mb-0'>"
                  "<dt class='col-4'>Modo</dt><dd class='col-8'>"
  ));
  client.print(cfg.use_static ? F("Est&aacute;tico") : F("DHCP"));
  client.print(F("</dd>"
                  "<dt class='col-4'>MAC</dt><dd class='col-8 mono'>"));
  client.print(macStr);
  client.print(F("</dd>"
                  "<dt class='col-4'>IP</dt><dd class='col-8 mono'>"));
  client.print(ipS);
  client.print(F("</dd>"
                  "<dt class='col-4'>Gateway</dt><dd class='col-8 mono'>"));
  client.print(gwS);
  client.print(F("</dd>"
                "</dl>"
              "</div>"
              "<div class='col-12 col-md-6'>"
                "<dl class='row mb-0'>"
                  "<dt class='col-4'>Mask</dt><dd class='col-8 mono'>"));
  client.print(msS);
  client.print(F("</dd>"
                  "<dt class='col-4'>DNS</dt><dd class='col-8 mono'>"));
  client.print(dnS);
  client.print(F("</dd>"
                "</dl>"
              "</div>"
            "</div>"
          "</div>"
        "</div>"
      "</div>"
    "</div>"
  ));

  // SD
  client.println(F("<div class='row g-3 mt-1'><div class='col-12'>"
                   "<div class='card shadow-sm'>"
                   "<div class='card-header'>Armazenamento</div>"
                   "<div class='card-body'>"));
  printSdStatusHtml(client);
  client.println(F("</div></div></div></div>"));

  // Ações rápidas
  client.println(F(
    "<div class='row g-3 mt-1'>"
      "<div class='col-12'>"
        "<div class='card shadow-sm'>"
          "<div class='card-header'>A&ccedil;&otilde;es r&aacute;pidas</div>"
          "<div class='card-body'>"
            "<div class='d-flex flex-wrap gap-2'>"
              "<a class='btn btn-primary' href='/historico'>Abrir Hist&oacute;rico</a>"
              "<a class='btn btn-secondary' href='/export'>Exportar/Limpar</a>"
              "<a class='btn btn-warning' href='/calibracao'>Calibrar</a>"
              "<a class='btn btn-outline-dark' target='_blank' href='/ws/log?hours=24'>JSON 24h</a>"
              "<a class='btn btn-outline-dark' target='_blank' href='/ws/temperatura'>JSON agora</a>"
            "</div>"
          "</div>"
        "</div>"
      "</div>"
    "</div>"
  ));

  // Script de atualização T/H
  client.println(F(
    "</main>"
    "<script>"
    "function upd(){"
      "fetch('/ws/temperatura',{cache:'no-store'})"
        ".then(r=>r.json())"
        ".then(j=>{"
          "const t=document.getElementById('mTemp');"
          "const h=document.getElementById('mHum');"
          "if(j && j.temperature!=null){t.textContent=(+j.temperature).toFixed(2)+' \\u00B0C';}"
          "else{t.textContent='-- \\u00B0C'}"
          "if(j && j.humidity!=null){h.textContent=(+j.humidity).toFixed(2)+' %';}"
          "else{h.textContent='-- %'}"
        "})"
        ".catch(()=>{});"
    "}"
    "upd(); setInterval(upd,3000);"
    "</script>"
    "<script src='https://cdn.jsdelivr.net/npm/bootstrap@5.3.3/dist/js/bootstrap.bundle.min.js'></script>"
    "</body></html>"
  ));
}

void handleExportPage(EthernetClient &client){
  sendHtmlHeader(client);
  client.println(F("<!doctype html><html lang='pt-br'><head><meta charset='utf-8'>"
                   "<meta name='viewport' content='width=device-width, initial-scale=1'>"
                   "<title>Exportar/Limpar</title>"
                   "<link href='https://cdn.jsdelivr.net/npm/bootstrap@5.3.3/dist/css/bootstrap.min.css' rel='stylesheet'></head><body class='p-3'>"
                   "<h3>Exportar/Limpar</h3>"
                   "<p>UI completa conforme sua vers&atilde;o pode ser expandida aqui. Use /ws/log e o nome do arquivo do m&ecirc;s (Lyyyymm.CSV) no SD.</p>"
                   "<p><a href='/' class='btn btn-secondary'>Voltar</a></p>"
                   "</body></html>"));
}

void handleHistoricoPage(EthernetClient &client){
  sendHtmlHeader(client);
  client.println(F("<!doctype html><html lang='pt-br'><head><meta charset='utf-8'>"
                   "<meta name='viewport' content='width=device-width, initial-scale=1'>"
                   "<title>Hist&oacute;rico</title>"
                   "<link href='https://cdn.jsdelivr.net/npm/bootstrap@5.3.3/dist/css/bootstrap.min.css' rel='stylesheet'></head><body class='p-3'>"
                   "<h3>Hist&oacute;rico</h3>"
                   "<p>Gr&aacute;ficos por m&ecirc;s (Chart.js) podem ser carregados a partir de /ws/log?yyyymm=YYYYMM.</p>"
                   "<p><a href='/' class='btn btn-secondary'>Voltar</a></p>"
                   "</body></html>"));
}

void handleCalibracaoPage(EthernetClient &client, const String &query){
  if (query.length()){
    handleSetCalibracao(client, query);
    return;
  }
  sendHtmlHeader(client);
  client.println(F("<!doctype html><html lang='pt-br'><head><meta charset='utf-8'>"
                   "<meta name='viewport' content='width=device-width, initial-scale=1'>"
                   "<title>Calibra&ccedil;&atilde;o</title>"
                   "<link href='https://cdn.jsdelivr.net/npm/bootstrap@5.3.3/dist/css/bootstrap.min.css' rel='stylesheet'></head><body class='p-3'>"
                   "<h3>Calibra&ccedil;&atilde;o</h3>"
                   "<p>Use /ws/calib com parametros (kt,at,kh,ah) ou modo=two com pontos (raw/ref) para aplicar.</p>"
                   "<p><a href='/' class='btn btn-secondary'>Voltar</a></p>"
                   "</body></html>"));
}

void handleSetCalibracao(EthernetClient &client, const String &query){
  bool changed=false;
  String mode = getQueryParam(query,"mode");
  NetConfig nc = cfg;

  if (mode == "two"){
    auto g = [&](const char* k)->float{
      String s = getQueryParam(query, k); s.replace(',', '.');
      return s.length()? s.toFloat() : NAN;
    };
    float raw1t=g("raw1t"), ref1t=g("ref1t"), raw2t=g("raw2t"), ref2t=g("ref2t");
    float raw1h=g("raw1h"), ref1h=g("ref1h"), raw2h=g("raw2h"), ref2h=g("ref2h");
    auto solve2p = [](float r1,float R1,float r2,float R2, float &K,float &A)->bool{
      if (!isfinite(r1)||!isfinite(R1)||!isfinite(r2)||!isfinite(R2)) return false;
      float dr = (r2 - r1); if (fabsf(dr) < 1e-6f) return false;
      K = (R2 - R1) / dr; A = R1 - K * r1; return isfinite(K) && isfinite(A);
    };
    float k,a;
    if (solve2p(raw1t,ref1t,raw2t,ref2t,k,a)){ nc.kTemp=k; nc.aTemp=a; changed=true; }
    if (solve2p(raw1h,ref1h,raw2h,ref2h,k,a)){ nc.kHum =k; nc.aHum =a; changed=true; }
    Serial.println(F("[CAL] Wizard 2 pontos aplicado."));
  } else {
    auto parse = [&](const char* key, float &dst)->bool{
      String s=getQueryParam(query,key); if(!s.length()) return false;
      s.replace(',', '.'); dst = s.toFloat(); return true;
    };
    changed |= parse("kt", nc.kTemp);
    changed |= parse("at", nc.aTemp);
    changed |= parse("kh", nc.kHum);
    changed |= parse("ah", nc.aHum);
    Serial.println(F("[CAL] Ajuste direto k/a recebido."));
  }

  String reset = getQueryParam(query,"reset");
  if (reset=="1"){
    nc.kTemp=1.0f; nc.aTemp=0.0f;
    nc.kHum =1.0f; nc.aHum =0.0f;
    changed = true;
    Serial.println(F("[CAL] Zerar (identidade)."));
  }

  sendJsonHeader(client);
  if (changed){
    nc.magic = CFG_MAGIC; nc.checksum = calcChecksum(nc);
    cfg = nc; saveConfig(cfg);
    Serial.println(F("[CFG] Calibracao salva na EEPROM."));
    client.print(F("{\"ok\":true,"));
  } else {
    Serial.println(F("[CAL] Nada alterado."));
    client.print(F("{\"ok\":false,"));
  }
  client.print(F("\"kTemp\":")); client.print(cfg.kTemp,6);
  client.print(F(",\"aTemp\":")); client.print(cfg.aTemp,6);
  client.print(F(",\"kHum\":"));  client.print(cfg.kHum,6);
  client.print(F(",\"aHum\":"));  client.print(cfg.aHum,6);
  client.println(F("}"));
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
  unsigned long t0 = millis();
  Serial.println(F("\n[WS/LOG] begin"));
  Serial.print  (F("[WS/LOG] query=")); Serial.println(query);
  Serial.print  (F("[WS/LOG] sdAvailable=")); Serial.println(sdAvailable ? F("true") : F("false"));

  if (!sdAvailable) { Serial.println(F("[WS/LOG] 404 SD")); sendNotFound(client); return; }

  sendJsonHeader(client);

  String v_hours = getQueryParam(query, "hours");
  String v_ym    = getQueryParam(query, "yyyymm");

  if (v_hours.length()) {
    unsigned long hrs = (unsigned long)v_hours.toInt();
    if (hrs == 0) hrs = 24;
    unsigned long nowEpoch = getEpochUTC();
    Serial.print(F("[WS/LOG] mode=hours hrs=")); Serial.print(hrs);
    Serial.print(F(" epochNow=")); Serial.println(nowEpoch);

    if (nowEpoch == 0UL) { Serial.println(F("[WS/LOG] epoch=0 -> []")); client.println(F("[]")); return; }

    unsigned long minEpoch = (nowEpoch > hrs*3600UL) ? (nowEpoch - hrs*3600UL) : 0UL;
    Serial.print(F("[WS/LOG] minEpoch=")); Serial.println(minEpoch);
    streamCsvAsJson(client, minEpoch, 0, -1, true);
    Serial.print(F("[WS/LOG] ms=")); Serial.println(millis()-t0);
    Serial.println(F("[WS/LOG] end\n"));
    return;
  }

  auto isSixDigits = [](const String& s)->bool{
    if (s.length() != 6) return false;
    for (uint8_t i=0;i<6;i++){ char c = s[i]; if (c<'0'||c>'9') return false; }
    return true;
  };

  if (isSixDigits(v_ym)) {
    uint32_t yyyymm = (uint32_t)v_ym.toInt();
    Serial.print(F("[WS/LOG] mode=yyyymm explicit=")); Serial.println((unsigned long)yyyymm);
    streamMonthCsvAsJson(client, yyyymm);
    Serial.print(F("[WS/LOG] ms=")); Serial.println(millis()-t0);
    Serial.println(F("[WS/LOG] end\n"));
    return;
  }

  uint32_t fallbackYm = currentYYYYMM(getEpochUTC());
  if (fallbackYm == 0UL) {
    digitalWrite(PIN_CS_ETH, HIGH);
    File root = SD.open("/");
    uint32_t best = 0;
    if (root) {
      File entry = root.openNextFile();
      while (entry) {
        if (!entry.isDirectory()) {
          char nm[32] = {0}; entry.getName(nm, sizeof(nm));
          size_t len = strlen(nm);
          if (len == 11 && nm[0]=='L' && nm[7]=='.' && nm[8]=='C' && nm[9]=='S' && nm[10]=='V') {
            bool ok = true;
            for (int i=1;i<=6;i++){ if (nm[i]<'0'||nm[i]>'9'){ ok=false; break; } }
            if (ok) {
              char buf[7]; memcpy(buf, nm+1, 6); buf[6]='\0';
              uint32_t val = (uint32_t)atoi(buf);
              if (val > best) best = val;
            }
          }
        }
        entry.close();
        entry = root.openNextFile();
      }
      root.close();
    }
    digitalWrite(PIN_CS_SD, HIGH);
    fallbackYm = best;
  }

  if (fallbackYm > 0) {
    Serial.print(F("[WS/LOG] usando atual/mais recente: "));
    Serial.println((unsigned long)fallbackYm);
    streamMonthCsvAsJson(client, fallbackYm);
  } else {
    Serial.println(F("[WS/LOG] nenhum mes -> []"));
    client.println(F("[]"));
  }
  Serial.print(F("[WS/LOG] ms=")); Serial.println(millis()-t0);
  Serial.println(F("[WS/LOG] end\n"));
}

// ======== Router HTTP ========
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

  Serial.print(F("[HTTP] ")); Serial.print(reqLine);
  Serial.print(F("[HTTP] path=")); Serial.print(path);
  Serial.print(F(" query=")); Serial.println(query);

  if(path=="/" || path=="/index.html") handleRootPage(client, query);
  else if(path=="/ws/temperatura")     handleJsonNow(client);
  else if(path=="/dht")                handleJsonNow(client);
  else if(path=="/ws/log")             handleJsonLog(client, query);
  else if(path=="/historico")          handleHistoricoPage(client);
  else if(path=="/export")             handleExportPage(client);
  else if(path=="/calibracao")         handleCalibracaoPage(client, query);
  else if(path=="/ws/calib")           handleSetCalibracao(client, query);
  else if(path=="/ws/clear") {
    sendHtmlHeader(client);
    client.println(F("<!doctype html><html><body class='p-3'>"
                     "<h4>Limpeza via UI</h4><p>Use a p&aacute;gina <a href='/export'>/export</a> para limpar um m&ecirc;s.</p>"
                     "<p><a href='/'>&larr; Voltar</a></p></body></html>"));
  }
  else sendNotFound(client);
}

// ======== Setup / Loop ========
void setup(){
  Serial.begin(115200);
  while(!Serial){} // alguns boards

  Serial.println();
  Serial.println(F("==============================================="));
  Serial.println(F("   mega temp02  -  Boot"));
  Serial.println(F("==============================================="));

  // Buzzer/LED/DHT
  pinMode(PIN_BUZZ_G, OUTPUT); digitalWrite(PIN_BUZZ_G, LOW);
  pinMode(PIN_BUZZER, OUTPUT); noTone(PIN_BUZZER);
  pinMode(PIN_LED, OUTPUT); digitalWrite(PIN_LED, LOW);
  pinMode(DHTPIN, INPUT_PULLUP);
  dht.begin(); dhtWarmupUntil = millis() + 2000;
  Serial.println(F("[DHT] Inicializado. Aguardando aquecimento..."));

  // LCD
  lcd.init(); lcd.backlight(); lcd.clear();
  lcd.setCursor(0,0); lcd.print(F("Inicializando..."));

  // Config
  if(!loadConfig(cfg)){
    Serial.println(F("[CFG] EEPROM invalida/ausente -> defaults."));
    loadDefaults(cfg); 
    saveConfig(cfg);
  } else {
    Serial.println(F("[CFG] EEPROM carregada."));
  }

  // Rede
  Serial.println(F("[NET] Subindo interface..."));
  applyNetworkFromConfig();

  // UDP/NTP inicial
  Serial.println(F("[NTP] Sincronizando..."));
  ntpSyncNow();

  // SD
  sdInit();

  // Splash IP
  lcd.clear(); printIPLine();
  lcd.setCursor(0,1); lcd.print("                ");
  ipSplashStartMs = millis(); ipSplashDone=false;

  // Som inicial
  startupChime();

  Serial.println(F("[BOOT] Finalizado."));
}

void sampleSensorIfNeeded(){
  unsigned long now=millis();
  if(now<dhtWarmupUntil) return;
  if(now - lastSampleMs < sampleIntervalMs) return;
  lastSampleMs = now;

  digitalWrite(PIN_LED, HIGH);

  float t=dht.readTemperature(), h=dht.readHumidity();
  if(!isnan(t) && !isnan(h)){
    lastTemp = applyCalTemp(t);
    lastHum  = applyCalHum(h);

    #if SERIAL_VERBOSE
      Serial.print(F("[DHT] T=")); Serial.print(t,1);
      Serial.print(F("C -> cal=")); Serial.print(lastTemp,1);
      Serial.print(F("C | H="));    Serial.print(h,1);
      Serial.print(F("% -> cal=")); Serial.print(lastHum,1);
      Serial.println(F("%"));
    #endif

    if(wasBelowThreshold && lastTemp>=THRESH_C){ beep(1800,180); wasBelowThreshold=false; }
    else if(lastTemp<THRESH_C){ wasBelowThreshold=true; }

    if (sdAvailable) (void)sdAppendLog(lastTemp, lastHum);

    if(ipSplashDone){ printIPLine(); printTHLine(); }
  } else {
    #if SERIAL_VERBOSE
      Serial.println(F("[DHT] Leitura invalida (NaN)."));
    #endif
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
  if (millis() - lastNtpSyncMs > NTP_INTERVAL_MS) {
    Serial.println(F("[NTP] Re-sincronizando..."));
    ntpSyncNow();
  }

  // DHCP lease maintain (apenas em DHCP)
  if (!cfg.use_static) {
    int rc = Ethernet.maintain(); // 1-renew fail, 2-renew OK, 3-rebind fail, 4-rebind OK
    if (rc == 1) Serial.println(F("[NET] DHCP renew FAIL"));
    else if (rc == 2) { Serial.println(F("[NET] DHCP renew OK")); printIPToSerial(); }
    else if (rc == 3) Serial.println(F("[NET] DHCP rebind FAIL"));
    else if (rc == 4) { Serial.println(F("[NET] DHCP rebind OK")); printIPToSerial(); }
  }

  sampleSensorIfNeeded();

  // HTTP
  EthernetClient client = server.available();
  if(client){ handleHttp(client); delay(1); client.stop(); }
}

// Retorna YYYYMM atual (via NTP) ou mais recente no SD (fallback).
uint32_t getCurrentOrLatestYYYYMM() {
  uint32_t yyyymm = currentYYYYMM(getEpochUTC());
  if (yyyymm != 0UL) return yyyymm;
  if (!sdAvailable) return 0UL;
  digitalWrite(PIN_CS_ETH, HIGH);
  File root = SD.open("/");
  uint32_t best = 0;
  if (root) {
    File entry = root.openNextFile();
    while (entry) {
      if (!entry.isDirectory()) {
        char nm[32] = {0};
        entry.getName(nm, sizeof(nm));
        size_t len = strlen(nm);
        if (len == 11 && nm[0] == 'L' && nm[7] == '.' && nm[8] == 'C' && nm[9] == 'S' && nm[10] == 'V') {
          bool ok = true;
          for (int i = 1; i <= 6; i++) if (nm[i] < '0' || nm[i] > '9') ok = false;
          if (ok) {
            char buf[7]; memcpy(buf, nm + 1, 6); buf[6] = '\0';
            uint32_t val = (uint32_t)atoi(buf);
            if (val > best) best = val;
          }
        }
      }
      entry.close();
      entry = root.openNextFile();
    }
    root.close();
  }
  digitalWrite(PIN_CS_SD, HIGH);
  return best;
}
