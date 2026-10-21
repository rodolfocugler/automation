# Device codes

Where the IR / BLE commands used by the firmware come from, and how to find new ones.

| Device                     | Link | Protocol            | Where the codes live                     | How they were found |
|----------------------------|------|---------------------|------------------------------------------|---------------------|
| Samsung TV                 | IR   | Samsung32 (`0xE0E0xxxx`) | `production/sala/001/001.ino` (`TV_*`)   | Internet (see below) |
| Led TV (strip behind TV)   | IR   | NEC (`0xF7xxxx`)    | `production/sala/001/001.ino` (`LED_*`, `LED_COLORS`) | Source not recorded |
| Briloner (living room)     | IR   | NEC addr `0x00` cmd `0x45` | `production/sala/002/002.ino`     | Internet (Flipper IRDB) |
| Triones LED (`Led Cama`)   | BLE  | Triones (service `FFD5`, char `FFD9`) | `production/quarto/001/001.ino` | Source not recorded |

## Samsung TV

**Found on the internet.**

- Discrete ON/OFF codes (Pronto format):
  [Remote Central thread](https://www.remotecentral.com/cgi-bin/mboard/rc-discrete/thread.cgi?5780)
- How to turn a Pronto code into the 32-bit value (`0xE0E019E6` = OFF, `0xE0E09966` = ON):
  [Stack Overflow](https://stackoverflow.com/questions/60718588/understanding-ir-codes-for-samsung-tv),
  with a step-by-step copy in [`ir_codes_alexa/README.md`](ir_codes_alexa/README.md).
- Pronto format explained: [Remote Central](https://www.remotecentral.com/features/irdisp2.htm).
- Code database that decodes Pronto strings: [irdb](https://github.com/probonopd/irdb).

The full key list is in `alexa_mqtt_luz_sala/samsungCodes.h`. Every code starts with `E0E0`
(device 7, subdevice 7); the last two bytes are the key and its inverse.

> YouTube has two values in the repo: `0xE0E0EF10` (`lampCodes.h`) and `0xE0E00E8C`
> (`samsungCodes.h`, used in production). If a key doesn't work, read it from the remote
> (see [Reading a code from a remote](#reading-a-code-from-a-remote-ir)).

Sent with `IrSender.sendSamsungMSB(code, 32)`.

## Led TV

**Source not recorded in the repo.** These are the codes of the common 24-key RGB LED remote
(`0xF700FF` brightness +, `0xF7C03F` ON, `0xF740BF` OFF, `0xF720DF` red, ...). The full list
is in `alexa_mqtt_luz_sala/ledCodes.h`.

To check or re-discover them, read the strip's remote with an IR receiver
(see [Reading a code from a remote](#reading-a-code-from-a-remote-ir)). Each colour key
gives one code; the RGB value next to it in `LED_COLORS` is just the colour picked by eye
to match the key, used to choose the closest key for Alexa's `rgb_color`.

Sent with `IrSender.sendNECMSB(code, 32)`.

## Briloner

**Found on the internet:** Flipper Zero IR database,
[`Briloner_leuchten.ir`](https://github.com/Lucaslhm/Flipper-IRDB/blob/main/LED_Lighting/Briloner/Briloner_leuchten.ir)
(link also in a comment in `002.ino`).

The file lists each button as `protocol: NEC`, `address`, `command`. The power button is
address `00`, command `45`, which maps directly to `IrSender.sendNEC(0x00, 0x45, 0)`.
It is a **toggle**: the remote has no separate ON/OFF.

## Triones LED (BLE)

**Source not recorded in the repo.** It is not IR: the ESP32 connects over BLE and writes to
characteristic `FFD9` of service `FFD5`.

| Command    | Bytes                          |
|------------|--------------------------------|
| ON         | `CC 23 33`                     |
| OFF        | `CC 24 33`                     |
| Colour     | `56 RR GG BB 00 F0 AA`         |
| Mode       | see `nimbletriones.h` (ids `0x25`–`0x38`, `0x61`, `0x62`) |

Public description of the same protocol:
[saberlight Triones protocol](https://github.com/madhead/saberlight/blob/master/protocols/Triones/protocol.md).

**Finding the MAC** (`53:02:02:00:04:BB`, set in `BluLed.h`): scan with
[nRF Connect](https://www.nordicsemi.com/Products/Development-tools/nRF-Connect-for-mobile)
on the phone; the strip shows up as `Triones-...`. Close the official app first, the strip
accepts only one connection.

**Reading a new command** from the official app (HappyLighting):

1. Android → Developer options → enable *Bluetooth HCI snoop log*, turn Bluetooth off/on.
2. Use the app to send the command you want.
3. Get the log (`adb bugreport`, file `btsnoop_hci.log`) and open it in Wireshark.
4. Filter `btatt.opcode == 0x52 || btatt.opcode == 0x12` (ATT writes): the value written to
   handle `FFD9` is the command.

## Reading a code from a remote (IR)

Use this for any IR remote (Samsung, Led TV, Briloner, new devices).

1. Wire an IR receiver (VS1838B / TSOP38238) to the ESP: `VCC → 3.3V`, `GND → GND`,
   `OUT → GPIO 14` (any free pin).
2. Flash the `IRrecvDumpV2` example from IRremoteESP8266
   (*File → Examples → IRremoteESP8266 → IRrecvDumpV2*), setting `kRecvPin = 14`.
3. Open Serial Monitor at 115200 and press the button pointing at the receiver:
   ```
   Protocol  : NEC
   Code      : 0xF7C03F (32 Bits)
   ```
4. Use `Code` as-is with the `...MSB(code, 32)` send functions (`sendNECMSB`,
   `sendSamsungMSB`).

With IRremote's `ReceiveDump` example you get `Protocol=NEC Address=0x0 Command=0x45` instead,
which matches `sendNEC(address, command, repeats)` (as used for Briloner).

If the protocol shows `UNKNOWN`, the remote uses a protocol the library can't decode; send the
raw timings printed by the dump with `sendRaw` instead.
