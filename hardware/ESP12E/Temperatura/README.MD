# MSTemp02
📘 Documentação – ESP8266 WiFi Setup + DHT11 + mDNS (srvTemp)
📖 Visão Geral

Este projeto implementa uma solução completa para configuração de Wi-Fi e monitoramento de temperatura/umidade usando um ESP8266 (NodeMCU/Lolin/ESP-12E).

Quando energizado, o dispositivo:

Tenta conectar ao último Wi-Fi salvo na EEPROM.

Se não encontrar ou falhar, cria uma rede Wi-Fi AP chamada srvTemp.

Disponibiliza uma página web (estilo D-Link, responsiva com Bootstrap) para seleção de SSID, senha e visualização de sensores.

Salva as credenciais na EEPROM, tenta se conectar à rede escolhida e reinicia o servidor web.

Disponibiliza API REST para status, redes disponíveis e leitura de sensores.

Responde sempre com a página principal em qualquer rota chamada na porta 80 (catch-all).

## 🛠️ Hardware Necessário

Placa ESP8266 (NodeMCU/Lolin/ESP-12E).

Sensor DHT11 conectado ao GPIO4 (D2):

- VCC → 3.3V
- DATA → D2 (com resistor pull-up de 10k para 3.3V)
- GND → GND

## 📦 Bibliotecas Utilizadas

- ESP8266WiFi
- ESP8266WebServer
- DNSServer
- EEPROM
- DHT Sensor Library
- ESP8266mDNS

## 🌐 Funcionalidades
Portal Cativo

Cria rede srvTemp (AP).

Redireciona qualquer endereço para a página de configuração.

Responsivo com Bootstrap 5 e visual clean, similar a roteadores D-Link.

Configuração Wi-Fi

Busca redes disponíveis (/scan).

Permite salvar SSID e senha na EEPROM.

Após salvar, tenta se conectar à rede automaticamente.

Leitura de Sensores

Lê temperatura e umidade via DHT11 a cada 3 segundos.

Exibe valores em tempo real na página principal.

Disponibiliza API REST para integração:
´´´
Endpoint /dht
{
  "temp": 26.3,
  "hum": 52.0,
  "ts": 123456
}
´´´

mDNS

Disponível em http://srvtemp.local (quando conectado na mesma rede).

Publica serviço http._tcp para descoberta automática.

Catch-All na Porta 80

Qualquer endereço acessado retorna a página principal, ideal para experiência de portal cativo.

## 🔗 Endpoints HTTP
Endpoint	Método	Descrição
- /	GET	Página principal (menu HTML).
- /status	GET	Retorna JSON com modo de operação (AP/STA), IP, SSID e RSSI.
- /scan	GET	Varre redes Wi-Fi próximas e retorna JSON com SSID, canal, RSSI e tipo.
- /save	POST	Recebe SSID e senha via x-www-form-urlencoded, salva na EEPROM e tenta conectar.
- /clear	POST	Limpa credenciais salvas e reinicia AP.
- /dht	GET	Retorna leitura de temperatura e umidade.

## 🖼️ Interface Web

Responsiva (mobile-first).

Estilo D-Link simplificado.

Exibe status de rede e dados de sensores.

Botões para buscar redes, salvar credenciais e limpar EEPROM.

## 🧪 Fluxo de Uso

Primeiro uso: conecte no Wi-Fi srvTemp.

Abra o navegador e acesse http://192.168.4.1/.

Clique em Buscar redes, selecione o SSID, informe a senha e clique em Salvar e Conectar.

O dispositivo tenta conectar e, se bem-sucedido, passa a responder em http://srvtemp.local.

Acesse /dht ou visualize a leitura na própria página inicial.

## 🐞 Depuração

No setup(), é exibido no Serial:

Razão do último reset via ESP.getResetReason().

Logs de conexão Wi-Fi.

Logs de POST /save e valores recebidos.



## 📜 Licença

Open Source - Livre uso e montagem, sem cobrança para este fim.
Proibido a venda por terceiros sem autorização prévia. 
Proibido a customização ou modificação.
Proibido a incorporação.