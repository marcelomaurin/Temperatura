/*******************************************************
 * ESP8266 - DHT22 + HTTP + Config + Calib + Média(10)
 * Wi-Fi:
 *   Tipo 1 = PSK (DEFAULT: SSID=maurinsrv / PASS=1425361425)
 *   Tipo 2 = WPA2-Enterprise (PEAP/MSCHAPv2)
 *            • Outer Identity (FIXO): anonymous@coderp.sp.gov.br
 *            • Inner Username/Password: via /config, persistidos em EEPROM
 *
 * Recursos:
 *   - EEPROM persistente (credenciais, calibração, MAC custom)
 *   - Fallback: 3 falhas ⇒ DEFAULT_SSID/DEFAULT_PASS (Tipo 1)
 *   - /ws/state (pipeline EAP + motivo de desconexão)
 *   - /ws/coleta (status, leituras, fw, etc.)
 *   - /dht (JSON compatível com campos antigos/atuais)
 *   - /mac (alterar MAC persistente)
 *   - /calib (coeficientes, auto-calibração)
 *   - Versão do firmware nos JSONs e Serial
 *******************************************************/

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <EEPROM.h>
#include <DHT.h>

extern "C" {
  #include "user_interface.h"
  #include "wpa2_enterprise.h"   // WPA2-Enterprise (ESP8266 core)
}

ESP8266WebServer server(80);

// === Blynk ===
#define BLYNK_TEMPLATE_ID   "TMPL2lEGiwwEI"
#define BLYNK_TEMPLATE_NAME "Quickstart Template"
#define BLYNK_AUTH_TOKEN    "EcDFkG_nACLP34JdV9hs-yLbHRPimLFi"  // use o seu

#define BLYNK_PRINT Serial
#include <BlynkSimpleEsp8266.h>
#include <Blynk/BlynkTimer.h>  // temporizador do Blynk
//#include <BlynkTimer.h>



// ====== Firmware ======
#define FW_NAME     "ESP8266-TempHum"
#define FW_VERSION  "3.0.0"
#define FW_BUILD    (__DATE__ " " __TIME__)

// ====== Defaults/Fallback ======
#define DEFAULT_SSID "maurinsrv"
#define DEFAULT_PASS "1425361425"

// ====== DHT ======
#define DHTPIN   4
#define DHTTYPE  DHT22
DHT dht(DHTPIN, DHTTYPE);

// ====== EEPROM Layout ======
#define EEPROM_SIZE 512
static const uint32_t WIFI_MAGIC_V3 = 0x42A1C0E3;
static const uint32_t CAL_MAGIC     = 0xC0A1B123;

BlynkTimer blynkTimer;
bool g_blynkReady = false;       // já fiz config()?
bool g_blynkConnected = false;   // status de conexão ao cloud


struct WifiStoreV3 {
  uint32_t magic;
  uint8_t  auth_type;              // 1=PSK, 2=EAP
  char ssid[33];
  char pass[65];
  char eap_identity[65];           // legado (ignorado em runtime; usamos outer fixo)
  char eap_username[65];           // INNER
  char eap_password[65];           // INNER
  uint8_t mac_custom;
  uint8_t mac[6];
};

struct CalibStore {
  uint32_t magic;
  float temp_gain, temp_offset;
  float hum_gain,  hum_offset;
};

const size_t WIFI_ADDR = 0;
const size_t CAL_ADDR  = sizeof(WifiStoreV3);

// ====== EAP (Outer Identity FIXO) ======
static const char* EAP_OUTER_IDENTITY_FIXED = "anonymous@coderp.sp.gov.br";

// ====== CA do RADIUS (COLE O PEM AQUI) ======
static const char CA_PEM_CODERP_8266[] PROGMEM = R"PEM(-----BEGIN CERTIFICATE-----
PASTE_YOUR_CODERP_CA_PEM_HERE
-----END CERTIFICATE-----
)PEM";

// ====== Estado runtime ======
enum WifiConnState { WIFI_IDLE, WIFI_CONNECTING, WIFI_CONNECTED, WIFI_BACKOFF };

static const uint32_t WIFI_FIRST_TIMEOUT_MS   = 8000;
static const uint32_t WIFI_MAX_TIMEOUT_MS     = 20000;
static const uint32_t WIFI_RETRY_COOLDOWN_MS  = 15000;

#define LED_ON()  digitalWrite(LED_BUILTIN, LOW)
#define LED_OFF() digitalWrite(LED_BUILTIN, HIGH)
#define LED_TG()  digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN))

WifiConnState wifiConnState = WIFI_IDLE;
uint32_t connectStartedAt=0, nextRetryAt=0, connectTimeoutMs=WIFI_FIRST_TIMEOUT_MS;
uint8_t  failCount=0;
bool     usingDefault=false;

struct WifiRuntime {
  uint8_t  auth_type; // 1,2
  String   ssid, pass;
  String   eap_identity, eap_username, eap_password;
  bool     mac_custom; uint8_t mac[6];
} g_cfg;

String g_active_ssid;

WiFiEventHandler g_onDisc;
volatile uint8_t g_lastDiscReason=0;

// ====== EAP (Enterprise) — pipeline de estágios para LOG ======
enum EapStage : uint8_t {
  EAP_IDLE=0, EAP_INIT, EAP_SET_SSID, EAP_SET_CA, EAP_SET_IDENTITY, EAP_SET_USERNAME,
  EAP_SET_PASSWORD, EAP_ENABLE, EAP_CONNECT, EAP_SUCCESS, EAP_ERROR
};



volatile EapStage g_eapStage = EAP_IDLE;
String g_eapMsg;

// ====== LOG CODERP ======
static inline void coderpLog(const char* stage, const String& msg) {
  Serial.printf("[CODERP] %-12s | %8lu ms | SSID='%s' | %s\n",
    stage, millis(), g_active_ssid.c_str(), msg.c_str());
}
static String caPreview(const char* pem) {
  if(!pem || !*pem) return String("CA: (vazia)");
  String body = String(pem).substring(0, 60);
  body.replace("\n","\\n");
  return String("CA preview: \"") + body + "\" ...";
}
static void setEapStage(EapStage st, const String& msg){
  g_eapStage = st; g_eapMsg = msg;
  const char* name =
    (st==EAP_IDLE?"IDLE":st==EAP_INIT?"INIT":st==EAP_SET_SSID?"SET_SSID":
     st==EAP_SET_CA?"SET_CA":st==EAP_SET_IDENTITY?"IDENTITY":st==EAP_SET_USERNAME?"USERNAME":
     st==EAP_SET_PASSWORD?"PASSWORD":st==EAP_ENABLE?"ENABLE":st==EAP_CONNECT?"CONNECT":
     st==EAP_SUCCESS?"SUCCESS":"ERROR");
  Serial.printf("[EAP] %02u %-12s | %lu ms | %s\n", (unsigned)st, name, millis(), msg.c_str());
}

// ====== Calibração + Média ======
float TEMP_GAIN=1.0f, TEMP_OFFSET=0.0f, HUM_GAIN=1.0f, HUM_OFFSET=0.0f;

struct DhtReading { float t,h; bool ok; float t_raw,h_raw; float t_avg,h_avg; uint8_t n_avg; };
DhtReading lastDht={NAN,NAN,false,NAN,NAN,NAN,NAN,0};
static const uint8_t AVG_WIN=10; float bufT[AVG_WIN], bufH[AVG_WIN];
uint8_t bufCount=0, bufPos=0; double sumT=0.0, sumH=0.0;

// ====== Utils ======
inline void safeYield(){ delay(0); }
inline void avgReset(){ bufCount=0; bufPos=0; sumT=sumH=0; lastDht.t_avg=lastDht.h_avg=NAN; lastDht.n_avg=0; }
inline void avgAddSample(float t,float h){
  if(bufCount<AVG_WIN){ bufT[bufCount]=t; bufH[bufCount]=h; sumT+=t; sumH+=h; bufCount++; }
  else { sumT-=bufT[bufPos]; sumH-=bufH[bufPos]; bufT[bufPos]=t; bufH[bufPos]=h; sumT+=t; sumH+=h; bufPos=(bufPos+1)%AVG_WIN; }
  lastDht.t_avg=float(sumT/bufCount); lastDht.h_avg=float(sumH/bufCount); lastDht.n_avg=bufCount;
}
static inline float clampf(float v,float lo,float hi){ if(isnan(v))return v; if(v<lo)return lo; if(v>hi)return hi; return v; }
static String argOrEmpty(const String& k){ return server.hasArg(k)? server.arg(k) : String(""); }

