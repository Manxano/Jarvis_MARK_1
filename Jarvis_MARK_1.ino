#include <Arduino.h>
#include <WiFi.h>
#include <LovyanGFX.hpp>
#include <Wire.h>

#include "AudioTools.h"
#include "AudioTools/AudioLibs/I2SCodecStream.h"


// =========================================================
// WIFI
// =========================================================

const char* ssid = "vivo V30 Lite";
const char* password = "davidsote";

WiFiServer server(80);


// =========================================================
// PANTALLA ILI9341
// =========================================================

class LGFX : public lgfx::LGFX_Device
{
  lgfx::Panel_ILI9341 _panel;
  lgfx::Bus_SPI _bus;

public:

  LGFX()
  {
    auto cfg = _bus.config();

    cfg.spi_host = SPI2_HOST;
    cfg.spi_mode = 0;
    cfg.freq_write = 40000000;
    cfg.freq_read = 16000000;

    cfg.pin_sclk = 12;
    cfg.pin_mosi = 11;
    cfg.pin_miso = 13;
    cfg.pin_dc   = 46;

    _bus.config(cfg);
    _panel.setBus(&_bus);

    auto pcfg = _panel.config();

    pcfg.pin_cs  = 10;
    pcfg.pin_rst = -1;

    pcfg.panel_width  = 240;
    pcfg.panel_height = 320;

    pcfg.memory_width  = 240;
    pcfg.memory_height = 320;

    _panel.config(pcfg);

    setPanel(&_panel);
  }
};

LGFX display;

#define BACKLIGHT 45


// =========================================================
// TOUCH FT6336
// =========================================================

#define TOUCH_SDA 16
#define TOUCH_SCL 15

#define FT6336_ADDR 0x38

#define FT_REG_STATUS 0x02
#define FT_REG_XH     0x03
#define FT_REG_XL     0x04
#define FT_REG_YH     0x05
#define FT_REG_YL     0x06


// =========================================================
// PANTALLAS
// =========================================================

enum Pantalla
{
  INICIO,
  PRINCIPAL,
  FC,
  SPO2,
  TEMPERATURA,
  PRESION
};

Pantalla pantallaActual = INICIO;


// =========================================================
// VALORES SIMULADOS
// =========================================================

int frecuencia = 130;
int spo2 = 98;
float temperatura = 36.5;

int sistolica = 120;
int diastolica = 80;


// =========================================================
// TOUCH
// =========================================================

unsigned long ultimoTouch = 0;

const unsigned long debounceTouch = 150;


// =========================================================
// AUDIO - TX
// =========================================================
//
// IMPORTANTE:
// Python envia:
// 44100 Hz
// 2 canales
// 16 bits
//
// Por eso el ESP32 debe usar exactamente el mismo formato.
// =========================================================

AudioInfo infoTX(44100, 2, 16);

I2SCodecStream audioTX(ESP32S3HosyondDisplay);

uint8_t bufferAudio[2048];


// =========================================================
// COLORES SOBRIOS
// =========================================================

#define COLOR_FONDO      0xFFDF
#define COLOR_AZUL       0x11CB
#define COLOR_AZUL_CLARO 0xDF5E
#define COLOR_GRIS       0x6B90
#define COLOR_GRIS_CLARO 0xE73D
#define COLOR_TEXTO      0x1946
#define COLOR_BLANCO     0xFFFF
#define COLOR_VERDE      0x2BEB


// =========================================================
// TOUCH
// =========================================================

bool leerTouch(int &x, int &y)
{
  Wire.beginTransmission(FT6336_ADDR);
  Wire.write(FT_REG_STATUS);

  if (Wire.endTransmission(false) != 0)
    return false;

  uint8_t cantidad = Wire.requestFrom(
    FT6336_ADDR,
    (uint8_t)5
  );

  if (cantidad < 5)
    return false;

  uint8_t touches = Wire.read();

  uint8_t xh = Wire.read();
  uint8_t xl = Wire.read();

  uint8_t yh = Wire.read();
  uint8_t yl = Wire.read();

  if (touches == 0)
    return false;

  x = ((xh & 0x0F) << 8) | xl;
  y = ((yh & 0x0F) << 8) | yl;

  x = 239 - x;
  y = 319 - y;

  x = constrain(x, 0, 239);
  y = constrain(y, 0, 319);

  return true;
}


