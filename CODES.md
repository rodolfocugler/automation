# Device codes

Where the IR / BLE commands used by the firmware come from, the full code lists, and how to
find new ones. This file is self-contained: the old experiment folders (`alexa_mqtt*`,
`ir_codes_alexa`, `trianon_led_*`) were deleted after their codes were copied here. They are
still in git history (`git show e04a85a:alexa_mqtt_luz_sala/samsungCodes.h`).

| Device                     | Link | Protocol            | Used in                                  | How they were found |
|----------------------------|------|---------------------|------------------------------------------|---------------------|
| Samsung TV                 | IR   | Samsung32 (`0xE0E0xxxx`) | `production/sala/001/001.ino` (`TV_*`)   | Internet (see below) |
| Led TV (strip behind TV)   | IR   | NEC (`0xF7xxxx`)    | `production/sala/001/001.ino` (`LED_*`, `LED_COLORS`) | Source not recorded |
| Briloner (living room)     | IR   | NEC addr `0x00`     | `production/sala/002/002.ino`            | Internet (Flipper IRDB) |
| Triones LED (`Led Cama`)   | BLE  | Triones (service `FFD5`, char `FFD9`) | `production/quarto/001/001.ino` | Source not recorded |

## Adding a new button (quick recipe)

1. Look the button up in the tables below. If it's there, use that code.
2. If not, **read it from the real remote** with an IR receiver
   ([Reading a code from a remote](#reading-a-code-from-a-remote-ir)), or look it up online
   ([irdb](https://github.com/probonopd/irdb), [Flipper-IRDB](https://github.com/Lucaslhm/Flipper-IRDB)).
3. Add it to the firmware with the same send call already used for that device:

   | Device    | Send call (IRremote 4.x, used in production)   |
   |-----------|------------------------------------------------|
   | Samsung   | `IrSender.sendSamsungMSB(0xE0E0xxxx, 32)`      |
   | Led TV    | `IrSender.sendNECMSB(0xF7xxxx, 32)`            |
   | Briloner  | `IrSender.sendNEC(0x00, <command>, 0)`         |

   For Samsung, add a `{"<Alexa value>", 0x...}` entry to `TV_MEDIA` (or a new table/action)
   in `production/sala/001/001.ino`.
4. Add the code to the table here, with where it came from.

The old firmware used IRremoteESP8266 instead; the same codes work there with
`irsend.sendSAMSUNG(code, 32)` and `irsend.sendNEC(code, 32)`.

## Samsung TV

**Found on the internet.**

- Discrete ON/OFF codes (Pronto format):
  [Remote Central thread](https://www.remotecentral.com/cgi-bin/mboard/rc-discrete/thread.cgi?5780)
- How to turn a Pronto code into the 32-bit value: [Stack Overflow](https://stackoverflow.com/questions/60718588/understanding-ir-codes-for-samsung-tv),
  copied below in [Pronto → 32-bit](#pronto--32-bit-samsung).
- Pronto format explained: [Remote Central](https://www.remotecentral.com/features/irdisp2.htm).
- Code database that decodes Pronto strings: [irdb](https://github.com/probonopd/irdb).

Every code starts with `E0E0` (device 7, subdevice 7); the last two bytes are the key and its
inverse (`xx` + `0xFF - xx`), which is a quick sanity check for a new code.

| Key           | Code         | | Key            | Code         |
|---------------|--------------|-|----------------|--------------|
| Power toggle  | `0xE0E040BF` | | Menu           | `0xE0E058A7` |
| **On**        | `0xE0E09966` | | Smart Hub      | `0xE0E09E61` |
| **Off**       | `0xE0E019E6` | | Guide          | `0xE0E0F20D` |
| Source        | `0xE0E0807F` | | Tools          | `0xE0E0D22D` |
| 1             | `0xE0E020DF` | | Info           | `0xE0E0F807` |
| 2             | `0xE0E0A05F` | | Up             | `0xE0E006F9` |
| 3             | `0xE0E0609F` | | Down           | `0xE0E08679` |
| 4             | `0xE0E010EF` | | Left           | `0xE0E0A659` |
| 5             | `0xE0E0906F` | | Right          | `0xE0E046B9` |
| 6             | `0xE0E050AF` | | **OK** (middle)| `0xE0E016E9` |
| 7             | `0xE0E030CF` | | Return         | `0xE0E01AE5` |
| 8             | `0xE0E0B04F` | | **Exit**       | `0xE0E0B44B` |
| 9             | `0xE0E0708F` | | A (red)        | `0xE0E036C9` |
| 0             | `0xE0E08877` | | B (green)      | `0xE0E028D7` |
| TTX/Mix       | `0xE0E034CB` | | C (yellow)     | `0xE0E0A857` |
| Pre-Ch        | `0xE0E0C837` | | D (blue)       | `0xE0E06897` |
| Vol +         | `0xE0E0E01F` | | E-Manual       | `0xE0E0FC03` |
| Vol −         | `0xE0E0D02F` | | 3D             | `0xE0E0F906` |
| Ch List       | `0xE0E0D629` | | AD/Subt        | `0xE0E0A45B` |
| **Mute**      | `0xE0E0F00F` | | Stop           | `0xE0E0629D` |
| Ch +          | `0xE0E048B7` | | Prev (rewind)  | `0xE0E0A25D` |
| Ch −          | `0xE0E008F7` | | **Play**       | `0xE0E0E21D` |
| **Netflix**   | `0xE0E0CF30` | | **Pause**      | `0xE0E052AD` |
| Amazon Prime  | `0xE0E02FD0` | | Next (ffwd)    | `0xE0E012ED` |

**Bold** = used in production.

> **YouTube:** two IR values were tried, `0xE0E00E8C` and `0xE0E0EF10`; neither worked
> reliably, so production opens YouTube through the TV's REST API instead
> (see [sala/001 README](production/sala/001/README.md)).

### Pronto → 32-bit (Samsung)

Source: [Stack Overflow](https://stackoverflow.com/questions/60718588/understanding-ir-codes-for-samsung-tv).
Samsung OFF code from the Remote Central thread:

```
0000 006D 0000 0022 00AC 00AC 0015 0040 0015 0040 0015 0040 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0040 0015 0040 0015 0040 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0040 0015 0040 0015 0015 0015 0015 0015 0040 0015 0040 0015 0040 0015 0040 0015 0015 0015 0015 0015 0040 0015 0040 0015 0015 0015 0689
```

Preamble:

- `0000` raw code format
- `006D` frequency, 109 = 38.028 kHz
- `0000` no burst pairs in the first sequence
- `0022` 34 burst pairs follow
- `00AC 00AC` header: on 172 cycles, off 172 cycles
- then 32 data burst pairs
- `0015 0689` trailer: on 21 cycles, off 1673 cycles (~44 ms gap)

Decode the 32 data pairs in groups of 8 numbers:

```
0015 0040 0015 0040 0015 0040 0015 0015   -> E
0015 0015 0015 0015 0015 0015 0015 0015   -> 0
0015 0040 0015 0040 0015 0040 0015 0015   -> E
0015 0015 0015 0015 0015 0015 0015 0015   -> 0
0015 0015 0015 0015 0015 0015 0015 0040   -> 1
0015 0040 0015 0015 0015 0015 0015 0040   -> 9
0015 0040 0015 0040 0015 0040 0015 0015   -> E
0015 0015 0015 0040 0015 0040 0015 0015   -> 6
```

- Ignore the "on" columns (always `0015`).
- In the "off" columns, big (`0040`) = `1`, small (`0015`) = `0`.
- `0040 0040 0040 0015` → `1110` → `E`. All lines give **`E0E019E6`** (OFF). The ON code
  gives `E0E09966`.

irdb decodes the same string as "NECx2, device 7, subdevice 7, OBC 152". To get the code from
that:

- bit-reverse device `07` → `E0`
- bit-reverse subdevice `07` → `E0`
- 152 = `0x98`, bit-reversed → `19`
- last byte = `0xFF - 0x19` = `E6`

## Led TV

**Source not recorded.** These are the codes of the common 24-key RGB LED remote. The RGB
value next to each colour key is just the colour picked by eye to match the key; the firmware
uses it to choose the key closest to Alexa's `rgb_color` (`LED_COLORS` in `001.ino`).

| Key        | Code       | RGB used        | | Key        | Code       | RGB used        |
|------------|------------|-----------------|-|------------|------------|-----------------|
| Bright +   | `0xF700FF` |                 | | B6         | `0xF7708F` | 88, 29, 71      |
| Bright −   | `0xF7807F` |                 | | Strobe     | `0xF7F00F` |                 |
| **Off**    | `0xF740BF` |                 | | B7 orange  | `0xF708F7` | 244, 134, 23    |
| **On**     | `0xF7C03F` |                 | | B8         | `0xF78877` | 2, 137, 151     |
| R          | `0xF720DF` | 255, 0, 0       | | B9         | `0xF748B7` | 127, 35, 83     |
| G          | `0xF7A05F` | 0, 255, 0       | | Fade       | `0xF7C837` |                 |
| B          | `0xF7609F` | 0, 0, 255       | | B10 yellow | `0xF728D7` | 254, 235, 50    |
| W          | `0xF7E01F` | 255, 255, 255   | | B11 cyan   | `0xF7A857` | 8, 90, 99       |
| B1         | `0xF710EF` | 235, 40, 7      | | B12        | `0xF76897` | 204, 50, 94     |
| B2         | `0xF7906F` | 27, 167, 70     | | Smooth     | `0xF7E817` |                 |
| B3         | `0xF750AF` | 4, 126, 210     | |            |            |                 |
| Flash      | `0xF7D02F` |                 | |            |            |                 |
| B4         | `0xF730CF` | 235, 66, 11     | |            |            |                 |
| B5         | `0xF7B04F` | 33, 197, 200    | |            |            |                 |

All colour keys are used in production; Bright±, Flash, Strobe, Fade, Smooth are not.

## Briloner

**Found on the internet:** Flipper Zero IR database,
[`Briloner_leuchten.ir`](https://github.com/Lucaslhm/Flipper-IRDB/blob/main/LED_Lighting/Briloner/Briloner_leuchten.ir)
(link also in a comment in `002.ino`, the file production was based on). Full contents:

| Name | Protocol | Address        | Command        | Send call                        |
|------|----------|----------------|----------------|----------------------------------|
| ON   | NEC      | `00 00 00 00`  | `45 00 00 00`  | `IrSender.sendNEC(0x00, 0x45, 0)` |
| OFF  | NEC      | `00 00 00 00`  | `47 00 00 00`  | `IrSender.sendNEC(0x00, 0x47, 0)` |

Flipper format: the **first byte** of `address` and `command` is the value; the rest is padding.

Production sends only `0x45` (+ an NEC repeat 40 ms later) and treats it as a **toggle**.
`0x47` (OFF) has never been tried here; if it works, the lamp could get discrete on/off.

### Other Briloner remotes in Flipper-IRDB (address `0x00`, untested)

The same [folder](https://github.com/Lucaslhm/Flipper-IRDB/tree/main/LED_Lighting/Briloner)
has more files. Two of them (`Briloner_Leuchten_LED.ir`, `LED_Ceiling_Light_Briloner_Leuchten.ir`,
identical) use the same ON `0x45` as ours, so they're probably the same remote:

| Name        | Command | | Name        | Command |
|-------------|---------|-|-------------|---------|
| ON          | `0x45`  | | COLD        | `0x07`  |
| OFF         | `0x47`  | | WARM        | `0x09`  |
| MAX         | `0x5A`  | | CHANGE TEMP | `0x52`  |
| DIM         | `0x19`  | | NIGHT MODE  | `0x15`  |
| BRIGHT UP   | `0x40`  | | OFF 30 MIN  | `0x42`  |

`BRILONER_BN254818.ir` also uses address `0x00` / Power `0x45` but labels the codes
differently: Warm `0x40`, Cold `0x19`, 10% `0x0C`, 50% `0x18`, D2 `0x5A`, Mode `0x47`
(Brighter, Darker, 100%, Nightlight, D1 are raw only). Test on the real lamp to see which
labels are right.

Other files in the folder use different addresses (`NECext 0x00EF`, `NEC 0x04`, raw) and are
other models.

For a new Briloner button not in the file, read it from the remote with IRremote's
`ReceiveDump` (gives `Address=0x0 Command=0x..`, see below).

## Triones LED (BLE)

**Source not recorded.** Not IR: the ESP32 connects over BLE (MAC `53:02:02:00:04:BB`, MTU 23)
and writes to characteristic `FFD9` of service `FFD5`. Service `FFD0` / characteristic `FFD4`
is the notify channel (status replies), not used by the firmware.

| Command    | Bytes                          | Status |
|------------|--------------------------------|--------|
| ON         | `CC 23 33`                     | used |
| OFF        | `CC 24 33`                     | used |
| Colour     | `56 RR GG BB 00 F0 AA`         | used (no brightness command: scale RGB) |
| Mode       | `BB <mode> <speed> 44`         | from the public protocol, not tested |
| Status     | `EF 01 77` (reply on `FFD4`)   | from the public protocol, not tested |

Mode ids (from the old `nimbletriones.h`):

| Id | Mode | | Id | Mode |
|----|------|-|----|------|
| `0x25` (37) | seven colour cross fade | | `0x30` (48) | seven colour strobe flash |
| `0x26` (38) | red gradual change      | | `0x31` (49) | red strobe flash |
| `0x27` (39) | green gradual change    | | `0x32` (50) | green strobe flash |
| `0x28` (40) | blue gradual change     | | `0x33` (51) | blue strobe flash |
| `0x29` (41) | yellow gradual change   | | `0x34` (52) | yellow strobe flash |
| `0x2A` (42) | cyan gradual change     | | `0x35` (53) | cyan strobe flash |
| `0x2B` (43) | purple gradual change   | | `0x36` (54) | purple strobe flash |
| `0x2C` (44) | white gradual change    | | `0x37` (55) | white strobe flash |
| `0x2D` (45) | red green cross fade    | | `0x38` (56) | seven colour jumping change |
| `0x2E` (46) | red blue cross fade     | | `0x61` (97) | RGB fade |
| `0x2F` (47) | green blue cross fade   | | `0x62` (98) | RGB cycle |

`0x41` (65) only shows up in the status reply: solid colour set by the remote; it can't be set.

Public description of the protocol:
[saberlight Triones protocol](https://github.com/madhead/saberlight/blob/master/protocols/Triones/protocol.md).

**Finding the MAC:** scan with
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
2. Flash a dump sketch and open Serial Monitor at 115200:
   - **IRremoteESP8266** → *File → Examples → IRremoteESP8266 → IRrecvDumpV2*, set
     `kRecvPin = 14`. Prints the 32-bit value:
     ```
     Protocol  : SAMSUNG
     Code      : 0xE0E09966 (32 Bits)
     ```
     Use `Code` as-is with `sendSamsungMSB(code, 32)` / `sendNECMSB(code, 32)`
     (Samsung, Led TV).
   - **IRremote** → *File → Examples → IRremote → ReceiveDump*, set the receive pin to 14.
     Prints `Protocol=NEC Address=0x0 Command=0x45`, which matches
     `sendNEC(address, command, 0)` (Briloner).
3. Point the remote at the receiver and press the button. Press it a few times and check you
   get the same value each time.

If the protocol shows `UNKNOWN`, the library can't decode it; send the raw timings printed
by the dump with `sendRaw` instead.
