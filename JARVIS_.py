
import speech_recognition as sr
import requests
import edge_tts
import asyncio
import subprocess
import os
import time
import threading
import unicodedata
import re
import audioop


# ============================================================
# CONFIGURACIÓN
# ============================================================

ESP32_IP = "192.168.1.25"

MICROFONO = 1

MP3_FILE = "jarvis_respuesta.mp3"
WAV_FILE = "jarvis_respuesta.wav"

VOZ = "es-MX-JorgeNeural"
VELOCIDAD_VOZ = "+3%"
TONO_VOZ = "-4Hz"
VOLUMEN_VOZ = "+10%"

INTERVALO_VIGIA = 2.0

DURACION_TRANSICION = 4.0
PASOS_TRANSICION = 20

PAUSA_ANTIECO = 1.0


# ============================================================
# RANGOS
# ============================================================

RANGOS = {
    "FC": (60, 100),
    "SpO2": (95, 100),
    "TEMP": (36.0, 37.5),
    "SYS": (90, 140),
    "DIA": (60, 90),
}


# ============================================================
# ESCENARIOS
# ============================================================

ESCENARIOS = {

    "estable": {
        "FC": 72,
        "SpO2": 98,
        "TEMP": 36.6,
        "SYS": 120,
        "DIA": 80
    },

    "taquicardia": {
        "FC": 130,
        "SpO2": 97,
        "TEMP": 36.8,
        "SYS": 125,
        "DIA": 82
    },

    "bradicardia": {
        "FC": 48,
        "SpO2": 98,
        "TEMP": 36.5,
        "SYS": 105,
        "DIA": 68
    },

    "hipoxemia": {
        "FC": 108,
        "SpO2": 89,
        "TEMP": 36.8,
        "SYS": 118,
        "DIA": 76
    },

    "fiebre": {
        "FC": 105,
        "SpO2": 97,
        "TEMP": 39.5,
        "SYS": 122,
        "DIA": 80
    },

    "hipotension": {
        "FC": 105,
        "SpO2": 96,
        "TEMP": 36.7,
        "SYS": 82,
        "DIA": 52
    },

    "hipertension": {
        "FC": 88,
        "SpO2": 97,
        "TEMP": 36.7,
        "SYS": 165,
        "DIA": 102
    },

    "mixto": {
        "FC": 125,
        "SpO2": 91,
        "TEMP": 38.2,
        "SYS": 88,
        "DIA": 56
    }
}


# ============================================================
# ALIAS
# ============================================================

ALIAS_ESCENARIOS = {

    "estable": [
        "estable",
        "normal",
        "modo estable",
        "modo normal",
        "escenario estable",
        "escenario normal",
        "pon estable",
        "pon normal",
        "cambia a estable",
        "cambia a normal",
        "simula estable",
        "simula normal"
    ],

    "taquicardia": [
        "taquicardia",
        "modo taquicardia",
        "escenario taquicardia",
        "pon taquicardia",
        "cambia a taquicardia",
        "simula taquicardia"
    ],

    "bradicardia": [
        "bradicardia",
        "modo bradicardia",
        "escenario bradicardia",
        "pon bradicardia",
        "cambia a bradicardia",
        "simula bradicardia"
    ],

    "hipoxemia": [
        "hipoxemia",
        "oxigeno bajo",
        "modo hipoxemia",
        "modo oxigeno bajo",
        "escenario hipoxemia",
        "pon hipoxemia",
        "pon oxigeno bajo",
        "cambia a hipoxemia",
        "simula hipoxemia",
        "simula oxigeno bajo"
    ],

    "fiebre": [
        "fiebre",
        "modo fiebre",
        "temperatura alta",
        "modo temperatura alta",
        "escenario fiebre",
        "pon fiebre",
        "pon temperatura alta",
        "cambia a fiebre",
        "simula fiebre",
        "simula temperatura alta"
    ],

    "hipotension": [
        "hipotension",
        "presion baja",
        "modo hipotension",
        "modo presion baja",
        "escenario hipotension",
        "pon hipotension",
        "pon presion baja",
        "cambia a hipotension",
        "simula hipotension",
        "simula presion baja"
    ],

    "hipertension": [
        "hipertension",
        "presion alta",
        "modo hipertension",
        "modo presion alta",
        "escenario hipertension",
        "pon hipertension",
        "pon presion alta",
        "cambia a hipertension",
        "simula hipertension",
        "simula presion alta"
    ],

    "mixto": [
        "mixto",
        "modo mixto",
        "escenario mixto",
        "pon mixto",
        "cambia a mixto",
        "simula mixto"
    ]
}