// =========================================================
// TEXTO
// =========================================================

void textoCentrado(
  const char* texto,
  int x,
  int y,
  int size,
  uint16_t color
)
{
  display.setTextDatum(MC_DATUM);
  display.setTextColor(color);
  display.setTextSize(size);
  display.drawString(texto, x, y);
  display.setTextDatum(TL_DATUM);
}


// =========================================================
// BOTON ATRAS
// =========================================================

void dibujarBotonAtras()
{
  display.fillRoundRect(
    8, 8,
    72, 38,
    10,
    COLOR_AZUL_CLARO
  );

  display.setTextColor(COLOR_AZUL);
  display.setTextSize(2);

  display.setTextDatum(MC_DATUM);

  display.drawString(
    "<",
    22,
    27
  );

  display.setTextSize(1);

  display.drawString(
    "ATRAS",
    49,
    27
  );

  display.setTextDatum(TL_DATUM);
}


// =========================================================
// INICIO
// =========================================================

void dibujarInicio()
{
  pantallaActual = INICIO;

  display.fillScreen(COLOR_FONDO);

  display.fillRect(
    0, 0,
    240, 5,
    COLOR_AZUL
  );

  textoCentrado(
    "MONITOR DE SIGNOS",
    120,
    105,
    2,
    COLOR_AZUL
  );

  textoCentrado(
    "VITALES",
    120,
    130,
    2,
    COLOR_AZUL
  );

  textoCentrado(
    "Sistema de monitorización",
    120,
    158,
    1,
    COLOR_GRIS
  );

  display.drawRoundRect(
    60, 215,
    120, 48,
    12,
    COLOR_AZUL
  );

  display.fillRoundRect(
    60, 215,
    120, 48,
    12,
    COLOR_AZUL
  );

  textoCentrado(
    "INICIAR",
    120,
    239,
    2,
    COLOR_BLANCO
  );
}


// =========================================================
// PRINCIPAL
// =========================================================

void dibujarPrincipal()
{
  pantallaActual = PRINCIPAL;

  display.fillScreen(COLOR_FONDO);

  display.fillRect(
    0, 0,
    240, 4,
    COLOR_AZUL
  );

  textoCentrado(
    "SIGNOS VITALES",
    120,
    27,
    2,
    COLOR_AZUL
  );

  textoCentrado(
    "Monitor en tiempo real",
    120,
    47,
    1,
    COLOR_GRIS
  );


  // -------------------------------------------------------
  // FC
  // -------------------------------------------------------

  display.fillRoundRect(
    10, 65,
    105, 85,
    12,
    COLOR_BLANCO
  );

  display.drawRoundRect(
    10, 65,
    105, 85,
    12,
    COLOR_GRIS_CLARO
  );

  textoCentrado(
    "FC",
    28,
    83,
    1,
    COLOR_GRIS
  );

  textoCentrado(
    String(frecuencia).c_str(),
    63,
    108,
    3,
    COLOR_AZUL
  );

  textoCentrado(
    "lpm",
    63,
    133,
    1,
    COLOR_GRIS
  );


  // -------------------------------------------------------
  // SPO2
  // -------------------------------------------------------

  display.fillRoundRect(
    125, 65,
    105, 85,
    12,
    COLOR_BLANCO
  );

  display.drawRoundRect(
    125, 65,
    105, 85,
    12,
    COLOR_GRIS_CLARO
  );

  textoCentrado(
    "SpO2",
    143,
    83,
    1,
    COLOR_GRIS
  );

  textoCentrado(
    String(spo2).c_str(),
    178,
    108,
    3,
    COLOR_AZUL
  );

  textoCentrado(
    "%",
    178,
    133,
    1,
    COLOR_GRIS
  );


  // -------------------------------------------------------
  // TEMPERATURA
  // -------------------------------------------------------

  display.fillRoundRect(
    10, 165,
    105, 85,
    12,
    COLOR_BLANCO
  );

  display.drawRoundRect(
    10, 165,
    105, 85,
    12,
    COLOR_GRIS_CLARO
  );

  textoCentrado(
    "TEMP",
    28,
    183,
    1,
    COLOR_GRIS
  );

  textoCentrado(
    String(temperatura, 1).c_str(),
    63,
    208,
    2,
    COLOR_AZUL
  );

  textoCentrado(
    "°C",
    63,
    233,
    1,
    COLOR_GRIS
  );


  // -------------------------------------------------------
  // PRESION
  // -------------------------------------------------------

  display.fillRoundRect(
    125, 165,
    105, 85,
    12,
    COLOR_BLANCO
  );

  display.drawRoundRect(
    125, 165,
    105, 85,
    12,
    COLOR_GRIS_CLARO
  );

  textoCentrado(
    "PRESION",
    143,
    183,
    1,
    COLOR_GRIS
  );

  char presion[20];

  sprintf(
    presion,
    "%d/%d",
    sistolica,
    diastolica
  );

  textoCentrado(
    presion,
    178,
    208,
    2,
    COLOR_AZUL
  );

  textoCentrado(
    "mmHg",
    178,
    233,
    1,
    COLOR_GRIS
  );
}


