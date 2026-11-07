# Tasks

Every step that writes to the robot's flash or resets it (`mise run backup`, `mise run upload`, `mise run restore`, any `esptool` command) needs the user's go-ahead first.

## 1. PlatformIO project and core library

- [ ] 1.1 Create `firmware/platformio.ini` with the `robot` and `native` environments of design D1, pinning `espressif8266` and `native` to their newest releases at implementation time; add `firmware/src/main.cpp` with an empty `setup()`/`loop()`. Verify that `mise exec -- pio run -d firmware -e robot` succeeds without a robot connected.
- [ ] 1.2 Create `core/library.json` (warnings as errors for its sources) and a first `core/test/test_smoke/` Unity test; reference `core/` from both environments with `lib_deps = symlink://../core` and `lib_ldf_mode = off`. Verify that `mise exec -- pio test -d firmware -e native` runs and passes the smoke test.
- [ ] 1.3 Verify the hardware-free rule: temporarily add `#include <Arduino.h>` to a `core/` source and confirm `pio test -d firmware -e native` fails, then remove it; temporarily add an unused variable to a `core/` source and to `firmware/src/main.cpp` and confirm each fails its build, then remove it.

## 2. Build checks, banner and mise tasks

- [ ] 2.1 Add `firmware/scripts/version.py` (D2) and print `mini-spooder <version>` at 115200 baud at the start of `setup()`. Verify with `strings firmware/.pio/build/robot/firmware.bin | grep mini-spooder` that the image carries the `git describe --always --dirty` output.
- [ ] 2.2 Add `firmware/scripts/check_image.py` (D2) with its parsing and comparison as pure functions, and `firmware/scripts/test_check_image.py` covering: a matching header and layout; flash size, mode and frequency mismatches; `_EEPROM_start` other than 0x405FB000; a non-empty filesystem range; an image reaching 0x3FB000; a missing symbol. Verify with `python -m unittest -v test_check_image` in `firmware/scripts/`.
- [ ] 2.3 Add the `build`, `upload` and `test` tasks to `mise.toml` as in D3, leaving `backup`, `restore` and `monitor` unchanged. Verify that `mise run build` passes and prints the check's result, that `mise run test` runs the Unity and Python tests, and that `ROBOT_PORT=/nonexistent mise run upload` fails naming `ROBOT_PORT` before any esptool call.
- [ ] 2.4 Verify the layout check end to end: temporarily set `board_build.ldscript = eagle.flash.4m1m.ld` (a 4 MB layout with a 1 MB filesystem) and confirm `mise run build` fails naming the filesystem range; restore `eagle.flash.4m.ld`. Verify `esptool image-info` on the built image reports 4 MB, DIO, 40 MHz.

## 3. Servo model in core

- [ ] 3.1 Implement `core/servo/servo_bank` (D4): servo indices 0–7, complete pose, single-servo command, clamping to 0–180°, release of one or all, pulse width `round(544 + 1856 × out / 180)` µs, and the state report. Verify with `core/test/test_servo_bank/` covering every servo-output scenario that needs no pin: index 8 rejected; 90° → 1472 µs; 0° → 544 µs and 180° → 2400 µs; 200° → 180°; single command before the first pose rejected; report before the first pose; report after a pose with servo 2 released; release all; resume after release; releasing a released servo changes nothing.
- [ ] 3.2 Add the offset rules to the model: output = clamp(commanded + offset, 0, 180), offsets within −30…+30, set one (accepted or rejected), reset all, and a "table changed" result. Verify with `core/test/test_servo_bank/` cases: offset −5 at 90° → 85° and 1420 µs with commanded 90° reported; +5 at 178° → 180°; offset change on a driven servo changes its width without a new command; +31 and index 9 rejected with the table unchanged; setting the current value reports no change; reset zeroes all.

## 4. Calibration record

- [ ] 4.1 Implement `core/calibration/calibration_record` (D5): encode and decode the 15-byte record with CRC-16/CCITT-FALSE. Verify with `core/test/test_calibration_record/`: round trip; all-`0xFF` sector → zeros and `default`; wrong identifier, version or CRC → zeros and `default`; an offset of +31 in an otherwise valid record → zeros and `default`; the encoding of the table −3, 0, 0, 0, −7, 0, 0, +12 equals `core/test/fixtures/calibration_record_v1.bin` byte for byte.
- [ ] 4.2 Add `firmware/scripts/make_calibration_record.py` (writes a record file for eight offsets) and `firmware/scripts/test_make_calibration_record.py`, which checks that its output for −3, 0, 0, 0, −7, 0, 0, +12 equals the same fixture and that it refuses an offset outside −30…+30. Verify with `mise run test`.