// ====== Info Wi-Fi ======
const char* phyToStr(WiFiPhyMode_t m){
  switch(m){ case WIFI_PHY_MODE_11B: return "802.11b";
             case WIFI_PHY_MODE_11G: return "802.11g";
             case WIFI_PHY_MODE_11N: return "802.11n";
             default: return "unknown"; }
}
String currentBand(){ return "2.4GHz"; }
uint16_t centerFreqMHz(uint8_t ch){ if(ch>=1 && ch<=13) return 2407 + 5*ch; if(ch==14) return 2484; return 0; }
const char* wifiReasonToStr(uint8_t r){
  switch(r){
    case 1: return "UNSPECIFIED"; case 2: return "AUTH_EXPIRE"; case 3: return "AUTH_LEAVE";
    case 4: return "ASSOC_EXPIRE"; case 5: return "ASSOC_TOOMANY"; case 6: return "NOT_AUTHED";
    case 7: return "NOT_ASSOCED"; case 8: return "ASSOC_LEAVE"; case 9: return "ASSOC_NOT_AUTHED";
    case 10:return "PWRCAP_BAD"; case 11:return "SUPCHAN_BAD"; case 13:return "IE_INVALID";
    case 14:return "MIC_FAILURE"; case 15:return "4WAY_TIMEOUT"; case 16:return "GTK_TIMEOUT";
    case 17:return "IE_4WAY_DIFF"; case 18:return "GRP_CIPHER_INV"; case 19:return "PAIR_CIPHER_INV";
    case 20:return "AKMP_INVALID"; case 21:return "RSN_VER_UNSUP"; case 22:return "RSN_CAP_INV";
    case 23:return "8021X_AUTH_FAIL"; case 24:return "CIPHER_REJECTED";
    case 200:return "BEACON_TIMEOUT"; case 201:return "NO_AP_FOUND"; case 202:return "AUTH_FAIL";
    case 203:return "ASSOC_FAIL"; case 204:return "HANDSHAKE_TIMEOUT";
    default:return "UNKNOWN";
  }
}
const char* currentSecurity(){ return (g_cfg.auth_type==2) ? "WPA2-Enterprise" : "WPA2-PSK"; }

// ====== MAC helpers ======
bool parseMacString(const String& s, uint8_t out[6]){
  int idx=0, nibbles=0; uint8_t val=0;
  for(size_t i=0;i<s.length();++i){
    char c=s[i];
    if(c==':'||c=='-'||c==' '){
      if(nibbles==0) return false;
      if(idx>=6) return false;
      out[idx++]=val; val=0; nibbles=0; continue;
    }
    uint8_t d=0xFF;
    if(c>='0'&&c<='9') d=c-'0';
    else if(c>='a'&&c<='f') d=10+(c-'a');
    else if(c>='A'&&c<='F') d=10+(c-'A');
    else return false;
    val = (nibbles==0)? d : (uint8_t)((val<<4)|d);
    nibbles = (nibbles+1)&1;
    if(nibbles==0){ if(idx>=6) return false; out[idx++]=val; val=0; }
  }
  if(nibbles!=0) return false;
  return idx==6;
}
String macToString(const uint8_t m[6]){ char b[18]; snprintf(b,sizeof(b),"%02X:%02X:%02X:%02X:%02X:%02X",m[0],m[1],m[2],m[3],m[4],m[5]); return String(b); }

// V0: ecoa o valor para V1 (exemplo)
BLYNK_WRITE(V0) {
  int value = param.asInt();
  Blynk.virtualWrite(V1, value);
}

// Ao conectar no Blynk.Cloud
BLYNK_CONNECTED() {
  g_blynkConnected = true;


    // Rótulos bonitinhos
  g_blynkConnected = true;
  Blynk.setProperty(V4,  "label", "t_avg (°C)");
  Blynk.setProperty(V5,  "label", "h_avg (%)");
  Blynk.setProperty(V6,  "label", "t (°C)");
  Blynk.setProperty(V7,  "label", "h (%)");
  Blynk.setProperty(V10, "label", "ip");

  // Muda imagens do botão web (exemplo padrão)
  Blynk.setProperty(V3, "offImageUrl", "https://static-image.nyc3.cdn.digitaloceanspaces.com/general/fte/congratulations.png");
  Blynk.setProperty(V3, "onImageUrl",  "https://static-image.nyc3.cdn.digitaloceanspaces.com/general/fte/congratulations_pressed.png");
  Blynk.setProperty(V3, "url", "https://docs.blynk.io/en/getting-started/what-do-i-need-to-blynk/how-quickstart-device-was-made");
}

/* / Envia uptime e leituras para o app
void blynkTickSend() {
  Blynk.virtualWrite(V2, millis()/1000);           // uptime (s)
  if (lastDht.ok && !isnan(lastDht.t_avg)) {
    Blynk.virtualWrite(V4, lastDht.t_avg);         // temperatura média
  }
  if (lastDht.ok && !isnan(lastDht.h_avg)) {
    Blynk.virtualWrite(V5, lastDht.h_avg);         // umidade média
  }
}
*/

void handleWsMin() {
  String ip = (WiFi.status() == WL_CONNECTED) ? WiFi.localIP().toString() : String("");

  String out = "{";
  out += "\"t\":"      + String(lastDht.ok && !isnan(lastDht.t)     ? String(lastDht.t,2)     : "null") + ",";
  out += "\"t_avg\":"  + String(lastDht.ok && !isnan(lastDht.t_avg) ? String(lastDht.t_avg,2) : "null") + ",";
  out += "\"h\":"      + String(lastDht.ok && !isnan(lastDht.h)     ? String(lastDht.h,2)     : "null") + ",";
  out += "\"h_avg\":"  + String(lastDht.ok && !isnan(lastDht.h_avg) ? String(lastDht.h_avg,2) : "null") + ",";
  out += "\"ip\":\""   + ip + "\"}";
  sendJson(out);
}


// Inicializa Blynk quando o Wi-Fi estiver conectado
void beginBlynkIfReady() {
  if (g_blynkReady) return;
  if (WiFi.status() != WL_CONNECTED) return;

  Blynk.config(BLYNK_AUTH_TOKEN, "blynk.cloud", 8080); // <-- porta 8080
  bool ok = Blynk.connect(3000);
  Serial.printf("[BLYNK] connect(8080) => %s\n", ok ? "OK" : "FAIL");
  g_blynkReady = true;

  blynkTimer.setInterval(1000L, blynkTickSend);
}


// ====== Serial helpers ======
void printResetInfo(){
  Serial.println(); Serial.println(F("===== RESET INFO ====="));
  Serial.printf("Reason: %s\n", ESP.getResetReason().c_str());
  Serial.printf("Info:   %s\n", ESP.getResetInfo().c_str());
  Serial.println(F("======================"));
}
void dumpWifiCfg(const char* tag){
  Serial.printf("[CFG] (%s) Auth=%u(%s) SSID='%s' MAC=%s\n",
    tag, g_cfg.auth_type, (g_cfg.auth_type==2?"EAP+CA":"PSK"), g_cfg.ssid.c_str(),
    g_cfg.mac_custom?"custom":"factory");
  if(g_cfg.auth_type==1){
    Serial.printf("[CFG] PSK: PASS(len)=%u\n", (unsigned)g_cfg.pass.length());
  } else {
    Serial.printf("[CFG] EAP: outer='%s' inner='%s' pass_len=%u (CA fix coderp)\n",
      g_cfg.eap_identity.c_str(), g_cfg.eap_username.c_str(), (unsigned)g_cfg.eap_password.length());
  }
}