// =========================================================
// FC
// =========================================================

void dibujarFC()
{
  pantallaActual = FC;

  display.fillScreen(COLOR_FONDO);

  dibujarBotonAtras();

  textoCentrado(
    "FRECUENCIA CARDIACA",
    120,
    27,
    1,
    COLOR_AZUL
  );

  textoCentrado(
    String(frecuencia).c_str(),
    120,
    65,
    4,
    COLOR_AZUL
  );

  textoCentrado(
    "latidos por minuto",
    120,
    92,
    1,
    COLOR_GRIS
  );

  display.drawRoundRect(
    10, 115,
    220, 145,
    8,
    COLOR_GRIS_CLARO
  );

  for (int y = 135; y <= 240; y += 25)
  {
    display.drawFastHLine(
      20,
      y,
      200,
      COLOR_GRIS_CLARO
    );
  }
}


// =========================================================
// GRAFICA ECG ANIMADA
// =========================================================

void actualizarECG()
{
  if (pantallaActual != FC)
    return;

  display.fillRoundRect(
    11, 116,
    218, 143,
    7,
    COLOR_FONDO
  );

  for (int y = 135; y <= 240; y += 25)
  {
    display.drawFastHLine(
      20,
      y,
      200,
      COLOR_GRIS_CLARO
    );
  }

  static int fase = 0;

  int xAnterior = 20;
  int yAnterior = 190;

  for (int x = 20; x < 220; x++)
  {
    int posicion =
      (x + fase) % 80;

    int y = 190;

    if (posicion < 45)
    {
      y = 190;
    }
    else if (posicion < 50)
    {
      y = 190 - (posicion - 45) * 8;
    }
    else if (posicion < 55)
    {
      y = 150 + (posicion - 50) * 8;
    }
    else if (posicion < 59)
    {
      y = 190 - (posicion - 55) * 16;
    }
    else if (posicion < 64)
    {
      y = 126 + (posicion - 59) * 13;
    }
    else
    {
      y = 190;
    }

    y = constrain(y, 125, 245);

    if (x > 20)
    {
      display.drawLine(
        xAnterior,
        yAnterior,
        x,
        y,
        COLOR_AZUL
      );
    }

    xAnterior = x;
    yAnterior = y;
  }

  fase += 3;

  if (fase >= 80)
    fase = 0;
}


// =========================================================
// SPO2
// =========================================================

void dibujarSpO2()
{
  pantallaActual = SPO2;

  display.fillScreen(COLOR_FONDO);

  dibujarBotonAtras();

  textoCentrado(
    "SATURACION DE OXIGENO",
    120,
    27,
    1,
    COLOR_AZUL
  );

  textoCentrado(
    String(spo2).c_str(),
    120,
    105,
    5,
    COLOR_AZUL
  );

  textoCentrado(
    "%",
    120,
    145,
    2,
    COLOR_GRIS
  );

  display.drawRoundRect(
    35, 185,
    170, 20,
    10,
    COLOR_GRIS_CLARO
  );

  int ancho = map(
    spo2,
    0,
    100,
    0,
    170
  );

  display.fillRoundRect(
    35, 185,
    ancho,
    20,
    10,
    COLOR_AZUL
  );

  textoCentrado(
    "Saturacion de oxigeno",
    120,
    230,
    1,
    COLOR_GRIS
  );
}


