# Spec Delta

## Purpose

MiniSpooder.pak: a NextUI tool pak on a TrimUI Smart Pro that plays the robot's stock motions over the drive link (`drive-link`) on the robot's own access point, shows what the robot reports and the link's quality, and leaves the robot at rest when it exits; with the rules for building, installing and testing it without the robot.

## ADDED Requirements

### Requirement: Packaging and install
The pak SHALL be delivered as a zip of one folder, `MiniSpooder.pak`, which the user unzips into `Tools/tg5040/` on the TrimUI's SD card. The folder SHALL hold `launch.sh`, the pak's binary for linux/arm64 and the SDL2 add-on libraries the device lacks, in `lib/`. The binary SHALL be an AArch64 ELF that needs no GLIBC symbol version above 2.31. The pak SHALL NOT read, write or depend on any other pak's folder, and SHALL write its log, when NextUI provides a log directory, to `MiniSpooder.txt` in it.

#### Scenario: Device build and zip
- **WHEN** the device build and the packaging finish
- **THEN** the binary is an AArch64 ELF whose highest required GLIBC version is at most 2.31, and the zip's entries are `MiniSpooder.pak/launch.sh`, `MiniSpooder.pak/<binary>` and `MiniSpooder.pak/lib/` with its libraries, both files executable

#### Scenario: Wrong architecture or glibc rejected
- **WHEN** the build's check is given an AArch64 binary needing GLIBC_2.34, or an x86-64 binary
- **THEN** the check fails and no zip is written

#### Scenario: Next to Spooder.pak
- **WHEN** `MiniSpooder.pak` is installed in `Tools/tg5040/` beside an existing `Spooder.pak`
- **THEN** NextUI lists both tools, and running MiniSpooder changes no file inside `Spooder.pak`

### Requirement: Desktop build
The same program SHALL also build for desktop Linux (amd64). Started without the device flag, it SHALL use the robot address given on its command line (defaulting to the drive link's robot address and port), SHALL run no WiFi step, no address check and no power-saving change, and SHALL leave the Power button unhandled; everything else (mapping, frames, screen, leaving with Menu) SHALL behave as on the device.

#### Scenario: Desktop start against the fake robot
- **WHEN** the desktop build's start-up is given the fake robot's loopback address and no device flag
- **THEN** it runs no WiFi command and no address check, and plays motions on the fake robot as it would on the robot

### Requirement: Joining the robot's network
With the device flag, before it sends any command frame, the pak SHALL check that `wlan0` holds an IPv4 address in the robot's subnet as `drive-link` defines it. If it does not, it SHALL renew the lease once and check again. If there is still none, it SHALL show a message naming the robot's network and its password (as `drive-link` defines them) to be joined in NextUI's WiFi settings, naming the network `wlan0` is on if any, with the choices to retry the check or quit, and SHALL send no command frame until the check passes. The pak SHALL NOT join or change WiFi networks itself.

#### Scenario: Already joined
- **WHEN** `wlan0` holds an address in the robot's subnet at start
- **THEN** the pak starts its link without renewing the lease

#### Scenario: Stale lease
- **WHEN** `wlan0` holds an address outside the robot's subnet and one renewal gives an address inside it
- **THEN** the pak renews exactly once and then starts its link

#### Scenario: Not joined
- **WHEN** `wlan0` has no address in the robot's subnet after the renewal
- **THEN** the pak shows the robot's network name and password and the current network, sends no datagram, and checks again (renewing once more) only when the user chooses retry

### Requirement: Command datagrams
When its link starts, and again when it resumes from suspend, the pak SHALL send nothing to the robot for at least 350 ms, the silence `drive-link` requires of a remote before its first frame. After that it SHALL send one command frame per UDP datagram to the robot's address and port, at `drive-link`'s 50 ms command period, every frame being `drive-link`'s command frame carrying the pak's complete desired state: its requested motion (0 for stop) and `drive-link`'s single motion-request counter, with the frame sequence number advancing by one per frame. It SHALL send every datagram of a run, including those after a suspend and resume, from one socket bound once, so from one source port. Its frames of a run SHALL carry motion 0 and counter 0 until the first press is taken, and a motion SHALL be requested only by a control press made during the run. A failed send SHALL be counted and SHALL NOT stop the next period's send.

#### Scenario: Silence before the first frame
- **WHEN** the pak starts its link, or resumes from suspend
- **THEN** a listener on the robot's address receives no datagram from it for at least 350 ms, and then the first command frame

#### Scenario: Rate, sequence and source port
- **WHEN** the pak runs for 1 s after its first frame
- **THEN** a listener on the robot's address receives one datagram every 50 ms (within one period's tolerance), each exactly one valid command frame, all from one source port, with sequence numbers rising by one