// ====== EEPROM: Load/Save ======
static void loadDefaults(){
  g_cfg.auth_type=1;
  g_cfg.ssid=DEFAULT_SSID; g_cfg.pass=DEFAULT_PASS;
  g_cfg.eap_identity=EAP_OUTER_IDENTITY_FIXED;
  g_cfg.eap_username=g_cfg.eap_password="";
  g_cfg.mac_custom=false; memset(g_cfg.mac,0,6);
}

bool eepromLoadWifi(){
  WifiStoreV3 v3; EEPROM.get(WIFI_ADDR, v3);
  if(v3.magic!=WIFI_MAGIC_V3) return false;
  g_cfg.auth_type=v3.auth_type? v3.auth_type:1;
  g_cfg.ssid=v3.ssid; g_cfg.pass=v3.pass;
  g_cfg.eap_identity = EAP_OUTER_IDENTITY_FIXED;       // força outer fixo
  g_cfg.eap_username=v3.eap_username; g_cfg.eap_password=v3.eap_password;
  g_cfg.mac_custom=v3.mac_custom; memcpy(g_cfg.mac, v3.mac, 6);
  return (g_cfg.ssid.length()>0);
}
bool eepromSaveWifi(){
  WifiStoreV3 v3{}; v3.magic=WIFI_MAGIC_V3;
  v3.auth_type=g_cfg.auth_type;
  g_cfg.ssid.substring(0,32).toCharArray(v3.ssid,sizeof(v3.ssid));
  g_cfg.pass.substring(0,64).toCharArray(v3.pass,sizeof(v3.pass));
  String compat_identity = EAP_OUTER_IDENTITY_FIXED;
  compat_identity.substring(0,64).toCharArray(v3.eap_identity,sizeof(v3.eap_identity));
  g_cfg.eap_username.substring(0,64).toCharArray(v3.eap_username,sizeof(v3.eap_username));
  g_cfg.eap_password.substring(0,64).toCharArray(v3.eap_password,sizeof(v3.eap_password));
  v3.mac_custom=g_cfg.mac_custom?1:0; memcpy(v3.mac,g_cfg.mac,6);
  EEPROM.put(WIFI_ADDR, v3); bool ok=EEPROM.commit();
  Serial.println(ok?"[EEPROM] WiFi salvo.":"[EEPROM] ERRO ao salvar WiFi!");
  return ok;
}

bool eepromLoadCalib(){
  CalibStore cs; EEPROM.get(CAL_ADDR, cs);
  if(cs.magic!=CAL_MAGIC) return false;
  TEMP_GAIN=cs.temp_gain; TEMP_OFFSET=cs.temp_offset; HUM_GAIN=cs.hum_gain; HUM_OFFSET=cs.hum_offset;
  Serial.printf("[EEPROM] Calib T(a=%.3f,b=%.3f) H(a=%.3f,b=%.3f)\n",TEMP_GAIN,TEMP_OFFSET,HUM_GAIN,HUM_OFFSET);
  return true;
}
bool eepromSaveCalib(float tg,float to,float hg,float ho){
  CalibStore cs{CAL_MAGIC,tg,to,hg,ho}; EEPROM.put(CAL_ADDR, cs); bool ok=EEPROM.commit();
  if(ok){ TEMP_GAIN=tg; TEMP_OFFSET=to; HUM_GAIN=hg; HUM_OFFSET=ho; }
  Serial.println(ok?"[EEPROM] Calib salva.":"[EEPROM] ERRO ao salvar calib!");
  return ok;
}
void calibSetDefaultsIfEmpty(){ if(!eepromLoadCalib()){ eepromSaveCalib(1.0f,0.0f,1.0f,0.0f); Serial.println(F("[CAL] Defaults gravados.")); } }

// ====== Common Wi-Fi Tweaks ======
void applyCustomMacIfAny(){
  if(!g_cfg.mac_custom) return;
  wifi_set_macaddr(STATION_IF, g_cfg.mac);
  Serial.printf("[WiFi] MAC custom aplicado: %s\n", macToString(g_cfg.mac).c_str());
}
void applyCommonStaTweaks(){
  WiFi.mode(WIFI_STA);
  WiFi.persistent(false);
  WiFi.setAutoReconnect(false);
  WiFi.setSleepMode(WIFI_MODEM_SLEEP);
  WiFi.setOutputPower(10);
  WiFi.hostname("ESP8266-TempHum");
  if(g_cfg.mac_custom) applyCustomMacIfAny();
}

// ====== Enterprise (ESP8266) ======
static void applyEnterprise8266(){
  setEapStage(EAP_INIT, "Inicializando EAP (ESP8266)");
  coderpLog("INIT", "Pipeline PEAP/MSCHAPv2");

  struct station_config conf{}; 
  strncpy((char*)conf.ssid, g_cfg.ssid.c_str(), sizeof(conf.ssid));
  wifi_station_set_config(&conf);
  setEapStage(EAP_SET_SSID, String("SSID='")+g_cfg.ssid+"' configurado");
  coderpLog("SET_SSID", "SSID configurado");

  // Enterprise ON
  wifi_station_set_wpa2_enterprise_auth(true);
  setEapStage(EAP_ENABLE, "WPA2-Enterprise habilitado");
  coderpLog("ENABLE", "Enterprise ON");

  // CA
  if (strlen(CA_PEM_CODERP_8266)>0) {
    wifi_station_set_enterprise_ca_cert((uint8*)CA_PEM_CODERP_8266, strlen(CA_PEM_CODERP_8266)+1);
    setEapStage(EAP_SET_CA, "CA (PEM) aplicada (coderp.sp.gov.br)");
    coderpLog("SET_CA", caPreview(CA_PEM_CODERP_8266));
  } else {
    setEapStage(EAP_SET_CA, "CA AUSENTE — NÃO RECOMENDADO");
    coderpLog("SET_CA", "ATENÇÃO: sem CA (inseguro)");
  }

  // OUTER (fixo)
  g_cfg.eap_identity = EAP_OUTER_IDENTITY_FIXED;
  wifi_station_set_enterprise_identity((uint8*)g_cfg.eap_identity.c_str(), g_cfg.eap_identity.length());
  setEapStage(EAP_SET_IDENTITY, String("identity='")+g_cfg.eap_identity+"' (fixo)");
  coderpLog("IDENTITY", String("outer='")+g_cfg.eap_identity+"'");

  // INNER (MSCHAPv2)
  if (g_cfg.eap_username.length()==0) {
    setEapStage(EAP_ERROR, "USERNAME vazio");
    coderpLog("USERNAME", "VAZIO — configure em /config");
    return;
  }
  wifi_station_set_enterprise_username((uint8*)g_cfg.eap_username.c_str(), g_cfg.eap_username.length());
  setEapStage(EAP_SET_USERNAME, String("username='")+g_cfg.eap_username+"'");
  coderpLog("USERNAME", String("inner='")+g_cfg.eap_username+"' len="+String(g_cfg.eap_username.length()));

  wifi_station_set_enterprise_password((uint8*)g_cfg.eap_password.c_str(), g_cfg.eap_password.length());
  setEapStage(EAP_SET_PASSWORD, "password: ******");
  coderpLog("PASSWORD", String("len=")+String(g_cfg.eap_password.length()));

  wifi_station_connect();
  setEapStage(EAP_CONNECT, "wifi_station_connect() disparado");
  coderpLog("CONNECT", "802.1X/PEAP iniciado");
}

// ====== Conectar ======
void beginConnect(){
  g_active_ssid = g_cfg.ssid;
  wifiConnState = WIFI_CONNECTING; connectStartedAt = millis();
  Serial.printf("[WiFi] Conectando a '%s' (auth=%u)%s\n",
    g_active_ssid.c_str(), g_cfg.auth_type, usingDefault?" [DEFAULT]":"");
  applyCommonStaTweaks();

  if(g_cfg.auth_type==2){
    applyEnterprise8266();
    LED_ON(); return;
  }

  // Tipo 1 (PSK)
  WiFi.begin(g_cfg.ssid.c_str(), g_cfg.pass.c_str());
  LED_ON();
}