## 5. Firmware glue

- [ ] 5.1 Implement `firmware/src/servo_output` (D4): sync the model to eight `Servo` objects on GPIO 14, 12, 13, 15, 16, 5, 4, 2 with `writeMicroseconds`, attach all eight in one pass on the first pose, detach and drive low on release, and leave the pins untouched before the first pose. Verify `mise run build` passes and `grep -rn 'Serial1\|DEBUG_ESP_PORT\|INPUT_PULLUP\|INPUT_PULLDOWN' firmware/src firmware/platformio.ini` finds nothing.
- [ ] 5.2 Implement `firmware/src/calibration_store` (D5): load the record at start-up, write it with `EEPROM.commit()` only when the model reports a changed table. Verify `mise run build` passes.
- [ ] 5.3 Wire `main.cpp`: banner, load calibration, print `calibration <source> <o0> … <o7>`, give the model the offsets and then the temporary boot pose of D6, and sync the output on every loop pass. Verify `mise run build` and `mise run test` pass.

## 6. Hardware checks (each flash write or reset with the user's go-ahead)

- [ ] 6.1 Run `mise run backup`, then `mise run upload`, then `mise run backup` again. Verify that `mise run monitor` shows `mini-spooder <version>` and `calibration default 0 0 0 0 0 0 0 0` after a reset, and that `cmp` of the two backups over 0x3FB000–0x3FFFFF finds no difference (the EEPROM sector is still erased).
- [ ] 6.2 With all servos connected, verify on the robot that all eight servos hold the boot pose stiffly (each resists being turned by hand), including the servos on GPIO16, GPIO15 and GPIO2; if a logic analyser or oscilloscope is available, record the pulse width on GPIO12 (expected 1472 µs at 90°). Switch the robot off and on 10 times and verify that the banner appears each time. Release and offset changes on the robot are checked in 6.4.
- [ ] 6.3 Write the golden record to the EEPROM sector with `mise exec -- sh -c 'esptool --port "$ROBOT_PORT" write-flash 0x3FB000 core/test/fixtures/calibration_record_v1.bin'` and verify that the console then shows `calibration stored -3 0 0 0 -7 0 0 12`. Run `mise run upload` again and verify the same line after start-up.
- [ ] 6.4 Add the `robot-calcheck` environment of design D7 and verify `mise exec -- pio run -d firmware -e robot-calcheck` builds with each value (1, 2 and 3). With the golden record from 6.3 still in the sector, flash the check build with value 2 (reset all) and verify the console shows `calibration stored 0 0 0 0 0 0 0 0` after a restart. Then flash it with value 1 (set −3, 0, 0, 0, −7, 0, 0, +12), read the sector with `mise exec -- sh -c 'esptool --port "$ROBOT_PORT" read-flash 0x3FB000 15 build/cal.bin'` and verify that `cmp build/cal.bin core/test/fixtures/calibration_record_v1.bin` finds no difference. While value 1 runs, verify that 2 s after start-up servo 0, servo 4 and servo 7 visibly move by about 3°, 7° and 12° without any other servo moving (offset applied to a driven servo). Flash it with value 3 and verify that about 2 s after start-up every servo can be turned freely by hand (released; if a logic analyser is available, every servo pin low with no pulses within 200 ms), and that 3 s later only servo 5 (GPIO5) stiffens again. Finish with `mise run upload` and verify that the normal build shows `calibration stored -3 0 0 0 -7 0 0 12` after a power cycle.
- [ ] 6.5 Run `mise run restore`, then `mise run backup`. Verify that `cmp` of the backup against `baseline/original-flash-4MB.bin` over 0x000000–0x3FBFFF finds no difference and that a phone sees the access point `Spider Robot` again. Then, if the user wants this firmware back on the robot, run `mise run upload` with their go-ahead.

## 7. Documentation

- [ ] 7.1 Update the Commands section of `CLAUDE.md` with `mise run build`, `mise run upload` and `mise run test`, the first-flash procedure (`mise run backup`, then `mise run upload`, each with the user's go-ahead), the return to stock with `mise run restore`, the `firmware/` and `core/` layout, and the image path. Verify by reading the section against the firmware-build spec.
- [ ] 7.2 Run `mise run test`, `mise run build` and `openspec validate firmware-foundation --strict`, and verify all three pass.