# ============================================================
# ESTADO GLOBAL
# ============================================================

modo_vigia = False
modo_conversacion = False

escenario_actual = "estable"

valores_actuales = ESCENARIOS["estable"].copy()

estado_alerta = {}

transicion_activa = False
hilo_transicion = None


# ============================================================
# BLOQUEOS DE AUDIO
# ============================================================

lock_audio = threading.Lock()

jarvis_hablando = False

audio_bloqueado_hasta = 0

lock_habla = threading.Lock()

lock_valores = threading.Lock()
lock_transicion = threading.Lock()

reconocedor = sr.Recognizer()

ultimo_texto = ""
ultimo_texto_tiempo = 0


# ============================================================
# NORMALIZAR TEXTO
# ============================================================

def normalizar(texto):

    texto = texto.lower().strip()

    texto = unicodedata.normalize(
        "NFD",
        texto
    )

    texto = "".join(
        c for c in texto
        if unicodedata.category(c) != "Mn"
    )

    texto = re.sub(
        r"[¿?¡!.,;:]",
        " ",
        texto
    )

    texto = re.sub(
        r"\s+",
        " ",
        texto
    ).strip()

    return texto


# ============================================================
# ESP32 - LEER VALORES
# ============================================================

def obtener_valores():

    try:

        respuesta = requests.get(
            f"http://{ESP32_IP}/valores",
            timeout=2.5
        )

        if respuesta.status_code != 200:
            return None

        datos = {}

        for linea in respuesta.text.splitlines():

            if "=" not in linea:
                continue

            clave, valor = linea.split("=", 1)

            clave = clave.strip()
            valor = valor.strip()

            try:

                if clave == "FC":
                    datos["FC"] = int(float(valor))

                elif clave == "SpO2":
                    datos["SpO2"] = int(float(valor))

                elif clave == "TEMP":
                    datos["TEMP"] = float(valor)

                elif clave == "SYS":
                    datos["SYS"] = int(float(valor))

                elif clave == "DIA":
                    datos["DIA"] = int(float(valor))

            except ValueError:
                pass

        if len(datos) == 5:
            return datos

    except Exception:
        pass

    return None


# ============================================================
# ESP32 - ENVIAR VALORES
# ============================================================

def enviar_valores_esp32(valores):

    try:

        datos = {
            "FC": valores["FC"],
            "SpO2": valores["SpO2"],
            "TEMP": valores["TEMP"],
            "SYS": valores["SYS"],
            "DIA": valores["DIA"]
        }

        respuesta = requests.post(
            f"http://{ESP32_IP}/simular",
            data=datos,
            timeout=5
        )

        return respuesta.status_code == 200

    except requests.exceptions.ReadTimeout:

        print(
            "ESP32 tardó demasiado en responder a /simular."
        )

        return False

    except Exception as e:

        print(
            "Error enviando valores:",
            e
        )

        return False


# ============================================================
# TTS
# ============================================================

async def generar_audio(texto):

    comunicador = edge_tts.Communicate(
        texto,
        VOZ,
        rate=VELOCIDAD_VOZ,
        pitch=TONO_VOZ,
        volume=VOLUMEN_VOZ
    )

    await comunicador.save(MP3_FILE)


def convertir_wav():

    comando = [
        "ffmpeg",
        "-y",
        "-i",
        MP3_FILE,
        "-ar",
        "44100",
        "-ac",
        "2",
        "-sample_fmt",
        "s16",
        WAV_FILE
    ]

    resultado = subprocess.run(
        comando,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL
    )

    return resultado.returncode == 0


def enviar_audio():

    try:

        with open(WAV_FILE, "rb") as archivo:

            audio = archivo.read()

        respuesta = requests.post(
            f"http://{ESP32_IP}/play",
            data=audio,
            headers={
                "Content-Type": "audio/wav"
            },
            timeout=15
        )

        return respuesta.status_code == 200

    except Exception as e:

        print(
            "Error enviando audio:",
            e
        )

        return False


