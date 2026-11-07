# Proposal

## Why

The stock firmware is driven only from its web portal in a phone browser (`/controller?pm=<n>`, `baseline/README.md`). The settled plan drives the robot from a separate NextUI tool pak, `MiniSpooder.pak`, on a TrimUI Smart Pro over the robot's own access point. That needs a robot-side link with explicit safety rules: frames that cannot be misread, one remote at a time, and a defined fallback when the remote stops sending. Spooder's link (`../spooder/core/protocol`, `core/wifi_drive/handheld_controller`) already solves this for a larger robot, so this change copies its framing and safety rules and gives them small frames that fit this robot's motions.

## What Changes

- The robot's access point is specified explicitly, with the network renamed from the stock `Spider Robot` to `mini-spooder` and the password changed from stock `12345678` to `87654321` by the user; 192.168.4.1, set explicitly where stock presumably used the SDK default), a plain access point with no DNS or mDNS as in stock, so the pak and the restyled web portal share it.
- The robot listens on one UDP port for command frames from the pak, one frame per datagram.
- A new link protocol, version 1, copies spooder's framing (start marker, version, type, sequence number, payload length, CRC-16/CCITT-FALSE; discard, resynchronise and count) with two new, smaller frames:
  - a command frame: the requested motion id (the stock `pm` numbers 1–15, 99 and 100; 0 for stop) and a motion-request counter;
  - a telemetry frame: the motion playing and the one pending, the result of the last request, an echo of the newest command frame's sequence number and age for the round-trip time, the joint angles and the link statistics.
- The remote sends its complete desired state every 50 ms. The robot turns each change of the motion-request counter into one motion request, ignores repeats, and sets new baselines after a gap, so a lost or repeated frame changes nothing.
- One remote at a time (sender lock), a sequence-number rule, a datagram rate limit and a 300 ms liveness rule. When the remote's frames stop, the link-loss failsafe calls motion playback's stop and requests the firmware's safe state, whose effect `firmware-foundation` defines (output disabled, overriding every motion and portal command). Only accepted frames keep a session alive.
- The web portal and the pak share the access point. The robot reports whether a session is live, a session's start stops whatever the portal started, and the link-loss failsafe applies whichever source started the motion. How the portal handles its own requests during a session is `web-portal`'s rule: it locks itself against every robot-moving request while a session is live, using the live-session report.
- Hardware-free codec and session logic in `core/`, host-tested with golden frames that pin the encoding for the pak's copy, and fuzz tests of the decoder and of datagram acceptance with a bounded fuzz command.
- A small desktop probe that sends command frames and prints telemetry, so the link can be checked on the robot before the pak exists.

## Capabilities

### New Capabilities
- `drive-link`: the robot side of the remote link. It covers the access point (SSID, password, address), the UDP port, the frame format, the command and telemetry frames, the sending periods, the sequence, rate and sender rules, the behaviour when the link is lost, coexistence with the web portal, and the host and fuzz tests that pin the protocol.

### Modified Capabilities
None. No specs exist yet in `openspec/specs/`.

## Impact

- **New code:** `core/drive_link/` (protocol codec, decoder and session logic), its host tests, a golden frame file, a fuzz harness in `core/fuzz/drive_link/` (which neither firmware-foundation env compiles), `firmware/` glue for the access point and the UDP socket, and `tools/drive-probe.py`.
- **mise:** a `fuzz-link` task. The host tests join the existing `test` task.
- **Dependencies:** none new. The firmware uses the Arduino ESP8266 core's `ESP8266WiFi` and `WiFiUdp`. The probe uses only the Python standard library.
- **Other changes:** this change depends on `motion-playback` (request with its pending rule, stop, current and pending motion, its catalogue), on `firmware-foundation`'s safe-state gate, and on `servo-output` (commanded angles), all by name. `web-portal` serves on the access point defined here and reads the live-session state. `handheld-pak` implements the remote side of this protocol against the golden file.
- **On-robot order:** this change's link checks run after `firmware-foundation`'s hardware checks (which play no motion), legs off the ground. Its link-loss checks request at most one Standby, to prove that a lapse releases a driven pose; walking and other motions over the link, and `motion-playback`'s motion-acceptance checks, follow only once those checks have passed.
- **Flash:** the firmware turns off the SDK's persistent Wi-Fi settings, so the access point settings are not stored in the SDK configuration sectors (0x3FC000–0x3FFFFF).