#### Scenario: Quick restart
- **WHEN** the pak exits and is started again within 300 ms of its last frame, reusing the same source port
- **THEN** its first frame reaches the robot more than 300 ms after the previous run's last frame, so `drive-link` takes it as a new session's baseline and makes no request from it

#### Scenario: Start with no motion requested
- **WHEN** the pak starts its link and no control has been pressed
- **THEN** every command frame carries motion 0 and counter 0

#### Scenario: Send error
- **WHEN** one send fails
- **THEN** the send-error count rises by one and the next period's frame is sent

### Requirement: Control mapping
The pak SHALL map its controls to motion requests and stop requests as follows, each acting once on its press, with no repeat while held and nothing on release:

| Control | Request |
|---|---|
| D-pad up / down / left / right | motion 2 Forward / 3 Backward / 6 Turn left / 7 Turn right |
| L1 / R1 | motion 4 Left shift / 5 Right shift |
| Start | motion 1 Standby |
| A / X / Y | motion 9 Say Hi / 10 Fighting / 11 Push up |
| R2 held, then A / X / Y | motion 13 Dance 1 / 14 Dance 2 / 15 Dance 3 |
| L2 held, then A / X / Y | motion 8 Lie / 12 Sleep / 100 Zero position |
| B, with or without a trigger held | stop |

A press SHALL be taken only while the link is live: at least two telemetry frames have been accepted since the first command frame after the link last started, resumed or was lost (see Link quality) was sent, and the frame last taken as newest was accepted no more than 250 ms ago. A datagram read during the silence before the first frame SHALL be discarded without being counted or shown. Each command frame SHALL carry at most one new request: a motion press made after a request has been set and before any frame carrying it has been sent SHALL NOT be taken, while a stop press in that time SHALL replace the unsent request. After a stop, a motion press SHALL NOT be taken until a telemetry frame echoes the stop's counter value or the stop is marked not confirmed; a stop press SHALL always be taken while the link is live. A press while the link is not live SHALL request nothing, change no counter and be shown as not sent; it SHALL NOT be queued. A motion request SHALL set the requested motion to that id and advance the motion-request counter by one; a stop SHALL set the requested motion to 0 and advance the same counter by one, wrapping from 255 to 0. No control SHALL request motion 99 (Centre), which the stock controller page offers no button for. L2 and R2 SHALL count as held beyond half travel. While both L2 and R2 are held, A, X and Y SHALL request nothing. A face button's layer SHALL be the one held at the moment it is pressed. Select SHALL have no function. Menu and Power SHALL NOT reach the mapping. The left and right sticks SHALL request nothing.

#### Scenario: One request per press
- **WHEN** D-pad up is pressed and held for 2 s, then released
- **THEN** the frames after the press request motion 2 with the motion-request counter advanced by exactly one, and neither the hold nor the release advances it again

#### Scenario: Layer from the trigger
- **WHEN** R2 is pressed beyond half travel and A is then pressed
- **THEN** the next frame requests motion 13 with the counter advanced by one, and motion 9 is not requested

#### Scenario: Trigger released before the button
- **WHEN** L2 is held, Y is pressed, and L2 is released while Y is still held
- **THEN** exactly one request for motion 100 is made, and nothing is requested when Y is released

#### Scenario: Both triggers held
- **WHEN** L2 and R2 are both held beyond half travel and A is pressed
- **THEN** no motion is requested and the counter does not change

#### Scenario: Stop always available
- **WHEN** B is pressed, with or without a trigger held, while a motion plays
- **THEN** the next frame carries motion 0 with the motion-request counter advanced by one

#### Scenario: Stop after a motion request
- **WHEN** D-pad up and then B are pressed while the link is live, each press in a different command period
- **THEN** the counter advances once with motion 2 and once more with motion 0, so the robot receives two requests

#### Scenario: Standby pressed right after stop
- **WHEN** a motion plays, B is pressed, and Start is pressed before any telemetry frame has echoed the stop's counter value
- **THEN** the frames carry the stop with its counter value until the echo arrives, Start is shown as not sent, and the robot receives the stop

#### Scenario: Two presses in one period
- **WHEN** D-pad up and then A are pressed before the next command frame is sent
- **THEN** the next frame requests motion 2 with the counter advanced by one, and A is shown as not sent

