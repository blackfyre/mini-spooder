# Spec Delta

## Purpose

The robot side of the link between the robot and its remote, `MiniSpooder.pak` on a TrimUI Smart Pro: the access point, the UDP port, the frame format, the command and telemetry frames, and the rules that keep a lost, repeated, corrupted or foreign frame from moving the robot, with the link-loss failsafe when the remote stops sending.

## ADDED Requirements

### Requirement: Access point
The robot SHALL run a Wi-Fi access point with the stock network name `Spider Robot` and the stock password `12345678`, with WPA2-PSK security, on channel 1. The robot SHALL have the address 192.168.4.1 with netmask 255.255.255.0, and SHALL lease addresses in that subnet to joining stations by DHCP. It SHALL accept at least two stations at the same time, so that the remote and a phone using the web portal can both join. The robot SHALL run in access point mode only and SHALL NOT join other networks. It SHALL NOT run a DNS server or an mDNS responder, as the stock firmware runs neither. It SHALL apply these settings from the firmware at every boot, with the SDK's persistent storage of Wi-Fi settings turned off, so that it neither stores them in the SDK's configuration sectors (0x3FC000–0x3FFFFF) nor depends on what those sectors hold.

#### Scenario: Remote joins
- **WHEN** a station joins `Spider Robot` with the password `12345678`
- **THEN** it receives an address in 192.168.4.0/24 by DHCP and can reach 192.168.4.1

#### Scenario: Remote and phone together
- **WHEN** the remote and a phone join the access point one after the other
- **THEN** both stay associated, and each can reach 192.168.4.1

#### Scenario: Boot with a different stored configuration
- **WHEN** the SDK's configuration sectors hold another network name or password, stored by an earlier firmware
- **THEN** the robot still starts `Spider Robot` with the password `12345678`

#### Scenario: No name service
- **WHEN** a joined station sends a DNS query to 192.168.4.1 on UDP port 53
- **THEN** the robot sends no DNS answer

### Requirement: Frame format
Every frame on the link SHALL have this layout, with multi-byte fields little-endian:

| Offset | Size | Field |
|---|---|---|
| 0 | 1 | start marker, 0xA5 |
| 1 | 1 | protocol version, 1 |
| 2 | 1 | frame type: 1 = command, 2 = telemetry |
| 3 | 1 | sequence number, wrapping from 255 to 0 |
| 4 | 1 | payload length N, fixed per type: 2 for command, 26 for telemetry |
| 5 | N | payload |
| 5+N | 2 | CRC-16/CCITT-FALSE (polynomial 0x1021, initial value 0xFFFF, no reflection, no final XOR) over bytes 1 to 4+N |

A frame decoder SHALL reject a candidate frame that has an unknown version, an unknown type, a payload length other than its type's, or a bad CRC. It SHALL count each rejected candidate, and SHALL resume decoding at the next start marker after the rejected candidate's first byte, so that a valid frame that follows garbage or a rejected candidate is not lost. When its input ends, as at the end of a datagram, the decoder SHALL reject an incomplete candidate in the same way, so that a false start cannot hide a complete frame behind it. The decoder SHALL hold at most one maximum-size frame (33 bytes) of undecided bytes.

#### Scenario: Corrupted frame
- **WHEN** a byte stream holds a command frame with one corrupted payload byte followed by a valid command frame
- **THEN** the decoder rejects the first, counts one rejected frame, and returns the second

#### Scenario: Garbage between frames
- **WHEN** random bytes that hold no valid frame arrive between two valid frames
- **THEN** the decoder returns both valid frames

#### Scenario: False start marker
- **WHEN** a byte 0xA5 that starts no valid frame is immediately followed by a complete valid frame
- **THEN** the decoder returns the valid frame

#### Scenario: Unknown version
- **WHEN** a frame with a valid CRC carries protocol version 2
- **THEN** the decoder rejects it and counts it

