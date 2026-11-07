# Spec Delta

## Purpose

The robot's web portal on its own access point. It reproduces the stock firmware's pages and routes for driving motions, moving single servos, checking the zero pose, calibrating the servos and editing poses. It is restyled in the Blackfyre Paper theme and locks itself against moving the robot while the MiniSpooder.pak remote drives it.

## ADDED Requirements

### Requirement: Portal server and address
The firmware SHALL serve the portal over HTTP on TCP port 80 at the robot's address on its own access point. drive-link defines the access point. The server SHALL be up within 10 seconds of power-on and SHALL stay up while the firmware runs, also while a motion or a keyframe plays and while a remote's session is live. No route handler SHALL wait for a motion or a keyframe to end. The routes SHALL answer `GET` requests. A request for any other path, or a request with any other method, SHALL be answered with status 404, content type `text/plain` and the body `Not found: <path>`, as the stock image's web server does. The portal SHALL NOT redirect requests, as the stock image does not; the access point's name services are drive-link's.

No HTTP work SHALL hold a main-loop pass for more than a few milliseconds, whatever the client does. The portal SHALL write each response across loop passes, in each pass at most what the client's send buffer accepts without waiting. Every response SHALL carry `Content-Length` (so HTTP/1.0 clients get the whole body) and `Connection: close`, and the portal SHALL close the connection once the client has acknowledged the response. The portal SHALL close a connection at once when the client disconnects, and within 2 seconds of the last progress when the client stops taking or acknowledging data. The portal SHALL write one response at a time; a connection that arrives meanwhile SHALL wait in the server's queue and be served after it. The portal SHALL wait at most 20 ms in one pass for a request line or header that has only partly arrived.

#### Scenario: Phone opens the portal
- **WHEN** a phone joined to the robot's access point requests `http://<robot address>/`
- **THEN** it receives the Controller page with status 200

#### Scenario: Unknown path
- **WHEN** a client requests `GET /generate_204`
- **THEN** the response has status 404, content type `text/plain` and the body `Not found: /generate_204`, and no servo moves

#### Scenario: Wrong method
- **WHEN** a client sends `POST /controller?pm=2`
- **THEN** the response has status 404, and no motion starts

#### Scenario: Page load does not hold the loop
- **WHEN** a phone loads the Controller page
- **THEN** the page arrives byte for byte with a `Content-Length` equal to its size, it is written across several loop passes, and no pass spends more than a few milliseconds on HTTP

#### Scenario: Client leaves in the middle of a page
- **WHEN** a client requests `/editor` and closes its connection before the page has arrived
- **THEN** the portal closes the connection in the next pass, and no pass waits for the client

#### Scenario: Client stops reading
- **WHEN** a client requests `/editor` and then reads nothing more
- **THEN** the portal closes the connection within 2 seconds of the last data the client took, no pass waits for the client, and a request queued behind it is served afterwards

### Requirement: Self-contained pages
Every page SHALL be a complete HTML document that loads no external resources: no remote or local stylesheets, scripts, fonts or images. Each page's styles and scripts SHALL be inline. Each page SHALL send `<meta name=viewport content="width=device-width,initial-scale=1">` and SHALL lay out without horizontal scrolling at a viewport width of 320 CSS pixels. Text SHALL be in English.

#### Scenario: No network beyond the robot
- **WHEN** a phone with no other network loads any page
- **THEN** the page renders completely, and the browser makes no request other than the page itself

#### Scenario: Narrow phone
- **WHEN** any page is shown at a viewport width of 320 CSS pixels
- **THEN** all its controls are visible without horizontal scrolling

