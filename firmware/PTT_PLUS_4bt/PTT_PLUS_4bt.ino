#include <Arduino.h>
#include <HijelHID_BLEMouse.h>
#include <Preferences.h>

// ============================================================================
// ESTRUTURAS E PROTÓTIPOS (EVITA ERRO DE PRÉ-PROCESSAMENTO NO ARDUINO IDE)
// ============================================================================
struct ButtonCoords {
  int16_t x;
  int16_t y;
};

struct ButtonState {
  uint8_t pin;
  bool rawState;
  bool debouncedState;
  uint32_t lastDebounceTime;
};

// Protótipos explícitos para evitar que o Arduino IDE gere protótipos antes da struct
bool updateButton(ButtonState& btn);
void updateOled(const char* line1, const char* line2 = nullptr, const char* line3 = nullptr, const char* line4 = nullptr);
void loadCalibrationFromNVS();
void saveCalibrationToNVS();
void moveMouseSegmented(int16_t totalX, int16_t totalY);
void resetCursorPosition();
void runCalibrationMode();
void updatePttLedStatus();
void startPTT();
void stopPTT();
void handleButton2();
void handleButton3();
void handleButton4();

// ============================================================================
// CONFIGURAÇÕES E PINOS (ESP32-C3 SuperMini)
// ============================================================================
constexpr uint8_t BUTTON_PIN1 = 20;    // Botão 1: PTT (CBTalk) / Calibração: CIMA
constexpr uint8_t BUTTON_PIN2 = 10;    // Botão 2: Detalhes / Calibração: BAIXO
constexpr uint8_t BUTTON_PIN3 = 0;     // Botão 3: Lista / Calibração: DIREITA
constexpr uint8_t BUTTON_PIN4 = 1;     // Botão 4: Mute / Calibração: ESQUERDA
constexpr uint8_t BUTTON_BOOT = 9;     // Botão BOOT onboard do ESP32-C3 Super Mini (GPIO9)
constexpr uint8_t PIN_LED_STATUS = 8;  // LED onboard do ESP32-C3 Super Mini (GPIO 8)

// No ESP32-C3 Super Mini o LED onboard do GPIO8 é ATIVO EM NÍVEL BAIXO (LOW = aceso).
// As funções abaixo abstraem isso para o resto do código continuar "pensando" em ON/OFF normal.
constexpr bool LED_ACTIVE_LOW = true;

inline void ledOn() {
  digitalWrite(PIN_LED_STATUS, LED_ACTIVE_LOW ? LOW : HIGH);
}
inline void ledOff() {
  digitalWrite(PIN_LED_STATUS, LED_ACTIVE_LOW ? HIGH : LOW);
}
inline void ledSet(bool on) {
  on ? ledOn() : ledOff();
}
inline void ledToggle(bool& stateVar) {
  stateVar = !stateVar;
  ledSet(stateVar);
}

constexpr uint32_t DEBOUNCE_DELAY = 50;               // Tempo de debounce para os botões (ms)
constexpr uint32_t PTT_MAX_TIMEOUT = 90000;           // Tempo máximo do PTT ativo em ms (90s)
constexpr uint32_t BOOT_HOLD_TO_CALIBRATE_MS = 5000;  // Tempo segurando o BOOT para entrar na calibração

// Constantes para os avisos regressivos do LED
constexpr uint32_t PTT_WARN_30S_ELAPSED = 60000;   // Faltam 30s (60s decorridos)
constexpr uint32_t PTT_WARN_15S_ELAPSED = 75000;   // Faltam 15s (75s decorridos)
constexpr uint32_t LED_BLINK_SLOW_INTERVAL = 300;  // Intervalo do pisca lento (300ms)
constexpr uint32_t LED_BLINK_FAST_INTERVAL = 100;  // Intervalo do pisca rápido (100ms)

// Modo de funcionamento do PTT: true = TOGGLE, false = HOLD
constexpr bool PTT_TOGGLE_MODE = true;

// Instâncias de periféricos e armazenamento
HijelBLEMouse mouse("PTT_PLUS_4bt", "MOSAICO", 100);
Preferences preferences;

// Instâncias de Coordenadas e Botões
ButtonCoords btnCoords[4] = {
  { -50, 254 },   // Botão 1 (PTT)
  { -127, 120 },  // Botão 2
  { -300, 115 },  // Botão 3
  { -100, 330 }   // Botão 4
};

