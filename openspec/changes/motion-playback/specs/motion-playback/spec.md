# motion-playback Specification

## Purpose

Plays the stock firmware's motions (ids 1–15), its centre and zero positions (ids 99 and 100) and single caller-supplied keyframes on the robot's eight servos, from keyframe data taken from the stock flash image, once per request, and lets the web portal and the drive link start, queue and stop them without blocking the rest of the firmware. It moves nothing until it is asked to, and it gives way to servo output's safe state.

## ADDED Requirements

### Requirement: Motion catalogue

The player SHALL accept exactly the motion ids of the stock firmware's `pm` parameter, with these names and compositions:

| Id | Motion | Played as |
|---|---|---|
| 1 | Standby | Standby table |
| 2 | Forward | Forward table |
| 3 | Backward | Backward table |
| 4 | Left shift | Left shift table |
| 5 | Right shift | Right shift table |
| 6 | Turn left | Turn left table |
| 7 | Turn right | Turn right table |
| 8 | Lie | Lie table |
| 9 | Say Hi | Say Hi table, then the Standby table |
| 10 | Fighting | Fighting table |
| 11 | Push up | Push up table |
| 12 | Sleep | Standby table, then the Sleep table |
| 13 | Dance 1 | Dance 1 table |
| 14 | Dance 2 | Dance 2 table |
| 15 | Dance 3 | Dance 3 table |
| 99 | Centre | Direct-pose routine with the centre pose |
| 100 | Zero position | Direct-pose routine with the zero pose |

The player SHALL make this list of ids and names available to its callers. The request call SHALL return at once whether it accepted the request. A request for any other id SHALL be rejected and SHALL change neither the motion or keyframe that is playing nor the pending request.

#### Scenario: Known id

- **WHEN** motion 2 is requested while the player is idle and servo output is enabled
- **THEN** the request call returns accepted and the player plays Forward

#### Scenario: Centre

- **WHEN** motion 99 is requested while the player is idle and servo output is enabled
- **THEN** the request call returns accepted and the player runs the direct-pose routine with the centre pose

#### Scenario: Unknown id

- **WHEN** motion 16, 0, 98 or 101 is requested while motion 13 plays with motion 2 pending
- **THEN** the request call returns rejected, motion 13 continues and motion 2 stays pending

#### Scenario: Composite motion

- **WHEN** motion 9 is requested and plays to the end
- **THEN** the player plays the 7 Say Hi keyframes followed by the 2 Standby keyframes, and reports motion 9 as current throughout

### Requirement: Keyframe data matches the stock image

Each motion table SHALL hold, in order, the keyframes stored for that motion in `baseline/original-flash-4MB.bin`: per keyframe, one angle in degrees for each servo index 0–7 and a duration in milliseconds. The tables SHALL have these keyframe counts: Standby 2, Forward 11, Backward 11, Left shift 11, Right shift 11, Turn left 8, Turn right 8, Lie 1, Say Hi 7, Fighting 11, Push up 11, Sleep 2, Dance 1 10, Dance 2 9, Dance 3 10. The centre pose and the zero pose SHALL be the stock image's poses at flash 0x40DE8 and 0x40E0C, both 135, 45, 135, 45, 45, 135, 45, 135 for servos 0–7. The repository's motion data SHALL be generated from the stock image, and a check SHALL fail when the generated data and the image differ or when the image's SHA-256 hash is not the one recorded in `baseline/README.md`.

#### Scenario: Data agrees with the image

- **WHEN** `mise run check-motions` runs on an unmodified checkout
- **THEN** it verifies the image hash, re-extracts every table, the centre pose and the zero pose, finds them identical to the checked-in data and exits with status 0

#### Scenario: Edited table

- **WHEN** one angle or duration in the checked-in motion data is changed by hand
- **THEN** `mise run check-motions` exits non-zero and names the motion and keyframe that differ

#### Scenario: Wrong image

- **WHEN** `mise run check-motions` runs against an image whose SHA-256 hash differs from the recorded hash
- **THEN** it exits non-zero without comparing the tables

### Requirement: Current pose comes from servo output

