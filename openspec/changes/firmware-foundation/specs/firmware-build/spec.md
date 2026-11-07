# Spec Delta

## Purpose

Repeatable build, flash, host-test and return-to-stock procedures for the robot firmware, start-up diagnostics, and one non-blocking main loop that every firmware service shares. Every later change builds on one project layout, and the robot can always be put back to its verified stock image.

## ADDED Requirements

### Requirement: Firmware build

`mise run build` SHALL build the robot firmware from the PlatformIO project in `firmware/` for the Arduino ESP8266 core, with the PlatformIO platform pinned to an exact version. It SHALL produce one flashable image at a fixed, documented path under `firmware/.pio/build/`. The project's own sources (in `firmware/` and `core/`) SHALL compile with all warnings enabled and treated as errors. Warnings in the Arduino core or the PlatformIO packages SHALL NOT fail the build. The build SHALL need no robot connected.

#### Scenario: Clean checkout builds

- **WHEN** `mise install` and then `mise run build` run in a fresh clone with no robot connected
- **THEN** the build succeeds and the image exists at the documented path

#### Scenario: Warning in project source

- **WHEN** a source file in `firmware/src/` or `core/` contains code that raises a compiler warning, such as an unused variable
- **THEN** `mise run build` fails and names the file

### Requirement: Flash layout

The image SHALL declare a 4 MB flash in DIO mode at 40 MHz in its header, matching the robot's chip as recorded in `baseline/README.md`. The firmware's flash layout SHALL keep the EEPROM emulation sector at 0x3FB000–0x3FBFFF and the SDK data at 0x3FC000–0x3FFFFF, as the stock image does, and SHALL use no filesystem. `mise run build` SHALL check the built image and the linked layout. It SHALL fail, naming the mismatch, in any of these cases:

- the header's flash size, mode or frequency differs;
- the EEPROM sector starts anywhere other than 0x3FB000;
- the image would extend into 0x3FB000 or above.

#### Scenario: Header matches the chip

- **WHEN** `esptool image-info` reads the image that `mise run build` produced
- **THEN** it reports a flash size of 4 MB, DIO mode and 40 MHz

#### Scenario: Layout moved

- **WHEN** the project is configured with a linker layout whose EEPROM sector is not at 0x3FB000
- **THEN** `mise run build` fails and names the EEPROM sector address it found

### Requirement: Start-up diagnostics

After every reset, before any other output on UART0 from this firmware, the firmware SHALL print these lines on UART0 at 115200 baud, in this order:

1. `mini-spooder <version>`, where `<version>` identifies the source it was built from (the output of `git describe --always --dirty` at build time);
2. `reset <cause>`, where `<cause>` is the SDK's reset reason: `power-on`, `hardware-watchdog`, `exception`, `software-watchdog`, `software-restart`, `deep-sleep-wake` or `external`, or `unknown-<n>` for any other value `n`. For `exception`, `hardware-watchdog` and `software-watchdog`, the line SHALL add the SDK's `exccause` and `epc1` values;
3. the calibration line (servo-calibration).

The firmware SHALL NOT drive any servo before these lines are printed. The stock firmware's 9600-baud `Q1 mini Start!` banner is not reproduced. The ESP8266 reports a supply brown-out as `power-on`, so a `reset power-on` line during a session means that the supply dipped.

#### Scenario: Banner and cause after an external reset

- **WHEN** the robot running this firmware is reset by esptool while `mise run monitor` is open
- **THEN** the console shows `mini-spooder <version>` with the version of the flashed build, then `reset external`, then the calibration line

#### Scenario: Watchdog reset reported

- **WHEN** the check build's `hang` command makes the loop stop returning until the software watchdog resets the chip
- **THEN** the next start-up prints `reset software-watchdog` with its `exccause` and `epc1` values

### Requirement: Non-blocking main loop

The firmware SHALL run as one cooperative main loop. Each service SHALL be a poll function that `main.cpp` calls on every pass with the current `millis()` time. Services include servo output, calibration, and the services that later changes add (link receive and failsafe, keyframe player, web server). A service SHALL do only the work that is due, and SHALL time its periodic work by advancing its deadline by its interval, not by the time it ran. No service SHALL call `delay()` or wait for a condition. `loop()` SHALL return to the core after every pass so that the Wi-Fi stack runs.

The loop budget SHALL be: every pass, with all services together, takes at most 20 ms. The only exception SHALL be a pass that commits calibration to flash (servo-calibration). The firmware SHALL record the longest pass since start-up, the number of passes over 20 ms that did not commit calibration, and the longest calibration-commit pass, and SHALL make these available to the rest of the firmware. Each 40 ms deadline of servo-output and servo-calibration SHALL be extended by the length of any calibration-commit pass that runs within it; no other pass extends it.

#### Scenario: Release within the budget

- **WHEN** all eight servos are released in one pass while the check build reports its loop statistics
- **THEN** no pass after the release takes longer than 20 ms, and the over-budget count is unchanged