ButtonState btn1 = { BUTTON_PIN1, HIGH, HIGH, 0 };
ButtonState btn2 = { BUTTON_PIN2, HIGH, HIGH, 0 };
ButtonState btn3 = { BUTTON_PIN3, HIGH, HIGH, 0 };
ButtonState btn4 = { BUTTON_PIN4, HIGH, HIGH, 0 };

bool isBleConnected = false;
bool isPttActive = false;
uint32_t pttStartTime = 0;

uint32_t lastLedBlinkTime = 0;
bool ledState = false;
int lastPrintedSecond = -1;

bool bootHeld = false;  // controla a deteccao do "segurar BOOT por 5s"
uint32_t bootPressStart = 0;

// Substitui a atualização do display OLED (que não existe nesta placa) por uma
// impressão equivalente no Serial Monitor. Mantém a mesma assinatura de função
// para que todas as chamadas existentes no resto do código continuem funcionando
// sem precisar editar cada uma individualmente.
void updateOled(const char* line1, const char* line2, const char* line3, const char* line4) {
  Serial.print("[STATUS] ");
  if (line1) Serial.print(line1);
  if (line2) {
    Serial.print(" | ");
    Serial.print(line2);
  }
  if (line3) {
    Serial.print(" | ");
    Serial.print(line3);
  }
  if (line4) {
    Serial.print(" | ");
    Serial.print(line4);
  }
  Serial.println();
}

// ============================================================================
// PERSISTÊNCIA NA MEMÓRIA FLASH (NVS)
// ============================================================================
void loadCalibrationFromNVS() {
  preferences.begin("ptt_calib", true);  // Modo Leitura
  btnCoords[0].x = preferences.getShort("b1_x", -50);
  btnCoords[0].y = preferences.getShort("b1_y", 254);
  btnCoords[1].x = preferences.getShort("b2_x", -127);
  btnCoords[1].y = preferences.getShort("b2_y", 120);
  btnCoords[2].x = preferences.getShort("b3_x", -300);
  btnCoords[2].y = preferences.getShort("b3_y", 115);
  btnCoords[3].x = preferences.getShort("b4_x", -100);
  btnCoords[3].y = preferences.getShort("b4_y", 330);
  preferences.end();

  Serial.println("Coordenadas carregadas da memória Flash:");
  for (int i = 0; i < 4; i++) {
    Serial.printf("  Botão %d -> X: %d, Y: %d\n", i + 1, btnCoords[i].x, btnCoords[i].y);
  }
}

void saveCalibrationToNVS() {
  preferences.begin("ptt_calib", false);  // Modo Escrita
  preferences.putShort("b1_x", btnCoords[0].x);
  preferences.putShort("b1_y", btnCoords[0].y);
  preferences.putShort("b2_x", btnCoords[1].x);
  preferences.putShort("b2_y", btnCoords[1].y);
  preferences.putShort("b3_x", btnCoords[2].x);
  preferences.putShort("b3_y", btnCoords[2].y);
  preferences.putShort("b4_x", btnCoords[3].x);
  preferences.putShort("b4_y", btnCoords[3].y);
  preferences.end();

  Serial.println("Coordenadas gravadas com sucesso na memória Flash (NVS)!");
}

// ============================================================================
// FUNÇÕES DE MOVIMENTAÇÃO DO MOUSE (COM LIMITE DE 127 PASSOS)
// ============================================================================
void moveMouseSegmented(int16_t totalX, int16_t totalY) {
  int16_t remX = totalX;
  int16_t remY = totalY;

  while (remX != 0 || remY != 0) {
    int8_t stepX = (int8_t)constrain(remX, -127, 127);
    int8_t stepY = (int8_t)constrain(remY, -127, 127);

    mouse.moveTo(stepX, stepY);
    remX -= stepX;
    remY -= stepY;

    delay(50);  // Delay obrigatório para estabilização entre comandos
  }
}

void resetCursorPosition() {
  Serial.println("Alinhando cursor no canto superior direito...");
  mouse.moveTo(2000, -2000);
}