### Requirement: Blackfyre Paper styling
The pages SHALL use the Blackfyre design system's Paper theme. Every colour, shadow and radius SHALL be a `bf-*` token value taken from the design system's `tokens.json` (the Paper values). The page background SHALL be `bf-paper`, panels `bf-surface`, and text `bf-ink`. Orange text on light SHALL be `bf-accent-text`, and text on `bf-accent` SHALL be `bf-on-accent`. Labels and buttons SHALL be set in the mono family, uppercase, with the `label` letter-spacing. Each page title SHALL end in a full stop coloured `bf-accent`. Buttons SHALL be at least 44 CSS pixels tall and SHALL show a 2 px `bf-accent` focus outline offset by 3 px. Fonts SHALL use the token font families with their system fallbacks, and no font files are sent.

#### Scenario: Token values
- **WHEN** the generated pages are checked against the vendored `tokens.json`
- **THEN** every colour, shadow and radius in their styles equals the Paper value of the `bf-*` token it names

#### Scenario: Keyboard focus
- **WHEN** a button on any page receives keyboard focus
- **THEN** it shows a 2 px `bf-accent` outline offset by 3 px

### Requirement: Navigation and command feedback
Every page SHALL show a navigation bar linking the four pages: Controller (`/`), Zero check (`/zero`), Servo calibration (`/setting`) and Motion editor (`/editor`). The bar SHALL mark the current page. Every page that sends commands SHALL show a status line with the outcome of its most recent command, one distinct message for each of: sent (status 200); not run or not saved because a remote is driving the robot and the portal is locked until its session ends (status 409, body `remote in control`); not saved because a motion is playing (status 409, body `motion playing`); not run because servo output did not accept the servo command (status 503, body `servo output off`); not run because of a calibration fault, with the hint to repair the trims on the Servo calibration page (status 503, body `servo output fault`); invalid (status 400); or no answer from the robot within 5 seconds. A no-answer outcome SHALL say that the command may still have run. A command SHALL NOT reload the page.

#### Scenario: Navigate to calibration
- **WHEN** the operator taps Servo calibration in the navigation bar on the Controller page
- **THEN** the browser opens `/setting`, and the bar marks Servo calibration as the current page

#### Scenario: Command refused during a session
- **WHEN** the operator taps Forward while a remote's session is live, whatever the servo output mode
- **THEN** the status line says that a remote is driving the robot, that the command was not run and that the portal is locked until the remote's session ends, and the page stays open

#### Scenario: Robot out of reach
- **WHEN** the operator taps a control and no response arrives within 5 seconds
- **THEN** the status line says that the robot did not answer and that the command may still have run

### Requirement: Controller page
`GET /` SHALL return the Controller page with status 200 and content type `text/html`. The page SHALL offer the stock motion buttons in the stock order, three to a row. The first group is Turn left (6), Forward (2) and Turn right (7), then Left shift (4), Backward (3) and Right shift (5). After a gap, the second group is Standby (1), Say Hi (9) and Push up (11); Lie (8), Fighting (10) and Sleep (12); and Dance 1 (13), Dance 2 (14) and Dance 3 (15). Each button SHALL send `GET /controller?pm=<n>` with its number. As in stock, the page SHALL have no button for Centre (99) or Zero position (100). The page SHALL NOT show the stock vendor title or the stock shop advert.

#### Scenario: Forward button
- **WHEN** the operator taps Forward
- **THEN** the page sends `GET /controller?pm=2`

#### Scenario: All motions present
- **WHEN** the Controller page is loaded
- **THEN** it shows exactly fifteen motion buttons, one for each number from 1 to 15, labelled with the stock names

### Requirement: Motion request
`GET /controller?pm=<n>`, where n is a decimal integer, SHALL be answered with status 200, content type `text/html` and the body `(pm)=(<pm>)`, where `<pm>` is the `pm` argument exactly as received (so `pm=02` gives `(pm)=(02)`, as stock echoes the argument string). The response SHALL be sent without waiting for any motion to finish. If n is a motion id in motion-playback's catalogue (the stock ids 1–15, 99 and 100), the portal SHALL pass it to motion-playback's request call, the same call that the drive link uses, with origin `portal`. If n is any other integer, the portal SHALL do nothing, as stock accepts and ignores unknown `pm` values. What a request does while a motion plays, and how it brings servo output back when output is off, are motion-playback's and firmware-foundation's. A `pm` that is empty or not an integer SHALL be answered with status 400, and no motion is requested. The refusals of "Portal lock during a remote session" and "Servo output modes" SHALL apply first.

