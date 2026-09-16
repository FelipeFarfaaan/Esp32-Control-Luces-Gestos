# Control de iluminación por gestos de la mano

Sistema que reconoce gestos de la mano con **MediaPipe Gesture Recognizer** y controla la
intensidad de tres LEDs conectados a un **ESP32**, incluyendo dos rutinas de secuencia
disparadas por interrupción.

Universidad Militar Nueva Granada — Ingeniería Mecatrónica

---

## 1. Descripción del sistema

El sistema se divide en tres bloques:

| Bloque | Función |
|---|---|
| **PC + webcam** | Ejecuta MediaPipe Gesture Recognizer sobre el video en vivo y clasifica el gesto de la mano. |
| **Enlace serie (USB)** | El PC envía un byte al ESP32 únicamente cuando el gesto **cambia**, a 115200 baudios. |
| **ESP32** | Interpreta el comando, ajusta el PWM de los LEDs (periférico LEDC) y ejecuta las secuencias con un timer por hardware. |

El modelo `gesture_recognizer.task` de MediaPipe ya viene preentrenado con las siete
categorías por defecto, entre ellas las cinco que necesita este proyecto. No fue necesario
entrenar un modelo propio.

### Tabla de gestos y acciones

| Gesto (MediaPipe) | Gesto físico | Comando | Acción en el ESP32 |
|---|---|---|---|
| `Closed_Fist` | Puño cerrado | `1` | LED amarillo al **30 %** |
| `Victory` | Señal de victoria | `2` | LED azul al **70 %** |
| `Open_Palm` | Palma abierta | `3` | LED rojo al **100 %** |
| `Thumb_Down` | Pulgar abajo | `A` | **Interrupción 1** → secuencia Modo 1 |
| `Thumb_Up` | Pulgar arriba | `B` | **Interrupción 2** → secuencia Modo 2 |
| — | Sin mano en cuadro | `0` | Todos los LEDs apagados |

**Modo 1 (barrido):** los LEDs se encienden en orden amarillo → azul → rojo → azul y se repite,
con 150 ms por paso.

**Modo 2 (respiración):** los tres LEDs suben y bajan de intensidad de forma sincronizada en
ciclos de 2 s.

---

## 2. Materiales

- 1 × ESP32 DevKit v1
- 1 × LED amarillo, 1 × LED azul, 1 × LED rojo
- 3 × resistencias de 220 Ω
- Protoboard, jumpers y cable micro-USB
- PC con webcam

## 3. Diagrama de conexión

| Componente | Pin del ESP32 | Notas |
|---|---|---|
| LED amarillo (ánodo) | GPIO 25 | En serie con 220 Ω |
| LED azul (ánodo) | GPIO 26 | En serie con 220 Ω |
| LED rojo (ánodo) | GPIO 27 | En serie con 220 Ω |
| Cátodos de los 3 LEDs | GND | Nodo común |

```
ESP32 GPIO25 ──[220 Ω]──▶│── LED amarillo ──┐
ESP32 GPIO26 ──[220 Ω]──▶│── LED azul     ──┼── GND
ESP32 GPIO27 ──[220 Ω]──▶│── LED rojo     ──┘
```

Se eligieron los GPIO 25, 26 y 27 porque son de propósito general, soportan PWM por LEDC y no
interfieren con la secuencia de arranque ni con el ADC2 del módulo WiFi.

---

## 4. Instalación

### 4.1 Lado del ESP32

1. Instalar el IDE de Arduino y, en *Gestor de tarjetas*, el paquete **esp32** de Espressif.
2. Seleccionar la tarjeta **ESP32 Dev Module** y el puerto correspondiente.
3. Abrir `esp32_gestos/esp32_gestos.ino` y cargarlo.

El sketch es compatible con las versiones 2.x y 3.x del core de Arduino-ESP32 gracias a las
directivas `#if ESP_ARDUINO_VERSION_MAJOR >= 3`.

### 4.2 Lado del PC

Se recomienda Python 3.10, 3.11 o 3.12. MediaPipe todavía no publica ruedas para las versiones
más recientes, así que con Python 3.13 la instalación falla.

Los comandos se ejecutan **uno por uno** en el símbolo del sistema (`cmd`), desde la carpeta del
proyecto.

**Paso 1 — Verificar la versión de Python**

```bat
python --version
```

Debe responder `Python 3.10.x`, `3.11.x` o `3.12.x`. Si abre la Microsoft Store o dice que el
comando no se reconoce, Python no quedó en el PATH: reinstálalo marcando la casilla
*Add Python to PATH*.

**Paso 2 — Entrar a la carpeta `pc`**

```bat
cd pc
```

El prompt debe terminar en `...\control-luces-gestos\pc>`.

**Paso 3 — Crear el entorno virtual**

```bat
python -m venv venv
```

No imprime nada y tarda unos segundos. Al terminar existe la carpeta `venv`.

**Paso 4 — Activar el entorno virtual**

```bat
venv\Scripts\activate
```

El prompt queda precedido por `(venv)`. Ese prefijo debe estar visible en todos los pasos
siguientes; si cierras la ventana, hay que repetir este paso.

> En PowerShell el comando es `venv\Scripts\Activate.ps1`. En Linux o macOS,
> `source venv/bin/activate`.

**Paso 5 — Instalar las dependencias**

```bat
pip install -r requirements.txt
```

Descarga alrededor de 300 MB y puede tardar varios minutos. Termina con
`Successfully installed mediapipe-... opencv-python-... pyserial-... numpy-...`.