// ============================================================================
// ROTINA DE CALIBRAÇÃO INTERATIVA
// ============================================================================
void runCalibrationMode() {
  Serial.println("\n=======================================================");
  Serial.println(">>> MODO DE CALIBRAÇÃO ATIVADO (BOOT segurado por 5s)! <<<");
  Serial.println("=======================================================\n");

  // Indicador de entrada: LED aceso fixo por 3 segundos, com mensagem no console
  Serial.println("Entrando no modo de calibracao... aguarde.");
  updateOled("CALIBRACAO", "Iniciando...");
  ledOn();
  delay(3000);
  ledOff();

  // Aguarda soltar o BOOT (que foi usado para entrar aqui) antes de prosseguir,
  // para não confundir essa liberação com um "confirmar" do primeiro botão.
  while (digitalRead(BUTTON_BOOT) == LOW) {
    delay(10);
  }
  delay(200);

  Serial.println("----------------------------------------------------------------------------------");
  Serial.println("A calibração agora é feita 100% pelo Serial Monitor.");
  Serial.println("Para cada botão, digite a posição absoluta desejada no formato 'X,Y' e Enter");
  Serial.println("(ex: -50,254) — o cursor se move até lá a partir do canto. Repita quantas vezes");
  Serial.println("precisar até acertar o lugar certo, depois digite 'ok' e Enter para confirmar");
  Serial.println("(o botão BOOT físico também confirma, se preferir).");
  Serial.println("O LED faz N piscadas no primeiro segundo (N = numero do botao) e fica");
  Serial.println("apagado no segundo seguinte, repetindo esse ciclo de 2 segundos.");
  Serial.println("----------------------------------------------------------------------------------\n");

  updateOled("CALIBRACAO", "Use o Serial", "X,Y + Enter", "ok=Confirma");
  delay(1500);

  for (int i = 0; i < 4; i++) {
    Serial.printf(">>> CONFIGURANDO BOTÃO %d <<< (LED pisca %d vez(es) por segundo)\n", i + 1, i + 1);

    resetCursorPosition();
    delay(300);

    int16_t currentX = 0;
    int16_t currentY = 0;

    // Aguarda a liberação do botão BOOT antes de iniciar o ajuste deste botão
    while (digitalRead(BUTTON_BOOT) == LOW) {
      delay(10);
    }
    delay(200);

    // Velocidade do pisca-LED especifica deste botao: (i+1) piscadas dentro do
    // primeiro segundo de um ciclo de 2 segundos; o segundo seguinte fica apagado.
    const uint32_t blinksThisButton = (uint32_t)(i + 1);
    const uint32_t halfPeriod = 500UL / blinksThisButton;  // duracao de cada aceso/apagado dentro do 1o segundo
    bool lastLedComputed = false;

    Serial.printf(">>> Ajustando Botao %d <<<\n", i + 1);
    Serial.println("Digite a posicao absoluta 'X,Y' (a partir do canto) e Enter, ex: -50,254");
    Serial.println("O cursor se move ate essa posicao. Repita quantas vezes quiser para ajustar.");
    Serial.println("Quando estiver no lugar certo, digite 'ok' e Enter para confirmar.");

    bool stepConfirmed = false;
    while (!stepConfirmed) {
      bool confirmNow = (digitalRead(BUTTON_BOOT) == LOW);  // BOOT fisico continua valendo como atalho

      // --- Entrada via Serial: posicao absoluta ---
      if (Serial.available()) {
        String cmd = Serial.readStringUntil('\n');
        cmd.trim();
        if (cmd.length() > 0) {
          String cmdLower = cmd;
          cmdLower.toLowerCase();
          if (cmdLower == "ok" || cmdLower == "enter") {
            confirmNow = true;
          } else {
            int commaIndex = cmd.indexOf(',');
            if (commaIndex > 0) {
              int16_t targetX = (int16_t)cmd.substring(0, commaIndex).toInt();
              int16_t targetY = (int16_t)cmd.substring(commaIndex + 1).toInt();
              int16_t deltaX = targetX - currentX;
              int16_t deltaY = targetY - currentY;

              moveMouseSegmented(deltaX, deltaY);
              currentX = targetX;
              currentY = targetY;

              Serial.printf("  Cursor movido para X=%d Y=%d (a partir do canto)\n", currentX, currentY);
            } else {
              Serial.println("  Comando nao reconhecido. Use 'X,Y' (ex: -50,254) ou 'ok'.");
            }
          }
        }
      }

      // Padrão do LED: dentro de cada ciclo de 2000ms, o primeiro segundo pisca
      // (i+1) vezes, o segundo seguinte fica totalmente apagado.
      uint32_t cyclePos = millis() % 2000;
      bool shouldBeOn;
      if (cyclePos < 1000) {
        shouldBeOn = ((cyclePos / halfPeriod) % 2) == 0;
      } else {
        shouldBeOn = false;
      }
      if (shouldBeOn != lastLedComputed) {
        lastLedComputed = shouldBeOn;
        ledSet(shouldBeOn);
      }

      // Confirmação (via BOOT físico ou 'ok'/'enter' pelo Serial)
      if (confirmNow) {
        btnCoords[i].x = currentX;
        btnCoords[i].y = currentY;

        Serial.printf("-> Botão %d MARCADOR SALVO! Offset: X = %d, Y = %d\n\n", i + 1, currentX, currentY);

        char line1Buf[16];
        char line2Buf[16];
        snprintf(line1Buf, sizeof(line1Buf), "BOTAO %d/4", i + 1);
        snprintf(line2Buf, sizeof(line2Buf), "X:%d Y:%d", currentX, currentY);
        updateOled("SALVO!", line1Buf, line2Buf);

        // Feedback visual: Pisca LED 3 vezes rápido, confirmando o salvamento
        for (int flash = 0; flash < 3; flash++) {
          ledOn();
          delay(80);
          ledOff();
          delay(80);
        }

        stepConfirmed = true;

        while (digitalRead(BUTTON_BOOT) == LOW) {
          delay(10);
        }
        delay(300);
      }

      delay(10);
    }
  }

  saveCalibrationToNVS();

  Serial.println("\n=======================================================");
  Serial.println("Calibração concluída! Todos os 4 botões foram salvos.");
  Serial.println("=======================================================\n");

  updateOled("CALIBRACAO", "CONCLUIDA!", "Salvo no NVS");
  delay(1500);

  ledOff();
  resetCursorPosition();
  updateOled("CBTalk", "CONECTADO", "PRONTO");
}

