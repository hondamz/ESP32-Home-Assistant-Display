# ESP32 Home Assistant Display

**Version 1.1**

ESP32-basiertes Display-System für [Home Assistant](https://www.home-assistant.io/), entwickelt für das **LILYGO T-Display-S3** (ST7789 LCD, 170×320 px). Zeigt Sensordaten aus Home Assistant live auf dem integrierten Display an und hostet gleichzeitig ein Web-Interface zur Datenanzeige und Gerätekonfiguration.

---

## Features

- **Live-Sensordaten** vom Home Assistant REST-API (alle 30 Sekunden)
- **TFT-Display** mit 2×2-Grid-Layout – übersichtliche Darstellung der 4 Messwerte
- **Web-Interface** (Port 80) – erreichbar im Browser ohne Login:
  - Dashboard mit Sensorwerten und ESP-Status
  - Einstellungsseite für WLAN, IP-Konfiguration und Home Assistant
- **DHCP mit AP-Fallback** – kein WLAN erreichbar? Der ESP öffnet nach 60 s ein eigenes WLAN (`ESPDisplay1`) zur Erstkonfiguration
- **Persistente Konfiguration** – alle Einstellungen bleiben nach Neustart erhalten (NVS/Flash)
- **Display-lOS Build** – per `#define HAS_DISPLAY` lässt sich der Code auch auf ESP32-Boards ohne Display einsetzen

---

## Angezeigte Sensoren

| Anzeige | Home Assistant Entity |
|---|---|
| Aktueller Stromverbrauch | `sensor.hlp_strom_aktueller_bezug` |
| Akku 1 – Ladezustand | `sensor.victron_battery_soc` |
| Außentemperatur 1 | `sensor.bthome_sensor_83ec_temperatur` |
| Außentemperatur 2 | `sensor.wohnzimmer_mz_aussenmodul_temperatur` |

---

## Hardware

| Merkmal | Wert |
|---|---|
| Board | LILYGO T-Display-S3 |
| MCU | ESP32-S3 |
| Display | ST7789, 1,9", 170×320 px, 8-Bit parallel |
| PSRAM | 8 MB OPI |
| Flash | 16 MB |

### Display-Pin-Belegung (intern, fest verdrahtet)

| Funktion | GPIO |
|---|---|
| WR | 8 |
| RD | 9 |
| DC/RS | 7 |
| CS | 6 |
| RST | 5 |
| Backlight | 38 |
| D0–D7 | 39–42, 45–48 |

---

## Benötigte Bibliotheken

Im Arduino Library Manager installieren:

| Bibliothek | Autor |
|---|---|
| **LovyanGFX** | lovyan03 |
| **ArduinoJson** | Benoit Blanchon |

Built-in (kein Install nötig): `WiFi`, `WebServer`, `HTTPClient`, `Preferences`

---

## Arduino IDE – Board-Einstellungen

| Einstellung | Wert |
|---|---|
| Board | `ESP32S3 Dev Module` |
| PSRAM | `OPI PSRAM` |
| Flash Size | `16MB (128Mb)` |
| Upload Speed | `921600` |

---

## Home Assistant – Einrichtung

Die Home Assistant **REST-API ist standardmäßig aktiv** – keine Änderung an `configuration.yaml` nötig.

Einzig erforderlich: ein **Long-Lived Access Token**

1. In Home Assistant einloggen
2. Benutzer-Icon (unten links) → **Profil**
3. Reiter **Sicherheit**
4. Abschnitt *Langlebige Zugriffstoken* → **Token erstellen**
5. Einen Namen vergeben (z. B. `ESP32-Display`), Token kopieren
6. Im Web-Interface des ESP unter **Einstellungen → Home Assistant** eintragen

---

## Erstinbetriebnahme

1. Sketch auf den ESP32 flashen
2. ESP startet im **AP-Modus** (kein WLAN konfiguriert):
   - WLAN-Name: `ESPDisplay1`
   - Browser öffnen: `http://192.168.4.1/settings`
3. WLAN-Daten, Home-Assistant-URL und Token eintragen → **Speichern**
4. ESP verbindet sich mit dem Heimnetz und zeigt sofort die HA-Sensorwerte an

---

## Web-Interface

Nach der Einrichtung im Browser erreichbar unter `http://<ESP-IP>/`

| URL | Funktion |
|---|---|
| `/` | Dashboard – Sensorwerte + ESP-Status |
| `/settings` | Konfiguration (WLAN, IP, HA-Token) |
| `/api/sensors` | Sensordaten als JSON (REST-Endpunkt) |

**Kein Login erforderlich.**

---

## Netzwerk-Konfiguration

| Modus | Verhalten |
|---|---|
| **DHCP** (Standard) | IP-Adresse automatisch vom Router |
| **Statisch** | Feste IP, Gateway, Subnetz und DNS konfigurierbar |
| **AP-Fallback** | Falls kein WLAN erreichbar → eigenes WLAN nach 60 s |

AP-Name und alle Netzwerkparameter sind über das Web-Interface konfigurierbar.

---

## Display-Layout

```
+------------------+------------------+
|  STROM BEZUG     |  AKKU 1          |
|   1 234 W        |   85 %           |
+------------------+------------------+
|  AUSSENTEMP 1    |  AUSSENTEMP 2    |
|   18,5 °C        |   17,2 °C        |
+------------------+------------------+
```

Reine Sensorwert-Anzeige, kein Titel-Header, maximale Lesbarkeit.

---

## Build ohne Display

Für ESP32-Boards ohne TFT-Display einfach am Anfang von `ESP32_HA_Display.ino` auskommentieren:

```cpp
// #define HAS_DISPLAY
```

Alle anderen Funktionen (WLAN, Web-Interface, HA-Abfrage) bleiben vollständig erhalten.

---

## Changelog

| Version | Änderungen |
|---|---|
| **1.1** | Display-Bug behoben (weiße Striche); Display-Layout auf reines 2×2-Sensor-Grid vereinfacht; Versionsnummer im Web-Interface |
| **1.0** | Erstveröffentlichung: Display, Webserver, HA REST-API, AP-Fallback |

---

## Lizenz

MIT License – freie Nutzung, Änderung und Weitergabe.
