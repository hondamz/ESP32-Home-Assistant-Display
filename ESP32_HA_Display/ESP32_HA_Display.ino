/**
 * ================================================================
 *  ESP32 Home Assistant Display
 *  Gerät  : LILYGO T-Display-S3  (170 × 320 px, ST7789, parallel)
 *  Autor  : generiert 2026-05-24
 * ================================================================
 *
 *  BENÖTIGTE BIBLIOTHEKEN  (Arduino Library Manager)
 *  --------------------------------------------------
 *  • LovyanGFX   by lovyan03      (Display-Treiber, kein User_Setup.h nötig)
 *  • ArduinoJson by Benoit Blanchon
 *  Built-in: WiFi, WebServer, HTTPClient, Preferences
 *
 *  BOARD-EINSTELLUNG  (Arduino IDE → Werkzeuge)
 *  --------------------------------------------
 *  Board        : "ESP32S3 Dev Module"
 *  PSRAM        : "OPI PSRAM"
 *  Flash Size   : "16MB (128Mb)"
 *  Upload Speed : 921600
 *
 *  FEATURE-FLAG
 *  ------------
 *  #define HAS_DISPLAY  → ein Display vorhanden  (Standard: aktiv)
 *  Auskommentieren für ESP32 ohne Display.
 *
 *  HOME ASSISTANT
 *  --------------
 *  • REST-API ist ab HA-Core standardmäßig aktiviert.
 *  • Einen Long-Lived Access Token erstellen:
 *    HA → Profil (Benutzer-Icon unten links) → Sicherheit
 *    → Langlebige Zugriffstoken → Token erstellen
 *  • Token im Web-Interface des ESP eintragen.
 * ================================================================
 */

// ── FEATURE-FLAGS ────────────────────────────────────────────────────────────
#define HAS_DISPLAY          // auskommentieren → Build ohne Display

// ── BOARD-AUSWAHL ─────────────────────────────────────────────────────────────
//  Genau eine Zeile aktivieren, alle anderen auskommentieren.
//  Arduino IDE Board-Einstellung muss zum gewählten Board passen (s. README).
// -----------------------------------------------------------------------------
//#define BOARD_TDISPLAY_S3     // LILYGO T-Display-S3  │ ESP32-S3 │ 170×320 px │ 8-Bit parallel
#define BOARD_TDISPLAY_V11  // LILYGO T-Display V1.1 │ ESP32    │ 135×240 px │ SPI

#if !defined(BOARD_TDISPLAY_S3) && !defined(BOARD_TDISPLAY_V11)
  #error "Kein Board definiert – genau eine BOARD_xxx-Zeile oben aktivieren."
#endif
#if defined(BOARD_TDISPLAY_S3) && defined(BOARD_TDISPLAY_V11)
  #error "Nur ein Board gleichzeitig definieren."
#endif

// ── VERSION ──────────────────────────────────────────────────────────────────
#define APP_VERSION "1.8"

// ── INCLUDES ─────────────────────────────────────────────────────────────────
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <ArduinoOTA.h>
#include <Update.h>

#ifdef HAS_DISPLAY
  #define LGFX_USE_V1
  #include <LovyanGFX.hpp>
#endif

// Board-spezifische Display-Bezeichnung (nach Includes, PROGMEM nicht nötig auf ESP32)
#ifdef BOARD_TDISPLAY_S3
  static const char DISP_NAME[] = "ST7789 170x320px (8-Bit parallel)";
#endif
#ifdef BOARD_TDISPLAY_V11
  static const char DISP_NAME[] = "ST7789 135x240px (SPI)";
#endif

// ── TIMING ───────────────────────────────────────────────────────────────────
static constexpr uint32_t WIFI_TIMEOUT_MS  = 60000UL;  // 60 s → AP-Fallback
static constexpr uint32_t HA_POLL_MS       =  5000UL;  // Sensor-Abfrageintervall
static constexpr uint32_t WIFI_RETRY_MS    = 120000UL; // Neuverbindungsversuch

// ── VERLAUFSGRAFIK ───────────────────────────────────────────────────────────
static constexpr uint16_t HIST_MAX    = 1440;    // 24 h @ 1-Minuten-Intervall
static constexpr uint32_t HIST_INT_MS = 60000UL; // Abtastintervall 1 Minute
float    stromHist[HIST_MAX];
float    solarHist[HIST_MAX];
uint16_t histCount  = 0;
uint16_t histHead   = 0;
uint32_t lastHistMS = 0;

// ── HOME ASSISTANT ENTITY IDs ────────────────────────────────────────────────
static const char* ENT_STROM = "sensor.hlp_strom_aktueller_bezug";
static const char* ENT_AKKU  = "sensor.victron_battery_soc";
static const char* ENT_TEMP1 = "sensor.bthome_sensor_83ec_temperatur";
static const char* ENT_TEMP2 = "sensor.wohnzimmer_mz_aussenmodul_temperatur";
static const char* ENT_SOLAR = "sensor.hlp_solar_produktion_summe";

// ─────────────────────────────────────────────────────────────────────────────
//  LovyanGFX – Display-Konfiguration (board-spezifisch per #ifdef)
// ─────────────────────────────────────────────────────────────────────────────
#ifdef HAS_DISPLAY
class LGFX : public lgfx::LGFX_Device {
  lgfx::Panel_ST7789 _panel;
#ifdef BOARD_TDISPLAY_S3
  lgfx::Bus_Parallel8 _bus;
#endif
#ifdef BOARD_TDISPLAY_V11
  lgfx::Bus_SPI       _bus;
#endif
  lgfx::Light_PWM    _light;

public:
  LGFX() {

#ifdef BOARD_TDISPLAY_S3
    // ── Bus: 8-Bit parallel, LCD_CAM (ESP32-S3) ─────────────────
    {
      auto cfg       = _bus.config();
      cfg.port       = 0;
      cfg.freq_write = 20000000;   // 20 MHz – stabiler als 40 MHz
      cfg.pin_wr     = 8;
      cfg.pin_rd     = 9;
      cfg.pin_rs     = 7;          // DC / RS
      cfg.pin_d0     = 39;
      cfg.pin_d1     = 40;
      cfg.pin_d2     = 41;
      cfg.pin_d3     = 42;
      cfg.pin_d4     = 45;
      cfg.pin_d5     = 46;
      cfg.pin_d6     = 47;
      cfg.pin_d7     = 48;
      _bus.config(cfg);
      _panel.setBus(&_bus);
    }
    // ── Panel: ST7789, 170×320 ───────────────────────────────────
    {
      auto cfg             = _panel.config();
      cfg.pin_cs           = 6;
      cfg.pin_rst          = 5;
      cfg.pin_busy         = -1;
      cfg.panel_width      = 170;
      cfg.panel_height     = 320;
      cfg.memory_width     = 240;   // ST7789 Speicher-Breite
      cfg.memory_height    = 320;
      cfg.offset_x         = 35;   // (240-170)/2 = 35
      cfg.offset_y         = 0;
      cfg.offset_rotation  = 0;
      cfg.dummy_read_pixel = 8;
      cfg.dummy_read_bits  = 1;
      cfg.readable         = false;
      cfg.invert           = true;
      cfg.rgb_order        = false;
      cfg.dlen_16bit       = false;
      cfg.bus_shared       = false;
      _panel.config(cfg);
    }
    // ── Backlight: GPIO 38 ───────────────────────────────────────
    {
      auto cfg        = _light.config();
      cfg.pin_bl      = 38;
      cfg.invert      = false;
      cfg.freq        = 44100;
      cfg.pwm_channel = 7;
      _light.config(cfg);
      _panel.setLight(&_light);
    }
#endif // BOARD_TDISPLAY_S3

#ifdef BOARD_TDISPLAY_V11
    // ── Bus: SPI (VSPI, ESP32 original) ─────────────────────────
    {
      auto cfg        = _bus.config();
      cfg.spi_host    = SPI3_HOST; // VSPI – Pins 18/19
      cfg.freq_write  = 40000000;  // 40 MHz
      cfg.freq_read   = 16000000;
      cfg.pin_sclk    = 18;
      cfg.pin_mosi    = 19;
      cfg.pin_miso    = -1;
      cfg.pin_dc      = 16;
      _bus.config(cfg);
      _panel.setBus(&_bus);
    }
    // ── Panel: ST7789, 135×240 ───────────────────────────────────
    {
      auto cfg             = _panel.config();
      cfg.pin_cs           = 5;
      cfg.pin_rst          = 23;
      cfg.pin_busy         = -1;
      cfg.panel_width      = 135;
      cfg.panel_height     = 240;
      cfg.memory_width     = 240;
      cfg.memory_height    = 320;
      cfg.offset_x         = 52;
      cfg.offset_y         = 40;
      cfg.offset_rotation  = 0;
      cfg.dummy_read_pixel = 8;
      cfg.dummy_read_bits  = 1;
      cfg.readable         = false;
      cfg.invert           = true;
      cfg.rgb_order        = false;
      cfg.dlen_16bit       = false;
      cfg.bus_shared       = true;
      _panel.config(cfg);
    }
    // ── Backlight: GPIO 4 ────────────────────────────────────────
    {
      auto cfg        = _light.config();
      cfg.pin_bl      = 4;
      cfg.invert      = false;
      cfg.freq        = 44100;
      cfg.pwm_channel = 7;
      _light.config(cfg);
      _panel.setLight(&_light);
    }
#endif // BOARD_TDISPLAY_V11

    setPanel(&_panel);
  }
};