// ============================================================================
// FUNÇÕES AUXILIARES DE LEITURA E STATUS
// ============================================================================
bool updateButton(ButtonState& btn) {
  bool reading = digitalRead(btn.pin);

  if (reading != btn.rawState) {
    btn.lastDebounceTime = millis();
    btn.rawState = reading;
  }

  if ((millis() - btn.lastDebounceTime) > DEBOUNCE_DELAY) {
    if (reading != btn.debouncedState) {
      btn.debouncedState = reading;
      return true;
    }
  }
  return false;
}

void updatePttLedStatus() {
  if (!isPttActive) {
    ledOff();
    ledState = false;
    return;
  }

  uint32_t elapsedTime = millis() - pttStartTime;

  if (elapsedTime <= PTT_MAX_TIMEOUT) {
    int remainingSec = (PTT_MAX_TIMEOUT - elapsedTime + 999) / 1000;
    if (remainingSec != lastPrintedSecond) {
      lastPrintedSecond = remainingSec;
      Serial.print("PTT - Tempo restante: ");
      Serial.print(remainingSec);
      Serial.println("s");

      char timerBuf[16];
      snprintf(timerBuf, sizeof(timerBuf), "Tempo: %ds", remainingSec);
      updateOled("PTT ATIVO", "Transmitindo", timerBuf);
    }
  }

  if (elapsedTime >= PTT_WARN_15S_ELAPSED) {
    if (millis() - lastLedBlinkTime >= LED_BLINK_FAST_INTERVAL) {
      lastLedBlinkTime = millis();
      ledToggle(ledState);
    }
  } else if (elapsedTime >= PTT_WARN_30S_ELAPSED) {
    if (millis() - lastLedBlinkTime >= LED_BLINK_SLOW_INTERVAL) {
      lastLedBlinkTime = millis();
      ledToggle(ledState);
    }
  } else {
    ledOn();
    ledState = true;
  }
}

// ============================================================================
// AÇÕES DOS BOTÕES
// ============================================================================
void startPTT() {
  Serial.println("PTT: Transmissão Iniciada");
  updateOled("PTT ATIVO", "Iniciando...");

  moveMouseSegmented(btnCoords[0].x, btnCoords[0].y);
  mouse.press(MouseButton::Left);

  isPttActive = true;
  pttStartTime = millis();
  lastLedBlinkTime = millis();
  lastPrintedSecond = -1;
  ledState = true;
  ledOn();
}

void stopPTT() {
  Serial.println("PTT: Transmissão Encerrada");

  mouse.release(MouseButton::Left);
  ledOff();
  ledState = false;
  lastPrintedSecond = -1;

  delay(50);
  resetCursorPosition();

  isPttActive = false;
  updateOled("CBTalk", "PRONTO", "PTT Off");
}

void handleButton2() {
  Serial.println("Botão 2: Executando Ação");
  updateOled("BOTAO 2", "Detalhes");

  moveMouseSegmented(btnCoords[1].x, btnCoords[1].y);
  mouse.press(MouseButton::Left);
  delay(80);
  mouse.release(MouseButton::Left);
  delay(80);

  resetCursorPosition();
  updateOled("CBTalk", "PRONTO");
}