#### Scenario: Start a motion
- **WHEN** servo output is `active`, and a client requests `GET /controller?pm=9`
- **THEN** the response is status 200 with the body `(pm)=(9)`, and motion-playback is asked for motion 9 (Say Hi)

#### Scenario: Zero position and Centre
- **WHEN** servo output is `active`, and a client requests `GET /controller?pm=100`, or `GET /controller?pm=99`
- **THEN** the response is status 200, and motion-playback is asked for motion 100 (Zero position), or for motion 99 (Centre)

#### Scenario: Unknown motion id
- **WHEN** servo output is `active`, and a client requests `GET /controller?pm=21`
- **THEN** the response is status 200 with the body `(pm)=(21)`, no motion is requested, and no servo moves

#### Scenario: Malformed motion id
- **WHEN** servo output is `active`, and a client requests `GET /controller?pm=2a`
- **THEN** the response is status 400, and no motion is requested

#### Scenario: Page stays responsive during a motion
- **WHEN** a client requests `GET /zero` while Push up is playing
- **THEN** the Zero check page is returned within 1 second, before the motion ends

### Requirement: Direct servo command
`GET /controller?servo=<i>&value=<a>`, with a servo index i from 0 to 7 and an integer angle a from 0 to 180, SHALL first call motion-playback's stop. It SHALL then command servo i to angle a through servo-output's direct servo command. The portal SHALL pass the requested angle unchanged and SHALL NOT clamp or offset it itself. The response SHALL be status 200, content type `text/html` and the body `(pm)=()`. If servo-output rejects the command, the response SHALL instead be status 503, content type `text/plain` and the body `servo output off`. A missing, non-integer or out-of-range `servo` or `value` SHALL be answered with status 400, and nothing is stopped or commanded. When a request carries both `pm` and `servo`, the request SHALL be answered with status 400, and nothing is requested, stopped or commanded. The refusals of "Portal lock during a remote session" and "Servo output modes" SHALL apply first.

As in stock, which attaches every servo at boot, a direct servo command SHALL also be accepted while servo output is in `idle` or `safe`. Switching output on for it is servo-output's direct servo command's rule (firmware-foundation): it enables output with origin `portal` at the enable pose, the stock zero pose 135, 45, 135, 45, 45, 135, 45, 135 for servos 0 to 7, and then applies the commanded servo's angle. The portal SHALL make no other call for it and SHALL NOT call servo-output's enable itself. This applies to every page control that sends a direct servo command: the Zero check servo buttons and the Motion editor's sliders and Send buttons.

#### Scenario: Move one servo
- **WHEN** servo output is `active`, and a client requests `GET /controller?servo=3&value=110`
- **THEN** servo-output is commanded to move servo 3 to 110°, and the response body is `(pm)=()`

#### Scenario: Missing value
- **WHEN** servo output is `active`, and a client requests `GET /controller?servo=3`
- **THEN** the response is status 400, and servo 3 receives no command

#### Scenario: Index out of range
- **WHEN** servo output is `active`, and a client requests `GET /controller?servo=9&value=90`
- **THEN** the response is status 400, and no servo receives a command

#### Scenario: Servo command during a motion
- **WHEN** no remote's session is live, and a client requests `GET /controller?servo=0&value=90` while Forward is playing
- **THEN** motion-playback's stop is called, and servo-output is then commanded to move servo 0 to 90°

#### Scenario: Servo command straight after boot
- **WHEN** the robot has just booted, servo output is in `idle`, no remote's session is live, and a client requests `GET /controller?servo=0&value=135`
- **THEN** motion-playback's stop is called, servo-output's direct servo command is called for servo 0 at 135°, the response is status 200 with the body `(pm)=()`, and the portal makes no enable call of its own

