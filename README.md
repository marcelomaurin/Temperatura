# Projeto Temperatura – Documentação


## Visão Geral

  

O Projeto Temperatura é uma solução completa para o controle e monitoramento contínuo da temperatura e umidade em ambientes críticos, como farmácias e geladeiras de medicamentos.
    O sistema registra medições 24/7, fornecendo relatórios detalhados com valores atuais, máximos e mínimos, garantindo a segurança e integridade de produtos sensíveis.


    

## Finalidade
      

- Monitoramento constante de temperatura e umidade ambiental.
      
- Preservação da qualidade e segurança de produtos sensíveis, especialmente medicamentos.

      - Geração de relatórios analíticos para acompanhamento e controle.


    

## Vantagens
      

- Funcionamento ininterrupto, 24 horas por dia, 7 dias por semana.
      
- Relatórios precisos contendo dados atuais, máximos e mínimos de temperatura e umidade.

      - Sistema open source para permitir flexibilidade e personalização.
- Compatível com três tipos distintos de equipamentos de aquisição de dados.

---

## Equipamentos Suportados

1. **USB Serial (Arduino Nano)**  
   Comunicação usando porta serial USB, ideal para instalações locais simples.

2. **Ethernet (Arduino Mega com Ethernet Shield)**  
   Comunicação via rede cabeada, adequado para infraestruturas com cabeamento Ethernet.

3. **ESP8266 (Wi-Fi)**  
   Módulo Wi-Fi para conexão sem fio, oferecendo maior flexibilidade na instalação.

---

## Estrutura do Projeto

- **hardware/**  
  Firmwares para placas Arduino Nano, Mega com Ethernet Shield e ESP8266, além de manuais e designs 3D dos equipamentos.

- **software/**  
  Código-fonte da aplicação principal (desenvolvida em Pascal/Delphi), banco de dados SQLite, instaladores para Windows e Linux, e bibliotecas auxiliares.

- **docs/**  
  Documentação técnica, incluindo manuais em PDF.

- **imgs/**  
  Imagens ilustrativas do hardware, software e capturas de tela.

- **.git/**  
  Arquivos e configurações para controle de versão com Git.

---

## Instalando e Configurando

1. Instale o equipamento de medição apropriado para seu ambiente.  
2. Configure o software informando o tipo de equipamento e parâmetros de comunicação.  
3. Execute a aplicação para iniciar o monitoramento contínuo.

---

## Telas do Sistema

### MSTemp01

| Visão Superior | Visão Traseira |
|---|---|
| <img src="https://github.com/marcelomaurin/Temperatura/blob/main/imgs/superior.jpeg" alt="Visão Superior" width="300" height="300"> | <img src="https://github.com/marcelomaurin/Temperatura/blob/main/imgs/traseira.jpeg" alt="Visão Traseira" width="300" height="300"> |

---

## Projeto de Sensor de Temperatura em Arduino

Este projeto permite a leitura de temperatura e umidade utilizando dois tipos de equipamentos:

- Um equipamento que cria uma Web API para compartilhamento dos dados de temperatura.
- Outro que disponibiliza uma Web API para integração com outros sistemas.

Para mais informações, acesse nosso site:  
[http://maurinsoft.com.br:8082/index.php/sensor-de-temperatura/](http://maurinsoft.com.br:8082/index.php/sensor-de-temperatura/)

---

## Licença

Este software e hardware podem ser utilizados livremente para uso próprio, por pessoas físicas ou jurídicas, porém a montagem para venda é proibida. O firmware e software são open source, porém não autorizamos modificações no código original.

---

## Customizações

Se desejar solicitar customizações, por favor entre em contato conosco:  
[http://maurinsoft.com.br:8082/index.php/fale-conosco/](http://maurinsoft.com.br:8082/index.php/fale-conosco/)
