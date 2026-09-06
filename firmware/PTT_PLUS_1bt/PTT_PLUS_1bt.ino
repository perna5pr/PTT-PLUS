#include <Arduino.h>
#include <HijelHID_BLEMouse.h>

// Instância do Mouse BLE com nome e fabricante personalizados
HijelBLEMouse mouse("PTT_PLUS_1bt", "MOSAICO", 100);

#define PINO_PTT 9
#define PINO_LED 8

const bool LED_ACTIVE_LOW = true;  // no ESP32-C3 SuperMini o LED onboard do GPIO8 acende em LOW.
                                    // Se ficar invertido (aceso quando devia estar apagado), mude para false.

bool ledOn = false;
unsigned long lastLedToggle = 0;

const unsigned long LED_BLINK_WAITING_MS = 300;           // velocidade do pisca enquanto aguarda conexao
const unsigned long LED_WARN_SLOW_THRESHOLD_MS = 30000;   // a partir daqui (30s restantes) comeca a piscar devagar
const unsigned long LED_WARN_FAST_THRESHOLD_MS = 10000;   // a partir daqui (10s restantes) pisca rapido
const unsigned long LED_BLINK_SLOW_MS = 500;               // intervalo do pisca devagar
const unsigned long LED_BLINK_FAST_MS = 150;                // intervalo do pisca rapido

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

void setup() {
  Serial.begin(115200);
  Serial.println("Iniciando ESP32 PTT_PLUS 1bt");

  pinMode(PINO_PTT, INPUT_PULLUP);
  pinMode(PINO_LED, OUTPUT);
  setLed(false);

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
  // mouse.moveTo(X, Y) X=direita, -X=esquerda, Y=baixo, -Y=cima
  mouse.moveTo(-70, 120);
  delay(50);
  mouse.moveTo(0, 120);
  delay(50);
  mouse.press(MouseButton::Left);  // press()/release() nao bloqueiam
  delay(50);
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
