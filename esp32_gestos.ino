/*
 * =====================================================================
 *  CONTROL DE ILUMINACION POR GESTOS DE LA MANO
 *  Universidad Militar Nueva Granada - Ingenieria Mecatronica
 * ---------------------------------------------------------------------
 *  Firmware ESP32 (Arduino C++)
 *
 *  El PC ejecuta MediaPipe Gesture Recognizer y envia un caracter por
 *  el puerto serie cada vez que cambia el gesto detectado:
 *
 *    '1' -> Puno cerrado   -> LED amarillo al  30 %
 *    '2' -> Victoria       -> LED azul     al  70 %
 *    '3' -> Palma abierta  -> LED rojo     al 100 %
 *    'A' -> Pulgar abajo   -> INTERRUPCION 1 : secuencia Modo 1
 *    'B' -> Pulgar arriba  -> INTERRUPCION 2 : secuencia Modo 2
 *    '0' -> Sin gesto      -> Todo apagado
 *
 *  Las secuencias NO usan delay(): un timer por hardware genera una
 *  interrupcion cada 50 ms que avanza el paso de la secuencia.
 * =====================================================================
 */

// ---------------------- Configuracion de pines ----------------------
#define LED_AMARILLO 25
#define LED_AZUL     26
#define LED_ROJO     27

// Canales LEDC (solo se usan en Arduino-ESP32 core 2.x)
#define CH_AMARILLO 0
#define CH_AZUL     1
#define CH_ROJO     2

#define PWM_FREQ 5000   // 5 kHz: no hay parpadeo visible
#define PWM_RES  8      // 8 bits -> duty de 0 a 255

// Ciclos utiles pedidos por el enunciado
#define DUTY_30  77     // 0.30 * 255
#define DUTY_70  178    // 0.70 * 255
#define DUTY_100 255    // 1.00 * 255

#define TICK_US 50000   // periodo de la interrupcion = 50 ms

// ---------------------- Estados del sistema -------------------------
enum Estado {
  REPOSO,
  INTENSIDAD_30,
  INTENSIDAD_70,
  INTENSIDAD_100,
  MODO_1,
  MODO_2
};

volatile Estado estadoActual = REPOSO;

// ---------------------- Variables de la ISR -------------------------
hw_timer_t *timerSecuencia = NULL;
portMUX_TYPE muxTimer = portMUX_INITIALIZER_UNLOCKED;

volatile bool     hayTick = false;   // bandera levantada por la ISR
volatile uint32_t contadorTicks = 0; // paso actual de la secuencia

/*
 * Rutina de servicio de interrupcion.
 * Vive en IRAM y solo hace dos cosas: incrementar el contador y
 * levantar una bandera. Nunca se llama a ledcWrite() ni a Serial
 * desde aqui, porque esas funciones no son seguras dentro de una ISR.
 */
void IRAM_ATTR isrTimer() {
  portENTER_CRITICAL_ISR(&muxTimer);
  contadorTicks++;
  hayTick = true;
  portEXIT_CRITICAL_ISR(&muxTimer);
}

// ---------------------- Capa de abstraccion PWM ---------------------
// El core 3.x de Arduino-ESP32 cambio la API de LEDC. Estas funciones
// hacen que el mismo sketch compile en 2.x y en 3.x.

void pwmIniciar(uint8_t pin, uint8_t canal) {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcAttach(pin, PWM_FREQ, PWM_RES);
#else
  ledcSetup(canal, PWM_FREQ, PWM_RES);
  ledcAttachPin(pin, canal);
#endif
}

void pwmEscribir(uint8_t pin, uint8_t canal, uint8_t duty) {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcWrite(pin, duty);
#else
  ledcWrite(canal, duty);
#endif
}

void apagarTodo() {
  pwmEscribir(LED_AMARILLO, CH_AMARILLO, 0);
  pwmEscribir(LED_AZUL,     CH_AZUL,     0);
  pwmEscribir(LED_ROJO,     CH_ROJO,     0);
}

// ---------------------- SETUP ---------------------------------------
void setup() {
  Serial.begin(115200);

  pwmIniciar(LED_AMARILLO, CH_AMARILLO);
  pwmIniciar(LED_AZUL,     CH_AZUL);
  pwmIniciar(LED_ROJO,     CH_ROJO);
  apagarTodo();

  // --- Configuracion del timer por hardware ---
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  timerSecuencia = timerBegin(1000000);                 // base 1 MHz
  timerAttachInterrupt(timerSecuencia, &isrTimer);
  timerAlarm(timerSecuencia, TICK_US, true, 0);         // autorecarga
#else
  timerSecuencia = timerBegin(0, 80, true);             // 80 MHz / 80 = 1 MHz
  timerAttachInterrupt(timerSecuencia, &isrTimer, true);
  timerAlarmWrite(timerSecuencia, TICK_US, true);
  timerAlarmEnable(timerSecuencia);
#endif

  Serial.println(F("ESP32 listo. Esperando comandos de gestos..."));
}

