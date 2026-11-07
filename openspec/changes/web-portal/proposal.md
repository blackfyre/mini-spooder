# Proposal

## Why

The stock firmware's only user interface is a web portal on the robot's own access point: a controller page with the motions, a zero check, a servo calibration page and a motion editor. The replacement firmware must keep that functionality, because it is the only way to calibrate the robot and to edit poses without the handheld. The stock pages are unstyled vendor pages with a vendor shop advert, label the servos with GPIO numbers that are not the pins the firmware drives, and accept any input without checking it: a missing value moves a servo to 0°. This change reproduces the portal's behaviour on the new firmware, restyles it in the Blackfyre Paper theme, and makes it share the robot with the MiniSpooder.pak remote under the link-loss safe state.

## What Changes

- An HTTP server on port 80 of the robot's access point, with the stock routes `/`, `/controller`, `/save`, `/editor`, `/zero`, `/setting` and `/online`, all GET, and their stock behaviour. `baseline/README.md`, `baseline/portal-strings.txt`, `docs/lafvin-comparison.md` and the stock image's handlers, recorded in design.md, are the reference.
- The four stock pages (Controller, Zero check, Servo calibration, Motion editor) with their stock controls, layout and requests, restyled with the Blackfyre design system's Paper tokens. The pages are self-contained: the phone has no internet on the robot's network.
- The stock command routes keep their request parameters and their 200 response bodies, so any existing client keeps working. Unknown integer `pm` values are accepted and ignored, as in stock; pm 99 (Centre) is passed to motion-playback.
- Requests that are refused, with nothing moved: malformed or out-of-range input (400); a calibration save while a motion plays (409); robot-moving requests while servo output is in the safe state after a fault or link loss (503). Portal and remote requests otherwise share the robot, and the most recent wins, as drive-link specifies.
- No handler waits for a motion. `/online` (one eight-joint keyframe over a given time) replies once the keyframe is accepted, not after it, so the server keeps serving while it plays. The unused `pms` parameter is specified as the stock image implements it.
- Departures from stock, each recorded in design.md: the vendor title, the shop advert, the stock GPIO labels and the commented-out state of the RESET ALL button are replaced. The pages gain a shared navigation bar and a status line. Requests are validated.
- Not added: a captive-portal DNS responder or mDNS. The stock image has neither.

## Capabilities

### New Capabilities
- `web-portal`: the robot's web portal. It covers the HTTP server and its address, the stock pages and routes with their stock behaviour, input validation, sharing the robot with the drive link, respect for the safe state, and the Blackfyre Paper styling.

### Modified Capabilities
- None. The repository has no specifications yet. This change uses `servo-output` and `servo-calibration` (owned by firmware-foundation), `motion-playback` (owned by motion-playback) and the access point and coexistence rule of `drive-link` (owned by drive-link) by reference only. design.md lists the assumptions it makes about them.

## Impact

- **Firmware:** a portal module under `firmware/` (the HTTP routes and page delivery), and the page sources and their generator.
- **Core:** a hardware-free request handler in `core/`, covered by Unity host tests in the native environment.
- **Tooling:** a vendored copy of the design system's `tokens.json`, a servo label table, a page generator run before every firmware build, and a `mise run portal-check` task that checks the generated pages against the tokens and the labels against servo-output's pin map.
- **Flash and RAM:** the pages are kept in flash and streamed, not built in RAM as stock does.
- **Dependencies:** the Arduino ESP8266 core's web server, which ships with the core. Nothing new is fetched.
