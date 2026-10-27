#!/usr/bin/env bash
# Installs arduino-cli, the core and the libraries needed to build quarto/001.
#
# NimBLE-Arduino works here ONLY on 1.4.3, and 1.4.3 needs arduino-esp32 core 2.0.17.
# On core 3.x NimBLEDevice::init() hangs forever and the whole board stops.
#
# arduino-cli keeps one version of each core, so this replaces any other esp32 core
# (e.g. the 3.3.8 used by sala/*). To keep both, run with a separate data dir:
#   ARDUINO_DIRECTORIES_DATA=~/.arduino15-quarto001 ./setup.sh
# and build with the same variable set.
set -euo pipefail

ESP32_CORE_VERSION="2.0.17"
ESP32_URL="https://espressif.github.io/arduino-esp32/package_esp32_index.json"

LIBS=(
  "NimBLE-Arduino@1.4.3"
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
if ! arduino-cli config dump | grep -qF "$ESP32_URL"; then
  arduino-cli config add board_manager.additional_urls "$ESP32_URL"
fi
arduino-cli core update-index
arduino-cli core install "esp32:esp32@$ESP32_CORE_VERSION"

arduino-cli lib update-index
for lib in "${LIBS[@]}"; do
  arduino-cli lib install "$lib"
done

echo
echo "Done. Build with:"
echo "  arduino-cli compile --fqbn esp32:esp32:esp32:PartitionScheme=min_spiffs \\"
echo "    --build-property \"compiler.cpp.extra_flags=-DCURRENT_VERSION=\\\"\$VERSION\\\"\" production/quarto/001"