The player SHALL keep no pose of its own. At the start of every keyframe and of every direct-pose routine, it SHALL take each servo's start angle from servo output's last commanded angle for that servo, before calibration.

#### Scenario: Direct servo command between motions

- **WHEN** Standby has ended, servo 2 is then commanded directly to 120 through servo output, and motion 8 is requested
- **THEN** Lie's keyframe starts servo 2 from 120, not from Standby's 90

#### Scenario: Servo moved away from its next keyframe angle

- **WHEN** servo 0 is commanded directly to 100 after Standby and a motion whose first keyframe sets servo 0 to 70 is requested
- **THEN** the player interpolates servo 0 from 100 to 70 during that keyframe

### Requirement: Keyframe interpolation

The player SHALL move the servos through a table one keyframe at a time, on a 10 ms tick. For a keyframe of duration `T` ms, the player SHALL run `n = max(1, floor(T / 10))` ticks, numbered `i = 1 … n`. At each tick, for each servo index 0–7 in ascending order, with start angle `A` and keyframe angle `B`:

- when `A = B`, the player SHALL write nothing to that servo;
- otherwise it SHALL write `A + d` when `B > A`, or `A − d` when `B < A`, where `d = floor((i·|B − A| + floor(n / 2)) / n)`.

At tick `n`, every servo whose angle changes in the keyframe SHALL therefore be written its keyframe angle exactly, so that at the end of every keyframe servo output's commanded angles equal the keyframe angles.

#### Scenario: First tick of a keyframe

- **WHEN** a 200 ms keyframe starts for a servo whose start angle is 135 and whose keyframe angle is 90
- **THEN** the player writes 133 to that servo at tick 1

#### Scenario: Last tick of a keyframe

- **WHEN** a 200 ms keyframe moves a servo from 135 to 90
- **THEN** the player runs 20 ticks and writes 90 to that servo at tick 20

#### Scenario: Duration not a multiple of 10 ms

- **WHEN** a keyframe has a duration of 375 ms
- **THEN** the player runs 37 ticks for it, uses 18 as `floor(n / 2)` and writes the keyframe angles at tick 37

#### Scenario: Duration under 10 ms

- **WHEN** a keyframe has a duration of 0 ms or 9 ms
- **THEN** the player runs one tick for it and writes the keyframe angle to every servo whose angle changes

#### Scenario: Standby from the zero pose

- **WHEN** motion 1 plays to the end with the zero pose as servo output's commanded angles
- **THEN** servo output's commanded angles at the end are 70, 90, 90, 110, 110, 90, 90, 70

#### Scenario: Unchanged servo

- **WHEN** a keyframe's angle for a servo equals that servo's start angle
- **THEN** the player writes nothing to that servo during the keyframe

### Requirement: Playback timing

Each tick SHALL have a deadline. The first tick of a motion or keyframe accepted while the player is idle SHALL be due at the player's next chance to run. After a tick due at `D` runs at time `t`, the next tick SHALL be due at `D + 10` ms when `t < D + 10`, and otherwise at `t + 10` ms. The player SHALL run at most one tick at each chance to run and SHALL NOT make up missed ticks: jitter of less than one tick does not accumulate, and a longer delay lengthens the motion by the delay instead of compressing it. When the firmware gives the player the chance to run every 1 ms, a motion's duration SHALL therefore be 10 ms times its tick count: for example 1650 ms for Forward (11 keyframes of 15 ticks). A motion or keyframe SHALL end at the player's first chance to run at or after the deadline that its next tick would have had. A pending motion or keyframe SHALL run its first tick at that same chance, and with nothing pending the player SHALL report itself idle from it.

#### Scenario: On-time ticks

- **WHEN** Forward plays and the player is given the chance to run every 1 ms of a simulated clock
- **THEN** its 165 ticks fall at 0, 10, 20 … 1640 ms after the first tick and it ends at 1650 ms

#### Scenario: Consecutive motions

- **WHEN** Forward plays with motion 2 pending and the player is given the chance to run every 1 ms
- **THEN** the second Forward's first tick runs 1650 ms after the first Forward's first tick

#### Scenario: Slightly late tick

