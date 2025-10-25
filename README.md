# Projeto Temperatura – Documentação Aprimorada

## Visão Geral

O Projeto Temperatura é uma solução integrada para o controle e monitoramento contínuo de temperatura e umidade em ambientes críticos, como farmácias e geladeiras de medicamentos. O sistema opera 24/7, registrando dados em tempo real e gerando relatórios detalhados com valores atuais, máximos e mínimos, garantindo a segurança e preservação de produtos sensíveis.

## Finalidade

- Monitorar constantemente temperatura e umidade ambiental;
- Assegurar a qualidade e segurança de produtos sensíveis, especialmente medicamentos;
- Gerar relatórios analíticos para acompanhamento e controle eficaz.

## Vantagens

- Operação ininterrupta, 24 horas por dia, 7 dias por semana;
- Relatórios precisos com dados atuais, máximos e mínimos de temperatura e umidade;
- Sistema open source para flexibilidade e personalização;
- Compatível com três tipos de equipamentos de aquisição de dados.

## Equipamentos Suportados

1. **USB Serial (Arduino Nano)**: Comunicação via porta serial USB, ideal para instalações locais simples.  
2. **Ethernet (Arduino Mega com Ethernet Shield)**: Comunicação via rede cabeada, adequado para ambientes com infraestrutura Ethernet.  
3. **ESP8266 (Wi-Fi)**: Conexão sem fio via módulo Wi-Fi, oferecendo maior flexibilidade na instalação.

## Estrutura do Projeto

- **hardware/**: Firmwares para Arduino Nano, Mega com Ethernet Shield e ESP8266, manuais e modelos 3D (STL/SLDPRT) dos dispositivos e protetores.
- **software/**: Código-fonte da aplicação principal em Pascal/Delphi, banco de dados SQLite, instaladores para Windows e Linux, bibliotecas e ferramentas auxiliares (SQLiteStudio).
- **docs/**: Documentação técnica, manuais em PDF.
- **imgs/**: Imagens e capturas de tela ilustrativas de hardware e software.
- **.git/**: Configurações e arquivos para controle de versão com Git.

## Instalação e Configuração

1. Instale o equipamento de medição adequado ao ambiente.  
2. Configure o software informando o tipo do equipamento e parâmetros de comunicação.  
3. Execute a aplicação para iniciar o monitoramento contínuo.

## Interface do Sistema – Exemplos de Telas

### MSTemp01 – Visões dos Sensores  

| Visão Superior | Visão Traseira |
|---------------|----------------|
| ![Visão Superior](https://github.com/marcelomaurin/Temperatura/blob/main/imgs/superior.jpeg) | ![Visão Traseira](https://github.com/marcelomaurin/Temperatura/blob/main/imgs/traseira.jpeg) |

## Imagens do projeto

| ![box.jpg](https://github.com/marcelomaurin/Temperatura/blob/main/imgs/box.jpg) |
|---------------------|
| ![box_tampa.jpg](https://github.com/marcelomaurin/Temperatura/blob/main/imgs/box_tampa.jpg) |
| ![compile.jpg](https://github.com/marcelomaurin/Temperatura/blob/main/imgs/compile.jpg) |
| ![dht22.jpg](https://github.com/marcelomaurin/Temperatura/blob/main/imgs/dht22.jpg) |
| ![ide.jpg](https://github.com/marcelomaurin/Temperatura/blob/main/imgs/ide.jpg) |
| ![LCD.jpg](https://github.com/marcelomaurin/Temperatura/blob/main/imgs/LCD.jpg) |
| ![M3x12.jpg](https://github.com/marcelomaurin/Temperatura/blob/main/imgs/M3x12.jpg) |
| ![MSTemp01.jpeg](https://github.com/marcelomaurin/Temperatura/blob/main/imgs/MSTemp01.jpeg) |
| ![nano.jpg](https://github.com/marcelomaurin/Temperatura/blob/main/imgs/nano.jpg) |
| ![setup_protetor_100.exe](https://github.com/marcelomaurin/Temperatura/blob/main/imgs/setup_protetor_100.exe) |
| ![shield.jpg](https://github.com/marcelomaurin/Temperatura/blob/main/imgs/shield.jpg) |
| ![superior.jpeg](https://github.com/marcelomaurin/Temperatura/blob/main/imgs/superior.jpeg) |
| ![traseira.jpeg](https://github.com/marcelomaurin/Temperatura/blob/main/imgs/traseira.jpeg) |
| ![MSTEMP02.jpeg](https://github.com/marcelomaurin/Temperatura/blob/main/imgs/MSTemp02.jpeg) |
| ![MSTEMP02b.jpg](https://github.com/marcelomaurin/Temperatura/blob/main/imgs/MSTEMP02b.jpg) |
| ![vista frontal da peça.jpg](https://github.com/marcelomaurin/Temperatura/blob/main/imgs/vista%20frontal%20da%20pe%C3%A7a.jpg) |

## Telas do Instalador

| ![Instalador 2](https://github.com/marcelomaurin/Temperatura/blob/main/software/img/instalador_tela02.png) | ![Instalador 3](https://github.com/marcelomaurin/Temperatura/blob/main/software/img/instalador_tela03.png) | ![Instalador 4](https://github.com/marcelomaurin/Temperatura/blob/main/software/img/instalador_tela04.png) | ![Instalador 5](https://github.com/marcelomaurin/Temperatura/blob/main/software/img/instalador_tela05.png) |
|-----------------|-----------------|-----------------|-----------------|

## Projeto de Sensor de Temperatura em Arduino

O projeto suporta leitura e compartilhamento de dados de temperatura e umidade por meio de:  

- Equipamento com Web API para dados de temperatura;  
- Equipamento com Web API para integração com outros sistemas.

Mais informações:  
[https://maurinsoft.com.br/index.php/sensor-de-temperatura/](https://maurinsoft.com.br/index.php/sensor-de-temperatura/)

## Licença

Uso livre para pessoas físicas e jurídicas para uso próprio. Proibida montagem para venda. Firmware e software são open source, porém alterações no código original não autorizadas.

## Customizações

Solicitações de customização podem ser enviadas via contato:  
[https://maurinsoft.com.br/index.php/fale-conosco/](https://maurinsoft.com.br/index.php/fale-conosco/)