### Requirement: Drive port and datagrams
The robot SHALL listen on UDP port 52041 at 192.168.4.1. The robot SHALL give every datagram exactly one outcome, deciding it in this order: first it ends a session that has lapsed; then it discards a datagram from another sender while a session is live (One remote at a time); then it discards a datagram beyond the window's budget (Datagram rate limit); only then does it validate the datagram. A datagram that reaches validation SHALL count as a remote frame only when it holds exactly one command frame of protocol version 1 with a valid CRC and no other byte, which makes it exactly 9 bytes long. Every other datagram that reaches validation, including a valid telemetry frame, a frame of another version and a datagram with extra bytes, SHALL be discarded, SHALL be counted as malformed, and SHALL change nothing else: the last accepted state, the session's liveness and the motion player stay as they were.

#### Scenario: Valid command datagram
- **WHEN** the remote sends a datagram holding one valid version 1 command frame
- **THEN** the robot accepts it as the remote's newest desired state

#### Scenario: Corrupt or foreign datagram
- **WHEN** a datagram from the locked sender, or from any sender while no session is live, arrives within the window's budget and has a bad CRC, protocol version 2, the telemetry type, a payload length other than 2, or one byte after a valid command frame
- **THEN** the robot discards it, counts it as malformed, and no motion request is made

#### Scenario: Corrupt datagram from another sender
- **WHEN** a datagram with a bad CRC arrives from another sender while a session is live
- **THEN** the robot counts it as from another sender, not as malformed

### Requirement: Command frames
The remote SHALL send a command frame every 50 ms while it drives the robot. Each frame SHALL carry the remote's complete desired state and no events:

| Payload offset | Size | Field |
|---|---|---|
| 0 | 1 | requested motion: 0 = stop, or a motion id of motion playback (the stock `pm` numbers 1–15, 99 and 100) |
| 1 | 1 | motion-request counter: the remote increments it, wrapping from 255 to 0, on every explicit motion or stop request, and repeats it unchanged in every other frame |

The requested motion SHALL be the motion of the remote's most recent request. The robot SHALL act only on the newest accepted frame.

#### Scenario: Lost frame loses nothing
- **WHEN** the remote makes a motion request, the first frame that carries the incremented counter is lost, and the next frame arrives
- **THEN** the robot makes the same one motion request as if both frames had arrived

#### Scenario: Repeated state
- **WHEN** frames keep arriving with an unchanged motion-request counter
- **THEN** the robot makes no motion request, whatever the requested motion field holds

### Requirement: Motion requests
Within a session, the robot SHALL make exactly one request for each change of the motion-request counter between consecutive accepted frames, whatever the size of the change. The request SHALL be taken from the frame that carries the change:
- a requested motion of 0 SHALL call motion playback's stop, whose effect motion playback defines;
- any other value SHALL be passed to motion playback as a motion request, which motion playback accepts or rejects, plays or keeps pending, by its own rules;
- the drive link SHALL report motion playback's answer, accepted or rejected, as the request's result, and SHALL do nothing else for a rejected value.

The first accepted frame of a session SHALL only set the baseline of the motion-request counter, so that a remote that restarts, whose counter starts again, makes no request.

#### Scenario: Request a motion while idle
- **WHEN** motion playback is idle, servo output is not in its fault mode, and within a session a frame arrives whose counter is one higher than the previous accepted frame's and whose requested motion is 2 (Forward)
- **THEN** the robot requests Forward from motion playback once, and the next telemetry frame reports the request as accepted and motion 2 as playing

#### Scenario: Request a motion while another plays
- **WHEN** Push up (11) plays and within a session a frame arrives with a changed counter and requested motion 9 (Say Hi)
- **THEN** the robot requests Say Hi once, and each telemetry frame reports the request as accepted and the current and pending motions that motion playback reports at the time the frame is built

#### Scenario: Request a stop
- **WHEN** Forward plays with motion 6 pending, and within a session a frame arrives with a changed counter and requested motion 0
- **THEN** the robot calls motion playback's stop once, before the player's next tick, and the next telemetry frame reports the request as stop requested

