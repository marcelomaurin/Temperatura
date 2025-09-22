#include <DHT.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <SPI.h>
#include <Ethernet.h>
#include <EEPROM.h>
#include <EthernetUdp.h>
#include <Dns.h>   // <-- ADD
#include <math.h>

#include <SdFat.h>
SdFat    SD;       // objeto principal para SD (segue usando SD.open/exists/etc.)

SdCard   g_sdCard;   // correto na SdFat v2
FsVolume g_sdVol;    // correto na SdFat v2

// Último valor **gravado** no CSV
float lastLoggedTemp = NAN;
float lastLoggedHum  = NAN;

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
  uint8_t  magic;       // 0x42
  uint8_t  use_static;  // 0/1
  uint8_t  ip[4];
  uint8_t  dns[4];
  uint8_t  gw[4];
  uint8_t  mask[4];
  uint8_t  mac[6];

  // ===== Calibração =====
  // y_cal = k * y_raw + a
  float    kTemp;   // ganho temperatura
  float    aTemp;   // offset temperatura
  float    kHum;    // ganho umidade
  float    aHum;    // offset umidade

  uint8_t  checksum;    // soma simples (exceto checksum)
};

const uint8_t CFG_MAGIC = 0x42;
const int EEPROM_ADDR = 0;


NetConfig cfg;


// ======== Calibração: aplica (k,a) ========
// Retorna y_cal = k*y + a. Se y for NaN, mantém NaN.
static inline float applyCalTemp(float raw){ return isnan(raw) ? raw : (cfg.kTemp*raw + cfg.aTemp); }
static inline float applyCalHum (float raw){ return isnan(raw) ? raw : (cfg.kHum *raw + cfg.aHum ); }


// ======== SD / Log ========
bool sdAvailable = false;
char currentMonthFile[20] = {0}; // e.g. "L202509.CSV"
volatile bool sdBusy = false;    // <-- trava escrita durante export/export/clear

// === Helpers padronizados para nome de arquivo de mês ===
// Gera "LYYYYMM.CSV" a partir de (ano, mes)
static inline void formatMonthFilenameFromYM(uint16_t year, uint8_t month,
                                             char* out, size_t sz) {
  if (month < 1)  month = 1;
  if (month > 12) month = 12;
  // Sempre "LYYYYMM.CSV" — SEM dia.
  snprintf(out, sz, "L%04u%02u.CSV", (unsigned)year, (unsigned)month);
}

// Gera "LYYYYMM.CSV" a partir de YYYYMM (ex.: 202509)
static inline void formatMonthFilenameFromYYYYMM(uint32_t yyyymm,
                                                 char* out, size_t sz) {
  uint16_t year  = (uint16_t)(yyyymm / 100U);
  uint8_t  month = (uint8_t)(yyyymm % 100U);
  formatMonthFilenameFromYM(year, month, out, sz);
}


bool getSdCapacityAndFree(uint64_t &totalBytes, uint64_t &freeBytes) {
  if (!sdAvailable) return false;

  // Garante Ethernet desabilitada no SPI
  digitalWrite(PIN_CS_ETH, HIGH);

  // (Re)inicializa card/volume com a mesma config usada no resto do código
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

  // Solta o SD
  digitalWrite(PIN_CS_SD, HIGH);
  return true;
}





bool resolveHostname(const char* host, IPAddress& outIP) {
  IPAddress dnsIP = Ethernet.dnsServerIP();
  if (dnsIP == IPAddress(0,0,0,0)) {
    // fallback se não houver DNS configurado
    dnsIP = IPAddress(DEFAULT_DNS[0], DEFAULT_DNS[1], DEFAULT_DNS[2], DEFAULT_DNS[3]);
  }
  DNSClient dns;
  dns.begin(dnsIP);
  int rc = dns.getHostByName(host, outIP);     // 1 = sucesso
  return (rc == 1);
}


// ---------- Utils: EEPROM ----------
uint8_t calcChecksum(const NetConfig &c){
  const uint8_t *p = (const uint8_t*)&c;
  // Soma todos os bytes EXCETO o último (checksum)
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

  // Calibração default (identidade)
  c.kTemp = 1.0f;  c.aTemp = 0.0f;
  c.kHum  = 1.0f;  c.aHum  = 0.0f;

  c.checksum = calcChecksum(c);
}


