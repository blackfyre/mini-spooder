# Stock motions

Where each stock motion lives in `baseline/original-flash-4MB.bin`, how the stock firmware plays it, and what the new player does differently. The motion data in `core/motion/stock_motions.h` is generated from the image by `tools/extract_motions.py` (`mise run extract-motions`); `mise run check-motions` fails when the header and the image differ. Requirements: `openspec/changes/motion-playback/specs/motion-playback/spec.md`; decisions: its `design.md`.

Offsets are flash offsets into the full image; flash offset `x` is the instruction-ROM address `0x40200000 + x`. "V" marks a fact verified in the cited source; "I" an inference.

## Evidence

| Finding | Where | Source |
|---|---|---|
| Keyframes are rows of nine little-endian 32-bit integers: angles for servos 0–7, then a duration in ms | tables 0x3FC9C–0x40DE7; centre pose 0x40DE8; zero pose 0x40E0C | V: `docs/lafvin-comparison.md`, `docs/lafvin-motions.md` (Evidence and units) |
| The motion dispatch loads one table address and one step-count address per `pm` 1–15 | literal pairs at 0x500C–0x5083 | V: `docs/lafvin-motions.md`; the table-to-`pm` mapping is I, high confidence. `extract_motions.py` checks that the literals equal its fixed offsets |
| Step counts, 32-bit integers from `pm` 15 down to `pm` 1: 10, 9, 10, 2, 11, 11, 7, 1, 8, 8, 11, 11, 11, 11, 2 | 0x4AE84 (RAM 0x3FFE84CC) | Found by scan; consistent with the table boundaries (the generator checks that the 15 tables tile 0x3FC9C–0x40DE7 exactly) |
| `pm` 9 runs the Say Hi table and then the Standby table; `pm` 12 runs the Standby table and then the Sleep table | dispatch 0x5169–0x517A, 0x5191–0x51A2 | V: `docs/lafvin-motions.md` (Playback, delays and repeats) |
| `pm` 99 and `pm` 100 write the centre and the zero pose to servos 0–7 with `delay(10)` after each, then `delay(300)`; both poses are 135, 45, 135, 45, 45, 135, 45, 135 | writers 0x1380, 0x13D0; dispatch 0x5106–0x5115, 0x51C4–0x51D3 | V: `docs/lafvin-motions.md` (Servo column mapping and direct poses) |
| The stock setup runs the zero routine at boot | ROM 0x40201FD8 | `design.md` Context; not reproduced (below) |
| The keyframe loop: `floor(T / 10)` steps from index 0, each writing an interpolated angle only to servos whose angle changes, then `delay(10)`; the keyframe angle itself is never written, but the cached pose becomes the keyframe | 0x1420–0x14FE | V: `docs/lafvin-motions.md`, `docs/lafvin-comparison.md` (Motion servicing) |
| The keyframe loop does not call the web server, so stock motions block every request until they end | 0x1420–0x14FE | V: `docs/lafvin-comparison.md` |
| `/online` reads `m0`–`m7` and `t1` and calls the same runner with one keyframe; no stock page sends it | 0x3920–0x3B03 | V: `baseline/README.md` |
| The servo write adds a signed EEPROM byte at the servo's index, then maps 1–180° to 400–2400 µs | 0x1304–0x132E | V: `docs/lafvin-motions.md`, `baseline/README.md` |
| Unknown `pm` values are accepted and ignored; `pms` is never handled | 0x3814–0x38F4, dispatch 0x5084–0x51E3 | V: `baseline/README.md` |

## Motions

Ticks are 10 ms; a keyframe of `T` ms takes `floor(T / 10)` ticks (at least one). The keyframe counts are those of the generated header; the test `core/test/test_motion_catalogue` pins them.

