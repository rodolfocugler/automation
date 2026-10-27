# quarto/001 – Led Cama (Triones, BLE)

ESP32 firmware that drives the Triones LED strip by the bed over BLE (on/off, color,
brightness) when Alexa asks. It does nothing else.

## How it works

1. **Boot**: onboard LED off → Wi-Fi (WiFiManager) → NTP → MQTT → OTA check → BLE task → loop.
2. **Loop** (Arduino task): MQTT only. Reconnects MQTT if needed, validates commands and
   queues them for the BLE task, publishes the BLE task's logs and results.
3. **BLE task** (FreeRTOS): owns NimBLE (init, connect, write). Its blocking calls never
   stall MQTT, and MQTT never stalls BLE. Only the loop touches PubSubClient (not
   thread-safe), so the BLE task hands its logs and results back through queues.
4. **Restart**: every 30 min, and when Wi-Fi drops. The OTA check runs at boot, so it
   runs every 30 min too.

While idle, the BLE task keeps trying to open the BLE connection (every 15 s, starting at boot)
so the link is already up when a command arrives. If it is still down then, the command
reconnects on demand (up to 10 attempts, 5 s each, 500 ms apart, fixed 8.75 ms connection interval, like the old `trianon_led_alexa` firmware). If the strip can't be reached, the command is **not** stored, so it is not
marked as executed. A newer command replaces one still waiting for the BLE task.

The BLE task logs `BLE: starting` and `BLE: ready` around the NimBLE init, so a hang there
shows up in Grafana while MQTT keeps working.

### Topics

| Topic                | Purpose |
|----------------------|---------|
| `alexa/quarto/001`   | Command for this device |
| `alexa/quarto/all`   | Command for every device in the room |
| `alexa/all`          | Command for every device |
| `device/quarto/001`  | Last executed command (**retained**), written by this device |
| `logs/quarto/001`    | Logs (sent to Grafana) |

### Command payload

```json
{ "type": "led", "deviceName": "Led Cama", "state": true, "rgb_color": [255, 0, 0], "brightness_pct": 80, "createdAt": 1760000000 }
```

A command is executed only if **all** of these hold:

- `type` is `led` and `deviceName` is `Led Cama`
- `createdAt` (Unix seconds, UTC) is at most **30 s** old
- `createdAt` is newer than the one stored in `device/quarto/001` (or that topic is empty)

`rgb_color` is optional (missing = keep current color). `brightness_pct` (0–100, default 100)
scales the color, the Triones has no separate brightness command, so a brightness without
`rgb_color` only sends on/off.

BLE commands (service `FFD5`, characteristic `FFD9`), see [CODES.md](../../../CODES.md):

| Command | Bytes                    |
|---------|--------------------------|
| ON      | `CC 23 33`               |
| OFF     | `CC 24 33`               |
| Color   | `56 R G B 00 F0 AA`      |

## Hardware

| Chip    | LED                         | Onboard LED      |
|---------|-----------------------------|------------------|
| ESP32   | BLE, MAC `53:02:02:00:04:BB` (`LED_MAC`) | GPIO 2, kept off |

ESP8266 is not supported (no BLE).

## Size / partition scheme

Wi-Fi + BLE don't fit the default 1.25 MB app slot (1,345,716 B, 102 %). Build with
**`min_spiffs`** (1.9 MB per slot, 68 %):

```bash
arduino-cli compile --fqbn esp32:esp32:esp32:PartitionScheme=min_spiffs \
  --build-property "compiler.cpp.extra_flags=-DCURRENT_VERSION=\"$VERSION\"" production/quarto/001
```

> OTA can't change the partition table. `ota/ota.ino` must be flashed over USB with the
> **same** `PartitionScheme=min_spiffs`, and the Jenkins job must use it too.

## OTA

At boot the device fetches `http://pi-desktop:9595/esp32/quarto/001/latest.json`. If
`version` differs from `CURRENT_VERSION`, it flashes `file` and reboots. The check runs
before BLE starts, so the update has the RAM it needs.

## Core

arduino-esp32 **2.0.17** (`arduino-cli core install esp32:esp32@2.0.17`). NimBLE-Arduino
1.4.3 does not work on core 3.x: `NimBLEDevice::init()` spins forever waiting for host/controller
sync (log stops at `BLE: starting`) and the whole board stops (no MQTT, no Wi-Fi).

## Libraries

Run `./setup.sh` to install arduino-cli, the core and every library below at the right version.

- WiFiManager (tzapu) 2.x
- PubSubClient 2.8
- ArduinoJson 6 or 7
- NimBLE-Arduino **1.4.3** only, needs core **2.0.17** (1.x API, as the old `trianon_led_alexa` firmware; 2.x kept failing with BLE error 574). Install with `arduino-cli lib install "NimBLE-Arduino@1.4.3"`

## First setup

1. Flash `ota/ota.ino` over USB with `LOCALE "quarto"`, `ID "001"` and `min_spiffs`.
2. Join the `ESP-Config` access point (password `senha123`) and choose the Wi-Fi.
