#include <Arduino.h>
#include <HijelHID_BLEMouse.h>
#include <Preferences.h>

// Instância do Mouse BLE com nome e fabricante personalizados
HijelBLEMouse mouse("PTT_PLUS_2bt", "MOSAICO", 100);

// Configuração dos Pinos dos Botões (GPIO 3 e GPIO 4 no ESP32-C3)
const int BUTTON_PIN1 = 3;
const int BUTTON_PIN2 = 4;
const int PINO_BOOT = 9;  // Botão BOOT onboard do ESP32-C3 SuperMini, usado para calibração

Preferences preferences;

// --- LED onboard simples (azul, GPIO8 no ESP32-C3 SuperMini) ---
const int LED_PIN = 8;
const bool LED_ACTIVE_LOW = true;  // na maioria das SuperMini o LED acende com GPIO em LOW.
                                    // Se o LED ficar invertido (aceso quando devia estar apagado), mude para false.

bool ledOn = false;                 // estado lógico atual do LED (para controlar o pisca-pisca)
unsigned long lastLedToggle = 0;

const unsigned long LED_WARN_SLOW_THRESHOLD_MS = 30000;  // a partir daqui (30s restantes) começa a piscar devagar
const unsigned long LED_WARN_FAST_THRESHOLD_MS = 10000;  // a partir daqui (10s restantes) pisca rápido
const unsigned long LED_BLINK_SLOW_MS = 500;              // intervalo do pisca devagar
const unsigned long LED_BLINK_FAST_MS = 150;               // intervalo do pisca rápido

// --- Posições calibráveis dos cliques na tela (salvas na memória flash) ---
int16_t ptt1X = -25;
int16_t ptt1Y = 130;
int16_t btn2X = -125;
int16_t btn2Y = 65;

// --- Controle de entrada no modo calibração (segurar BOOT por 5s) ---
bool bootHeld = false;
unsigned long bootPressStart = 0;
const unsigned long BOOT_HOLD_TO_CALIBRATE_MS = 5000;

// --- BOTÃO 1 (PTT) ---
bool isPressed1 = false;      // true enquanto o PTT está ativo (transmitindo)
bool isLocked1 = false;       // true quando travado via clique rápido
int lastButtonState1 = HIGH;  // última leitura bruta (para debounce)
bool stableState1 = HIGH;     // último estado já confirmado/debounced
unsigned long lastDebounceTime1 = 0;
unsigned long pressStartTime1 = 0;  // quando o botão foi pressionado (p/ distinguir clique de hold)
unsigned long timePTT = 0;          // quando o PTT ligou (p/ trava de segurança de 90s e contagem do LED)

// --- BOTÃO 2 ---
bool isPressed2 = false;
int lastButtonState2 = HIGH;
unsigned long lastDebounceTime2 = 0;

const unsigned long DEBOUNCE_DELAY = 50;
const unsigned long CLICK_THRESHOLD_MS = 300;  // abaixo disso = "clique" (trava); acima = "hold"
const unsigned long PTT_MAX_HOLD_MS = 90000;   // trava de segurança: solta sozinho após 90s

// Variável de controle para posicionamento inicial único pós-conexão
bool isPositioned = false;

// Armazena o estado anterior da conexão Bluetooth
bool lastPairedState = false;

void setup() {
  Serial.begin(115200);
  Serial.println("Iniciando PTT_PLUS 2bt");

  // Configura os botões com Pull-Up interno
  pinMode(BUTTON_PIN1, INPUT_PULLUP);
  pinMode(BUTTON_PIN2, INPUT_PULLUP);
  pinMode(PINO_BOOT, INPUT_PULLUP);

  // Inicializa o LED apagado
  pinMode(LED_PIN, OUTPUT);
  setLed(false);

  loadCalibrationFromNVS();

  // Inicializa o serviço BLE Mouse
  mouse.begin();

  delay(1000);
}

// Liga/desliga o LED fisicamente, respeitando a polaridade da placa
void setLed(bool on) {
  bool physicalHigh = LED_ACTIVE_LOW ? !on : on;
  digitalWrite(LED_PIN, physicalHigh ? HIGH : LOW);
  ledOn = on;
}

