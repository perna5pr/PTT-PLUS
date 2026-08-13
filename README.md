# 🤖 Sistema de Automação PTT Plus (4bt + OLED) para ESP32-C3

[![License: CC BY-NC-SA 4.0](https://shields.io)](http://creativecommons.org)
[![Hardware License](https://shields.io)](https://ohwr.org)
[![Software License](https://shields.io)](https://gnu.org)


Controle de comunicação sem fio de alta precisão baseado em Bluetooth Low Energy (BLE) projetado para automatizar comandos de clique e posicionamento de tela (emulação de mouse HID) no aplicativo CBTalk. O sistema possui temporização inteligente de segurança, interface visual em display compacto de 0.42" e calibração de coordenadas interativa gravada na memória Flash do circuito.

---

## 🧠 Funcionamento do Algoritmo e Lógica de Controle

O fluxo de execução contínuo do firmware baseia-se em varredura ativa (*polling*) com tempo de ciclo controlado por um `delay(10)` para estabilização de leitura:

1. **Segmentação de Passos HID (Mouse)**: O protocolo Bluetooth clássico limita comandos nativos do mouse a um deslocamento máximo de -127 a +127 pixels por pacote de transmissão. A função `moveMouseSegmented()` fraciona distâncias longas automaticamente e introduz uma pausa de `50ms` para estabilização da interface de toque no smartphone.
2. **Ciclo de Segurança PTT (Anti-Bloqueio)**: Ao ativar a transmissão por voz, o controlador monitora o canal continuamente. Se o temporizador ultrapassar o limite de segurança de `90 segundos`, uma rotina de interrupção força a liberação do clique esquerdo (`mouse.release`) para não ocupar os canais públicos do CBTalk em transmissões acidentais.
3. **Modulação do LED de Feedback**: O pino `GPIO8` opera em modo intermitente comandado por relógio de software (`millis()`). A frequência visual do LED onboard do ESP32-C3 altera-se dinamicamente para sinalizar ao operador o tempo de portadora restante.
4. **Alinhamento Absoluto**: Como dispositivos móveis utilizam coordenadas relativas para emulação de mouses, a rotina `resetCursorPosition()` força intencionalmente o cursor para a borda extrema superior direita `(2000, -2000)` antes de realizar qualquer movimento mapeado. Isso garante que o cálculo em pixel parta sempre do mesmo ponto zero de referência da tela.
5. **Chaveamento Dinâmico do Gatilho**: A variável `PTT_TOGGLE_MODE` define o comportamento do botão principal:
   * **Modo Alternado (`true`)**: Um clique inicial dispara a função `startPTT()`. O estado permanece ativo até que um novo clique execute a rotina `stopPTT()`.
   * **Modo Pressionado (`false`)**: A transmissão ocorre estritamente na transição de descida do sinal (`LOW`), e o encerramento é chamado imediatamente na borda de subida (`HIGH`), emulando um microfone de rádio transceptor tradicional.
6. **Entrada Segura na Calibração**: O modo de ajuste de coordenadas exige que o `BUTTON_PIN3` e o `BUTTON_PIN4` entrem em nível lógico baixo (`LOW`) de forma simultânea. O algoritmo valida se o PTT está inativo (`!isPttActive`) antes de permitir o desvio para a calibração, impedindo que ruídos ou cliques acidentais quebrem a operação de voz.

---

## 🔌 Engenharia de Hardware e Esquema Elétrico (EasyEDA)

O design da placa de circuito impresso (PCB) foi desenvolvido no EasyEDA focado em tamanho reduzido para integração em caixas compactas, utilizando o barramento nativo do ESP32-C3 Super Mini.

### 🎛️ Diagrama de Conexões e Netlist

A distribuição das conexões elétricas das chaves táteis e do barramento de comunicação obedece à seguinte arquitetura de malha tratada no arquivo de fabricação:

*   **Barramento GND Comum:** Todas as 4 chaves táteis (`BT01` a `BT04`) compartilham um nó elétrico de aterramento unificado conectado diretamente ao pino `GND` do ESP32-C3.
*   **Malhas de Sinal (Trilhas de Controle):**
    *   **BT01 (PTT / Cima):** Conectado diretamente ao pino de entrada `GPIO 1`.
    *   **BT02 (Detalhes / Baixo):** Conectado diretamente ao pino de entrada `GPIO 2`.
    *   **BT03 (Lista / Direita):** Conectado diretamente ao pino de entrada `GPIO 3`.
    *   **BT04 (Mute / Esquerda):** Conectado diretamente ao pino de entrada `GPIO 4`.
*   **Interface I2C do Display OLED (0.42"):**
    *   Linha de Dados (`SDA`) roteada de forma isolada para o pino `GPIO 5`.
    *   Linha de Clock (`SCL`) roteada de forma paralela para o pino `GPIO 6`.

### 🛠️ Lista de Materiais de Referência (BOM - Bill of Materials)

| Qtd | Identificador na PCB | Componente Técnico | Tipo / Encapsulamento | Função no Circuito |
| :--- | :--- | :--- | :--- | :--- |
| 1 | U1 | ESP32-C3 Super Mini | SMD Dev Board | Microcontrolador central e interface BLE |
| 4 | BT01 a BT04 | Chaves Táteis Push-Button | 2 pinos / 4.3mm (ou similar) | Entrada de comandos de automação |
| 1 | DISP1 | Display OLED 0.42" 72x40 | SSD1306 I2C (4 pinos) | Feedback visual de estados e contagem |
| 1 | LED1 | LED de Status Integrado | Onboard (GPIO 8) | Alertas regressivos visuais de transmissão |

> ⚠️ **Nota de Montagem:** Como os pinos de entrada `GPIO 1` a `GPIO 4` estão configurados no firmware com resistores internos ativados (`INPUT_PULLUP`), não é necessária a adição de resistores de pull-up externos de 10kΩ na placa, simplificando o layout de trilhas.

---

## 📜 Licença e Termos de Uso

Este projeto é de código e hardware abertos, protegido sob o conceito híbrido **CC BY-NC-SA 4.0** (Atribuição-NãoComercial-CompartilhaIgual):

*   👥 **Atribuição:** Você é livre para montar, testar e alterar este projeto, desde que dê os devidos créditos aos desenvolvedores originais.
*   🛑 **Não Comercial:** É proibida a comercialização de placas montadas baseadas nesses circuitos ou a venda do firmware embarcado sem licenciamento dedicado das partes.
*   🔄 **CompartilhaIgual:** Qualquer projeto derivado ou modificação gerada a partir deste repositório deve obrigatoriamente manter a mesma licença aberta e não-comercial.

## 👥 Colaboradores e Créditos

Agradecimento especial aos desenvolvedores que projetaram e integraram este sistema:

*   🛠️ `~ CASSA VAGABUNDO`
*   🎛️ `_PERNA-5-PR_`
*   ⚡ `!# _TECNICO_ / SC`
