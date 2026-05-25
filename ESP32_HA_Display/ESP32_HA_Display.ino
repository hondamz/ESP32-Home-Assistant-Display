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
#define APP_VERSION "1.1"

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
static constexpr uint32_t HA_POLL_MS       = 30000UL;  // Sensor-Abfrageintervall
static constexpr uint32_t WIFI_RETRY_MS    = 120000UL; // Neuverbindungsversuch

// ── HOME ASSISTANT ENTITY IDs ────────────────────────────────────────────────
static const char* ENT_STROM = "sensor.hlp_strom_aktueller_bezug";
static const char* ENT_AKKU  = "sensor.victron_battery_soc";
static const char* ENT_TEMP1 = "sensor.bthome_sensor_83ec_temperatur";
static const char* ENT_TEMP2 = "sensor.wohnzimmer_mz_aussenmodul_temperatur";

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
} cfg;

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
  uint32_t lastHA   = 0;
  uint32_t lastWifi = 0;
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
  prefs.end();
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
  if (app.apMode) return;  // Im AP-Modus keine automatischen Reconnects
  if (WiFi.status() == WL_CONNECTED) {
    app.wifiConnected = true;
    app.ownIP         = WiFi.localIP().toString();
    return;
  }
  // Verbindung verloren
  if (app.wifiConnected) {
    Serial.println("[WiFi] Verbindung verloren");
    app.wifiConnected = false;
    app.haConnected   = false;
  }
  // Neuverbindungsversuch nach Wartezeit
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
  if (cfg.haToken.isEmpty()) {
    return false;
  }

  String url = cfg.haURL;
  // Trailing slash entfernen
  if (url.endsWith("/")) url.remove(url.length() - 1);
  url += "/api/states/";
  url += entityId;

  HTTPClient http;
  http.begin(url);
  http.addHeader("Authorization", "Bearer " + cfg.haToken);
  http.addHeader("Content-Type",  "application/json");
  http.setTimeout(8000);

  int code    = http.GET();
  bool success = false;

  if (code == HTTP_CODE_OK) {
    StaticJsonDocument<1024> doc;
    DeserializationError err = deserializeJson(doc, http.getStream());
    if (!err) {
      value   = doc["state"].as<String>();
      unit    = doc["attributes"]["unit_of_measurement"] | "";
      // "unavailable" / "unknown" als Fehler werten
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

  app.haConnected = ok;
  app.lastHA      = millis();

  Serial.printf("[HA] Strom=%s%s | Akku=%s%s | T1=%s%s | T2=%s%s | OK=%d\n",
    app.strom.c_str(),  app.stromUnit.c_str(),
    app.akku.c_str(),   app.akkuUnit.c_str(),
    app.temp1.c_str(),  app.temp1Unit.c_str(),
    app.temp2.c_str(),  app.temp2Unit.c_str(),
    (int)ok);
}

// =============================================================================
//  TFT-DISPLAY (LovyanGFX)
// =============================================================================
#ifdef HAS_DISPLAY

// ── Farbpalette ─────────────────────────────────────────────────────────────
static const uint32_t C_BG    = 0x000000;
static const uint32_t C_WHITE = 0xFFFFFF;
static const uint32_t C_CYAN  = 0x00FFFF;
static const uint32_t C_GRAY  = 0x999999;
static const uint32_t C_DIM   = 0x334455;
static const uint32_t C_LINE  = 0x1A1A33;
static const uint32_t C_GREEN = 0x00E676;
static const uint32_t C_RED   = 0xFF5252;

// ── Display initialisieren ───────────────────────────────────────────────────
void initDisplay() {
  tft.init();
  delay(50);                    // kurze Pause nach Init
  tft.setColorDepth(16);        // explizit 16-Bit Farbe setzen
  tft.setRotation(1);           // Landscape: 320 × 170
  tft.setBrightness(220);
  tft.fillScreen(C_BG);
  delay(20);
}

// ── Hilfsfunktion: eine Zelle im 2×2-Grid zeichnen ─────────────────────────
//  cx/cy = Ecke oben links der Zelle, w=160, h=85
void drawCell(int cx, int cy, const char* label, const String& val, const String& unit) {
  // Label (klein, oben)
  tft.setFont(&fonts::Font0);   // 6×8 px
  tft.setTextColor(C_GRAY, C_BG);
  tft.setCursor(cx + 6, cy + 6);
  tft.print(label);

  // Wert (groß, Mitte)
  tft.setFont(&fonts::Font4);   // ~26 px
  tft.setTextColor(C_WHITE, C_BG);
  tft.setCursor(cx + 6, cy + 30);
  tft.print(val);

  // Einheit (mittelgroß, hinter dem Wert)
  tft.setFont(&fonts::Font2);   // ~16 px
  tft.setTextColor(C_CYAN, C_BG);
  tft.print(" ");
  tft.print(unit);
}

// ── Hauptanzeige: 2×2-Grid mit reinen Sensorwerten ─────────────────────────
//
//   +------------------+------------------+
//   |  STROM BEZUG     |  AKKU 1          |
//   |   1234 W         |   85 %           |
//   +------------------+------------------+
//   |  AUSSENTEMP 1    |  AUSSENTEMP 2    |
//   |   18.5 °C        |   17.2 °C        |
//   +------------------+------------------+
//
void drawDisplay() {
  tft.fillScreen(C_BG);

  // Trennlinien
  tft.drawFastVLine(160, 0,  170, C_LINE);
  tft.drawFastHLine(0,   85, 320, C_LINE);

  // 4 Zellen befüllen
  drawCell(  0,  0, "STROM BEZUG",  app.strom, app.stromUnit);
  drawCell(161,  0, "AKKU 1",       app.akku,  app.akkuUnit);
  drawCell(  0, 86, "AUSSENTEMP 1", app.temp1, app.temp1Unit);
  drawCell(161, 86, "AUSSENTEMP 2", app.temp2, app.temp2Unit);
}

// ── Boot-Screen ─────────────────────────────────────────────────────────────
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
    "<a href='/settings'>Einstellungen</a>"
    "</nav><div class='wrap'>"));
}

