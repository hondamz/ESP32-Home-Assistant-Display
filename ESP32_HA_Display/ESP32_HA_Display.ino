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

// ── VERSION ──────────────────────────────────────────────────────────────────
#define APP_VERSION "1.3"

// ── INCLUDES ─────────────────────────────────────────────────────────────────
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>

#ifdef HAS_DISPLAY
  #define LGFX_USE_V1
  #include <LovyanGFX.hpp>
#endif

// ── TIMING ───────────────────────────────────────────────────────────────────
static constexpr uint32_t WIFI_TIMEOUT_MS  = 60000UL;  // 60 s → AP-Fallback
static constexpr uint32_t HA_POLL_MS       =  5000UL;  // Sensor-Abfrageintervall
static constexpr uint32_t WIFI_RETRY_MS    = 120000UL; // Neuverbindungsversuch

// ── HOME ASSISTANT ENTITY IDs ────────────────────────────────────────────────
static const char* ENT_STROM = "sensor.hlp_strom_aktueller_bezug";
static const char* ENT_AKKU  = "sensor.victron_battery_soc";
static const char* ENT_TEMP1 = "sensor.bthome_sensor_83ec_temperatur";
static const char* ENT_TEMP2 = "sensor.wohnzimmer_mz_aussenmodul_temperatur";
static const char* ENT_SOLAR = "sensor.hlp_solar_produktion_summe";

