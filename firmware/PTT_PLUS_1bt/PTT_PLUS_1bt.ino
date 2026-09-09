#include <Arduino.h>
#include <HijelHID_BLEMouse.h>
#include <Preferences.h>

// Instância do Mouse BLE com nome e fabricante personalizados
HijelBLEMouse mouse("PTT_PLUS_1bt", "MOSAICO", 100);

#define PINO_PTT 9  // Mesmo pino do botao BOOT onboard do ESP32-C3 SuperMini
#define PINO_LED 8

Preferences preferences;

const bool LED_ACTIVE_LOW = true;  // no ESP32-C3 SuperMini o LED onboard do GPIO8 acende em LOW.
                                    // Se ficar invertido (aceso quando devia estar apagado), mude para false.

bool ledOn = false;
unsigned long lastLedToggle = 0;

const unsigned long LED_BLINK_WAITING_MS = 300;           // velocidade do pisca enquanto aguarda conexao
const unsigned long LED_WARN_SLOW_THRESHOLD_MS = 30000;   // a partir daqui (30s restantes) comeca a piscar devagar
const unsigned long LED_WARN_FAST_THRESHOLD_MS = 10000;   // a partir daqui (10s restantes) pisca rapido
const unsigned long LED_BLINK_SLOW_MS = 500;               // intervalo do pisca devagar
const unsigned long LED_BLINK_FAST_MS = 150;                // intervalo do pisca rapido

// --- Posição do clique do PTT na tela (calibrável, salva na memória flash) ---
int16_t pttX = -70;
int16_t pttY = 240;

// --- Controle de entrada no modo calibração ---
// Como este projeto so tem 1 botao (PINO_PTT = 9, o mesmo do BOOT onboard),
// a calibracao e acionada segurando o PROPRIO botao de PTT por 5s continuos.
const unsigned long BOOT_HOLD_TO_CALIBRATE_MS = 5000;
bool calibrationArmed = false;  // vira true assim que o hold ultrapassa 5s nesta pressao

// --- PTT ---
bool isPressed = false;       // true enquanto o PTT esta ativo (transmitindo)
bool isLocked = false;        // true quando travado via clique rapido
int lastButtonState = HIGH;   // ultima leitura bruta (para debounce)
bool stableState = HIGH;      // ultimo estado ja confirmado/debounced
unsigned long lastDebounceTime = 0;
unsigned long pressStartTime = 0;  // quando o botao foi pressionado (p/ distinguir clique de hold)
unsigned long timePTT = 0;         // quando o PTT ligou (p/ trava de seguranca de 90s e contagem do LED)

const unsigned long DEBOUNCE_DELAY = 50;
const unsigned long CLICK_THRESHOLD_MS = 300;  // abaixo disso = "clique" (trava); acima = "hold"
const unsigned long PTT_MAX_HOLD_MS = 90000;   // trava de seguranca: solta sozinho apos 90s

bool isConnected = false;
bool isPositioned = false;   // controle do posicionamento inicial do cursor pos-conexao
bool lastPairedState = false;

// Liga/desliga o LED fisicamente, respeitando a polaridade da placa
void setLed(bool on) {
  bool physicalHigh = LED_ACTIVE_LOW ? !on : on;
  digitalWrite(PINO_LED, physicalHigh ? HIGH : LOW);
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
  pttX = preferences.getShort("ptt_x", -70);
  pttY = preferences.getShort("ptt_y", 240);
  preferences.end();
  Serial.printf("Posicao do PTT carregada: X=%d Y=%d\n", pttX, pttY);
}

void saveCalibrationToNVS() {
  preferences.begin("ptt_calib", false);
  preferences.putShort("ptt_x", pttX);
  preferences.putShort("ptt_y", pttY);
  preferences.end();
  Serial.println("Posicao do PTT salva na memoria flash (NVS)!");
}

