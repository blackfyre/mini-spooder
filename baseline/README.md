# Original firmware baseline

Read from the robot on 2026-11-07 over the board's CP2102 USB-to-UART adapter. Nothing was written to the board. A second full read was byte-identical.

## Files

| File | Content |
|---|---|
| `original-flash-4MB.bin` | Full flash image, 0x000000–0x3FFFFF |
| `portal-strings.txt` | Readable strings of the web portal (routes, pages, JavaScript), in image order |

SHA-256 of `original-flash-4MB.bin`:

```
a477d40f405df5dc50b64d7fff90c7d89173d6c750f351aa12084e1bc1f143f7
```

Restore the original firmware with `mise run restore`.

## Chip

| Property | Value |
|---|---|
| Chip | ESP8266EX, 26 MHz crystal |
| MAC | `c8:c9:a3:64:0e:26` |
| Flash | 4 MB, manufacturer ID `0x68`, device `0x4016`; DIO, 40 MHz |

## Flash layout

| Range | Content |
|---|---|
| 0x000000–0x000FFF | `eboot` bootloader (Arduino ESP8266 core); prints `v39c79d9b` at 74880 baud |
| 0x001000–0x04DFFF | Application, about 308 KB |
| 0x04E000–0x3FBFFF | Erased; no filesystem in use |
| 0x3FB000–0x3FBFFF | EEPROM emulation sector: erased, so no calibration has been saved |
| 0x3FC000–0x3FFFFF | SDK data: RF calibration and Wi-Fi configuration |

The SDK Wi-Fi configuration holds the access point `Spider Robot` with password `12345678`, and the station hostname `ESP-640E26`. The SDK version string is `2.2.2-dev(38a443e)`.

## Firmware behaviour

- **Build:** Arduino ESP8266 core. The boot banner is `Q1 mini Start!`.
- **Serial:** nothing readable after boot at 115200 baud.
- **Network:** soft access point `Spider Robot` with a DNS server for the captive portal. The `.local` string suggests mDNS (unconfirmed).

### HTTP routes

| Route | Use |
|---|---|
| `/` | Controller page (assumed: too short to appear in the strings) |
| `/controller?pm=<n>` | Run motion `n` |
| `/controller?pms=<n>` | Variant of `pm`; purpose unknown |
| `/controller?servo=<i>&value=<deg>` | Move servo `i` to an angle |
| `/save?key=<i>&value=<v>` | Save a servo calibration; `key=100` resets all |
| `/editor` | Motion editor page |
| `/zero` | Zero check page |
| `/setting` | Servo calibration page |
| `/online` | Unknown |

The route list comes from the strings. How each handler behaves is inferred from the page JavaScript, not from disassembly.

### Motions (`pm` values)

| `pm` | Motion |
|---|---|
| 1 | Standby |
| 2 | Forward |
| 3 | Backward |
| 4 | Left shift |
| 5 | Right shift |
| 6 | Turn left |
| 7 | Turn right |
| 8 | Lie |
| 9 | Say Hi |
| 10 | Fighting |
| 11 | Push up |
| 12 | Sleep |
| 13–15 | Dance 1–3 |
| 100 | Zero position |

### Servo map

The calibration page labels each servo index with its GPIO.

| Servo index | GPIO | Default angle in page | Page group |
|---|---|---|---|
| 0 | 14 | 70 | yellow |
| 1 | 12 | 90 | blue |
| 2 | 13 | 90 | blue |
| 3 | 15 | 110 | yellow |
| 4 | 16 | 110 | yellow |
| 5 | 5 | 90 | blue |
| 6 | 4 | 90 | blue |
| 7 | 2 | 70 | yellow |

The two colour groups probably separate the hip and knee joints. Which servo sits on which leg is not yet confirmed on the hardware. Sliders range from 45° to 135°.

Hardware note: GPIO15 (must be low at boot) and GPIO2 (must be high at boot) are boot-strapping pins, so their servos may twitch at reset. GPIO2 also drives the module's blue LED on most ESP-12 boards.

## References

- **LAFVIN Quadruped Spider Robot kit** — <https://lafvin-quadruped-spider-robot.readthedocs.io/en/latest/>. Its course code uses the same servo pin order (`14, 12, 13, 15, 16, 5, 4, 2`), which suggests that this robot is this kit or a clone of it. Its course pages name MG90 servos, an 18650 cell and an expansion board. The documentation links to a source and library download, which has not been inspected.
- **Q1 mini by Jason Workshop** — <https://www.myminifactory.com/object/3d-print-35704>, <https://q1robot10.blogspot.com/2019/05/q1mini.html>. This is the original 8-DOF design that the boot banner names. It ran on a LinkIt 7697, not an ESP8266, so the firmware on this robot is probably a port.
- **kabuku/jbotq1** — <https://github.com/kabuku/jbotq1>. An Android Things port of the Q1 mini's motion code.
- **miniKame** — <https://github.com/JavierIH/miniKame>. An independent 8-DOF ESP8266 quadruped with oscillator-based gaits.
- **Arduino ESP8266 core** — <https://github.com/esp8266/Arduino>. The framework that the original firmware was built with.