// ─────────────────────────────────────────────────────────────────────────────
//  LovyanGFX – Display-Konfiguration für LILYGO T-Display-S3
//  (ST7789, 8-Bit parallel, 170 × 320 px)
// ─────────────────────────────────────────────────────────────────────────────
#ifdef HAS_DISPLAY
class LGFX : public lgfx::LGFX_Device {
  lgfx::Panel_ST7789  _panel;
  lgfx::Bus_Parallel8 _bus;
  lgfx::Light_PWM     _light;

public:
  LGFX() {
    // ── Bus-Konfiguration (8-Bit parallel, LCD_CAM) ──────────────
    {
      auto cfg        = _bus.config();
      cfg.port        = 0;
      cfg.freq_write  = 20000000;  // 20 MHz – stabiler als 40 MHz
      cfg.pin_wr      = 8;         // WR
      cfg.pin_rd      = 9;         // RD
      cfg.pin_rs      = 7;         // DC / RS
      cfg.pin_d0      = 39;
      cfg.pin_d1      = 40;
      cfg.pin_d2      = 41;
      cfg.pin_d3      = 42;
      cfg.pin_d4      = 45;
      cfg.pin_d5      = 46;
      cfg.pin_d6      = 47;
      cfg.pin_d7      = 48;
      _bus.config(cfg);
      _panel.setBus(&_bus);
    }
    // ── Panel-Konfiguration ──────────────────────────────────────
    {
      auto cfg              = _panel.config();
      cfg.pin_cs            = 6;
      cfg.pin_rst           = 5;
      cfg.pin_busy          = -1;
      cfg.panel_width       = 170;   // physische Breite
      cfg.panel_height      = 320;   // physische Höhe
      cfg.memory_width      = 240;   // ST7789 Speicherbreite (wichtig!)
      cfg.memory_height     = 320;
      cfg.offset_x          = 35;    // Versatz: (240-170)/2 = 35
      cfg.offset_y          = 0;
      cfg.offset_rotation   = 0;
      cfg.dummy_read_pixel  = 8;
      cfg.dummy_read_bits   = 1;
      cfg.readable          = false;
      cfg.invert            = true;  // T-Display-S3 benötigt Farbinversion
      cfg.rgb_order         = false;
      cfg.dlen_16bit        = false;
      cfg.bus_shared        = false;
      _panel.config(cfg);
    }
    // ── Hintergrundbeleuchtung ───────────────────────────────────
    {
      auto cfg        = _light.config();
      cfg.pin_bl      = 38;
      cfg.invert      = false;
      cfg.freq        = 44100;
      cfg.pwm_channel = 7;
      _light.config(cfg);
      _panel.setLight(&_light);
    }
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
  float  akku1Cap  = 0.0;   // Gesamtkapazität Akku 1 in kWh
  float  akku2Cap  = 0.0;   // Gesamtkapazität Akku 2 in kWh
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
  cfg.akku1Cap = prefs.getFloat ("akku1cap", 0.0);
  cfg.akku2Cap = prefs.getFloat ("akku2cap", 0.0);
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
  prefs.putFloat ("akku1cap", cfg.akku1Cap);
  prefs.putFloat ("akku2cap", cfg.akku2Cap);
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
  hw.display = "ST7789 170x320px (8-Bit parallel)";
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

  WiFi.mode(WIFI_STA);
  WiFi.begin(cfg.wifiSSID.c_str(), cfg.wifiPass.c_str());
  Serial.printf("[WiFi] Verbinde mit '%s'", cfg.wifiSSID.c_str());

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
    app.wifiConnected = true;
    app.ownIP         = WiFi.localIP().toString();
    return;
  }
  if (app.wifiConnected) {
    Serial.println("[WiFi] Verbindung verloren");
    app.wifiConnected = false;
    app.haConnected   = false;
  }
  if (millis() - app.lastWifi > WIFI_RETRY_MS) {
    Serial.println("[WiFi] Neuverbindungsversuch...");
    app.lastWifi = millis();
    connectWiFi();
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
//  TFT-DISPLAY (LovyanGFX)
// =============================================================================
#ifdef HAS_DISPLAY

static const uint32_t C_BG    = 0x000000;
static const uint32_t C_WHITE = 0xFFFFFF;
static const uint32_t C_CYAN  = 0x00FFFF;
static const uint32_t C_GRAY  = 0x999999;
static const uint32_t C_DIM   = 0x334455;
static const uint32_t C_LINE  = 0x1A1A33;
static const uint32_t C_GREEN = 0x00E676;
static const uint32_t C_RED   = 0xFF5252;

void initDisplay() {
  tft.init();
  delay(50);
  tft.setColorDepth(16);
  tft.setRotation(1);           // Landscape: 320 × 170
  tft.setBrightness(220);
  tft.fillScreen(C_BG);
  delay(20);
}

void drawCell(int cx, int cy, const char* label, const String& val, const String& unit) {
  tft.setFont(&fonts::Font0);
  tft.setTextColor(C_GRAY, C_BG);
  tft.setCursor(cx + 6, cy + 6);
  tft.print(label);

  tft.setFont(&fonts::Font4);
  tft.setTextColor(C_WHITE, C_BG);
  tft.setCursor(cx + 6, cy + 30);
  tft.print(val);

  tft.setFont(&fonts::Font2);
  tft.setTextColor(C_CYAN, C_BG);
  tft.print(" ");
  tft.print(unit);
}

void drawDisplay() {
  tft.fillScreen(C_BG);
  tft.drawFastVLine(160, 0,  170, C_LINE);
  tft.drawFastHLine(0,   85, 320, C_LINE);
  drawCell(  0,  0, "STROM BEZUG",  app.strom, app.stromUnit);
  drawCell(161,  0, "AKKU 1",       app.akku,  app.akkuUnit);
  drawCell(  0, 86, "AUSSENTEMP 1", app.temp1, app.temp1Unit);
  drawCell(161, 86, "AUSSENTEMP 2", app.temp2, app.temp2Unit);
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
  server.sendContent(F("<span id='dtm' style='color:#7788aa;font-size:.82rem'></span>"));
  server.sendContent(F("</div>"));

  // ── Sensordaten-Karte ────────────────────────────────────────
  server.sendContent(F("<div class='card'><h2>Sensordaten</h2>"));

  const char* labels[] = {
    "Aktueller Stromverbrauch",
    "Solar Produktion",
    "Akku 1 – Ladezustand",
    "Au&szlig;entemperatur 1",
    "Au&szlig;entemperatur 2"
  };
  const String* vals[]  = { &app.strom, &app.solar, &app.akku,  &app.temp1,  &app.temp2  };
  const String* units[] = { &app.stromUnit, &app.solarUnit, &app.akkuUnit, &app.temp1Unit, &app.temp2Unit };

  for (int i = 0; i < 5; i++) {
    server.sendContent("<div class='row'><span class='lbl'>");
    server.sendContent(labels[i]);
    server.sendContent("</span><span><span class='val'>");
    server.sendContent(*vals[i]);
    server.sendContent("</span><span class='unit'>");
    server.sendContent(*units[i]);
    server.sendContent(F("</span></span></div>"));
  }
  server.sendContent(F("</div>"));

  // Zeitstempel
  if (app.lastHA > 0) {
    uint32_t ago = (millis() - app.lastHA) / 1000;
    server.sendContent("<p class='ts'>Letzte Aktualisierung: vor " + String(ago)
      + " s &nbsp;&bull;&nbsp; <a class='rl' href='/'>&#8635; Neu laden</a></p>");
  } else {
    server.sendContent(F("<p class='ts'>Noch keine Daten abgerufen</p>"));
  }

  // ── JavaScript: Uhr + HA-Status-LED + Auto-Reload ───────────
  server.sendContent("<script>var ago=");
  server.sendContent(String(agoSec));
  server.sendContent(F(";(function tick(){"
    "var l=document.getElementById('haLed'),"
    "t=document.getElementById('haAgo');"
    "if(ago>=9999){l.className='led err';t.textContent='Keine HA-Daten';}"
    "else if(ago<=15){l.className='led ok';t.textContent='HA OK – vor '+ago+' s';}"
    "else if(ago<=60){l.className='led warn';t.textContent='HA Warnung – vor '+ago+' s';}"
    "else{l.className='led err';t.textContent='HA Fehler – vor '+ago+' s';}"
    "ago++;setTimeout(tick,1000);})();"
    "function clock(){"
    "var d=new Date();"
    "document.getElementById('dtm').textContent="
    "d.toLocaleDateString('de-DE')+' '+"
    "String(d.getHours()).padStart(2,'0')+':'+String(d.getMinutes()).padStart(2,'0');}"
    "clock();setInterval(clock,10000);"
    "setTimeout(function(){location.reload();},6000);</script>"));

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
  j += "\"ip\":\""         + app.ownIP    + "\"";
  j += "}";
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "application/json", j);
}

void setupWebServer() {
  server.on("/",            HTTP_GET,  handleRoot);
  server.on("/status",      HTTP_GET,  handleStatus);
  server.on("/settings",    HTTP_GET,  handleSettings);
  server.on("/save",        HTTP_POST, handleSave);
  server.on("/api/sensors", HTTP_GET,  handleApiSensors);
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
  checkWiFi();

  if (app.wifiConnected && (millis() - app.lastHA >= HA_POLL_MS)) {
    updateSensors();
#ifdef HAS_DISPLAY
    drawDisplay();
#endif
  }

  delay(5);
}
