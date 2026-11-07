# Tasks

The paths follow design.md (D1, D8) and `firmware-foundation`'s layout. If that change put the native env or the firmware sources elsewhere, use its paths. Every `mise run test` below is the host test gate. The on-robot checks in group 6 flash the robot, which needs the user's go-ahead first (`CLAUDE.md`), and run in the order of design.md D10.

## 1. Protocol codec

- [ ] 1.1 Copy `../spooder/core/protocol/protocol.{h,cpp}` to `core/drive_link/protocol.{h,cpp}` and adapt it to D2. Keep version 1, the types command (2-byte payload) and telemetry (26-byte payload), and `MAX_FRAME_SIZE` 33. Keep `crc16`, `encodeCommand`, `encodeTelemetry` and `Decoder` (`push`, `next`, `idle`, counts, `pending`). Drop the other frame types and `frameBytes`. Verify that `mise run test` builds it.
- [ ] 1.2 Add `core/test/test_drive_link_protocol`:
  - `crc16("123456789")` is 0x29B1;
  - encode and decode round trips for both types at sequence numbers 0 and 255;
  - each of the Frame format requirement's scenarios: a corrupted frame, garbage between frames, a false start marker, and an unknown version;
  - an unknown type and a wrong length are rejected;
  - `idle()` rejects an incomplete candidate and then returns the complete frame behind it;
  - `pending()` never exceeds 33.

  Verify that `mise run test` passes, and that it fails when the CRC's initial value is changed temporarily.
- [ ] 1.3 Add `core/test/test_drive_link_golden` and generate `core/test/golden/drive_link_v1.txt` with `MINI_SPOODER_WRITE_GOLDEN=1 mise run test`, in spooder's line format. Include:
  - command frames for a motion request, a stop, motions 99 and 100, and the extremes at sequence number 255;
  - telemetry for each result value 0–3, with a pending motion, with a joint at 255, with an age of 255, and the extremes;
  - rejected frames for a bad CRC, version 2, type 3, and a command frame with payload length 3.

  Verify that `mise run test` passes without the variable, and that it fails and names the frame when one golden byte is edited by hand.

## 2. Session logic

- [ ] 2.1 Implement `core/drive_link/session.{h,cpp}` (D1, D3–D6) with named constants:
  - `PORT` 52041, `COMMAND_PERIOD_MS` 50, `LIVENESS_MS` 300, `TELEMETRY_PERIOD_MS` 100;
  - `WINDOW_MS` 20 and `WINDOW_BUDGET` 4, `SEQ_WINDOW` 8, `DRAIN_MAX` 128.

  Provide the outcomes (accepted, other sender, rate limited, malformed, out of sequence, with a flag for a jump that ends the session), a session-started flag, the request (`Play(id)`, `Stop`, none), `recordResult` (accepted or rejected by the player), `judge` with its failsafe report, `live`, the telemetry schedule (deadline advanced by the period, re-based when more than one period late), `makeTelemetry` from the player's current and pending motion, the angles and the echo age, and the six counts. Only an accepted frame refreshes liveness. Use unsigned millisecond differences throughout. Verify that `mise run test` builds it.
- [ ] 2.2 Add `core/test/test_drive_link_session`, with one test per scenario of these requirements:
  - Drive port and datagrams, Command frames, Motion requests (with a stand-in player that accepts 1–15, 99 and 100, keeps one pending request and reports current and pending) and Sequence numbers;
  - One remote at a time, Datagram rate limit (the two flood scenarios at the session level), Session start (stop requested on the first accepted frame) and Link loss: live at 300 ms, ended at 301 ms, five losses tolerated, bad-CRC datagrams from the locked sender do not refresh liveness, a jump ends the session, every end reports exactly one failsafe with stop and safe state, and an end while a request is pending but not started reports the stop;
  - Telemetry frames: the first frame due at session start, then every 100 ms, the late-pass scenario (115 ms send, next deadline still 200 ms), a send more than one period late re-bases the schedule, nothing due without a session, the field values including the echoed sequence and its age capped at 255, and counts wrapping at 65535.

  Add the acceptance-order cases (a corrupt datagram from another sender counts as other sender; a corrupt datagram beyond the budget counts as rate limited), a restart on the same sender port 301 ms after the last accepted frame with a changed counter (a failsafe, then a new session, baseline only, no request), and a session that runs across the `millis()` wrap at 0xFFFFFFFF. Verify that `mise run test` passes.

