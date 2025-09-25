# 🌡️ Monitor de Temperatura & Umidade  
**Plataforma:** Arduino Mega 2560 + Ethernet Shield + DHT22  

Sistema embarcado para leitura de temperatura/umidade, interface web responsiva e registro em SD.

---

## ✨ Funcionalidades
- 📊 Leitura de temperatura/umidade (DHT22).
- 🖥️ Exibição em LCD I²C 16×2.
- 🌐 Servidor web na porta `8081` com Bootstrap.
- 🗄️ Registro histórico em cartão SD (`LYYYYMM.CSV`).
- ⏰ Sincronização NTP (UTC) a cada 10 min.
- ⚙️ Configuração de rede persistente na EEPROM (DHCP ou estático).
- 🔧 Calibração direta ou de 2 pontos (ganho + offset).
- 🔔 Alarme sonoro ao cruzar 30 °C.

---

## 🔌 Hardware
- **Placa:** Arduino Mega 2560  
- **Shield:** Ethernet (W5100/W5500) + SD no mesmo barramento SPI  
- **Sensores:**
  - DHT22 em D6
  - LCD I²C (0x27)
- **Outros:**
  - Buzzer (A11) + enable (A8)
  - LED status (D7)

---

## 📚 Bibliotecas
- `DHT.h`
- `LiquidCrystal_I2C.h`
- `Ethernet.h`, `EthernetUdp.h`
- `Dns.h`, `SPI.h`
- `EEPROM.h`
- `SdFat.h` (v2+)

---

## 🌐 Rede e EEPROM
- **DHCP** ou IP estático (`172.17.240.253` padrão).
- Configurações persistentes em EEPROM:
  - `use_static`, `ip`, `dns`, `gw`, `mask`, `mac`
  - `kTemp`, `aTemp`, `kHum`, `aHum`
- `magic` (0x42) e `checksum` para validar dados.

---

## 🗂️ Registro no SD
- Arquivos por mês: `LYYYYMM.CSV`
- Cabeçalho: `epoch,temperature,humidity`
- Só grava se ΔT ≥ 1 °C ou ΔH ≥ 1 %

---

## 🌍 Interface Web
### Páginas
- `/` – Status e configuração de rede
- `/historico` – Gráficos (Chart.js)
- `/export` – Download e limpeza de CSV
- `/calibracao` – Ajuste de k/offset ou modo 2 pontos

### APIs
```http
GET /ws/temperatura      # JSON atual
GET /ws/log?hours=24     # Últimas 24h
GET /ws/log?yyyymm=202509
GET /ws/csv?yyyymm=202509
GET /ws/clear?yyyymm=202509
GET /ws/calib?kt=1&at=0&kh=1&ah=0
```

# 🏗️ Fluxo de Boot

## Configura pinos e periféricos.

- Lê EEPROM (ou aplica defaults).

- Conecta rede (DHCP/estático).

- Sincroniza NTP.

- Inicializa SD.

- Exibe IP no LCD.

Loop principal: leitura DHT, gravação condicional, HTTP.

## 🛠️ Compilação

IDE: Arduino IDE

Placa: Arduino Mega 2560

Serial: 115200 baud

## 🧪 Testes Rápidos
### JSON atual
curl http://<IP>:8081/ws/temperatura

### Log mês 2025-09
curl http://<IP>:8081/ws/log?yyyymm=202509

### Exportar CSV
curl -OJ http://<IP>:8081/ws/csv?yyyymm=202509


## 📜 Licença

Open Source - Livre uso e montagem, sem cobrança para este fim.
Proibido a venda por terceiros sem autorização prévia. 
Proibido a customização ou modificação.
Proibido a incorporação.