# ============================================================
# HABLAR
# ============================================================

def hablar(texto):

    global jarvis_hablando
    global audio_bloqueado_hasta

    texto = texto.strip()

    if not texto:
        return

    with lock_habla:

        jarvis_hablando = True

        try:

            print()
            print(f"JARVIS: {texto}")

            asyncio.run(
                generar_audio(texto)
            )

            if not convertir_wav():

                print(
                    "Error convirtiendo audio."
                )

                return

            enviar_audio()

            time.sleep(0.25)

        except Exception as e:

            print(
                "Error de voz:",
                e
            )

        finally:

            audio_bloqueado_hasta = (
                time.time() + PAUSA_ANTIECO
            )

            jarvis_hablando = False


# ============================================================
# COMPROBAR SI EL AUDIO ESTÁ BLOQUEADO
# ============================================================

def audio_disponible():

    if jarvis_hablando:
        return False

    if time.time() < audio_bloqueado_hasta:
        return False

    return True


# ============================================================
# ALERTAS
# ============================================================

def detectar_alertas(valores):

    alertas = {}

    if valores is None:
        return alertas

    fc = valores["FC"]
    spo2 = valores["SpO2"]
    temp = valores["TEMP"]
    sys = valores["SYS"]
    dia = valores["DIA"]

    if fc < 60:

        alertas["FC"] = "bradicardia"

    elif fc > 100:

        alertas["FC"] = "taquicardia"

    if spo2 < 95:

        alertas["SpO2"] = "oxigenación baja"

    if temp > 37.5:

        alertas["TEMP"] = "temperatura alta"

    elif temp < 36.0:

        alertas["TEMP"] = "temperatura baja"

    if sys > 140 or dia > 90:

        alertas["PRESION"] = "presión alta"

    elif sys < 90 or dia < 60:

        alertas["PRESION"] = "presión baja"

    return alertas


# ============================================================
# VIGÍA
# ============================================================

def revisar_vigia():

    global estado_alerta

    valores = obtener_valores()

    if valores is None:
        return

    alertas = detectar_alertas(valores)

    nuevas = []

    for tipo, descripcion in alertas.items():

        if tipo not in estado_alerta:

            nuevas.append(
                f"Alerta. Se detecta {descripcion}."
            )

    for tipo in estado_alerta:

        if tipo not in alertas:

            if not alertas:

                nuevas.append(
                    "Los signos vitales han vuelto a valores normales."
                )

    estado_alerta = alertas

    for mensaje in nuevas:

        hablar(mensaje)


def hilo_vigia_funcion():

    print(
        "Vigía en segundo plano iniciado."
    )

    while True:

        try:

            if modo_vigia:

                revisar_vigia()

            time.sleep(
                INTERVALO_VIGIA
            )

        except Exception as e:

            print(
                "Error en vigía:",
                e
            )

            time.sleep(1)


# ============================================================
# LEER SIGNOS
# ============================================================

def hablar_valores():

    valores = obtener_valores()

    if valores is None:

        hablar(
            "No puedo comunicarme con el monitor."
        )

        return

    fc = valores["FC"]
    spo2 = valores["SpO2"]
    temp = valores["TEMP"]
    sys = valores["SYS"]
    dia = valores["DIA"]

    hablar(
        f"Tienes {fc} pulsaciones por minuto, "
        f"saturación de {spo2} por ciento, "
        f"temperatura de {temp:.1f} grados, "
        f"y presión de {sys} sobre {dia}."
    )


def consultar_frecuencia():

    valores = obtener_valores()

    if valores is None:

        hablar(
            "No puedo leer la frecuencia."
        )

        return

    fc = valores["FC"]

    if fc > 100:

        hablar(
            f"Tienes {fc} pulsaciones por minuto. "
            "La frecuencia está alta."
        )

    elif fc < 60:

        hablar(
            f"Tienes {fc} pulsaciones por minuto. "
            "La frecuencia está baja."
        )

    else:

        hablar(
            f"Tienes {fc} pulsaciones por minuto. "
            "La frecuencia está dentro del rango."
        )


