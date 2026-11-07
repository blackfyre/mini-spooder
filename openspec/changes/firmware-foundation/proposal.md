# Proposal

## Why

So far the repository has only the stock image in `baseline/`, its analysis (`baseline/README.md`, `docs/lafvin-comparison.md`, `docs/lafvin-motions.md`), a mise toolchain and the `backup`, `restore` and `monitor` tasks. It has no firmware. The other planned changes (motion-playback, drive-link, web-portal, handheld-pak) all need a firmware project that builds and flashes repeatably, and a host-test setup for hardware-free code. They also need one layer that drives the eight servos with the stock firmware's per-servo calibration, stays still until it is told to move, and falls back to a safe state that no other part of the firmware can override. This change provides that foundation and nothing above it.

## What Changes

- A PlatformIO project in `firmware/` for the Arduino ESP8266 core on a 4 MB board. Its flash layout keeps the stock positions of the EEPROM emulation sector (0x3FB000) and the SDK data (0x3FC000–0x3FFFFF).
- Start-up diagnostics on the serial console: a version banner, the reset cause, and the loaded calibration.
- A main loop that never blocks: each service runs on `millis()` timing within one documented per-pass budget, and `loop()` returns to the core on every pass.
- A hardware-free C++ library in `core/`, host-tested with Unity in a PlatformIO native env. It holds the servo model, the output modes, the pulse maths and the trim codec. Executable sources such as fuzz targets stay outside the library build.
- New mise tasks: `build`, `upload` (to `$ROBOT_PORT` only, never to an auto-detected port) and `test` (host tests, no board). The existing `backup`, `restore` and `monitor` tasks stay unchanged.
- A separate check build (`robot-check`) with a serial command console, used only by the hardware checks. The normal image never contains the console.
- The servo output layer:
  - eight servos on GPIO 15, 13, 12, 14, 2, 4, 5 and 16, the stock start-up order that the stock motion tables assume;
  - the stock pulse mapping, 1–180° to 400–2400 µs;
  - a per-servo safe range, applied after the trim;
  - boot-safe use of the strapping pins (GPIO15 is servo 0, GPIO2 is servo 4);
  - non-blocking release;
  - a token-free direct servo command for the web portal, accepted only while output is enabled.
- An output mode state machine with an overriding safe state. The robot boots still. Output is enabled only on behalf of an explicit motion or keyframe request, with a complete pose. After a reset or a safe state, that request must come from a drive-link session that started after output was disabled, or from the web portal while no session is live; builds made only for the on-robot checks may also enable output on behalf of their own commands. Any mode can enter the safe state, which releases every servo and invalidates every earlier writer until output is enabled again.
- Servo calibration in the stock format: one signed trim byte per servo in EEPROM bytes 0–7 of the 512-byte emulation sector, with an erased byte reading as −1 and the stock accepted range −124…+124. A trim is set per servo, all trims are reset together, and the trims are kept across restarts, uploads and a return to stock. A damaged trim byte puts the firmware into a reported fault mode with output disabled.
- A documented first-flash procedure (backup, then upload) and the return to stock (`mise run restore`), plus a hardware check with the legs off the ground that records each servo index's joint, direction and mechanical range.

## Capabilities

### New Capabilities

- `firmware-build`: the PlatformIO firmware project, its flash layout, the start-up diagnostics, the non-blocking main loop and its budget, the host-test setup, the check build, the mise `build`, `upload` and `test` tasks, the first-flash procedure and the return to stock.
- `servo-output`: driving the eight servos, which covers:
  - the index-to-GPIO map;
  - the pulse mapping;
  - the per-servo safe range;
  - the output modes and the safe state;
  - when output may be enabled again;
  - the strapping pins;
  - release;
  - the reported servo state.
- `servo-calibration`: the per-servo trims, which covers their stock storage format and range, how they apply to output, setting, reset, persistence, and the calibration fault.

### Modified Capabilities

None. `openspec/specs/` is empty.

## Impact

- New directories `firmware/` and `core/`, and a new document `docs/servo-joints.md` written by the hardware checks. `mise.toml` gains the new tasks. In `CLAUDE.md`, the Commands section gains the new tasks and the flashing procedure, and the Hardware section points to the confirmed joint map.
- New PlatformIO platform dependency: `espressif8266` (pinned), which PlatformIO downloads on the first build. No new mise tools.
- Hardware: the first `mise run upload` replaces the stock application on the robot. An upload does not write the calibration sector or the SDK data sector. Trims are stored in the stock format, so trims saved by the stock firmware are kept and used when this firmware is uploaded. `mise run restore` writes the full baseline image, which returns the sector to its erased stock state and so drops all trims. Every flash write needs the user's go-ahead.
- Later changes build on this one. motion-playback, drive-link and web-portal call the servo-output and servo-calibration interfaces, register their services with the main loop, and add their own sources and tests to `core/` and `firmware/`.
