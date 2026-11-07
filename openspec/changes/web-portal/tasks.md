# Tasks

Hardware tasks (group 6) need the user's go-ahead before anything is written to the robot. Every esptool command resets the board. Groups 1–5 need no hardware.

## 1. Tokens and page generator

- [ ] 1.1 Copy the design system's `project/tokens.json` (artifact JX4X2WDSVT48H94TboBR6V, version `1794044556-c45e`) to `firmware/portal/tokens/tokens.json`, and write `firmware/portal/tokens/SOURCE.md` with the artifact URL, version and SHA-256. Verify with `sha256sum firmware/portal/tokens/tokens.json`, which must equal `25da75f563fb81140601fa92a83dde5ec8449bfdc84a38e2608f50016e35fb73` or the newer hash recorded in `SOURCE.md`.
- [ ] 1.2 Write `tools/portal/gen_pages.py`. It reads `firmware/portal/pages/*.html` and the shared fragments, emits a `:root` block with the Paper (`light`) value of every `bf-*` token that the pages use, inlines the fragments, and writes `portal_pages.h` (one `PROGMEM` string per page) to the build directory. Verify: two runs produce byte-identical output (`cmp`), and a page that uses an unknown `var(--bf-x)` makes the generator exit non-zero.
- [ ] 1.3 Write `tools/portal/check_pages.py`. It checks the token values, that every `var(--bf-*)` used is defined, that there are no external references (`http:`, `https:`, `//`, `@import`, `src=` or `href=` to a file), the viewport meta on every page, the 16 KB per-page budget, and the stock control table (decision 2 in design.md: `pm`, `servo` and `value` per control, defaults, slider directions and label order). Verify: it passes on the generated pages, and it fails on each of three fixture pages under `tools/portal/fixtures/`: a wrong token value, an external stylesheet link, and a wrong `data-value`.
- [ ] 1.4 Add the mise tasks `portal-gen` (runs the generator) and `portal-check` (runs the generator, then the check), and a PlatformIO `pre:` extra script in `firmware/` that runs the generator before every build. Verify: `mise run portal-check` passes, and after `touch firmware/portal/pages/controller.html`, `mise run build` regenerates `portal_pages.h`, as its timestamp shows.

## 2. Core request handler (host-tested)

- [ ] 2.1 Add the portal handler to `core/`: the request type (method, path, arguments), the read-only context (remote connected, valid motion ids, angle range, offset range) and the result type (status, content type, body, at most one action), with strict integer parsing (optional sign and digits only). Verify: the native environment builds, and a Unity test of the integer parser passes for `"7"`, `"-5"` and `"+3"`, and fails for `""`, `"7a"`, `"1.5"` and `" 7"`.
- [ ] 2.2 Implement `/controller`: `pm` (a valid id gives 200 `(pm)=(<pm as received>)`, tested with `pm=02` giving `(pm)=(02)`, and a motion request; an unknown or non-integer id gives 400), `servo` with `value` (stop plus move, body `(pm)=()`; a missing or invalid index or value gives 400), `pm` together with `servo` (400), and `pms` only (200 `(pm)=()`, no action). Verify with Unity tests covering every scenario of "Motion request", "Direct servo command" and "Unused pms parameter" in the spec; `mise run test` passes.
- [ ] 2.3 Implement `/save`: keys 0–7 with a value in range store the offset, with body `(key, value)=(<i>,<v>)`; key 100 resets all, ignoring a missing or malformed value, with body `(key, value)=(100,0)`; any other key, or a missing or invalid value, gives 400 and no action. Verify with Unity tests covering the "Calibration save" scenarios and the boundaries of the offset range; `mise run test` passes.
- [ ] 2.4 Implement `/online`: nine required parameters, angle and `t1` (0–60000) ranges, a keyframe action, and body `(m0, m1)=(<a0>,<a1>)`. Verify with Unity tests for a valid request, a missing `m5`, `t1=60001` and `t1=-1`; `mise run test` passes.
- [ ] 2.5 Implement arbitration and fall-through: with a remote connected, `pm`, `servo` and `/online` give 409 `text/plain` and no action, checked before parameter validation (so `servo=3` without `value` gives 409), while `pms`-only, `/save` and page paths are unaffected. Unknown paths and non-GET methods give 404 `text/plain` `Not found: <path>`. Verify with Unity tests for each "One controller at a time" and "Portal server and address" scenario that does not need hardware; `mise run test` passes.

## 3. Pages