// Move o cursor em passos de no maximo 127 unidades (limite do protocolo HID),
// dividindo automaticamente deslocamentos maiores.
void moveMouseSegmented(int16_t totalX, int16_t totalY) {
  int16_t remX = totalX;
  int16_t remY = totalY;
  while (remX != 0 || remY != 0) {
    int8_t stepX = (int8_t)constrain(remX, -127, 127);
    int8_t stepY = (int8_t)constrain(remY, -127, 127);
    mouse.moveTo(stepX, stepY);
    remX -= stepX;
    remY -= stepY;
    delay(50);
  }
}

void loadCalibrationFromNVS() {
  preferences.begin("ptt_calib", true);
  ptt1X = preferences.getShort("b1_x", -25);
  ptt1Y = preferences.getShort("b1_y", 130);
  btn2X = preferences.getShort("b2_x", -125);
  btn2Y = preferences.getShort("b2_y", 65);
  preferences.end();
  Serial.printf("Posições carregadas: Botao1 X=%d Y=%d | Botao2 X=%d Y=%d\n", ptt1X, ptt1Y, btn2X, btn2Y);
}

void saveCalibrationToNVS() {
  preferences.begin("ptt_calib", false);
  preferences.putShort("b1_x", ptt1X);
  preferences.putShort("b1_y", ptt1Y);
  preferences.putShort("b2_x", btn2X);
  preferences.putShort("b2_y", btn2Y);
  preferences.end();
  Serial.println("Posições salvas na memória flash (NVS)!");
}

// Modo de calibração: segurar o BOOT por 5s entra aqui. Configuração 100% pelo
// Serial Monitor (digite 'X,Y' + Enter para mover o cursor até lá, e 'ok' ou o
// próprio BOOT para confirmar). Passa pelos 2 botões em sequência.
void runCalibrationMode() {
  Serial.println("\n=======================================================");
  Serial.println(">>> MODO DE CALIBRAÇÃO ATIVADO (BOOT segurado por 5s)! <<<");
  Serial.println("=======================================================\n");

  Serial.println("Entrando no modo de calibracao... aguarde.");
  setLed(true);
  delay(3000);
  setLed(false);

  while (digitalRead(PINO_BOOT) == LOW) {
    delay(10);
  }
  delay(200);

  Serial.println("----------------------------------------------------------------------------------");
  Serial.println("Para cada botão, digite 'X,Y' + Enter (ex: -25,130) para mover o cursor até lá,");
  Serial.println("a partir do canto. Repita quantas vezes quiser até acertar. Depois digite 'ok'");
  Serial.println("(ou aperte o BOOT) para confirmar e passar para o próximo.");
  Serial.println("O LED faz N piscadas no primeiro segundo (N = numero do botao) e apaga no seguinte.");
  Serial.println("----------------------------------------------------------------------------------\n");

  int16_t targetCoordsX[2] = { ptt1X, btn2X };
  int16_t targetCoordsY[2] = { ptt1Y, btn2Y };

  for (int i = 0; i < 2; i++) {
    Serial.printf(">>> CONFIGURANDO BOTÃO %d <<< (LED pisca %d vez(es) por segundo)\n", i + 1, i + 1);

    mouse.moveTo(2000, -2000);  // reseta o cursor no canto
    delay(300);

    while (digitalRead(PINO_BOOT) == LOW) {
      delay(10);
    }
    delay(200);

    const unsigned long blinksThisButton = (unsigned long)(i + 1);
    const unsigned long halfPeriod = 500UL / blinksThisButton;
    bool lastLedComputed = false;

    int16_t currentX = 0;
    int16_t currentY = 0;
    bool stepConfirmed = false;

    while (!stepConfirmed) {
      bool confirmNow = (digitalRead(PINO_BOOT) == LOW);

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
              Serial.println("  Comando nao reconhecido. Use 'X,Y' (ex: -25,130) ou 'ok'.");
            }
          }
        }
      }

      // LED: N piscadas no primeiro segundo do ciclo de 2s, apagado no segundo seguinte
      unsigned long cyclePos = millis() % 2000;
      bool shouldBeOn;
      if (cyclePos < 1000) {
        shouldBeOn = ((cyclePos / halfPeriod) % 2) == 0;
      } else {
        shouldBeOn = false;
      }
      if (shouldBeOn != lastLedComputed) {
        lastLedComputed = shouldBeOn;
        setLed(shouldBeOn);
      }

      if (confirmNow) {
        targetCoordsX[i] = currentX;
        targetCoordsY[i] = currentY;
        Serial.printf("-> Botão %d SALVO! X=%d Y=%d\n\n", i + 1, currentX, currentY);

        for (int flash = 0; flash < 3; flash++) {
          setLed(true);
          delay(80);
          setLed(false);
          delay(80);
        }

        stepConfirmed = true;
        while (digitalRead(PINO_BOOT) == LOW) {
          delay(10);
        }
        delay(300);
      }

      delay(10);
    }
  }

  ptt1X = targetCoordsX[0];
  ptt1Y = targetCoordsY[0];
  btn2X = targetCoordsX[1];
  btn2Y = targetCoordsY[1];

  saveCalibrationToNVS();

  Serial.println("\n=======================================================");
  Serial.println("Calibração concluída! Os 2 botões foram salvos.");
  Serial.println("=======================================================\n");

  setLed(false);
  mouse.moveTo(2000, -2000);
}