#### Scenario: Calibration commit measured

- **WHEN** a trim is set through the check build and its loop statistics are then read
- **THEN** the longest calibration-commit pass is reported separately, and the over-budget count is unchanged

### Requirement: Upload to the robot's port only

`mise run upload` SHALL build the firmware. Only if the build and its checks pass SHALL it write the image to the serial port named by `$ROBOT_PORT`. It SHALL NOT fall back to auto-detecting a port. When `$ROBOT_PORT` is empty or names a path that does not exist, it SHALL fail before writing anything, with a message naming the variable. The upload SHALL write only the image, starting at 0x000000, and SHALL NOT erase or write 0x3FB000–0x3FFFFF.

#### Scenario: Port missing

- **WHEN** `mise run upload` runs with `$ROBOT_PORT` set to a path that does not exist, while another ESP board is connected as `/dev/ttyUSB0`
- **THEN** the task fails with a message naming `ROBOT_PORT`, and nothing is written to any board

#### Scenario: Failed build

- **WHEN** `mise run upload` runs and the build or its layout check fails
- **THEN** the task fails and nothing is written to the robot

#### Scenario: Calibration and SDK sectors kept

- **WHEN** `mise run backup` runs before and after a `mise run upload`
- **THEN** the two backups are byte-identical from 0x3FB000 to 0x3FFFFF

### Requirement: Host tests

`mise run test` SHALL compile the `core/` library for the host and run the Unity tests in `core/test/` in a PlatformIO native env. It SHALL need neither a robot nor the ESP8266 toolchain. It SHALL fail when any test fails or any library source fails to compile. The library SHALL consist of every C++ source under `core/` except those under `core/test/` and `core/fuzz/`, compiled without a source list, in both the native env and the robot env. Sources under `core/fuzz/` (fuzz targets and other executables with their own entry point) SHALL be compiled by neither env. The library sources SHALL compile with all warnings enabled and treated as errors, and SHALL build without the Arduino core, so that a library source that includes an Arduino or ESP8266 header fails `mise run test`.

#### Scenario: Failing test

- **WHEN** a host test's assertion fails
- **THEN** `mise run test` fails and names the test

#### Scenario: Hardware header in core

- **WHEN** a library source under `core/` includes `Arduino.h`
- **THEN** `mise run test` fails to compile that source and fails

#### Scenario: New source compiled without listing

- **WHEN** a new `.cpp` file is added under `core/` outside `core/test/` and `core/fuzz/`, and nothing else is changed
- **THEN** `mise run test` and `mise run build` both compile it

#### Scenario: Fuzz targets kept out of the library

- **WHEN** two files under `core/fuzz/` each define `LLVMFuzzerTestOneInput`, and one also defines `main`
- **THEN** `mise run test` and `mise run build` both succeed, and neither the robot image nor the native test programme contains those symbols

### Requirement: Check build

A third PlatformIO env, `robot-check`, SHALL build the normal firmware plus a line-based command console on UART0 at 115200 baud, for the hardware checks only. The console SHALL call the same servo-output and servo-calibration operations as the rest of the firmware, with no other access to the pins or the sector. It SHALL offer at least these commands:

- enable with a complete pose;
- a pose or a single servo with the current token;
- release one servo or all;
- a safe-state request with reason `check`;
- set one trim;
- reset all trims;
- print the servo state report, the trims, the safe ranges and the loop statistics;
- restart (`ESP.restart()`);
- `hang`, which stops returning from `loop()`.

A malformed command SHALL be answered with an error line and SHALL change nothing. The console SHALL respect the output modes exactly as any other caller does. `mise run build` and `mise run upload` SHALL never build or write the check build, and the normal image SHALL contain no console code.

#### Scenario: Console absent from the normal image

- **WHEN** `mise run build` has built the normal image
- **THEN** the toolchain's `nm` lists no symbol of the check console in it

#### Scenario: Malformed command

- **WHEN** the console receives `servo 3` without an angle
- **THEN** it prints an error line, and no servo's output or mode changes

### Requirement: First flash and return to stock

`CLAUDE.md` SHALL document the first flash of this firmware as: `mise run backup`, then `mise run upload`, each run only with the user's go-ahead, since both reset the board and the upload writes to flash. `mise run restore` SHALL stay the documented way back to stock. It checks the SHA-256 of `baseline/original-flash-4MB.bin` and writes the full 4 MB image. That also returns the EEPROM emulation sector to its erased stock state, dropping every trim, and returns the SDK data to the stock access point configuration.

#### Scenario: Return to stock

- **WHEN** `mise run restore` runs on a robot running this firmware, and `mise run backup` runs after it
- **THEN** the backup is byte-identical to `baseline/original-flash-4MB.bin` from 0x000000 to 0x3FBFFF, and the robot offers the access point `Spider Robot` again

#### Scenario: Corrupted baseline

- **WHEN** `mise run restore` runs and the baseline file's SHA-256 differs from the recorded hash
- **THEN** the task fails and nothing is written to the robot
