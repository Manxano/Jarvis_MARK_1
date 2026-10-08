#include <Arduino.h>
#include <WiFi.h>
#include <LovyanGFX.hpp>
#include <Wire.h>

#include "AudioTools.h"
#include "AudioTools/AudioLibs/I2SCodecStream.h"


// =========================================================
// WIFI
// =========================================================

const char* ssid = "fmamanzanocamargo";
const char* password = "52815900Liz";

WiFiServer server(80);

bool servidorIniciado = false;

unsigned long ultimoIntentoWiFi = 0;
const unsigned long intervaloReconectarWiFi = 10000;


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
    cfg.pin_dc = 46;

    _bus.config(cfg);

    _panel.setBus(&_bus);

    auto pcfg = _panel.config();

    pcfg.pin_cs = 10;
    pcfg.pin_rst = -1;

    pcfg.panel_width = 240;
    pcfg.panel_height = 320;

    pcfg.memory_width = 240;
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
#define FT_REG_XH 0x03
#define FT_REG_XL 0x04
#define FT_REG_YH 0x05
#define FT_REG_YL 0x06


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
// VALORES
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
// AUDIO
// =========================================================

AudioInfo infoTX(44100, 2, 16);

I2SCodecStream audioTX(ESP32S3HosyondDisplay);

uint8_t bufferAudio[2048];


// =========================================================
// COLORES
// =========================================================

#define COLOR_FONDO      0xFFFF
#define COLOR_AZUL       0x11CB
#define COLOR_AZUL_CLARO 0xDF5E
#define COLOR_GRIS       0x6B90
#define COLOR_GRIS_CLARO 0xE73D
#define COLOR_TEXTO      0x1946
#define COLOR_BLANCO     0xFFFF
#define COLOR_VERDE      0x2BEB


// =========================================================
// WIFI
// =========================================================

void iniciarWiFi()
{
  WiFi.mode(WIFI_STA);

  WiFi.setAutoReconnect(true);
  WiFi.persistent(false);

  WiFi.begin(ssid, password);

  ultimoIntentoWiFi = millis();

  Serial.println();
  Serial.println("Iniciando WiFi...");
}


void revisarWiFi()
{
  if (WiFi.status() == WL_CONNECTED)
  {
    if (!servidorIniciado)
    {
      server.begin();

      servidorIniciado = true;

      Serial.println();
      Serial.println("WiFi conectado");

      Serial.print("IP ESP32: ");
      Serial.println(WiFi.localIP());

      Serial.println("Servidor iniciado");
      Serial.println("Sistema listo");
    }

    return;
  }


  servidorIniciado = false;


  if (
    millis() - ultimoIntentoWiFi >=
    intervaloReconectarWiFi
  )
  {
    ultimoIntentoWiFi = millis();

    Serial.println();
    Serial.println("WiFi no conectado. Reintentando...");

    WiFi.disconnect();

    delay(20);

    WiFi.begin(
      ssid,
      password
    );
  }
}


// =========================================================
// TOUCH
// =========================================================
//
// ORIENTACION HORIZONTAL
//
// display.setRotation(1)
//
// Pantalla:
// 320 x 240
//
// =========================================================

bool leerTouch(int &x, int &y)
{
  Wire.beginTransmission(FT6336_ADDR);

  Wire.write(FT_REG_STATUS);

  if (Wire.endTransmission(false) != 0)
    return false;


  uint8_t cantidad =
    Wire.requestFrom(
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


  int rawX =
    ((xh & 0x0F) << 8) |
    xl;


  int rawY =
    ((yh & 0x0F) << 8) |
    yl;


  // -------------------------------------------------------
  // MAPEO HORIZONTAL
  // -------------------------------------------------------

  x = 319 - rawY;
  y = rawX;


  x = constrain(
    x,
    0,
    319
  );


  y = constrain(
    y,
    0,
    239
  );


  return true;
}


// =========================================================
// TEXTO CENTRADO
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

  display.drawString(
    texto,
    x,
    y
  );

  display.setTextDatum(TL_DATUM);
}


// =========================================================
// BOTON ATRAS
// =========================================================

void dibujarBotonAtras()
{
  display.fillRoundRect(
    8,
    42,
    75,
    38,
    10,
    COLOR_AZUL_CLARO
  );


  display.setTextColor(
    COLOR_AZUL
  );

  display.setTextSize(1);

  display.setTextDatum(
    MC_DATUM
  );


  display.drawString(
    "<",
    23,
    61
  );


  display.drawString(
    "ATRAS",
    53,
    61
  );


  display.setTextDatum(
    TL_DATUM
  );
}