// Atualiza o LED a cada volta do loop, sem bloquear nada.
// Regras:
//  - Fora de transmissão -> apagado
//  - Segurando (hold, sem estar travado) -> aceso fixo
//  - Travado (clique) -> aceso fixo até faltarem 30s do timeout de 90s,
//    depois pisca devagar, e piscando rápido nos últimos 10s
void atualizarLed() {
  if (!isPressed1) {
    if (ledOn) setLed(false);
    return;
  }

  if (!isLocked1) {
    // Modo "segurando": aceso fixo enquanto o dedo estiver no botão
    if (!ledOn) setLed(true);
    return;
  }

  // Modo travado: calcula quanto tempo falta para o timeout de segurança
  unsigned long elapsed = millis() - timePTT;
  long remaining = (long)PTT_MAX_HOLD_MS - (long)elapsed;
  if (remaining < 0) remaining = 0;

  if (remaining > LED_WARN_SLOW_THRESHOLD_MS) {
    // Mais de 30s restantes -> aceso fixo
    if (!ledOn) setLed(true);
  } else {
    unsigned long blinkInterval =
        (remaining > LED_WARN_FAST_THRESHOLD_MS) ? LED_BLINK_SLOW_MS : LED_BLINK_FAST_MS;
    if (millis() - lastLedToggle >= blinkInterval) {
      lastLedToggle = millis();
      setLed(!ledOn);
    }
  }
}

// Liga o PTT: posiciona o cursor e pressiona o clique esquerdo
void ativarPTT() {
  isPressed1 = true;
  moveMouseSegmented(ptt1X, ptt1Y);
  mouse.press(MouseButton::Left);  // press()/release() não bloqueiam
  Serial.println("Botao 1: PTT ativado");
}

// Solta o PTT e devolve o cursor para o canto
void liberarPTT() {
  isPressed1 = false;
  mouse.release(MouseButton::Left);
  delay(50);
  mouse.moveTo(2000, -2000);
  delay(50);
  Serial.println("Botao 1: PTT liberado. Cursor de volta ao canto.");
}