// ---------------------- Cambio de estado ----------------------------
void aplicarComando(char c) {
  Estado nuevo = estadoActual;

  switch (c) {
    case '1': nuevo = INTENSIDAD_30;  break;
    case '2': nuevo = INTENSIDAD_70;  break;
    case '3': nuevo = INTENSIDAD_100; break;
    case 'A': nuevo = MODO_1;         break;
    case 'B': nuevo = MODO_2;         break;
    case '0': nuevo = REPOSO;         break;
    default:  return;                 // caracter no reconocido
  }

  if (nuevo == estadoActual) return;

  estadoActual = nuevo;

  // Reiniciar el contador de la secuencia de forma atomica
  portENTER_CRITICAL(&muxTimer);
  contadorTicks = 0;
  portEXIT_CRITICAL(&muxTimer);

  apagarTodo();

  // Los estados estaticos se resuelven aqui mismo, una sola vez
  switch (estadoActual) {
    case INTENSIDAD_30:
      pwmEscribir(LED_AMARILLO, CH_AMARILLO, DUTY_30);
      Serial.println(F("Puno cerrado -> amarillo 30%"));
      break;
    case INTENSIDAD_70:
      pwmEscribir(LED_AZUL, CH_AZUL, DUTY_70);
      Serial.println(F("Victoria -> azul 70%"));
      break;
    case INTENSIDAD_100:
      pwmEscribir(LED_ROJO, CH_ROJO, DUTY_100);
      Serial.println(F("Palma abierta -> rojo 100%"));
      break;
    case MODO_1:
      Serial.println(F("INTERRUPCION 1 -> secuencia Modo 1 (barrido)"));
      break;
    case MODO_2:
      Serial.println(F("INTERRUPCION 2 -> secuencia Modo 2 (parpadeo)"));
      break;
    case REPOSO:
      Serial.println(F("Sin gesto -> apagado"));
      break;
  }
}

// ---------------------- Secuencias de luces -------------------------

/*
 * MODO 1: barrido tipo "auto fantastico".
 * Cada paso dura 3 ticks = 150 ms.
 * Orden: amarillo -> azul -> rojo -> azul -> (repite)
 */
void secuenciaModo1(uint32_t ticks) {
  uint8_t paso = (ticks / 3) % 4;

  apagarTodo();
  switch (paso) {
    case 0: pwmEscribir(LED_AMARILLO, CH_AMARILLO, DUTY_100); break;
    case 1: pwmEscribir(LED_AZUL,     CH_AZUL,     DUTY_100); break;
    case 2: pwmEscribir(LED_ROJO,     CH_ROJO,     DUTY_100); break;
    case 3: pwmEscribir(LED_AZUL,     CH_AZUL,     DUTY_100); break;
  }
}

/*
 * MODO 2: respiracion (fade in / fade out) de los tres LEDs a la vez.
 * Un ciclo completo dura 40 ticks = 2 s.
 */
void secuenciaModo2(uint32_t ticks) {
  uint8_t paso = ticks % 40;
  uint8_t duty = (paso < 20) ? (paso * 255 / 19)
                             : ((39 - paso) * 255 / 19);

  pwmEscribir(LED_AMARILLO, CH_AMARILLO, duty);
  pwmEscribir(LED_AZUL,     CH_AZUL,     duty);
  pwmEscribir(LED_ROJO,     CH_ROJO,     duty);
}

// ---------------------- LOOP ----------------------------------------
void loop() {
  // 1) Atender los comandos que llegan del PC
  while (Serial.available() > 0) {
    aplicarComando((char)Serial.read());
  }

  // 2) Atender el tick de la interrupcion
  if (hayTick) {
    uint32_t ticks;

    portENTER_CRITICAL(&muxTimer);
    hayTick = false;
    ticks = contadorTicks;
    portEXIT_CRITICAL(&muxTimer);

    if (estadoActual == MODO_1) {
      secuenciaModo1(ticks);
    } else if (estadoActual == MODO_2) {
      secuenciaModo2(ticks);
    }
  }
}
