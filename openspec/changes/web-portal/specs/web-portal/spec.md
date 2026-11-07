# Spec Delta

## Purpose

The robot's web portal on its own access point. It reproduces the stock firmware's pages and routes for driving motions, moving single servos, checking the zero pose, calibrating the servos and editing poses. It is restyled in the Blackfyre Paper theme and shares the robot safely with the MiniSpooder.pak remote.

## ADDED Requirements

### Requirement: Portal server and address
The firmware SHALL serve the portal over HTTP on TCP port 80 at the robot's address on its own access point. drive-link defines the access point. The server SHALL be up within 10 seconds of power-on and SHALL stay up while the firmware runs, also while a motion plays and while a remote is connected. The routes SHALL answer `GET` requests. A request for any other path, or a request with any other method, SHALL be answered with status 404, content type `text/plain` and the body `Not found: <path>`, as the stock image's web server does. The portal SHALL NOT run a DNS server or redirect requests, because the stock image has no captive portal.

#### Scenario: Phone opens the portal
- **WHEN** a phone joined to the robot's access point requests `http://<robot address>/`
- **THEN** it receives the Controller page with status 200

#### Scenario: Unknown path
- **WHEN** a client requests `GET /generate_204`
- **THEN** the response has status 404, content type `text/plain` and the body `Not found: /generate_204`, and no servo moves

#### Scenario: Wrong method
- **WHEN** a client sends `POST /controller?pm=2`
- **THEN** the response has status 404, and no motion starts

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
Every page SHALL show a navigation bar linking the four pages: Controller (`/`), Zero check (`/zero`), Servo calibration (`/setting`) and Motion editor (`/editor`). The bar SHALL mark the current page. Every page that sends commands SHALL show a status line with the outcome of its most recent command: sent (status 200), refused because the handheld is driving (status 409), invalid (status 400), or no answer from the robot within 5 seconds. A no-answer outcome SHALL say that the command may still run, since the robot may be serving an earlier request. A command SHALL NOT reload the page.

#### Scenario: Navigate to calibration
- **WHEN** the operator taps Servo calibration in the navigation bar on the Controller page
- **THEN** the browser opens `/setting`, and the bar marks Servo calibration as the current page

#### Scenario: Command refused
- **WHEN** the operator taps Forward while the handheld is driving the robot
- **THEN** the status line says the handheld is driving and that the command was not run, and the page stays open

#### Scenario: Robot out of reach
- **WHEN** the operator taps a control and no response arrives within 5 seconds
- **THEN** the status line says that the robot did not answer and that the command may still run

### Requirement: Controller page
`GET /` SHALL return the Controller page with status 200 and content type `text/html`. The page SHALL offer the stock motion buttons in the stock order, three to a row. The first group is Turn left (6), Forward (2) and Turn right (7), then Left shift (4), Backward (3) and Right shift (5). After a gap, the second group is Standby (1), Say Hi (9) and Push up (11); Lie (8), Fighting (10) and Sleep (12); and Dance 1 (13), Dance 2 (14) and Dance 3 (15). Each button SHALL send `GET /controller?pm=<n>` with its number. The page SHALL NOT show the stock vendor title or the stock shop advert.

#### Scenario: Forward button
- **WHEN** the operator taps Forward
- **THEN** the page sends `GET /controller?pm=2`

#### Scenario: All motions present
- **WHEN** the Controller page is loaded
- **THEN** it shows exactly fifteen motion buttons, one for each number from 1 to 15, labelled with the stock names

### Requirement: Motion request
`GET /controller?pm=<n>`, where n is a motion id that motion-playback defines, SHALL request that motion from motion-playback. A request from the portal SHALL have the same effect as the same request from the drive link. The response SHALL be status 200, content type `text/html` and the body `(pm)=(<pm>)`, where `<pm>` is the `pm` argument exactly as received (so `pm=02` gives `(pm)=(02)`, as stock echoes the argument string), sent without waiting for the motion to finish. The behaviour when a motion is already playing SHALL be motion-playback's. If n is not a motion id that motion-playback defines, or is not an integer, the response SHALL be status 400, and no motion starts.

#### Scenario: Start a motion
- **WHEN** a client requests `GET /controller?pm=9` and no remote is connected
- **THEN** the response is status 200 with the body `(pm)=(9)`, and the robot plays Say Hi

#### Scenario: Motion id 100
- **WHEN** a client requests `GET /controller?pm=100`
- **THEN** the robot moves to the zero position, as motion-playback defines for id 100

#### Scenario: Unknown motion id
- **WHEN** a client requests `GET /controller?pm=21`
- **THEN** the response is status 400, and no servo moves

#### Scenario: Page stays responsive during a motion
- **WHEN** a client requests `GET /zero` while Push up is playing
- **THEN** the Zero check page is returned within 1 second, before the motion ends