bool loadConfig(NetConfig &c){
  EEPROM.get(EEPROM_ADDR, c);
  if(c.magic!=CFG_MAGIC) return false;
  if(c.checksum!=calcChecksum(c)) return false;
  // Sanidade mínima: evitar NaN/inf
  if (!isfinite(c.kTemp)) c.kTemp=1.0f;
  if (!isfinite(c.aTemp)) c.aTemp=0.0f;
  if (!isfinite(c.kHum )) c.kHum =1.0f;
  if (!isfinite(c.aHum )) c.aHum =0.0f;
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

// --- Helpers: tamanho fixo em KB (arredondado p/ cima) ---
static inline String humanKB(unsigned long long bytes) {
  unsigned long kb = (unsigned long)((bytes + 1023ULL) / 1024ULL);
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

  // Handoff SPI e varredura de arquivos
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
        char nm[64];                 // buffer para o nome
        entry.getName(nm, sizeof(nm)); // <-- API nova (substitui entry.name())

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

  // Capacidade e livre (se conseguirmos medir)
  uint64_t capBytes = 0, freeBytes = 0;
  bool okSpace = getSdCapacityAndFree(capBytes, freeBytes);

  // Resumo
  client.print(F("<div class='small text-muted'>Arquivos: "));
  client.print(totalFiles);
  client.print(F(" &middot; Ocupa&ccedil;&atilde;o total: "));
  client.print(humanKB(totalBytesUsed));
  client.println(F("</div>"));

  client.print(F("<div class='small text-muted'>Espa&ccedil;o livre: "));
  if (okSpace) client.print(humanKB(freeBytes));
  else         client.print(F("N/D"));
  client.println(F("</div>"));

  // Libera CS do SD
  digitalWrite(PIN_CS_SD, HIGH);
}



// ---------- Rede ----------
void applyNetworkFromConfig(){
  // Linha SS do MCU deve ser saída e ficar em HIGH para não virar “SPI slave”
  #if defined(SS)
    pinMode(SS, OUTPUT);
    digitalWrite(SS, HIGH);
  #else
    // (compat) no Mega era 53; manter HIGH também
    pinMode(53, OUTPUT);
    digitalWrite(53, HIGH);
  #endif

  // CS dos periféricos (inativos em HIGH)
  pinMode(PIN_CS_ETH, OUTPUT);
  pinMode(PIN_CS_SD,  OUTPUT);
  digitalWrite(PIN_CS_ETH, HIGH);
  digitalWrite(PIN_CS_SD,  HIGH);

  // MAC
  uint8_t macLocal[6];
  for (int i=0;i<6;i++) macLocal[i]=cfg.mac[i];

  // Sobe rede
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

// antes:
// void buildMonthFilename(int yyyymm, char* out, size_t sz) {
//   snprintf(out, sz, "L%06d.CSV", yyyymm);
// }
/*
void buildMonthFilename(uint32_t yyyymm, char* out, size_t sz) {
  // em AVR, unsigned long == 32 bits
  snprintf(out, sz, "L%06lu.CSV", (unsigned long)yyyymm);
}
*/



// Pega YYYYMM (UTC) do epoch atual
uint32_t currentYYYYMM(unsigned long epochUTC){
  if (epochUTC == 0) return 0UL;
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
  if (leap) md[1] = 29;
  int m = 0;
  while (m<12 && d >= (unsigned long)md[m]) { d -= md[m]; m++; }
  return (uint32_t)y*100UL + (uint32_t)(m+1);  // ex.: 202509
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
// Agora RETORNA true/false. Se epochUTC == 0, NÃO altera nada e retorna false.
bool updateMonthFileName(unsigned long epochUTC){
  if (epochUTC == 0) {
    return false; // sem NTP -> não muda currentMonthFile
  }

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

bool sdAppendLog(float t, float h) {
  if (!sdAvailable) return false;

  // Não deixa disputar com export/clear
  if (sdBusy) return false;
  sdBusy = true;

  bool firstLog = isnan(lastLoggedTemp) || isnan(lastLoggedHum);
  bool dt = !firstLog && (fabsf(t - lastLoggedTemp) >= 1.0f);
  bool dh = !firstLog && (fabsf(h - lastLoggedHum ) >= 1.0f);
  if (!firstLog && !dt && !dh) { sdBusy=false; return false; }

  unsigned long epoch = getEpochUTC();
  if (epoch == 0) { sdBusy=false; return false; }

  // Desabilita Ethernet no barramento antes de mexer no SD
  digitalWrite(PIN_CS_ETH, HIGH);

  if (!updateMonthFileName(epoch)) { sdBusy=false; return false; }
  char fname[20];
  strncpy(fname, currentMonthFile, sizeof(fname));
  fname[sizeof(fname)-1] = '\0';

  bool newFile = !SD.exists(fname);
  File f = SD.open(fname, FILE_WRITE);
  if (!f) {
    Serial.print(F("[SD] Falha ao abrir ")); Serial.println(fname);
    digitalWrite(PIN_CS_SD, HIGH);
    sdBusy=false;
    return false;
  }

  if (newFile) f.println(F("epoch,temperature,humidity"));
  f.print(epoch); f.print(','); f.print(t, 2); f.print(','); f.println(h, 2);
  f.flush();
  f.close();

  // Atualiza “último gravado” **apenas** após sucesso
  lastLoggedTemp = t;
  lastLoggedHum  = h;

  digitalWrite(PIN_CS_SD, HIGH);
  sdBusy = false;

  Serial.print(F("[SD] Append ")); Serial.print(fname);
  Serial.print(F(" -> ")); Serial.print(t,2);
  Serial.print(F("C, ")); Serial.print(h,2);
  Serial.println(F("%"));

  return true;
}


// Itera linhas do CSV e manda JSON filtrado
// yearMonth: yyyymm ou -1 para "mês atual + anterior"
// debugLogs: quando true, imprime métricas no Serial
void streamCsvAsJson(EthernetClient &client,
                     unsigned long minEpoch,
                     unsigned long maxEpoch,
                     int yearMonth,
                     bool debugLogs = false) {
  unsigned long t0 = millis();
  if (!sdAvailable) { client.println(F("[]")); return; }

  // ---- SPI handoff: desabilita Ethernet p/ operar SD ----
  digitalWrite(PIN_CS_ETH, HIGH);

  bool first = true;
  client.print('[');

  // Métricas de debug (opcional)
  unsigned long filesTried = 0, filesOpened = 0;
  unsigned long linesTotal = 0, linesParsed = 0, linesEmitted = 0;

  auto streamFile = [&](const char* fname){
    // Se o arquivo não existe, apenas informa (se debug) e sai
    if (!SD.exists(fname)) {
      if (debugLogs) {
        Serial.print(F("[WS/LOG] arquivo inexistente: "));
        Serial.println(fname);
      }
      return;
    }

    filesTried++;
    File f = SD.open(fname, FILE_READ);
    if (!f) {
      if (debugLogs) {
        Serial.print(F("[WS/LOG] falha ao abrir: "));
        Serial.println(fname);
      }
      return;
    }
    filesOpened++;

    String line;
    while (f.available()) {
      char c = f.read();
      if (c=='\n' || c=='\r') {
        if (line.length()>0) {
          linesTotal++;
          // pular cabeçalho
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
                client.print(F(",\"temperature\":")); client.print(st);
                client.print(F(",\"humidity\":")); client.print(sh);
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
    snprintf(fname, sizeof(fname), "L%06d.CSV", yearMonth);
    if (debugLogs) { Serial.print(F("[WS/LOG] modo yyyymm, arquivo=")); Serial.println(fname); }
    streamFile(fname);
  } else {
    unsigned long nowEpoch = getEpochUTC();
    updateMonthFileName(nowEpoch);
    char cur[20]; strcpy(cur, currentMonthFile);

    unsigned long prevEpoch = (nowEpoch > 31UL*86400UL) ? (nowEpoch - 31UL*86400UL) : 0UL;
    updateMonthFileName(prevEpoch);
    char prev[20]; strcpy(prev, currentMonthFile);

    // restaura nome do mês atual
    updateMonthFileName(nowEpoch);

    if (debugLogs) {
      Serial.print(F("[WS/LOG] lendo prev=")); Serial.print(prev);
      Serial.print(F(" cur=")); Serial.println(cur);
      Serial.print(F("[WS/LOG] filtro minEpoch=")); Serial.print(minEpoch);
      Serial.print(F(" maxEpoch=")); Serial.println(maxEpoch);
    }

    // Agora só tenta o que existir
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
        "<li class='nav-item'><a class='nav-link' href='/calibracao'>Calibra&ccedil;&atilde;o</a></li>"  // <-- NOVO
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

// /ws/calib?kt=1.000&at=0.000&kh=1.000&ah=0.000
// Também aceita wizard 2-pontos:
// /ws/calib?mode=two&raw1t=...&ref1t=...&raw2t=...&ref2t=...&raw1h=...&ref1h=...&raw2h=...&ref2h=...
void handleSetCalibracao(EthernetClient &client, const String &query){
  bool changed=false;
  String mode = getQueryParam(query,"mode"); // "" | "two"

  NetConfig nc = cfg;

  if (mode == "two"){
    // ===== Wizard 2 pontos: T =====
    auto g = [&](const char* k)->float{
      String s = getQueryParam(query, k);
      s.replace(',', '.');
      return s.length()? s.toFloat() : NAN;
    };
    float raw1t=g("raw1t"), ref1t=g("ref1t"), raw2t=g("raw2t"), ref2t=g("ref2t");
    float raw1h=g("raw1h"), ref1h=g("ref1h"), raw2h=g("raw2h"), ref2h=g("ref2h");

    auto solve2p = [](float r1,float R1,float r2,float R2, float &K,float &A)->bool{
      if (!isfinite(r1)||!isfinite(R1)||!isfinite(r2)||!isfinite(R2)) return false;
      float dr = (r2 - r1);
      if (fabsf(dr) < 1e-6f) return false;
      K = (R2 - R1) / dr;
      A = R1 - K * r1;
      return isfinite(K) && isfinite(A);
    };

    float k,a;
    if (solve2p(raw1t,ref1t,raw2t,ref2t,k,a)){ nc.kTemp=k; nc.aTemp=a; changed=true; }
    if (solve2p(raw1h,ref1h,raw2h,ref2h,k,a)){ nc.kHum =k; nc.aHum =a; changed=true; }
  } else {
    // ===== Set direto: ganhos/offsets =====
    auto parse = [&](const char* key, float &dst)->bool{
      String s=getQueryParam(query,key); if(!s.length()) return false;
      s.replace(',', '.'); dst = s.toFloat(); return true;
    };
    changed |= parse("kt", nc.kTemp);
    changed |= parse("at", nc.aTemp);
    changed |= parse("kh", nc.kHum);
    changed |= parse("ah", nc.aHum);
  }

  // Reset (identidade)
  String reset = getQueryParam(query,"reset");
  if (reset=="1"){
    nc.kTemp=1.0f; nc.aTemp=0.0f;
    nc.kHum =1.0f; nc.aHum =0.0f;
    changed = true;
  }

  sendJsonHeader(client);
  if (changed){
    nc.magic = CFG_MAGIC;
    nc.checksum = calcChecksum(nc);
    cfg = nc;
    saveConfig(cfg);
    client.print(F("{\"ok\":true,"));
  } else {
    client.print(F("{\"ok\":false,"));
  }
  client.print(F("\"kTemp\":")); client.print(cfg.kTemp,6);
  client.print(F(",\"aTemp\":")); client.print(cfg.aTemp,6);
  client.print(F(",\"kHum\":"));  client.print(cfg.kHum,6);
  client.print(F(",\"aHum\":"));  client.print(cfg.aHum,6);
  client.println(F("}"));
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
      "data:{labels:[],datasets:[{label:'Temperatura (°C)',data:[]}]},"
      "options:chartOptions});"

    "const gH=new Chart(document.getElementById('chartH'),{type:'line',"
      "data:{labels:[],datasets:[{label:'Umidade (%RH)',data:[]}]},"
      "options:chartOptions});"

    // ===== Helpers de mês =====
    "function ymNow(){const d=new Date();const m=('0'+(d.getMonth()+1)).slice(-2);return d.getFullYear()+'-'+m;}"
    "function yyyymmFromInput(v){return (/^\\d{4}-\\d{2}$/).test(v)?v.replace('-',''):null;}"

    // ===== Carrega mês (sempre LYYYYMM.CSV) =====
    "async function loadMonth(yyyymm){"
      "try{const r=await fetch('/ws/log?yyyymm='+yyyymm,{cache:'no-store'});"
      "if(!r.ok){console.warn('Sem dados para o mês', yyyymm);return;}"
      "const j=await r.json(); const d=toSeries(j);"
      "gT.data.labels=d.labels; gT.data.datasets[0].data=d.t; gT.update();"
      "gH.data.labels=d.labels; gH.data.datasets[0].data=d.h; gH.update();"
      "}catch(e){console.error(e);}"
    "}"

    // ===== UI: input mês e botões =====
    "const inp=document.getElementById('yyyymm');"
    "const btnMes=document.getElementById('btnMes');"
    "inp.value = ymNow();"

    "btnMes.addEventListener('click',()=>{"
      "const ym = yyyymmFromInput(inp.value);"
      "if(!ym){alert('Use YYYY-MM');return;}"
      "loadMonth(ym);"
    "});"

    // ===== Inicialização: SEM 24h; SEM janela móvel =====
    "(function init(){"
      "const ym = yyyymmFromInput(ymNow());"
      "if(ym) loadMonth(ym);"
    "})();"
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
  unsigned long t0 = millis();
  Serial.println(F("\n[WS/CSV] ====== handleCsvDownload begin ======"));
  Serial.print  (F("[WS/CSV] query=")); Serial.println(query);
  Serial.print  (F("[WS/CSV] sdAvailable=")); Serial.println(sdAvailable ? F("true") : F("false"));

  if (!sdAvailable) {
    sendHtmlHeader(client);
    client.println(F("<!doctype html><html><head><meta charset='utf-8'><title>Exportar CSV</title></head><body>"));
    client.println(F("<p style='padding:1rem'>SD n&atilde;o dispon&iacute;vel.</p></body></html>"));
    Serial.print(F("[WS/CSV] total(ms)=")); Serial.println(millis()-t0);
    Serial.println(F("[WS/CSV] ====== handleCsvDownload end ======\n"));
    return;
  }

  String v_ym = getQueryParam(query, "yyyymm");
  Serial.print(F("[WS/CSV] yyyymm(param)=")); Serial.println(v_ym);

  char fname[20];
  if (v_ym.length() == 6) {
    uint32_t yyyymm = (uint32_t)v_ym.toInt();
    //buildMonthFilename(yyyymm, fname, sizeof(fname));
    formatMonthFilenameFromYYYYMM(yyyymm, fname, sizeof(fname));

    Serial.print(F("[WS/CSV] usando yyyymm informado -> ")); Serial.println(fname);
  } else {
    uint32_t yyyymm = currentYYYYMM(getEpochUTC());
    if (yyyymm == 0UL) {
      sendHtmlHeader(client);
      client.println(F("<!doctype html><html><head><meta charset='utf-8'><title>Exportar CSV</title></head><body>"));
      client.println(F("<p style='padding:1rem'>Sem data/hora v&aacute;lida (NTP). "
                       "N&atilde;o h&aacute; m&ecirc;s atual para exportar.</p></body></html>"));
      Serial.print(F("[WS/CSV] total(ms)=")); Serial.println(millis()-t0);
      Serial.println(F("[WS/CSV] ====== handleCsvDownload end ======\n"));
      return;
    }
    //buildMonthFilename(yyyymm, fname, sizeof(fname));
    formatMonthFilenameFromYYYYMM(yyyymm, fname, sizeof(fname));

    Serial.print(F("[WS/CSV] yyyymm atual (NTP) -> ")); Serial.println(fname);
  }

  if (!SD.exists(fname)) {
    sendHtmlHeader(client);
    client.print(F("<!doctype html><html><head><meta charset='utf-8'><title>Exportar CSV</title></head><body>"));
    client.print(F("<p style='padding:1rem'>Arquivo inexistente para o per&iacute;odo solicitado: <code>"));
    client.print(fname);
    client.println(F("</code></p></body></html>"));
    Serial.print(F("[WS/CSV] total(ms)=")); Serial.println(millis()-t0);
    Serial.println(F("[WS/CSV] ====== handleCsvDownload end ======\n"));
    return;
  }

  // --------- LOCK REAL do SD ---------
  if (sdBusy) {
    sendHtmlHeader(client);
    client.println(F("<!doctype html><html><head><meta charset='utf-8'><title>Exportar CSV</title></head><body>"));
    client.println(F("<p style='padding:1rem'>SD est&aacute; em uso. Tente novamente em instantes.</p></body></html>"));
    Serial.println(F("[WS/CSV] sdBusy=true -> negando export no momento"));
    return;
  }
  sdBusy = true;

  digitalWrite(PIN_CS_ETH, HIGH);
  delay(1);

  File f = SD.open(fname, FILE_READ);
  if (!f) {
    sdBusy = false;                       // desbloqueia antes de sair
    digitalWrite(PIN_CS_SD, HIGH);
    sendHtmlHeader(client);
    client.println(F("<!doctype html><html><head><meta charset='utf-8'><title>Exportar CSV</title></head><body>"));
    client.println(F("<p style='padding:1rem'>Falha ao abrir o arquivo no SD.</p></body></html>"));
    Serial.print(F("[WS/CSV] total(ms)=")); Serial.println(millis()-t0);
    Serial.println(F("[WS/CSV] ====== handleCsvDownload end ======\n"));
    return;
  }

  const unsigned long expectedSize = f.size();

  client.println(F("HTTP/1.1 200 OK"));
  client.println(F("Content-Type: text/csv; charset=utf-8"));
  client.print  (F("Content-Disposition: attachment; filename=\""));
  client.print  (fname);
  client.println(F("\""));
  client.println(F("Connection: close"));
  client.println();

  const size_t BUFSZ = 512;
  uint8_t buf[BUFSZ];
  unsigned long totalBytes = 0;
  unsigned long lastProgressMs = millis();
  const unsigned long MAX_IDLE_MS  = 3000;
  const unsigned long MAX_TOTAL_MS = 120000;
  unsigned long startMs = millis();

  while (f.available()) {
    if (!client.connected()) break;

    int avail  = f.available();
    int toRead = (avail > (int)BUFSZ) ? (int)BUFSZ : avail;
    int n = f.read(buf, toRead);

    if (n > 0) {
      client.write(buf, (size_t)n);
      totalBytes += (unsigned long)n;
      lastProgressMs = millis();
    } else {
      delay(2);
      if (millis() - lastProgressMs > MAX_IDLE_MS) break;
    }
    if (millis() - startMs > MAX_TOTAL_MS) break;
  }

  f.close();
  digitalWrite(PIN_CS_SD, HIGH);
  sdBusy = false;                         // **sempre** destrava ao final

  Serial.print(F("[WS/CSV] bytes enviados=")); Serial.println(totalBytes);
  Serial.print(F("[WS/CSV] esperado="));       Serial.println(expectedSize);
  Serial.print(F("[WS/CSV] total(ms)="));      Serial.println(millis()-t0);
  Serial.println(F("[WS/CSV] ====== handleCsvDownload end ======\n"));
}


// ======================== CLEAR (com debug no Serial) =========================
void handleCsvClear(EthernetClient &client, const String &query) {
  unsigned long t0 = millis();
  Serial.println(F("\n[CLR] ==== Inicio handleCsvClear ===="));
  Serial.print  (F("[CLR] query=")); Serial.println(query);
  Serial.print  (F("[CLR] sdAvailable=")); Serial.println(sdAvailable ? F("true") : F("false"));
  Serial.print  (F("[CLR] sdBusy="));      Serial.println(sdBusy ? F("true") : F("false"));

  if (!sdAvailable) {
    sendHtmlHeader(client);
    client.println(F("<!doctype html><html><head><meta charset='utf-8'><title>Limpar SD</title></head><body>"));
    client.println(F("<p style='padding:1rem'>SD n&atilde;o dispon&iacute;vel.</p></body></html>"));
    return;
  }

  if (sdBusy) {
    sendHtmlHeader(client);
    client.println(F("<!doctype html><html><head><meta charset='utf-8'><title>Limpar SD</title></head><body>"));
    client.println(F("<p style='padding:1rem'>SD est&aacute; em uso. Tente novamente em instantes.</p></body></html>"));
    Serial.println(F("[CLR] sdBusy=true -> negando limpeza agora"));
    return;
  }

  // Vai limpar: impede que novos logs gravem enquanto remove
  sdBusy = true;

  lastLoggedTemp = NAN;
  lastLoggedHum  = NAN;

  String v_ym = getQueryParam(query, "yyyymm");
  char fname[20];
  uint32_t yyyymm;

  if (v_ym.length() == 6) {
    yyyymm = (uint32_t)v_ym.toInt();
  } else {
    yyyymm = currentYYYYMM(getEpochUTC());
    if (yyyymm == 0UL) {
      sdBusy = false;
      sendHtmlHeader(client);
      client.println(F("<!doctype html><html><head><meta charset='utf-8'><title>Limpar SD</title></head><body>"));
      client.println(F("<p style='padding:1rem'>Sem data/hora v&aacute;lida (NTP). "
                       "N&atilde;o h&aacute; m&ecirc;s atual para limpar.</p></body></html>"));
      return;
    }
  }
  //buildMonthFilename(yyyymm, fname, sizeof(fname));
  formatMonthFilenameFromYYYYMM(yyyymm, fname, sizeof(fname));


  digitalWrite(PIN_CS_ETH, HIGH);
  delay(2);

  bool existed = SD.exists(fname);
  bool ok = false;
  if (existed) {
    ok = SD.remove(fname);
  }

  digitalWrite(PIN_CS_SD, HIGH);
  sdBusy = false; // destrava

  sendHtmlHeader(client);
  client.println(F("<!doctype html><html><head><meta charset='utf-8'><title>Limpar SD</title>"
                   "<link href='https://cdn.jsdelivr.net/npm/bootstrap@5.3.3/dist/css/bootstrap.min.css' rel='stylesheet'>"
                   "</head><body class='p-3'><div class='container'>"));
  client.print(F("<h4>Limpar SD - <code>")); client.print(fname); client.println(F("</code></h4>"));

  if (!existed) {
    client.println(F("<div class='alert alert-warning'>Arquivo n&atilde;o existia.</div>"));
  } else if (ok) {
    client.println(F("<div class='alert alert-success'>Arquivo removido.</div>"));
  } else {
    client.println(F("<div class='alert alert-danger'>Falha ao remover o arquivo.</div>"));
  }

  client.println(F("<a class='btn btn-secondary' href='/export'>Voltar</a></div></body></html>"));

  Serial.print(F("[CLR] Fim handleCsvClear. Duracao(ms)="));
  Serial.println(millis() - t0);
  Serial.println(F("[CLR] ===================================\n"));
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



// ===== Helper: mostra progresso no LCD (16x2) =====
// Coloque este bloco APENAS uma vez no projeto (por exemplo, acima de streamMonthCsvAsJson)
#ifndef LCD_PROGRESS_HELPER_DEFINED
#define LCD_PROGRESS_HELPER_DEFINED

static void lcdShowProgress(const char* label, uint32_t done, uint32_t total) {
  if (total == 0) total = 1;
  const uint8_t cols = 16;
  uint8_t filled = (uint32_t)((uint64_t)done * cols / total);
  if (filled > cols) filled = cols;

  // Linha 0: label (corta em 16)
  char l0[17]; memset(l0, ' ', 16); l0[16] = '\0';
  size_t n = strnlen(label, 16);
  memcpy(l0, label, n);
  lcd.setCursor(0, 0); lcd.print(l0);

  // Linha 1: barra + percentual
  char bar[17];
  for (uint8_t i = 0; i < cols; i++) bar[i] = (i < filled ? '#' : '-');
  bar[16] = '\0';
  uint8_t pct = (uint8_t)((uint64_t)done * 100 / total);

  lcd.setCursor(0, 1);  lcd.print(bar);
  char pbuf[5]; snprintf(pbuf, sizeof(pbuf), "%3u%%", pct);
  lcd.setCursor(12, 1); lcd.print(pbuf);
}

#endif // LCD_PROGRESS_HELPER_DEFINED


// Lê APENAS o arquivo do mês informado (yyyymm) e envia JSON.
// Não cria nada, só abre FILE_READ. Se não existir, envia [].
// Agora exibe progresso no LCD enquanto faz o streaming.
// Lê APENAS o arquivo do mês informado (yyyymm) e envia JSON.
// Não cria nada; se não existir, envia [].
// Implementação robusta: laço por bytes lidos (totalSize) + watchdog.

// Lê APENAS o arquivo do mês informado (yyyymm) e envia JSON.
// Usa a API File (texto) do SD, sem FsFile.
// Leitura por tamanho, retry inicial, watchdog e progresso no LCD.
void streamMonthCsvAsJson(EthernetClient &client, uint32_t yyyymm) {
  char fname[20];
  formatMonthFilenameFromYYYYMM(yyyymm, fname, sizeof(fname));

  Serial.print(F("[LOG] streamMonthCsvAsJson yyyymm="));
  Serial.print(yyyymm);
  Serial.print(F(" arquivo="));
  Serial.println(fname);

  // Impede concorrência com gravação/export/clear
  if (sdBusy) {
    Serial.println(F("[LOG] SD ocupado por outra operação. Retornando []."));
    client.println(F("[]"));
    return;
  }
  sdBusy = true;

  // Garante ETH fora do barramento antes de tocar no SD
  digitalWrite(PIN_CS_ETH, HIGH);

  if (!SD.exists(fname)) {
    Serial.println(F("[LOG] Arquivo não existe. Retornando []."));
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

  File f = SD.open(fname, FILE_READ);  // <<< API "texto"
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

  // Ponteiro no início e pega tamanho
  f.seek(0);
  const uint32_t totalSize = (uint32_t)f.size();
  uint32_t bytesRead = 0;

  // Label do LCD
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

  // Buffer e montagem de linhas
  const size_t BUFSZ = 256;
  uint8_t buf[BUFSZ];
  String line;

  // Watchdogs
  unsigned long lastProgressMs = millis();
  const unsigned long MAX_IDLE_MS  = 2000;   // 2s sem progresso => aborta
  const unsigned long MAX_TOTAL_MS = 120000; // 120s hard limit
  unsigned long startMs = millis();

  while (bytesRead < totalSize) {
    size_t toRead = totalSize - bytesRead;
    if (toRead > BUFSZ) toRead = BUFSZ;

    int n = f.read(buf, (int)toRead);

    // Retry único se o primeiro read voltar 0 (algumas libs retornam 0 antes do primeiro bloco)
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

      // Constrói JSON linha a linha (aceita \n e \r\n)
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
      // Nada lido — pequena espera e checa watchdogs
      delay(2);
      if (millis() - lastProgressMs > MAX_IDLE_MS) {
        Serial.println(F("[LOG] Idle timeout lendo SD — abortando para evitar loop infinito."));
        break;
      }
    }

    if (millis() - startMs > MAX_TOTAL_MS) {
      Serial.println(F("[LOG] Max total time alcançado — abortando leitura."));
      break;
    }
  }

  // Última linha (sem newline no fim do arquivo)
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

  // Finaliza barra e solta o SD/lock
  #ifdef LCD_PROGRESS_HELPER_DEFINED
    lcdShowProgress(label, bytesRead, totalSize);
  #endif
  digitalWrite(PIN_CS_SD, HIGH);
  sdBusy = false;

  Serial.print(F("[LOG] Streaming concluído. bytesRead="));
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


void handleCalibracaoPage(EthernetClient &client, const String &query){
  // Permite também salvar via GET simples (kt/at/kh/ah/reset) para facilitar testes
  if (query.length()){
    // Encaminha para o endpoint JSON e imediatamente volta para a página
    // (UX simples: salva e recarrega)
    handleSetCalibracao(client, query);
    return;
  }

  sendHtmlHeader(client);
  client.println(F(
    "<!doctype html><html lang='pt-br'><head><meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width, initial-scale=1'>"
    "<title>Calibra&ccedil;&atilde;o</title>"
    "<link href='https://cdn.jsdelivr.net/npm/bootstrap@5.3.3/dist/css/bootstrap.min.css' rel='stylesheet'>"
    "<style>"
      "body{background:#f6f7fb}"
      ".navbar{background:#0d6efd}.navbar-brand{color:#fff!important;font-weight:600}"
      ".sidebar{min-height:100vh;background:#fff;border-right:1px solid #e5e7eb}"
      ".sidebar .nav-link{color:#0d6efd;font-weight:500}"
      ".sidebar .nav-link.active{background:#e7f1ff;border-radius:.5rem}"
      ".card{border-radius:.75rem}"
      ".grid2{display:grid;grid-template-columns:1fr 1fr;gap:1rem}"
      "@media(max-width:768px){.grid2{grid-template-columns:1fr}}"
    "</style></head><body>"
    "<nav class='navbar navbar-expand-lg'><div class='container-fluid'>"
      "<a class='navbar-brand' href='#'>Equipamento de Monitoramento</a>"
    "</div></nav>"
    "<div class='container-fluid'><div class='row'>"
    "<aside class='col-12 col-md-3 col-lg-2 p-3 sidebar'>"
      "<ul class='nav nav-pills flex-column'>"
        "<li class='nav-item'><a class='nav-link' href='/?#status'>Status</a></li>"
        "<li class='nav-item'><a class='nav-link' href='/?#rede'>Rede</a></li>"
        "<li class='nav-item'><a class='nav-link' href='/historico'>Hist&oacute;rico</a></li>"
        "<li class='nav-item'><a class='nav-link' href='/export'>Exportar</a></li>"
        "<li class='nav-item'><a class='nav-link active' href='/calibracao'>Calibra&ccedil;&atilde;o</a></li>"
        "<li class='nav-item'><a class='nav-link' href='/ws/temperatura' target='_blank'>JSON</a></li>"
      "</ul>"
    "</aside>"
    "<main class='col-12 col-md-9 col-lg-10 p-4'>"
      "<h3 class='mb-3'>Calibra&ccedil;&atilde;o</h3>"

      "<div class='card mb-3'><div class='card-body'>"
        "<h5 class='mb-3'>Ganho e Offset (aplica&ccedil;&atilde;o direta)</h5>"
        "<form class='row g-3' method='GET' action='/ws/calib'>"
          "<div class='col-6 col-lg-3'><label class='form-label'>kTemp</label>"
            "<input name='kt' class='form-control' value='"
  ));
  client.print(cfg.kTemp, 6);
  client.println(F("'></div>"));

  client.print(F("<div class='col-6 col-lg-3'><label class='form-label'>aTemp</label>"
                 "<input name='at' class='form-control' value='"));
  client.print(cfg.aTemp, 6);
  client.println(F("'></div>"));

  client.print(F("<div class='col-6 col-lg-3'><label class='form-label'>kHum</label>"
                 "<input name='kh' class='form-control' value='"));
  client.print(cfg.kHum, 6);
  client.println(F("'></div>"));

  client.print(F("<div class='col-6 col-lg-3'><label class='form-label'>aHum</label>"
                 "<input name='ah' class='form-control' value='"));
  client.print(cfg.aHum, 6);
  client.println(F("'></div>"));

  client.println(F(
          "<div class='col-12 d-flex gap-2'>"
            "<button class='btn btn-primary'>Salvar</button>"
            "<a class='btn btn-outline-secondary' href='/calibracao'>Recarregar</a>"
            "<a class='btn btn-outline-danger' href='/ws/calib?reset=1'>Zerar (identidade)</a>"
          "</div>"
        "</form>"
      "</div></div>"

      "<div class='card mb-3'><div class='card-body'>"
        "<h5 class='mb-3'>Assistente de 2 Pontos</h5>"
        "<p class='text-muted'>Informe duas medi&ccedil;&otilde;es: "
        "<code>raw</code> (o que o sensor mostrou) e <code>ref</code> (valor de refer&ecirc;ncia).</p>"
        "<form class='grid2' method='GET' action='/ws/calib'>"
          "<input type='hidden' name='mode' value='two'>"
          "<div>"
            "<h6>Temperatura</h6>"
            "<div class='row g-2'>"
              "<div class='col-6'><label class='form-label'>raw1 (°C)</label><input name='raw1t' class='form-control' placeholder='ex.: 24.7'></div>"
              "<div class='col-6'><label class='form-label'>ref1 (°C)</label><input name='ref1t' class='form-control' placeholder='ex.: 25.0'></div>"
              "<div class='col-6'><label class='form-label'>raw2 (°C)</label><input name='raw2t' class='form-control' placeholder='ex.: 34.8'></div>"
              "<div class='col-6'><label class='form-label'>ref2 (°C)</label><input name='ref2t' class='form-control' placeholder='ex.: 35.0'></div>"
            "</div>"
          "</div>"
          "<div>"
            "<h6>Umidade</h6>"
            "<div class='row g-2'>"
              "<div class='col-6'><label class='form-label'>raw1 (%RH)</label><input name='raw1h' class='form-control' placeholder='ex.: 74.2'></div>"
              "<div class='col-6'><label class='form-label'>ref1 (%RH)</label><input name='ref1h' class='form-control' placeholder='ex.: 75.3'></div>"
              "<div class='col-6'><label class='form-label'>raw2 (%RH)</label><input name='raw2h' class='form-control' placeholder='ex.: 32.5'></div>"
              "<div class='col-6'><label class='form-label'>ref2 (%RH)</label><input name='ref2h' class='form-control' placeholder='ex.: 33.0'></div>"
            "</div>"
          "</div>"
          "<div class='col-12 mt-3 d-flex gap-2'>"
            "<button class='btn btn-primary'>Calcular e Salvar</button>"
            "<a class='btn btn-outline-danger' href='/ws/calib?reset=1'>Zerar (identidade)</a>"
          "</div>"
        "</form>"
      "</div></div>"

      "<div class='card'><div class='card-body'>"
        "<h5 class='mb-2'>Dicas pr&aacute;ticas</h5>"
        "<ul class='mb-0'>"
          "<li>Evite correntes de ar e aguarde estabiliza&ccedil;&atilde;o (2&ndash;5 min) antes de registrar cada ponto.</li>"
          "<li>Para umidade, use solu&ccedil;&otilde;es salinas saturadas para pontos de refer&ecirc;ncia (~75% &agrave; 25&nbsp;&deg;C com NaCl; ~33% com MgCl<sub>2</sub>) em recipiente fechado.</li>"
          "<li>Para temperatura, use pontos em torno da faixa de uso (ex.: ambiente e pr&oacute;ximo ao topo esperado).</li>"
        "</ul>"
        "<div class='text-muted small mt-2'>M&eacute;todo de dois pontos (ganho/offset) para DHT22 em clima tropical descrito em literatura t&eacute;cnica. "
        "Considere repetir a calibra&ccedil;&atilde;o periodicamente.</div>"
      "</div></div>"

    "</main></div></div>"
    "<script src='https://cdn.jsdelivr.net/npm/bootstrap@5.3.3/dist/js/bootstrap.bundle.min.js'></script>"
    "</body></html>"
  ));
}


void handleJsonLog(EthernetClient &client, const String &query){
  unsigned long t0 = millis();
  Serial.println(F("\n[WS/LOG] ====== handleJsonLog begin ======"));
  Serial.print  (F("[WS/LOG] query=")); Serial.println(query);
  Serial.print  (F("[WS/LOG] sdAvailable=")); Serial.println(sdAvailable ? F("true") : F("false"));

  if (!sdAvailable) {
    Serial.println(F("[WS/LOG] SD indisponivel -> 404"));
    sendNotFound(client);
    return;
  }

  sendJsonHeader(client);

  // Parâmetros
  String v_hours = getQueryParam(query, "hours");
  String v_ym    = getQueryParam(query, "yyyymm");

  // ===================== MODO HOURS (janela móvel) =====================
  if (v_hours.length()) {
    unsigned long hrs = (unsigned long)v_hours.toInt();
    if (hrs == 0) hrs = 24;

    unsigned long nowEpoch = getEpochUTC();
    Serial.print(F("[WS/LOG] modo hours, hrs=")); Serial.print(hrs);
    Serial.print(F(" nowEpoch="));                Serial.println(nowEpoch);

    if (nowEpoch == 0UL) {
      // Sem referência de tempo -> não arriscamos
      Serial.println(F("[WS/LOG] epoch=0 -> retornando []"));
      client.println(F("[]"));
      Serial.print(F("[WS/LOG] total(ms)=")); Serial.println(millis()-t0);
      Serial.println(F("[WS/LOG] ====== handleJsonLog end ======\n"));
      return;
    }

    unsigned long minEpoch = (nowEpoch > hrs*3600UL) ? (nowEpoch - hrs*3600UL) : 0UL;
    Serial.print(F("[WS/LOG] minEpoch=")); Serial.println(minEpoch);

    // Lê mês atual + anterior e filtra por epoch
    streamCsvAsJson(client, minEpoch, 0, -1, /*debugLogs=*/true);

    Serial.print(F("[WS/LOG] total(ms)=")); Serial.println(millis()-t0);
    Serial.println(F("[WS/LOG] ====== handleJsonLog end ======\n"));
    return;
  }

  // ===================== MODO YYYYMM (mês fechado) =====================
  auto isSixDigits = [](const String& s)->bool{
    if (s.length() != 6) return false;
    for (uint8_t i=0;i<6;i++){ char c = s[i]; if (c<'0'||c>'9') return false; }
    return true;
  };

  if (isSixDigits(v_ym)) {
    // Use 32 bits para evitar overflow (ex.: 202509 cabe em uint32_t)
    uint32_t yyyymm = (uint32_t)v_ym.toInt();
    Serial.print(F("[WS/LOG] modo yyyymm (fornecido), yyyymm="));
    Serial.println((unsigned long)yyyymm);

    // Envia exatamente LYYYYMM.CSV (sem criar nada)
    streamMonthCsvAsJson(client, yyyymm);

    Serial.print(F("[WS/LOG] total(ms)=")); Serial.println(millis()-t0);
    Serial.println(F("[WS/LOG] ====== handleJsonLog end ======\n"));
    return;
  }

  // ============== FALLBACK: mês atual (NTP) ou mais recente no SD ==============
  uint32_t fallbackYm = currentYYYYMM(getEpochUTC());
  if (fallbackYm == 0UL) {
    // Sem NTP: varre o SD por LYYYYMM.CSV e pega o maior
    digitalWrite(PIN_CS_ETH, HIGH); // libera barramento para SD
    File root = SD.open("/");
    uint32_t best = 0;
    if (root) {
      File entry = root.openNextFile();
      while (entry) {
        if (!entry.isDirectory()) {
          char nm[32] = {0};
          entry.getName(nm, sizeof(nm));
          // Esperado: "L" + 6 dígitos + ".CSV"  (11 chars)
          // Ex.: L202509.CSV
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
    digitalWrite(PIN_CS_SD, HIGH); // solta SD
    fallbackYm = best;
  }

  if (fallbackYm > 0) {
    Serial.print(F("[WS/LOG] yyyymm inválido/ausente -> usando atual/mais recente: "));
    Serial.println((unsigned long)fallbackYm);
    streamMonthCsvAsJson(client, fallbackYm);
  } else {
    Serial.println(F("[WS/LOG] nenhum mês disponível -> []"));
    client.println(F("[]"));
  }

  Serial.print(F("[WS/LOG] total(ms)=")); Serial.println(millis()-t0);
  Serial.println(F("[WS/LOG] ====== handleJsonLog end ======\n"));
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
  else if(path=="/export")             handleExportPage(client);
  else if(path=="/calibracao")         handleCalibracaoPage(client, query);   // <-- NOVO
  else if(path=="/ws/calib")           handleSetCalibracao(client, query);    // <-- NOVO
  else if(path=="/ws/clear")           handleCsvClear(client, query);
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
    // Aplica calibração antes de publicar/logar
    lastTemp = applyCalTemp(t);
    lastHum  = applyCalHum(h);

    if(wasBelowThreshold && lastTemp>=THRESH_C){ beep(1800,180); wasBelowThreshold=false; }
    else if(lastTemp<THRESH_C){ wasBelowThreshold=true; }

    if (sdAvailable) {
      // sdAppendLog já decide se deve gravar (mudança ≥1.0) e faz o lock
      (void)sdAppendLog(lastTemp, lastHum);
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


// Retorna YYYYMM atual (via NTP). Se NTP não disponível, varre o SD e retorna o LYYYYMM mais recente.
// Se nada encontrado, retorna 0.
uint32_t getCurrentOrLatestYYYYMM() {
  // 1) Tenta via NTP
  uint32_t yyyymm = currentYYYYMM(getEpochUTC());
  if (yyyymm != 0UL) return yyyymm;

  // 2) Fallback: varre o SD por LYYYYMM.CSV e pega o maior
  if (!sdAvailable) return 0UL;

  digitalWrite(PIN_CS_ETH, HIGH);  // libera barramento para SD
  File root = SD.open("/");
  uint32_t best = 0;

  if (root) {
    File entry = root.openNextFile();
    while (entry) {
      if (!entry.isDirectory()) {
        char nm[32] = {0};
        entry.getName(nm, sizeof(nm));
        // Formato esperado: 'L' + 6 dígitos + ".CSV" => tamanho 11
        // Ex.: "L202509.CSV"
        size_t len = strlen(nm);
        if (len == 11 && nm[0] == 'L' &&
            nm[7] == '.' && nm[8] == 'C' && nm[9] == 'S' && nm[10] == 'V') {
          // Captura os 6 dígitos
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

  digitalWrite(PIN_CS_SD, HIGH);   // solta SD
  return best;
}