static LGFX tft;
#endif // HAS_DISPLAY

// ── GLOBALE OBJEKTE ───────────────────────────────────────────────────────────
Preferences prefs;
WebServer   server(80);

// ── KONFIGURATIONSSTRUKTUR (NVS-persistent) ───────────────────────────────────
struct Config {
  String wifiSSID  = "";
  String wifiPass  = "";
  String apSSID    = "ESPDisplay1";
  bool   dhcp      = true;
  String sIP       = "";
  String sGW       = "";
  String sSN       = "255.255.255.0";
  String sDNS      = "8.8.8.8";
  String haURL     = "http://192.168.50.35:8123";
  String haToken   = "";
  float   akku1Cap   = 0.0;  // Gesamtkapazität Akku 1 in kWh
  float   akku2Cap   = 0.0;  // Gesamtkapazität Akku 2 in kWh
  uint8_t chartHours = 6;    // Verlaufsgrafik: angezeigte Stunden (1–24)
  String  hostname   = "esp32-ha-display";  // mDNS / DHCP-Hostname
} cfg;

// ── HARDWARE-INFO (beim Start und nach Konfigurationsänderung befüllt) ────────
struct HWInfo {
  String   chip    = "";
  uint32_t flashMB = 0;    // Flash-Speicher in MB
  uint32_t ramKB   = 0;    // RAM gesamt in KB
  uint32_t freeKB  = 0;    // RAM verfügbar in KB (Snapshot)
  String   display = "";
} hw;

// ── LAUFZEIT-STATUS ───────────────────────────────────────────────────────────
struct AppState {
  bool   wifiConnected = false;
  bool   apMode        = false;
  bool   haConnected   = false;
  String ownIP         = "---";
  // Sensordaten
  String strom     = "--";  String stromUnit = "";
  String akku      = "--";  String akkuUnit  = "";
  String temp1     = "--";  String temp1Unit = "";
  String temp2     = "--";  String temp2Unit = "";
  String solar     = "--";  String solarUnit = "";
  uint32_t lastHA        = 0;
  uint32_t lastHASuccess = 0;  // nur bei vollständig erfolgreicher Abfrage
  uint32_t lastWifi      = 0;
} app;

// =============================================================================
//  NVS – laden / speichern
// =============================================================================
void loadConfig() {
  prefs.begin("ha-disp", true);
  cfg.wifiSSID = prefs.getString("ssid",    cfg.wifiSSID.c_str());
  cfg.wifiPass = prefs.getString("pass",    cfg.wifiPass.c_str());
  cfg.apSSID   = prefs.getString("apssid",  cfg.apSSID.c_str());
  cfg.dhcp     = prefs.getBool  ("dhcp",    cfg.dhcp);
  cfg.sIP      = prefs.getString("sip",     cfg.sIP.c_str());
  cfg.sGW      = prefs.getString("sgw",     cfg.sGW.c_str());
  cfg.sSN      = prefs.getString("ssn",     cfg.sSN.c_str());
  cfg.sDNS     = prefs.getString("sdns",    cfg.sDNS.c_str());
  cfg.haURL    = prefs.getString("haurl",   cfg.haURL.c_str());
  cfg.haToken  = prefs.getString("hatoken", cfg.haToken.c_str());
  cfg.akku1Cap   = prefs.getFloat("akku1cap",  0.0);
  cfg.akku2Cap   = prefs.getFloat("akku2cap",  0.0);
  cfg.chartHours = prefs.getUChar("charthrs",  6);
  cfg.hostname   = prefs.getString("hostname", "esp32-ha-display");
  prefs.end();
}

void saveConfig() {
  prefs.begin("ha-disp", false);
  prefs.putString("ssid",    cfg.wifiSSID);
  prefs.putString("pass",    cfg.wifiPass);
  prefs.putString("apssid",  cfg.apSSID);
  prefs.putBool  ("dhcp",    cfg.dhcp);
  prefs.putString("sip",     cfg.sIP);
  prefs.putString("sgw",     cfg.sGW);
  prefs.putString("ssn",     cfg.sSN);
  prefs.putString("sdns",    cfg.sDNS);
  prefs.putString("haurl",   cfg.haURL);
  prefs.putString("hatoken", cfg.haToken);
  prefs.putFloat ("akku1cap",  cfg.akku1Cap);
  prefs.putFloat ("akku2cap",  cfg.akku2Cap);
  prefs.putUChar ("charthrs",  cfg.chartHours);
  prefs.putString("hostname",  cfg.hostname);
  prefs.end();
}

// =============================================================================
//  HARDWARE-INFO abfragen
// =============================================================================
void readHWInfo() {
  hw.chip    = ESP.getChipModel();
  hw.flashMB = ESP.getFlashChipSize() / (1024UL * 1024UL);
  hw.ramKB   = ESP.getHeapSize() / 1024;
  hw.freeKB  = ESP.getFreeHeap() / 1024;
#ifdef HAS_DISPLAY
  hw.display = DISP_NAME;
#else
  hw.display = "Kein Display";
#endif
  Serial.printf("[HW] Chip=%s Flash=%uMB RAM=%uKB frei=%uKB\n",
    hw.chip.c_str(), hw.flashMB, hw.ramKB, hw.freeKB);
}

// =============================================================================
//  WLAN
// =============================================================================
void startAP() {
  WiFi.disconnect(true);
  delay(200);
  WiFi.mode(WIFI_AP);
  String apName = cfg.apSSID.length() ? cfg.apSSID : "ESPDisplay1";
  WiFi.softAP(apName.c_str());
  app.ownIP         = WiFi.softAPIP().toString();
  app.apMode        = true;
  app.wifiConnected = false;
  Serial.printf("[WiFi] AP gestartet: SSID=%s  IP=%s\n",
                apName.c_str(), app.ownIP.c_str());
}

void connectWiFi() {
  if (cfg.wifiSSID.isEmpty()) {
    Serial.println("[WiFi] Keine SSID konfiguriert → AP-Modus");
    startAP();
    return;
  }

  // Statische IP konfigurieren (wenn gewünscht)
  if (!cfg.dhcp && cfg.sIP.length() && cfg.sGW.length()) {
    IPAddress ip, gw, sn, dns;
    if (ip.fromString(cfg.sIP) && gw.fromString(cfg.sGW)) {
      sn.fromString(cfg.sSN);
      dns.fromString(cfg.sDNS);
      WiFi.config(ip, gw, sn, dns);
    }
  }

  // Reihenfolge wichtig für ESP32:
  // 1. mode() – initialisiert das Interface
  // 2. setHostname() – danach stabil im lwIP-Stack
  // 3. setAutoReconnect() – ESP wiederverbindet intern ohne neuen DHCP-Cycle
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(cfg.hostname.c_str());
  WiFi.setAutoReconnect(true);
  WiFi.begin(cfg.wifiSSID.c_str(), cfg.wifiPass.c_str());
  Serial.printf("[WiFi] Verbinde mit '%s' (hostname=%s)",
                cfg.wifiSSID.c_str(), cfg.hostname.c_str());

  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED) {
    if (millis() - t0 > WIFI_TIMEOUT_MS) {
      Serial.println("\n[WiFi] Timeout → AP-Modus");
      startAP();
      return;
    }
    delay(500);
    Serial.print(".");
  }

  app.wifiConnected = true;
  app.apMode        = false;
  app.ownIP         = WiFi.localIP().toString();
  app.lastWifi      = millis();
  Serial.printf("\n[WiFi] Verbunden  IP=%s\n", app.ownIP.c_str());
}

