#!/usr/bin/env bash
# ================================================================
#  flash.sh – Kompilieren und/oder Flashen des ESP32 HA Display
#  Verwendung:
#    bash flash.sh            → kompilieren + flashen (Port auto)
#    bash flash.sh compile    → nur kompilieren
#    bash flash.sh ports      → verfügbare USB-Ports anzeigen
# ================================================================

set -euo pipefail

SKETCH_DIR="$(cd "$(dirname "$0")" && pwd)/ESP32_HA_Display"
export PATH="$HOME/bin:$PATH"

# Board-Define aus Sketch auslesen
if grep -q '^#define BOARD_TDISPLAY_S3' "$SKETCH_DIR/ESP32_HA_Display.ino"; then
  FQBN="esp32:esp32:esp32s3:PSRAM=opi,FlashSize=16M"
  BOARD_NAME="T-Display-S3 (ESP32-S3)"
elif grep -q '^#define BOARD_TDISPLAY_V11' "$SKETCH_DIR/ESP32_HA_Display.ino"; then
  FQBN="esp32:esp32:esp32"
  BOARD_NAME="T-Display V1.1 (ESP32)"
else
  echo "❌ Kein Board-Define im Sketch gefunden."
  exit 1
fi

echo "=== ESP32 HA Display – Build/Flash ==="
echo "Board : $BOARD_NAME"
echo "FQBN  : $FQBN"
echo ""

# Nur Ports anzeigen
if [[ "${1:-}" == "ports" ]]; then
  echo "Verfügbare Ports:"
  arduino-cli board list 2>/dev/null || ls /dev/cu.usb* /dev/cu.SLAB* 2>/dev/null || echo "(keine gefunden)"
  exit 0
fi

# Kompilieren
echo "▶ Kompiliere..."
arduino-cli compile --fqbn "$FQBN" "$SKETCH_DIR" 2>&1
echo "✓ Kompilierung erfolgreich"
echo ""

# Nur kompilieren?
if [[ "${1:-}" == "compile" ]]; then
  echo "ℹ Flash übersprungen (nur compile)."
  exit 0
fi

# Port automatisch erkennen
PORT=$(arduino-cli board list 2>/dev/null | grep -i "esp32\|CP210\|CH340\|JTAG" | awk '{print $1}' | head -1)

if [[ -z "$PORT" ]]; then
  # Fallback: erstes USB-Seriell-Gerät
  PORT=$(ls /dev/cu.usb* /dev/cu.SLAB* 2>/dev/null | head -1)
fi

if [[ -z "$PORT" ]]; then
  echo "❌ Kein ESP32 gefunden. USB-Kabel prüfen oder Port manuell angeben:"
  echo "   arduino-cli upload -p /dev/cu.usbXXX --fqbn $FQBN $SKETCH_DIR"
  exit 1
fi

echo "▶ Flashe auf Port: $PORT"
arduino-cli upload -p "$PORT" --fqbn "$FQBN" "$SKETCH_DIR" 2>&1
echo ""
echo "✓ Fertig! ESP32 startet neu."