def consultar_oxigenacion():

    valores = obtener_valores()

    if valores is None:

        hablar(
            "No puedo leer la saturación."
        )

        return

    spo2 = valores["SpO2"]

    if spo2 < 95:

        hablar(
            f"La saturación es de {spo2} por ciento. "
            "Está por debajo del rango."
        )

    else:

        hablar(
            f"La saturación es de {spo2} por ciento. "
            "Está dentro del rango."
        )


def consultar_temperatura():

    valores = obtener_valores()

    if valores is None:

        hablar(
            "No puedo leer la temperatura."
        )

        return

    temp = valores["TEMP"]

    if temp > 37.5:

        hablar(
            f"La temperatura es de {temp:.1f} grados. "
            "Está elevada."
        )

    elif temp < 36.0:

        hablar(
            f"La temperatura es de {temp:.1f} grados. "
            "Está baja."
        )

    else:

        hablar(
            f"La temperatura es de {temp:.1f} grados. "
            "Está dentro del rango."
        )


def consultar_presion():

    valores = obtener_valores()

    if valores is None:

        hablar(
            "No puedo leer la presión."
        )

        return

    sys = valores["SYS"]
    dia = valores["DIA"]

    if sys > 140 or dia > 90:

        hablar(
            f"La presión es de {sys} sobre {dia}. "
            "Está elevada."
        )

    elif sys < 90 or dia < 60:

        hablar(
            f"La presión es de {sys} sobre {dia}. "
            "Está baja."
        )

    else:

        hablar(
            f"La presión es de {sys} sobre {dia}. "
            "Está dentro del rango."
        )


def consultar_estado():

    valores = obtener_valores()

    if valores is None:

        hablar(
            "No puedo consultar el estado."
        )

        return

    alertas = detectar_alertas(valores)

    if not alertas:

        hablar(
            "Tus signos vitales están dentro de los rangos establecidos."
        )

        return

    nombres = list(
        alertas.values()
    )

    if len(nombres) == 1:

        hablar(
            f"Hay una alteración: {nombres[0]}."
        )

    else:

        hablar(
            "Hay varias alteraciones: "
            + ", ".join(nombres)
            + "."
        )


# ============================================================
# ESCENARIOS
# ============================================================

def listar_escenarios():

    hablar(
        "Puedes usar estable, taquicardia, bradicardia, "
        "hipoxemia, fiebre, hipotensión, hipertensión o mixto."
    )


def ejecutar_transicion(nombre, objetivo):

    global valores_actuales
    global escenario_actual
    global transicion_activa

    with lock_transicion:

        transicion_activa = True

        inicio = valores_actuales.copy()

        for i in range(
            1,
            PASOS_TRANSICION + 1
        ):

            factor = i / PASOS_TRANSICION

            valores = {}

            for clave in inicio:

                valores[clave] = (
                    inicio[clave]
                    + (
                        objetivo[clave]
                        - inicio[clave]
                    ) * factor
                )

            valores["FC"] = int(
                round(valores["FC"])
            )

            valores["SpO2"] = int(
                round(valores["SpO2"])
            )

            valores["TEMP"] = round(
                valores["TEMP"],
                1
            )

            valores["SYS"] = int(
                round(valores["SYS"])
            )

            valores["DIA"] = int(
                round(valores["DIA"])
            )

            with lock_valores:

                valores_actuales = (
                    valores.copy()
                )

            enviar_valores_esp32(
                valores
            )

            time.sleep(
                DURACION_TRANSICION
                / PASOS_TRANSICION
            )

        with lock_valores:

            valores_actuales = (
                objetivo.copy()
            )

        escenario_actual = nombre

        enviar_valores_esp32(
            objetivo
        )

        transicion_activa = False


def aplicar_escenario(nombre):

    global hilo_transicion

    if nombre not in ESCENARIOS:
        return

    if transicion_activa:

        hablar(
            "Ya estoy cambiando de escenario."
        )

        return

    hablar(
        f"Cambiando al escenario {nombre}."
    )

    hilo_transicion = threading.Thread(
        target=ejecutar_transicion,
        args=(
            nombre,
            ESCENARIOS[nombre].copy()
        ),
        daemon=True
    )

    hilo_transicion.start()


# ============================================================
# AYUDA
# ============================================================

