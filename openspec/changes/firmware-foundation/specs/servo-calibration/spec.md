# Spec Delta

## Purpose

Holds the per-servo calibration offsets that the stock image's "Servo calibration" page sets, applies them to every servo output, and keeps them across restarts and firmware uploads.

## ADDED Requirements

### Requirement: Offset table

The firmware SHALL hold one calibration offset for each of the eight servo indices: a signed whole number of degrees from −30 to +30. An offset of 0 SHALL mean no correction. The current table SHALL be readable at any time, as the stock calibration page shows each servo's current value.

#### Scenario: Readout

- **WHEN** servo 4's offset was set to −7 and every other offset is 0
- **THEN** reading the table gives 0, 0, 0, 0, −7, 0, 0, 0 for servos 0 to 7

### Requirement: Offsets apply to every output

A servo's output angle SHALL be its commanded angle plus its offset, limited to 0–180°. This SHALL apply to every source of commanded angles, including motions, direct servo control and the zero check. The commanded angle that servo-output reports SHALL exclude the offset. When a driven servo's offset changes, the servo SHALL move to its commanded angle plus the new offset without a new command.

#### Scenario: Offset applied

- **WHEN** servo 1 has offset −5 and is commanded to 90°
- **THEN** it is driven at an output angle of 85° (a 1420 µs pulse), and its reported commanded angle is 90°

#### Scenario: Output limited at the end of the range

- **WHEN** servo 0 has offset +5 and is commanded to 178°
- **THEN** it is driven at 180°

#### Scenario: Offset changed while driven

- **WHEN** servo 6 is driven at a commanded 90° with offset 0, and its offset is set to +4
- **THEN** within 60 ms it is driven at 94° and its commanded angle is still 90°

### Requirement: Setting one offset

Setting a servo's offset SHALL take a servo index and a whole number of degrees. It SHALL be accepted only when the index is 0–7 and the value is within −30 to +30; otherwise it SHALL be rejected and the table SHALL be unchanged. The result SHALL say whether the value was accepted. An accepted value SHALL take effect on the output at once and SHALL be stored persistently before the result is returned.

#### Scenario: Accepted

- **WHEN** servo 2's offset is set to +12
- **THEN** the result is accepted, the table shows +12 for servo 2, and the value is stored

#### Scenario: Value out of range

- **WHEN** servo 2's offset is set to +31
- **THEN** the result is rejected and servo 2's offset is unchanged

#### Scenario: Index out of range

- **WHEN** an offset is set for servo index 9
- **THEN** the result is rejected and the table is unchanged

### Requirement: Reset all offsets

Resetting the calibration SHALL set all eight offsets to 0, apply them to the output at once, and store the zeroed table persistently. This is the operation that the stock page's `saveServo(100,0)` (`/save?key=100`) requests.

#### Scenario: Reset

- **WHEN** servos 2 and 4 have offsets +12 and −7 and the calibration is reset
- **THEN** all eight offsets read 0, and after a restart they still read 0

### Requirement: Persistence

The offset table SHALL be stored in the EEPROM emulation sector at 0x3FB000–0x3FBFFF as one record that carries a format identifier, a format version and a checksum. At start-up, before the first complete pose is driven, the firmware SHALL load the stored table. When the sector holds no valid record (it is erased, as in the stock image, or the identifier, version or checksum does not match), every offset SHALL be 0 and the firmware SHALL NOT write the sector until an offset is set or the calibration is reset. The sector SHALL be written only when the table changes: setting an offset to its current value SHALL NOT write it. Stored offsets SHALL survive restarts, power cycles and `mise run upload`. If power fails while the sector is being written, the next start-up SHALL load either the previous table, the new table or all zeros, never other values.

After loading, the firmware SHALL print one line on UART0 at 115200 baud, after the boot banner, in the form `calibration <source> <o0> <o1> <o2> <o3> <o4> <o5> <o6> <o7>`, where `<source>` is `stored` when a valid record was loaded and `default` otherwise, and each offset is a signed decimal integer.

#### Scenario: Load reported at start-up

- **WHEN** the robot starts with a valid stored record holding −3 for servo 0 and 0 for the others
- **THEN** the console shows `calibration stored -3 0 0 0 0 0 0 0`

#### Scenario: Survives a power cycle

- **WHEN** servo 0's offset is set to −3 and the robot is switched off and on
- **THEN** servo 0's offset reads −3 and the first pose is driven with it

#### Scenario: Survives a firmware upload

- **WHEN** offsets are set, and a new build is flashed with `mise run upload`
- **THEN** after start-up the table holds the same offsets

#### Scenario: Erased sector from the stock image

- **WHEN** this firmware first starts on a robot whose EEPROM sector is erased, as after `mise run restore`
- **THEN** every offset reads 0 and the sector is still erased

#### Scenario: Corrupted record

- **WHEN** the stored record's checksum does not match its contents
- **THEN** every offset reads 0 at start-up

#### Scenario: Unchanged value not written

- **WHEN** servo 3's offset is 0 and is set to 0
- **THEN** the result is accepted and the sector is not written