#### Scenario: Press before the robot answers
- **WHEN** the pak has started its link and no telemetry has arrived yet, and D-pad up is pressed
- **THEN** the counter does not change, the screen shows the press as not sent, and frames sent after telemetry then arrives still carry motion 0 and counter 0

#### Scenario: Press after a resume
- **WHEN** the pak resumes from suspend and A is pressed before two telemetry frames have arrived since the first command frame after the resume
- **THEN** no motion is requested and the counter does not change

#### Scenario: Telemetry queued before a suspend
- **WHEN** telemetry frames that arrived before a suspend are read during the silence after the resume
- **THEN** they are discarded without being counted, the link is not live, and a press is not taken

#### Scenario: Every stock motion reachable
- **WHEN** each control of the table is pressed in turn while the link is live
- **THEN** the requested motion ids cover 1–15 and 100, each exactly once

### Requirement: Received frames
The pak SHALL accept a datagram only when it comes from the robot's address and port and holds exactly one valid telemetry frame (version, type, length and CRC as `drive-link` defines them) spanning the whole datagram. It SHALL drop every other datagram, counting as invalid each one from the robot's address and port, and SHALL never act on the contents of a dropped datagram. Accepted frames SHALL change only what the screen shows and the leave confirmation, never what the pak requests.

#### Scenario: Valid telemetry
- **WHEN** a datagram from the robot's address and port holds exactly one valid telemetry frame
- **THEN** the screen shows its values

#### Scenario: Invalid datagram
- **WHEN** a datagram from the robot's address and port holds a frame with a bad CRC, a valid frame with one extra byte, two frames, or a command frame
- **THEN** it is dropped, the invalid count rises by one, and nothing else on the screen changes

#### Scenario: Foreign sender
- **WHEN** a valid telemetry frame arrives from another address or port
- **THEN** it is dropped and nothing on the screen changes

### Requirement: Drive screen
The pak SHALL show one live screen at 1280×720, redrawn at least 10 times a second. From the newest telemetry it SHALL show the motion the robot reports by its stock name (the ids 1–15, 99 and 100 of `motion-playback`; `MOTION <n>` for an id it does not know, and `IDLE` for none), the eight joint angles in degrees labelled by servo index and its GPIO (`---` for an angle the robot reports as not commanded), and the robot's own link statistics. From its own state it SHALL show the last request, by motion name or as stop, with its status: `NOT SENT` when the press was not taken, `REQUESTED` until a telemetry frame echoes the request's counter value, then `TAKEN` when the echoed result is motion started or stop requested, `UNKNOWN` when it is unknown motion, `NOT TAKEN` when the counter value is echoed as a new session's baseline with no result, and `NOT CONFIRMED` when no echo has arrived 1 s after the request. It SHALL show the active control layer with the motion each of A, X and Y would request in it. It SHALL show the link's quality (see Link quality) and, along the bottom, the controls for stop, standby and leaving. When no telemetry frame has been taken as newest for 500 ms, it SHALL mark the robot's values as stale, keeping the last values visible. Frames SHALL be shown in sequence order: a telemetry frame older by its sequence number than the one shown SHALL NOT replace it, except a frame that rebaselines the sequence (see Link quality), which SHALL be shown whatever its sequence number.

#### Scenario: Motion by name
- **WHEN** telemetry reports motion 14 playing
- **THEN** the screen shows `DANCE 2` as the robot's motion

#### Scenario: Centre named
- **WHEN** telemetry reports motion 99 playing, started from the web portal
- **THEN** the screen shows `CENTRE` as the robot's motion

#### Scenario: Request taken
- **WHEN** the pak requests Forward with counter value 5, and a telemetry frame then echoes counter 5 with the result motion started
- **THEN** the screen shows Forward as the last request, marked `TAKEN`

#### Scenario: Request lost to a new session
- **WHEN** the pak requests Forward with counter value 5, and a telemetry frame then echoes counter 5 with the result none yet
- **THEN** the screen shows Forward as the last request, marked `NOT TAKEN`

#### Scenario: Servo not commanded
- **WHEN** telemetry reports 255 for servo 4
- **THEN** the well for servo 4 (GPIO 2) shows `---`, not a number

#### Scenario: Layer shown
- **WHEN** L2 is held beyond half travel
- **THEN** the screen shows the L2 layer with A, X and Y labelled Lie, Sleep and Zero position, and returns to the base layer's labels when L2 is released

