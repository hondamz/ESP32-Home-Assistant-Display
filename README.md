# ESP32 Home Assistant Display

**Version 1.4**

ESP32-basiertes Display-System für [Home Assistant](https://www.home-assistant.io/), entwickelt für das **LILYGO T-Display-S3** (ST7789 LCD, 170×320 px). Zeigt Sensordaten aus Home Assistant live auf dem integrierten Display an und hostet gleichzeitig ein Web-Interface zur Datenanzeige und Gerätekonfiguration.

---

## Features

- **Live-Sensordaten** vom Home Assistant REST-API (alle **5 Sekunden**)
- **TFT-Display** mit 2×2-Grid-Layout – übersichtliche Darstellung von 4 Messwerten
- **Web-Interface** (Port 80) – erreichbar im Browser ohne Login:
  - **Dashboard** – Sensorwerte (inkl. Solar + Akku-Reichweite), HA-Status-LED, Datum/Uhrzeit; **AJAX-Aktualisierung alle 6 s** (kein Seitenneustart)
  - **Verlaufsgrafiken** – Strom und Solar als Canvas-Chart, konfigurierbare Zeitfenster (1–24 h, Standard 6 h)
  - **Status** – Netzwerkstatus, Akku-Ladestände + Restkapazitätsberechnung, ESP32 Hardware-Info
  - **Einstellungen** – WLAN, IP, HA-Token, Akku-Kapazitäten (kWh), Verlaufsstunden
- **HA-Verbindungsanzeige** – farbige LED auf dem Dashboard zeigt den HA-Status in Echtzeit
- **Akku-Restkapazitätsberechnung** – aus konfigurierter Gesamtkapazität und aktuellem SOC
- **Akku-Reichweite** – Hochrechnung, wie lange Akku 1 bei aktuellem Verbrauch noch reicht (Stunden, 1 Dezimalstelle); Anzeige im Dashboard, aktualisiert mit jedem AJAX-Poll
- **ESP32 Hardware-Info** – Chip, Flash, RAM, Display-Typ (erfasst beim Start und nach Konfigurationsänderung)
- **DHCP mit AP-Fallback** – kein WLAN erreichbar? Der ESP öffnet nach 60 s ein eigenes WLAN
- **Persistente Konfiguration** – alle Einstellungen bleiben nach Neustart erhalten (NVS/Flash)

---

## Angezeigte Sensoren

| Anzeige | Home Assistant Entity |
|---|---|
| Aktueller Stromverbrauch | `sensor.hlp_strom_aktueller_bezug` |
| Solar Produktion | `sensor.hlp_solar_produktion_summe` |
| Akku 1 – Ladezustand | `sensor.victron_battery_soc` |
| Außentemperatur 1 | `sensor.bthome_sensor_83ec_temperatur` |
| Außentemperatur 2 | `sensor.wohnzimmer_mz_aussenmodul_temperatur` |

> Solar-Werte können negativ sein (Einspeisung). Akku 2 wird in einer späteren Version eingebunden.

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

| Bibliothek | Autor |
|---|---|
| **LovyanGFX** | lovyan03 |
| **ArduinoJson** | Benoit Blanchon |

Built-in: `WiFi`, `WebServer`, `HTTPClient`, `Preferences`

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

1. HA → Benutzer-Icon → **Profil** → Reiter **Sicherheit**
2. *Langlebige Zugriffstoken* → **Token erstellen**
3. Token im Web-Interface des ESP unter **Einstellungen → Home Assistant** eintragen

---

## Erstinbetriebnahme

1. Sketch flashen
2. ESP startet im **AP-Modus**: WLAN `ESPDisplay1`, Browser → `http://192.168.4.1/settings`
3. WLAN + HA-URL + Token eintragen → **Speichern**
4. ESP zeigt sofort Sensorwerte an

---

## Web-Interface

| URL | Funktion |
|---|---|
| `/` | **Dashboard** – Sensorwerte, HA-LED, Uhrzeit, Verlaufsgrafiken; AJAX-Update alle 6 s |
| `/status` | **Status** – Netzwerk, Akku-Kapazitäten + Restladung, Hardware-Info |
| `/settings` | **Einstellungen** – WLAN, IP, HA-Token, Akku-Kapazitäten, Verlaufsstunden |
| `/api/sensors` | Sensordaten als JSON |
| `/api/history` | Verlaufsdaten (Strom + Solar) als JSON |

### Dashboard – HA-Status-LED

| Farbe | Bedeutung |
|---|---|
| 🟢 Grün | Letzte erfolgreiche Abfrage ≤ 15 s |
| 🟡 Gelb | Letzte erfolgreiche Abfrage 16–60 s |
| 🔴 Rot | > 60 s oder keine Daten |

### Status-Seite – Akku-Restkapazität

Die Restkapazität wird berechnet aus:

```
Restkapazität [kWh] = Gesamtkapazität [kWh] × (Ladezustand [%] / 100)
```

Die Gesamtkapazitäten werden unter **Einstellungen → Akku-Konfiguration** eingetragen.

---

## Netzwerk-Konfiguration

| Modus | Verhalten |
|---|---|
| **DHCP** (Standard) | IP automatisch vom Router |
| **Statisch** | Feste IP, Gateway, Subnetz, DNS |
| **AP-Fallback** | Eigenes WLAN nach 60 s ohne Verbindung |

---

## Display-Layout (TFT)

```
+------------------+------------------+
|  STROM BEZUG     |  AKKU 1          |
|   1 234 W        |   85 %           |
+------------------+------------------+
|  AUSSENTEMP 1    |  AUSSENTEMP 2    |
|   18,5 °C        |   17,2 °C        |
+------------------+------------------+
```

Solar-Wert ist nur im Web-Dashboard sichtbar, nicht auf dem TFT.

---

## Timing-Konstanten

| Konstante | Wert | Bedeutung |
|---|---|---|
| `HA_POLL_MS` | 5 000 ms | HA-Abfrageintervall |
| `HIST_INT_MS` | 60 000 ms | Verlaufspuffer-Abtastintervall (1 Minute) |
| `HIST_MAX` | 1 440 | Verlaufspuffer-Tiefe (24 h @ 1 min) |
| `WIFI_TIMEOUT_MS` | 60 000 ms | WLAN-Verbindungsversuch |
| `WIFI_RETRY_MS` | 120 000 ms | WLAN-Neuverbindungsabstand |

---

## ESP32 Hardware-Info (Status-Seite)

Beim Start und nach jeder Konfigurationsspeicherung werden folgende Werte erfasst und auf der Status-Seite angezeigt:

- Chip-Modell (z. B. `ESP32-S3`)
- Flash-Speicher in MB
- RAM gesamt in KB
- RAM verfügbar (Snapshot beim Start) in KB
- Verbautes Display

---

## JSON-API

`GET /api/sensors`:

```json
{
  "strom": "1234",        "strom_unit": "W",
  "solar": "2.5",         "solar_unit": "kW",
  "akku": "85",           "akku_unit": "%",
  "temp1": "18.5",        "temp1_unit": "°C",
  "temp2": "17.2",        "temp2_unit": "°C",
  "ha_ok": true,
  "wifi_ok": true,
  "ap_mode": false,
  "last_ha_success_ago": 3,
  "ip": "192.168.50.100"
}
```

`GET /api/history`:

```json
{
  "strom": [1234.0, 1180.0, 950.0],
  "solar": [2.5, 3.1, 0.0],
  "strom_unit": "W",
  "solar_unit": "kW",
  "count": 3,
  "hours": 6
}
```

---

## Build ohne Display

```cpp
// #define HAS_DISPLAY
```

Alle Web-Funktionen (WLAN, Dashboard, HA-Abfrage) bleiben vollständig erhalten.

---

## Lizenz

Copyright © 2026 Andreas  
Licensed under the Apache License, Version 2.0

---

## Changelog

| Version | Änderungen |
|---|---|
| **1.4** | Akku-Reichweite im Dashboard: Hochrechnung, wie lange Akku 1 bei aktuellem Stromverbrauch noch reicht (Stunden, 1 Dezimalstelle); Wert wird bei jedem AJAX-Poll aktualisiert; `akku_range_h`-Feld in `/api/sensors` |
| **1.31** | Dashboard-Neustart durch AJAX-Polling ersetzt (kein `location.reload()` mehr); Verlaufsgrafiken für Strom und Solar (Canvas, 1-Minuten-Abtastrate, bis 24 h); Zeitfenster konfigurierbar in Einstellungen (1–24 h, Standard 6 h); neuer Endpunkt `/api/history` |
| **1.3** | Dashboard auto-reload alle 6 s (synchron mit 5s HA-Poll); Solar-Sensor hinzugefügt; Akku-Restkapazitätsberechnung auf Status-Seite; ESP32 Hardware-Info auf Status-Seite; Akku-Kapazitäten in Einstellungen; Footer-Farbe korrigiert; Copyright-Hinweis in Footer |
| **1.2** | HA-Abfrageintervall auf 5 s; neue Status-Seite; HA-Status-LED auf Dashboard; Datum/Uhrzeit; Abfrageintervall in Einstellungen |
| **1.1** | Display-Bug behoben; Layout auf 2×2-Sensor-Grid vereinfacht; Versionsnummer im Web |
| **1.0** | Erstveröffentlichung |