### Requirement: Direct servo command
`GET /controller?servo=<i>&value=<a>`, with a servo index i from 0 to 7 and an integer angle a within servo-output's accepted angle range, SHALL first stop any playing motion through motion-playback. It SHALL then command servo i to angle a through servo-output. How the calibration offset is applied is servo-output's and servo-calibration's. The response SHALL be status 200, content type `text/html` and the body `(pm)=()`. A missing, non-integer or out-of-range `servo` or `value` SHALL be answered with status 400, and no servo moves. When a request carries both `pm` and `servo`, the request SHALL be answered with status 400, and nothing moves.

#### Scenario: Move one servo
- **WHEN** a client requests `GET /controller?servo=3&value=110` and no remote is connected
- **THEN** servo-output is commanded to move servo 3 to 110°, and the response body is `(pm)=()`

#### Scenario: Missing value
- **WHEN** a client requests `GET /controller?servo=3`
- **THEN** the response is status 400, and servo 3 does not move

#### Scenario: Index out of range
- **WHEN** a client requests `GET /controller?servo=9&value=90`
- **THEN** the response is status 400, and no servo moves

#### Scenario: Servo command during a motion
- **WHEN** a client requests `GET /controller?servo=0&value=90` while Forward is playing
- **THEN** motion-playback is told to stop Forward, and servo-output is then commanded to move servo 0 to 90°

### Requirement: Unused pms parameter
The `pms` parameter of `/controller` SHALL have no effect, as in the stock image. The stock controller page defines a `controlPms` function, but no control calls it, and the stock handler never reads `pms`. A request that carries only `pms` SHALL be answered with status 200, content type `text/html` and the body `(pm)=()`. No page SHALL send `pms`.

#### Scenario: pms request
- **WHEN** a client requests `GET /controller?pms=3`
- **THEN** the response is status 200 with the body `(pm)=()`, and nothing moves

### Requirement: Zero check page
`GET /zero` SHALL return the Zero check page with status 200 and content type `text/html`. The page SHALL show one button per servo, labelled with its stock GPIO label, in the stock two-column arrangement: D16 (servo 4) and D14 (servo 0), then D05 (5) and D12 (1), then D04 (6) and D13 (2), then D02 (7) and D15 (3). Each button SHALL send `GET /controller?servo=<i>&value=<z>`, with the servo's stock zero-check angle z: 135° for servos 0, 2, 5 and 7, and 45° for servos 1, 3, 4 and 6. A full-width Zero position button SHALL send `GET /controller?pm=100`. The servos of the stock yellow group (0, 3, 4 and 7) and blue group (1, 2, 5 and 6) SHALL be visually distinguishable.

#### Scenario: Check one servo
- **WHEN** the operator taps D14 on the Zero check page
- **THEN** the page sends `GET /controller?servo=0&value=135`

#### Scenario: Zero position
- **WHEN** the operator taps Zero position
- **THEN** the page sends `GET /controller?pm=100`

### Requirement: Servo calibration page
`GET /setting` SHALL return the Servo calibration page with status 200 and content type `text/html`. The page SHALL show, for each servo, a text field holding that servo's current calibration offset in degrees. The value is read from servo-calibration when the page is served. Each field has a SET button. The fields SHALL use the stock arrangement and labels of the Zero check page. SET SHALL send `GET /save?key=<i>&value=<field value>`. A RESET ALL button SHALL set every field to `0` and send `GET /save?key=100&value=0`. The stock page has this button commented out; this design keeps it, as the settled decisions list it.

#### Scenario: Page shows stored offsets
- **WHEN** servo 2's stored offset is -3 and the operator opens `/setting`
- **THEN** the D13 field shows `-3`

#### Scenario: Set one offset
- **WHEN** the operator enters `4` in the D05 field and taps its SET button
- **THEN** the page sends `GET /save?key=5&value=4`

#### Scenario: Reset all
- **WHEN** the operator taps RESET ALL
- **THEN** every field shows `0`, and the page sends `GET /save?key=100&value=0`

### Requirement: Calibration save
`GET /save?key=<i>&value=<v>`, with i from 0 to 7 and an integer v within servo-calibration's accepted offset range, SHALL ask servo-calibration to store v as servo i's offset. The response SHALL be status 200, content type `text/html` and the body `(key, value)=(<i>,<v>)`, with the key and value as decimal integers. `GET /save?key=100` SHALL ask servo-calibration to reset every servo's offset to 0. Its `value` parameter is ignored and may be missing or malformed, as in stock, and the body SHALL always be `(key, value)=(100,0)`. Persistence and when an offset takes effect are servo-calibration's and servo-output's. The portal SHALL NOT command any servo as part of a save. Any other key, or a missing, non-integer or out-of-range value for keys 0 to 7, SHALL be answered with status 400, and no offset changes. Saving SHALL be accepted also while a remote is connected.

#### Scenario: Save an offset
- **WHEN** a client requests `GET /save?key=1&value=-5`
- **THEN** the response body is `(key, value)=(1,-5)`, servo-calibration is asked to store -5 for servo 1, and a following `GET /setting` shows `-5` in the D12 field