#### Scenario: Link lost
- **WHEN** no telemetry has arrived for 500 ms
- **THEN** the robot's values are marked stale and the time since the last frame keeps rising

#### Scenario: Older frame after a newer one
- **WHEN** telemetry 10 is shown and telemetry 9 then arrives
- **THEN** the screen keeps showing telemetry 10's values and counts frame 9 as late

### Requirement: Screen style
The drive screen and the pak's message screens SHALL follow the Blackfyre design system's dark app rules: the window on `bf-ink`, panels on `bf-ink-raised` outlined with `bf-shadow-outline-dark`, wells on `bf-ink-code`, text in `bf-surface`, labels and muted text in `bf-ink-subtle`, orange text in `bf-accent-on-dark`, text on orange fills in `bf-on-accent`, status fills `bf-ok` and `bf-warn` with `bf-ink` text, always with a text label and never colour alone. Labels, numbers and keycaps SHALL be uppercase JetBrains Mono; read text SHALL be Archivo. Every colour SHALL come from the Blackfyre `tokens.json` values, not from another application's code.

#### Scenario: Colours match the tokens
- **WHEN** the screen's colour table is compared with the pak's committed snapshot of `tokens.json`
- **THEN** every colour the screen uses equals its token's value, and the check fails if a token is missing or differs

#### Scenario: Status not by colour alone
- **WHEN** the link is stale
- **THEN** the screen shows the word `STALE`, not only a change of colour

### Requirement: Link quality
The pak SHALL show the WiFi link's quality: the signal level of `wlan0` in dBm (or unknown), with a dot labelled by level, `bf-ok` from -67 dBm, `bf-warn` from -80 dBm and `bf-accent` below; telemetry frames received in the last second; invalid datagrams, lost frames (gaps in the robot's telemetry sequence numbers), late frames (a sequence number not newer than the newest) and send errors, counted since start; and the time since the last valid frame. The first valid frame of a run, the first after a resume from suspend and the first after 500 ms without a frame taken as newest (so also when every valid frame for 500 ms was late, as after a robot restart whose sequence starts again lower) SHALL set the newest sequence number without counting a frame lost or late. Sequence comparisons SHALL wrap modulo the sequence number's range. It SHALL NOT show a round-trip time.

#### Scenario: Loss from sequence numbers
- **WHEN** telemetry frames arrive with sequence numbers 10, 11, 14
- **THEN** the lost count rises by two

#### Scenario: Late frame
- **WHEN** a telemetry frame with sequence number 12 arrives after 14
- **THEN** the late count rises by one and frame 12 is not shown

#### Scenario: Sequence wrap
- **WHEN** telemetry frames arrive with sequence numbers 254, 255, 0, 1
- **THEN** no frame is counted lost or late

#### Scenario: Gap or robot restart
- **WHEN** telemetry stops for 500 ms or more, or the pak resumes from suspend, and the next frame's sequence number is any value
- **THEN** that frame is shown and nothing is counted lost or late

#### Scenario: Robot restart while frames keep arriving
- **WHEN** telemetry 100 is shown and the robot restarts, sending telemetry 0, 1, 2 … every 100 ms
- **THEN** those frames are counted late and the values are marked stale until 500 ms after frame 100 was taken, and the first frame after that is shown and taken as newest without being counted late

#### Scenario: Signal unknown
- **WHEN** neither source of the signal level gives a value
- **THEN** the screen shows the signal as unknown, not as a number

### Requirement: Leaving safely
When Menu is pressed, or Power is pressed briefly or held 2 s, the pak SHALL first request stop (motion 0, advancing the motion-request counter once, whether or not the link is live), keep sending command frames, and wait until a telemetry frame newer by its sequence number than every telemetry frame taken before the press echoes that stop's counter value and reports no motion playing, for at most 3 s from the press. The stop SHALL be the pak's only safety action on leaving: it ends the motion and the robot holds its pose; what the robot does once the pak's frames stop is `drive-link`'s link-loss rule. It SHALL show that it is stopping the robot while it waits. While it waits, no control SHALL change what it requests, and a second Menu or Power press SHALL NOT start another leave. Only then SHALL it exit (Menu), run NextUI's suspend (a short Power press) or power off (Power held). With no telemetry arriving, it SHALL wait the full 3 s before it acts. After a suspend returns, the pak SHALL be driving again, with the link's sequence counting rebaselined, and SHALL take no press until the link is live again.

