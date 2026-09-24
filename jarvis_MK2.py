import requests
import speech_recognition as sr
import wave
import asyncio
import edge_tts
import unicodedata
import re
import time
import os
import shutil
import subprocess


# ============================================================
# CONFIGURACIÓN
# ============================================================

ESP32_IP = "10.129.42.191"
MICROFONO = 1

MP3_FILE = "jarvis_respuesta.mp3"
WAV_FILE = "jarvis_respuesta.wav"

# VOZ
VOZ = "es-MX-JorgeNeural"
VELOCIDAD_VOZ = "-5%"
TONO_VOZ = "-12Hz"
VOLUMEN_VOZ = "+0%"

# ============================================================
# RANGOS DEL MODO VIGÍA
# ============================================================

RANGOS = {
    "FC": (80, 120),
    "SpO2": (95, 100),
    "TEMP": (36.0, 37.5),
    "SYS": (90, 140),
    "DIA": (60, 90)
}

# ============================================================
# VARIABLES DE ESTADO
# ============================================================

modo_vigia = False
modo_conversacion = False

estado_alerta = {
    "FC": False,
    "SpO2": False,
    "TEMP": False,
    "SYS": False,
    "DIA": False
}

ultimo_vigia = 0
INTERVALO_VIGIA = 2.0


# ============================================================
# NORMALIZAR TEXTO
# ============================================================

def normalizar(texto):
    texto = texto.lower()

    texto = unicodedata.normalize("NFD", texto)
    texto = "".join(
        c for c in texto
        if unicodedata.category(c) != "Mn"
    )

    texto = re.sub(r"[^\w\s]", " ", texto)
    texto = re.sub(r"\s+", " ", texto).strip()

    return texto


# ============================================================
# OBTENER VALORES DEL ESP32
# ============================================================

def obtener_valores():

    try:
        url = f"http://{ESP32_IP}/valores"

        respuesta = requests.get(
            url,
            timeout=2
        )

        texto = respuesta.text

        valores = {}

        for linea in texto.splitlines():

            if "=" in linea:

                clave, valor = linea.split("=", 1)

                clave = clave.strip()
                valor = valor.strip()

                try:
                    valores[clave] = float(valor)
                except:
                    pass

        return valores

    except Exception as e:

        print("Error obteniendo valores:", e)

        return None


# ============================================================
# GENERAR RESPUESTAS
# ============================================================

def generar_respuesta(comando, valores):

    if valores is None:
        return "No puedo obtener los signos vitales en este momento señor."

    if comando == "todos":

        fc = valores.get("FC", 0)
        spo2 = valores.get("SpO2", 0)
        temp = valores.get("TEMP", 0)
        sys = valores.get("SYS", 0)
        dia = valores.get("DIA", 0)

        return (
            f"Sus signos vitales actuales son: "
            f"frecuencia cardiaca de {fc:.0f} pulsaciones por minuto, "
            f"saturación de oxígeno de {spo2:.0f} por ciento, "
            f"temperatura de {temp:.1f} grados, "
            f"y presión arterial de {sys:.0f} sobre {dia:.0f}."
        )

    if comando == "fc":

        fc = valores.get("FC", 0)

        return (
            f"Su frecuencia cardiaca es de "
            f"{fc:.0f} pulsaciones por minuto."
        )

    if comando == "spo2":

        spo2 = valores.get("SpO2", 0)

        return (
            f"Su saturación de oxígeno es de "
            f"{spo2:.0f} por ciento."
        )

    if comando == "temperatura":

        temp = valores.get("TEMP", 0)

        return (
            f"Su temperatura es de "
            f"{temp:.1f} grados."
        )

    if comando == "presion":

        sys = valores.get("SYS", 0)
        dia = valores.get("DIA", 0)

        return (
            f"Su presión arterial es de "
            f"{sys:.0f} sobre {dia:.0f}."
        )

    return None


# ============================================================
# TEXTO A VOZ
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


