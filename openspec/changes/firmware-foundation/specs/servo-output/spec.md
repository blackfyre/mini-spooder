# Spec Delta

## Purpose

Drives the robot's eight servos from commanded angles, with the stock pin map and pulse mapping, without stray output at start-up, and with a way to let servos go limp, so that motions, direct servo control and the zero check all share one output path.

## ADDED Requirements

### Requirement: Servo map

The firmware SHALL drive servo indices 0 to 7 on GPIO 14, 12, 13, 15, 16, 5, 4 and 2 respectively, as the stock calibration page labels them. A command for an index outside 0–7 SHALL be rejected and SHALL change no output.

#### Scenario: Index to pin

- **WHEN** servo 3 is commanded to 45° while every other servo holds its angle
- **THEN** only the pulse on GPIO15 changes

#### Scenario: Index out of range

- **WHEN** a command names servo index 8
- **THEN** the command is rejected and no servo's output changes

### Requirement: Angle range and pulse mapping

A commanded angle SHALL be a whole number of degrees from 0 to 180; a commanded angle below 0 SHALL be taken as 0 and one above 180 as 180. Each driven servo SHALL receive one pulse every 20 ms whose width is the output angle (the commanded angle with its calibration offset applied, as servo-calibration specifies) mapped linearly from 544 µs at 0° to 2400 µs at 180°, rounded to the nearest microsecond. A new commanded angle SHALL take effect within 60 ms of the command, also while other servos are being released.

#### Scenario: Centre

- **WHEN** servo 1 has offset 0 and is commanded to 90°
- **THEN** GPIO12 carries 1472 µs pulses every 20 ms

#### Scenario: Ends of the range

- **WHEN** a servo with offset 0 is commanded to 0° and then to 180°
- **THEN** its pulses are 544 µs and then 2400 µs wide

#### Scenario: Commanded angle above the range

- **WHEN** a servo with offset 0 is commanded to 200°
- **THEN** it is driven as if commanded to 180°, and its reported commanded angle is 180°

### Requirement: No output before the first complete pose

After a reset, the firmware SHALL send no pulses on any of the eight servo pins, and SHALL leave their pin configuration as the chip's reset left it, until it is given a complete pose: one commanded angle for each of the eight servos. It SHALL then start all eight servos within one 20 ms period, each servo's first pulse already at the width for its angle in that pose. A command for a single servo before the first complete pose SHALL be rejected.

#### Scenario: Start-up

- **WHEN** the robot resets and the first complete pose is given 1 s later
- **THEN** no servo pin carries pulses during that second, and all eight carry pulses for that pose within 20 ms of it

#### Scenario: Single servo before the first pose

- **WHEN** servo 0 is commanded on its own before any complete pose has been given
- **THEN** the command is rejected and no servo pin carries pulses

### Requirement: Boot-strapping pins

GPIO15, which must be low at boot, and GPIO2, which must be high at boot, carry servos 3 and 7. The firmware SHALL use these two pins only for servo pulses: it SHALL NOT route serial output (including the Arduino core's debug output on UART1, which uses GPIO2), an LED, SPI or any other function to them, and SHALL NOT enable an internal pull resistor on them. When it stops pulses on either pin after they have started, it SHALL drive the pin low.

#### Scenario: Repeated power-up

- **WHEN** the robot, with all eight servos connected, is switched off and on 10 times
- **THEN** each time it boots into this firmware, which `mise run monitor` shows by the boot banner

#### Scenario: No debug output on GPIO2

- **WHEN** the firmware is built by `mise run build`
- **THEN** the build configuration sets no Arduino debug port, and no project source writes to UART1

### Requirement: Release

The firmware SHALL be able to release one servo or all eight. Within 200 ms of the release, a released servo SHALL receive no pulses, with its pin driven low, so that it goes limp; until then it SHALL receive only complete pulses of its last width. It SHALL stay released until it is commanded again; the command SHALL restart its pulses at the newly commanded angle. Releasing a servo that is already released, including any servo before the first complete pose, SHALL have no effect and SHALL leave its pin as it is.

#### Scenario: Release all

- **WHEN** all eight servos are driven and all are released
- **THEN** within 200 ms no servo pin carries pulses, and every servo pin reads low

#### Scenario: Resume after release

- **WHEN** servo 5 is released and then commanded to 100°
- **THEN** GPIO5 carries pulses again, its first pulse already at the width for 100° plus its offset, and the other servos are unaffected

#### Scenario: Release before the first pose

- **WHEN** all servos are released straight after a reset, before any complete pose
- **THEN** nothing changes: no pin carries pulses and every pin keeps its reset configuration

### Requirement: Servo state report

The firmware SHALL make available, for each servo index, whether it is driven or released and its last commanded angle without the calibration offset (0–180°). Before the first complete pose, every servo SHALL be reported released with no commanded angle.

#### Scenario: Report after a pose

- **WHEN** the complete pose 70, 90, 90, 110, 110, 90, 90, 70 is given and then servo 2 is released
- **THEN** the report shows servos 0, 1 and 3–7 driven at those angles, and servo 2 released with its last commanded angle 90°

#### Scenario: Report before the first pose

- **WHEN** the report is read before any complete pose has been given
- **THEN** all eight servos are reported released with no commanded angle
