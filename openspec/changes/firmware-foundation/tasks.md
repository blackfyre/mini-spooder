# Tasks

Every step that writes to the robot's flash or resets it (`mise run backup`, `mise run upload`, `mise run restore`, any `esptool` command) needs the user's go-ahead first. Every step that drives a servo over its range runs with the robot on a stand, legs off the ground.

## 1. PlatformIO project and core library

- [ ] 1.1 Create `firmware/platformio.ini` with the `robot`, `robot-check` and `native` environments of design D1. Pin `espressif8266` and `native` to their newest releases at implementation time. Add `firmware/src/main.cpp` with an empty `setup()` and `loop()`. Read the pinned core's `core_esp8266_waveform*.cpp` and record in design D5 whether a timing change and a zero high time take effect at the period boundary without truncating the pulse in progress. Verify that `mise exec -- pio run -d firmware -e robot` succeeds without a robot connected.
- [ ] 1.2 Create `core/library.json` with warnings as errors and the `srcFilter` of D1, a first `core/test/test_smoke/` Unity test, and two temporary files under `core/fuzz/dummy_a/` and `core/fuzz/dummy_b/`. Each temporary file defines `LLVMFuzzerTestOneInput`, and one also defines `main`. Reference `core/` from every env with `lib_deps = symlink://../core` and `lib_ldf_mode = off`. Verify that `mise exec -- pio test -d firmware -e native` passes the smoke test, that `pio run -d firmware -e robot` succeeds, and that `nm` on both outputs lists no `LLVMFuzzerTestOneInput`. Then remove the dummy targets.
- [ ] 1.3 Verify the hardware-free rule. Temporarily add `#include <Arduino.h>` to a `core/` source and confirm that `pio test -d firmware -e native` fails, then remove it. Temporarily add an unused variable to a `core/` source and to `firmware/src/main.cpp`, confirm that each fails its build, then remove it.

## 2. Build checks, diagnostics and mise tasks

- [ ] 2.1 Add `firmware/scripts/version.py` (D2) and `core/system/reset_cause`, which maps the SDK reason codes 0–6 to the names in firmware-build and any other value to `unknown-<n>`, and formats the `exccause` and `epc1` suffix. In `setup()`, print `mini-spooder <version>` and then the `reset …` line at 115200 baud. Verify with `core/test/test_reset_cause/` (codes 0–6, code 9 → `unknown-9`, the suffix present for codes 1–3 and absent otherwise) under `mise run test`. Verify with `strings firmware/.pio/build/robot/firmware.bin | grep mini-spooder` that the image carries the `git describe --always --dirty` output.
- [ ] 2.2 Add `firmware/scripts/check_image.py` (D2), with its parsing and comparison as pure functions, and `firmware/scripts/test_check_image.py`. The tests cover:
  - a matching header and layout;
  - flash size, mode and frequency mismatches;
  - `_EEPROM_start` other than 0x405FB000;
  - a non-empty filesystem range;
  - an image reaching 0x3FB000;
  - a missing symbol;
  - a present check-console symbol.

  Verify with `python -m unittest -v test_check_image` in `firmware/scripts/`.
- [ ] 2.3 Add the `build`, `upload` and `test` tasks to `mise.toml` as in D3, leaving `backup`, `restore` and `monitor` unchanged. Verify that `mise run build` passes and prints the check's result. Verify that `mise run test` runs the Unity and Python tests. Verify that `ROBOT_PORT=/nonexistent mise run upload` fails, naming `ROBOT_PORT`, before any esptool call.
- [ ] 2.4 Verify the layout check end to end. Temporarily set `board_build.ldscript = eagle.flash.4m1m.ld` (a 4 MB layout with a 1 MB filesystem), confirm that `mise run build` fails naming the filesystem range, then restore `eagle.flash.4m.ld`. Verify that `esptool image-info` on the built image reports 4 MB, DIO and 40 MHz.

## 3. Servo model, safe ranges and output gate in core

- [ ] 3.1 Implement `core/servo/pulse` (D5): the stock conversion and the per-servo safe ranges with the spec's defaults. Generate `core/test/fixtures/stock_keyframes.h` once from the stock rows of `docs/lafvin-motions.md`, plus the zero pose 135, 45, 135, 45, 45, 135, 45, 135, and commit it. Verify with `core/test/test_pulse/`:
  - 1° → 400 µs, 90° → 1394 µs, 85° → 1338 µs, 94° → 1439 µs, 160° → 2176 µs, 180° → 2400 µs;
  - servo 3 at a commanded 158° with trim +5 → 160°;
  - servo 1 at a commanded 0° with trim −1 → 35°;
  - every range satisfies `1 ≤ low < high ≤ 180`;
  - no stock keyframe angle, nor any zero-pose angle, is limited with trims −1 and 0.
