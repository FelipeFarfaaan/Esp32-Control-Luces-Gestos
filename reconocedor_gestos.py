"""
=======================================================================
 CONTROL DE ILUMINACION POR GESTOS DE LA MANO
 Universidad Militar Nueva Granada - Ingenieria Mecatronica
-----------------------------------------------------------------------
 Aplicacion de PC: captura la webcam, reconoce el gesto con MediaPipe
 Gesture Recognizer y envia un comando de un byte al ESP32 por el
 puerto serie.

 Uso:
   python reconocedor_gestos.py --puerto COM5
   python reconocedor_gestos.py --listar-puertos
   python reconocedor_gestos.py --sin-serial      (prueba sin el ESP32)

 Teclas: ESC o 'q' para salir.
=======================================================================
"""

import argparse
import sys
import time

import cv2
import mediapipe as mp
import serial
import serial.tools.list_ports

# ---------------------------------------------------------------------
# Configuracion
# ---------------------------------------------------------------------
MODELO = "gesture_recognizer.task"
BAUDIOS = 115200

# Gesto de MediaPipe -> (comando para el ESP32, texto en pantalla, color BGR)
MAPA_GESTOS = {
    "Closed_Fist": ("1", "Puno cerrado -> 30%",   (0, 255, 255)),
    "Victory":     ("2", "Victoria -> 70%",       (255, 128, 0)),
    "Open_Palm":   ("3", "Palma abierta -> 100%", (0, 0, 255)),
    "Thumb_Down":  ("A", "INTERRUPCION 1 (Modo 1)", (0, 140, 255)),
    "Thumb_Up":    ("B", "INTERRUPCION 2 (Modo 2)", (0, 200, 0)),
}

UMBRAL_CONFIANZA = 0.60   # descarta detecciones dudosas
FRAMES_ESTABLES = 4       # antirrebote: cuadros seguidos con el mismo gesto

# Conexiones entre landmarks de la mano, para dibujar el esqueleto
CONEXIONES = [
    (0, 1), (1, 2), (2, 3), (3, 4),
    (0, 5), (5, 6), (6, 7), (7, 8),
    (5, 9), (9, 10), (10, 11), (11, 12),
    (9, 13), (13, 14), (14, 15), (15, 16),
    (13, 17), (17, 18), (18, 19), (19, 20),
    (0, 17),
]


# ---------------------------------------------------------------------
def listar_puertos():
    puertos = list(serial.tools.list_ports.comports())
    if not puertos:
        print("No se encontro ningun puerto serie.")
        return
    print("Puertos disponibles:")
    for p in puertos:
        print(f"  {p.device}  ->  {p.description}")


def abrir_serial(puerto):
    """Abre el puerto y espera el reinicio automatico del ESP32."""
    try:
        conexion = serial.Serial(puerto, BAUDIOS, timeout=1)
    except serial.SerialException as e:
        print(f"[ERROR] No se pudo abrir {puerto}: {e}")
        print("Verifica el puerto con --listar-puertos y cierra el "
              "Monitor Serie del IDE de Arduino.")
        sys.exit(1)
    time.sleep(2.0)          # el ESP32 se reinicia al abrir el puerto
    conexion.reset_input_buffer()
    print(f"[OK] Conectado a {puerto} a {BAUDIOS} baudios.")
    return conexion


def dibujar_mano(frame, landmarks):
    """Dibuja los 21 puntos y sus conexiones sobre el cuadro."""
    alto, ancho = frame.shape[:2]
    puntos = [(int(lm.x * ancho), int(lm.y * alto)) for lm in landmarks]

    for a, b in CONEXIONES:
        cv2.line(frame, puntos[a], puntos[b], (0, 255, 0), 2)
    for (x, y) in puntos:
        cv2.circle(frame, (x, y), 4, (0, 0, 255), -1)


def crear_reconocedor():
    BaseOptions = mp.tasks.BaseOptions
    GestureRecognizer = mp.tasks.vision.GestureRecognizer
    GestureRecognizerOptions = mp.tasks.vision.GestureRecognizerOptions
    VisionRunningMode = mp.tasks.vision.RunningMode

    opciones = GestureRecognizerOptions(
        base_options=BaseOptions(model_asset_path=MODELO),
        running_mode=VisionRunningMode.VIDEO,
        num_hands=1,
        min_hand_detection_confidence=0.5,
        min_tracking_confidence=0.5,
    )
    return GestureRecognizer.create_from_options(opciones)


