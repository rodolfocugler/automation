# sala/002 – Briloner living room light

ESP32 / ESP8266 firmware that turns the Briloner ceiling light on and off when Alexa asks.
It does nothing else.

## How it works

1. **Boot**: all LEDs off → Wi-Fi (WiFiManager) → NTP → MQTT → OTA check → loop.
2. **Loop**: handles MQTT messages, reconnects MQTT if needed.
3. **Restart**: every 30 min, and when Wi-Fi drops. The OTA check runs at boot, so it
   runs every 30 min too.

### Topics

| Topic                | Purpose |
|----------------------|---------|
| `alexa/sala/002`     | Command for this device |
| `alexa/sala/all`     | Command for every device in the room |
| `alexa/all`          | Command for every device |
| `device/sala/002`    | Last executed command (**retained**), written by this device |
| `logs/sala/002`      | Logs (sent to Grafana) |

### Command payload

```json
{ "type": "led", "deviceName": "Luz Sala", "state": true, "createdAt": 1760000000 }
```

A command is executed only if **all** of these hold:

- `type` is `led` and `deviceName` is `Luz Sala`
- `createdAt` (Unix seconds, UTC) is at most **30 s** old
- `createdAt` is newer than the one stored in `device/sala/002` (or that topic is empty)

After executing, the device publishes the command (retained) to `device/sala/002`, so it is
never executed again, even after a restart. The state topic is subscribed before the command
topics, so the broker always delivers the retained state first.

> The Briloner remote has a single power **toggle** code (NEC `0x00/0x45`), so `state` is
> not used: every accepted command toggles the light.

## Hardware

| Chip    | IR LED pin     | Onboard LED              |
|---------|----------------|--------------------------|
| ESP32   | GPIO 23        | GPIO 2, kept off         |
| ESP8266 | GPIO 4 (D2)    | GPIO 2, kept off         |

## Logs

Every log line goes to Serial (115200 baud) and, once MQTT is connected, to `logs/sala/002`
(Grafana). Lines logged before MQTT connects (Wi-Fi, NTP) only go to Serial.

## OTA

At boot the device fetches `http://pi-desktop:9595/<chip>/sala/002/latest.json`:

```json
{ "version": "20260602_214237", "file": "firmware.bin" }
```

If `version` differs from `CURRENT_VERSION`, it flashes `file` and reboots.
Jenkins must inject the same version at compile time (`-DCURRENT_VERSION` in the sketch is only a fallback), otherwise the device re-flashes on every boot:

```bash
arduino-cli compile --fqbn esp32:esp32:esp32 \
  --build-property "compiler.cpp.extra_flags=-DCURRENT_VERSION=\"$VERSION\"" production/sala/002
```

A build without the flag reports version `dev`. Do **not** use `build.extra_flags`: on ESP32 it
replaces the core flags (`-DESP32`, ...) and the build fails.

## Size

OTA needs room for two firmware images, so the code avoids extra libraries and heap strings
(topics are compile-time constants). Measured builds:

| Board                       | Flash used                |
|-----------------------------|---------------------------|
| ESP32 (core 3.3.8)          | 1,101,855 / 1,310,720 B (84 %) |
| ESP8266 generic (core 3.1.2)| ~385 KB                   |

If the ESP32 image ever outgrows the slot, use the `min_spiffs` partition scheme
(`--fqbn esp32:esp32:esp32:PartitionScheme=min_spiffs`, ~1.9 MB per slot).

## Libraries

Run `./setup.sh` to install arduino-cli, the cores and every library at the right version.

- WiFiManager (tzapu) 2.x
- PubSubClient 2.8
- ArduinoJson 6 or 7
- IRremote **4.x** (3.x does not compile on ESP32 core 3.x); the Pi has 4.7.1

## First setup

1. Flash over USB once (OTA only works after this firmware is running).
2. Join the `ESP-Config` access point (password `senha123`) and choose the Wi-Fi.
