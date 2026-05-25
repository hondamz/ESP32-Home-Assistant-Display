# Projektanweisungen – ESP32 Home Assistant Display

## GitHub-Workflow (PFLICHT nach jeder Codeänderung)

Nach **jeder** Änderung am Code oder der README musst du folgende Schritte ausführen:

### 1. README.md aktualisieren
- Versionsnummer anpassen (`APP_VERSION` aus dem Sketch übernehmen)
- Changelog-Zeile für die neue Version ergänzen (mit allen Änderungen)
- Betroffene Abschnitte aktualisieren (Features, Web-Interface, Timing, etc.)

### 2. Commit und Push zu GitHub
```bash
cd /Users/andreasliedke/SynologyDriveShared/SynologyDrive/_entwicklung/ESP-HA-Anzeige/ESP-HA-Display
bash git_push.sh
```

Das Skript liest den Token aus `secrets.local`, setzt die Remote-URL, erstellt den Commit und pusht.

**Repository:** https://github.com/hondamz/ESP32-Home-Assistant-Display

---

## Projektstruktur

```
ESP-HA-Display/
├── ESP32_HA_Display/
│   └── ESP32_HA_Display.ino   ← Haupt-Sketch (Arduino)
├── README.md                  ← Funktionsbeschreibung (immer aktuell halten)
├── CLAUDE.md                  ← Diese Datei (Projektanweisungen)
├── git_push.sh                ← Push-Skript (nach Änderungen ausführen)
├── github_setup.sh            ← Einmalig-Setup (bereits ausgeführt)
├── secrets.local              ← GitHub-Token (NICHT pushen, in .gitignore)
└── .gitignore
```

---

## Technische Rahmenbedingungen

- **Board:** LILYGO T-Display-S3 (ESP32-S3, ST7789, 170×320 px)
- **Framework:** Arduino IDE
- **Display-Bibliothek:** LovyanGFX
- **JSON:** ArduinoJson
- **Webserver:** WebServer (built-in)
- **Konfiguration:** NVS via Preferences

## Coding-Regeln

- Webseiten-Strings via `server.sendContent()` + `F()` Makro (Flash-Speicher)
- CSS in `PAGE_CSS[]` PROGMEM-Array
- Keine Abhängigkeiten von externen Cloud-Diensten – alles lokal
- `secrets.local` enthält nur den GH_TOKEN – niemals committen
