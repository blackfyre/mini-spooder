# motion-playback Specification

## Purpose

Plays the stock firmware's motions (ids 1–15) and its zero position (id 100) on the robot's eight servos exactly as the stock firmware plays them, from keyframe data taken from the stock flash image, and lets the web portal and the drive link start, queue, stop and replace motions without blocking the rest of the firmware.

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
| 100 | Zero position | Zero-position routine |

A request for any other id SHALL be rejected and SHALL change neither the motion that is playing nor the pending request.

#### Scenario: Known id

- **WHEN** motion 2 is requested while the player is idle
- **THEN** the player accepts the request and plays Forward

#### Scenario: Unknown id

- **WHEN** motion 16, 0, 99 or 101 is requested while motion 13 plays with motion 2 pending
- **THEN** the request is rejected, motion 13 continues and motion 2 stays pending

#### Scenario: Composite motion

- **WHEN** motion 9 is requested and plays to the end
- **THEN** the player plays the 7 Say Hi keyframes followed by the 2 Standby keyframes, and reports motion 9 as current throughout

### Requirement: Keyframe data matches the stock image

Each motion table SHALL hold, in order, the keyframes stored for that motion in `baseline/original-flash-4MB.bin`: per keyframe, one angle in degrees for each servo index 0–7 and a duration in milliseconds. The tables SHALL have these keyframe counts: Standby 2, Forward 11, Backward 11, Left shift 11, Right shift 11, Turn left 8, Turn right 8, Lie 1, Say Hi 7, Fighting 11, Push up 11, Sleep 2, Dance 1 10, Dance 2 9, Dance 3 10. The zero pose SHALL be the stock image's zero pose: 135, 45, 135, 45, 45, 135, 45, 135 for servos 0–7. The repository's motion data SHALL be generated from the stock image, and a check SHALL fail when the generated data and the image differ or when the image's SHA-256 hash is not the one recorded in `baseline/README.md`.

#### Scenario: Data agrees with the image

- **WHEN** `mise run check-motions` runs on an unmodified checkout
- **THEN** it verifies the image hash, re-extracts every table and the zero pose, finds them identical to the checked-in data and exits with status 0

#### Scenario: Edited table

- **WHEN** one angle or duration in the checked-in motion data is changed by hand
- **THEN** `mise run check-motions` exits non-zero and names the motion and keyframe that differ

#### Scenario: Wrong image

- **WHEN** `mise run check-motions` runs against an image whose SHA-256 hash differs from the recorded hash
- **THEN** it exits non-zero without comparing the tables

### Requirement: Stock interpolation

The player SHALL move the servos through a table one keyframe at a time, on a 10 ms tick, with the stock firmware's arithmetic. For a keyframe of duration `T` ms, the player SHALL run `floor(T / 10)` ticks, numbered `i = 0 … floor(T / 10) − 1`. At each tick, for each servo index 0–7 in ascending order, with start angle `A` and keyframe angle `B`:

- when `A = B`, the player SHALL write nothing to that servo;
- otherwise it SHALL write `A + d` when `B > A`, or `A − d` when `B < A`, where `d = floor((10·i·|B − A| + floor(T / 2)) / T)`.

After the last tick of a keyframe, each servo's start angle for the next keyframe SHALL be that keyframe's angle `B`, whether or not `B` itself was written. The player SHALL NOT write the keyframe angles at the end of a keyframe or at the end of a motion.

#### Scenario: First tick of a keyframe

- **WHEN** a keyframe starts for a servo whose start angle is 135 and whose keyframe angle is 90
- **THEN** the player writes 135 to that servo at tick 0

#### Scenario: Last tick of a keyframe

- **WHEN** a 200 ms keyframe moves a servo from 135 to 90
- **THEN** the player runs 20 ticks, writes 92 to that servo at tick 19 and writes nothing further for that keyframe

#### Scenario: Duration not a multiple of 10 ms

- **WHEN** a keyframe has a duration of 375 ms
- **THEN** the player runs 37 ticks for it and uses 187 as `floor(T / 2)`

#### Scenario: Standby from the zero pose

- **WHEN** motion 1 plays to the end with the zero pose as the start pose
- **THEN** the last angles written are 71 to servo 0 and 88 to servo 1, as the stock firmware leaves them