void loop() {
  bool isConnected = mouse.isPaired();

  // Executado quando o status do Bluetooth muda (Conectou ou Desconectou)
  if (isConnected != lastPairedState) {
    lastPairedState = isConnected;
    if (isConnected) {
      Serial.println("CBTalk - CONECTADO - PRONTO");
      if (!isPositioned) {
        Serial.println("Celular conectado! Alinhando cursor no canto superior direito...");
        delay(400);  // Aguarda estabilização da conexão BLE do Android

        mouse.moveTo(2000, -2000);
        delay(50);

        isPositioned = true;
        Serial.println("Cursor posicionado sobre o PTT! Transmissão instantânea pronta.");
      }
    } else {
      Serial.println("CBTalk - DESCONECTADO - Aguardando");
      if (isPositioned) {
        Serial.println("Bluetooth desconectado. Aguardando reconexão...");
        isPositioned = false;
      }
      // Se caiu a conexão com o PTT ativo, zera o estado local
      isPressed1 = false;
      isLocked1 = false;
      isPressed2 = false;
    }
  }

  // Executa as ações apenas quando o celular estiver conectado
  if (isConnected) {
    // Entrada na calibração: segurar o botão BOOT por 5 segundos
    bool bootPressedNow = (digitalRead(PINO_BOOT) == LOW);
    if (bootPressedNow && !isPressed1) {
      if (!bootHeld) {
        bootHeld = true;
        bootPressStart = millis();
      } else if (millis() - bootPressStart >= BOOT_HOLD_TO_CALIBRATE_MS) {
        bootHeld = false;
        runCalibrationMode();
        return;
      }
    } else {
      bootHeld = false;
    }

    int reading1 = digitalRead(BUTTON_PIN1);
    int reading2 = digitalRead(BUTTON_PIN2);

    // --- TRATAMENTO DO BOTÃO 1 (PTT: clique-trava + segurar-para-transmitir) ---
    if (reading1 != lastButtonState1) {
      lastDebounceTime1 = millis();
    }

    // Só processa quando o sinal estabilizou (debounce) E realmente mudou de estado
    if ((millis() - lastDebounceTime1) > DEBOUNCE_DELAY && reading1 != stableState1) {
      stableState1 = reading1;

      if (stableState1 == LOW) {
        // --- Borda de descida: botão foi pressionado agora ---
        if (isLocked1) {
          // Segundo clique enquanto travado -> destrava e solta
          isLocked1 = false;
          liberarPTT();
        } else if (!isPressed1) {
          // Início de um novo toque: liga o PTT na hora.
          // Ainda não sabemos se vai virar "clique" (trava) ou "hold" (soltar ao soltar o dedo).
          pressStartTime1 = millis();
          timePTT = millis();
          ativarPTT();
        }
      } else {
        // --- Borda de subida: botão foi solto agora ---
        if (isPressed1 && !isLocked1) {
          unsigned long heldFor = millis() - pressStartTime1;
          if (heldFor < CLICK_THRESHOLD_MS) {
            // Foi um clique rápido -> trava ligado, não solta o PTT
            isLocked1 = true;
            lastLedToggle = millis();  // começa o ciclo de pisca do LED do zero
            Serial.println("Botao 1: Clique detectado - PTT TRAVADO");
          } else {
            // Foi um hold -> solta o PTT agora que o dedo saiu
            liberarPTT();
          }
        }
      }
    }
    lastButtonState1 = reading1;

    // Trava de segurança: solta o PTT automaticamente após PTT_MAX_HOLD_MS,
    // seja no modo travado (clique) ou segurando (hold)
    if (isPressed1 && (millis() - timePTT) >= PTT_MAX_HOLD_MS) {
      Serial.println("Botao 1: Timeout de seguranca atingido, liberando PTT");
      isLocked1 = false;
      liberarPTT();
    }

    // --- TRATAMENTO DO BOTÃO 2 ---
    if (reading2 != lastButtonState2) {
      lastDebounceTime2 = millis();
    }

    if ((millis() - lastDebounceTime2) > DEBOUNCE_DELAY) {
      if (reading2 == LOW && !isPressed2 && !isPressed1) {
        isPressed2 = true;
        Serial.println("Botão 2: Detalhes do Usuario!");
        moveMouseSegmented(btn2X, btn2Y);
        mouse.click(MouseButton::Left, 50);
        delay(50);
      } else if (reading2 == HIGH && isPressed2) {
        isPressed2 = false;
        Serial.println("Botão 2 Solto!");
        mouse.moveTo(2000, -2000);
        delay(50);
      }
    }
    lastButtonState2 = reading2;

  } else {
    // Reseta as flags se perder a conexão Bluetooth
    isPressed1 = false;
    isLocked1 = false;
    isPressed2 = false;
  }

  // Atualiza o LED em toda volta do loop (não bloqueia nada)
  atualizarLed();

  delay(10);
}