## 3. Fuzz tests

- [ ] 3.1 Add `fuzz/drive_link/targets.{h,cpp}` with `fuzzDecoderOne` and `fuzzSessionOne` (D8), which check the properties of the Decoder and datagram fuzz tests requirement and `abort()` on a violation. The session target checks the per-seed outcome too: a lone golden command frame is accepted as a session's first frame with no request, and a lone telemetry or rejected frame is malformed. Add `fuzz/drive_link/libfuzzer_decoder.cpp` and `fuzz/drive_link/libfuzzer_session.cpp`, each defining `LLVMFuzzerTestOneInput` for its target. Seed `fuzz/drive_link/corpus/decoder/` and `fuzz/drive_link/corpus/session/` from every golden line, the session seeds as single-datagram records that hold each golden frame's bytes intact (record lengths up to 40). Verify that the seed files exist, one per golden line and target, and that `grep -rn LLVMFuzzerTestOneInput core firmware` finds nothing.
- [ ] 3.2 Add `core/test/test_drive_link_fuzz_corpus`, which compiles `fuzz/drive_link/targets.cpp` into that test only and runs every file in both corpus directories through its target once. Verify that `mise run test` runs it and passes, and that `mise run build` still passes with no fuzz source in the firmware build.
- [ ] 3.3 Add the random driver `fuzz/drive_link/driver.cpp` and the mise task `fuzz-link` with `--time` (default `60s`). The driver builds with the host compiler, with ASan and UBSan when they link and a warning otherwise. It prints its seed, and on a failure it writes the input to the target's corpus directory and exits non-zero. Verify that `mise run fuzz-link --time 10s` exits 0. Then make a property check fail temporarily, and verify that the run exits non-zero and saves an input. Revert the change and delete the saved input.

## 4. Firmware

- [ ] 4.1 Start the access point in `setup()` as D7 describes. The `WiFi.persistent(false)` call comes before `WiFi.mode(WIFI_AP)`, and there is no station mode, DNS server or mDNS. Verify that `mise run build` passes and that a search of `firmware/` finds no `WiFi.begin`, no `persistent(true)`, no `DNSServer` and no `MDNS`.
- [ ] 4.2 Add the drive port glue (D1, D4): a `WiFiUDP` socket bound to 52041. Each loop pass drains up to `DRAIN_MAX` datagrams into a 10-byte buffer, using the length from `parsePacket()`, and passes each one to `session::accept` with its sender and `millis()` read at that moment. The pass then calls `session::judge`, and sends a due telemetry frame to the locked sender. The glue's step runs before motion playback's step in `loop()`. Verify that `mise run build` passes.
- [ ] 4.3 Connect the session's results to `motion-playback`, `firmware-foundation`'s safe state and `servo-output`:
  - on a session start, call the player's stop;
  - `Play(id)` calls the player's request and records its accept or reject answer; `Stop` calls the player's stop;
  - on a failsafe, call the player's stop unconditionally and then enter the safe state, in the same pass;
  - expose `live()` for `web-portal`;
  - the telemetry carries the player's current and pending motion and the commanded angles, with 255 for a servo not commanded or with output disabled.

  Verify that `mise run build` passes, that `mise run test` still passes, and that a search of the glue finds no call that enables servo output.

## 5. Desktop probe