- [ ] 3.2 Implement `core/servo/servo_bank` (D5): servo indices 0–7, complete pose, single-servo command, commanded angle limited to 0–180°, release of one or all, the trim applied before the safe range, and the state report. Verify with `core/test/test_servo_bank/`:
  - index 8 rejected;
  - 200° reported as 180°;
  - a report after a pose with servo 2 released;
  - release all;
  - resume after release;
  - releasing a released servo changes nothing;
  - a trim change on a driven servo changes its width with no new command (servo 6, trim +4 → 1439 µs), with the commanded angle still reported as 90°.
- [ ] 3.3 Implement `core/servo/output_gate` (D5): the four modes, the transitions of the servo-output state diagram, the writer token, the safe-state reasons and the mode-line text. Verify with `core/test/test_output_gate/`:
  - `idle` after construction with valid trims, and `fault` with an invalid trim;
  - a single servo or a pose in `idle` rejected;
  - enable in `idle` → `active` with every servo driven at the pose;
  - `enterSafe(link-loss)` from `active` releases all servos, sets the reason and makes the token stale;
  - a pose with the stale token rejected after a second caller re-enables;
  - `enterSafe` in `idle` → `safe`;
  - enable rejected in `fault`;
  - `fault` → `safe` only when the trims become valid, never → `active`;
  - the mode line for each transition.

## 4. Calibration

- [ ] 4.1 Implement `core/calibration/trim_table` (D6): decode eight bytes, valid range −124…+124, set (accepted, rejected or unchanged), reset all, "write needed", and "all valid". Commit the fixtures `core/test/fixtures/trims_example.bin` and `trims_fault.bin` of D6. Verify with `core/test/test_trim_table/`:
  - eight 0xFF bytes → −1 × 8, all valid;
  - `trims_example.bin` → −3, 0, 0, 0, −7, 0, 0, +12, and encoding that table gives the same 8 bytes;
  - `trims_fault.bin` → servo 2 invalid, not all valid;
  - set +31 accepted; +124 and −124 accepted; +125 rejected; index 9 rejected;
  - set to the current value → unchanged, no write;
  - reset → zeros and a write needed;
  - setting servo 2 to 0 in the fault table → all valid.
- [ ] 4.2 Implement `firmware/src/calibration_store` (D6): `EEPROM.begin(512)` at start-up, then decode bytes 0–7 and print the calibration line. Write the changed bytes and call `EEPROM.commit()` only when the table reports a write needed, and flag that pass to the loop statistics. Verify that `mise run build` passes and that `grep -n 'EEPROM.begin(512)' firmware/src/calibration_store.cpp` finds the call.

## 5. Firmware glue

- [ ] 5.1 Implement `core/system/loop_stats` (D4). Verify with `core/test/test_loop_stats/`:
  - a 25 ms pass counts as over budget;
  - a 25 ms pass flagged as a commit pass does not count, and updates the longest commit pass;
  - the longest pass is kept.
- [ ] 5.2 Implement `firmware/src/servo_driver` (D5). It syncs the model to waveforms on GPIO 15, 13, 12, 14, 2, 4, 5, 16, starts each servo with its first pulse at the commanded width, releases in two non-blocking phases, and leaves the pins untouched in `idle`. Verify that `mise run build` passes, that `grep -rn 'Serial1\|DEBUG_ESP_PORT\|INPUT_PULLUP\|INPUT_PULLDOWN\|Servo.h\|delay(' firmware/src firmware/platformio.ini` finds nothing outside `check_console.cpp`'s `hang`, and that the pin table in `servo_driver.cpp` reads 15, 13, 12, 14, 2, 4, 5, 16.
- [ ] 5.3 Wire `main.cpp` (D4): banner, reset cause, load calibration, construct the gate in `idle` or `fault`, print `mode fault calibration` when in `fault`, then on each pass poll `servo_driver`, record the pass time, and return. There is no boot pose. Verify that `mise run build` and `mise run test` pass, and that `grep -n 'enable' firmware/src/main.cpp` finds no call outside the check-console build.
- [ ] 5.4 Implement `core/check/console_parse` and `firmware/src/check_console` (D7), built only in `robot-check`. Verify with `core/test/test_console_parse/` (every command of D7, and `servo 3`, `servo 9 90`, `enable 1 2 3` and an empty line rejected) under `mise run test`. Verify that `mise exec -- pio run -d firmware -e robot-check` builds, and that `mise run build`'s image check reports no console symbol.

## 6. Hardware checks (each flash write or reset with the user's go-ahead; order per design D8)

