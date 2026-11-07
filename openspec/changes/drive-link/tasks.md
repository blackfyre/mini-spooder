# Tasks

The paths follow design.md (D1, D8) and `firmware-foundation`'s layout. If that change put the native env or the firmware sources elsewhere, use its paths. Every `mise run test` below is the host test gate. The on-robot checks in group 6 flash the robot, which needs the user's go-ahead first (`CLAUDE.md`).

## 1. Protocol codec

- [ ] 1.1 Copy `../spooder/core/protocol/protocol.{h,cpp}` to `core/drive_link/protocol.{h,cpp}` and adapt it to D2. Keep version 1, the types command (2-byte payload) and telemetry (23-byte payload), and `MAX_FRAME_SIZE` 30. Keep `crc16`, `encodeCommand`, `encodeTelemetry` and `Decoder` (`push`, `next`, `idle`, counts, `pending`). Drop the other frame types and `frameBytes`. Add the source to the native env's source filter. Verify that `mise run test` builds it.
- [ ] 1.2 Add `core/test/test_drive_link_protocol`:
  - `crc16("123456789")` is 0x29B1;
  - encode and decode round trips for both types at sequence numbers 0 and 255;
  - each of the Frame format requirement's scenarios: a corrupted frame, garbage between frames, a false start marker, and an unknown version;
  - an unknown type and a wrong length are rejected;
  - `idle()` rejects an incomplete candidate and then returns the complete frame behind it;
  - `pending()` never exceeds 30.

  Verify that `mise run test` passes, and that it fails when the CRC's initial value is changed temporarily.
- [ ] 1.3 Add `core/test/test_drive_link_golden` and generate `core/test/golden/drive_link_v1.txt` with `MINI_SPOODER_WRITE_GOLDEN=1 mise run test`, in spooder's line format. Include:
  - command frames for a motion request, a stop, motion 100, and the extremes at sequence number 255;
  - telemetry for each result value 0–3, with a joint at 255, and the extremes;
  - rejected frames for a bad CRC, version 2, type 3, and a command frame with payload length 3.

  Verify that `mise run test` passes without the variable, and that it fails and names the frame when one golden byte is edited by hand.

## 2. Session logic

- [ ] 2.1 Implement `core/drive_link/session.{h,cpp}` (D1, D4, D5) with named constants:
  - `PORT` 52041, `COMMAND_PERIOD_MS` 50, `LIVENESS_MS` 300, `TELEMETRY_PERIOD_MS` 100;
  - `WINDOW_MS` 20 and `WINDOW_BUDGET` 4, `SEQ_WINDOW` 8, `DRAIN_MAX` 128.

  Provide the outcomes (accepted, other sender, rate limited, malformed, out of sequence, with a flag for a jump that ends the session), the request result (`Play(id)`, `Stop`, `Unknown`, none), `judge`, `live`, the telemetry schedule, `makeTelemetry` and the six counts. The known-motion check is a function parameter, so the tests can pass motion playback's table or a stand-in. Use unsigned millisecond differences throughout. Add the source to the source filter. Verify that `mise run test` builds it.
- [ ] 2.2 Add `core/test/test_drive_link_session`, with one test per scenario of these requirements:
  - Drive port and datagrams, Command frames, Motion requests and Sequence numbers;
  - One remote at a time, Datagram rate limit (the two flood scenarios at the session level) and Link loss: live at 300 ms, ended at 301 ms, five losses tolerated, a jump ends the session, and an end while idle reports that no stop is needed;
  - Telemetry frames: the first frame due at session start, then every 100 ms, nothing due without a session, and the field values and counts wrapping at 65535.

  Add the acceptance-order cases (a corrupt datagram from another sender counts as other sender; a corrupt datagram beyond the budget counts as rate limited), a restart on the same sender port 301 ms after the last accepted frame with a changed counter (a new session, baseline only, no request), and a session that runs across the `millis()` wrap at 0xFFFFFFFF. Verify that `mise run test` passes.

## 3. Fuzz tests

- [ ] 3.1 Add the two fuzz targets with libFuzzer's signature, `core/fuzz/fuzz_decoder.cpp` and `core/fuzz/fuzz_session.cpp`, that check the properties of the Decoder and datagram fuzz tests requirement and `abort()` on a violation. The session target checks the per-seed outcome too: a lone golden command frame is accepted as a session's first frame with no request, and a lone telemetry or rejected frame is malformed. Seed `core/test/fuzz/decoder/` and `core/test/fuzz/session/` from every golden line, the session seeds as single-datagram records that hold each golden frame's bytes intact (record lengths up to 40). Verify that the seed files exist, one per golden line and target.
- [ ] 3.2 Add `core/test/test_drive_link_fuzz_corpus`, which runs every file in both corpus directories through its target once. Verify that `mise run test` runs it and passes.
- [ ] 3.3 Add the random driver `core/fuzz/drive_link_fuzz.cpp` and the mise task `fuzz-link` with `--time` (default `60s`). The driver builds with the host compiler, with ASan and UBSan when they link and a warning otherwise. It prints its seed, and on a failure it writes the input to the target's corpus directory and exits non-zero. Verify that `mise run fuzz-link --time 10s` exits 0. Then make a property check fail temporarily, and verify that the run exits non-zero and saves an input. Revert the change and delete the saved input.