// Modo de calibração: acionado ao segurar o próprio botão de PTT por 5s contínuos.
// Configuração 100% pelo Serial Monitor (digite 'X,Y' + Enter para mover o cursor
// até lá, e 'ok' ou o próprio botão para confirmar).
void runCalibrationMode() {
  Serial.println("\n=======================================================");
  Serial.println(">>> MODO DE CALIBRAÇÃO ATIVADO (botao segurado por 5s)! <<<");
  Serial.println("=======================================================\n");

  Serial.println("Entrando no modo de calibracao... aguarde.");
  setLed(true);
  delay(3000);
  setLed(false);

  while (digitalRead(PINO_PTT) == LOW) {
    delay(10);
  }
  delay(200);

  Serial.println("----------------------------------------------------------------------------------");
  Serial.println("Digite a posição absoluta 'X,Y' e Enter (ex: -70,240) para mover o cursor até lá,");
  Serial.println("a partir do canto. Repita quantas vezes quiser até acertar. Depois digite 'ok'");
  Serial.println("(ou aperte o botão) para confirmar e salvar.");
  Serial.println("O LED pisca 1x no primeiro segundo de cada ciclo de 2s, e apaga no segundo seguinte.");
  Serial.println("----------------------------------------------------------------------------------\n");

  mouse.moveTo(2000, -2000);  // reseta o cursor no canto antes de calibrar
  delay(300);

  int16_t currentX = 0;
  int16_t currentY = 0;
  bool lastLedComputed = false;
  bool stepConfirmed = false;

  while (!stepConfirmed) {
    bool confirmNow = (digitalRead(PINO_PTT) == LOW);

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
            Serial.println("  Comando nao reconhecido. Use 'X,Y' (ex: -70,240) ou 'ok'.");
          }
        }
      }
    }

    // LED: 1 piscada no primeiro segundo de cada ciclo de 2s, apagado no segundo seguinte
    unsigned long cyclePos = millis() % 2000;
    bool shouldBeOn = (cyclePos < 500);
    if (shouldBeOn != lastLedComputed) {
      lastLedComputed = shouldBeOn;
      setLed(shouldBeOn);
    }

    if (confirmNow) {
      pttX = currentX;
      pttY = currentY;
      Serial.printf("-> Posição do PTT SALVA! X=%d Y=%d\n\n", pttX, pttY);

      for (int flash = 0; flash < 3; flash++) {
        setLed(true);
        delay(80);
        setLed(false);
        delay(80);
      }

      stepConfirmed = true;
      while (digitalRead(PINO_PTT) == LOW) {
        delay(10);
      }
      delay(300);
    }

    delay(10);
  }

  saveCalibrationToNVS();

  Serial.println("\n=======================================================");
  Serial.println("Calibração concluída!");
  Serial.println("=======================================================\n");

  setLed(false);
  mouse.moveTo(2000, -2000);
}

void setup() {
  Serial.begin(115200);
  Serial.println("Iniciando ESP32 PTT_PLUS 1bt");

  pinMode(PINO_PTT, INPUT_PULLUP);
  pinMode(PINO_LED, OUTPUT);
  setLed(false);

  loadCalibrationFromNVS();

  mouse.begin();

  delay(1000);
}

