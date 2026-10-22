# sala/001 – TV LED strip + Samsung TV

ESP32 / ESP8266 firmware that, when Alexa asks, drives via IR the LED strip behind the TV
(on/off + color) and the Samsung TV (power, media keys, mute). It does nothing else.

Same structure as [sala/002](../002/README.md): boot, topics, dedup, logs, OTA and first
setup work exactly the same, with `002` replaced by `001`.

## Commands

A command is executed only if `createdAt` is at most **30 s** old and newer than the one
stored (retained) in `device/sala/001`. After executing, it is published there.

### LED strip (`type: led`, `deviceName: Led TV`)

```json
{ "type": "led", "deviceName": "Led TV", "state": true, "rgb_color": [255, 0, 0], "createdAt": 1760000000 }
```

- `state: false` → OFF.
- `state: true` → ON, then the remote color closest to `rgb_color` (16-color palette).
  Without `rgb_color` the strip keeps its last color.

### Samsung TV (`type: tv`)

```json
{ "type": "tv", "action": "mediaControl", "value": "Netflix", "createdAt": 1760000000 }
```

| `action`        | `value`                                         |
|-----------------|-------------------------------------------------|
| `setPowerState` | `On`, `Off`                                     |
| `mediaControl`  | `Netflix`, `Youtube`, `Exit`, `Ok`, `Play`, `Pause` |
| `setMute`       | (ignored, toggles mute)                         |

Unknown actions/values and any other `type`/`deviceName` are ignored (not stored as executed).

`Youtube` is not sent via IR (Samsung has no working IR key for it): the ESP calls the TV's
REST API, `POST https://192.168.178.47:8002/api/v2/applications/111299001912`. No token or
pairing is needed, but the TV must be on and keep that IP (reserve it in the router; change
`TV_HOST` otherwise). If the call fails (TV off), the command is not stored as executed.
`youtube.py` does the same from a PC (`python youtube.py IP_DA_TV rest`).

## Hardware

| Chip    | IR LED pin     | Onboard LED              |
|---------|----------------|--------------------------|
| ESP32   | GPIO 23        | GPIO 2, kept off         |
| ESP8266 | GPIO 4 (D2)    | GPIO 2, kept off         |

## Size

| Board                       | Flash used                     |
|-----------------------------|--------------------------------|
| ESP32 (IRremote 4.4.1)      | 1,192,455 / 1,310,720 B (90 %) |
| ESP8266 generic             | ~448 KB                        |

## Libraries

Same as sala/002 (IRremote **4.x** required: uses `sendNECMSB` / `sendSamsungMSB`).
