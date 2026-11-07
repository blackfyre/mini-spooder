# Spec Delta

## Purpose

Repeatable build, flash, host-test and return-to-stock procedures for the robot firmware, so that every later change builds on one project layout and the robot can always be put back to its verified stock image.

## ADDED Requirements

### Requirement: Firmware build

`mise run build` SHALL build the robot firmware from the PlatformIO project in `firmware/` for the Arduino ESP8266 core, with the PlatformIO platform pinned to an exact version, and SHALL produce one flashable image at a fixed, documented path under `firmware/.pio/build/`. The project's own sources (in `firmware/` and `core/`) SHALL compile with all warnings enabled and treated as errors; warnings in the Arduino core or the PlatformIO packages SHALL NOT fail the build. The build SHALL need no robot connected.

#### Scenario: Clean checkout builds

- **WHEN** `mise install` and then `mise run build` run in a fresh clone with no robot connected
- **THEN** the build succeeds and the image exists at the documented path

#### Scenario: Warning in project source

- **WHEN** a source file in `firmware/src/` or `core/` contains code that raises a compiler warning, such as an unused variable
- **THEN** `mise run build` fails and names the file

### Requirement: Flash layout

The image SHALL declare a 4 MB flash in DIO mode at 40 MHz in its header, matching the robot's chip as recorded in `baseline/README.md`. The firmware's flash layout SHALL keep the EEPROM emulation sector at 0x3FB000–0x3FBFFF and the SDK data at 0x3FC000–0x3FFFFF, as the stock image does, and SHALL use no filesystem. `mise run build` SHALL check the built image and the linked layout and SHALL fail, naming the mismatch, when the header's flash size, mode or frequency differs, when the EEPROM sector starts anywhere other than 0x3FB000, or when the image would extend into 0x3FB000 or above.

#### Scenario: Header matches the chip

- **WHEN** `esptool image-info` reads the image that `mise run build` produced
- **THEN** it reports a flash size of 4 MB, DIO mode and 40 MHz

#### Scenario: Layout moved

- **WHEN** the project is configured with a linker layout whose EEPROM sector is not at 0x3FB000
- **THEN** `mise run build` fails and names the EEPROM sector address it found

### Requirement: Boot banner

After every reset, the firmware SHALL print one line on UART0 at 115200 baud, before any servo output starts, in the form `mini-spooder <version>`, where `<version>` identifies the source it was built from (the output of `git describe --always --dirty` at build time).

#### Scenario: Banner after reset

- **WHEN** the robot running this firmware is reset while `mise run monitor` is open
- **THEN** the console shows `mini-spooder <version>` with the version of the flashed build

### Requirement: Upload to the robot's port only

`mise run upload` SHALL build the firmware and, only if the build and its checks pass, write the image to the serial port named by `$ROBOT_PORT`. It SHALL NOT fall back to auto-detecting a port. When `$ROBOT_PORT` is empty or names a path that does not exist, it SHALL fail before writing anything, with a message naming the variable. The upload SHALL write only the image, starting at 0x000000, and SHALL NOT erase or write 0x3FB000–0x3FFFFF.

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

`mise run test` SHALL compile every C++ source under `core/` for the host and run the Unity tests for `core/` in a PlatformIO native env. It SHALL need neither a robot nor the ESP8266 toolchain, and SHALL fail when any test fails or any `core/` source fails to compile. The `core/` sources SHALL compile with all warnings enabled and treated as errors, and SHALL build without the Arduino core, so that a `core/` source that includes an Arduino or ESP8266 header fails `mise run test`.

#### Scenario: Failing test

- **WHEN** a host test's assertion fails
- **THEN** `mise run test` fails and names the test

#### Scenario: Hardware header in core

- **WHEN** a source under `core/` includes `Arduino.h`
- **THEN** `mise run test` fails to compile that source and fails

#### Scenario: New source compiled without listing

- **WHEN** a new `.cpp` file is added under `core/` and nothing else is changed
- **THEN** `mise run test` compiles it

### Requirement: First flash and return to stock

`CLAUDE.md` SHALL document the first flash of this firmware as: `mise run backup`, then `mise run upload`, each run only with the user's go-ahead, since both reset the board and the upload writes to flash. `mise run restore` SHALL stay the documented way back to stock: it checks the SHA-256 of `baseline/original-flash-4MB.bin` and writes the full 4 MB image, which also returns the EEPROM emulation sector to its erased stock state and the SDK data to the stock access point configuration.

#### Scenario: Return to stock

- **WHEN** `mise run restore` runs on a robot running this firmware, and `mise run backup` runs after it
- **THEN** the backup is byte-identical to `baseline/original-flash-4MB.bin` from 0x000000 to 0x3FBFFF, and the robot offers the access point `Spider Robot` again

#### Scenario: Corrupted baseline

- **WHEN** `mise run restore` runs and the baseline file's SHA-256 differs from the recorded hash
- **THEN** the task fails and nothing is written to the robot