#### Scenario: Rejected motion id
- **WHEN** within a session a frame arrives with a changed counter and requested motion 50
- **THEN** the robot passes it to motion playback, makes no other call, and the next telemetry frame reports the request as rejected

#### Scenario: First frame of a session
- **WHEN** the first accepted frame of a session carries requested motion 9 (Say Hi) and any counter value
- **THEN** the robot makes no motion request

#### Scenario: Counter wraps
- **WHEN** within a session the counter changes from 255 to 0
- **THEN** the robot makes exactly one request

### Requirement: Sequence numbers
Within a session, with d the difference between a frame's sequence number and that of the last accepted frame, taken modulo 256:
- a frame with d from 1 to 8 SHALL be newer and accepted;
- a frame with d of 0 or from 128 to 255 SHALL be discarded and counted as out of sequence, and SHALL NOT refresh the session's liveness, so that a late or repeated datagram changes nothing;
- a frame with d from 9 to 127 SHALL be discarded, counted as out of sequence, and SHALL end the session at once, since it means the remote restarted.

#### Scenario: Reordered datagram
- **WHEN** a frame with sequence number 41 arrives after the accepted frame with sequence number 42
- **THEN** the robot discards it and counts it as out of sequence

#### Scenario: Duplicated datagram
- **WHEN** a frame with a changed counter is accepted and the network delivers the same datagram again
- **THEN** the robot discards the copy, and only one motion request is made

#### Scenario: Sequence wrap
- **WHEN** a frame with sequence number 0 arrives after the accepted frame with sequence number 255
- **THEN** the robot accepts it as newer

#### Scenario: Sequence jump
- **WHEN** the last accepted frame had sequence number 200, and within 300 ms a valid frame from the same sender carries sequence number 0
- **THEN** the robot discards it and ends the session with the link-loss failsafe, and the next valid frame starts a new session that only sets the counter baseline

### Requirement: One remote at a time
The robot SHALL lock to the sender, by IPv4 address and UDP port, of the first accepted frame of a session. While that session is live, the robot SHALL discard datagrams from every other sender without decoding them, count them as from another sender, and not charge them to the rate budget. After the session ends, the sender of the next valid command frame SHALL start a new session and become the locked sender. The robot SHALL report to the rest of the firmware whether a session is live.

#### Scenario: Second sender ignored
- **WHEN** a second station sends valid command frames with changing counters while the locked remote's session is live
- **THEN** the robot makes no request for them, counts them as from another sender, and keeps following the locked remote

#### Scenario: Handover after a lapse
- **WHEN** the locked remote stops sending for longer than 300 ms and another station then sends valid command frames
- **THEN** the robot locks to the other station, and that station's first frame only sets the counter baseline

### Requirement: Datagram rate limit
The robot SHALL decode at most 4 datagrams in each 20 ms window of a fixed grid of its millisecond clock. It SHALL discard every further datagram in that window without decoding it, and SHALL count it as rate limited. A flood SHALL NOT stop the robot from serving the web portal, playing motions, or sending telemetry.

#### Scenario: Flood from the locked sender
- **WHEN** the locked remote sends 200 valid datagrams within one 20 ms window
- **THEN** the robot decodes at most 4 of them in that window and counts the rest as rate limited

#### Scenario: Flood from another station
- **WHEN** another station floods the drive port while the locked remote sends its frames every 50 ms
- **THEN** the remote's session stays live and its motion requests are made

#### Scenario: Sustained flood
- **WHEN** a station sends 2000 datagrams per second to the drive port for 10 seconds
- **THEN** the robot does not restart, and the web portal still answers its pages

### Requirement: Session start
When a session starts, the robot SHALL call motion playback's stop before the player's next tick, so that what motion playback was doing before the session, for example a motion or keyframe started from the web portal, ends as motion playback's stop defines, and the remote's requests govern from then on. The session start SHALL make no other call that could move a servo.

#### Scenario: Remote joins during a portal motion
- **WHEN** Dance 1 started from the web portal is playing with no session live, and a remote's first valid command frame arrives
- **THEN** the session starts and the robot calls motion playback's stop before the player's next tick, and makes no motion request