| `pm` | Motion | Table | Step count | Keyframes | Played as | Ticks |
|---|---|---|---|---|---|---|
| 1 | Standby | 0x40DA0 | 0x4AEBC | 2 | Standby table | 40 |
| 2 | Forward | 0x40C14 | 0x4AEB8 | 11 | Forward table | 165 |
| 3 | Backward | 0x40A88 | 0x4AEB4 | 11 | Backward table | 165 |
| 4 | Left shift | 0x408FC | 0x4AEB0 | 11 | Left shift table | 165 |
| 5 | Right shift | 0x40770 | 0x4AEAC | 11 | Right shift table | 165 |
| 6 | Turn left | 0x40650 | 0x4AEA8 | 8 | Turn left table | 120 |
| 7 | Turn right | 0x40530 | 0x4AEA4 | 8 | Turn right table | 120 |
| 8 | Lie | 0x4050C | 0x4AEA0 | 1 | Lie table | 50 |
| 9 | Say Hi | 0x40410 | 0x4AE9C | 7 | Say Hi table, then Standby table | 280 + 40 = 320 |
| 10 | Fighting | 0x40284 | 0x4AE98 | 11 | Fighting table | 407 |
| 11 | Push up | 0x400F8 | 0x4AE94 | 11 | Push up table | 718 |
| 12 | Sleep | 0x400B0 | 0x4AE90 | 2 | Standby table, then Sleep table | 40 + 74 = 114 |
| 13 | Dance 1 | 0x3FF48 | 0x4AE8C | 10 | Dance 1 table | 300 |
| 14 | Dance 2 | 0x3FE04 | 0x4AE88 | 9 | Dance 2 table | 270 |
| 15 | Dance 3 | 0x3FC9C | 0x4AE84 | 10 | Dance 3 table | 300 |
| 99 | Centre | 0x40DE8 (pose) | — | — | direct pose: 8 writes, 30-tick hold | 38 |
| 100 | Zero position | 0x40E0C (pose) | — | — | direct pose: 8 writes, 30-tick hold | 38 |

The first motion after a reset or a safe state takes one tick more: the tick that sends servo output the enable request with the zero pose (design D11).

## Stock behaviour that the player does not reproduce

- **Boot zero routine.** Stock runs the zero routine in its setup, so every servo jumps to the zero pose at boot. The new firmware moves nothing until its first command (design D7); the zero pose is kept as the pose that output is enabled with.
- **Endpoint shortfall.** Stock interpolates from step index 0 and never writes the keyframe angle, so a servo can stop up to 3° short (servo 0 is commanded to 71, not 70, after Standby from the zero pose) while the cached pose records the keyframe. The player numbers ticks `1 … n` and writes `A ± floor((i·|B − A| + floor(n/2)) / n)`, so the last tick writes the keyframe angle exactly; the tick counts stay stock's (design D3). `core/test/test_motion_trace` checks this against a reference model of the stock loop.
- **Blocking playback.** Stock serves no HTTP request while a motion plays. The player runs one tick per loop pass when one is due, so the portal and the drive link stay served, a motion can be stopped, and a request during a motion waits in a single pending slot, latest wins (design D4, D5).
- **Unknown `pm` values.** Stock accepts and ignores them; the player rejects them and changes nothing (design D8).

## On-robot results

Not yet run. Tasks 5.1–5.5 of the change record here: the prerequisite checks (firmware-foundation 6.1–6.5, drive-link 6.1–6.3), the `motion-demo` log checked by `tools/check_motion_log.py` (with the maximum service time), the servo directions and extremes, and the per-motion comparison with stock (pose sequence and total duration within 10%).

### Running the demonstration build (each flash write only with the user's go-ahead)

1. Lift the robot so that no leg touches the ground.
2. `mise exec -- pio run -d firmware -e motion-demo -t upload --upload-port "$ROBOT_PORT"`.
3. `mise run monitor`, capturing the output to a file. For 10 s after the reset no servo moves and no `motion` line appears.
4. Send `go`. The demo prints `mode active check`, one `motion …` line per run, `command …` lines, one `keyframe …` line and `demo done tick_max_us <n>`.
5. `python -I tools/check_motion_log.py <captured log>`.