#### Scenario: Servo command after link loss
- **WHEN** a remote's session has lapsed, no session is live, servo output is in `safe`, and a client requests `GET /controller?servo=3&value=80`
- **THEN** servo-output's direct servo command is called for servo 3 at 80°, and the response is status 200 with the body `(pm)=()`

#### Scenario: Servo command rejected by servo-output
- **WHEN** no remote's session is live, servo output is not in `fault`, a client requests `GET /controller?servo=3&value=80`, and servo-output rejects the direct servo command
- **THEN** the response is status 503 with content type `text/plain` and the body `servo output off`

### Requirement: Unused pms parameter
The `pms` parameter of `/controller` SHALL have no effect, as in the stock image. The stock controller page defines a `controlPms` function, but no control calls it, and the stock handler never reads `pms`. A request that carries only `pms` SHALL be answered with status 200, content type `text/html` and the body `(pm)=()`. No page SHALL send `pms`.

#### Scenario: pms request
- **WHEN** a client requests `GET /controller?pms=3`
- **THEN** the response is status 200 with the body `(pm)=()`, and nothing moves

### Requirement: Zero check page
`GET /zero` SHALL return the Zero check page with status 200 and content type `text/html`. The page SHALL show one button per servo in the stock two-column arrangement by servo index: servos 4 and 0, then 5 and 1, then 6 and 2, then 7 and 3. Each button SHALL be labelled with its servo index and the GPIO that servo-output attaches that index to, in the form `S<i> · GPIO<g>` (for example `S0 · GPIO15`). The portal SHALL NOT use the stock pages' GPIO labels (`D14` for servo 0 and so on), because they do not match the pins that the stock firmware drives. Each button SHALL send `GET /controller?servo=<i>&value=<z>`, with the servo's stock zero-check angle z: 135° for servos 0, 2, 5 and 7, and 45° for servos 1, 3, 4 and 6. A full-width Zero position button SHALL send `GET /controller?pm=100`. The servos of the stock yellow group (0, 3, 4 and 7) and blue group (1, 2, 5 and 6) SHALL be visually distinguishable.

#### Scenario: Check one servo
- **WHEN** the operator taps the `S0` button on the Zero check page
- **THEN** the page sends `GET /controller?servo=0&value=135`

#### Scenario: Labels follow the hardware map
- **WHEN** the Zero check page is loaded
- **THEN** servo 0's button names GPIO15 and servo 4's button names GPIO2, matching servo-output's index-to-GPIO map

#### Scenario: Zero position
- **WHEN** the operator taps Zero position
- **THEN** the page sends `GET /controller?pm=100`

### Requirement: Servo calibration page
`GET /setting` SHALL return the Servo calibration page with status 200 and content type `text/html`. The page SHALL show, for each servo, a text field holding that servo's current calibration trim in degrees. The value is read from servo-calibration when the page is served. Each field has a SET button. The fields SHALL use the arrangement and labels of the Zero check page. SET SHALL send `GET /save?key=<i>&value=<field value>`. A RESET ALL button SHALL set every field to `0` and send `GET /save?key=100&value=0`. The stock page has this button commented out; this design keeps it, as the settled decisions list it.

#### Scenario: Page shows stored trims
- **WHEN** servo 2's stored trim is -3 and the operator opens `/setting`
- **THEN** the `S2` field shows `-3`

#### Scenario: Set one trim
- **WHEN** the operator enters `4` in the `S5` field and taps its SET button
- **THEN** the page sends `GET /save?key=5&value=4`

#### Scenario: Reset all
- **WHEN** the operator taps RESET ALL
- **THEN** every field shows `0`, and the page sends `GET /save?key=100&value=0`

