# Leitura de Temperatura e Umidade com DHT22 + LCD I²C (Arduino)

Projeto simples para ler temperatura e umidade usando um DHT22 e exibir os valores em um LCD 16x2 via I²C. Também envia os dados pela Serial (115200).

Observação importante: o código usa a classe SimpleDHT22 (sensor DHT22). O comentário inicial do exemplo menciona DHT11, mas o hardware e a classe utilizados são DHT22.

## 🧩 Hardware

- Arduino (Uno/Nano/MEGA — 5V ou 3.3V)
- Sensor DHT22
- LCD 16x2 I²C (endereços comuns: 0x27 ou 0x3F)
(Recomendado) Resistor de pull-up 10k entre DATA e VCC do DHT22
- Jumpers
- Ligações
- DHT22
- VCC → 5V (ou 3.3V)
- GND → GND
- DATA → A2 (conforme o código)
- LCD I²C (PCF8574)
- VCC → 5V
- GND → GND
- SDA → A4 (Arduino Uno/Nano)
- SCL → A5 (Arduino Uno/Nano)

Em placas diferentes, confira os pinos SDA/SCL (ex.: MEGA: SDA=20, SCL=21).

## 📦 Bibliotecas

Instale pela IDE do Arduino (Gerenciador de Bibliotecas) ou via ZIP:

SimpleDHT (para DHT22)

LiquidCrystal_I2C (LCD I²C)

Wire (já vem na IDE)

## 🔧 Configuração no Código
- int pinDHT22 = A2;                 // Pino DATA do DHT22
- SimpleDHT22 dht22(pinDHT22);
- LiquidCrystal_I2C lcd(0x27,16,2);  // Endereço I²C do LCD e dimensões


Se o LCD não aparecer, teste o endereço 0x3F.

Para outro pino do sensor, altere pinDHT22.

## ▶️ Como usar

Conecte o hardware conforme descrito.

Abra a IDE do Arduino → selecione a placa/porta correta.

Instale as bibliotecas.

Cole o código do seu sketch.

Faça o Upload.

Abra o Monitor Serial em 115200 baud.

## 🖥️ O que aparece

### No LCD

Linha 1: Temp:XX.XX C

Linha 2: Hum:YY.YY RH

### Na Serial

Temperatura:25.37
Humidade:58.42


(A leitura é atualizada a cada ~1,5 s.)

## 🛡️ Boas práticas

Use pull-up 10k no DATA do DHT22 para leituras estáveis.

Evite cabos longos no sensor (ou use cabo blindado).

Alimente o sensor com tensão estável (3.3V ou 5V).

DHT22 tem taxa de amostragem ~0,5–1 Hz (respeite delay(1500)).

🧪 Troubleshooting

LCD sem texto: verifique o endereço (0x27 ↔ 0x3F) e o contraste (trimpot da placa I²C).

Leitura falhou (Read DHT22 failed): confira cabos, pull-up e alimentação.

Valores “nan”/zero: verifique o pino DATA e o aterramento comum (GND único).

## 🗂️ Estrutura do Sketch

setup(): inicia Serial e LCD, liga backlight.

loop(): lê DHT22 (read2), formata e mostra no LCD, imprime na Serial, aguarda 1500 ms.

## 🔁 Personalizações rápidas

Mudar pino do DHT22: int pinDHT22 = A2; → A1/D2/...

Mudar endereço do LCD: LiquidCrystal_I2C lcd(0x27,16,2); → 0x3F

Ajustar intervalo de leitura: delay(1500); (evite < 1000 ms para DHT22)

## 📜 Licença

Open Source - Livre uso e montagem, sem cobrança para este fim.
Proibido a venda por terceiros sem autorização prévia. 
Proibido a customização ou modificação.
Proibido a incorporação.