## 4. Firmware

- [ ] 4.1 Start the access point in `setup()` as D7 describes. The `WiFi.persistent(false)` call comes before `WiFi.mode(WIFI_AP)`, and there is no station mode. Verify that `mise run build` passes and that a search of `firmware/` finds no `WiFi.begin` and no `persistent(true)`.
- [ ] 4.2 Add the drive port glue (D1, D4): a `WiFiUDP` socket bound to 52041. Each loop pass drains up to `DRAIN_MAX` datagrams into a 10-byte buffer, using the length from `parsePacket()`, and passes each one to `session::accept` with its sender and `millis()` read at that moment. The pass then calls `session::judge`, and sends a due telemetry frame to the locked sender. Verify that `mise run build` passes.
- [ ] 4.3 Connect the session's results to `motion-playback` and `servo-output`:
  - `Play(id)` calls play and `Stop` calls stop;
  - at a session end, the glue calls stop if a motion is playing;
  - the telemetry carries the motion playing and the commanded angles, with 255 for a servo not commanded or detached.

  Verify that `mise run build` passes and that `mise run test` still passes.

## 5. Desktop probe

- [ ] 5.1 Write `tools/drive-probe.py` (D9), standard library only. Its options are:
  - `--addr` (default `192.168.4.1:52041`), `--request <motion>@<ms>` (repeatable), `--stop@<ms>` and `--pause <from_ms>:<to_ms>`;
  - `--flood <datagrams per second>` and `--corrupt <fraction>`, with `--sender-port` for a second instance;
  - `--duration`.

  It waits 350 ms before its first frame, prints each telemetry frame's fields and the interval between telemetry frames, and has a `--self-test` that encodes and decodes every line of `core/test/golden/drive_link_v1.txt`. Verify that `mise exec -- python tools/drive-probe.py --self-test` exits 0 for every golden line.

## 6. On-robot checks (after the user's go-ahead to flash)

- [ ] 6.1 Run `mise run upload`. Join `Spider Robot` with `12345678` from a laptop, then from a phone as well. Verify that both get 192.168.4.x addresses by DHCP and that both can reach 192.168.4.1.
- [ ] 6.2 Run the probe with `--request 2@1000 --stop@4000 --request 9@6000`. Verify that the robot walks forward, stops in standby, and says hi. Verify that the telemetry reports motion 2, then 0, then 9, the counters with the results started, stop requested and started, and an interval between telemetry frames of 100 ms ± 20 ms.
- [ ] 6.3 Run the probe with `--request 2@1000 --pause 3000:5000`. Verify that the robot stops walking and goes to standby about 0.3 s into the pause, that the session-end count goes up by one, and that no motion starts when frames resume.
- [ ] 6.4 Run a second probe with another `--sender-port` and its own requests while the first probe is live. Verify that the robot follows only the first, and that its other-sender count rises. Stop the first probe, and verify that the second takes over after 300 ms without replaying a request.
- [ ] 6.5 While one probe drives, run a second probe with another `--sender-port` and `--flood 2000 --duration 10`. Verify that the robot does not restart (the counts in telemetry keep rising and do not reset), that the first probe's session stays live, that only the other-sender count rises for the flood, and that the web portal's pages still load.
- [ ] 6.6 Run a single probe with `--corrupt 0.5`, then a single probe with `--flood 2000 --duration 10`, each as the locked sender. Verify that the malformed count rises in the first run and the rate-limited count in the second, that the robot does not restart, and that the web portal's pages still load.
- [ ] 6.7 While a probe's session is live, request Dance 1 from the web portal on the phone. Verify that the dance plays to its end without interruption from the probe's frames, and that telemetry reports motion 13. Then send a stop from the probe, and verify that the robot stops.
- [ ] 6.8 With the web portal's calibration page holding a servo and no motion playing, pause the probe past 300 ms. Verify that no servo moves.

## 7. Completion

- [ ] 7.1 Run `mise run test`, `mise run build`, `mise run fuzz-link --time 60s` and `openspec validate drive-link --strict`. Verify that all four pass.