### Requirement: Calibration save
`GET /save?key=<i>&value=<v>`, with i from 0 to 7 and an integer v within servo-calibration's accepted trim range, SHALL ask servo-calibration to store v as servo i's trim. The response SHALL be status 200, content type `text/html` and the body `(key, value)=(<i>,<v>)`, with the key and value as decimal integers. `GET /save?key=100` SHALL ask servo-calibration to reset every servo's trim to 0. Its `value` parameter is ignored and may be missing or malformed, as in stock, and the body SHALL always be `(key, value)=(100,0)`. Any other key, or a missing, non-integer or out-of-range value for keys 0 to 7, SHALL be answered with status 400, and no trim changes.

A save SHALL be accepted only while motion-playback reports that no motion or keyframe is playing or pending. Otherwise it SHALL be answered with status 409, content type `text/plain` and the body `motion playing`, and no trim changes. Stock serves no HTTP while a motion plays, so it never saves during one either. A save SHALL be accepted in every servo output mode, including `fault`, so that a damaged trim can be repaired. While a remote's session is live, a save SHALL be refused ("Portal lock during a remote session"), because a changed trim moves a driven servo at once. The lock and the `motion playing` refusal SHALL apply before the parameter checks of this requirement.

The portal SHALL NOT command any servo, request or stop any motion, or change the servo output mode as part of a save.

#### Scenario: Save a trim
- **WHEN** no motion is playing and a client requests `GET /save?key=1&value=-5`
- **THEN** the response body is `(key, value)=(1,-5)`, servo-calibration is asked to store -5 for servo 1, and a following `GET /setting` shows `-5` in the `S1` field

#### Scenario: Save while a servo is held
- **WHEN** servo 1 is held at 90°, no motion is playing, and a client requests `GET /save?key=1&value=-5`
- **THEN** the response is status 200, servo-calibration is asked to store -5 for servo 1, and the portal issues no servo command, no motion request and no stop

#### Scenario: Save during a motion
- **WHEN** Forward is playing and a client requests `GET /save?key=1&value=-5`
- **THEN** the response is status 409, and servo 1's trim is unchanged

#### Scenario: Reset all trims
- **WHEN** no motion is playing and a client requests `GET /save?key=100&value=abc`
- **THEN** servo-calibration is asked to reset all trims, the response body is `(key, value)=(100,0)`, and a following `GET /setting` shows `0` in every field

#### Scenario: Key outside the servos
- **WHEN** a client requests `GET /save?key=50&value=3`
- **THEN** the response is status 400, and servo-calibration is not called

#### Scenario: Repair in fault
- **WHEN** servo output is in `fault` because servo 2's stored trim is invalid, no remote's session is live, and a client requests `GET /save?key=2&value=0`
- **THEN** the response is status 200 with the body `(key, value)=(2,0)`, and servo-calibration is asked to store 0 for servo 2

### Requirement: Motion editor page
`GET /editor` SHALL return the Motion editor page with status 200 and content type `text/html`. For each servo, in the arrangement and labels of the Zero check page, the page SHALL show a slider from 45 to 135, a text field and a Send button. Each servo SHALL also show its stock default angle: 70 for servos 0 and 7, 110 for servos 3 and 4, and 90 for servos 1, 2, 5 and 6. Slider and field SHALL start at that default. The sliders of servos 0, 1, 4 and 5 SHALL run from 135 on the left to 45 on the right. The sliders of servos 2, 3, 6 and 7 SHALL run from 45 on the left to 135 on the right. Releasing a slider SHALL copy its value to that servo's field and send `GET /controller?servo=<i>&value=<value>`. Tapping Send SHALL send the field's text unchanged as `value`, as the stock editor does, and SHALL set the slider to it, which the slider shows limited to 45–135; the field keeps the typed text. A typed value that the robot refuses is reported in the status line (400). The page SHALL show an Action Code line: the eight current field values in servo order 0 to 7, separated by `, `. The line SHALL update on load and on every change. A Standby button SHALL send `GET /controller?pm=1`. As in the stock editor, the page SHALL send no other request.