- **WHEN** a tick due at 10 ms runs at 19 ms and the player is then given the chance to run every 1 ms
- **THEN** the next tick runs at 20 ms, and the ticks after it at 30, 40 … ms

#### Scenario: Stall longer than a tick

- **WHEN** a tick is due at 10 ms and the player's next chance to run is at 60 ms
- **THEN** the player runs exactly one tick at 60 ms, the next tick is due at 70 ms and the motion's remaining ticks are all still played

#### Scenario: Late chance at the end of a motion

- **WHEN** Forward's last tick runs at 1640 ms with motion 6 pending and the player's next chance to run is at 1700 ms
- **THEN** Forward ends at 1700 ms and Turn left runs its first tick at 1700 ms

### Requirement: Direct-pose routine

Motions 99 and 100 SHALL write their pose (the centre pose and the zero pose) to servos 0–7, one servo per tick in ascending index order with no interpolation, and SHALL then hold for 30 ticks (300 ms) before the player reports the motion as finished, 38 ticks in all.

#### Scenario: Zero position

- **WHEN** motion 100 is requested while the player is idle and servo output is enabled
- **THEN** the player writes 135 to servo 0 at the first tick, 45 to servo 1 at the second, and so on to 135 to servo 7 at the eighth, writes nothing for the next 30 ticks and becomes idle 380 ms after the first tick

### Requirement: Standby pose

The Standby table SHALL move all servos to 90 in a 200 ms keyframe and then to the standby pose, 70, 90, 90, 110, 110, 90, 90, 70 for servos 0–7, in a second 200 ms keyframe.

#### Scenario: Standby keyframes

- **WHEN** the Standby table is read from the generated data
- **THEN** it holds the keyframes 90, 90, 90, 90, 90, 90, 90, 90 for 200 ms and 70, 90, 90, 110, 110, 90, 90, 70 for 200 ms

### Requirement: Still until the first command

After a reset the player SHALL write nothing to servo output, and SHALL send it no enable request, until it accepts a motion request or a single keyframe. It SHALL NOT run any motion or routine of its own at start-up.

#### Scenario: No command after reset

- **WHEN** the firmware runs for 10 s after a reset and the player receives no request
- **THEN** the player has written nothing to servo output, has sent it no enable request and reports itself idle

### Requirement: Output enable and the safe state

At the first tick of a motion or keyframe accepted while the player is idle, the player SHALL send servo output one enable request carrying a complete pose:

- when servo output reports a mode other than active, the zero pose, 135, 45, 135, 45, 45, 135, 45, 135 for servos 0–7 (motion 100's pose, which stock attaches at boot); that tick SHALL write nothing else, and the motion or keyframe SHALL run from the zero pose, starting at its next tick;
- when servo output reports active, its eight commanded angles, so that no servo moves because of the request; the motion's first tick SHALL then run in the same tick.

Each motion request and each keyframe SHALL carry the origin of its caller, as servo output names origins (`link` for the drive link, `portal` for the web portal, `check` for the demonstration build), and the player SHALL keep it with the request. The enable request SHALL carry the origin of the motion or keyframe whose first tick sends it.

A motion or keyframe that starts from the pending slot when the previous one ends SHALL send no enable request. The player SHALL send an enable request at no other time. Whether servo output accepts an enable request, including after a safe state, is servo output's rule.

At every service call while a motion or keyframe is under way (after the tick that sent its enable request), the player SHALL first check servo output's mode, whether or not a tick is due; when it is not active, the player SHALL end the current motion or keyframe without writing, discard the pending request and report itself idle. When servo output refuses the enable request or a write, the player SHALL likewise end the current motion or keyframe at once, discard the pending request and report itself idle.

#### Scenario: First command after reset

- **WHEN** motion 1 is requested for the first time after a reset, with servo output active in its boot pose (the zero pose)
- **THEN** the player sends servo output an enable request with its commanded angles, the zero pose, at the first tick, no servo moves because of it, and Standby plays from that pose in the same tick, 40 ticks in all

#### Scenario: First command while output is not active

- **WHEN** motion 1 is requested while servo output is in `safe`
- **THEN** the player sends servo output an enable request with the pose 135, 45, 135, 45, 45, 135, 45, 135 at the first tick, writes nothing else in that tick and then plays Standby from that pose, 41 ticks in all

#### Scenario: Origin of the enable request

- **WHEN** servo output is not active, the player is idle, and motion 1 is requested with origin `portal`
- **THEN** the enable request that the player sends at the first tick carries origin `portal` and the zero pose

#### Scenario: Safe state during a motion

- **WHEN** servo output enters its safe state after tick 40 of Forward, with motion 6 pending, before the player's next chance to run
- **THEN** at that chance the player writes nothing, motion 6 does not play and the player reports itself idle

#### Scenario: Write refused

- **WHEN** servo output refuses a write at tick 40 of Forward with motion 6 pending
- **THEN** the player writes nothing further, motion 6 does not play and the player reports itself idle

#### Scenario: Request after the safe state

- **WHEN** servo output has entered its safe state and motion 2 is then requested
- **THEN** the player sends servo output an enable request with the zero pose at the first tick, and plays Forward from the zero pose if servo output accepts it

#### Scenario: Enable refused

- **WHEN** motion 2 is requested and servo output refuses the player's enable request
- **THEN** the player writes nothing, discards the pending request and reports itself idle

#### Scenario: Output enabled by a direct servo command

- **WHEN** a motion ended, servo output then entered its safe state, a direct servo command then enabled output again with the zero pose and set servo 2 to 120 while the player was idle, and motion 8 is then requested
- **THEN** at the first tick the player sends an enable request with servo output's commanded angles (the zero pose with servo 2 at 120), no servo moves because of it, Lie's first tick runs in the same tick and Lie starts servo 2 from 120

#### Scenario: Direct servo command does not start the player

- **WHEN** the player is idle after a reset and a direct servo command sets one servo
- **THEN** the player writes nothing, sends no enable request and reports itself idle

### Requirement: Single keyframe

The player SHALL play one keyframe supplied by a caller: eight angles of 0–180° for servos 0–7 and a duration of 0–60000 ms. It SHALL reject angles or a duration outside those ranges and then change nothing. An accepted keyframe SHALL be handled as an accepted motion request is (Requests while a motion plays), and SHALL run, interpolated as a motion keyframe, from servo output's commanded angles at its start. The player SHALL return a non-zero identifier for each accepted keyframe, different from every identifier it has returned since the reset (for at least the first 4294967295 accepted keyframes), and SHALL answer, for any identifier, whether that keyframe has ended: it has ended unless it is the keyframe now playing or the pending keyframe, so it has ended once it finished, was stopped, was replaced while pending, or was ended by servo output's mode or a refused write.

#### Scenario: Keyframe while idle

- **WHEN** a keyframe of 70, 90, 90, 110, 110, 90, 90, 70 over 500 ms is accepted while the player is idle and servo output is active
- **THEN** its first tick runs at the player's next chance to run, it runs 50 ticks and servo output's commanded angles end at 70, 90, 90, 110, 110, 90, 90, 70

#### Scenario: Keyframe during a motion

- **WHEN** a keyframe is accepted while Dance 1 plays with motion 3 pending
- **THEN** motion 3 is discarded, Dance 1 completes and the keyframe runs after it

#### Scenario: Keyframe ended

- **WHEN** a 500 ms keyframe is accepted with identifier `k` while the player is idle and servo output is active, and the player is given the chance to run every 1 ms
- **THEN** the player answers that `k` has not ended until 500 ms after its first tick and that it has ended from then on

#### Scenario: Pending keyframe not ended

- **WHEN** keyframe `k` is pending behind Forward
- **THEN** the player answers that `k` has not ended, and answers that it has ended once motion 4 is requested and replaces it

#### Scenario: Keyframe stopped

- **WHEN** stop is commanded while keyframe `k` plays
- **THEN** the player answers that `k` has ended

#### Scenario: Zero-duration keyframe

- **WHEN** a keyframe with duration 0 is accepted while the player is idle and servo output is active
- **THEN** the player writes its angles at one tick and the keyframe has ended 10 ms later

#### Scenario: Invalid keyframe

- **WHEN** a keyframe with an angle of 181 or a duration of 60001 ms is supplied while Forward plays with motion 6 pending
- **THEN** it is rejected, no identifier is returned, Forward continues and motion 6 stays pending

### Requirement: Requests while a motion plays

The player SHALL play each accepted motion request or keyframe once, and SHALL NOT repeat a motion of its own accord. A motion request or keyframe accepted while a motion or keyframe plays SHALL become the single pending request, replacing any earlier pending request, and SHALL start when the current motion or keyframe ends. A request for the motion that is playing SHALL be pending like any other, so that it plays once more after the current run. When nothing is pending at the end of a motion or keyframe, the player SHALL become idle and SHALL write nothing until the next request.

#### Scenario: Latest request wins

- **WHEN** motion 4 and then motion 5 are requested while Forward plays
- **THEN** Forward completes, motion 5 plays next and motion 4 is never played

#### Scenario: Same motion repeated

- **WHEN** motion 2 is requested while Forward plays
- **THEN** Forward completes and a second Forward starts at the next tick from the last Forward keyframe's angles

#### Scenario: Played once

- **WHEN** motion 2 is requested once while the player is idle and nothing else is requested
- **THEN** Forward plays its 165 ticks once, and the player then reports itself idle and writes no further angles

### Requirement: Stop

A stop command SHALL end the current motion or keyframe before the next tick and SHALL discard the pending request, including a request accepted while the player was idle that has not yet had its first tick. The servos SHALL keep the last angles written, which servo output's commanded angles hold, and the next motion SHALL start from them. A stop while nothing plays and nothing is pending SHALL have no effect.

#### Scenario: Stop mid-motion

- **WHEN** stop is commanded at tick 40 of Forward with motion 6 pending
- **THEN** the player writes no further Forward angles, motion 6 does not play and the player reports itself idle

#### Scenario: Stop before a request starts

- **WHEN** motion 2 is requested while the player is idle and stop is commanded before the player's next tick
- **THEN** Forward never starts and the player writes no angle and sends no enable request

#### Scenario: Start pose after a stop

- **WHEN** Forward is stopped mid-keyframe and motion 1 is then requested
- **THEN** Standby's first tick starts from the angles last written during Forward

### Requirement: Non-blocking playback

Playing a motion or keyframe SHALL NOT stop the firmware from serving the drive link and the web portal: no tick SHALL take longer than 2 ms of processor time, and the player SHALL hold the processor only during its ticks. A request, keyframe or stop that reaches the player during a motion SHALL be handled at once, without waiting for the motion to end.

#### Scenario: Stop arrives during a long motion

- **WHEN** a stop command reaches the player 2 s into Push up
- **THEN** the player handles it before its next tick, without waiting for Push up's 7.18 s to elapse

### Requirement: Angles pass through calibrated servo output

The player SHALL write the table angles, before calibration, to servo output, and SHALL NOT clamp or offset them itself: the stock tables use angles from 30° to 170°.

#### Scenario: Angle outside the slider range

- **WHEN** Say Hi writes 170 to servo 0
- **THEN** the player passes 170 to servo output unchanged

### Requirement: Player state report

The player SHALL report, at any time: whether it is idle, playing a motion or playing a keyframe; the id of the current motion (0 when idle or playing a keyframe); the identifier of the current keyframe (0 when none); the id of the pending motion (0 when none); and the identifier of the pending keyframe (0 when none). A motion or keyframe accepted while the player is idle SHALL be reported as pending until its first tick, and as current from that tick, including an enable tick, until it ends.

#### Scenario: State during a motion

- **WHEN** the state is read during Forward with motion 6 pending
- **THEN** it reports playing a motion, current motion 2, keyframe 0, pending motion 6 and pending keyframe 0

#### Scenario: State when idle

- **WHEN** the state is read after a motion has ended with nothing pending
- **THEN** it reports idle, current motion 0, keyframe 0, pending motion 0 and pending keyframe 0

#### Scenario: State before the first command

- **WHEN** the state is read after a reset before any command
- **THEN** it reports idle, current motion 0, keyframe 0, pending motion 0 and pending keyframe 0