def ayuda():

    hablar(
        "Puedes preguntarme por tus signos, "
        "frecuencia, oxígeno, temperatura o presión. "
        "También puedes decir modo vigía, "
        "o cambiar a un escenario como fiebre o taquicardia."
    )


# ============================================================
# AUDIO VÁLIDO
# ============================================================

def es_audio_valido(audio):

    try:

        duracion = len(
            audio.frame_data
        ) / (
            audio.sample_rate
            * audio.sample_width
        )

        # Más permisivo: antes 0.45 s
        if duracion < 0.35:
            return False

        datos = audio.get_raw_data()

        if not datos:
            return False

        rms = audioop.rms(
            datos,
            audio.sample_width
        )

        # Más sensible que antes
        umbral = max(
            50,
            reconocedor.energy_threshold * 0.35
        )

        print(
            f"Audio RMS: {rms} | Umbral: {umbral:.0f}"
        )

        if rms < umbral:

            print(
                "Audio demasiado bajo."
            )

            return False

        return True

    except Exception:

        return True


# ============================================================
# ESCUCHAR
# ============================================================

def escuchar():

    global audio_bloqueado_hasta

    if not audio_disponible():

        return None

    with lock_audio:

        if not audio_disponible():

            return None

        try:

            with sr.Microphone(
                device_index=MICROFONO
            ) as fuente:

                print(
                    "Escuchando..."
                )

                audio = reconocedor.listen(
                    fuente,
                    timeout=2,
                    phrase_time_limit=7
                )

        except sr.WaitTimeoutError:

            return None

        except Exception as e:

            print(
                "Error de micrófono:",
                e
            )

            return None

    if not audio_disponible():

        print(
            "Audio descartado: Jarvis estaba hablando."
        )

        return None

    if not es_audio_valido(audio):

        print(
            "Audio descartado."
        )

        return None

    try:

        texto = reconocedor.recognize_google(
            audio,
            language="es-CO"
        )

        texto = texto.strip()

        if not texto:

            return None

        print(
            "Usuario:",
            texto
        )

        return texto

    except sr.UnknownValueError:

        print(
            "No se entendió el audio."
        )

        return None

    except sr.RequestError as e:

        print(
            "Error de reconocimiento:",
            e
        )

        return None

    except Exception as e:

        print(
            "Error reconociendo voz:",
            e
        )

        return None


# ============================================================
# CALIBRAR MICRÓFONO
# ============================================================

def calibrar_microfono():

    print(
        "Calibrando micrófono..."
    )

    try:

        with sr.Microphone(
            device_index=MICROFONO
        ) as fuente:

            print(
                "Quédate en silencio durante 1 segundo..."
            )

            reconocedor.adjust_for_ambient_noise(
                fuente,
                duration=1
            )

            # ==================================================
            # MAYOR SENSIBILIDAD
            # ==================================================

            reconocedor.energy_threshold = max(
                80,
                reconocedor.energy_threshold * 0.50
            )

            # Evita que el programa vuelva a subir
            # automáticamente el umbral mientras hablas.
            reconocedor.dynamic_energy_threshold = False

            reconocedor.pause_threshold = 0.8
            reconocedor.phrase_threshold = 0.2
            reconocedor.non_speaking_duration = 0.3

        print(
            "Umbral de energía:",
            reconocedor.energy_threshold
        )

    except Exception as e:

        print(
            "No se pudo calibrar el micrófono:",
            e
        )


# ============================================================
# PROCESAR COMANDO
# ============================================================