void checkWiFi() {
  if (app.apMode) return;

  if (WiFi.status() == WL_CONNECTED) {
    if (!app.wifiConnected) {
      // Gerade wiederverbunden (z. B. durch autoReconnect)
      app.wifiConnected = true;
      app.ownIP         = WiFi.localIP().toString();
      Serial.printf("[WiFi] Wiederverbunden  IP=%s\n", app.ownIP.c_str());
    } else {
      app.ownIP = WiFi.localIP().toString();
    }
    return;
  }

  if (app.wifiConnected) {
    Serial.println("[WiFi] Verbindung verloren – autoReconnect aktiv");
    app.wifiConnected = false;
    app.haConnected   = false;
    app.lastWifi      = millis();  // Timer ab jetzt
  }

  // Leichter Reconnect-Anstoß nach WIFI_RETRY_MS – KEIN WiFi.begin() / DHCP-Cycle.
  // WiFi.setHostname() bleibt im Stack erhalten; kein DHCP-Churn → kein Hostname-Flap.
  if (millis() - app.lastWifi > WIFI_RETRY_MS) {
    Serial.println("[WiFi] WiFi.reconnect() (leicht, kein DHCP-Neuzyklus)");
    app.lastWifi = millis();
    WiFi.reconnect();
  }
}

// =============================================================================
//  HOME ASSISTANT – Sensor abfragen
// =============================================================================
bool fetchEntity(const char* entityId, String& value, String& unit) {
  if (cfg.haToken.isEmpty()) return false;

  String url = cfg.haURL;
  if (url.endsWith("/")) url.remove(url.length() - 1);
  url += "/api/states/";
  url += entityId;

  HTTPClient http;
  http.begin(url);
  http.addHeader("Authorization", "Bearer " + cfg.haToken);
  http.addHeader("Content-Type",  "application/json");
  http.setTimeout(8000);

  int  code    = http.GET();
  bool success = false;

  if (code == HTTP_CODE_OK) {
    StaticJsonDocument<1024> doc;
    DeserializationError err = deserializeJson(doc, http.getStream());
    if (!err) {
      value   = doc["state"].as<String>();
      unit    = doc["attributes"]["unit_of_measurement"] | "";
      success = (value != "unavailable" && value != "unknown" && value != "null");
    } else {
      Serial.printf("[HA] JSON-Fehler bei %s: %s\n", entityId, err.c_str());
    }
  } else {
    Serial.printf("[HA] HTTP %d für %s\n", code, entityId);
  }

  http.end();
  return success;
}

void updateSensors() {
  if (!app.wifiConnected) return;

  bool ok  = fetchEntity(ENT_STROM, app.strom, app.stromUnit);
       ok &= fetchEntity(ENT_AKKU,  app.akku,  app.akkuUnit);
       ok &= fetchEntity(ENT_TEMP1, app.temp1, app.temp1Unit);
       ok &= fetchEntity(ENT_TEMP2, app.temp2, app.temp2Unit);

  // Solar: beste Bemühung, beeinflusst nicht den Verbindungsstatus
  fetchEntity(ENT_SOLAR, app.solar, app.solarUnit);

  app.haConnected = ok;
  app.lastHA      = millis();
  if (ok) app.lastHASuccess = millis();

  Serial.printf("[HA] Strom=%s%s | Solar=%s%s | Akku=%s%s | T1=%s%s | T2=%s%s | OK=%d\n",
    app.strom.c_str(),  app.stromUnit.c_str(),
    app.solar.c_str(),  app.solarUnit.c_str(),
    app.akku.c_str(),   app.akkuUnit.c_str(),
    app.temp1.c_str(),  app.temp1Unit.c_str(),
    app.temp2.c_str(),  app.temp2Unit.c_str(),
    (int)ok);
}

// =============================================================================
//  VERLAUFSPUFFER – Messwert pro Minute speichern
// =============================================================================
void updateHistory() {
  stromHist[histHead] = app.strom.toFloat();
  solarHist[histHead] = app.solar.toFloat();
  histHead  = (histHead + 1) % HIST_MAX;
  if (histCount < HIST_MAX) histCount++;
  lastHistMS = millis();
}

// =============================================================================
//  TFT-DISPLAY (LovyanGFX)
// =============================================================================
#ifdef HAS_DISPLAY

static const uint32_t C_BG     = 0x000000;
static const uint32_t C_WHITE  = 0xFFFFFF;
static const uint32_t C_CYAN   = 0x00FFFF;
static const uint32_t C_GRAY   = 0x999999;
static const uint32_t C_DIM    = 0x334455;
static const uint32_t C_LINE   = 0x1A1A33;
static const uint32_t C_GREEN  = 0x00E676;
static const uint32_t C_RED    = 0xFF5252;
static const uint32_t C_BLUE   = 0x2196F3;  // Akku ≥ 20 %
static const uint32_t C_YELLOW = 0xFFEB3B;  // Akku < 20 %
static const uint32_t C_ORANGE = 0xFF9800;  // Temperatur > 30 °C

void initDisplay() {
  tft.init();
  delay(50);
  tft.setColorDepth(16);
  tft.setRotation(1);           // Landscape (S3: 320×170, V1.1: 240×135)
  tft.setBrightness(220);
  tft.fillScreen(C_BG);
  delay(20);
}

// valColor: Wertfarbe (Standard C_WHITE); 80 % Textskalierung
void drawCell(int cx, int cy, const char* label, const String& val,
              const String& unit, uint32_t valColor = C_WHITE) {
  bool bigFont = (tft.height() >= 150);

  // Beschriftung (klein, grau) – Größe unverändert
  tft.setTextSize(1.0f);
  tft.setFont(&fonts::Font0);
  tft.setTextColor(C_GRAY, C_BG);
  tft.setCursor(cx + 6, cy + 4);
  tft.print(label);

  // Messwert – 80 % der Fontgröße:
  // S3 (bigFont): Font8 × 0.8 ≈ 60 px effektiv
  // V1.1:         Font6 × 0.8 ≈ 38 px effektiv
  tft.setTextSize(0.8f);
  tft.setFont(bigFont ? &fonts::Font8 : &fonts::Font6);
  tft.setTextColor(valColor, C_BG);
  tft.setCursor(cx + 4, cy + 13);
  tft.print(val);
  tft.setTextSize(1.0f);   // zurücksetzen für Einheit

  // Einheit (mittel, cyan)
  tft.setFont(&fonts::Font2);
  tft.setTextColor(C_CYAN, C_BG);
  tft.print(" ");
  tft.print(unit);
}

void drawDisplay() {
  int W       = tft.width();           // nach Rotation: S3=320, V1.1=240
  int H       = tft.height();          // nach Rotation: S3=170, V1.1=135
  bool bigFont = (H >= 150);
  int ipBarH  = bigFont ? 22 : 16;     // IP-Leiste unten
  int gridH   = H - ipBarH;            // Sensor-Grid-Höhe
  int mx      = W / 2;                 // vertikale Mittellinie
  int my      = gridH / 2;             // horizontale Mittellinie im Grid

  tft.fillScreen(C_BG);

  // Trennlinien im Grid
  tft.drawFastVLine(mx,     0,      gridH, C_LINE);
  tft.drawFastHLine(0,      my,     W,     C_LINE);
  // Trennlinie über IP-Leiste
  tft.drawFastHLine(0,      gridH,  W,     C_LINE);

  // ── Werte aufbereiten ────────────────────────────────────────────
  // Strom: ohne Nachkommastelle
  bool stromOk = (app.strom != "--" && app.strom != "unavailable");
  String stromDisp = stromOk ? String((int)round(app.strom.toFloat())) : app.strom;

  // Akku: ohne Nachkommastelle + Farbe (blau / gelb < 20 % / rot < 15 %)
  bool akkuOk = (app.akku != "--" && app.akku != "unavailable");
  float akkuPct = akkuOk ? app.akku.toFloat() : 100.0f;
  String akkuDisp = akkuOk ? String((int)round(akkuPct)) : app.akku;
  uint32_t akkuColor = (akkuPct < 15.0f) ? C_RED
                     : (akkuPct < 20.0f) ? C_YELLOW
                     :                     C_BLUE;

  // Temperaturen: orange wenn > 30 °C
  float temp1Val = app.temp1.toFloat();
  float temp2Val = app.temp2.toFloat();
  uint32_t temp1Color = (app.temp1 != "--" && temp1Val > 30.0f) ? C_ORANGE : C_WHITE;
  uint32_t temp2Color = (app.temp2 != "--" && temp2Val > 30.0f) ? C_ORANGE : C_WHITE;

  drawCell(0,        0,       "STROM BEZUG",  stromDisp, app.stromUnit, C_WHITE);
  drawCell(mx + 1,   0,       "AKKU 1",       akkuDisp,  app.akkuUnit,  akkuColor);
  drawCell(0,        my + 1,  "AUSSENTEMP 1", app.temp1, app.temp1Unit, temp1Color);
  drawCell(mx + 1,   my + 1,  "AUSSENTEMP 2", app.temp2, app.temp2Unit, temp2Color);

  // IP-Adresse in der unteren Leiste
  tft.setFont(&fonts::Font0);
  tft.setTextColor(C_GRAY, C_BG);
  int ipY = gridH + (ipBarH - 8) / 2;  // Font0 ≈ 8 px hoch – vertikal zentrieren
  tft.setCursor(6, ipY);
  tft.print(app.ownIP.c_str());
}