// =========================================================
// TEMPERATURA
// =========================================================

void dibujarTemperatura()
{
  pantallaActual = TEMPERATURA;

  display.fillScreen(COLOR_FONDO);

  dibujarBotonAtras();

  textoCentrado(
    "TEMPERATURA",
    120,
    27,
    2,
    COLOR_AZUL
  );

  textoCentrado(
    String(temperatura, 1).c_str(),
    120,
    65,
    4,
    COLOR_AZUL
  );

  textoCentrado(
    "grados Celsius",
    120,
    95,
    1,
    COLOR_GRIS
  );

  display.drawRoundRect(
    105, 125,
    30, 110,
    15,
    COLOR_GRIS
  );

  display.fillRoundRect(
    112, 150,
    16, 72,
    8,
    COLOR_AZUL
  );

  display.fillCircle(
    120,
    225,
    18,
    COLOR_AZUL
  );

  display.drawFastHLine(
    145,
    145,
    25,
    COLOR_GRIS
  );

  display.drawFastHLine(
    145,
    175,
    25,
    COLOR_GRIS
  );

  display.drawFastHLine(
    145,
    205,
    25,
    COLOR_GRIS
  );
}


// =========================================================
// PRESION
// =========================================================

void dibujarPresion()
{
  pantallaActual = PRESION;

  display.fillScreen(COLOR_FONDO);

  dibujarBotonAtras();

  textoCentrado(
    "PRESION ARTERIAL",
    120,
    27,
    2,
    COLOR_AZUL
  );

  char presion[30];

  sprintf(
    presion,
    "%d / %d",
    sistolica,
    diastolica
  );

  textoCentrado(
    presion,
    120,
    90,
    4,
    COLOR_AZUL
  );

  textoCentrado(
    "mmHg",
    120,
    120,
    1,
    COLOR_GRIS
  );

  display.drawRoundRect(
    30, 160,
    180, 18,
    9,
    COLOR_GRIS_CLARO
  );

  int anchoSYS = map(
    sistolica,
    0,
    200,
    0,
    180
  );

  display.fillRoundRect(
    30,
    160,
    constrain(anchoSYS, 0, 180),
    18,
    9,
    COLOR_AZUL
  );

  display.drawRoundRect(
    30, 205,
    180, 18,
    9,
    COLOR_GRIS_CLARO
  );

  int anchoDIA = map(
    diastolica,
    0,
    150,
    0,
    180
  );

  display.fillRoundRect(
    30,
    205,
    constrain(anchoDIA, 0, 180),
    18,
    9,
    COLOR_AZUL
  );

  textoCentrado(
    "Sistolica",
    120,
    190,
    1,
    COLOR_GRIS
  );

  textoCentrado(
    "Diastolica",
    120,
    235,
    1,
    COLOR_GRIS
  );
}


// =========================================================
// TOUCH
// =========================================================

void procesarTouch()
{
  int x;
  int y;

  if (!leerTouch(x, y))
    return;

  if (
    millis() - ultimoTouch
    < debounceTouch
  )
  {
    return;
  }

  ultimoTouch = millis();


  if (pantallaActual == INICIO)
  {
    if (
      x >= 60 &&
      x <= 180 &&
      y >= 215 &&
      y <= 265
    )
    {
      dibujarPrincipal();
    }

    return;
  }


  if (pantallaActual == PRINCIPAL)
  {
    if (
      x < 120 &&
      y >= 65 &&
      y <= 150
    )
    {
      dibujarFC();
      return;
    }

    if (
      x >= 120 &&
      y >= 65 &&
      y <= 150
    )
    {
      dibujarSpO2();
      return;
    }

    if (
      x < 120 &&
      y >= 165 &&
      y <= 250
    )
    {
      dibujarTemperatura();
      return;
    }

    if (
      x >= 120 &&
      y >= 165 &&
      y <= 250
    )
    {
      dibujarPresion();
      return;
    }
  }


  if (
    pantallaActual != PRINCIPAL &&
    x >= 5 &&
    x <= 85 &&
    y >= 5 &&
    y <= 55
  )
  {
    dibujarPrincipal();
    return;
  }
}