def procesar_comando(texto):

    global modo_vigia
    global modo_conversacion

    texto = normalizar(texto)

    if not texto:
        return

    print(
        "Comando:",
        texto
    )

    # ========================================================
    # SALIR
    # ========================================================

    if texto in [
        "adios",
        "hasta luego",
        "deja de escuchar",
        "ya no te necesito",
        "salir",
        "terminar",
        "cerrar"
    ]:

        modo_conversacion = False

        hablar(
            "Entendido. Hasta luego."
        )

        return

    # ========================================================
    # VIGÍA OFF
    # ========================================================

    if any(frase in texto for frase in [

        "deja de vigilar",
        "ya no vigiles",
        "desactiva vigia",
        "desactivar vigia",
        "apaga el modo vigia",
        "apagar el modo vigia",
        "quita el modo vigia",
        "modo vigia apagado"

    ]):

        modo_vigia = False

        hablar(
            "Modo vigía desactivado."
        )

        return

    # ========================================================
    # VIGÍA ON
    # ========================================================

    if any(frase in texto for frase in [

        "modo vigia",
        "activa vigia",
        "activar vigia",
        "activa el modo vigia",
        "activar el modo vigia",
        "pon vigia",
        "pon el modo vigia",
        "quiero vigia",
        "quiero que vigiles",
        "quiero que me vigiles",
        "empieza a vigilar",
        "vigila mis signos",
        "vigila mis signos vitales",
        "quiero que estes pendiente"

    ]):

        if modo_vigia:

            hablar(
                "El modo vigía ya está activo."
            )

        else:

            modo_vigia = True

            hablar(
                "Modo vigía activado."
            )

        return

    # ========================================================
    # ESCENARIOS
    # ========================================================

    palabras_activacion = [
        "modo",
        "pon",
        "cambia",
        "cambiar",
        "simula",
        "simular",
        "escenario"
    ]

    es_peticion_escenario = any(
        palabra in texto
        for palabra in palabras_activacion
    )

    if es_peticion_escenario:

        for nombre, alias in ALIAS_ESCENARIOS.items():

            for frase in alias:

                if (
                    texto == frase
                    or frase in texto
                ):

                    aplicar_escenario(
                        nombre
                    )

                    return

    # Nombres directos

    nombres_directos = {

        "estable": "estable",
        "normal": "estable",
        "taquicardia": "taquicardia",
        "bradicardia": "bradicardia",
        "hipoxemia": "hipoxemia",
        "fiebre": "fiebre",
        "hipotension": "hipotension",
        "hipertension": "hipertension",
        "mixto": "mixto"

    }

    if texto in nombres_directos:

        aplicar_escenario(
            nombres_directos[texto]
        )

        return

    # ========================================================
    # TODOS LOS SIGNOS
    # ========================================================

    if (
        "como estoy" in texto
        or "como estan mis signos" in texto
        or "como estan mis signos vitales" in texto
        or "dime mis signos" in texto
        or "dime mis signos vitales" in texto
        or "lee mis signos" in texto
        or "cuales son mis signos" in texto
        or "quiero saber mis signos" in texto
        or "como esta mi estado" in texto
    ):

        hablar_valores()

        return

    # ========================================================
    # ALTERACIONES
    # ========================================================

    if (
        "tengo algo alterado" in texto
        or "hay algo alterado" in texto
        or "tengo alguna alteracion" in texto
        or "hay alguna alteracion" in texto
        or "tengo algo fuera de lo normal" in texto
        or "hay algo fuera de lo normal" in texto
        or "tengo algo fuera de rango" in texto
        or "hay algo fuera de rango" in texto
        or "hay alguna alerta" in texto
        or "tengo alguna alerta" in texto
        or "todo esta bien" in texto
        or "estoy bien" in texto
        or "hay algo mal" in texto
        or "que esta fuera de rango" in texto
    ):

        consultar_estado()

        return

    # ========================================================
    # FRECUENCIA
    # ========================================================

    if (
        "frecuencia" in texto
        or "pulsaciones" in texto
        or "latidos" in texto
        or "corazon" in texto
        or "taquicardia" in texto
        or "bradicardia" in texto
    ):

        consultar_frecuencia()

        return

    # ========================================================
    # OXÍGENO
    # ========================================================

    if (
        "oxigeno" in texto
        or "saturacion" in texto
        or "spo2" in texto
        or "poco oxigeno" in texto
    ):

        consultar_oxigenacion()

        return

    # ========================================================
    # TEMPERATURA
    # ========================================================

    if (
        "temperatura" in texto
        or "tengo fiebre" in texto
        or "estoy con fiebre" in texto
        or "tengo calor" in texto
    ):

        consultar_temperatura()

        return

    # ========================================================
    # PRESIÓN
    # ========================================================

    if (
        "presion" in texto
        or "tension" in texto
        or "presion alta" in texto
        or "presion baja" in texto
    ):

        consultar_presion()

        return

    # ========================================================
    # ESCENARIOS DISPONIBLES
    # ========================================================

    if (
        "que escenarios hay" in texto
        or "que escenarios tienes" in texto
        or "muestrame los escenarios" in texto
        or "lista los escenarios" in texto
        or "que modos tienes" in texto
        or "que puedo simular" in texto
    ):

        listar_escenarios()

        return

    # ========================================================
    # AYUDA
    # ========================================================

    if (
        texto == "ayuda"
        or "que puedes hacer" in texto
        or "que puedo decir" in texto
        or "que comandos tienes" in texto
    ):

        ayuda()

        return

    # ========================================================
    # GRACIAS
    # ========================================================

    if texto in [
        "gracias",
        "muchas gracias",
        "te agradezco"
    ]:

        hablar(
            "Con gusto."
        )

        return

    # ========================================================
    # SALUDO
    # ========================================================

    if texto in [
        "hola",
        "buenas",
        "buenos dias",
        "buenas tardes",
        "buenas noches"
    ]:

        if modo_conversacion:

            hablar(
                "Hola, señor."
            )

        else:

            print(
                "Saludo ignorado."
            )

        return

    # ========================================================
    # FALLBACK
    # ========================================================

    hablar(
        "No entendí la solicitud."
    )