void switchToDefaultsIfNeeded(){
  if(failCount>=3 && !usingDefault){
    Serial.println(F("[WiFi] Falhou 3x — alternando para DEFAULT (Auth=1)"));
    usingDefault=true;
    g_cfg.auth_type=1; g_cfg.ssid=DEFAULT_SSID; g_cfg.pass=DEFAULT_PASS;
    g_cfg.eap_identity=EAP_OUTER_IDENTITY_FIXED; g_cfg.eap_username=g_cfg.eap_password="";
    wifi_station_clear_enterprise_ca_cert();
    WiFi.disconnect(); delay(100);
    beginConnect();
  }
}

void wifiInitIfNeeded(){ if(wifiConnState!=WIFI_IDLE) return; connectTimeoutMs = WIFI_FIRST_TIMEOUT_MS; beginConnect(); }

void wifiStateMachine(){
  switch (wifiConnState) {

    case WIFI_IDLE:
      wifiInitIfNeeded();
      break;

    case WIFI_CONNECTING: {
      static uint32_t lastBlink = 0;
      if (millis() - lastBlink >= 150) { lastBlink = millis(); LED_TG(); }

      wl_status_t st = WiFi.status();
      if (st == WL_CONNECTED) {
        wifiConnState = WIFI_CONNECTED;
        LED_OFF();
        failCount = 0;
        if (g_cfg.auth_type == 2) {
          setEapStage(EAP_SUCCESS, "EAP ok • IP=" + WiFi.localIP().toString());
          coderpLog("SUCCESS", "Autenticado e IP obtido");
        }
        Serial.println(F("[WiFi] CONECTADO"));
        Serial.printf("[WiFi] SSID:%s IP:%s RSSI:%d dBm Sec:%s Default:%s MAC:%s\n",
                      g_active_ssid.c_str(), WiFi.localIP().toString().c_str(), WiFi.RSSI(),
                      currentSecurity(), usingDefault ? "sim" : "não", WiFi.macAddress().c_str());

        // Inicializa Blynk agora que temos link/IP
        beginBlynkIfReady();
        break;
      }

      if (millis() - connectStartedAt >= connectTimeoutMs) {
        failCount++;
        if (g_cfg.auth_type == 2) {
          setEapStage(EAP_ERROR, String("EAP falhou (status=") + (int)WiFi.status() + ")");
          coderpLog("ERROR", "Timeout durante autenticação");
        }
        Serial.printf("[WiFi] TIMEOUT (%ums) — falha #%u. Status=%d\n",
                      connectTimeoutMs, failCount, (int)WiFi.status());

        wifiConnState = WIFI_BACKOFF;
        nextRetryAt = millis() + WIFI_RETRY_COOLDOWN_MS;
        connectTimeoutMs = min<uint32_t>(connectTimeoutMs * 2, WIFI_MAX_TIMEOUT_MS);
        LED_OFF();
        WiFi.disconnect();
        switchToDefaultsIfNeeded();
      }
    } break;

    case WIFI_CONNECTED: {
      static uint32_t lastChk = 0;
      if (millis() - lastChk >= 2000) {
        lastChk = millis();

        if (WiFi.status() != WL_CONNECTED) {
          if (g_cfg.auth_type == 2) {
            setEapStage(EAP_ERROR, "Link perdido durante EAP");
            coderpLog("ERROR", "Link perdido");
          }
          Serial.println(F("[WiFi] Conexão perdida. Retentando..."));
          wifiConnState = WIFI_BACKOFF;
          nextRetryAt = millis() + 1000;
          WiFi.disconnect();
          LED_OFF();

          g_blynkConnected = false;   // caiu Wi-Fi => considera Blynk down
        } else {
          // Garante Blynk inicializado…
          beginBlynkIfReady();

          // …e tenta reconectar ao cloud se ainda não estiver conectado
          static uint32_t lastBlynkTry = 0;
          if (!Blynk.connected() && millis() - lastBlynkTry > 5000) {
            lastBlynkTry = millis();
            Blynk.connect(1000);  // tentativinha de 1s, não bloqueia
          }
        }
      }
    } break;


    case WIFI_BACKOFF:
      if ((int32_t)(millis() - nextRetryAt) >= 0) {
        Serial.printf("[WiFi] Nova tentativa (timeout=%ums)...\n", connectTimeoutMs);
        beginConnect();
      }
      break;
  }
}


// ====== DHT ======
void readDhtIfDue(){
  static uint32_t lastRead=0; const uint32_t PERIOD_MS=3000;
  if(millis()-lastRead<PERIOD_MS) return; lastRead=millis();

  float h_raw=dht.readHumidity(), t_raw=dht.readTemperature();
  if(isnan(h_raw)||isnan(t_raw)){
    lastDht.ok=false; lastDht.t=lastDht.h=NAN; lastDht.t_raw=t_raw; lastDht.h_raw=h_raw;
    Serial.println(F("[DHT] Falha na leitura.")); return;
  }
  float t_cal = t_raw*TEMP_GAIN + TEMP_OFFSET;
  float h_cal = clampf(h_raw*HUM_GAIN + HUM_OFFSET, 0.0f, 100.0f);

  lastDht={t_cal,h_cal,true,t_raw,h_raw,lastDht.t_avg,lastDht.h_avg,lastDht.n_avg};
  avgAddSample(t_cal,h_cal);

  Serial.printf("[DHT] RAW T=%.1f H=%.1f | CAL T=%.2f H=%.2f | AVG(%u) T=%.2f H=%.2f\n",
    t_raw,h_raw,t_cal,h_cal,lastDht.n_avg,lastDht.t_avg,lastDht.h_avg);
}