def convertir_mp3_wav():

    ffmpeg = shutil.which("ffmpeg")

    if ffmpeg is None:

        print("ERROR: FFmpeg no está instalado o no está en PATH.")

        return False

    comando = [
        ffmpeg,
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


def leer_pcm_wav():

    with wave.open(WAV_FILE, "rb") as wav:

        canales = wav.getnchannels()
        sample_width = wav.getsampwidth()
        frecuencia = wav.getframerate()

        datos = wav.readframes(
            wav.getnframes()
        )

    return datos, frecuencia, canales, sample_width


def hablar(texto):

    print(f"JARVIS: {texto}")

    try:

        asyncio.run(
            generar_audio(texto)
        )

        if not convertir_mp3_wav():
            return

        datos, frecuencia, canales, sample_width = leer_pcm_wav()

        url = f"http://{ESP32_IP}/play"

        respuesta = requests.post(
            url,
            data=datos,
            headers={
                "Content-Type": "application/octet-stream"
            },
            timeout=20
        )

        if respuesta.status_code != 200:

            print(
                "Error enviando audio:",
                respuesta.status_code
            )

        # Pequeña pausa para evitar que el micrófono
        # vuelva a captar inmediatamente la voz de Jarvis.
        time.sleep(0.5)

    except Exception as e:

        print("Error reproduciendo audio:", e)


# ============================================================
# COMPROBAR RANGOS
# ============================================================

def comprobar_rango(valor, minimo, maximo):

    if valor < minimo:
        return "bajo"

    if valor > maximo:
        return "alto"

    return "normal"


# ============================================================
# MODO VIGÍA
# ============================================================

def vigilar_signos():

    global estado_alerta

    valores = obtener_valores()

    if valores is None:
        return

    alertas_nuevas = []

    # --------------------------------------------------------
    # FRECUENCIA CARDIACA
    # --------------------------------------------------------

    if "FC" in valores:

        valor = valores["FC"]

        estado = comprobar_rango(
            valor,
            *RANGOS["FC"]
        )

        fuera = estado != "normal"

        if fuera and not estado_alerta["FC"]:

            if estado == "alto":

                alertas_nuevas.append(
                    f"Frecuencia cardiaca alta, "
                    f"{valor:.0f} pulsaciones por minuto"
                )

            elif estado == "bajo":

                alertas_nuevas.append(
                    f"Frecuencia cardiaca baja, "
                    f"{valor:.0f} pulsaciones por minuto"
                )

        estado_alerta["FC"] = fuera

    # --------------------------------------------------------
    # SPO2
    # --------------------------------------------------------

    if "SpO2" in valores:

        valor = valores["SpO2"]

        estado = comprobar_rango(
            valor,
            *RANGOS["SpO2"]
        )

        fuera = estado != "normal"

        if fuera and not estado_alerta["SpO2"]:

            if estado == "alto":

                alertas_nuevas.append(
                    f"Saturación de oxígeno alta, "
                    f"{valor:.0f} por ciento"
                )

            elif estado == "bajo":

                alertas_nuevas.append(
                    f"Saturación de oxígeno baja, "
                    f"{valor:.0f} por ciento"
                )

        estado_alerta["SpO2"] = fuera

    # --------------------------------------------------------
    # TEMPERATURA
    # --------------------------------------------------------

    if "TEMP" in valores:

        valor = valores["TEMP"]

        estado = comprobar_rango(
            valor,
            *RANGOS["TEMP"]
        )

        fuera = estado != "normal"

        if fuera and not estado_alerta["TEMP"]:

            if estado == "alto":

                alertas_nuevas.append(
                    f"Temperatura alta, "
                    f"{valor:.1f} grados"
                )

            elif estado == "bajo":

                alertas_nuevas.append(
                    f"Temperatura baja, "
                    f"{valor:.1f} grados"
                )

        estado_alerta["TEMP"] = fuera

    # --------------------------------------------------------
    # PRESIÓN SISTÓLICA
    # --------------------------------------------------------

    if "SYS" in valores:

        valor = valores["SYS"]

        estado = comprobar_rango(
            valor,
            *RANGOS["SYS"]
        )

        fuera = estado != "normal"

        if fuera and not estado_alerta["SYS"]:

            if estado == "alto":

                alertas_nuevas.append(
                    f"Presión sistólica alta, "
                    f"{valor:.0f} milímetros de mercurio"
                )

            elif estado == "bajo":

                alertas_nuevas.append(
                    f"Presión sistólica baja, "
                    f"{valor:.0f} milímetros de mercurio"
                )

        estado_alerta["SYS"] = fuera

    # --------------------------------------------------------
    # PRESIÓN DIASTÓLICA
    # --------------------------------------------------------

    if "DIA" in valores:

        valor = valores["DIA"]

        estado = comprobar_rango(
            valor,
            *RANGOS["DIA"]
        )

        fuera = estado != "normal"

        if fuera and not estado_alerta["DIA"]:

            if estado == "alto":

                alertas_nuevas.append(
                    f"Presión diastólica alta, "
                    f"{valor:.0f} milímetros de mercurio"
                )

            elif estado == "bajo":

                alertas_nuevas.append(
                    f"Presión diastólica baja, "
                    f"{valor:.0f} milímetros de mercurio"
                )

        estado_alerta["DIA"] = fuera

    # --------------------------------------------------------
    # HABLAR SOLO SI APARECE UNA NUEVA ALERTA
    # --------------------------------------------------------

    if alertas_nuevas:

        mensaje = ". ".join(
            alertas_nuevas
        ) + "."

        hablar(mensaje)


# ============================================================
# ACTIVAR VIGÍA
# ============================================================

def activar_vigia():

    global modo_vigia
    global estado_alerta
    global ultimo_vigia

    modo_vigia = True

    estado_alerta = {
        "FC": False,
        "SpO2": False,
        "TEMP": False,
        "SYS": False,
        "DIA": False
    }

    ultimo_vigia = time.time()

    hablar(
        "Modo vigía activado señor. "
        "Supervisaré constantemente sus signos vitales."
    )


# ============================================================
# DESACTIVAR VIGÍA
# ============================================================

def desactivar_vigia():

    global modo_vigia
    global estado_alerta

    modo_vigia = False

    estado_alerta = {
        "FC": False,
        "SpO2": False,
        "TEMP": False,
        "SYS": False,
        "DIA": False
    }

    hablar(
        "Modo vigía desactivado señor."
    )


# ============================================================
# DETECTAR COMANDO
# ============================================================

def detectar_comando(texto, exigir_jarvis=True):

    texto = normalizar(texto)

    if not texto:
        return None

    # ========================================================
    # SALIR DE CONVERSACIÓN
    # ========================================================

    frases_salida = [
        "adios",
        "hasta luego",
        "descansa",
        "silencio",
        "deja de escuchar",
        "terminamos",
        "eso es todo",
        "ya esta",
        "gracias jarvis"
    ]

    for frase in frases_salida:

        if frase in texto:

            return "salir"


    # ========================================================
    # DESACTIVAR VIGÍA
    #
    # IMPORTANTE:
    # ESTO SE REVISA ANTES DE ACTIVAR.
    #
    # También acepta frases incompletas como:
    # "desactiva el modo"
    # "desactiva el modo vi"
    # ========================================================

    patrones_desactivar = [

        "desactiva modo vigia",
        "desactivar modo vigia",
        "desactiva el modo vigia",
        "desactivar el modo vigia",

        "apaga modo vigia",
        "apagar modo vigia",
        "apaga el modo vigia",
        "apagar el modo vigia",

        "deten modo vigia",
        "detener modo vigia",
        "deten el modo vigia",
        "detener el modo vigia",

        "desactiva vigia",
        "desactivar vigia",

        "apaga vigia",
        "apagar vigia",

        "deten vigia",
        "detener vigia"
    ]

    for patron in patrones_desactivar:

        if patron in texto:

            return "desactivar_vigia"


    # ========================================================
    # DETECCIÓN INTELIGENTE DE DESACTIVACIÓN PARCIAL
    # ========================================================

    tiene_desactivar = any(
        palabra in texto
        for palabra in [
            "desactiva",
            "desactivar",
            "apaga",
            "apagar",
            "deten",
            "detener"
        ]
    )

    tiene_modo = (
        "modo" in texto
        or "vigia" in texto
        or "vi" in texto.split()
    )

    if tiene_desactivar and tiene_modo:

        return "desactivar_vigia"


    # ========================================================
    # ACTIVAR VIGÍA
    #
    # También acepta:
    # "activa el modo"
    # "activa el modo vi"
    # "activar modo"
    # ========================================================

    patrones_activar = [

        "modo vigia",

        "activa vigia",
        "activar vigia",

        "enciende vigia",
        "encender vigia",

        "enciende el modo vigia",
        "encender el modo vigia",

        "activa el modo vigia",
        "activar el modo vigia",

        "enciende modo vigia",
        "encender modo vigia"
    ]

    for patron in patrones_activar:

        if patron in texto:

            return "activar_vigia"


    # ========================================================
    # DETECCIÓN INTELIGENTE DE ACTIVACIÓN PARCIAL
    # ========================================================

    tiene_activar = any(
        palabra in texto
        for palabra in [
            "activa",
            "activar",
            "enciende",
            "encender"
        ]
    )

    if tiene_activar and tiene_modo:

        return "activar_vigia"


    # ========================================================
    # COMANDOS DE SIGNOS VITALES
    # ========================================================

    # Todos los signos

    palabras_todos = [
        "signos vitales",
        "mis signos",
        "como estoy",
        "como estan mis signos",
        "dime mis signos",
        "dime mis signos vitales"
    ]

    for frase in palabras_todos:

        if frase in texto:

            if exigir_jarvis and "jarvis" not in texto:

                return None

            return "todos"


    # Frecuencia cardiaca

    palabras_fc = [
        "frecuencia cardiaca",
        "frecuencia cardíaca",
        "pulso",
        "pulsaciones",
        "latidos",
        "cuantas pulsaciones",
        "cuantos latidos",
        "cual es mi frecuencia",
        "cual es mi pulso"
    ]

    for frase in palabras_fc:

        if frase in texto:

            if exigir_jarvis and "jarvis" not in texto:

                return None

            return "fc"


    # SpO2

    palabras_spo2 = [
        "spo2",
        "saturacion",
        "saturación",
        "oxigeno",
        "oxigenación",
        "oxigenacion",
        "nivel de oxigeno",
        "nivel de oxígeno"
    ]

    for frase in palabras_spo2:

        if frase in texto:

            if exigir_jarvis and "jarvis" not in texto:

                return None

            return "spo2"


    # Temperatura

    palabras_temp = [
        "temperatura",
        "cuanto tengo de temperatura",
        "cual es mi temperatura"
    ]

    for frase in palabras_temp:

        if frase in texto:

            if exigir_jarvis and "jarvis" not in texto:

                return None

            return "temperatura"


    # Presión

    palabras_presion = [
        "presion",
        "tension",
        "presion arterial",
        "tensión arterial",
        "cuanto tengo de presion",
        "cual es mi presion"
    ]

    for frase in palabras_presion:

        if frase in texto:

            if exigir_jarvis and "jarvis" not in texto:

                return None

            return "presion"


    return None


# ============================================================
# RECONOCIMIENTO DE AUDIO
# ============================================================

reconocedor = sr.Recognizer()

microfono = sr.Microphone(
    device_index=MICROFONO
)


def reconocer_audio():

    with microfono as source:

        print("\nEscuchando...")

        try:

            audio = reconocedor.listen(
                source,
                timeout=5,
                phrase_time_limit=6
            )

        except sr.WaitTimeoutError:

            return None

    try:

        texto = reconocedor.recognize_google(
            audio,
            language="es-CO"
        )

        print("Tú:", texto)

        return texto

    except sr.UnknownValueError:

        print("No se entendió el audio.")

        return None

    except sr.RequestError as e:

        print(
            "Error con reconocimiento de voz:",
            e
        )

        return None


# ============================================================
# PROGRAMA PRINCIPAL
# ============================================================

print()
print("======================================")
print("       JARVIS - MONITOR VITAL")
print("======================================")
print()
print("Jarvis usa el micrófono del PC")
print("Modo Vigía disponible")
print()

hablar("Sistema listo señor.")


while True:

    # ========================================================
    # VIGÍA EN SEGUNDO PLANO
    # ========================================================

    if modo_vigia:

        ahora = time.time()

        if ahora - ultimo_vigia >= INTERVALO_VIGIA:

            ultimo_vigia = ahora

            vigilar_signos()


    # ========================================================
    # ESCUCHAR
    # ========================================================

    texto = reconocer_audio()

    if texto is None:
        continue

    texto_normalizado = normalizar(texto)


    # ========================================================
    # ACTIVACIÓN SIMPLE: "JARVIS"
    # ========================================================

    if (
        texto_normalizado == "jarvis"
        or texto_normalizado == "jarvis jarvis"
    ):

        hablar("¿Sí señor?")

        modo_conversacion = True

        continue


    # ========================================================
    # DETECTAR COMANDO
    # ========================================================

    comando = detectar_comando(
        texto,
        exigir_jarvis=not modo_conversacion
    )


    # ========================================================
    # SALIR
    # ========================================================

    if comando == "salir":

        hablar(
            "Entendido señor."
        )

        modo_conversacion = False

        continue


    # ========================================================
    # ACTIVAR VIGÍA
    # ========================================================

    if comando == "activar_vigia":

        if not modo_vigia:

            activar_vigia()

        else:

            hablar(
                "El modo vigía ya está activo señor."
            )

        continue


    # ========================================================
    # DESACTIVAR VIGÍA
    # ========================================================

    if comando == "desactivar_vigia":

        if modo_vigia:

            desactivar_vigia()

        else:

            hablar(
                "El modo vigía ya está desactivado señor."
            )

        continue


    # ========================================================
    # COMANDOS DE SIGNOS
    # ========================================================

    if comando in [
        "todos",
        "fc",
        "spo2",
        "temperatura",
        "presion"
    ]:

        valores = obtener_valores()

        respuesta = generar_respuesta(
            comando,
            valores
        )

        if respuesta:

            hablar(respuesta)

        continue


    # ========================================================
    # COMANDO NO RECONOCIDO
    # ========================================================

    print(
        "Comando no reconocido. "
        "Jarvis permanecerá en silencio."
    )

