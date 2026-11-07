# MiniSpooder.pak

MiniSpooder.pak is a NextUI tool pak for the TrimUI Smart Pro. It plays the robot's stock motions over the robot's drive link, on the robot's own access point. It needs the robot to run this project's firmware: the stock firmware has no drive link, and it ignores the pak's datagrams.

The OpenSpec change `handheld-pak` specifies it. The source is in `handheld/`.

## Build and install

The device build runs under rootless Podman, with qemu-user-static for arm64.

1. Run `mise install`.
2. Run `mise run handheld-package`. It builds the arm64 binary, checks it (AArch64, GLIBC 2.31 at most) and writes `build/handheld/MiniSpooder.pak.zip`.
3. Unzip `MiniSpooder.pak.zip` into `Tools/tg5040/` on the TrimUI's SD card.

The folder `Tools/tg5040/MiniSpooder.pak/` then holds `launch.sh`, `minispooder` and `lib/libSDL2_gfx-1.0.so.0`. It does not touch any other pak, including `Spooder.pak`. To remove the pak, delete the folder.

When NextUI sets a log directory, the log goes to `MiniSpooder.txt` in it.

## Join the robot's network

The pak does not join WiFi networks itself.

1. Switch the robot on.
2. In NextUI's WiFi settings, join the network `Spider Robot` with the password `12345678`.
3. Start MiniSpooder from Tools.

At start the pak checks that `wlan0` has an address in 192.168.4.0/24. If it has none, the pak renews the lease once. If there is still none, it names the robot's network, its password and the network `wlan0` is on. Press A to check again, or Menu to quit.

While it runs, the pak turns WiFi power saving off. It restores the setting however it ends.

## Controls

Each control acts once on its press and does nothing on release.

| Control | Request |
|---|---|
| D-pad up / down / left / right | Forward / Backward / Turn left / Turn right |
| L1 / R1 | Left shift / Right shift |
| Start | Standby |
| A / X / Y | Say Hi / Fighting / Push up |
| R2 held, then A / X / Y | Dance 1 / Dance 2 / Dance 3 |
| L2 held, then A / X / Y | Lie / Sleep / Zero position |
| B, with or without a trigger held | Stop |
| Menu | Leave |
| Power, short press / held 2 s | Leave and suspend / leave and power off |

- **Walking:** while a D-pad direction or L1 or R1 is held, the pak requests the same motion again each time the previous one has ended. Releasing the control ends the walk after the step that plays.
- **Both triggers:** while L2 and R2 are both held, A, X and Y do nothing.
- **Stop:** B ends the motion and holds the pose reached. Press Start afterwards to return to Standby.
- **Centre:** no control requests Centre (motion 99). It stays on the web portal.
- **Not sent:** a press is taken only while the link is live, and only one new request goes in each 50 ms frame. A press that is not taken shows `NOT SENT` and is dropped, not queued.

## The screen

- **Header:** the link chip (`LINKED`, `STALE` or `NO TELEMETRY`) and the WiFi signal in dBm.
- **Robot panel:** the motion the robot reports as playing (or `IDLE`) and as pending (or `NONE`). It also shows the eight joint angles by servo index and GPIO, with `---` for a servo that is not commanded or is limp, and the robot's own link counts. After 500 ms without telemetry the panel is marked `STALE`, and it keeps the last values.
- **Controls panel:** the active layer and what A, X and Y request in it. It also shows the last request with its status, and `REPEATING` while a walk repeats:
  - `REQUESTED`: sent, not yet echoed by the robot.
  - `TAKEN`: the robot accepted it.
  - `REJECTED`: the robot rejected it.
  - `NOT TAKEN`: it became a new session's baseline.
  - `NOT CONFIRMED`: no echo within 1 s.
  - `NOT SENT`: the press was not taken.
- **Link panel:** telemetry frames per second, the round-trip time, invalid, lost and late frames, send errors, and the time since the last frame.

## Leaving

Menu or Power first requests Standby and keeps sending frames until the robot reports that Standby has finished, for at most 3 s. B still stops during this wait: the pak then requests Standby again. After the wait the pak stops sending, and exits, suspends or powers off. About 300 ms later the robot's link-loss rule releases the servos.

After a suspend the pak is driving again. It takes no press until the link is live again.

## Desktop build and the fake robot

The desktop build needs the host's SDL2 development packages. `mise run handheld-deps` names any that are missing.

1. Run `mise run fake-robot` in one terminal. It listens on `127.0.0.1:52041` and logs each request.
2. Run `mise run handheld-build` in another terminal.
3. Run `build/handheld/amd64/minispooder --addr 127.0.0.1:52041`.

Without a controller, the keyboard stands in for one:

| Key | Control |
|---|---|
| Arrow keys | D-pad |
| A, B, X, Y | A, B, X, Y |
| Q / E | L1 / R1 |
| 1 / 3 held | L2 / R2 |
| Enter | Start |
| Escape | Menu |

## Checks

| Task | Does |
|---|---|
| `mise run handheld-fork` | Downloads gabagool v2.26.6 and applies the power-button patch |
| `mise run handheld-deps` | Checks the SDL2 development packages |
| `mise run handheld-test` | Runs gofmt, go vet, staticcheck and the host tests |
| `mise run handheld-e2e` | Runs the end-to-end tests against an in-process fake robot |
| `mise run handheld-build` | Builds the desktop binary and the fake robot |
| `mise run handheld-build-device` | Builds the arm64 binary under Podman |
| `mise run handheld-package` | Writes `MiniSpooder.pak.zip` |
| `mise run fake-robot` | Runs the fake robot |