void handleButton3() {
  Serial.println("Botão 3: Executando Ação");
  updateOled("BOTAO 3", "Lista Users");

  moveMouseSegmented(btnCoords[2].x, btnCoords[2].y);
  mouse.press(MouseButton::Left);
  delay(80);
  mouse.release(MouseButton::Left);
  delay(80);

  resetCursorPosition();
  updateOled("CBTalk", "PRONTO");
}

void handleButton4() {
  Serial.println("Botão 4: Executando Ação");
  updateOled("BOTAO 4", "MUTE");

  moveMouseSegmented(btnCoords[3].x, btnCoords[3].y);
  mouse.press(MouseButton::Left);
  delay(80);
  mouse.release(MouseButton::Left);
  delay(80);

  resetCursorPosition();
  updateOled("CBTalk", "PRONTO");
}

// ============================================================================
// SETUP
// ============================================================================
void setup() {
  Serial.begin(115200);
  Serial.println("Iniciando ESP32-C3 SuperMini BLE Mouse PTT (4 botoes)");

  updateOled("ESP32-C3", "Iniciando...", "PTT BLE");

  pinMode(BUTTON_PIN1, INPUT_PULLUP);
  pinMode(BUTTON_PIN2, INPUT_PULLUP);
  pinMode(BUTTON_PIN3, INPUT_PULLUP);
  pinMode(BUTTON_PIN4, INPUT_PULLUP);
  pinMode(BUTTON_BOOT, INPUT_PULLUP);

  // Configura o pino 6 com resistor de Pull-down interno (Fica em LOW por padrão)
  pinMode(6, INPUT_PULLDOWN);  // essa linha só deve ser ativa para esp32c3 supermini

  pinMode(PIN_LED_STATUS, OUTPUT);
  ledOff();

  loadCalibrationFromNVS();

  mouse.begin();
  delay(500);

  updateOled("CBTalk", "Aguardando", "Bluetooth...");
}

// ============================================================================
// LOOP PRINCIPAL
// ============================================================================
void loop() {
  bool currentlyPaired = mouse.isPaired();

  if (currentlyPaired != isBleConnected) {
    isBleConnected = currentlyPaired;

    if (isBleConnected) {
      Serial.println("CBTalk - CONECTADO - PRONTO");
      updateOled("CBTalk", "CONECTADO", "PRONTO");
      delay(200);
      resetCursorPosition();
    } else {
      Serial.println("CBTalk - DESCONECTADO - Aguardando...");
      updateOled("CBTalk", "DESCONECTADO", "Aguardando...");
      if (isPttActive) {
        stopPTT();
      }
    }
  }

  if (isBleConnected) {
    bool changed1 = updateButton(btn1);
    bool changed2 = updateButton(btn2);
    bool changed3 = updateButton(btn3);
    bool changed4 = updateButton(btn4);

    // Entrada na calibração: segurar o botão BOOT por 5 segundos
    bool bootPressedNow = (digitalRead(BUTTON_BOOT) == LOW);
    if (bootPressedNow && !isPttActive) {
      if (!bootHeld) {
        bootHeld = true;
        bootPressStart = millis();
      } else if (millis() - bootPressStart >= BOOT_HOLD_TO_CALIBRATE_MS) {
        bootHeld = false;
        runCalibrationMode();
        return;
      }
    } else {
      bootHeld = false;  // soltou antes de completar os 5s, ou PTT esta ativo: cancela
    }

    if (isPttActive) {
      updatePttLedStatus();
    }

    // Botão 1 (PTT)
    if (PTT_TOGGLE_MODE) {
      if (changed1 && btn1.debouncedState == LOW) {
        if (!isPttActive) {
          startPTT();
        } else {
          stopPTT();
        }
      }
    } else {
      if (changed1) {
        if (btn1.debouncedState == LOW && !isPttActive) {
          startPTT();
        } else if (btn1.debouncedState == HIGH && isPttActive) {
          stopPTT();
        }
      }
    }

    // Timeout de segurança (90s)
    if (isPttActive && (millis() - pttStartTime >= PTT_MAX_TIMEOUT)) {
      Serial.println("PTT: Timeout atingido (90s)");
      stopPTT();
    }

    // Botão 2
    if (changed2 && btn2.debouncedState == LOW && !isPttActive) {
      handleButton2();
    }

    // Botão 3
    if (changed3 && btn3.debouncedState == LOW && !isPttActive) {
      handleButton3();
    }

    // Botão 4 (Mute)
    if (changed4 && btn4.debouncedState == LOW && !isPttActive) {
      handleButton4();
    }
  }

  delay(10);
}