- [ ] 5.1 Write `tools/drive-probe.py` (D9), standard library only. Its options are:
  - `--addr` (default `192.168.4.1:52041`), `--request <motion>@<ms>` (repeatable), `--stop@<ms>` and `--pause <from_ms>:<to_ms>`;
  - `--flood <datagrams per second>` and `--corrupt <fraction>`, with `--sender-port` for a second instance;
  - `--duration`.

  It waits 350 ms before its first frame, prints each telemetry frame's fields, the interval between telemetry frames and the round-trip time, and has a `--self-test` that encodes and decodes every line of `core/test/golden/drive_link_v1.txt`. Verify that `mise exec -- python tools/drive-probe.py --self-test` exits 0 for every golden line.

## 6. On-robot checks (after the user's go-ahead to flash)

Servos are attached and the robot stands on a support with its legs off the ground for every check in this group (D10). Checks 6.2 and 6.3 are the first powered checks with servos attached and request no walking motion.

- [ ] 6.1 Run `mise run upload`. Join `Spider Robot` with `12345678` from a laptop, then from a phone as well. Verify that both get 192.168.4.x addresses by DHCP, that both can reach 192.168.4.1, and that `dig @192.168.4.1 example.com` gets no answer.
- [ ] 6.2 After a power cycle, which `firmware-foundation` specifies as still with output disabled, run the probe with no request and `--pause 2000:3000`. Verify that no servo moves at any time, that telemetry reports no motion and every angle as 255, that the failsafe count rises by one in the pause, and that the round-trip time is printed.
- [ ] 6.3 Run the probe with `--request 1@1000 --pause 3000:4000`. Verify that the robot moves to Standby, that the interval between telemetry frames is 100 ms ± 20 ms, that about 0.3 s into the pause the servos go limp (no holding torque by hand, angles 255 in telemetry once frames resume) and the failsafe count rises by one, and that no servo moves when frames resume.
- [ ] 6.4 Only after `firmware-foundation`'s suspended check of all eight servo indices, joints and directions has passed: run the probe with `--request 2@1000 --stop@1500 --request 1@3000 --request 9@5000 --request 3@5200`. Verify that the legs make Forward's stepping movement (eleven 150 ms keyframes, so still playing at 1500 ms), that the stop holds the mid-step pose at once, that Standby then plays, and that telemetry reports motion 2 playing, then the stop with nothing playing, then 1, then 9 playing with 3 pending until Say Hi ends, with results accepted, stop requested, accepted, accepted.
- [ ] 6.5 Run the probe with `--request 2@1000 --pause 1500:3000`. Verify that the stepping ends with the servos limp about 0.3 s into the pause, that the failsafe count goes up by one, and that no motion starts when frames resume.
- [ ] 6.6 Run a second probe with another `--sender-port` and its own requests while the first probe is live. Verify that the robot follows only the first, and that its other-sender count rises. Stop the first probe, and verify that the failsafe applies and that the second takes over after 300 ms without replaying a request.
- [ ] 6.7 While one probe drives, run a second probe with another `--sender-port` and `--flood 2000 --duration 10`. Verify that the robot does not restart (the counts in telemetry keep rising and do not reset), that the first probe's session stays live, that only the other-sender count rises for the flood, and that the web portal's pages still load.
- [ ] 6.8 Run a single probe with `--corrupt 0.5`, then a single probe with `--flood 2000 --duration 10`, each as the locked sender. Verify that the malformed count rises in the first run and the rate-limited count in the second, that the robot does not restart, and that the web portal's pages still load.
- [ ] 6.9 With no session live, start Dance 1 from the web portal on the phone, then start the probe. Verify that Dance 1 ends when the session starts, that telemetry reports no motion playing, and that the portal's movement requests are refused while the probe's session is live, as `web-portal` specifies.

## 7. Completion

- [ ] 7.1 Run `mise run test`, `mise run build`, `mise run fuzz-link --time 60s` and `openspec validate drive-link --strict`. Verify that all four pass.