void sendFooter() {
  server.sendContent(F("<p style='text-align:center;color:#2a2a4a;font-size:.72rem;"
    "margin-top:24px;padding-bottom:10px'>HA Display &nbsp;v" APP_VERSION "</p>"
    "</div></body></html>"));
}

// =============================================================================
//  WEB-SERVER – GET /
// =============================================================================
void handleRoot() {
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/html; charset=utf-8", "");
  sendHeader("HA Display", true);

  // ── Status-Karte ────────────────────────────────────────────
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

  // Modus
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

  server.sendContent(F("</div>"));  // /card Status

  // ── Sensordaten-Karte ────────────────────────────────────────
  server.sendContent(F("<div class='card'><h2>Sensordaten</h2>"));

  const char* labels[] = {
    "Aktueller Stromverbrauch",
    "Akku 1 – Ladezustand",
    "Au&szlig;entemperatur 1",
    "Au&szlig;entemperatur 2"
  };
  const String* vals[]  = { &app.strom,  &app.akku,  &app.temp1,  &app.temp2  };
  const String* units[] = { &app.stromUnit, &app.akkuUnit, &app.temp1Unit, &app.temp2Unit };

  for (int i = 0; i < 4; i++) {
    server.sendContent("<div class='row'><span class='lbl'>");
    server.sendContent(labels[i]);
    server.sendContent("</span><span><span class='val'>");
    server.sendContent(*vals[i]);
    server.sendContent("</span><span class='unit'>");
    server.sendContent(*units[i]);
    server.sendContent(F("</span></span></div>"));
  }
  server.sendContent(F("</div>"));  // /card Sensordaten

  // Zeitstempel
  if (app.lastHA > 0) {
    uint32_t ago = (millis() - app.lastHA) / 1000;
    server.sendContent("<p class='ts'>Letzte Aktualisierung: vor " + String(ago)
      + " s &nbsp;&bull;&nbsp; <a class='rl' href='/'>&#8635; Neu laden</a></p>");
  } else {
    server.sendContent(F("<p class='ts'>Noch keine Daten abgerufen</p>"));
  }

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
    "</p></div>"));

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

  saveConfig();
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
  j += "\"akku\":\""       + app.akku      + "\",";
  j += "\"akku_unit\":\""  + app.akkuUnit  + "\",";
  j += "\"temp1\":\""      + app.temp1     + "\",";
  j += "\"temp1_unit\":\"" + app.temp1Unit + "\",";
  j += "\"temp2\":\""      + app.temp2     + "\",";
  j += "\"temp2_unit\":\"" + app.temp2Unit + "\",";
  j += "\"ha_ok\":"        + String(app.haConnected   ? "true" : "false") + ",";
  j += "\"wifi_ok\":"      + String(app.wifiConnected ? "true" : "false") + ",";
  j += "\"ap_mode\":"      + String(app.apMode        ? "true" : "false") + ",";
  j += "\"ip\":\""         + app.ownIP    + "\"";
  j += "}";
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "application/json", j);
}

void setupWebServer() {
  server.on("/",            HTTP_GET,  handleRoot);
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

  // Regelmäßige HA-Abfrage (nur wenn WLAN verbunden)
  if (app.wifiConnected && (millis() - app.lastHA >= HA_POLL_MS)) {
    updateSensors();
#ifdef HAS_DISPLAY
    drawDisplay();
#endif
  }

  delay(5);
}
