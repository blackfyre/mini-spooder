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
| 0x3FB000–0x3FBFFF | EEPROM emulation sector: calibration trims in bytes 0–7; erased, so no calibration has been saved |
| 0x3FC000–0x3FFFFF | SDK data: RF calibration and Wi-Fi configuration |

The SDK Wi-Fi configuration holds the access point `Spider Robot` with password `12345678`, and the station hostname `ESP-640E26`. The SDK version string is `2.2.2-dev(38a443e)`.

## Firmware behaviour

Corrected after the disassembly in `docs/lafvin-comparison.md`, which has the evidence (flash offsets) for each point below.

- **Build:** Arduino ESP8266 core, probably close to release 2.7.4 (inferred from the Servo library code). The boot banner is `Q1 mini Start!`.
- **Serial:** 9600 baud. The firmware prints the banner at boot and nothing else; this is why nothing was readable at 115200.
- **Network:** soft access point `Spider Robot`, password `12345678`, no explicit IP configuration, so presumably the SDK default 192.168.4.1 (not measured). It is **not** a captive portal: no DNS server or mDNS is started.
- **Motions:** each `pm` request plays its table once, then stops. While a motion plays, the firmware does not serve HTTP, so a motion can't be interrupted.

### HTTP routes

| Route | Use |
|---|---|
| `/` | Controller page (assumed: too short to appear in the strings) |
| `/controller?pm=<n>` | Run motion `n` |
| `/controller?pms=<n>` | Not handled: the page's JavaScript sends it, but the handler reads only `pm`, `servo` and `value` |
| `/controller?servo=<i>&value=<deg>` | Move servo `i` to an angle, immediately |
| `/save?key=<i>&value=<v>` | Save servo `i`'s calibration trim; `key=100` resets all (its page button is commented out) |
| `/editor` | Motion editor page: eight sliders (45–135°) and a generated comma-separated keyframe |
| `/zero` | Zero check page |
| `/setting` | Servo calibration page |
| `/online?m0=…&m7=&t1=` | Plays one keyframe: eight angles over `t1` ms. No stock page sends it |

Every route is registered for GET. Unknown `pm` values are accepted and ignored.

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
| 99 | Centre: on stock the same pose as zero (no button) |
| 100 | Zero position |

The keyframe tables are in the application image (flash 0x3FC9C–0x40DE7): each keyframe is eight angles plus a duration in milliseconds, interpolated in 10 ms steps. Say Hi ends with Standby, and Sleep starts with Standby. `docs/lafvin-motions.md` lists every keyframe.

### Servo map

The pages' labels and the GPIO each servo index is actually attached to disagree, and stock changes the mapping after a save:

| Servo index | GPIO in page labels | GPIO at startup | GPIO after any `/save` | Default angle in page | Page group |
|---|---|---|---|---|---|
| 0 | 14 | 15 | 16 | 70 | yellow |
| 1 | 12 | 13 | 5 | 90 | blue |
| 2 | 13 | 12 | 4 | 90 | blue |
| 3 | 15 | 14 | 2 | 110 | yellow |
| 4 | 16 | 2 | 14 | 110 | yellow |
| 5 | 5 | 4 | 12 | 90 | blue |
| 6 | 4 | 5 | 13 | 90 | blue |
| 7 | 2 | 16 | 15 | 70 | yellow |

The startup and after-save columns come from the disassembled attach calls. The after-save remapping, which happens even for an invalid save, is a stock defect. The vendor (LAFVIN) source always uses 14, 12, 13, 15, 16, 5, 4, 2, which is the label column. Which GPIO drives which joint on this robot is **not yet confirmed on the hardware**. The two colour groups probably separate the hip and knee joints.

### Calibration

Calibration is real. It stores one signed byte per servo index (a trim added to every angle) in EEPROM bytes 0–7, in the 512-byte EEPROM emulation sector at 0x3FB000. In the dump that sector is erased, so every trim currently reads as −1. Angles pass through a 1–180 to 400–2400 µs conversion.

Hardware note: GPIO15 (must be low at boot) and GPIO2 (must be high at boot) are boot-strapping pins, so their servos may twitch at reset. GPIO2 also drives the module's blue LED on most ESP-12 boards.

## References

- **LAFVIN Quadruped Spider Robot kit** — <https://lafvin-quadruped-spider-robot.readthedocs.io/en/latest/>. Its course code uses the same servo pin order (`14, 12, 13, 15, 16, 5, 4, 2`), which suggests that this robot is this kit or a clone of it. Its course pages name MG90 servos, an 18650 cell and an expansion board. The documentation links to a source and library download, which has not been inspected.
- **Q1 mini by Jason Workshop** — <https://www.myminifactory.com/object/3d-print-35704>, <https://q1robot10.blogspot.com/2019/05/q1mini.html>. This is the original 8-DOF design that the boot banner names. It ran on a LinkIt 7697, not an ESP8266, so the firmware on this robot is probably a port.
- **kabuku/jbotq1** — <https://github.com/kabuku/jbotq1>. An Android Things port of the Q1 mini's motion code.
- **miniKame** — <https://github.com/JavierIH/miniKame>. An independent 8-DOF ESP8266 quadruped with oscillator-based gaits.
- **Arduino ESP8266 core** — <https://github.com/esp8266/Arduino>. The framework that the original firmware was built with.
