#!/usr/bin/env bash
# Installs arduino-cli, the cores and the libraries needed to build sala/001
# (ESP32 or ESP8266).
#
# IRremote must be 4.x (3.x does not compile on ESP32 core 3.x).
#
# arduino-cli keeps one version of each core, so this replaces any other esp32 core
# (e.g. the 2.0.17 that quarto/001 needs for NimBLE 1.4.3). To keep both, run with a
# separate data dir:
#   ARDUINO_DIRECTORIES_DATA=~/.arduino15-sala ./setup.sh
# and build with the same variable set.
set -euo pipefail

ESP32_CORE_VERSION="3.3.8"
ESP8266_CORE_VERSION="3.1.2"
ESP32_URL="https://espressif.github.io/arduino-esp32/package_esp32_index.json"
ESP8266_URL="https://arduino.esp8266.com/stable/package_esp8266com_index.json"

LIBS=(
  "IRremote@4.7.1"
  "WiFiManager@2.0.17"
  "PubSubClient@2.8"
  "ArduinoJson@7.4.2"
)

if ! command -v arduino-cli >/dev/null; then
  echo "Installing arduino-cli into ~/.local/bin"
  mkdir -p ~/.local/bin
  curl -fsSL https://raw.githubusercontent.com/arduino/arduino-cli/master/install.sh | BINDIR=~/.local/bin sh
  export PATH="$HOME/.local/bin:$PATH"
fi

arduino-cli config init >/dev/null 2>&1 || true   # keeps an existing config
for url in "$ESP32_URL" "$ESP8266_URL"; do
  if ! arduino-cli config dump | grep -qF "$url"; then
    arduino-cli config add board_manager.additional_urls "$url"
  fi
done
arduino-cli core update-index
arduino-cli core install "esp32:esp32@$ESP32_CORE_VERSION"
arduino-cli core install "esp8266:esp8266@$ESP8266_CORE_VERSION"

arduino-cli lib update-index
for lib in "${LIBS[@]}"; do
  arduino-cli lib install "$lib"
done

echo
echo "Done. Build with:"
echo "  arduino-cli compile --fqbn esp32:esp32:esp32 \\"
echo "    --build-property \"compiler.cpp.extra_flags=-DCURRENT_VERSION=\\\"\$VERSION\\\"\" production/sala/001"
echo "  (ESP8266: --fqbn esp8266:esp8266:generic)"