// ====== HTML/CSS ======
String htmlHeaderCss(){ return R"HTML(<style>
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
.wrap{max-width:900px;margin:0 auto}
.card{background:#fff;border-radius:12px;box-shadow:0 6px 18px rgba(0,0,0,.06);padding:18px;margin:12px 0}
.grid{display:grid;grid-template-columns:1fr 1fr;gap:12px}
.k{font-size:13px;color:#6b7280;text-transform:uppercase;letter-spacing:.08em}
.v{font-size:26px;font-weight:700;margin-top:6px}
.small{font-size:12px;color:#6b7280}
.foot{margin-top:16px;font-size:12px;color:#6b7280}
.ok{display:inline-block;margin-left:8px;width:10px;height:10px;border-radius:50%;background:#10b981}
.bad{background:#ef4444}
button{border:0;border-radius:10px;padding:10px 14px;cursor:pointer}
.btn{background:#0b63b6;color:#fff}
input,select{width:100%;padding:10px;border:1px solid #d1d5db;border-radius:10px;margin-top:4px;font-size:16px}
label{display:block;font-size:14px;color:#374151;margin-top:8px}
pre{white-space:pre-wrap;word-break:break-word;background:#f9fafb;border-radius:10px;padding:12px}
.mac{margin-left:12px;font-weight:500;opacity:.9}
@media(max-width:900px){.layout{flex-direction:column}.side{width:100%;height:auto;display:flex;gap:6px}}
.hidden{display:none}
</style>)HTML"; }

String htmlSidebar(const String& active){
  String aHome=(active=="home")?"active":"", aCfg=(active=="config")?"active":"",
         aCal=(active=="calib")?"active":"", aMac=(active=="mac")?"active":"";
  String s;
  s  = "<div class='side'><div class='brand'>ESP8266</div>";
  s += "<a class='"+aHome+"' href='/'>Início</a>";
  s += "<a class='"+aCfg +"' href='/config'>Configuração</a>";
  s += "<a class='"+aMac +"' href='/mac'>MAC Address</a>";
  s += "<a class='"+aCal +"' href='/calib'>Calibração</a>";
  s += "<a href='/ws/coleta' target='_blank'>API /ws/coleta</a>";
  s += "<a href='/ws/reboot'>Reiniciar</a></div>";
  return s;
}

String htmlPage(){
  String ip = WiFi.isConnected()? WiFi.localIP().toString() : "-";
  String mac=WiFi.macAddress();
  String s;
  s.reserve(7000);
  s += "<!doctype html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>";
  s += "<title>ESP8266 - Temperatura & Umidade</title>";
  s += htmlHeaderCss();
  s += "</head><body><div class='top'>ESP8266 • Temperatura & Umidade ";
  s += "<span class='mac'>MAC: "+mac+"</span>";
  s += " <span class='badge'>v"; s += FW_VERSION; s += "</span>";
  s += "</div><div class='layout'>";
  s += htmlSidebar("home");
  s += "<div class='main'><div class='wrap'><div class='card'>";
  s += "<div class='k'>SSID atual</div><div class='v'>"; s+= g_active_ssid; s+="</div>";
  s += "<div class='grid' style='margin-top:12px'>";
  s += "<div><div class='k'>Temperatura (média)</div><div id='temp' class='v'>--</div>";
  s += "<div class='small'>Média das últimas <span id='navg'>--</span> leituras</div></div>";
  s += "<div><div class='k'>Umidade (média)</div><div id='hum' class='v'>--</div>";
  s += "<div class='small'>Média das últimas <span id='navg2'>--</span> leituras</div></div>";
  s += "</div>";
  s += "<div class='small' style='margin-top:10px'>";
  s += "Segurança: <b id='sec'>--</b> • Banda: <b id='band'>--</b> • Canal: <b id='ch'>--</b> • Freq: <b id='cf'>--</b> • AuthType: <b id='atype'>--</b> • MAC: <b id='macv'>";
  s += mac; s += "</b></div>";
  s += "<div class='foot'>IP: <span id='ip'>"; s+=ip; s+= "</span><span id='status' class='ok'></span><span id='time' style='margin-left:12px'></span></div>";
  s += "<div style='margin-top:10px'><button class='btn' onclick='refreshNow()'>Atualizar</button></div>";
  s += "</div><div class='card'><div class='k'>Web API (JSON)</div><pre id='apiOut'>GET /ws/coleta → aguarde...</pre></div>";
  s += "</div></div></div>";
  s += "<script>";
  s += "function pick(d,keys){for(const k of keys){if(d&&Object.prototype.hasOwnProperty.call(d,k)&&d[k]!=null)return d[k];}return null;}";
  s += "function pickNum(d,keys){const v=pick(d,keys);if(v==null)return null;const n=+v;return Number.isFinite(n)?n:null;}";
  s += "function setVals(d){const tAvg=pickNum(d,[\"temperature_avg\",\"temp_avg\",\"t_avg\"]);const hAvg=pickNum(d,[\"humidity_avg\",\"hum_avg\",\"h_avg\"]);const nAvg=pickNum(d,[\"n_avg\",\"n\",\"samples\"]);";
  s += "document.getElementById('temp').textContent=(tAvg!=null)?(tAvg.toFixed(1)+' °C'):'--';";
  s += "document.getElementById('hum').textContent =(hAvg!=null)?(hAvg.toFixed(1)+' %') :'--';";
  s += "document.getElementById('navg').textContent=(nAvg!=null)?nAvg:'--';";
  s += "document.getElementById('navg2').textContent=(nAvg!=null)?nAvg:'--';";
  s += "document.getElementById('status').className=(tAvg!=null&&hAvg!=null)?'ok':'ok bad';";
  s += "document.getElementById('time').textContent=new Date().toLocaleTimeString();}";
  s += "async function refreshNow(){try{const r=await fetch('/dht',{cache:'no-store'});const j=await r.json();const d=(j&&(j.data||j.payload))?(j.data||j.payload):j;setVals(d);}catch(e){setVals(null);}}";
  s += "async function refreshApi(){try{const r=await fetch('/ws/coleta',{cache:'no-store'});const j=await r.json();";
  s += "document.getElementById('apiOut').textContent=JSON.stringify(j,null,2);";
  s += "document.getElementById('sec').textContent=j.security||'--';";
  s += "document.getElementById('band').textContent=j.band||'--';";
  s += "document.getElementById('ch').textContent=(j.channel??'--');";
  s += "document.getElementById('cf').textContent=(j.center_freq_mhz?(j.center_freq_mhz+' MHz'):'--');";
  s += "document.getElementById('atype').textContent=(j.auth_type==2?'Enterprise':'PSK');";
  s += "document.getElementById('macv').textContent=j.mac||document.getElementById('macv').textContent;";
  s += "}catch(e){document.getElementById('apiOut').textContent='Erro ao chamar /ws/coleta';}}";
  s += "refreshNow();setInterval(refreshNow,2000);refreshApi();setInterval(refreshApi,4000);";
  s += "</script></body></html>";
  return s;
}

String htmlMacPage(){
  String cur = WiFi.macAddress();
  String checked = g_cfg.mac_custom?"checked":"";
  String curCustom = g_cfg.mac_custom? macToString(g_cfg.mac) : cur;
  String html;
  html.reserve(4000);
  html += "<!doctype html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>";
  html += "<title>MAC Address</title>";
  html += htmlHeaderCss();
  html += "</head><body><div class='top'>ESP8266 • MAC Address</div><div class='layout'>";
  html += htmlSidebar("mac");
  html += "<div class='main'><div class='wrap'><div class='card'>";
  html += "<div class='k'>Endereço MAC atual (STA)</div><div class='v'>"+cur+"</div>";
  html += "<form method='POST' action='/ws/savemac' style='margin-top:14px'>";
  html += "<label><input type='checkbox' name='use_custom' value='1' "+checked+"> Usar MAC customizado</label>";
  html += "<label>MAC custom (XX:XX:XX:XX:XX:XX)</label>";
  html += "<input type='text' name='mac' maxlength='17' value='"+curCustom+"'>";
  html += "<button class='btn' type='submit'>Salvar</button>";
  html += "<div class='foot'>Aplica no próximo ciclo de conexão.</div>";
  html += "</form></div></div></div></div></body></html>";
  return html;
}

String htmlCalibPage(){
  String s; s.reserve(3000);
  s += "<!doctype html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>";
  s += "<title>Calibração</title>";
  s += htmlHeaderCss();
  s += "</head><body><div class='top'>ESP8266 • Calibração (ganho/offset)</div><div class='layout'>";
  s += htmlSidebar("calib");
  s += "<div class='main'><div class='wrap'><div class='card'><div class='k'>Coeficientes atuais</div><pre>";
  s += "TEMP_GAIN="+String(TEMP_GAIN,4)+"\n";
  s += "TEMP_OFFSET="+String(TEMP_OFFSET,2)+"\n";
  s += "HUM_GAIN="+String(HUM_GAIN,4)+"\n";
  s += "HUM_OFFSET="+String(HUM_OFFSET,2)+"\n";
  s += "</pre>";
  s += "<form method='POST' action='/ws/savecalib'>";
  s += "<h3>1) Definir coeficientes manualmente</h3>";
  s += "<label>Temp - Ganho (a_T)</label><input name='t_gain' type='number' step='0.0001' value='"+String(TEMP_GAIN,4)+"'>";
  s += "<label>Temp - Offset (b_T)</label><input name='t_off'  type='number' step='0.01'   value='"+String(TEMP_OFFSET,2)+"'>";
  s += "<label>Umid - Ganho (a_H)</label><input name='h_gain' type='number' step='0.0001' value='"+String(HUM_GAIN,4)+"'>";
  s += "<label>Umid - Offset (b_H)</label><input name='h_off'  type='number' step='0.01'   value='"+String(HUM_OFFSET,2)+"'>";
  s += "<button class='btn' type='submit' name='mode' value='set'>Salvar coeficientes</button>";
  s += "</form><hr>";
  s += "<form method='POST' action='/ws/savecalib'>";
  s += "<h3>2) Auto-calibrar pela leitura atual</h3>";
  s += "<label>Temperatura referência (°C)</label><input name='t_ref' type='number' step='0.1' required>";
  s += "<label>Umidade referência (%RH)</label><input name='h_ref' type='number' step='0.1' required>";
  s += "<input type='hidden' name='t_gain' value='"+String(TEMP_GAIN,4)+"'>";
  s += "<input type='hidden' name='h_gain' value='"+String(HUM_GAIN,4)+"'>";
  s += "<button class='btn' type='submit' name='mode' value='auto'>Auto-calibrar</button>";
  s += "</form>";
  s += "<div class='foot'>Média móvel de até 10 leituras. y_cal = a·y_raw + b.</div>";
  s += "</div></div></div></div></body></html>";
  return s;
}

String htmlConfigPage(){
  uint8_t at = g_cfg.auth_type;
  String html; html.reserve(9000);
  html += "<!doctype html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>";
  html += "<title>Configuração de Wi-Fi</title>";
  html += htmlHeaderCss();
  html += "</head><body><div class='top'>ESP8266 • Configuração</div><div class='layout'>";
  html += htmlSidebar("config");
  html += "<div class='main'><div class='wrap'><div class='card'>";
  html += "<form method='POST' action='/ws/savewifi' id='wf'>";
  html += "<label>Tipo de Autenticação</label>";
  html += "<select name='auth_type' id='auth_type'>";
  html += "<option value='1'>Wi-Fi Normal (PSK)</option>";
  html += "<option value='2'>Corporativa (WPA2-Enterprise PEAP/MSCHAPv2)</option>";
  html += "</select>";
  html += "<label>SSID (até 32 chars)</label><input type='text' name='ssid' maxlength='32' required value='"+g_cfg.ssid+"'>";
  html += "<div id='psk_blk'>";
  html += "<label>Senha (até 64 chars)</label><input type='password' name='pass' maxlength='64' placeholder='••••••••' value='"+g_cfg.pass+"'>";
  html += "</div>";
  html += "<div id='eap_blk' class='hidden'>";
  html += "<h3>Enterprise (PEAP + MSCHAPv2)</h3>";
  html += "<div class='small'><b>Outer Identity (fixo):</b> anonymous@coderp.sp.gov.br</div>";
  html += "<label>EAP Username (inner)</label><input type='text' name='eap_username' maxlength='64' value='"+g_cfg.eap_username+"'>";
  html += "<label>EAP Password</label><input type='password' name='eap_password' maxlength='64' placeholder='••••••••' value='"+g_cfg.eap_password+"'>";
  html += "<div class='small'>A CA do RADIUS de coderp.sp.gov.br é aplicada automaticamente.</div>";
  html += "</div>";
  html += "<button class='btn' type='submit'>Salvar</button>";
  html += "<div class='foot'>Após salvar, a conexão inicia automaticamente.</div>";
  html += "</form>";
  html += "</div></div></div></div>";
  html += "<script>";
  html += "const sel=document.getElementById('auth_type'); const psk=document.getElementById('psk_blk'); const eap=document.getElementById('eap_blk');";
  html += "function upd(){ const v=sel.value|0; psk.classList.toggle('hidden', v!=1); eap.classList.toggle('hidden', v!=2);} ";
  html += "sel.value='"+String(at)+"'; upd(); sel.addEventListener('change',upd);";
  html += "</script></body></html>";
  return html;
}

// ====== JSONs ======
void sendJson(const String& body){
  server.sendHeader("Access-Control-Allow-Origin","*");
  server.sendHeader("Cache-Control","no-store, no-cache, must-revalidate");
  server.send(200, "application/json; charset=utf-8", body);
}
void handleDhtJson(){
  String out="{";
  out += "\"temperature_avg\":" + String((lastDht.ok&&!isnan(lastDht.t_avg))?String(lastDht.t_avg,2):"null") + ",";
  out += "\"humidity_avg\":"    + String((lastDht.ok&&!isnan(lastDht.h_avg))?String(lastDht.h_avg,2):"null") + ",";
  out += "\"n_avg\":" + String(lastDht.n_avg) + ",";
  out += "\"temperature\":" + String(lastDht.ok?String(lastDht.t,2):"null") + ",";
  out += "\"humidity\":"    + String(lastDht.ok?String(lastDht.h,2):"null") + ",";
  out += "\"raw_temperature\":" + String(lastDht.ok?String(lastDht.t_raw,2):"null") + ",";
  out += "\"raw_humidity\":"    + String(lastDht.ok?String(lastDht.h_raw,2):"null") + ",";

  // aliases legado
  out += "\"t_avg\":" + String((lastDht.ok&&!isnan(lastDht.t_avg))?String(lastDht.t_avg,2):"null") + ",";
  out += "\"h_avg\":" + String((lastDht.ok&&!isnan(lastDht.h_avg))?String(lastDht.h_avg,2):"null") + ",";
  out += "\"t\":"     + String(lastDht.ok?String(lastDht.t,2):"null") + ",";
  out += "\"h\":"     + String(lastDht.ok?String(lastDht.h,2):"null") + ",";

  out += "\"calib\":{"
          "\"TEMP_GAIN\":" + String(TEMP_GAIN,4) + ","
          "\"TEMP_OFFSET\":" + String(TEMP_OFFSET,2) + ","
          "\"HUM_GAIN\":" + String(HUM_GAIN,4) + ","
          "\"HUM_OFFSET\":" + String(HUM_OFFSET,2) +
         "},";

  out += "\"fw\":{"
          "\"name\":\""    + String(FW_NAME)        + "\","
          "\"version\":\"" + String(FW_VERSION)     + "\","
          "\"build\":\""   + String(FW_BUILD)       + "\""
         "}";
  out += "}";
  sendJson(out);
}
void handleWsColeta(){
  bool connected = WiFi.status()==WL_CONNECTED;
  uint8_t ch = WiFi.channel();
  String out="{";
  out += "\"device\":\""+String(FW_NAME)+"\",";
  out += "\"mac\":\""+WiFi.macAddress()+"\",";
  out += "\"fw\":{"
          "\"name\":\""    + String(FW_NAME)        + "\","
          "\"version\":\"" + String(FW_VERSION)     + "\","
          "\"build\":\""   + String(FW_BUILD)       + "\","
          "\"sdk\":\""     + String(ESP.getSdkVersion()) + "\""
         "},";
  out += "\"mac_custom\":" + String(g_cfg.mac_custom?"true":"false") + ",";
  out += "\"ip\":" + (connected?("\""+WiFi.localIP().toString()+"\""):"null") + ",";
  out += "\"ssid\":\""+g_active_ssid+"\",";
  out += "\"auth_type\":" + String((int)g_cfg.auth_type) + ",";
  out += "\"using_default\":" + String(usingDefault?"true":"false") + ",";
  out += "\"attempts\":" + String((int)failCount) + ",";
  out += "\"ok\":" + String(lastDht.ok?"true":"false") + ",";
  out += "\"blynk_connected\":" + String(g_blynkConnected?"true":"false") + ",";  // <<< NOVO
  out += "\"security\":\"" + String(currentSecurity()) + "\",";
  out += "\"band\":\"" + currentBand() + "\",";
  out += "\"channel\":" + String(ch) + ",";
  out += "\"center_freq_mhz\":" + String(centerFreqMHz(ch)) + ",";
  out += "\"temperature_avg\":" + String((lastDht.ok&&!isnan(lastDht.t_avg))?String(lastDht.t_avg,2):"null") + ",";
  out += "\"humidity_avg\":"    + String((lastDht.ok&&!isnan(lastDht.h_avg))?String(lastDht.h_avg,2):"null") + ",";
  out += "\"n_avg\":"           + String(lastDht.n_avg) + ",";
  out += "\"temperature\":"     + String(lastDht.ok?String(lastDht.t,2):"null") + ",";
  out += "\"humidity\":"        + String(lastDht.ok?String(lastDht.h,2):"null") + ",";
  out += "\"raw_temperature\":" + String(lastDht.ok?String(lastDht.t_raw,2):"null") + ",";
  out += "\"raw_humidity\":"    + String(lastDht.ok?String(lastDht.h_raw,2):"null") + ",";
  out += "\"ts_ms\":" + String(millis()) + "}";
  sendJson(out);
}

void handleState(){
  wl_status_t st = WiFi.status();
  const char* st_name =
      (st==WL_CONNECTED)    ? "CONNECTED" :
      (st==WL_IDLE_STATUS)  ? "IDLE" :
      (st==WL_NO_SSID_AVAIL)? "NO_SSID" :
      (st==WL_CONNECT_FAILED)? "CONNECT_FAILED" :
      (st==WL_CONNECTION_LOST)? "CONNECTION_LOST" :
      (st==WL_DISCONNECTED) ? "DISCONNECTED" : "UNKNOWN";

  const char* sm_name =
      (wifiConnState==WIFI_IDLE)       ? "IDLE" :
      (wifiConnState==WIFI_CONNECTING) ? "CONNECTING" :
      (wifiConnState==WIFI_CONNECTED)  ? "CONNECTED" :
      (wifiConnState==WIFI_BACKOFF)    ? "BACKOFF" : "NA";

  String out = "{";
  out += "\"ssid\":\""+ g_active_ssid +"\",";
  out += "\"auth_type\":"+ String((int)g_cfg.auth_type) +",";
  out += "\"using_default\":"+ String(usingDefault?"true":"false") +",";
  out += "\"wifi_status\":"+ String((int)st) +",";
  out += "\"wifi_status_name\":\""+ String(st_name) +"\",";
  out += "\"state\":\""+ String(sm_name) +"\",";

  // Pipeline EAP no JSON
  const char* eap_name =
    (g_eapStage==EAP_IDLE?"IDLE":g_eapStage==EAP_INIT?"INIT":g_eapStage==EAP_SET_SSID?"SET_SSID":
     g_eapStage==EAP_SET_CA?"SET_CA":g_eapStage==EAP_SET_IDENTITY?"IDENTITY":g_eapStage==EAP_SET_USERNAME?"USERNAME":
     g_eapStage==EAP_SET_PASSWORD?"PASSWORD":g_eapStage==EAP_ENABLE?"ENABLE":g_eapStage==EAP_CONNECT?"CONNECT":
     g_eapStage==EAP_SUCCESS?"SUCCESS":"ERROR");
  out += "\"eap_stage\":"+ String((int)g_eapStage) + ",";
  out += "\"eap_stage_name\":\"" + String(eap_name) + "\",";
  out += "\"eap_msg\":\"" + g_eapMsg + "\",";

  out += "\"attempts\":"+ String((int)failCount) +",";
  out += "\"last_reason\":"+ String((int)g_lastDiscReason) +",";
  out += "\"last_reason_str\":\""+ String(wifiReasonToStr(g_lastDiscReason)) +"\",";
  out += "\"connected\":" + String(st==WL_CONNECTED?"true":"false") + ",";
  out += "\"ip\":" + ((st==WL_CONNECTED)?("\""+WiFi.localIP().toString()+"\""):"null");
  out += "}";
  sendJson(out);
}

// ====== Handlers ======
void handleConfig(){ server.send(200, "text/html; charset=utf-8", htmlConfigPage()); }
void handleMacPage(){ server.send(200, "text/html; charset=utf-8", htmlMacPage()); }
void handleSaveMac(){
  bool use_custom = server.hasArg("use_custom");
  String macs = argOrEmpty("mac"); macs.trim();
  uint8_t m[6];
  if(use_custom){
    if(!parseMacString(macs,m)){ server.send(400,"text/plain; charset=utf-8","MAC inválido. Use XX:XX:XX:XX:XX:XX"); return; }
    g_cfg.mac_custom=true; memcpy(g_cfg.mac,m,6);
  } else {
    g_cfg.mac_custom=false; memset(g_cfg.mac,0,6);
  }
  bool ok = eepromSaveWifi();
  String msg = ok? "✅ MAC atualizado (persistente)." : "❌ Falha ao salvar MAC.";
  String html;
  html.reserve(2000);
  html += "<!doctype html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>";
  html += "<title>MAC salvo</title>";
  html += htmlHeaderCss();
  html += "</head><body><div class='top'>ESP8266 • MAC Address</div><div class='layout'>";
  html += htmlSidebar("mac");
  html += "<div class='main'><div class='wrap'><div class='card'>";
  html += msg;
  html += "<br>Modo: "; html += (g_cfg.mac_custom? "custom" : "factory");
  html += "<br>MAC: "; html += (g_cfg.mac_custom? macToString(g_cfg.mac) : WiFi.macAddress());
  html += "<div style='margin-top:10px'><a class='btn' href='/mac'>Voltar</a> <a class='btn' style='margin-left:8px' href='/ws/reboot'>Reiniciar</a></div></div></div></div></div></body></html>";
  server.send(200, "text/html; charset=utf-8", html);
}

void handleSaveCalib(){
  String mode = server.hasArg("mode") ? server.arg("mode") : "set";
  float t_gain=TEMP_GAIN, t_off=TEMP_OFFSET, h_gain=HUM_GAIN, h_off=HUM_OFFSET;
  if(mode=="set"){
    if(server.hasArg("t_gain")) t_gain=server.arg("t_gain").toFloat();
    if(server.hasArg("t_off"))  t_off =server.arg("t_off").toFloat();
    if(server.hasArg("h_gain")) h_gain=server.arg("h_gain").toFloat();
    if(server.hasArg("h_off"))  h_off =server.arg("h_off").toFloat();
  }else if(mode=="auto"){
    float t_ref=server.hasArg("t_ref")?server.arg("t_ref").toFloat():NAN;
    float h_ref=server.hasArg("h_ref")?server.arg("h_ref").toFloat():NAN;
    readDhtIfDue();
    if(!lastDht.ok){
      float h_raw=dht.readHumidity(), t_raw=dht.readTemperature();
      if(!isnan(h_raw)&&!isnan(t_raw)){ lastDht.t_raw=t_raw; lastDht.h_raw=h_raw; lastDht.ok=true; }
    }
    if(isnan(t_ref)||isnan(h_ref)||!lastDht.ok){
      server.send(400,"text/plain; charset=utf-8","Auto-calibracao requer t_ref/h_ref e leitura atual OK."); return;
    }
    if(server.hasArg("t_gain")) t_gain=server.arg("t_gain").toFloat();
    if(server.hasArg("h_gain")) h_gain=server.arg("h_gain").toFloat();
    t_off = t_ref - (t_gain * lastDht.t_raw);
    h_off = h_ref - (h_gain * lastDht.h_raw);
  }
  bool ok=eepromSaveCalib(t_gain,t_off,h_gain,h_off); if(ok) avgReset();
  String html;
  html.reserve(2000);
  html += "<!doctype html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>";
  html += "<title>Calibração salva</title>";
  html += htmlHeaderCss();
  html += "</head><body><div class='top'>ESP8266 • Calibração</div><div class='layout'>";
  html += htmlSidebar("calib");
  html += "<div class='main'><div class='wrap'><div class='card'>";
  html += (ok? "✅ Coeficientes salvos e média reiniciada." : "❌ Falha ao gravar.");
  html += "<pre>";
  html += String("TEMP_GAIN=")+String(TEMP_GAIN,4)+"\n";
  html += String("TEMP_OFFSET=")+String(TEMP_OFFSET,2)+"\n";
  html += String("HUM_GAIN=")+String(HUM_GAIN,4)+"\n";
  html += String("HUM_OFFSET=")+String(HUM_OFFSET,2)+"\n";
  html += "</pre><a class='btn' href='/calib'>Voltar</a> <a class='btn' style='margin-left:8px' href='/'>Início</a>"
          "</div></div></div></div></body></html>";
  server.send(200,"text/html; charset=utf-8",html);
}

void blynkTickSend() {
  // IP
  String ip = (WiFi.status() == WL_CONNECTED) ? WiFi.localIP().toString() : String("");
  Blynk.virtualWrite(V10, ip);

  if (!lastDht.ok) return;

  // Instantâneo
  if (!isnan(lastDht.t)) Blynk.virtualWrite(V6, lastDht.t);  // t
  if (!isnan(lastDht.h)) Blynk.virtualWrite(V7, lastDht.h);  // h

  // Médias
  if (!isnan(lastDht.t_avg)) Blynk.virtualWrite(V4, lastDht.t_avg); // t_avg
  if (!isnan(lastDht.h_avg)) Blynk.virtualWrite(V5, lastDht.h_avg); // h_avg
}



void handleSaveWifi(){
  String ssid = argOrEmpty("ssid"); ssid.trim();
  String pass = argOrEmpty("pass"); pass.trim();
  int auth_type_form = server.hasArg("auth_type") ? server.arg("auth_type").toInt() : 0;

  // Outer identity é fixo (não vem da UI)
  String eap_id  = EAP_OUTER_IDENTITY_FIXED;
  String eap_usr = argOrEmpty("eap_username");  eap_usr.trim();
  String eap_pwd = argOrEmpty("eap_password");  // senha: não trime; remove CR/LF finais
  while(eap_pwd.endsWith("\r")||eap_pwd.endsWith("\n")) eap_pwd.remove(eap_pwd.length()-1);

  bool looksEAP = (eap_usr.length() && eap_pwd.length());
  int auth_type = looksEAP ? 2 : (auth_type_form==2?2:1);

  if(ssid.length()==0 || ssid.length()>32){ server.send(400,"text/plain; charset=utf-8","SSID inválido."); return; }
  if(auth_type==1 && pass.length()>64){ server.send(400,"text/plain; charset=utf-8","Senha PSK muito longa."); return; }
  if(auth_type==2 && !looksEAP){ server.send(400,"text/plain; charset=utf-8","Para Enterprise preencha Username/Password."); return; }

  g_cfg.auth_type    = auth_type;
  g_cfg.ssid         = ssid;
  g_cfg.pass         = pass;
  g_cfg.eap_identity = eap_id;       // OUTER fixo
  g_cfg.eap_username = eap_usr;      // INNER
  g_cfg.eap_password = eap_pwd;      // INNER

  if(auth_type==2){
    Serial.println("[EAP] Processo de autenticacao corporativa (Tipo 2) iniciado");
    Serial.printf("[EAP] SSID='%s' outer='%s' inner='%s'\n",
      ssid.c_str(), eap_id.c_str(), eap_usr.c_str());
  }

  dumpWifiCfg("savewifi");

  if(eepromSaveWifi()){
    WiFi.disconnect();
    wifiConnState = WIFI_BACKOFF; connectTimeoutMs=WIFI_FIRST_TIMEOUT_MS; nextRetryAt=millis();
    failCount = 0; g_lastDiscReason=0; setEapStage(EAP_IDLE,"");

    String html;
    html.reserve(5000);
    html += "<!doctype html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>";
    html += "<title>Conectando...</title>";
    html += htmlHeaderCss();
    html += "</head><body><div class='top'>ESP8266 • Salvando & Conectando</div><div class='layout'>";
    html += htmlSidebar("config");
    html += "<div class='main'><div class='wrap'><div class='card'>";
    html += "✅ Credenciais salvas na EEPROM.<br>";
    html += "SSID: <b>"+ssid+"</b><br>AuthType: ";
    html += (g_cfg.auth_type==2?"Enterprise (PEAP; outer fixo; CA coderp)":"PSK");
    html += "<div class='small' style='margin-top:10px'>Tentando conexão automaticamente...<br>";
    html += "Aguarde. Esta página irá redirecionar quando a conexão for estabelecida.</div>";
    html += "<pre id='st'>Iniciando...</pre>";
    html += "<div style='margin-top:10px'><a class='btn' href='/'>Ir para Início</a></div>";
    html += "</div></div></div></div>";
    html += "<script>async function poll(){try{const r=await fetch('/ws/state',{cache:'no-store'});const j=await r.json();";
    html += "document.getElementById('st').textContent=JSON.stringify(j,null,2); if(j.connected){ location.href='/'; return; }}catch(e){} setTimeout(poll,1000);} poll();</script>";
    html += "</body></html>";
    server.send(200,"text/html; charset=utf-8",html);
  } else {
    server.send(500,"text/plain; charset=utf-8","Falha ao gravar EEPROM.");
  }
}

void handleReboot(){ server.send(200,"text/plain; charset=utf-8","Reiniciando..."); server.client().flush(); delay(100); ESP.restart(); }
void handleNotFound(){
  if(server.uri().startsWith("/ws/")||server.uri().startsWith("/dht"))
    server.send(404,"application/json","{\"error\":\"not found\"}");
  else
    server.send(200,"text/html; charset=utf-8", htmlPage());
}

// ====== Eventos Wi-Fi ======
void onWifiDisconnected(const WiFiEventStationModeDisconnected& ev){
  g_lastDiscReason = ev.reason;
  Serial.printf("[WiFi] Disc SSID='%s' Reason=%u (%s)\n", ev.ssid.c_str(), ev.reason, wifiReasonToStr(ev.reason));
}

// ====== Setup/Loop ======
void setup(){
  pinMode(LED_BUILTIN, OUTPUT); LED_OFF();
  Serial.begin(115200); delay(10);
  Serial.printf("[FW] %s v%s (build %s) | SDK=%s\n", FW_NAME, FW_VERSION, FW_BUILD, ESP.getSdkVersion());
  printResetInfo();




  EEPROM.begin(EEPROM_SIZE);

  if(!eepromLoadWifi()){
    Serial.println(F("[EEPROM] Sem credenciais. Usando DEFAULT_* (PSK)."));
    loadDefaults(); usingDefault=true;
  }
  // reforça identity fixo em runtime
  g_cfg.eap_identity = EAP_OUTER_IDENTITY_FIXED;

  g_active_ssid = g_cfg.ssid; dumpWifiCfg("boot");

  calibSetDefaultsIfEmpty(); avgReset(); dht.begin();

  g_onDisc = WiFi.onStationModeDisconnected(onWifiDisconnected);
 
  server.on("/",             [](){ server.send(200,"text/html; charset=utf-8", htmlPage()); });
  server.on("/config",       handleConfig);
  server.on("/mac",          handleMacPage);
  server.on("/calib",        [](){ server.send(200,"text/html; charset=utf-8", htmlCalibPage()); });
  server.on("/dht",          handleDhtJson);
  server.on("/ws/coleta",    HTTP_GET, handleWsColeta);
  server.on("/ws/state",     HTTP_GET, handleState);
  server.on("/ws/savewifi",  HTTP_POST, handleSaveWifi);
  server.on("/ws/savemac",   HTTP_POST, handleSaveMac);
  server.on("/ws/savecalib", HTTP_POST, handleSaveCalib);
  server.on("/ws/reboot",    handleReboot);
  server.on("/ws/min", HTTP_GET, handleWsMin);
  server.onNotFound(handleNotFound);

  server.begin(); Serial.println(F("[HTTP] Servidor iniciado na porta 80"));

  wifiInitIfNeeded();
}

void loop(){
  wifiStateMachine();
  readDhtIfDue();
  server.handleClient();
  // Blynk: tenta iniciar quando o Wi-Fi estiver de pé
  beginBlynkIfReady();

  // Blynk: processa eventos e timers
  Blynk.run();
  blynkTimer.run();

  static uint32_t last=0;
  if(millis()-last>=5000){
    last=millis();
    wl_status_t st=WiFi.status();
    Serial.printf("[TICK] WiFi=%d SSID='%s'", (int)st, g_active_ssid.c_str());
    if(st==WL_CONNECTED){
      Serial.printf(" IP=%s RSSI=%d dBm Sec=%s MAC=%s",
        WiFi.localIP().toString().c_str(), WiFi.RSSI(), currentSecurity(), WiFi.macAddress().c_str());
    }
    if(lastDht.ok){
      Serial.printf(" | INST T=%.2f H=%.2f AVG(%u) T=%.2f H=%.2f RAW T=%.2f H=%.2f (aT=%.3f,bT=%.2f aH=%.3f,bH=%.2f)",
        lastDht.t,lastDht.h,lastDht.n_avg,lastDht.t_avg,lastDht.h_avg,lastDht.t_raw,lastDht.h_raw,
        TEMP_GAIN,TEMP_OFFSET,HUM_GAIN,HUM_OFFSET);
    }
    Serial.println();
  }
  safeYield();
}
