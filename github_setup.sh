#!/bin/bash
# ================================================================
#  GitHub Setup – ESP32 Home Assistant Display
#  Einmalig ausführen: bash github_setup.sh
# ================================================================

set -e

REPO_DIR="$(cd "$(dirname "$0")" && pwd)"
GH_USER="hondamz"
REPO_NAME="ESP32-Home-Assistant-Display"

# Token aus secrets.local lesen (wird NICHT zu GitHub hochgeladen)
SECRETS_FILE="$REPO_DIR/secrets.local"
if [ ! -f "$SECRETS_FILE" ]; then
  echo "FEHLER: $SECRETS_FILE nicht gefunden."
  echo "Bitte Datei anlegen mit Inhalt: GH_TOKEN=ghp_..."
  exit 1
fi
source "$SECRETS_FILE"

if [ -z "$GH_TOKEN" ]; then
  echo "FEHLER: GH_TOKEN ist leer in $SECRETS_FILE"
  exit 1
fi

REMOTE_URL="https://${GH_USER}:${GH_TOKEN}@github.com/${GH_USER}/${REPO_NAME}.git"

echo ""
echo "=== ESP32 HA Display – GitHub Setup ==="
echo "Ordner: $REPO_DIR"
echo ""

cd "$REPO_DIR"

# ── 1. Repository auf GitHub anlegen ─────────────────────────────
echo "[ 1/5 ] Erstelle GitHub-Repository..."
HTTP_STATUS=$(curl -s -o /tmp/gh_response.json -w "%{http_code}" \
  -X POST \
  -H "Authorization: token ${GH_TOKEN}" \
  -H "Accept: application/vnd.github.v3+json" \
  https://api.github.com/user/repos \
  -d "{\"name\":\"${REPO_NAME}\",\"description\":\"ESP32 LILYGO T-Display-S3 – Home Assistant Sensor Display mit Web-Interface\",\"private\":false,\"auto_init\":false}")

if [ "$HTTP_STATUS" = "201" ]; then
  echo "     ✓ Repository erstellt"
elif [ "$HTTP_STATUS" = "422" ]; then
  echo "     ✓ Repository existiert bereits – weiter"
else
  echo "     ! GitHub API Status: $HTTP_STATUS"
  cat /tmp/gh_response.json
fi

# ── 2. Git initialisieren ────────────────────────────────────────
echo "[ 2/5 ] Initialisiere lokales Git-Repository..."
git init
git config user.name "$GH_USER"
git config user.email "liedke.andreas@googlemail.com"
git checkout -b main 2>/dev/null || git checkout main
echo "     ✓ Git initialisiert (Branch: main)"

# ── 3. Credential-Helper konfigurieren ───────────────────────────
echo "[ 3/5 ] Speichere GitHub-Zugangsdaten..."
git config credential.helper store
printf "https://%s:%s@github.com\n" "$GH_USER" "$GH_TOKEN" >> ~/.git-credentials
chmod 600 ~/.git-credentials
echo "     ✓ Zugangsdaten gespeichert"

# ── 4. Remote setzen ─────────────────────────────────────────────
echo "[ 4/5 ] Setze Remote-URL..."
git remote remove origin 2>/dev/null || true
git remote add origin "$REMOTE_URL"
echo "     ✓ Remote: https://github.com/${GH_USER}/${REPO_NAME}"

# ── 5. Ersten Commit erstellen und pushen ────────────────────────
echo "[ 5/5 ] Erstelle Commit und pushe zu GitHub..."
git add -A
git commit -m "v1.1 – Initialversion: T-Display-S3, HA REST-API, Webserver, AP-Fallback

- LovyanGFX Display-Konfiguration fuer LILYGO T-Display-S3
- Display-Bug behoben: 20 MHz Bus, memory_width=240, ColorDepth 16-Bit
- 2x2-Grid Layout: nur HA-Sensorwerte (Strom, Akku, 2x Aussentemperatur)
- Web-Interface: Dashboard + Einstellungsseite + JSON-API Endpunkt
- DHCP mit AP-Fallback nach 60s (ESPDisplay1)
- Alle Einstellungen NVS-persistent (ueberleben Neustart)
- Build mit/ohne Display via HAS_DISPLAY Flag
- README.md mit vollstaendiger Dokumentation"

git push -u origin main
echo ""
echo "========================================================"
echo "  ✓ Fertig!"
echo "  Repository: https://github.com/${GH_USER}/${REPO_NAME}"
echo "========================================================"
echo ""
echo "Zukuenftige Updates:"
echo "  git add -A && git commit -m 'Beschreibung' && git push"
echo ""
