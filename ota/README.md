# OTA bootstrap

Minimal sketch flashed **once over USB** on a new ESP32/ESP8266. It connects to Wi-Fi, downloads
the latest production firmware for its `LOCALE`/`ID` from the Pi and replaces itself. After that,
the production firmware handles OTA updates on its own.

## How it works

1. Connects to Wi-Fi via WiFiManager. If no credentials are saved, it opens the `ESP-Config`
   access point (password `senha123`) for 180 s, then restarts. Credentials are kept after the
   flash, so production firmware reuses them.
2. Fetches `http://pi-desktop:9595/<chip>/<locale>/<id>/latest.json`.
3. Flashes the `file` listed there (default `firmware.bin`) **unconditionally**, no version check,
   and reboots into it.
4. On any failure (HTTP error, invalid JSON, flash error) it logs to serial (115200) and retries
   every 30 s.

`<chip>` is detected at compile time: `esp32` or `esp8266`.

## Usage

Make sure Jenkins has already published a build to `/shared/ota/<chip>/<locale>/<id>/`.
Otherwise the bootstrap just keeps retrying.

Set `LOCALE` and `ID` at the top of `ota.ino`, or pass them at compile time:

```bash
# ESP8266
arduino-cli compile --fqbn esp8266:esp8266:generic --build-property "compiler.cpp.extra_flags=-DLOCALE=\"quarto\" -DID=\"001\"" ota
arduino-cli upload  --fqbn esp8266:esp8266:generic -p COM3 ota

# ESP32
arduino-cli compile --fqbn esp32:esp32:esp32 --build-property "compiler.cpp.extra_flags=-DLOCALE=\"sala\" -DID=\"002\"" ota
arduino-cli upload  --fqbn esp32:esp32:esp32 -p COM3 ota
```

Libraries: WiFiManager, ArduinoJson (v6).

See the [root README](../README.md#adding-a-new-device) for the full new-device flow.