**Paso 6 — Descargar el modelo preentrenado**

```bat
curl -o gesture_recognizer.task https://storage.googleapis.com/mediapipe-models/gesture_recognizer/gesture_recognizer/float16/1/gesture_recognizer.task
```

Muestra una barra de progreso y deja el archivo `gesture_recognizer.task` (unos 8 MB) dentro de
`pc/`.

**Paso 7 — Comprobar la instalación**

```bat
python -c "import cv2, mediapipe, serial; print('Todo OK')"
```

Debe imprimir `Todo OK`. Si aparece `ModuleNotFoundError`, el entorno virtual no está activo o
el paso 5 no terminó bien.

---

## 5. Uso

Identificar el puerto del ESP32:

```bash
python reconocedor_gestos.py --listar-puertos
```

Ejecutar el sistema (reemplazar por el puerto real):

```bash
python reconocedor_gestos.py --puerto COM5
```

Probar solo la visión, sin la tarjeta conectada:

```bash
python reconocedor_gestos.py --sin-serial
```

> **Importante:** el Monitor Serie del IDE de Arduino debe estar cerrado, porque el puerto no se
> puede compartir entre dos programas.

Se sale con `ESC` o con la tecla `q`. Al cerrar, el programa envía `0` para apagar los LEDs.

---

## 6. Explicación del desarrollo

### 6.1 Reconocimiento de gestos

MediaPipe entrega 21 puntos característicos por mano (del `WRIST` al `PINKY_TIP`) y, sobre esa
representación, una capa de clasificación devuelve la categoría del gesto con su nivel de
confianza. El programa se ejecuta en modo `VIDEO`, que aprovecha el seguimiento entre cuadros y
resulta más estable que procesar imágenes sueltas.

### 6.2 Filtro antirrebote

Un clasificador que corre a 30 fps puede oscilar entre dos categorías durante la transición de
un gesto a otro. Para evitar que el ESP32 reciba una ráfaga de comandos contradictorios se
aplican dos filtros:

1. Se descartan las detecciones con confianza menor a 0,60.
2. Un gesto solo se acepta cuando aparece en **4 cuadros consecutivos**.

Además, el comando se transmite únicamente cuando difiere del último enviado, de modo que el
enlace serie permanece prácticamente en silencio mientras el gesto no cambia.

### 6.3 Control de intensidad por PWM

El ESP32 tiene el periférico **LEDC**, un controlador de PWM por hardware. Se configura a 5 kHz
con resolución de 8 bits, lo que da 256 niveles de ciclo útil:

| Intensidad | Ciclo útil (0–255) |
|---|---|
| 30 % | 77 |
| 70 % | 178 |
| 100 % | 255 |

Al ser por hardware, la señal se mantiene estable sin consumir tiempo de CPU y sin parpadeo
perceptible.

### 6.4 Manejo de las interrupciones

Las dos secuencias se resuelven con el **timer por hardware 0** del ESP32, configurado con una
base de 1 MHz y una alarma con autorrecarga cada 50 ms.

La rutina de servicio de interrupción (ISR) está declarada con `IRAM_ATTR` para que resida en
memoria RAM interna y se ejecute sin latencia de acceso a la flash. Siguiendo la buena práctica
de diseño, la ISR es deliberadamente mínima: solo incrementa un contador y levanta una bandera,
protegidos por un `portMUX` que garantiza el acceso atómico frente al segundo núcleo.

```cpp
void IRAM_ATTR isrTimer() {
  portENTER_CRITICAL_ISR(&muxTimer);
  contadorTicks++;
  hayTick = true;
  portEXIT_CRITICAL_ISR(&muxTimer);
}
```

La actualización de los LEDs se hace en el `loop()` al detectar la bandera. Esto es intencional:
`ledcWrite()` no reside en IRAM y llamarla desde una ISR puede provocar un *panic* del núcleo.
El patrón *ISR corta + procesamiento diferido* es el estándar en sistemas embebidos.

La consecuencia práctica es que en el firmware **no existe un solo `delay()`**: las secuencias
avanzan por interrupción mientras el `loop()` sigue atendiendo el puerto serie, de manera que el
sistema responde a un cambio de gesto de forma inmediata incluso en mitad de una secuencia.

### 6.5 Máquina de estados

El firmware se organiza como una máquina de estados finitos con seis estados (`REPOSO`,
`INTENSIDAD_30`, `INTENSIDAD_70`, `INTENSIDAD_100`, `MODO_1`, `MODO_2`). Los estados de
intensidad fija se resuelven una sola vez al entrar en ellos; los estados de secuencia se
actualizan en cada tick del timer.

---

## 7. Estructura del repositorio

```
control-luces-gestos/
├── README.md
├── .gitignore
├── esp32_gestos/
│   └── esp32_gestos.ino        Firmware del ESP32 (Arduino C++)
└── pc/
    ├── reconocedor_gestos.py   Visión por computador y enlace serie
    └── requirements.txt        Dependencias de Python
```

## 8. Video de demostración

**Enlace:** [Ver video de demostración](video/demo.mp4)

## 9. Referencias

- MediaPipe Gesture Recognizer — documentación oficial de Google AI Edge
- Demo web de referencia: `https://google-ai-edge.github.io/mediapipe-samples-web/#/vision/gesture_recognizer`
- Espressif — ESP32 Technical Reference Manual, capítulos LEDC y Timer Group