// =========================================================
// INICIO
// =========================================================

void dibujarInicio()
{
  pantallaActual = INICIO;

  display.fillScreen(
    COLOR_FONDO
  );


  display.fillRect(
    0,
    0,
    320,
    5,
    COLOR_AZUL
  );


  textoCentrado(
    "MONITOR DE SIGNOS",
    160,
    75,
    3,
    COLOR_AZUL
  );


  textoCentrado(
    "VITALES",
    160,
    110,
    3,
    COLOR_AZUL
  );


  textoCentrado(
    "Sistema de monitorizacion",
    160,
    140,
    1,
    COLOR_GRIS
  );


  display.fillRoundRect(
    110,
    170,
    100,
    45,
    12,
    COLOR_AZUL
  );


  textoCentrado(
    "INICIAR",
    160,
    193,
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

  display.fillScreen(
    COLOR_FONDO
  );


  display.fillRect(
    0,
    0,
    320,
    4,
    COLOR_AZUL
  );


  textoCentrado(
    "SIGNOS VITALES",
    160,
    25,
    2,
    COLOR_AZUL
  );


  // -------------------------------------------------------
  // CARD FC
  // -------------------------------------------------------

  display.fillRoundRect(
    10,
    50,
    145,
    80,
    12,
    COLOR_BLANCO
  );

  display.drawRoundRect(
    10,
    50,
    145,
    80,
    12,
    COLOR_GRIS_CLARO
  );


  textoCentrado(
    "FC",
    30,
    68,
    1,
    COLOR_GRIS
  );


  textoCentrado(
    String(frecuencia).c_str(),
    75,
    91,
    3,
    COLOR_AZUL
  );


  textoCentrado(
    "lpm",
    120,
    91,
    1,
    COLOR_GRIS
  );


  // -------------------------------------------------------
  // CARD SPO2
  // -------------------------------------------------------

  display.fillRoundRect(
    165,
    50,
    145,
    80,
    12,
    COLOR_BLANCO
  );

  display.drawRoundRect(
    165,
    50,
    145,
    80,
    12,
    COLOR_GRIS_CLARO
  );


  textoCentrado(
    "SpO2",
    190,
    68,
    1,
    COLOR_GRIS
  );


  textoCentrado(
    String(spo2).c_str(),
    235,
    91,
    3,
    COLOR_AZUL
  );


  textoCentrado(
    "%",
    278,
    91,
    1,
    COLOR_GRIS
  );


  // -------------------------------------------------------
  // CARD TEMPERATURA
  // -------------------------------------------------------

  display.fillRoundRect(
    10,
    145,
    145,
    80,
    12,
    COLOR_BLANCO
  );

  display.drawRoundRect(
    10,
    145,
    145,
    80,
    12,
    COLOR_GRIS_CLARO
  );


  textoCentrado(
    "TEMP",
    35,
    163,
    1,
    COLOR_GRIS
  );


  textoCentrado(
    String(temperatura, 1).c_str(),
    75,
    187,
    2,
    COLOR_AZUL
  );


  textoCentrado(
    "°C",
    118,
    187,
    1,
    COLOR_GRIS
  );


  // -------------------------------------------------------
  // CARD PRESION
  // -------------------------------------------------------

  display.fillRoundRect(
    165,
    145,
    145,
    80,
    12,
    COLOR_BLANCO
  );

  display.drawRoundRect(
    165,
    145,
    145,
    80,
    12,
    COLOR_GRIS_CLARO
  );


  textoCentrado(
    "PRESION",
    195,
    163,
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
    235,
    187,
    2,
    COLOR_AZUL
  );


  textoCentrado(
    "mmHg",
    282,
    187,
    1,
    COLOR_GRIS
  );
}


// =========================================================
// FRECUENCIA CARDIACA
// =========================================================

void dibujarFC()
{
  pantallaActual = FC;

  display.fillScreen(
    COLOR_FONDO
  );


  dibujarBotonAtras();


  textoCentrado(
    "FRECUENCIA CARDIACA",
    190,
    25,
    2,
    COLOR_AZUL
  );


  textoCentrado(
    String(frecuencia).c_str(),
    160,
    70,
    4,
    COLOR_AZUL
  );


  textoCentrado(
    "latidos por minuto",
    160,
    100,
    1,
    COLOR_GRIS
  );


  display.drawRoundRect(
    20,
    120,
    280,
    100,
    8,
    COLOR_GRIS_CLARO
  );
}


// =========================================================
// ECG
// =========================================================

void actualizarECG()
{
  if (
    pantallaActual != FC
  )
  {
    return;
  }


  display.fillRoundRect(
    21,
    121,
    278,
    98,
    7,
    COLOR_FONDO
  );


  for (
    int y = 135;
    y <= 205;
    y += 20
  )
  {
    display.drawFastHLine(
      25,
      y,
      270,
      COLOR_GRIS_CLARO
    );
  }


  static int fase = 0;


  int xAnterior = 25;
  int yAnterior = 170;


  for (
    int x = 25;
    x < 295;
    x++
  )
  {
    int posicion =
      (x + fase) % 80;


    int y = 170;


    if (posicion < 45)
    {
      y = 170;
    }

    else if (posicion < 50)
    {
      y =
        170 -
        (posicion - 45) * 7;
    }

    else if (posicion < 55)
    {
      y =
        135 +
        (posicion - 50) * 7;
    }

    else if (posicion < 59)
    {
      y =
        170 -
        (posicion - 55) * 12;
    }

    else if (posicion < 64)
    {
      y =
        122 +
        (posicion - 59) * 10;
    }

    else
    {
      y = 170;
    }


    y = constrain(
      y,
      125,
      210
    );


    if (x > 25)
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
  {
    fase = 0;
  }
}


// =========================================================
// SPO2
// =========================================================

void dibujarSpO2()
{
  pantallaActual = SPO2;

  display.fillScreen(
    COLOR_FONDO
  );


  dibujarBotonAtras();


  textoCentrado(
    "SATURACION DE OXIGENO",
    190,
    25,
    2,
    COLOR_AZUL
  );


  textoCentrado(
    String(spo2).c_str(),
    160,
    85,
    5,
    COLOR_AZUL
  );


  textoCentrado(
    "%",
    215,
    85,
    2,
    COLOR_GRIS
  );


  display.drawRoundRect(
    70,
    145,
    180,
    20,
    10,
    COLOR_GRIS_CLARO
  );


  int ancho =
    map(
      spo2,
      0,
      100,
      0,
      180
    );


  display.fillRoundRect(
    70,
    145,
    constrain(
      ancho,
      0,
      180
    ),
    20,
    10,
    COLOR_AZUL
  );


  textoCentrado(
    "Saturacion de oxigeno",
    160,
    190,
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

  display.fillScreen(
    COLOR_FONDO
  );


  dibujarBotonAtras();


  textoCentrado(
    "TEMPERATURA",
    190,
    25,
    2,
    COLOR_AZUL
  );


  textoCentrado(
    String(temperatura, 1).c_str(),
    145,
    90,
    4,
    COLOR_AZUL
  );


  textoCentrado(
    "°C",
    220,
    90,
    2,
    COLOR_GRIS
  );


  textoCentrado(
    "Temperatura corporal",
    180,
    125,
    1,
    COLOR_GRIS
  );


  // Termometro horizontal

  display.drawRoundRect(
    70,
    165,
    180,
    25,
    12,
    COLOR_GRIS_CLARO
  );


  int ancho =
    map(
      (int)(temperatura * 10),
      300,
      400,
      0,
      180
    );


  ancho =
    constrain(
      ancho,
      0,
      180
    );


  display.fillRoundRect(
    70,
    165,
    ancho,
    25,
    12,
    COLOR_AZUL
  );
}


// =========================================================
// PRESION
// =========================================================

void dibujarPresion()
{
  pantallaActual = PRESION;

  display.fillScreen(
    COLOR_FONDO
  );


  dibujarBotonAtras();


  textoCentrado(
    "PRESION ARTERIAL",
    190,
    25,
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
    160,
    80,
    4,
    COLOR_AZUL
  );


  textoCentrado(
    "mmHg",
    160,
    112,
    1,
    COLOR_GRIS
  );


  // Sistolica

  textoCentrado(
    "SISTOLICA",
    70,
    150,
    1,
    COLOR_GRIS
  );


  display.drawRoundRect(
    125,
    140,
    160,
    18,
    9,
    COLOR_GRIS_CLARO
  );


  int anchoSYS =
    map(
      sistolica,
      0,
      200,
      0,
      160
    );


  display.fillRoundRect(
    125,
    140,
    constrain(
      anchoSYS,
      0,
      160
    ),
    18,
    9,
    COLOR_AZUL
  );


  // Diastolica

  textoCentrado(
    "DIASTOLICA",
    70,
    195,
    1,
    COLOR_GRIS
  );


  display.drawRoundRect(
    125,
    185,
    160,
    18,
    9,
    COLOR_GRIS_CLARO
  );


  int anchoDIA =
    map(
      diastolica,
      0,
      150,
      0,
      160
    );


  display.fillRoundRect(
    125,
    185,
    constrain(
      anchoDIA,
      0,
      160
    ),
    18,
    9,
    COLOR_AZUL
  );
}


// =========================================================
// REDIBUJAR PANTALLA ACTUAL
// =========================================================

void redibujarPantallaActual()
{
  switch (pantallaActual)
  {
    case INICIO:
      dibujarInicio();
      break;

    case PRINCIPAL:
      dibujarPrincipal();
      break;

    case FC:
      dibujarFC();
      break;

    case SPO2:
      dibujarSpO2();
      break;

    case TEMPERATURA:
      dibujarTemperatura();
      break;

    case PRESION:
      dibujarPresion();
      break;
  }
}


// =========================================================
// TOUCH
// =========================================================

void procesarTouch()
{
  int x;
  int y;


  if (
    !leerTouch(x, y)
  )
  {
    return;
  }


  if (
    millis() - ultimoTouch <
    debounceTouch
  )
  {
    return;
  }


  ultimoTouch =
    millis();


  // -------------------------------------------------------
  // INICIO
  // -------------------------------------------------------

  if (
    pantallaActual == INICIO
  )
  {
    if (
      x >= 110 &&
      x <= 210 &&
      y >= 170 &&
      y <= 215
    )
    {
      dibujarPrincipal();
    }

    return;
  }


  // -------------------------------------------------------
  // PRINCIPAL
  // -------------------------------------------------------

  if (
    pantallaActual == PRINCIPAL
  )
  {
    // FC
    if (
      x >= 10 &&
      x <= 155 &&
      y >= 50 &&
      y <= 130
    )
    {
      dibujarFC();
      return;
    }


    // SpO2
    if (
      x >= 165 &&
      x <= 310 &&
      y >= 50 &&
      y <= 130
    )
    {
      dibujarSpO2();
      return;
    }


    // Temperatura
    if (
      x >= 10 &&
      x <= 155 &&
      y >= 145 &&
      y <= 225
    )
    {
      dibujarTemperatura();
      return;
    }


    // Presion
    if (
      x >= 165 &&
      x <= 310 &&
      y >= 145 &&
      y <= 225
    )
    {
      dibujarPresion();
      return;
    }
  }


  // -------------------------------------------------------
  // ATRAS
  // -------------------------------------------------------

  if (
    pantallaActual != PRINCIPAL &&
    x >= 5 &&
    x <= 85 &&
    y >= 39 &&
    y <= 85
  )
  {
    dibujarPrincipal();
    return;
  }
}


// =========================================================
// PAGINA WEB
// =========================================================

void paginaWeb(
  WiFiClient &client
)
{
  client.println(
    "HTTP/1.1 200 OK"
  );

  client.println(
    "Content-Type: text/html"
  );

  client.println(
    "Connection: close"
  );

  client.println();


  client.println(
    "<!DOCTYPE html>"
  );

  client.println(
    "<html>"
  );

  client.println(
    "<head>"
  );

  client.println(
    "<meta charset='UTF-8'>"
  );

  client.println(
    "<title>Monitor de Signos Vitales</title>"
  );

  client.println(
    "</head>"
  );

  client.println(
    "<body>"
  );

  client.println(
    "<h1>Monitor de Signos Vitales</h1>"
  );


  client.print(
    "<p>FC: "
  );

  client.print(
    frecuencia
  );

  client.println(
    " lpm</p>"
  );


  client.print(
    "<p>SpO2: "
  );

  client.print(
    spo2
  );

  client.println(
    " %</p>"
  );


  client.print(
    "<p>TEMP: "
  );

  client.print(
    temperatura,
    1
  );

  client.println(
    " °C</p>"
  );


  client.print(
    "<p>PRESION: "
  );

  client.print(
    sistolica
  );

  client.print(
    "/"
  );

  client.print(
    diastolica
  );

  client.println(
    " mmHg</p>"
  );


  client.println(
    "</body>"
  );

  client.println(
    "</html>"
  );
}


// =========================================================
// /VALORES
// =========================================================

void enviarValores(
  WiFiClient &client
)
{
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


  client.print(
    "FC="
  );

  client.println(
    frecuencia
  );


  client.print(
    "SpO2="
  );

  client.println(
    spo2
  );


  client.print(
    "TEMP="
  );

  client.println(
    temperatura,
    1
  );


  client.print(
    "SYS="
  );

  client.println(
    sistolica
  );


  client.print(
    "DIA="
  );

  client.println(
    diastolica
  );
}


// =========================================================
// OBTENER PARAMETRO DEL FORMULARIO
// =========================================================

String obtenerParametro(
  String body,
  String parametro
)
{
  String buscado =
    parametro + "=";


  int inicio =
    body.indexOf(
      buscado
    );


  if (
    inicio < 0
  )
  {
    return "";
  }


  inicio +=
    buscado.length();


  int fin =
    body.indexOf(
      '&',
      inicio
    );


  if (
    fin < 0
  )
  {
    fin =
      body.length();
  }


  return body.substring(
    inicio,
    fin
  );
}


// =========================================================
// /SIMULAR
// =========================================================
//
// Python enviara:
//
// FC=130
// SpO2=98
// TEMP=36.5
// SYS=120
// DIA=80
//
// Formato real:
//
// FC=130&SpO2=98&TEMP=36.5&SYS=120&DIA=80
//
// =========================================================

void manejarSimular(
  WiFiClient &client,
  int contentLength
)
{
  if (
    contentLength <= 0
  )
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


  String body = "";


  body.reserve(
    contentLength
  );


  unsigned long inicioEspera =
    millis();


  while (
    body.length() < contentLength &&
    millis() - inicioEspera < 3000
  )
  {
    if (
      client.available()
    )
    {
      char c =
        client.read();

      body += c;

      inicioEspera =
        millis();
    }
    else
    {
      delay(1);
    }
  }


  Serial.println();
  Serial.println(
    "========== SIMULACION =========="
  );

  Serial.print(
    "Datos recibidos: "
  );

  Serial.println(
    body
  );


  // -------------------------------------------------------
  // FC
  // -------------------------------------------------------

  String valorFC =
    obtenerParametro(
      body,
      "FC"
    );


  if (
    valorFC.length() > 0
  )
  {
    frecuencia =
      valorFC.toInt();
  }


  // -------------------------------------------------------
  // SPO2
  // -------------------------------------------------------

  String valorSpO2 =
    obtenerParametro(
      body,
      "SpO2"
    );


  if (
    valorSpO2.length() > 0
  )
  {
    spo2 =
      valorSpO2.toInt();
  }


  // -------------------------------------------------------
  // TEMPERATURA
  // -------------------------------------------------------

  String valorTemp =
    obtenerParametro(
      body,
      "TEMP"
    );


  if (
    valorTemp.length() > 0
  )
  {
    temperatura =
      valorTemp.toFloat();
  }


  // -------------------------------------------------------
  // SISTOLICA
  // -------------------------------------------------------

  String valorSYS =
    obtenerParametro(
      body,
      "SYS"
    );


  if (
    valorSYS.length() > 0
  )
  {
    sistolica =
      valorSYS.toInt();
  }


  // -------------------------------------------------------
  // DIASTOLICA
  // -------------------------------------------------------

  String valorDIA =
    obtenerParametro(
      body,
      "DIA"
    );


  if (
    valorDIA.length() > 0
  )
  {
    diastolica =
      valorDIA.toInt();
  }


  // -------------------------------------------------------
  // MOSTRAR RESULTADO
  // -------------------------------------------------------

  Serial.println(
    "Valores actualizados:"
  );


  Serial.print(
    "FC: "
  );

  Serial.println(
    frecuencia
  );


  Serial.print(
    "SpO2: "
  );

  Serial.println(
    spo2
  );


  Serial.print(
    "TEMP: "
  );

  Serial.println(
    temperatura,
    1
  );


  Serial.print(
    "SYS: "
  );

  Serial.println(
    sistolica
  );


  Serial.print(
    "DIA: "
  );

  Serial.println(
    diastolica
  );


  Serial.println(
    "================================"
  );


  // -------------------------------------------------------
  // ACTUALIZAR PANTALLA
  // -------------------------------------------------------

  redibujarPantallaActual();


  // -------------------------------------------------------
  // RESPUESTA HTTP
  // -------------------------------------------------------

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
}


// =========================================================
// /PLAY
// =========================================================

void manejarPlay(
  WiFiClient &client,
  int contentLength
)
{
  if (
    contentLength <= 0
  )
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
  Serial.println(
    "=============================="
  );

  Serial.println(
    "       JARVIS AUDIO"
  );

  Serial.println(
    "=============================="
  );


  Serial.print(
    "Audio recibido: "
  );

  Serial.print(
    contentLength
  );

  Serial.println(
    " bytes"
  );


  // -------------------------------------------------------
  // AUDIO
  // -------------------------------------------------------

  auto configTX =
    audioTX.defaultConfig(
      TX_MODE
    );


  configTX.copyFrom(
    infoTX
  );


  audioTX.begin(
    configTX
  );


  audioTX.setVolume(
    0.70f
  );


  Serial.println(
    "Audio configurado:"
  );

  Serial.println(
    "44100 Hz | 2 canales | 16 bits"
  );


  // -------------------------------------------------------
  // RECIBIR AUDIO
  // -------------------------------------------------------

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


    if (
      disponibles > 0
    )
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


      if (
        recibidos > 0
      )
      {
        audioTX.write(
          bufferAudio,
          recibidos
        );


        restantes -=
          recibidos;


        ultimoDato =
          millis();
      }
    }

    else
    {
      if (
        millis() - ultimoDato >
        5000
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
// SERVIDOR HTTP
// =========================================================

void manejarServidor()
{
  if (
    !servidorIniciado ||
    WiFi.status() != WL_CONNECTED
  )
  {
    return;
  }


  WiFiClient client =
    server.available();


  if (!client)
  {
    return;
  }


  unsigned long inicio =
    millis();


  while (
    !client.available() &&
    millis() - inicio < 100
  )
  {
    delay(1);
  }


  if (
    !client.available()
  )
  {
    client.stop();

    return;
  }


  String requestLine =
    client.readStringUntil(
      '\r'
    );


  client.readStringUntil(
    '\n'
  );


  int contentLength = 0;


  // -------------------------------------------------------
  // LEER HEADERS
  // -------------------------------------------------------

  while (
    client.connected()
  )
  {
    String linea =
      client.readStringUntil(
        '\n'
      );


    linea.trim();


    if (
      linea.length() == 0
    )
    {
      break;
    }


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


  // -------------------------------------------------------
  // GET /valores
  // -------------------------------------------------------

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


  // -------------------------------------------------------
  // POST /simular
  // -------------------------------------------------------

  if (
    requestLine.startsWith(
      "POST /simular"
    )
  )
  {
    manejarSimular(
      client,
      contentLength
    );

    delay(5);

    client.stop();

    return;
  }


  // -------------------------------------------------------
  // POST /play
  // -------------------------------------------------------

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


  // -------------------------------------------------------
  // GET /
  // -------------------------------------------------------

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


  // -------------------------------------------------------
  // 404
  // -------------------------------------------------------

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
  // -------------------------------------------------------
  // SERIAL
  // -------------------------------------------------------

  Serial.begin(
    115200
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


  // =======================================================
  // ORIENTACION HORIZONTAL
  // =======================================================

  display.setRotation(
    1
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
  // INTERFAZ INICIAL
  // -------------------------------------------------------

  dibujarInicio();


  // -------------------------------------------------------
  // WIFI
  // -------------------------------------------------------

  iniciarWiFi();


  // -------------------------------------------------------
  // ESPERAR MAXIMO 5 SEGUNDOS
  // -------------------------------------------------------
  //
  // La pantalla ya esta funcionando.
  // Si WiFi falla, NO bloquea el monitor.
  //
  // -------------------------------------------------------

  unsigned long inicioWiFi =
    millis();


  while (
    WiFi.status() != WL_CONNECTED &&
    millis() - inicioWiFi < 5000
  )
  {
    delay(100);
  }


  if (
    WiFi.status() == WL_CONNECTED
  )
  {
    server.begin();

    servidorIniciado = true;


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


    Serial.println(
      "Servidor iniciado"
    );
  }

  else
  {
    Serial.println();
    Serial.println(
      "WiFi no disponible."
    );

    Serial.println(
      "La pantalla continuara funcionando."
    );

    Serial.println(
      "Se intentara reconectar automaticamente."
    );
  }


  // -------------------------------------------------------
  // SISTEMA
  // -------------------------------------------------------

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
  // Touch
  procesarTouch();


  // WiFi
  revisarWiFi();


  // Servidor
  manejarServidor();


  // ECG
  actualizarECG();


  delay(20);
}