- [ ] 6.1 Normal build, all servos connected, robot on a stand. Run `mise run backup`, then `mise run upload`, then `mise run backup` again.
  - Verify that `cmp` of the two backups over 0x3FB000–0x3FFFFF finds no difference.
  - Verify that `mise run monitor` shows `mini-spooder <version>`, `reset external` and `calibration ok -1 -1 -1 -1 -1 -1 -1 -1` after an esptool reset.
  - Verify that every servo can be turned freely by hand for 10 s after start-up (boot still). If a logic analyser is available, verify that no servo pin carries a pulse.
  - Switch the robot off and on 10 times, and verify that the banner and `reset power-on` appear each time.
- [ ] 6.2 Flash the `robot-check` build (`pio run -d firmware -e robot-check -t upload --upload-port "$ROBOT_PORT"`), robot on a stand. Verify each step in turn:
  1. `servo 0 80` before any enable is rejected and every servo stays limp.
  2. `enable 70 90 90 110 110 90 90 70` gives `mode active enable`, and all eight servos stiffen, including those on GPIO15, GPIO2 and GPIO16.
  3. `safe` gives `mode safe check`, and every servo goes limp. With a logic analyser, every pin is low with no pulses within 40 ms; before the `safe`, record the pulse width on GPIO13 at a commanded 90° (expected 1383 µs: 90° with the erased trim −1 is 89°).
  4. `servo 0 80` is then rejected.
  5. A new `enable` stiffens all servos again.
  6. `release all` followed by `status` shows the over-budget count unchanged and a longest pass under 20 ms.
  7. `hang` resets the chip, and the next start-up shows `reset software-watchdog` with `exccause` and `epc1`.
- [ ] 6.3 Joint and direction check, check build, legs off the ground. Enable with the standby pose 70, 90, 90, 110, 110, 90, 90, 70. For each index 0–7 in turn, command `servo i <standby + 15>` and then back to standby, and verify that exactly one joint moves. Record, for each index, its GPIO, the leg (front or rear, left or right), the joint (hip or knee), and the direction of increasing angle in `docs/servo-joints.md`. Update the Hardware section of `CLAUDE.md` to point to it. Verify by repeating the sequence once and checking each row of the document against what moves.
- [ ] 6.4 Mechanical stops, check build, legs off the ground. For each index, step `servo i a` in 5° steps from standby towards each end of its default safe range. Stop at the first contact or binding, and record the last free angle in `docs/servo-joints.md`. Where a measured limit is narrower than the default, set it in `core/servo/pulse`'s table. Where the measured limit falls inside the stock envelope, record that in the document and in design D5. Rerun `mise run test` (the envelope test then names any stock keyframe that is limited) and reflash the check build. Verify that commanding each servo 10° beyond its range holds it at the limit with no contact.
- [ ] 6.5 Calibration, check build, robot on a stand.
  1. `trim 4 -7`: verify that servo 4 visibly moves by about 7° while driven, with no other servo moving.
  2. `trimreset`, then `trim 0 -3`, `trim 4 -7` and `trim 7 12`. Read the sector with `mise exec -- sh -c 'esptool --port "$ROBOT_PORT" read-flash 0x3FB000 8 build/cal.bin'`, and verify that `cmp build/cal.bin core/test/fixtures/trims_example.bin` finds no difference.
  3. `status`: record the longest commit pass in design D6. If a logic analyser is available, record whether the pulses stayed intact during a commit.
  4. Write `core/test/fixtures/trims_fault.bin` to 0x3FB000 with esptool. Verify that the console shows `calibration fault 0 0 -128! 0 0 0 0 0` and `mode fault calibration`, that `enable …` is rejected and every servo stays limp, and that `trim 2 0` gives `calibration ok 0 0 0 0 0 0 0 0` and `mode safe calibration-ok`.
  5. Write `trims_example.bin` to 0x3FB000, run `mise run upload` (normal build), and verify `calibration ok -3 0 0 0 -7 0 0 12` after a power cycle.
- [ ] 6.6 Run `mise run restore`, then `mise run backup`. Verify that `cmp` of the backup against `baseline/original-flash-4MB.bin` over 0x000000–0x3FBFFF finds no difference, and that a phone sees the access point `Spider Robot` again. Then, if the user wants this firmware back on the robot, run `mise run upload` with their go-ahead.

## 7. Documentation

- [ ] 7.1 Update the Commands section of `CLAUDE.md` with:
  - `mise run build`, `mise run upload` and `mise run test`;
  - the check build's upload command and its console commands;
  - the first-flash procedure (`mise run backup`, then `mise run upload`, each with the user's go-ahead);
  - the return to stock with `mise run restore`;
  - the `firmware/` and `core/` layout, including `core/fuzz/`;
  - the image path;
  - the 115200-baud diagnostics.

  Verify by reading the section against the firmware-build spec.
- [ ] 7.2 Run `mise run test`, `mise run build` and `openspec validate firmware-foundation --strict`, and verify that all three pass.