// =========================================================
// PAGINA WEB
// =========================================================

void paginaWeb(WiFiClient &client)
{
  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: text/html");
  client.println("Connection: close");
  client.println();

  client.println("<!DOCTYPE html>");
  client.println("<html>");
  client.println("<head>");
  client.println("<meta charset='UTF-8'>");
  client.println("<title>Monitor de Signos Vitales</title>");
  client.println("</head>");

  client.println("<body>");
  client.println("<h1>Monitor de Signos Vitales</h1>");

  client.print("<p>Frecuencia cardiaca: ");
  client.print(frecuencia);
  client.println(" lpm</p>");

  client.print("<p>SpO2: ");
  client.print(spo2);
  client.println(" %</p>");

  client.print("<p>Temperatura: ");
  client.print(temperatura, 1);
  client.println(" °C</p>");

  client.print("<p>Presion arterial: ");
  client.print(sistolica);
  client.print("/");
  client.print(diastolica);
  client.println(" mmHg</p>");

  client.println("</body>");
  client.println("</html>");
}


// =========================================================
// /VALORES
// =========================================================

void enviarValores(WiFiClient &client)
{
  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: text/plain");
  client.println("Connection: close");
  client.println();

  client.print("FC=");
  client.println(frecuencia);

  client.print("SpO2=");
  client.println(spo2);

  client.print("TEMP=");
  client.println(temperatura, 1);

  client.print("SYS=");
  client.println(sistolica);

  client.print("DIA=");
  client.println(diastolica);
}


// =========================================================
// /PLAY
// =========================================================

void manejarPlay(
  WiFiClient &client,
  int contentLength
)
{
  if (contentLength <= 0)
  {
    client.println(
      "HTTP/1.1 400 Bad Request"
    );

    client.println(
      "Connection: close"
    );

    client.println();

    return;
  }


  Serial.println();
  Serial.println("==============================");
  Serial.println("       JARVIS AUDIO");
  Serial.println("==============================");

  Serial.print(
    "Audio recibido: "
  );

  Serial.print(
    contentLength
  );

  Serial.println(
    " bytes"
  );


  // =======================================================
  // CONFIGURAR AUDIO
  // =======================================================

  auto configTX =
    audioTX.defaultConfig(TX_MODE);

  configTX.copyFrom(infoTX);

  audioTX.begin(
    configTX
  );

  // 70 %
  audioTX.setVolume(
    0.70f
  );


  Serial.println(
    "Audio configurado:"
  );

  Serial.println(
    "44100 Hz | 2 canales | 16 bits"
  );


  // =======================================================
  // RECIBIR Y REPRODUCIR
  // =======================================================

  int restantes =
    contentLength;

  unsigned long ultimoDato =
    millis();


  while (
    restantes > 0
  )
  {

    int disponibles =
      client.available();


    if (disponibles > 0)
    {

      int cantidad =
        min(
          disponibles,
          (int)sizeof(bufferAudio)
        );


      int recibidos =
        client.read(
          bufferAudio,
          cantidad
        );


      if (recibidos > 0)
      {

        audioTX.write(
          bufferAudio,
          recibidos
        );

        restantes -= recibidos;

        ultimoDato =
          millis();

      }

    }
    else
    {

      // Esperar máximo 5 segundos
      // por más datos.

      if (
        millis() - ultimoDato
        > 5000
      )
      {
        Serial.println(
          "Timeout recibiendo audio."
        );

        break;
      }

      delay(1);
    }
  }


  Serial.println(
    "Recepcion de audio terminada."
  );


  // =======================================================
  // RESPONDER A PYTHON
  // =======================================================

  client.println(
    "HTTP/1.1 200 OK"
  );

  client.println(
    "Content-Type: text/plain"
  );

  client.println(
    "Connection: close"
  );

  client.println();

  client.println(
    "OK"
  );

  client.flush();


  Serial.println(
    "Respuesta enviada a Python."
  );

  Serial.println(
    "Jarvis reproduciendo por bocina."
  );

  Serial.println(
    "=============================="
  );

  Serial.println();
}


