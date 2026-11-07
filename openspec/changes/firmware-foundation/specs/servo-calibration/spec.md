# Spec Delta

## Purpose

Holds the per-servo calibration trims that the stock "Servo calibration" page sets, in the stock firmware's own storage format. It applies them to every servo output, keeps them across restarts and firmware uploads, and reports damaged calibration as a fault instead of driving servos with it.

## ADDED Requirements

### Requirement: Trim table

The firmware SHALL hold one calibration trim for each of the eight servo indices. A trim is a signed whole number of degrees added to every commanded angle of that servo. A trim SHALL be valid when it is within −124 to +124, the range that the stock `/save` handler accepts. A trim of 0 SHALL mean no correction. The table SHALL be readable at any time, as the stock calibration page shows each servo's current value. A trim read from storage outside the valid range SHALL be held as invalid (Calibration fault).

#### Scenario: Readout

- **WHEN** servo 4's trim was set to −7 and every other trim is 0
- **THEN** reading the table gives 0, 0, 0, 0, −7, 0, 0, 0 for servos 0 to 7

### Requirement: Trims apply to every output

A servo's output angle SHALL be its commanded angle plus its trim, then limited to its safe range (servo-output). This SHALL apply to every source of commanded angles, including motions, direct servo control, the zero check, motion-editor keyframes and the drive link. The commanded angle that servo-output reports SHALL exclude the trim. When the trim of a driven servo changes, the servo SHALL move to its commanded angle plus the new trim, within its safe range, within 40 ms and without a new command.

#### Scenario: Trim applied

- **WHEN** servo 1 has trim −5, its safe range is 35–145, and it is commanded to 90°
- **THEN** it is driven at an output angle of 85° (a 1338 µs pulse), and its reported commanded angle is 90°

#### Scenario: Trim changed while driven

- **WHEN** servo 6 is driven at a commanded 90° with trim 0, and its trim is set to +4
- **THEN** within 40 ms it is driven at 94° (a 1439 µs pulse), and its commanded angle is still 90°

### Requirement: Setting one trim

Setting a servo's trim SHALL take a servo index and a whole number of degrees, and SHALL be allowed in every output mode. It SHALL be accepted only when the index is 0–7 and the value is within −124 to +124. Otherwise it SHALL be rejected and the table SHALL be unchanged; stock writes any key, which is a stock defect and is not reproduced. The result SHALL say whether the value was accepted. An accepted value SHALL take effect on the output at once and SHALL be stored persistently before the result is returned.

#### Scenario: Accepted

- **WHEN** servo 2's trim is set to +31
- **THEN** the result is accepted, the table shows +31 for servo 2, and the value is stored

#### Scenario: Value out of range

- **WHEN** servo 2's trim is set to +125
- **THEN** the result is rejected and servo 2's trim is unchanged

#### Scenario: Index out of range

- **WHEN** a trim is set for servo index 9
- **THEN** the result is rejected, the table is unchanged and the sector is not written

### Requirement: Reset all trims

Resetting the calibration SHALL set all eight trims to 0, apply them to the output at once, and store the zeroed table persistently. Like setting one trim, it SHALL be allowed in every output mode. This is the operation that the stock `/save?key=100` performs.

#### Scenario: Reset

- **WHEN** servos 2 and 4 have trims +12 and −7 and the calibration is reset
- **THEN** all eight trims read 0, and after a restart they still read 0

### Requirement: Persistence in the stock format

The trims SHALL be stored as the stock firmware stores them, in the 512-byte EEPROM emulation buffer kept in the flash sector at 0x3FB000. Byte `i`, for `i` from 0 to 7, SHALL hold servo `i`'s trim as a signed two's-complement byte, with no identifier, version or checksum.

- **Erased byte:** a byte that reads 0xFF (erased flash) SHALL be the trim −1, as on stock.
- **Load:** at start-up, before any servo is driven, the firmware SHALL load bytes 0–7.
- **Write only on change:** the sector SHALL be written only when an accepted set or reset changes at least one trim. Setting a trim to its current value SHALL NOT write it.
- **Other bytes:** a write SHALL keep bytes 8–511 as they were loaded.
- **Survival:** stored trims SHALL survive restarts, power cycles and `mise run upload`. Trims written by the stock firmware SHALL load unchanged into this firmware.

After loading, the firmware SHALL print one line on UART0 after the reset-cause line (firmware-build). The line SHALL have the form `calibration ok <t0> <t1> <t2> <t3> <t4> <t5> <t6> <t7>` when every trim is valid, or `calibration fault <t0> … <t7>` otherwise. Each `<ti>` SHALL be the signed decimal value of byte `i`, followed by `!` when that trim is invalid.

#### Scenario: Erased sector from the stock image

- **WHEN** this firmware first starts on a robot whose EEPROM sector is erased, as in the baseline image
- **THEN** the console shows `calibration ok -1 -1 -1 -1 -1 -1 -1 -1`, every trim reads −1, and the sector is still erased afterwards

#### Scenario: Stock trims kept across the first upload

- **WHEN** the sector holds bytes 0–7 = FD 00 00 00 F9 00 00 0C, written by the stock firmware, and this firmware is uploaded with `mise run upload`
- **THEN** after start-up the console shows `calibration ok -3 0 0 0 -7 0 0 12`

#### Scenario: Survives a power cycle

- **WHEN** servo 0's trim is set to −3 and the robot is switched off and on
- **THEN** servo 0's trim reads −3 and its first driven pose uses it

#### Scenario: Other bytes kept

- **WHEN** bytes 8–511 of the sector hold data and servo 3's trim is changed
- **THEN** bytes 8–511 read back unchanged and byte 3 holds the new trim

#### Scenario: Unchanged value not written

- **WHEN** servo 3's trim is 0 and is set to 0
- **THEN** the result is accepted and the sector is not written

### Requirement: Calibration fault

The firmware SHALL treat a loaded trim outside −124 to +124 as a start-up failure, since neither stock nor this firmware writes such a value; it shows damaged storage. On that failure it SHALL:

- print the `calibration fault` line;
- put servo-output into `fault`, with output disabled;
- keep running every other service, including the web portal and the drive link, with `loop()` returning on every pass; it SHALL NOT stop in an endless loop or restart.

Setting an invalid trim to a valid value, or resetting all trims, SHALL make it valid. When every trim is valid, servo-calibration SHALL report this, so that servo-output leaves `fault` for `safe`, and the firmware SHALL print `calibration ok …` with the new table.

A power failure during a sector write cannot be detected in this format. The sector is erased and then programmed, so an interrupted write can leave any byte values. In-range values are then used as trims, still limited by each servo's safe range; only out-of-range values cause a fault.

#### Scenario: Damaged trim byte

- **WHEN** the robot starts with byte 2 of the sector holding 0x80 (−128) and the other bytes 0
- **THEN** the console shows `calibration fault 0 0 -128! 0 0 0 0 0` and `mode fault calibration`, no servo pin carries pulses, an enable request is rejected, and `loop()` keeps running

#### Scenario: Fault cleared by setting the trim

- **WHEN** the firmware is in `fault` because of servo 2's trim, and servo 2's trim is set to 0
- **THEN** the console shows `calibration ok` with the new table, the mode changes to `safe`, and a following enable request is accepted