### Requirement: Link loss
A session SHALL be live while the newest accepted frame from the locked sender is no older than 300 ms, its age measured from the time the robot read it from the socket. Only an accepted frame SHALL refresh the session's liveness: a malformed, rate-limited, out-of-sequence or other-sender datagram SHALL NOT. The robot SHALL read every queued datagram on each main-loop pass while no more than 128 are queued, which covers the 2000 datagrams per second of the sustained flood at loop passes of up to 20 ms, so that a frame is read at most one loop pass after it arrived and a frame waiting in the queue cannot extend the session by more than that. A loop pass that the firmware allows to exceed its pass budget, such as one that commits calibration, delays the read and the lapse detection by its excess; the robot SHALL still detect the lapse on the first pass after it. The robot SHALL detect a lapse on its first main-loop pass after the 300 ms have passed.

When a session ends, because it lapses or because of a sequence jump, the robot SHALL apply the link-loss failsafe in the same main-loop pass and before motion playback is serviced again: it SHALL release the sender lock, reset the request state, call motion playback's stop whether or not a motion is playing or pending, and request the firmware's safe state with the reason `link-loss`, whose effect on servo output, whose precedence over motions and web portal commands, and whose end the firmware defines. The drive link SHALL make no call that enables servo output or leaves the safe state; it passes a later session's motion requests to motion playback like any other. Each failsafe SHALL be counted.

#### Scenario: Remote goes quiet while walking
- **WHEN** the remote has requested Forward and its frames stop arriving
- **THEN** the session ends on the first loop pass after 300 ms have passed since the last accepted frame was read, and in that pass, before the player's next tick, the robot calls motion playback's stop, requests the safe state with the reason `link-loss`, and raises the failsafe count by one

#### Scenario: Pending request at the end of a session
- **WHEN** a request for Forward has been passed to motion playback while it was idle, and a sequence jump ends the session before the player's next tick
- **THEN** motion playback's stop is called before the player's next tick, and the safe state is requested

#### Scenario: Lapse while servos hold a pose
- **WHEN** no motion plays, the servos hold the last angles written, and the session lapses
- **THEN** the robot calls motion playback's stop and requests the safe state, although no motion was playing

#### Scenario: Undecodable frames do not keep the session alive
- **WHEN** the locked remote's valid frames stop, and datagrams with a bad CRC keep arriving from it every 50 ms
- **THEN** the session lapses 300 ms after the last accepted frame was read, and each bad datagram is counted as malformed

#### Scenario: Occasional losses
- **WHEN** up to five consecutive frames of a remote sending every 50 ms are lost, and the robot reads the accepted frames at most 300 ms apart
- **THEN** the session stays live and no failsafe occurs

#### Scenario: Gap past the timeout
- **WHEN** the robot reads two frames from the same sender 301 ms apart
- **THEN** the session has ended with the failsafe when the second frame is read, and the second frame starts a new session that only sets the counter baseline

#### Scenario: No session, no failsafe
- **WHEN** no session has started since boot and the web portal plays a motion
- **THEN** the drive link requests no stop and no safe state

### Requirement: Telemetry frames
While a session is live, the robot SHALL send a telemetry frame every 100 ms from UDP port 52041 to the locked sender's address and port, one frame per datagram, with its own sequence number incremented per frame. The first telemetry frame of a session SHALL be sent within 100 ms of the session's start. Each later frame SHALL be scheduled 100 ms after the previous frame's scheduled time, not after its send time; when a frame is sent more than 100 ms after its scheduled time, the next SHALL be scheduled 100 ms after that frame was sent, so that late frames never go out in a burst. Each telemetry frame SHALL be built and sent after motion playback has been serviced in the same loop pass, so that each frame reports the current and pending motion as motion playback reports them after that pass's step, including the effect of a request or stop passed on in that pass. The robot SHALL send no telemetry while no session is live, and no telemetry frame SHALL go to any sender other than the locked one. The payload SHALL be:

| Payload offset | Size | Field |
|---|---|---|
| 0 | 1 | motion playing: its motion id, or 0 when none is playing |
| 1 | 1 | motion pending behind it: its motion id, or 0 when none is pending |
| 2 | 1 | the remote's motion-request counter value of the last request processed in this session, or the session's baseline before its first request |
| 3 | 1 | result of that request: 0 = none yet in this session, 1 = motion accepted by motion playback, 2 = stop requested, 3 = motion rejected by motion playback |
| 4 | 1 | sequence number of the newest accepted command frame |
| 5 | 1 | age in milliseconds of that frame when this telemetry frame was built, measured from when it was read, 0–254, or 255 for 255 ms or more |
| 6 | 8 | joint angles, servo indices 0 to 7: for a servo that servo output reports as driven, its last commanded angle in whole degrees without the calibration trim; 255 for a servo that servo output reports as released or without a commanded angle |
| 14 | 2 | accepted command frames |
| 16 | 2 | malformed datagrams |
| 18 | 2 | out-of-sequence frames |
| 20 | 2 | datagrams from another sender |
| 22 | 2 | rate-limited datagrams |
| 24 | 2 | link-loss failsafes: sessions ended by lapse or sequence jump |

The counts SHALL run from boot and wrap from 65535 to 0.

#### Scenario: Telemetry after a request
- **WHEN** motion playback is idle, servo output is not in its fault mode, and the remote requests Say Hi with counter value 7
- **THEN** the next telemetry frame reports counter 7, result accepted and motion 9 playing

#### Scenario: Round-trip time
- **WHEN** the remote sends command frame sequence 40 at time T and receives at time U a telemetry frame that echoes sequence 40 with an age of A ms, A below 255
- **THEN** U − T − A is the round-trip time of the link, to within one robot loop pass

#### Scenario: Request refused by servo output
- **WHEN** servo output is in its fault mode and the remote requests Forward with a changed counter
- **THEN** the next telemetry frame reports the request as accepted, if motion playback accepted it, and reports the current and pending motion that motion playback then reports, not motion 2 playing

#### Scenario: Request in the pass of a due frame
- **WHEN** motion playback is idle, servo output is not in its fault mode, and in the loop pass in which a telemetry frame is due the robot accepts a frame that requests Forward with a changed counter
- **THEN** that pass's telemetry frame reports motion 2 as playing, not motion 0

#### Scenario: Telemetry reports link statistics
- **WHEN** three malformed datagrams arrive during a session
- **THEN** the next telemetry frame's malformed count is three higher than the previous frame's

#### Scenario: Late loop pass
- **WHEN** telemetry frames are scheduled at 0, 100 and 200 ms after a session's start, and the frame scheduled at 100 ms is sent at 115 ms
- **THEN** the next frame is still scheduled at 200 ms

#### Scenario: No session
- **WHEN** no session is live
- **THEN** the robot sends no datagrams from the drive port

### Requirement: Coexistence with the web portal
The drive link and the web portal SHALL share the access point. The web portal SHALL keep answering on 192.168.4.1 while a session is live, and the drive port SHALL keep working while web portal pages load. The drive link SHALL NOT refuse, undo or replay a web portal request: a frame with an unchanged motion-request counter makes no request (Command frames). How the web portal handles its own requests during a session is the web portal's rule. When the session ends, the link-loss failsafe SHALL apply to whatever the robot was doing, whichever source started it.

#### Scenario: Pages load while driving
- **WHEN** a phone loads the web portal's pages while the remote sends its frames every 50 ms
- **THEN** the pages load and the remote's session stays live

#### Scenario: Portal motion during a session
- **WHEN** a session is live with an unchanged motion-request counter, and motion playback plays a motion that the web portal requested
- **THEN** the remote's next frames make no request and no stop, and the motion plays on

#### Scenario: Portal motion when the session ends
- **WHEN** a motion that the web portal requested plays during a session, and the session lapses
- **THEN** the robot calls motion playback's stop and requests the safe state, as for a motion the remote requested

