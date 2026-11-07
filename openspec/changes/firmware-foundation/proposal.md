# Proposal

## Why

The repository has no firmware yet: only the stock image in `baseline/`, a mise toolchain and the `backup`, `restore` and `monitor` tasks. The other planned changes (motion-playback, drive-link, web-portal, handheld-pak) all need a firmware project that builds and flashes repeatably, a host-test setup for hardware-free code, and one layer that drives the eight servos and applies the stock firmware's per-servo calibration. This change provides that foundation, and nothing above it.

## What Changes

- A PlatformIO project in `firmware/` for the Arduino ESP8266 core on a 4 MB board, with a flash layout that keeps the stock positions of the EEPROM emulation sector (0x3FB000) and the SDK data (0x3FC000–0x3FFFFF), and a boot banner on the serial console.
- A hardware-free C++ library in `core/`, host-tested with Unity in a PlatformIO native env, holding the servo angle and pulse maths and the calibration record codec.
- New mise tasks: `build`, `upload` (to `$ROBOT_PORT` only, never an auto-detected port) and `test` (host tests, no board). The existing `backup`, `restore` and `monitor` tasks stay unchanged.
- A documented first-flash procedure (backup, then upload) and return to stock (`mise run restore`).
- The servo output layer: eight servos on GPIO 14, 12, 13, 15, 16, 5, 4 and 2, angles 0–180°, the stock pulse mapping, no output before the first complete pose, boot-safe handling of the strapping pins GPIO15 and GPIO2, and release (limp) per servo or for all.
- Servo calibration as the stock image shows it: one signed offset in whole degrees per servo, applied to every output, set per servo, reset for all, and kept across restarts and firmware uploads in the EEPROM emulation sector.
- A temporary boot pose (the stock pages' default angles) so that this change can be checked on the robot before motion-playback exists.

## Capabilities

### New Capabilities

- `firmware-build`: the PlatformIO firmware project, its flash layout, the host-test setup, the mise `build`, `upload` and `test` tasks, the first-flash procedure and the return to stock.
- `servo-output`: driving the eight servos: index-to-GPIO map, angle range, pulse mapping, start-up without stray output, the strapping pins, release, and the reported servo state.
- `servo-calibration`: per-servo offsets: their range, how they apply to output, setting, reset, and persistence.

### Modified Capabilities

None. `openspec/specs/` is empty.

## Impact

- New directories `firmware/` and `core/`; new tasks in `mise.toml`; the Commands section of `CLAUDE.md` gains the new tasks and the flashing procedure.
- New PlatformIO platform dependency: `espressif8266` (pinned), downloaded by PlatformIO on first build. No new mise tools.
- Hardware: the first `mise run upload` replaces the stock application on the robot. Calibration and SDK data sectors are not written by an upload; `mise run restore` writes the complete stock image back. Every flash write needs the user's go-ahead.
- Later changes build on this one: motion-playback, drive-link and web-portal call the servo-output and servo-calibration interfaces, and add their own sources and tests to `core/` and `firmware/`.
