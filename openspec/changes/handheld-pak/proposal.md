# Proposal

## Why

The robot's stock remote is a web page on a phone: one tap per motion, no feedback, and no way to tell whether the robot heard the request. The settled plan is a dedicated remote, a NextUI tool pak on a TrimUI Smart Pro, that plays the robot's stock motions from physical controls over the robot's own access point, using the drive link (`drive-link`). This change specifies that pak, `MiniSpooder.pak`: how it is built and installed, how it joins the robot, how its controls map to the stock motions, what it shows, and how it leaves without leaving the robot moving.

## What Changes

- A new Go module in `handheld/` (gabagool and SDL2), modelled on Spooder.pak (`../spooder/desktop/handheld`) but independent of it: code and ideas are copied, nothing is imported from or edits `../spooder`.
- The pak joins the robot's access point (as `drive-link` defines it), checks its address, and after the drive link's 350 ms start silence sends the complete desired state in command frames at its 50 ms period from one UDP socket.
- A control mapping from the TrimUI's buttons to every motion of the stock controller page (`motion-playback` ids 1–15 and 100; Centre, 99, has no stock button and stays on the web portal) and to the drive link's stop request (motion 0); one request per press, as the stock portal sends one request per click, except that a held walking control (motions 2–7) requests its motion again each time telemetry shows the previous one has ended; the robot keeps playing each request once.
- One drive screen in the Blackfyre design system's dark app style: the motion the robot reports as playing and as pending, its joint angles, what the pak requested and whether the robot took it (from the drive link's telemetry echo), the active control layer, and the link's quality with its round-trip time.
- Leaving safely: Menu and the Power button first request Standby and wait until telemetry shows it has finished (at most 3 s), then stop sending, so that the drive link's link-loss rule takes over, and exit, suspend or power off; WiFi power saving is turned off for the run and restored however the pak ends.
- Packaging: an arm64 device build under rootless Podman (glibc no newer than the device's), a zip of `MiniSpooder.pak` for `Tools/tg5040/`, and a desktop (amd64) build.
- A fake robot (Go, loopback UDP) that speaks the drive link, for host and end-to-end tests and for running the desktop build without hardware.
- `mise.toml` tasks for the pak's fork, dependency check, test, desktop build, device build, package, fake robot and end-to-end tests, and a Go version pin.

No firmware behaviour changes here: the access point, frame layouts, link-loss behaviour and the motions themselves belong to `drive-link` and `motion-playback`.

## Capabilities

### New Capabilities
- `handheld-pak`: MiniSpooder.pak, the TrimUI Smart Pro remote: building and installing it, joining the robot's network, the control mapping to the stock motions, hold-to-repeat for walking, the drive screen, link quality, leaving safely, the desktop build and the fake robot used to test it.

### Modified Capabilities

None.

## Impact

- **New code:** `handheld/` (Go module: `cmd/`, `internal/`, `pak/launch.sh`, `scripts/`, `build/Containerfile`, `third_party/` for the gabagool patch, embedded fonts with their OFL licences, a token snapshot of the Blackfyre design system).
- **Tests:** the pak's Go codec is checked against `drive-link`'s golden frames file, written from `core/`'s codec; this change adds no firmware test.
- **Tooling:** `mise.toml` gains Go and the `handheld-*` tasks; the device build needs rootless Podman with qemu-user-static for arm64; the desktop build needs the host's SDL2 development packages.
- **Dependencies:** `github.com/BrandonKowalski/gabagool/v2` (pinned, patched locally), `github.com/veandco/go-sdl2`.
- **Depends on:** `drive-link` (access point, framing, command and telemetry frames, link loss), `motion-playback` (motion ids and names, stop, the pending request) and `firmware-foundation` (servo indices and GPIOs; the safe state after link loss and its ending by the next motion request). None is changed by this change.
- **Device:** a new folder `Tools/tg5040/MiniSpooder.pak` on the TrimUI's SD card, next to (and independent of) any `Spooder.pak`.
