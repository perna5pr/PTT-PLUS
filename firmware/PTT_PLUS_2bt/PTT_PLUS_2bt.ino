#include <Arduino.h>
#include <HijelHID_BLEMouse.h>

// Instância do Mouse BLE com nome e fabricante personalizados
HijelBLEMouse mouse("PTT PLUS 2bt", "MOSAICO");

// Configuração dos Pinos dos Botões (GPIO 3 e GPIO 4 no ESP32-C3)
const int BUTTON_PIN1 = 3;
const int BUTTON_PIN2 = 4;

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
  Serial.println("Iniciando ESP32 PTT+ 2bt");

  // Configura os botões com Pull-Up interno
  pinMode(BUTTON_PIN1, INPUT_PULLUP);
  pinMode(BUTTON_PIN2, INPUT_PULLUP);

  // Inicializa o LED apagado
  pinMode(LED_PIN, OUTPUT);
  setLed(false);

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
  // mouse.moveTo(X, Y) X=direita, -X=esquerda, Y=baixo, -Y=cima
  // mouse.moveTo(-50, 127); //ANDROID
  mouse.moveTo(-25, 65);  //IPHONE
  delay(50);
  // mouse.moveTo(0, 127); //ANDROID
  mouse.moveTo(0, 65);  //IPHONE
  delay(50);
  mouse.press(MouseButton::Left);  // press()/release() não bloqueiam
  delay(50);
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
        // mouse.moveTo(X, Y) X=direita, -X=esquerda, Y=baixo, -Y=cima
        // mouse.moveTo(-127, 120); //ANDROID
        mouse.moveTo(-125, 65);  //IPHONE
        delay(50);
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