#### Scenario: Session state
- **WHEN** a remote's first valid command frame starts a session, and later its frames stop for more than 300 ms
- **THEN** the robot reports a live session from that frame until the lapse, and no live session afterwards

### Requirement: Golden frames
The protocol's encoding SHALL be pinned by a golden file of named frames: for each, the frame's bytes and its header and payload field values, plus frames that a decoder must reject. It SHALL cover both frame types, sequence number 255, every result value, the extreme values of every field, and rejected frames for a bad CRC, an unknown version, an unknown type and a wrong payload length. The host tests SHALL check that the encoder produces every accepted golden frame's bytes from its fields, that the decoder returns each one with its fields, and that it rejects every rejected one. The file SHALL change only with a deliberate protocol change, which SHALL also change the protocol version.

#### Scenario: Encoder drift
- **WHEN** a change to the encoder alters the bytes of any golden frame
- **THEN** the host tests fail and name the frame

#### Scenario: Rejected golden frame
- **WHEN** the host tests decode a golden frame marked rejected
- **THEN** the decoder returns no frame and counts it as rejected

### Requirement: Decoder and datagram fuzz tests
The frame decoder SHALL have a fuzz test seeded with every frame in the golden file. For any input byte sequence, fed one byte at a time with the decoder polled after each byte and told at the end that the input has ended, the test SHALL check that:
- the decoder neither crashes nor, in a build with sanitisers, triggers an AddressSanitizer or UndefinedBehaviorSanitizer report;
- the decoder never holds more than one maximum-size frame of undecided bytes;
- every input byte is accounted for exactly once and in order, either inside a returned frame or as a discarded byte;
- every returned frame's own bytes, decoded alone, give the same frame, and its re-encoding equals those bytes.

Datagram acceptance SHALL have a fuzz test that feeds arbitrary sequences of datagrams, each with a sender and an arrival time that never decreases, and checks that:
- every datagram gets exactly one outcome, so the outcome counts add up to the datagrams fed;
- motion requests come only from accepted frames of the locked sender, at most one per frame, and never from the first frame of a session;
- no more than 4 datagrams are decoded in any 20 ms window;
- a session never stays live more than 300 ms after its newest accepted frame;
- every session end reports exactly one failsafe, and no failsafe is reported without a session end.

The fuzz test sources SHALL NOT be compiled into the robot firmware.

#### Scenario: Golden seeds for the decoder
- **WHEN** the decoder fuzz test runs a golden frame from its seed corpus
- **THEN** an accepted golden frame is returned and re-encodes to its golden bytes, and a rejected golden frame yields no frame and only discarded bytes

#### Scenario: Golden seeds for datagram acceptance
- **WHEN** the datagram fuzz test runs a golden frame from its seed corpus as a single datagram
- **THEN** a golden command frame is accepted as the first frame of a session with no motion request, and a golden telemetry frame or rejected frame is counted as malformed

#### Scenario: Arbitrary bytes
- **WHEN** a fuzz test runs an arbitrary byte sequence
- **THEN** it completes without a crash, the decoder's held bytes never exceed 33, and the returned frames and discarded bytes together account for every input byte

### Requirement: Bounded fuzz runs
The repository SHALL provide one command that runs each drive-link fuzz test with random inputs for a bounded time per test, 60 seconds by default and configurable, and that exits non-zero and saves the input when a test fails. The seed corpus and every saved failing input SHALL run as ordinary deterministic tests in the host test gate, without random inputs.

#### Scenario: Bounded run
- **WHEN** a developer runs the fuzz command with a time of 10 seconds
- **THEN** each fuzz test runs for about 10 seconds, and the command exits non-zero if any test finds a failing input

#### Scenario: Deterministic gate
- **WHEN** the host test gate runs
- **THEN** each fuzz test runs its seed corpus and saved failing inputs once, with no random inputs

#### Scenario: Found bug kept as a regression
- **WHEN** a fuzz run finds a failing input and the bug is fixed
- **THEN** the failing input stays in the test's corpus and passes in the host test gate