- [ ] 3.1 Write the shared fragments: the stylesheet (Paper tokens, mono uppercase labels, 44 px buttons, focus ring, 320 px layout, the two servo-group tones), the navigation bar marking the current page, the status line, and one click handler. The handler sends a control's `data-*` request with a 5 s timeout and reports 200, 400, 409, or no answer (saying that the command may still run). Verify: `mise run portal-check` passes.
- [ ] 3.2 Write `controller.html`: the title `Spider Robot.`, fifteen motion buttons in the stock order and grouping, and no vendor title or shop advert. Verify: the stock control table in `portal-check` passes for `/`.
- [ ] 3.3 Write `zero.html`: eight servo buttons in the stock arrangement with their zero-check angles, and Zero position sending `pm=100`. Verify: `portal-check` passes for `/zero`.
- [ ] 3.4 Write `setting.html`: eight offset fields with placeholders `{{OFF0}}`…`{{OFF7}}`, SET buttons sending `/save?key=<i>&value=<field>`, and RESET ALL that zeroes the fields and sends `/save?key=100&value=0`. Verify: `portal-check` passes for `/setting` and confirms all eight placeholders are present.
- [ ] 3.5 Write `editor.html`: sliders 45–135 with the stock defaults and directions, fields, Send buttons, slider release sending `servo` and `value`, the Action Code line, and Standby. Verify: `portal-check` passes for `/editor`. Open the generated page in a desktop browser with the request URL logged instead of sent: moving D04 to 120 shows `70, 90, 90, 110, 110, 90, 120, 70`.

## 4. Firmware integration

- [ ] 4.1 Add the portal module to `firmware/`: `ESP8266WebServer` on port 80, the seven routes and `onNotFound` passed to the core handler, and the actions carried out through motion-playback, servo-output and servo-calibration. Static pages are sent with `send_P`, and `/setting` is sent in chunks with the offsets filled in. Verify: `mise run build` succeeds, and its memory report shows the portal adds no page-sized static RAM buffer (the pages sit in flash).
- [ ] 4.2 Call `handleClient()` once per main-loop iteration, beside the drive-link and motion-player steps. Implement the `/online` wait as a loop over the drive-link and motion-player service steps with `yield()`, ending when that keyframe (by its identifier) finishes or is stopped or replaced, or at t1 + 500 ms at the latest, and replying then. Verify: `mise run build` succeeds, and a Unity test of the wait loop's exit condition passes (with a fake player that finishes after N steps, is stopped early, or is replaced by another motion that keeps playing, where the wait must still end by t1 + 500 ms).
- [ ] 4.3 Feed the "remote connected" state from drive-link into the handler context. Verify: `mise run build` succeeds, and `mise run test` still passes.

## 5. Host gate

- [ ] 5.1 Run the full host gate: `mise run portal-check`, `mise run test` and `mise run build`. All three pass with no new compiler warnings in the portal sources.

## 6. Hardware verification (user's go-ahead required)

- [ ] 6.1 With the user's go-ahead, flash with `mise run upload`. Join the robot's access point from a phone and open `http://<robot address>/` within 10 s of power-on. Verify: the Controller page loads, and the phone browser's network log shows only the page request.
- [ ] 6.2 Tap each of the fifteen Controller buttons. Verify: each plays its motion, and while Push up plays, opening `/zero` returns within 1 s.
- [ ] 6.3 On `/zero`, tap each servo button and Zero position. Verify: each button moves only its labelled servo (GPIO as labelled) to the zero-check angle, and Zero position moves all eight.
- [ ] 6.4 On `/setting`, SET an offset on one servo, power-cycle the robot, and reopen the page. Verify: the offset is still shown, no servo moved on SET, and the next zero check of that servo is shifted by the offset. Then RESET ALL and power-cycle. Verify: all fields show `0`.
- [ ] 6.5 On `/editor`, move each slider and use one Send. Verify: the right servo follows, and the Action Code line matches the fields.
- [ ] 6.6 From a laptop on the access point, run `curl -i 'http://<robot address>/online?m0=70&m1=90&m2=90&m3=110&m4=110&m5=90&m6=90&m7=70&t1=1000'`, then `curl -i 'http://<robot address>/controller?servo=3'` and `curl -i 'http://<robot address>/generate_204'`. Verify: the first answers 200 `(m0, m1)=(70,90)` after about 1 s with the robot in that pose, the second answers 400 and nothing moves, and the third answers 404 `Not found: /generate_204`.
- [ ] 6.7 When drive-link and the pak (or drive-link's test client) are available, connect a remote, then tap Forward on the portal and SET an offset. Verify: Forward is refused (the status line says the handheld is driving, and the robot keeps following the remote), and the offset is saved. After the remote disconnects and drive-link's link-loss timeout passes, Forward plays.
- [ ] 6.8 Open each page in a phone browser at 320 CSS px width (or desktop responsive mode). Verify: there is no horizontal scrolling, buttons are at least 44 px tall, and the keyboard focus ring is visible.