#### Scenario: Unchanged servo

- **WHEN** a keyframe's angle for a servo equals that servo's start angle
- **THEN** the player writes nothing to that servo during the keyframe

### Requirement: Playback timing

Each tick SHALL be due 10 ms after the time at which the previous tick actually ran, so consecutive ticks are never less than 10 ms apart. When the firmware gives the player the chance to run every 1 ms, a motion's duration SHALL therefore be 10 ms times the sum of its keyframes' tick counts: for example 1650 ms for Forward (11 keyframes of 15 ticks). The player SHALL run at most one tick at each chance to run and SHALL NOT make up missed ticks, so a delay lengthens the motion rather than compressing it. A motion SHALL end at the player's first chance to run at or after 10 ms after its last tick ran, which is when its next tick would have been due. A pending motion SHALL run its first tick at that same chance, and with nothing pending the player SHALL report itself idle from it. A motion requested while the player is idle SHALL run its first tick at the player's next chance to run.

#### Scenario: On-time ticks

- **WHEN** Forward plays and the player is given the chance to run every 1 ms of a simulated clock
- **THEN** its 165 ticks fall at 0, 10, 20 … 1640 ms after the first tick and it ends at 1650 ms

#### Scenario: Consecutive motions

- **WHEN** Forward plays with motion 2 pending and the player is given the chance to run every 1 ms
- **THEN** the second Forward's first tick runs 1650 ms after the first Forward's first tick

#### Scenario: Late chance at the end of a motion

- **WHEN** Forward's last tick runs at 1640 ms with motion 6 pending and the player's next chance to run is at 1700 ms
- **THEN** Forward ends at 1700 ms and Turn left runs its first tick at 1700 ms

#### Scenario: Late run

- **WHEN** a motion plays and the player is next given the chance to run 50 ms after its previous tick
- **THEN** the player runs exactly one tick at that chance and the motion's remaining ticks are all still played

#### Scenario: Slightly late tick

- **WHEN** a tick due at 10 ms runs at 19 ms and the player is then given the chance to run every 1 ms
- **THEN** the next tick runs at 29 ms, not at 20 ms

### Requirement: Zero-position routine

Motion 100 SHALL write the zero pose to servos 0–7, one servo per tick in ascending index order with no interpolation, and SHALL then hold for 30 ticks (300 ms) before the player reports the motion as finished, 38 ticks in all. After motion 100 the start angles for the next motion SHALL be the zero pose.

#### Scenario: Zero position

- **WHEN** motion 100 is requested while the player is idle
- **THEN** the player writes 135 to servo 0 at tick 0, 45 to servo 1 at tick 1, and so on to 135 to servo 7 at tick 7, writes nothing for the next 30 ticks and becomes idle 380 ms after tick 0

### Requirement: Standby pose

The Standby table SHALL move all servos to 90 in a 200 ms keyframe and then to the standby pose, 70, 90, 90, 110, 110, 90, 90, 70 for servos 0–7, in a second 200 ms keyframe. After motion 1 the start angles for the next motion SHALL be the standby pose.

#### Scenario: Standby keyframes

- **WHEN** the Standby table is read from the generated data
- **THEN** it holds the keyframes 90, 90, 90, 90, 90, 90, 90, 90 for 200 ms and 70, 90, 90, 110, 110, 90, 90, 70 for 200 ms

### Requirement: Start-up pose

At start-up, once servo output is ready, the player SHALL run the zero-position routine before any requested motion, as the stock firmware does at boot. A motion requested during it SHALL be pending and SHALL start after it. A stop or return-to-standby command SHALL interrupt it like any other motion. Until the player has written an angle, it SHALL use and report the zero pose as the start angles.

#### Scenario: Request during start-up

- **WHEN** motion 2 is requested while the start-up zero-position routine runs
- **THEN** the routine completes its 38 ticks and Forward then starts from the zero pose

#### Scenario: Stop during start-up

- **WHEN** stop is commanded after the start-up routine has written servos 0–2
- **THEN** the player writes no further zero-pose angles and reports itself idle, with servos 3–7 never written

#### Scenario: Return to standby during start-up