# ---------------------------------------------------------------------
def main():
    parser = argparse.ArgumentParser(
        description="Control de iluminacion por gestos (MediaPipe + ESP32)")
    parser.add_argument("--puerto", help="Puerto serie del ESP32, p.ej. COM5 o /dev/ttyUSB0")
    parser.add_argument("--camara", type=int, default=0, help="Indice de la webcam")
    parser.add_argument("--sin-serial", action="store_true",
                        help="Ejecuta solo la vision, sin enviar al ESP32")
    parser.add_argument("--listar-puertos", action="store_true")
    args = parser.parse_args()

    if args.listar_puertos:
        listar_puertos()
        return

    conexion = None
    if not args.sin_serial:
        if not args.puerto:
            print("[ERROR] Falta --puerto. Usa --listar-puertos para verlos "
                  "o --sin-serial para probar sin el ESP32.")
            sys.exit(1)
        conexion = abrir_serial(args.puerto)

    try:
        reconocedor = crear_reconocedor()
    except Exception as e:
        print(f"[ERROR] No se pudo cargar '{MODELO}': {e}")
        print("Descarga el modelo y dejalo junto a este script (ver README).")
        sys.exit(1)

    camara = cv2.VideoCapture(args.camara)
    if not camara.isOpened():
        print(f"[ERROR] No se pudo abrir la camara {args.camara}.")
        sys.exit(1)

    ultimo_enviado = None      # ultimo comando confirmado
    gesto_candidato = None     # gesto que se esta estabilizando
    repeticiones = 0
    t0 = time.time()

    print("[INFO] Sistema en marcha. ESC o 'q' para salir.")

    while True:
        ok, frame = camara.read()
        if not ok:
            print("[WARN] No llego cuadro de la camara.")
            break

        frame = cv2.flip(frame, 1)          # efecto espejo
        rgb = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)
        imagen_mp = mp.Image(image_format=mp.ImageFormat.SRGB, data=rgb)

        marca_ms = int((time.time() - t0) * 1000)
        resultado = reconocedor.recognize_for_video(imagen_mp, marca_ms)

        gesto = None
        confianza = 0.0
        if resultado.gestures:
            categoria = resultado.gestures[0][0]
            if categoria.score >= UMBRAL_CONFIANZA:
                gesto = categoria.category_name
                confianza = categoria.score

        if resultado.hand_landmarks:
            dibujar_mano(frame, resultado.hand_landmarks[0])

        # ---------- Antirrebote ----------
        if gesto == gesto_candidato:
            repeticiones += 1
        else:
            gesto_candidato = gesto
            repeticiones = 1

        if repeticiones == FRAMES_ESTABLES:
            comando = MAPA_GESTOS.get(gesto, ("0",))[0] if gesto else "0"
            if comando != ultimo_enviado:
                ultimo_enviado = comando
                if conexion:
                    conexion.write(comando.encode())
                print(f"-> Enviado '{comando}'  ({gesto or 'ninguno'})")

        # ---------- Interfaz en pantalla ----------
        if gesto in MAPA_GESTOS:
            _, texto, color = MAPA_GESTOS[gesto]
        else:
            texto, color = "Sin gesto reconocido", (180, 180, 180)

        cv2.rectangle(frame, (0, 0), (frame.shape[1], 70), (30, 30, 30), -1)
        cv2.putText(frame, texto, (15, 32),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.8, color, 2)
        cv2.putText(frame, f"Confianza: {confianza:.2f}   Comando: {ultimo_enviado or '-'}",
                    (15, 58), cv2.FONT_HERSHEY_SIMPLEX, 0.5, (220, 220, 220), 1)

        cv2.imshow("Control de iluminacion por gestos - UMNG", frame)

        tecla = cv2.waitKey(1) & 0xFF
        if tecla == 27 or tecla == ord('q'):
            break

    # ---------- Cierre ordenado ----------
    if conexion:
        conexion.write(b'0')      # apagar los LEDs al salir
        time.sleep(0.1)
        conexion.close()
    camara.release()
    cv2.destroyAllWindows()
    print("[INFO] Programa finalizado.")


if __name__ == "__main__":
    main()