#### Scenario: Leave while a motion plays
- **WHEN** Menu is pressed while telemetry reports Forward playing and telemetry keeps arriving
- **THEN** the next command frame carries motion 0 with the counter advanced by one, and the pak exits when a telemetry frame newer than the press echoes that counter value and reports no motion playing, and not before, unless 3 s pass first

#### Scenario: Delayed idle frame
- **WHEN** Menu is pressed while a motion plays, and a telemetry frame reporting no motion arrives that echoes an earlier counter value, or that is older than the newest frame taken before the press
- **THEN** the pak does not exit on that frame

#### Scenario: Controls during the wait
- **WHEN** A, Start or the D-pad is pressed while the pak waits to leave
- **THEN** no motion is requested and the counter does not change

#### Scenario: Robot does not confirm
- **WHEN** Menu is pressed and no telemetry arrives
- **THEN** the pak exits 3 s after the press

#### Scenario: Power pressed briefly
- **WHEN** Power is pressed briefly while the robot is idle and telemetry keeps arriving
- **THEN** the stop is requested and confirmed before the suspend runs, and after resume no press is taken until the link is live again

### Requirement: WiFi power saving
With the device flag, at start the pak SHALL read the WiFi power-saving setting of `wlan0` and turn it off. However the pak exits (Menu, a signal, a crash of the binary, or power-off through the pak), it SHALL restore the setting it read and stop every process it started. It SHALL restore the setting before it suspends or powers off, and turn power saving off again when a suspend returns.

#### Scenario: Normal exit
- **WHEN** power saving was on at start and the user leaves with Menu
- **THEN** power saving is on again after the pak exits

#### Scenario: Suspend
- **WHEN** power saving was on at start and the user suspends with Power
- **THEN** power saving is on when the suspend runs, and off again after it returns

#### Scenario: Binary crashes
- **WHEN** the binary exits with an error while power saving is off
- **THEN** the launch script restores the setting it read

### Requirement: Fake robot
The project SHALL provide a fake robot for tests and desktop runs that listens on a loopback UDP address and follows `drive-link`'s contract towards a remote (its session, one-remote, sequence, counter, telemetry and link-loss rules, as `drive-link` specifies them, including motion 0 as stop and the echo of the last processed counter value and its result), standing in for the motions by reporting a requested motion as playing for a fixed time per motion and then idle, holding a request made while a motion plays as `motion-playback`'s single pending request, and reporting idle after a stop or link loss. It SHALL allow tests to drop or corrupt the telemetry it sends. It SHALL NOT model gaits or joint motion beyond reporting fixed joint angles.

#### Scenario: Plays and stops
- **WHEN** the pak requests motion 2 and then stop
- **THEN** the fake robot's telemetry reports motion 2 playing with the result motion started, then no motion with the result stop requested, echoing each request's counter value, and the pak's lost and invalid counts stay zero

#### Scenario: Restarted remote within the liveness time
- **WHEN** a sender's frames stop, and within 300 ms the same address and port sends frames whose sequence number is 9 to 127 ahead modulo 256 and whose counter has changed
- **THEN** the fake robot ends the session, takes the next frame as a new session's baseline, and makes no request

#### Scenario: Old frame keeps the session
- **WHEN** a frame whose sequence number is 128 to 255 behind modulo 256, or equal to the last accepted one, arrives from the locked sender
- **THEN** the fake robot discards it, keeps the session and makes no request

#### Scenario: Second remote ignored
- **WHEN** a second sender sends command frames while the first keeps sending
- **THEN** the fake robot neither acts on nor answers the second sender

#### Scenario: Corrupted telemetry
- **WHEN** the fake robot is told to corrupt every tenth telemetry frame
- **THEN** the pak's invalid count rises by one for each corrupted frame and its screen values come only from the valid ones

### Requirement: Protocol kept in step
The pak's encoding and decoding of the drive link's frames SHALL agree byte for byte with the robot's (`core/`) codec, checked against `drive-link`'s golden frames file, which the pak's tests read in place and do not copy.

#### Scenario: Golden frames agree
- **WHEN** the pak's tests run against the golden frames file
- **THEN** each accepted command frame the pak encodes from its golden fields equals the golden bytes, each accepted telemetry frame decodes to its golden fields, and each frame marked rejected is rejected

#### Scenario: Golden file changed
- **WHEN** a protocol change regenerates the golden file and the pak's codec is not updated
- **THEN** the pak's protocol tests fail and name the frame that differs