void showBootMessage(const String& msg) {
  tft.fillScreen(C_BG);
  tft.setFont(&fonts::Font4);
  tft.setTextColor(C_CYAN, C_BG);
  tft.setCursor(10, 45);
  tft.print("HA Display");
  tft.setFont(&fonts::Font2);
  tft.setTextColor(C_GRAY, C_BG);
  tft.setCursor(10, 85);
  tft.print(msg);
  tft.setFont(&fonts::Font0);
  tft.setTextColor(C_DIM, C_BG);
  tft.setCursor(10, 115);
  tft.print("v" APP_VERSION);
}

#endif // HAS_DISPLAY

// =============================================================================
//  WEB-SERVER – Hilfsfunktionen
// =============================================================================

static const char PAGE_CSS[] PROGMEM = R"CSS(
*{box-sizing:border-box;margin:0;padding:0}
body{background:#0f0f23;color:#dde;font-family:'Segoe UI',Roboto,Arial,sans-serif;font-size:14px}
nav{background:#1a1a6e;padding:10px 16px;display:flex;gap:20px;align-items:center}
nav b{color:#90caf9;font-size:1.1rem;margin-right:6px}
nav a{color:#ccd;text-decoration:none}
nav a:hover{color:#90caf9}
.wrap{max-width:640px;margin:18px auto;padding:0 12px}
.card{background:#1a1a38;border-radius:10px;padding:16px 18px;margin-bottom:14px;box-shadow:0 2px 8px #0006}
h2{font-size:.75rem;color:#556;text-transform:uppercase;letter-spacing:.12em;margin-bottom:12px}
.row{display:flex;justify-content:space-between;align-items:center;padding:7px 0;border-bottom:1px solid #222244}
.row:last-child{border-bottom:none}
.lbl{color:#7788aa}
.val{font-size:1.25rem;font-weight:700;font-variant-numeric:tabular-nums}
.unit{font-size:.85rem;color:#556;margin-left:3px}
.led{display:inline-block;width:11px;height:11px;border-radius:50%;vertical-align:middle;margin-right:6px}
.ok{background:#00e676}.err{background:#ff5252}.warn{background:#ff9800}
label{display:block;color:#7788aa;font-size:.82rem;margin:10px 0 3px}
input[type=text],input[type=password]{width:100%;padding:8px 10px;background:#0b0b1e;border:1px solid #333;border-radius:5px;color:#eee;font-size:.93rem}
input:focus{outline:none;border-color:#5c6bc0}
.chk{display:flex;align-items:center;gap:8px;cursor:pointer;color:#ccd;margin:8px 0;font-size:.93rem}
input[type=checkbox]{width:16px;height:16px;accent-color:#5c6bc0}
.btn{display:inline-block;margin-top:14px;padding:9px 26px;background:#00c853;color:#000;border:none;border-radius:5px;font-weight:700;font-size:.95rem;cursor:pointer}
.btn:hover{background:#69f0ae}
.note{color:#445;font-size:.78rem;margin-top:5px;line-height:1.5}
.ts{color:#3344;font-size:.78rem;margin-top:10px;text-align:right}
a.rl{color:#445;text-decoration:none}
a.rl:hover{color:#7788aa}
.ro{color:#7788aa;font-size:.88rem}
.footer{text-align:center;color:#556;font-size:.72rem;margin-top:24px;padding-bottom:16px}
.footer .copy{color:#445;margin-top:4px;font-size:.68rem}
canvas{display:block;width:100%;border-radius:4px;background:#0d0d26}
.cht-lbl{color:#7788aa;font-size:.78rem;margin:8px 0 3px}
)CSS";

void sendHeader(const char* title, bool refresh = false) {
  server.sendContent(F("<!DOCTYPE html><html lang='de'><head>"
    "<meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"));
  if (refresh) server.sendContent(F("<meta http-equiv='refresh' content='30'>"));
  server.sendContent("<title>");
  server.sendContent(title);
  server.sendContent(F("</title><style>"));
  server.sendContent_P(PAGE_CSS);
  server.sendContent(F("</style></head><body>"
    "<nav><b>&#9680; HA Display</b>"
    "<a href='/'>Dashboard</a>"
    "<a href='/status'>Status</a>"
    "<a href='/settings'>Einstellungen</a>"
    "<a href='/update'>Update</a>"
    "</nav><div class='wrap'>"));
}

void sendFooter() {
  server.sendContent(F("<div class='footer'>"
    "<p>HA Display &nbsp;v" APP_VERSION "</p>"
    "<p class='copy'>Copyright &copy; 2026 Andreas &nbsp;&bull;&nbsp; "
    "Licensed under the Apache License, Version 2.0</p>"
    "</div></div></body></html>"));
}

// =============================================================================
//  AKKU-REICHWEITE – Stunden verbleibend bei aktuellem Verbrauch
// =============================================================================
float calcAkkuRange() {
  if (cfg.akku1Cap <= 0.0f) return -1.0f;
  if (app.akku == "--" || app.akku == "unavailable") return -1.0f;
  if (app.strom == "--" || app.strom == "unavailable") return -1.0f;

  float stromW  = app.strom.toFloat();
  if (stromW <= 0.0f) return -1.0f;

  // Einheit berücksichtigen (W oder kW)
  float stromKW = (app.stromUnit == "kW") ? stromW : stromW / 1000.0f;
  if (stromKW <= 0.0f) return -1.0f;

  float remainKWh = cfg.akku1Cap * app.akku.toFloat() / 100.0f;
  return remainKWh / stromKW;
}

// =============================================================================
//  WEB-SERVER – GET /
// =============================================================================
void handleRoot() {
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/html; charset=utf-8", "");
  sendHeader("HA Display");   // kein Meta-Refresh – JS übernimmt (6 s)

  // Berechne Zeit seit letzter erfolgreicher HA-Abfrage (server-seitig)
  uint32_t agoSec = (app.lastHASuccess > 0) ? (millis() - app.lastHASuccess) / 1000 : 9999;

  // ── Kopfzeile: LED links, Datum/Uhrzeit rechts ───────────────
  server.sendContent(F("<div style='display:flex;justify-content:space-between;"
    "align-items:center;margin-bottom:10px'>"));
  server.sendContent(F("<span><span id='haLed' class='led'></span>"
    "<span id='haAgo' style='font-size:.85rem;color:#ccd'></span></span>"));
  server.sendContent(F("<span style='color:#7788aa;font-size:.82rem'>"));
  server.sendContent("<span style='color:#90caf9;font-weight:700'>" + cfg.hostname + "</span>&nbsp; ");
  server.sendContent(F("<span id='dtm'></span></span>"));
  server.sendContent(F("</div>"));

  // ── Sensordaten-Karte ────────────────────────────────────────
  server.sendContent(F("<div class='card'><h2>Sensordaten</h2>"));

  const char* labels[] = {
    "Aktueller Stromverbrauch",
    "Solar Produktion",
    "Akku 1 \xe2\x80\x93 Ladezustand",
    "Au&szlig;entemperatur 1",
    "Au&szlig;entemperatur 2"
  };
  const char* vids[]    = { "v-strom", "v-solar", "v-akku", "v-temp1", "v-temp2" };
  const char* uids[]    = { "u-strom", "u-solar", "u-akku", "u-temp1", "u-temp2" };
  const String* vals[]  = { &app.strom, &app.solar, &app.akku,  &app.temp1,  &app.temp2  };
  const String* units[] = { &app.stromUnit, &app.solarUnit, &app.akkuUnit, &app.temp1Unit, &app.temp2Unit };

  // Strom, Solar, Akku (Indizes 0–2)
  for (int i = 0; i < 3; i++) {
    server.sendContent("<div class='row'><span class='lbl'>");
    server.sendContent(labels[i]);
    server.sendContent("</span><span><span class='val' id='");
    server.sendContent(vids[i]);
    server.sendContent("'>");
    server.sendContent(*vals[i]);
    server.sendContent("</span><span class='unit' id='");
    server.sendContent(uids[i]);
    server.sendContent("'>");
    server.sendContent(*units[i]);
    server.sendContent(F("</span></span></div>"));
  }

  // Akku-Reichweite direkt nach Ladezustand
  {
    float rng = calcAkkuRange();
    String rStr  = (rng < 0) ? "--" : String(rng, 1);
    String rUnit = (rng < 0) ? ""   : "h";
    server.sendContent(F("<div class='row'><span class='lbl'>Akku 1 \xe2\x80\x93 Reichweite</span>"
      "<span><span class='val' id='v-range'>"));
    server.sendContent(rStr);
    server.sendContent(F("</span><span class='unit' id='u-range'>"));
    server.sendContent(rUnit);
    server.sendContent(F("</span></span></div>"));
  }

  // Temperaturen (Indizes 3–4)
  for (int i = 3; i < 5; i++) {
    server.sendContent("<div class='row'><span class='lbl'>");
    server.sendContent(labels[i]);
    server.sendContent("</span><span><span class='val' id='");
    server.sendContent(vids[i]);
    server.sendContent("'>");
    server.sendContent(*vals[i]);
    server.sendContent("</span><span class='unit' id='");
    server.sendContent(uids[i]);
    server.sendContent("'>");
    server.sendContent(*units[i]);
    server.sendContent(F("</span></span></div>"));
  }

  server.sendContent(F("</div>"));

  // ── Verlaufsgrafiken ─────────────────────────────────────────
  server.sendContent("<div class='card'><h2>Verlauf &ndash; letzte "
    + String(cfg.chartHours) + " Stunden</h2>");
  server.sendContent(F("<p class='cht-lbl'>Stromverbrauch</p>"
    "<canvas id='cv-strom' height='80'></canvas>"
    "<p class='cht-lbl' style='margin-top:10px'>Solar Produktion</p>"
    "<canvas id='cv-solar' height='80'></canvas>"
    "</div>"));

  // Zeitstempel
  if (app.lastHA > 0) {
    uint32_t ago = (millis() - app.lastHA) / 1000;
    server.sendContent("<p class='ts' id='ts'>Letzte Aktualisierung: vor " + String(ago)
      + " s &nbsp;&bull;&nbsp; <a class='rl' href='/'>&#8635; Neu laden</a></p>");
  } else {
    server.sendContent(F("<p class='ts' id='ts'>Noch keine Daten abgerufen</p>"));
  }

  // ── JavaScript: Uhr + HA-LED + AJAX-Poll + Verlaufsgrafiken ─
  server.sendContent("<script>var ago=");
  server.sendContent(String(agoSec));
  server.sendContent(F(
    ";(function tick(){"
    "var l=document.getElementById('haLed'),t=document.getElementById('haAgo');"
    "if(ago>=9999){l.className='led err';t.textContent='Keine HA-Daten';}"
    "else if(ago<=15){l.className='led ok';t.textContent='HA OK – vor '+ago+' s';}"
    "else if(ago<=60){l.className='led warn';t.textContent='HA Warnung – vor '+ago+' s';}"
    "else{l.className='led err';t.textContent='HA Fehler – vor '+ago+' s';}"
    "ago++;setTimeout(tick,1000);})();"
    "function clock(){var d=new Date();"
    "document.getElementById('dtm').textContent="
    "d.toLocaleDateString('de-DE')+' '+"
    "String(d.getHours()).padStart(2,'0')+':'+String(d.getMinutes()).padStart(2,'0');}"
    "clock();setInterval(clock,10000);"
    // AJAX sensor poll
    "function poll(){"
    "fetch('/api/sensors').then(function(r){return r.json();}).then(function(d){"
    "document.getElementById('v-strom').textContent=d.strom;"
    "document.getElementById('u-strom').textContent=d.strom_unit;"
    "document.getElementById('v-solar').textContent=d.solar;"
    "document.getElementById('u-solar').textContent=d.solar_unit;"
    "document.getElementById('v-akku').textContent=d.akku;"
    "document.getElementById('u-akku').textContent=d.akku_unit;"
    "document.getElementById('v-temp1').textContent=d.temp1;"
    "document.getElementById('u-temp1').textContent=d.temp1_unit;"
    "document.getElementById('v-temp2').textContent=d.temp2;"
    "document.getElementById('u-temp2').textContent=d.temp2_unit;"
    "ago=d.last_ha_success_ago;"
    "var vr=document.getElementById('v-range'),ur=document.getElementById('u-range');"
    "if(d.akku_range_h<0){if(vr)vr.textContent='–';if(ur)ur.textContent='';}"
    "else{if(vr)vr.textContent=d.akku_range_h.toFixed(1);if(ur)ur.textContent='h';}"
    "var ts=document.getElementById('ts');"
    "if(ts)ts.textContent='Letzte Aktualisierung: gerade eben';"
    "}).catch(function(){});"
    "setTimeout(poll,6000);}"
    "setTimeout(poll,6000);"
    // Chart drawing – nowMs: Date.now() beim Abruf (für Zeitmarkierungen)
    "function draw(id,data,color,unit,nowMs){"
    "var cv=document.getElementById(id);if(!cv)return;"
    "var w=cv.offsetWidth||300;cv.width=w;cv.height=80;"
    "var ctx=cv.getContext('2d');"
    "ctx.fillStyle='#0d0d26';ctx.fillRect(0,0,w,80);"
    "if(!data||data.length<2)return;"
    "var n=data.length;"
    "var mn=data[0],mx=data[0];"
    "for(var i=1;i<n;i++){if(data[i]<mn)mn=data[i];if(data[i]>mx)mx=data[i];}"
    "var rng=mx-mn;if(rng<0.01)rng=1;"
    "var pad=6,h=80-pad*2;"
    "function yx(v){return pad+h*(1-(v-mn)/rng);}"
    "function xi(i){return i*(w-1)/(n-1);}"
    // Nulllinie (pos./neg. Bereich)
    "if(mn<0&&mx>0){var y0=yx(0);"
    "ctx.setLineDash([3,3]);ctx.strokeStyle='#334';ctx.lineWidth=1;"
    "ctx.beginPath();ctx.moveTo(0,y0);ctx.lineTo(w,y0);ctx.stroke();ctx.setLineDash([]);}"
    // 30-Minuten-Markierungen mit HH:MM-Beschriftung
    "if(nowMs&&n>1){"
    "var step30=30*60*1000;"
    "var tMark=Math.floor(nowMs/step30)*step30;"
    "ctx.setLineDash([2,4]);ctx.strokeStyle='#334466';ctx.lineWidth=1;"
    "ctx.font='9px sans-serif';ctx.fillStyle='#556677';ctx.textAlign='center';"
    "while(tMark>=nowMs-(n-1)*60000){"
    "var minAgo=(nowMs-tMark)/60000;"
    "var xm=xi(n-1-minAgo);"
    "if(xm>=0&&xm<=w){"
    "ctx.beginPath();ctx.moveTo(xm,pad);ctx.lineTo(xm,80-pad-10);ctx.stroke();"
    "var dd=new Date(tMark);"
    "var lbl=String(dd.getHours()).padStart(2,'0')+':'+String(dd.getMinutes()).padStart(2,'0');"
    "ctx.fillText(lbl,xm,78);}"
    "tMark-=step30;}"
    "ctx.setLineDash([]);ctx.textAlign='left';}"
    // Füllfläche
    "ctx.beginPath();ctx.moveTo(0,yx(data[0]));"
    "for(var i=1;i<n;i++)ctx.lineTo(xi(i),yx(data[i]));"
    "ctx.lineTo(w,80);ctx.lineTo(0,80);ctx.closePath();"
    "ctx.fillStyle=color+'22';ctx.fill();"
    // Linie
    "ctx.beginPath();ctx.moveTo(0,yx(data[0]));"
    "for(var i=1;i<n;i++)ctx.lineTo(xi(i),yx(data[i]));"
    "ctx.strokeStyle=color;ctx.lineWidth=1.5;ctx.stroke();"
    // Min/Max-Labels
    "ctx.fillStyle='#7788aa';ctx.font='10px sans-serif';ctx.textAlign='left';"
    "ctx.fillText(mx.toFixed(1)+' '+unit,4,pad+10);"
    "ctx.fillText(mn.toFixed(1)+' '+unit,4,76);}"
    // Chart poll
    "function pollCharts(){"
    "fetch('/api/history').then(function(r){return r.json();}).then(function(d){"
    "var now=Date.now();"
    "draw('cv-strom',d.strom,'#90caf9',d.strom_unit,now);"
    "draw('cv-solar',d.solar,'#00e676',d.solar_unit,now);"
    "}).catch(function(){});"
    "setTimeout(pollCharts,30000);}"
    "pollCharts();"
    "</script>"));

  sendFooter();
}

// =============================================================================
//  WEB-SERVER – GET /status
// =============================================================================
void handleStatus() {
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/html; charset=utf-8", "");
  sendHeader("Status", true);

  // ── ESP-Status ───────────────────────────────────────────────
  server.sendContent(F("<div class='card'><h2>ESP Status</h2>"));

  // Netzwerk
  server.sendContent(F("<div class='row'><span class='lbl'>Netzwerk</span><span>"));
  if (app.apMode) {
    server.sendContent("<span class='led warn'></span>AP-Modus: "
      + cfg.apSSID + " &nbsp;(" + app.ownIP + ")");
  } else if (app.wifiConnected) {
    server.sendContent("<span class='led ok'></span>WLAN verbunden &nbsp;(" + app.ownIP + ")");
  } else {
    server.sendContent(F("<span class='led err'></span>Nicht verbunden"));
  }
  server.sendContent(F("</span></div>"));

  // IP-Modus
  server.sendContent(F("<div class='row'><span class='lbl'>IP-Modus</span><span>"));
  server.sendContent(cfg.dhcp ? F("DHCP") : F("Statisch"));
  server.sendContent(F("</span></div>"));

  // HA-Verbindung
  server.sendContent(F("<div class='row'><span class='lbl'>Home Assistant</span><span>"));
  if (cfg.haToken.isEmpty()) {
    server.sendContent(F("<span class='led err'></span>Kein Token konfiguriert"));
  } else if (app.haConnected) {
    server.sendContent(F("<span class='led ok'></span>Verbunden"));
  } else {
    server.sendContent(F("<span class='led err'></span>Nicht erreichbar"));
  }
  server.sendContent(F("</span></div>"));

  // HA URL
  server.sendContent("<div class='row'><span class='lbl'>HA-URL</span>"
    "<span style='font-size:.88rem'>" + cfg.haURL + "</span></div>");

  // Abfrageintervall
  server.sendContent("<div class='row'><span class='lbl'>Abfrageintervall</span>"
    "<span><span class='val' style='font-size:1rem'>" + String(HA_POLL_MS / 1000) + "</span>"
    "<span class='unit'>s</span></span></div>");

  server.sendContent(F("</div>"));  // /card ESP Status

  // ── Akku-Konfiguration & Ladestände ──────────────────────────
  server.sendContent(F("<div class='card'><h2>Akku-Status</h2>"));

  // Akku 1
  float akkuPct  = app.akku.toFloat();
  float akku1Rest = (cfg.akku1Cap > 0) ? cfg.akku1Cap * akkuPct / 100.0 : 0.0;

  server.sendContent("<div class='row'><span class='lbl'>Akku 1 – Gesamtkapazit&auml;t</span>"
    "<span><span class='val' style='font-size:1rem'>" + String(cfg.akku1Cap, 1) + "</span>"
    "<span class='unit'>kWh</span></span></div>");
  server.sendContent("<div class='row'><span class='lbl'>Akku 1 – Ladezustand</span>"
    "<span><span class='val' style='font-size:1rem'>" + app.akku + "</span>"
    "<span class='unit'>%</span></span></div>");
  server.sendContent("<div class='row'><span class='lbl'>Akku 1 – Restkapazit&auml;t</span>"
    "<span><span class='val' style='font-size:1rem'>" + String(akku1Rest, 2) + "</span>"
    "<span class='unit'>kWh</span></span></div>");

  // Akku 2 (Entität noch nicht verfügbar – Werte 0)
  server.sendContent("<div class='row'><span class='lbl'>Akku 2 – Gesamtkapazit&auml;t</span>"
    "<span><span class='val' style='font-size:1rem'>" + String(cfg.akku2Cap, 1) + "</span>"
    "<span class='unit'>kWh</span></span></div>");
  server.sendContent(F("<div class='row'><span class='lbl'>Akku 2 – Ladezustand</span>"
    "<span><span class='val' style='font-size:1rem'>0</span>"
    "<span class='unit'>%</span></span></div>"));
  server.sendContent(F("<div class='row'><span class='lbl'>Akku 2 – Restkapazit&auml;t</span>"
    "<span><span class='val' style='font-size:1rem'>0.00</span>"
    "<span class='unit'>kWh</span></span></div>"));

  server.sendContent(F("</div>"));  // /card Akku

  // ── ESP32 Hardware-Info ───────────────────────────────────────
  server.sendContent(F("<div class='card'><h2>ESP32 Hardware</h2>"));

  server.sendContent("<div class='row'><span class='lbl'>Chip-Modell</span>"
    "<span class='ro'>" + hw.chip + "</span></div>");
  server.sendContent("<div class='row'><span class='lbl'>Flash-Speicher</span>"
    "<span><span class='val' style='font-size:1rem'>" + String(hw.flashMB) + "</span>"
    "<span class='unit'>MB</span></span></div>");
  server.sendContent("<div class='row'><span class='lbl'>RAM gesamt</span>"
    "<span><span class='val' style='font-size:1rem'>" + String(hw.ramKB) + "</span>"
    "<span class='unit'>KB</span></span></div>");
  server.sendContent("<div class='row'><span class='lbl'>RAM verfügbar (beim Start)</span>"
    "<span><span class='val' style='font-size:1rem'>" + String(hw.freeKB) + "</span>"
    "<span class='unit'>KB</span></span></div>");
  server.sendContent("<div class='row'><span class='lbl'>Display</span>"
    "<span class='ro'>" + hw.display + "</span></div>");

  server.sendContent(F("</div>"));  // /card Hardware

  sendFooter();
}

// =============================================================================
//  WEB-SERVER – GET /settings
// =============================================================================
void handleSettings() {
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/html; charset=utf-8", "");
  sendHeader("Einstellungen");

  server.sendContent(F("<form method='post' action='/save'>"));

  // ── WLAN ────────────────────────────────────────────────────
  server.sendContent(F("<div class='card'><h2>WLAN</h2>"));
  server.sendContent("<label>SSID (Netzwerkname)</label>"
    "<input type='text' name='ssid' value='" + cfg.wifiSSID + "'>");
  server.sendContent(F("<label>Passwort (leer lassen = nicht ändern)</label>"
    "<input type='password' name='pass' placeholder='&#9679;&#9679;&#9679;&#9679;&#9679;&#9679;&#9679;&#9679;'>"));
  server.sendContent("<label>AP-Name (Fallback, falls kein WLAN)</label>"
    "<input type='text' name='apssid' value='" + cfg.apSSID + "'>");
  server.sendContent("<label>Hostname (DHCP / mDNS)</label>"
    "<input type='text' name='hostname' value='" + cfg.hostname + "'>");
  server.sendContent(F("<p class='note'>Wird als DHCP-Hostname und im Dashboard-Header angezeigt. "
    "Standard: esp32-ha-display</p>"));
  server.sendContent(F("</div>"));

  // ── IP-Konfiguration ────────────────────────────────────────
  server.sendContent(F("<div class='card'><h2>IP-Konfiguration</h2>"
    "<label class='chk'><input type='checkbox' id='dhcpCb' name='dhcp' value='1'"));
  server.sendContent(cfg.dhcp ? F(" checked") : F(""));
  server.sendContent(F("> DHCP (automatisch)</label>"
    "<div id='staticFields' style='"));
  server.sendContent(cfg.dhcp ? F("display:none'>") : F("display:block'>"));
  server.sendContent("<label>IP-Adresse</label><input type='text' name='sip' value='" + cfg.sIP + "'>");
  server.sendContent("<label>Gateway</label><input type='text' name='sgw' value='" + cfg.sGW + "'>");
  server.sendContent("<label>Subnetzmaske</label><input type='text' name='ssn' value='" + cfg.sSN + "'>");
  server.sendContent("<label>DNS-Server</label><input type='text' name='sdns' value='" + cfg.sDNS + "'>");
  server.sendContent(F("</div></div>"));

  // ── Home Assistant ───────────────────────────────────────────
  server.sendContent(F("<div class='card'><h2>Home Assistant</h2>"));
  server.sendContent("<label>URL (z. B. http://192.168.50.35:8123)</label>"
    "<input type='text' name='haurl' value='" + cfg.haURL + "'>");
  server.sendContent(F("<label>Long-Lived Access Token (leer lassen = nicht ändern)</label>"
    "<input type='password' name='hatoken' placeholder='&#9679;&#9679;&#9679;&#9679;&#9679; Token hier eintragen'>"));
  server.sendContent(F("<p class='note'>"
    "Token erstellen: HA &rarr; Profil (Benutzer-Icon links unten) &rarr; Sicherheit<br>"
    "&rarr; Langlebige Zugriffstoken &rarr; &bdquo;Token erstellen&ldquo;"
    "</p>"));
  // Abfrageintervall (read-only)
  server.sendContent("<div class='row' style='margin-top:12px'>"
    "<span class='lbl'>Abfrageintervall</span>"
    "<span class='ro'>" + String(HA_POLL_MS / 1000) + " s (fest)</span></div>");
  server.sendContent(F("</div>"));

  // ── Akku-Konfiguration ───────────────────────────────────────
  server.sendContent(F("<div class='card'><h2>Akku-Konfiguration</h2>"));
  server.sendContent("<label>Akku 1 – Gesamtkapazit&auml;t (kWh)</label>"
    "<input type='text' name='akku1cap' value='" + String(cfg.akku1Cap, 1) + "'>");
  server.sendContent(F("<p class='note'>Beispiel: 5.0 f&uuml;r 5 kWh. "
    "Wird f&uuml;r die Restkapazit&auml;tsberechnung unter Status verwendet.</p>"));
  server.sendContent("<label>Akku 2 – Gesamtkapazit&auml;t (kWh)</label>"
    "<input type='text' name='akku2cap' value='" + String(cfg.akku2Cap, 1) + "'>");
  server.sendContent(F("<p class='note'>Akku 2 – Sensor-Entit&auml;t wird in einer sp&auml;teren "
    "Version eingebunden. Wert wird schon jetzt gespeichert.</p>"));
  server.sendContent(F("</div>"));

  // ── Verlaufsgrafik ───────────────────────────────────────────
  server.sendContent(F("<div class='card'><h2>Verlaufsgrafik</h2>"));
  server.sendContent("<label>Angezeigte Stunden im Dashboard (1&ndash;24)</label>"
    "<input type='text' name='charthrs' value='" + String(cfg.chartHours) + "'>");
  server.sendContent(F("<p class='note'>Bestimmt, wie viele der letzten Stunden "
    "in den Verlaufsgrafiken f&uuml;r Strom und Solar angezeigt werden. "
    "Standard: 6. Werte au&szlig;erhalb von 1&ndash;24 werden auf diesen Bereich begrenzt.</p>"));
  server.sendContent(F("</div>"));

  server.sendContent(F("<button class='btn' type='submit'>&#128190; Speichern &amp; Neustart</button></form>"));

  // JS: DHCP-Checkbox schaltet statische Felder ein/aus
  server.sendContent(F("<script>"
    "document.getElementById('dhcpCb').addEventListener('change',function(){"
    "document.getElementById('staticFields').style.display=this.checked?'none':'block';});"
    "</script>"));

  sendFooter();
}

// =============================================================================
//  WEB-SERVER – POST /save
// =============================================================================
void handleSave() {
  cfg.wifiSSID = server.arg("ssid");
  if (server.arg("pass").length() > 0)
    cfg.wifiPass = server.arg("pass");

  String apssid = server.arg("apssid");
  cfg.apSSID = apssid.length() ? apssid : "ESPDisplay1";

  String hn = server.arg("hostname");
  cfg.hostname = hn.length() ? hn : "esp32-ha-display";

  cfg.dhcp = (server.arg("dhcp") == "1");
  cfg.sIP  = server.arg("sip");
  cfg.sGW  = server.arg("sgw");

  String ssn  = server.arg("ssn");
  String sdns = server.arg("sdns");
  cfg.sSN  = ssn.length()  ? ssn  : "255.255.255.0";
  cfg.sDNS = sdns.length() ? sdns : "8.8.8.8";

  cfg.haURL = server.arg("haurl");
  if (server.arg("hatoken").length() > 0)
    cfg.haToken = server.arg("hatoken");

  // Akku-Kapazitäten
  String a1 = server.arg("akku1cap");
  String a2 = server.arg("akku2cap");
  cfg.akku1Cap = a1.length() ? a1.toFloat() : 0.0;
  cfg.akku2Cap = a2.length() ? a2.toFloat() : 0.0;

  // Verlaufsgrafik-Stunden (1–24)
  String ch = server.arg("charthrs");
  if (ch.length()) {
    int v = ch.toInt();
    cfg.chartHours = (uint8_t)constrain(v, 1, 24);
  }

  saveConfig();

  // Hardware-Info nach Konfigurationsänderung aktualisieren
  readHWInfo();

  Serial.println("[WEB] Konfiguration gespeichert – Neustart");

  server.send(200, "text/html; charset=utf-8",
    F("<!DOCTYPE html><html><head><meta charset='UTF-8'>"
      "<meta http-equiv='refresh' content='4;url=/'>"
      "<style>body{background:#0f0f23;color:#dde;font-family:sans-serif;"
      "display:flex;justify-content:center;align-items:center;height:100vh;margin:0}"
      ".box{background:#1a1a38;padding:32px 44px;border-radius:12px;text-align:center}"
      "h2{color:#00e676;margin-bottom:10px} p{color:#556}</style></head><body>"
      "<div class='box'><h2>&#10003; Gespeichert</h2>"
      "<p>ESP startet neu&nbsp;&hellip;</p></div></body></html>"));
  delay(600);
  ESP.restart();
}

// =============================================================================
//  WEB-SERVER – GET /api/sensors  (JSON)
// =============================================================================
void handleApiSensors() {
  String j = "{";
  j += "\"strom\":\""      + app.strom     + "\",";
  j += "\"strom_unit\":\"" + app.stromUnit + "\",";
  j += "\"solar\":\""      + app.solar     + "\",";
  j += "\"solar_unit\":\"" + app.solarUnit + "\",";
  j += "\"akku\":\""       + app.akku      + "\",";
  j += "\"akku_unit\":\""  + app.akkuUnit  + "\",";
  j += "\"temp1\":\""      + app.temp1     + "\",";
  j += "\"temp1_unit\":\"" + app.temp1Unit + "\",";
  j += "\"temp2\":\""      + app.temp2     + "\",";
  j += "\"temp2_unit\":\"" + app.temp2Unit + "\",";
  j += "\"ha_ok\":"        + String(app.haConnected   ? "true" : "false") + ",";
  j += "\"wifi_ok\":"      + String(app.wifiConnected ? "true" : "false") + ",";
  j += "\"ap_mode\":"      + String(app.apMode        ? "true" : "false") + ",";
  j += "\"last_ha_success_ago\":" + String(app.lastHASuccess > 0 ? (millis() - app.lastHASuccess) / 1000 : 9999) + ",";
  {
    float rng = calcAkkuRange();
    j += "\"akku_range_h\":" + (rng < 0 ? String("-1") : String(rng, 1)) + ",";
  }
  j += "\"ip\":\""         + app.ownIP    + "\"";
  j += "}";
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "application/json", j);
}

// =============================================================================
//  WEB-SERVER – GET /api/history  (Verlaufsdaten als JSON)
// =============================================================================
void handleApiHistory() {
  uint16_t desired = (uint16_t)cfg.chartHours * 60u;
  uint16_t pts     = (desired < histCount) ? desired : histCount;

  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "application/json", "");

  server.sendContent(F("{\"strom\":["));
  for (uint16_t i = 0; i < pts; i++) {
    uint16_t idx = (histHead + HIST_MAX - pts + i) % HIST_MAX;
    if (i > 0) server.sendContent(",");
    server.sendContent(String(stromHist[idx], 1));
  }
  server.sendContent(F("],\"solar\":["));
  for (uint16_t i = 0; i < pts; i++) {
    uint16_t idx = (histHead + HIST_MAX - pts + i) % HIST_MAX;
    if (i > 0) server.sendContent(",");
    server.sendContent(String(solarHist[idx], 1));
  }
  server.sendContent("],\"strom_unit\":\"" + app.stromUnit + "\"");
  server.sendContent(",\"solar_unit\":\""  + app.solarUnit + "\"");
  server.sendContent(",\"count\":"  + String(pts));
  server.sendContent(",\"hours\":"  + String(cfg.chartHours) + "}");
}

// =============================================================================
//  OTA – ArduinoOTA (UDP, Port 3232) für IDE / arduino-cli --port <hostname>:3232
// =============================================================================
void setupOTA() {
  ArduinoOTA.setHostname(cfg.hostname.c_str());
  // optional: ArduinoOTA.setPassword("geheim");

  ArduinoOTA.onStart([]() {
    Serial.println("[OTA] Start");
#ifdef HAS_DISPLAY
    tft.fillScreen(C_BG);
    tft.setTextSize(1.0f);
    tft.setFont(&fonts::Font4);
    tft.setTextColor(C_CYAN, C_BG);
    tft.setCursor(10, 30);
    tft.print("OTA Update...");
#endif
  });

  ArduinoOTA.onEnd([]() {
    Serial.println("[OTA] Fertig – Neustart");
#ifdef HAS_DISPLAY
    tft.setFont(&fonts::Font2);
    tft.setTextColor(C_GREEN, C_BG);
    tft.setCursor(10, 90);
    tft.print("Neustart...");
#endif
  });

  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    uint8_t pct = (uint8_t)(progress * 100UL / total);
    Serial.printf("[OTA] %u%%\r", pct);
#ifdef HAS_DISPLAY
    int barW = (tft.width() - 20) * pct / 100;
    tft.fillRect(10, 70, barW, 10, C_CYAN);
    tft.setTextSize(1.0f);
    tft.setFont(&fonts::Font0);
    tft.setTextColor(C_WHITE, C_BG);
    tft.setCursor(10, 86);
    tft.printf("%3u%%  ", pct);
#endif
  });

  ArduinoOTA.onError([](ota_error_t err) {
    Serial.printf("[OTA] Fehler[%u]\n", err);
  });

  ArduinoOTA.begin();
  Serial.printf("[OTA] Bereit  hostname=%s  Port=3232\n", cfg.hostname.c_str());
}

// =============================================================================
//  OTA – HTTP-Upload-Seite  /update  (Browser-basiert, .bin hochladen)
// =============================================================================
void handleUpdatePage() {
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/html; charset=utf-8", "");
  sendHeader("Firmware-Update");
  server.sendContent(F(
    "<div class='card'><h2>Firmware-Update (HTTP)</h2>"
    "<p style='color:#7788aa;margin-bottom:12px'>"
    "Lade eine neue <b>.bin</b>-Datei hoch. "
    "Der ESP startet nach dem Update automatisch neu.</p>"
    "<form method='post' action='/update' enctype='multipart/form-data'>"
    "<label>Firmware-Datei (.bin)</label>"
    "<input type='file' name='firmware' accept='.bin' "
    "style='display:block;margin:8px 0 14px;color:#dde'>"
    "<button class='btn' type='submit'>&#8593; Firmware flashen</button>"
    "</form>"
    "<p class='note' style='margin-top:14px'>"
    "Die .bin-Datei liegt nach dem Kompilieren im arduino-cli Build-Cache oder kann mit<br>"
    "<code>arduino-cli compile --export-binaries ...</code> exportiert werden.</p>"
    "</div>"));
  sendFooter();
}

void handleUpdateUpload() {
  HTTPUpload& up = server.upload();
  if (up.status == UPLOAD_FILE_START) {
    Serial.printf("[OTA-HTTP] Start: %s\n", up.filename.c_str());
    if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
      Update.printError(Serial);
    }
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (Update.write(up.buf, up.currentSize) != up.currentSize) {
      Update.printError(Serial);
    }
  } else if (up.status == UPLOAD_FILE_END) {
    if (Update.end(true)) {
      Serial.printf("[OTA-HTTP] Fertig – %u Bytes\n", up.totalSize);
    } else {
      Update.printError(Serial);
    }
  }
}

void handleUpdateDone() {
  if (Update.hasError()) {
    server.send(200, "text/html; charset=utf-8",
      F("<!DOCTYPE html><html><head><meta charset='UTF-8'><style>"
        "body{background:#0f0f23;color:#dde;font-family:sans-serif;"
        "display:flex;justify-content:center;align-items:center;height:100vh;margin:0}"
        ".b{background:#1a1a38;padding:32px 44px;border-radius:12px;text-align:center}"
        "h2{color:#ff5252;margin-bottom:10px}p{color:#556}"
        "a{color:#90caf9}</style></head><body>"
        "<div class='b'><h2>&#10007; Update fehlgeschlagen</h2>"
        "<p><a href='/update'>Erneut versuchen</a></p></div></body></html>"));
  } else {
    server.send(200, "text/html; charset=utf-8",
      F("<!DOCTYPE html><html><head><meta charset='UTF-8'>"
        "<meta http-equiv='refresh' content='5;url=/'>"
        "<style>body{background:#0f0f23;color:#dde;font-family:sans-serif;"
        "display:flex;justify-content:center;align-items:center;height:100vh;margin:0}"
        ".b{background:#1a1a38;padding:32px 44px;border-radius:12px;text-align:center}"
        "h2{color:#00e676;margin-bottom:10px}p{color:#556}</style></head><body>"
        "<div class='b'><h2>&#10003; Update erfolgreich</h2>"
        "<p>ESP startet neu&nbsp;&hellip;</p></div></body></html>"));
    delay(500);
    ESP.restart();
  }
}

void setupWebServer() {
  server.on("/",            HTTP_GET,  handleRoot);
  server.on("/status",      HTTP_GET,  handleStatus);
  server.on("/settings",    HTTP_GET,  handleSettings);
  server.on("/save",        HTTP_POST, handleSave);
  server.on("/api/sensors", HTTP_GET,  handleApiSensors);
  server.on("/api/history", HTTP_GET,  handleApiHistory);
  server.on("/update",      HTTP_GET,  handleUpdatePage);
  server.on("/update",      HTTP_POST, handleUpdateDone, handleUpdateUpload);
  server.onNotFound([]() {
    server.sendHeader("Location", "/");
    server.send(302);
  });
  server.begin();
  Serial.printf("[WEB] Server gestartet – http://%s/\n", app.ownIP.c_str());
}

// =============================================================================
//  SETUP
// =============================================================================
void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\n\n=== ESP32 HA Display – Start ===");

  loadConfig();

  // Hardware-Info beim Start erfassen
  readHWInfo();

#ifdef HAS_DISPLAY
  initDisplay();
  showBootMessage("Verbinde WLAN...");
#endif

  connectWiFi();

  setupWebServer();

  if (app.wifiConnected) setupOTA();

#ifdef HAS_DISPLAY
  if (app.wifiConnected)
    showBootMessage("Lade HA-Daten...");
  else if (app.apMode)
    showBootMessage("AP-Modus: " + cfg.apSSID);
#endif

  if (app.wifiConnected) {
    updateSensors();
  }

#ifdef HAS_DISPLAY
  drawDisplay();
#endif

  Serial.printf("[BOOT] Bereit | IP=%s | AP=%d | WiFi=%d | HA=%d\n",
    app.ownIP.c_str(),
    (int)app.apMode,
    (int)app.wifiConnected,
    (int)app.haConnected);
}

// =============================================================================
//  LOOP
// =============================================================================
void loop() {
  server.handleClient();
  if (app.wifiConnected) ArduinoOTA.handle();
  checkWiFi();

  if (app.wifiConnected && (millis() - app.lastHA >= HA_POLL_MS)) {
    updateSensors();
#ifdef HAS_DISPLAY
    drawDisplay();
#endif
  }

  if (app.lastHASuccess > 0 && millis() - lastHistMS >= HIST_INT_MS) {
    updateHistory();
  }

  delay(5);
}