#### Scenario: Reset all offsets
- **WHEN** a client requests `GET /save?key=100&value=abc`
- **THEN** servo-calibration is asked to reset all offsets, the response body is `(key, value)=(100,0)`, and a following `GET /setting` shows `0` in every field

#### Scenario: Save moves nothing
- **WHEN** a client requests `GET /save?key=1&value=-5`
- **THEN** the portal issues no servo command and no motion request

#### Scenario: Key outside the servos
- **WHEN** a client requests `GET /save?key=50&value=3`
- **THEN** the response is status 400, and no stored data changes

### Requirement: Motion editor page
`GET /editor` SHALL return the Motion editor page with status 200 and content type `text/html`. For each servo, in the stock arrangement and labels of the Zero check page, the page SHALL show a slider from 45 to 135, a text field and a Send button. Each servo SHALL also show its stock default angle: 70 for servos 0 and 7, 110 for servos 3 and 4, and 90 for servos 1, 2, 5 and 6. Slider and field SHALL start at that default. The sliders of servos 0, 1, 4 and 5 SHALL run from 135 on the left to 45 on the right. The sliders of servos 2, 3, 6 and 7 SHALL run from 45 on the left to 135 on the right. Releasing a slider, or tapping Send, SHALL copy the value to both the slider and the field of that servo and send `GET /controller?servo=<i>&value=<value>`. The page SHALL show an Action Code line: the eight current field values in servo order 0 to 7, separated by `, `. The line SHALL update on load and on every change. A Standby button SHALL send `GET /controller?pm=1`.

#### Scenario: Move a slider
- **WHEN** the operator releases the D04 slider at 120
- **THEN** the D04 field shows 120, the page sends `GET /controller?servo=6&value=120`, and the Action Code line shows `70, 90, 90, 110, 110, 90, 120, 70`

#### Scenario: Initial action code
- **WHEN** the Motion editor page is loaded
- **THEN** the Action Code line shows `70, 90, 90, 110, 110, 90, 90, 70`

#### Scenario: Typed value
- **WHEN** the operator types 100 in the D14 field and taps Send
- **THEN** the D14 slider moves to 100, and the page sends `GET /controller?servo=0&value=100`

### Requirement: Online keyframe
`GET /online?m0=<a0>&m1=<a1>&...&m7=<a7>&t1=<t>` SHALL play a single keyframe through motion-playback. The keyframe moves servos 0 to 7 from their current angles to the angles a0 to a7 over t milliseconds, with the same interpolation and calibration as motion keyframes. All nine parameters SHALL be required. Each angle SHALL be an integer within servo-output's accepted angle range, and t SHALL be an integer from 0 to 60000. As in the stock image, the response SHALL be sent when the keyframe has finished, or as soon as it is stopped early (for example by a remote taking control), and in any case no later than t + 500 ms after the keyframe started. The body is the same in all three cases. It SHALL have status 200, content type `text/html` and the body `(m0, m1)=(<a0>,<a1>)`. A missing, non-integer or out-of-range parameter SHALL be answered at once with status 400, and nothing moves. A playing motion SHALL be stopped before the keyframe starts. No page SHALL use `/online`.

#### Scenario: Play a keyframe
- **WHEN** a client requests `GET /online?m0=70&m1=90&m2=90&m3=110&m4=110&m5=90&m6=90&m7=70&t1=500` and no remote is connected
- **THEN** the robot moves to that pose over about 500 ms, and the response `(m0, m1)=(70,90)` arrives after the move

#### Scenario: Missing joint
- **WHEN** a client requests `/online` without `m5`
- **THEN** the response is status 400, and no servo moves

### Requirement: One controller at a time
While drive-link reports a connected remote, the portal SHALL refuse every request that would move a servo: `/controller` with `pm` or `servo`, and `/online`. It SHALL answer them with status 409, content type `text/plain`, and a body saying the handheld is driving the robot. This check SHALL come before parameter validation: such a request gets 409 even when its parameters are missing or invalid. Nothing SHALL move as a result of a refused request. Page requests, `/controller` with only `pms`, and `/save` SHALL still be served. When no remote is connected any more, as drive-link defines, the portal SHALL accept robot-moving requests again. A request accepted before the remote connected, such as a playing motion or an `/online` keyframe, SHALL be handled as drive-link and motion-playback define for a remote taking control.

#### Scenario: Handheld driving
- **WHEN** the pak is connected and a client requests `GET /controller?pm=2`
- **THEN** the response is status 409, and the robot keeps following the pak

#### Scenario: Incomplete request while the handheld is connected
- **WHEN** the pak is connected and a client requests `GET /controller?servo=3` without `value`
- **THEN** the response is status 409, not 400, and nothing moves

#### Scenario: Calibration while the handheld is connected
- **WHEN** the pak is connected and a client requests `GET /save?key=0&value=2`
- **THEN** the offset is saved, and the response is status 200

#### Scenario: Handheld leaves
- **WHEN** the pak disconnects, drive-link reports no connected remote, and a client then requests `GET /controller?pm=1`
- **THEN** the response is status 200, and the robot plays Standby
