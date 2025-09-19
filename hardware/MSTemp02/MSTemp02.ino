#include <DHT.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <SPI.h>
#include <Ethernet.h>

// ======== CONFIGURAÇÃO ========
#define USE_STATIC_IP  1  // <-- 1 = usa IP fixo, 0 = usa DHCP

// ======== Pinos ========
#define DHTPIN 6        // Recomendado: D6. Troque para A2 se preferir.
#define DHTTYPE DHT22
const int PIN_BUZZER  = A11;
const int PIN_BUZZ_G  = A8;
const int PIN_LED     = 7;

// ======== Objetos ========
DHT dht(DHTPIN, DHTTYPE);
LiquidCrystal_I2C lcd(0x27, 16, 2);

byte mac[] = {0xDE,0xAD,0xBE,0xEF,0xFE,0xED};
EthernetServer server(80);

// ======== IP Fixo (quando USE_STATIC_IP = 1) ========
IPAddress ip_static(172, 17, 240, 253);
IPAddress dns_static(172, 17, 240, 1);
IPAddress gw_static(172, 17, 240, 1);
IPAddress mask_static(255, 255, 255, 0);

// ======== Amostragem ========
unsigned long lastSampleMs = 0;
const unsigned long sampleIntervalMs = 2500;
unsigned long dhtWarmupUntil = 0;
float lastTemp = NAN, lastHum = NAN;

// ======== Controle ========
const float THRESH_C = 30.0;
bool wasBelowThreshold = true;

unsigned long ipSplashStartMs = 0;
bool ipSplashDone = false;
const unsigned long IP_SPLASH_MS = 10000;

void printIPToSerial() {
  Serial.print(F("IP: ")); Serial.print(Ethernet.localIP());
  Serial.print(F(" | Gateway: ")); Serial.print(Ethernet.gatewayIP());
  Serial.print(F(" | DNS: ")); Serial.print(Ethernet.dnsServerIP());
  Serial.print(F(" | Subnet: ")); Serial.println(Ethernet.subnetMask());
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

void setup() {
  Serial.begin(115200);

  pinMode(PIN_BUZZ_G, OUTPUT); digitalWrite(PIN_BUZZ_G, LOW);
  pinMode(PIN_BUZZER, OUTPUT); noTone(PIN_BUZZER);
  pinMode(PIN_LED, OUTPUT); digitalWrite(PIN_LED, LOW);
  pinMode(DHTPIN, INPUT_PULLUP);

  dht.begin();
  dhtWarmupUntil = millis() + 2000;

  lcd.init(); lcd.backlight(); lcd.clear();
  lcd.setCursor(0,0); lcd.print(F("Inicializando..."));

  pinMode(53, OUTPUT); digitalWrite(53, HIGH);
  pinMode(4, OUTPUT);  digitalWrite(4, HIGH);

  lcd.setCursor(0,1); lcd.print(F("ETH...        "));
  if (USE_STATIC_IP) {
    Ethernet.begin(mac, ip_static, dns_static, gw_static, mask_static);
    Serial.println(F("Usando IP fixo."));
  } else {
    if (Ethernet.begin(mac) == 0) {
      Serial.println(F("DHCP falhou. Usando fallback 192.168.1.177"));
      IPAddress ip(192,168,1,177), dns(192,168,1,1), gw(192,168,1,1), mask(255,255,255,0);
      Ethernet.begin(mac, ip, dns, gw, mask);
    }
  }
  delay(1000);
  server.begin();

  Serial.println(F("Servidor HTTP iniciado"));
  printIPToSerial();

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
    lastTemp = t;
    lastHum = h;

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

void handleHttp(EthernetClient &client) {
  String reqLine = client.readStringUntil('\r');
  client.read();
  while (client.connected()) {
    String h = client.readStringUntil('\n');
    if (h == "\r" || h.length()==1) break;
  }

  client.println(F("HTTP/1.1 200 OK"));
  client.println(F("Content-Type: application/json; charset=utf-8"));
  client.println(F("Access-Control-Allow-Origin: *"));
  client.println(F("Connection: close"));
  client.println();

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

void loop() {
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
    unsigned long t0 = millis();
    while (client.connected() && !client.available() && millis() - t0 < 1000) {}
    if (client.available()) handleHttp(client);
    delay(1);
    client.stop();
  }
}