#### Scenario: Move a slider
- **WHEN** the operator releases the `S6` slider at 120
- **THEN** the `S6` field shows 120, the page sends `GET /controller?servo=6&value=120`, and the Action Code line shows `70, 90, 90, 110, 110, 90, 120, 70`

#### Scenario: Initial action code
- **WHEN** the Motion editor page is loaded
- **THEN** the Action Code line shows `70, 90, 90, 110, 110, 90, 90, 70`

#### Scenario: Typed value
- **WHEN** the operator types 100 in the `S0` field and taps Send
- **THEN** the `S0` slider moves to 100, and the page sends `GET /controller?servo=0&value=100`

#### Scenario: Typed value beyond the slider
- **WHEN** the operator types 170 in the `S0` field and taps Send
- **THEN** the page sends `GET /controller?servo=0&value=170`, the `S0` slider shows 135, the field shows 170, and the Action Code line shows 170 for servo 0

### Requirement: Online keyframe
`GET /online?m0=<a0>&m1=<a1>&...&m7=<a7>&t1=<t>` SHALL pass one keyframe to motion-playback's single-keyframe call: the angles a0 to a7 for servos 0 to 7 and the duration t in milliseconds. All nine parameters SHALL be required. Each angle SHALL be an integer from 0 to 180, and t SHALL be an integer from 0 to 60000. How the keyframe is played, what it does to a playing motion or keyframe, and how it brings servo output back when output is off, are motion-playback's and firmware-foundation's. The response SHALL be status 200, content type `text/html` and the body `(m0, m1)=(<a0>,<a1>)`, sent as soon as motion-playback has accepted the keyframe, without waiting for it to end. Stock replied only after the keyframe; this departure keeps the server and the main loop free while the keyframe plays. A missing, non-integer or out-of-range parameter SHALL be answered with status 400, and no keyframe is passed. If motion-playback rejects the keyframe, the response SHALL be status 400. The refusals of "Portal lock during a remote session" and "Servo output modes" SHALL apply first. No stock page sends `/online`, and no portal page SHALL send it.

#### Scenario: Play a keyframe
- **WHEN** servo output is `active`, and a client requests `GET /online?m0=70&m1=90&m2=90&m3=110&m4=110&m5=90&m6=90&m7=70&t1=1000`
- **THEN** motion-playback is given the keyframe 70, 90, 90, 110, 110, 90, 90, 70 over 1000 ms, and the response `(m0, m1)=(70,90)` arrives within 1 second of the request, before the keyframe ends

#### Scenario: Pages served during a keyframe
- **WHEN** a client requests `GET /zero` while a 10000 ms `/online` keyframe plays
- **THEN** the Zero check page is returned within 1 second

#### Scenario: Missing joint
- **WHEN** servo output is `active`, and a client requests `/online` without `m5`
- **THEN** the response is status 400, and no keyframe is passed to motion-playback

#### Scenario: Duration out of range
- **WHEN** servo output is `active`, and a client requests `/online` with all eight angles and `t1=60001`
- **THEN** the response is status 400, and no keyframe is passed to motion-playback

### Requirement: Portal lock during a remote session
While a drive-link session is live (drive-link reports a live session: the pak has control), the portal SHALL refuse every request that moves the robot or changes a trim: `/controller` with `pm` or `servo`, `/online`, and `/save`. A refused request SHALL be answered with status 409, content type `text/plain` and the body `remote in control`, in every servo output mode, before any other check and before parameter validation, and SHALL make no call to motion-playback, servo-output or servo-calibration. The portal SHALL NOT enable servo output while a session is live. Page requests (`/`, `/zero`, `/setting` and `/editor`) and `/controller` with only `pms` SHALL still be served, and `/setting` SHALL still show the stored trims. The lock SHALL end when the session ends, by a lapse or because the pak leaves; the next request is then handled by the other requirements. The lock SHALL apply to requests only: a portal motion or keyframe that is playing when a session starts is ended by drive-link's session-start stop.