// Atualiza o LED a cada volta do loop, sem bloquear nada.
// Regras:
//  - Desconectado -> pisca continuamente
//  - Conectado e fora de transmissao -> apagado
//  - Transmitindo -> aceso fixo ate faltarem 30s do limite de 90s,
//    depois pisca devagar, e piscando rapido nos ultimos 10s
void atualizarLed() {
  if (!isConnected) {
    if (millis() - lastLedToggle >= LED_BLINK_WAITING_MS) {
      lastLedToggle = millis();
      setLed(!ledOn);
    }
    return;
  }

  if (!isPressed) {
    if (ledOn) setLed(false);
    return;
  }

  if (!isLocked) {
    // Modo "segurando": aceso fixo enquanto o dedo estiver no botao
    if (!ledOn) setLed(true);
    return;
  }

  // Modo travado: calcula quanto tempo falta para o timeout de seguranca
  unsigned long elapsed = millis() - timePTT;
  long remaining = (long)PTT_MAX_HOLD_MS - (long)elapsed;
  if (remaining < 0) remaining = 0;

  if (remaining > LED_WARN_SLOW_THRESHOLD_MS) {
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
  isPressed = true;
  moveMouseSegmented(pttX, pttY);
  mouse.press(MouseButton::Left);  // press()/release() nao bloqueiam
  Serial.println("PTT ativado");
}

// Solta o PTT e devolve o cursor para o canto
void liberarPTT() {
  isPressed = false;
  mouse.release(MouseButton::Left);
  delay(50);
  mouse.moveTo(2000, -2000);
  delay(50);
  Serial.println("PTT liberado. Cursor de volta ao canto.");
}

void loop() {
  isConnected = mouse.isPaired();

  // Executado quando o status do Bluetooth muda (Conectou ou Desconectou)
  if (isConnected != lastPairedState) {
    lastPairedState = isConnected;
    if (isConnected) {
      Serial.println("PTT_PLUS - CONECTADO - PRONTO");
      if (!isPositioned) {
        Serial.println("Celular conectado! Alinhando cursor no canto superior direito...");
        delay(400);  // Aguarda estabilizacao da conexao BLE do Android

        mouse.moveTo(2000, -2000);
        delay(50);

        isPositioned = true;
        Serial.println("Cursor posicionado sobre o PTT! Transmissao instantanea pronta.");
      }
      setLed(false);  // ao conectar, o LED apaga (some do modo "piscando esperando")
    } else {
      Serial.println("PTT_PLUS - DESCONECTADO - Aguardando");
      isPositioned = false;
      // Se caiu a conexao com o PTT ativo, zera o estado local
      isPressed = false;
      isLocked = false;
    }
  }

  // Executa as acoes apenas quando o celular estiver conectado
  if (isConnected) {
    int reading = digitalRead(PINO_PTT);

    if (reading != lastButtonState) {
      lastDebounceTime = millis();
    }

    // So processa quando o sinal estabilizou (debounce) E realmente mudou de estado
    if ((millis() - lastDebounceTime) > DEBOUNCE_DELAY && reading != stableState) {
      stableState = reading;

      if (stableState == LOW) {
        // --- Borda de descida: botao foi pressionado agora ---
        calibrationArmed = false;  // nova pressao: ainda nao passou dos 5s
        if (isLocked) {
          // Segundo clique enquanto travado -> destrava e solta
          isLocked = false;
          liberarPTT();
        } else if (!isPressed) {
          // Inicio de um novo toque: liga o PTT na hora.
          // Ainda nao sabemos se vai virar "clique" (trava) ou "hold" (soltar ao soltar o dedo).
          pressStartTime = millis();
          timePTT = millis();
          ativarPTT();
        }
      } else {
        // --- Borda de subida: botao foi solto agora ---
        if (isPressed && !isLocked) {
          unsigned long heldFor = millis() - pressStartTime;
          if (heldFor < CLICK_THRESHOLD_MS) {
            // Foi um clique rapido -> trava ligado, nao solta o PTT
            isLocked = true;
            lastLedToggle = millis();  // comeca o ciclo de pisca do LED do zero
            Serial.println("Clique detectado - PTT TRAVADO");
          } else {
            // Foi um hold -> solta o PTT agora que o dedo saiu
            liberarPTT();
          }
        }
      }
    }
    lastButtonState = reading;

    // Deteccao do hold de 5s para calibracao: so verifica enquanto o botao
    // ainda esta fisicamente pressionado, num toque que comecou como "hold"
    // normal de PTT (nao travado). Ao ultrapassar 5s, cancela o PTT em
    // andamento e entra na calibracao.
    if (isPressed && !isLocked && !calibrationArmed &&
        (millis() - pressStartTime) >= BOOT_HOLD_TO_CALIBRATE_MS) {
      calibrationArmed = true;
      Serial.println("Hold de 5s detectado no botao de PTT - entrando em calibracao");
      liberarPTT();  // desfaz o clique que estava em andamento
      runCalibrationMode();
      // Reseta o debounce para nao reprocessar o release pendente deste toque
      stableState = HIGH;
      lastButtonState = HIGH;
      return;
    }

    // Trava de seguranca: solta o PTT automaticamente apos PTT_MAX_HOLD_MS,
    // seja no modo travado (clique) ou segurando (hold)
    if (isPressed && (millis() - timePTT) >= PTT_MAX_HOLD_MS) {
      Serial.println("Timeout de seguranca atingido, liberando PTT");
      isLocked = false;
      liberarPTT();
    }
  } else {
    // Reseta as flags se perder a conexao Bluetooth
    isPressed = false;
    isLocked = false;
  }

  // Atualiza o LED em toda volta do loop (nao bloqueia nada)
  atualizarLed();

  delay(10);
}