- **WHEN** return to standby is commanded after the start-up routine has written servos 0–2
- **THEN** the routine ends before its next tick and motion 1 plays, from the zero pose for servos 0–2 and from the zero pose as the unwritten start angle for servos 3–7

### Requirement: Requests while a motion plays

A motion request accepted while a motion plays SHALL become the single pending request, replacing any earlier pending request, and SHALL start when the current motion ends. A request for the motion that is playing SHALL be pending like any other, so that it plays again after the current run. When nothing is pending at the end of a motion, the player SHALL become idle and SHALL write nothing until the next request.

#### Scenario: Latest request wins

- **WHEN** motion 4 and then motion 5 are requested while Forward plays
- **THEN** Forward completes, motion 5 plays next and motion 4 is never played

#### Scenario: Same motion repeated

- **WHEN** motion 2 is requested while Forward plays
- **THEN** Forward completes and a second Forward starts at the next tick from the last Forward keyframe's angles

#### Scenario: Idle after the last motion

- **WHEN** a motion ends with nothing pending
- **THEN** the player reports itself idle and writes no further angles

### Requirement: Stop

A stop command SHALL end the current motion before the next tick and SHALL discard the pending request, including a request accepted while the player was idle that has not yet had its first tick. The servos SHALL keep the last angles written. After a stop, each servo's start angle for the next motion SHALL be the last angle written to that servo, or its previous start angle if no angle has been written to it since. A stop while nothing plays and nothing is pending SHALL have no effect.

#### Scenario: Stop mid-motion

- **WHEN** stop is commanded at tick 40 of Forward with motion 6 pending
- **THEN** the player writes no further Forward angles, motion 6 does not play and the player reports itself idle

#### Scenario: Stop before a request starts

- **WHEN** motion 2 is requested while the player is idle and stop is commanded before the player's next tick
- **THEN** Forward never starts and the player writes no angle

#### Scenario: Start pose after a stop

- **WHEN** Forward is stopped mid-keyframe and motion 1 is then requested
- **THEN** Standby's first tick starts from the angles last written during Forward, not from that keyframe's angles

### Requirement: Return to standby

A return-to-standby command SHALL end the current motion before the next tick, discard the pending request (including one not yet started) and play motion 1 from the last angles written, with the same start angles as after a stop. When the player is idle, it SHALL play motion 1. This command is distinct from a request for motion 1, which waits for the current motion to end.

#### Scenario: Standby during a motion

- **WHEN** return to standby is commanded during Dance 1 with motion 3 pending
- **THEN** Dance 1 ends before the next tick, motion 3 is discarded and motion 1 plays from the angles last written

#### Scenario: Request for motion 1 during a motion

- **WHEN** motion 1 is requested during Dance 1
- **THEN** Dance 1 completes and motion 1 plays after it

### Requirement: Non-blocking playback

Playing a motion SHALL NOT stop the firmware from serving the web portal and the drive link: no tick SHALL take longer than 2 ms of processor time, and the player SHALL hold the processor only during its ticks. A request that reaches the firmware during a motion SHALL be handled without waiting for the motion to end.

#### Scenario: Stop arrives during a long motion

- **WHEN** a stop command reaches the firmware 2 s into Push up
- **THEN** the player handles it before its next tick, without waiting for Push up's 7.18 s to elapse

### Requirement: Angles pass through calibrated servo output

The player SHALL write the table angles, before calibration, through `servo-output`, which applies the per-servo offsets of `servo-calibration`. The player SHALL NOT clamp the table angles: the stock tables use angles from 30° to 170°.

#### Scenario: Angle outside the slider range

- **WHEN** Say Hi writes 170 to servo 0
- **THEN** the player passes 170 to servo output unchanged

### Requirement: Player state report

The player SHALL report, at any time: whether it is idle or playing, the id of the current motion (0 when idle), the id of the pending motion (0 when none), and for each servo 0–7 the last angle it wrote before calibration, or the zero pose for a servo it has not yet written.

#### Scenario: State during a motion

- **WHEN** the state is read during Forward with motion 6 pending
- **THEN** it reports playing, current motion 2, pending motion 6, and the eight angles last written

#### Scenario: State when idle

- **WHEN** the state is read after a motion has ended with nothing pending
- **THEN** it reports idle, current motion 0 and pending motion 0