#### Scenario: Motion refused during a session
- **WHEN** a remote's session is live, servo output is `active`, and a client requests `GET /controller?pm=13`
- **THEN** the response is status 409 with the body `remote in control`, and no motion is requested

#### Scenario: Zero check refused during a session
- **WHEN** a remote's session is live, and a client requests `GET /controller?servo=0&value=135`, then `GET /controller?pm=100`
- **THEN** both responses are status 409 with the body `remote in control`, motion-playback's stop and request calls are not made, and servo-output receives no command

#### Scenario: Output off while a session is live
- **WHEN** servo output is in `safe`, a new session is live, and a client requests `GET /controller?servo=3&value=45`
- **THEN** the response is status 409 with the body `remote in control`, servo-output receives no command, and the mode stays `safe`

#### Scenario: Keyframe refused during a session
- **WHEN** a remote's session is live, and a client requests `GET /online` with all eight angles and `t1=1000`
- **THEN** the response is status 409 with the body `remote in control`, and no keyframe is passed to motion-playback

#### Scenario: Incomplete request during a session
- **WHEN** a remote's session is live, and a client requests `GET /controller?servo=3` without `value`
- **THEN** the response is status 409, not 400

#### Scenario: Pages and calibration during a session
- **WHEN** a remote's session is live, no motion is playing or pending, and a client requests `GET /setting`, then `GET /save?key=0&value=2`
- **THEN** the first response is status 200 with the stored trims, the second is status 409 with the body `remote in control`, and servo-calibration is not asked to store a trim

#### Scenario: Lock ends with the session
- **WHEN** a remote's session lapses, no session is live, servo output is in `safe`, and a client requests `GET /controller?pm=1`
- **THEN** the response is status 200 with the body `(pm)=(1)`, and motion-playback is asked for motion 1 (Standby)

### Requirement: Servo output modes
While no drive-link session is live, for every robot-moving request (`/controller` with `pm` or `servo`, and `/online`) the portal SHALL read servo-output's output mode (`idle`, `active`, `safe` or `fault`), as firmware-foundation defines it, and SHALL handle the request as follows, before parameter validation:

| Mode | `pm` and `/online` | `servo` |
|---|---|---|
| `active` | passed on | passed on |
| `idle` or `safe` | passed on | passed on |
| `fault` | refused, 503 `servo output fault` | refused, 503 `servo output fault` |

Refusals SHALL have content type `text/plain`, and a refused request SHALL make no call to motion-playback or servo-output. A `pm`, `/online` or `servo` request passed on in `idle` or `safe` is an explicit request; switching servo output on for it is firmware-foundation's and motion-playback's rule (for `servo`, servo-output's direct servo command; for `pm` and `/online`, motion-playback's enable with origin `portal`), and the portal makes no other call for it. The portal SHALL NOT call servo-output's enable or safe-state request itself. Page requests, `/controller` with only `pms`, and `/save` SHALL be served in every mode, subject to "Portal lock during a remote session" for `/save`.

#### Scenario: Incomplete request while output is off
- **WHEN** servo output is in `safe`, no session is live, and a client requests `GET /controller?servo=3` without `value`
- **THEN** the response is status 400, and nothing is stopped or commanded

#### Scenario: Motion request straight after boot
- **WHEN** the robot has just booted, servo output is in `idle`, no session is live, and a client requests `GET /controller?pm=100`
- **THEN** the response is status 200 with the body `(pm)=(100)`, motion-playback is asked for motion 100 (Zero position), and the portal makes no enable call of its own

#### Scenario: Calibration fault
- **WHEN** servo output is in `fault`, no session is live, and a client requests `GET /controller?pm=2`, then `GET /controller?servo=0&value=135`, then `GET /setting`
- **THEN** the first and second responses are status 503 with the body `servo output fault`, no motion is requested and no servo is commanded, and the third is status 200