// =========================================================
// SERVIDOR
// =========================================================

void manejarServidor()
{
  WiFiClient client =
    server.available();

  if (!client)
    return;


  unsigned long inicio =
    millis();


  while (
    !client.available() &&
    millis() - inicio < 100
  )
  {
    delay(1);
  }


  if (!client.available())
  {
    client.stop();
    return;
  }


  String requestLine =
    client.readStringUntil('\r');

  client.readStringUntil('\n');


  int contentLength = 0;


  while (
    client.connected()
  )
  {

    String linea =
      client.readStringUntil('\n');

    linea.trim();


    if (linea.length() == 0)
      break;


    if (
      linea.startsWith(
        "Content-Length:"
      )
    )
    {

      String numero =
        linea.substring(
          strlen(
            "Content-Length:"
          )
        );

      numero.trim();

      contentLength =
        numero.toInt();
    }
  }


  Serial.print(
    "HTTP -> "
  );

  Serial.println(
    requestLine
  );


  // =======================================================
  // /VALORES
  // =======================================================

  if (
    requestLine.startsWith(
      "GET /valores"
    )
  )
  {

    enviarValores(
      client
    );

    delay(5);

    client.stop();

    return;
  }


  // =======================================================
  // /PLAY
  // =======================================================

  if (
    requestLine.startsWith(
      "POST /play"
    )
  )
  {

    manejarPlay(
      client,
      contentLength
    );

    delay(10);

    client.stop();

    return;
  }


  // =======================================================
  // PAGINA PRINCIPAL
  // =======================================================

  if (
    requestLine.startsWith(
      "GET /"
    )
  )
  {

    paginaWeb(
      client
    );

    delay(5);

    client.stop();

    return;
  }


  // =======================================================
  // 404
  // =======================================================

  client.println(
    "HTTP/1.1 404 Not Found"
  );

  client.println(
    "Connection: close"
  );

  client.println();

  client.stop();
}


// =========================================================
// SETUP
// =========================================================

void setup()
{
  Serial.begin(
    115200
  );

  delay(
    1000
  );


  // -------------------------------------------------------
  // BACKLIGHT
  // -------------------------------------------------------

  pinMode(
    BACKLIGHT,
    OUTPUT
  );

  digitalWrite(
    BACKLIGHT,
    HIGH
  );


  // -------------------------------------------------------
  // PANTALLA
  // -------------------------------------------------------

  display.init();

  display.setRotation(
    0
  );

  display.fillScreen(
    COLOR_FONDO
  );


  // -------------------------------------------------------
  // TOUCH
  // -------------------------------------------------------

  Wire.begin(
    TOUCH_SDA,
    TOUCH_SCL
  );

  Wire.setClock(
    400000
  );


  // -------------------------------------------------------
  // INTERFAZ
  // -------------------------------------------------------

  dibujarInicio();


  // -------------------------------------------------------
  // WIFI
  // -------------------------------------------------------

  WiFi.mode(
    WIFI_STA
  );

  WiFi.begin(
    ssid,
    password
  );


  Serial.println();
  Serial.println(
    "Conectando WiFi..."
  );


  while (
    WiFi.status()
    != WL_CONNECTED
  )
  {
    delay(500);

    Serial.print(
      "."
    );
  }


  Serial.println();
  Serial.println(
    "WiFi conectado"
  );


  Serial.print(
    "IP ESP32: "
  );

  Serial.println(
    WiFi.localIP()
  );


  // -------------------------------------------------------
  // SERVIDOR
  // -------------------------------------------------------

  server.begin();


  Serial.println(
    "Servidor iniciado"
  );

  Serial.println(
    "Sistema listo"
  );

  Serial.println(
    "Jarvis usa el microfono del PC"
  );

  Serial.println(
    "Audio: 44100 Hz / Stereo / 16 bit"
  );

  Serial.println(
    "Volumen Jarvis: 70%"
  );
}


// =========================================================
// LOOP
// =========================================================

void loop()
{
  procesarTouch();

  manejarServidor();

  actualizarECG();

  delay(20);
}