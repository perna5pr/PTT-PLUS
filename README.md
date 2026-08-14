# 🤖 PTT-PLUS (2bt e 4bt) 
## Sistema de Automação com ESP32-C3 super mini para o APP CBTalk

Controle de comunicação sem fio de alta precisão baseado em Bluetooth Low Energy (BLE) projetado para automatizar comandos de clique e posicionamento de tela (emulação de mouse HID) no aplicativo CBTalk. O sistema possui temporização inteligente de segurança, interface visual em display compacto de 0.42" e calibração de coordenadas interativa gravada na memória Flash do circuito.

---

## Licenças

### Conteúdo
Este projeto utiliza a licença Creative Commons BY-NC-SA 4.0.
[![License: CC BY-NC-SA 4.0](https://img.shields.io/badge/License-CC%20BY--NC--SA%204.0-lightgrey.svg)](https://creativecommons.org/licenses/by-nc-sa/4.0/)

### Software
O código-fonte deste projeto está licenciado sob GPLv3.
[![Software License: GPLv3](https://img.shields.io/badge/Software%20License-GPLv3-red.svg)](https://www.gnu.org/licenses/gpl-3.0.html)

### Hardware
O projeto de hardware está licenciado sob CERN-OHL-S.
[![Hardware License: CERN-OHL-S](https://img.shields.io/badge/Hardware%20License-CERN--OHL--S-grey.svg)](https://ohwr.org/cern_ohl_s_v2.txt)

### Filosofia do projeto
Open Source Hardware can Learn from Software - OSS & OSHW
[![Open Source Software](https://img.shields.io/badge/Open%20Source%20Software-OSS-green.svg)](https://opensource.org/)
[![Open Source Hardware](https://img.shields.io/badge/Open%20Source%20Hardware-OSHWA-blue.svg)](https://www.oshwa.org/definition/)

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

Este projeto utiliza licenças diferentes para cada tipo de material. A licença aplicável depende do conteúdo que está sendo utilizado.

### 📄 Conteúdo e documentação

A documentação, textos, imagens, diagramas e demais materiais criativos deste projeto são disponibilizados sob a:

<a href="https://creativecommons.org/licenses/by-nc-sa/4.0/">
  <img src="https://licensebuttons.net/l/by-nc-sa/4.0/88x31.png" alt="Creative Commons BY-NC-SA 4.0" />
</a>

**Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International (CC BY-NC-SA 4.0)**

Você pode compartilhar e adaptar este material, desde que:

* dê os devidos créditos ao autor;
* não utilize o material para fins comerciais;
* distribua adaptações sob a mesma licença;
* mantenha os avisos de atribuição e licença aplicáveis.

[Leia o texto completo da CC BY-NC-SA 4.0](https://creativecommons.org/licenses/by-nc-sa/4.0/).

---

### 💻 Software

O código-fonte deste projeto é disponibilizado sob a:

<a href="https://www.gnu.org/licenses/gpl-3.0.html">
  <img src="https://www.gnu.org/graphics/gplv3-or-later.png" alt="GNU GPLv3" />
</a>

**GNU General Public License v3.0 (GPLv3)**

O software pode ser usado, estudado, modificado e redistribuído de acordo com os termos da GPLv3.

[Leia o texto completo da GNU GPLv3](https://www.gnu.org/licenses/gpl-3.0.html).

---

### 🔧 Hardware

O hardware, incluindo esquemas, arquivos de projeto, layouts, modelos e demais arquivos necessários para sua fabricação e modificação, é disponibilizado sob a:


<a href="https://ohwr.org/cern_ohl_s_v2.txt">
  <img src="https://cern-ohl.web.cern.ch/wp-content/themes/cern/public/images/logo-white.svg" alt="CERN Open Hardware Licence" width="100" />
</a>

**CERN Open Hardware Licence Version 2 – Strongly Reciprocal (CERN-OHL-S-2.0)**

O hardware pode ser estudado, fabricado, modificado e redistribuído de acordo com os termos da CERN-OHL-S-2.0, respeitando suas condições de reciprocidade.

[Leia o texto completo da CERN-OHL-S-2.0](https://ohwr.org/cern_ohl_s_v2.txt).

---

### 🌐 Filosofia: Open Source Hardware can Learn from Software

> **Open Source Hardware can Learn from Software**

Este projeto adota a filosofia de que o desenvolvimento de hardware aberto pode se beneficiar dos princípios, práticas e da cultura desenvolvidos no ecossistema de software livre e open source.

<a href="https://www.open-electronics.org/what-can-open-source-hardware-learn-from-software/">
  <img src="https://www.elektronart.com/wp-content/uploads/2021/04/OSS-OSHW-logo.jpg" alt="Open Source Hardware can Learn from Software" width="100" />
</a>

Assim como no software, o projeto busca incentivar:

* **transparência** — disponibilização dos arquivos e informações necessárias para compreender o projeto;
* **liberdade** — possibilidade de estudar, modificar e adaptar o projeto;
* **colaboração** — incentivo à participação e ao desenvolvimento por terceiros;
* **compartilhamento** — possibilidade de distribuir o projeto e suas modificações de acordo com a licença aplicável;
* **reprodutibilidade** — disponibilização dos arquivos necessários para que outras pessoas possam reproduzir o hardware.

**“Open Source Hardware can Learn from Software” não constitui uma licença jurídica adicional.** A licença legal aplicável ao hardware deste projeto é a **CERN-OHL-S-2.0**.

---

### ⚖️ Resumo

| Material                                                      | Licença / Princípio                          |
| ------------------------------------------------------------- | -------------------------------------------- |
| 📄 Documentação, textos, imagens e outros conteúdos criativos | CC BY-NC-SA 4.0                              |
| 💻 Código-fonte e software                                    | GNU GPLv3                                    |
| 🔧 Hardware e arquivos de projeto                             | CERN-OHL-S-2.0                               |
| 🌐 Filosofia do projeto                                       | Open Source Hardware can Learn from Software |

> **Importante:** cada licença se aplica ao respectivo tipo de material. O conteúdo criativo, o software e o hardware possuem termos de licenciamento distintos. Consulte a licença correspondente antes de redistribuir ou modificar qualquer parte deste projeto.

---

## 👥 Colaboradores e Créditos

Agradecimento especial aos desenvolvedores que projetaram e integraram este sistema (Usuários do CBTalk):

*   ⚡ `~ CASSA VAGABUNDO`
*   ⌨  `_PERNA-5-PR_`
*   🛠️ `!# _TECNICO_ / SC`