# ============================================================
# PROCESAR VOZ
# ============================================================

def procesar_voz(texto):

    global modo_conversacion
    global ultimo_texto
    global ultimo_texto_tiempo

    if not texto:
        return

    texto_normalizado = normalizar(
        texto
    )

    ahora = time.time()

    if (
        texto_normalizado == ultimo_texto
        and ahora - ultimo_texto_tiempo < 2
    ):

        print(
            "Comando repetido ignorado."
        )

        return

    ultimo_texto = texto_normalizado
    ultimo_texto_tiempo = ahora

    # ========================================================
    # JARVIS
    # ========================================================

    if texto_normalizado.startswith(
        "jarvis"
    ):

        resto = (
            texto_normalizado[6:]
            .strip()
        )

        if not resto:

            modo_conversacion = True

            hablar(
                "¿Sí señor?"
            )

            return

        modo_conversacion = True

        procesar_comando(
            resto
        )

        return

    # ========================================================
    # MODO CONVERSACIÓN
    # ========================================================

    if modo_conversacion:

        procesar_comando(
            texto_normalizado
        )

        return

    # ========================================================
    # SIN JARVIS
    # ========================================================

    print(
        "Ignorado. Di 'Jarvis' para iniciar."
    )


# ============================================================
# COMPROBAR ESP32
# ============================================================

def comprobar_esp32():

    print(
        f"Comprobando ESP32 en {ESP32_IP}..."
    )

    valores = obtener_valores()

    if valores is None:

        print(
            "No se pudo conectar con el ESP32."
        )

        return False

    print(
        "ESP32 conectado."
    )

    print(
        "Valores actuales:",
        valores
    )

    return True


# ============================================================
# MAIN
# ============================================================

def main():

    global modo_conversacion

    print()
    print("=" * 55)
    print(
        "       JARVIS - MONITOR DE SIGNOS VITALES"
    )
    print("=" * 55)
    print()

    if not comprobar_esp32():

        print(
            "Revisa la IP del ESP32."
        )

        return

    calibrar_microfono()

    # ========================================================
    # VIGÍA EN SEGUNDO PLANO
    # ========================================================

    threading.Thread(
        target=hilo_vigia_funcion,
        daemon=True
    ).start()

    print()
    print(
        "Sistema Jarvis iniciado."
    )
    print()
    print(
        "Di 'Jarvis' para comenzar."
    )
    print()
    print(
        "Después puedes hablar normalmente."
    )
    print()
    print("=" * 55)
    print()

    hablar(
        "Sistema Jarvis iniciado."
    )

    # ========================================================
    # LOOP PRINCIPAL
    # ========================================================

    while True:

        try:

            if not audio_disponible():

                time.sleep(0.1)

                continue

            texto = escuchar()

            if texto:

                procesar_voz(
                    texto
                )

        except KeyboardInterrupt:

            print()
            print(
                "Jarvis finalizado."
            )

            break

        except Exception as e:

            print(
                "Error en el ciclo principal:",
                e
            )

            time.sleep(1)


# ============================================================
# INICIO
# ============================================================

if __name__ == "__main__":

    main()