# Tasks

Prerequisite: `drive-link` is implemented (its codec in `core/` and its frame definitions settled). Replace the placeholders of design A1–A5 with its values before group 2; the checks named below assume them.

## 1. Toolchain and module

- [ ] 1.1 Add `go = "1.27.1"` to `mise.toml` and create the module `github.com/blackfyre/mini-spooder/handheld` in `handheld/` (`go.mod` with gabagool v2.26.6 and go-sdl2, `cmd/minispooder`, `internal/`), and verify `mise install` and `cd handheld && go build ./internal/...` succeed
- [ ] 1.2 Copy the gabagool power-hook patch to `handheld/third_party/gabagool-power-hook.patch`, write `handheld/scripts/fork-gabagool.sh` (`go mod download -json` of v2.26.6, copy to `build/handheld/gabagool`, `git apply --check` then apply) and the `handheld-fork` task, add the `replace` to `handheld/go.mod`, and verify `mise run handheld-fork` succeeds twice in a row and `go list -m github.com/BrandonKowalski/gabagool/v2` shows the replacement
- [ ] 1.3 Write `handheld/scripts/check-deps.sh` (names the missing Fedora or Debian SDL2 development packages) and the `handheld-deps` task, and verify it exits 0 on a host with them and names `SDL2_gfx` when its pkg-config file is hidden from `PKG_CONFIG_PATH`
- [ ] 1.4 Add the `handheld-test` task (gofmt check, go vet, staticcheck as a module tool, go test, depending on `handheld-fork` and `handheld-deps`), and verify `mise run handheld-test` passes on the empty packages

## 2. Protocol and golden frames

- [ ] 2.1 Add `test/test_drive_link_golden/` to the PlatformIO native env: it encodes fixed command and telemetry cases (counter wrap, motion 100, eight angles at range ends) with `core/`'s codec and compares them with `test/golden/drive-link-frames.txt`, rewriting it when `DRIVE_LINK_GOLDEN_UPDATE=1`; verify `pio test -e native -f test_drive_link_golden` passes, and fails after one byte of the committed file is changed (skip and use `drive-link`'s file if it ships one, design A7)
- [ ] 2.2 Write `handheld/internal/protocol` (frame encode, decoder with discard and resynchronise and a discard count, command and telemetry types per `drive-link`), and verify its tests: every golden command encodes to its bytes, every golden telemetry decodes to its fields, a flipped CRC byte, a truncated frame and a frame with one extra byte are rejected
- [ ] 2.3 Write `handheld/internal/motions` (ids 1–15 and 100 to their stock names, `MOTION <n>` and `IDLE`), and verify a test that the table matches `baseline/README.md`'s motion table and covers exactly those ids

## 3. Mapping, link and joining

- [ ] 3.1 Write `handheld/internal/padmap` (the spec's control table, press-edge events, layer read at the press, both triggers blocking A/X/Y, B as stop in every layer, Select, sticks, Menu and Power ignored), and verify go tests for each scenario of the spec's Control mapping requirement, including "every stock motion reachable"
- [ ] 3.2 Write `handheld/internal/udplink`: the sender (one connected socket, the command period, sequence +1 per frame, send-error count) and the receiver (source check, exactly one telemetry frame per datagram, invalid count, sequence-ordered newest, lost/late with wrap, rebaseline on first frame, resume and 500 ms gap, frames in the last second); verify go tests on loopback for the spec's Command datagrams, Received frames and Link quality scenarios
- [ ] 3.3 Write `handheld/internal/wlan` (address check in the robot's subnet, one `udhcpc` renewal, current network from `iw dev wlan0 link`, power-save read/off/restore, signal from `/proc/net/wireless` or `iw` with a 500 ms bound) behind an injectable runner, and verify go tests for the spec's Joining and WiFi power saving scenarios and the unknown-signal case with a fake runner

## 4. App, leave and screen model

- [ ] 4.1 Write `handheld/internal/app`: `ParseFlags` (`--device`, `--addr`), `Start` (WiFi steps and address check only with `--device`), the run loop turning pad events into the desired state (no motion requested at start; presses taken only while the link is live, so none before two telemetry frames after a start, a resume or a loss, and none queued), and the leave state machine of design D5 with an injected clock; verify go tests for the spec's Desktop build and Leaving safely scenarios and the Control mapping's "press before the robot answers" and "press after a resume" (two newer idle frames, delayed idle frame, controls ignored, 3 s timeout, second press ignored, power save restored before suspend and power-off, off again after resume)
- [ ] 4.2 Write the screen model in `handheld/internal/app` (header chip, robot panel, controls panel with the active layer's labels from the padmap table, link panel, footer, `STALE` after 500 ms), and verify go tests for the spec's Drive screen scenarios and "status not by colour alone"
- [ ] 4.3 Add `handheld/internal/theme`: the snapshot `tokens.json` (from the Blackfyre design system's `project/tokens.json`), a generator writing `theme_gen.go` with the tokens of design D8, and a test that regenerates in memory and compares; verify `go generate ./internal/theme` leaves `git diff` empty and the test fails when one value in the snapshot is changed

## 5. The SDL screen

- [ ] 5.1 Write `handheld/cmd/minispooder` (gabagool in NextUI mode with the power hook when `--device`, the 1280×720 screen drawn from the screen model every 50 ms with `theme_gen.go` colours, embedded Archivo SemiBold and JetBrains Mono with their OFL licences, the join and leave messages), and verify `mise run handheld-build` produces `build/handheld/amd64/minispooder` and `mise run handheld-test` passes
- [ ] 5.2 Add the `handheld-build` task (desktop binary and `fake-robot`), and verify it builds both from a clean `build/handheld/amd64`

## 6. Fake robot and end-to-end tests

- [ ] 6.1 Write `handheld/internal/fakerobot` and `handheld/cmd/fake-robot` (design D10: first sender locked, other senders ignored, telemetry at the drive link's rate, motion played on a counter advance for a fixed time, stop to idle, link loss to idle and unlock, drop and corrupt hooks), and verify go tests for the spec's Fake robot scenarios
- [ ] 6.2 Write `handheld/e2e` (in-process fake robot on a free loopback port, `app.ParseFlags`/`app.Start` with `--addr`, scripted pad input: play, stop, leave by Menu only after idle is confirmed, second sender ignored, corrupted telemetry counted invalid) and the `handheld-e2e` task, and verify `mise run handheld-e2e` passes
- [ ] 6.3 Add the `fake-robot` task (build and run `fake-robot --listen 127.0.0.1:<drive-link port>`), and verify by hand that the desktop build started with `--addr` at it shows a played motion and returns to `IDLE` after B

## 7. Device build and packaging

- [ ] 7.1 Write `handheld/build/Containerfile` (bullseye-slim by digest, linux/arm64, SDL2 dev packages, Go with SHA-256 checked) and `handheld/scripts/check-go-version.sh` (Containerfile's Go equals `mise.toml`'s), and verify the script passes, and fails when either version is edited
- [ ] 7.2 Write `handheld/scripts/check-glibc.sh` (AArch64 machine, highest GLIBC need ≤ 2.31) with a test using a stub `readelf` on `PATH`, and verify AArch64 + GLIBC_2.31 passes, AArch64 + GLIBC_2.34 fails and x86-64 + GLIBC_2.17 fails
- [ ] 7.3 Write `handheld/scripts/build-device.sh` and the `handheld-build-device` task (rootless Podman, qemu-user-static, `go build -trimpath` to `build/handheld/arm64/minispooder`, copy `libSDL2_gfx-1.0.so.0`, then `check-glibc.sh`), and verify `mise run handheld-build-device` succeeds and `check-glibc.sh` passes on its output
- [ ] 7.4 Write `handheld/pak/launch.sh` (design D6: power-save read and restore traps, own process group, `--device`, `LD_LIBRARY_PATH`, `MiniSpooder.txt` log) with a test running it under `sh` with stub `iw` and binary on `PATH`, and verify the setting is restored after a normal exit, after the stub exits 1 and after SIGTERM to the script
- [ ] 7.5 Write `handheld/scripts/package.sh` and the `handheld-package` task, and verify `mise run handheld-package` writes `build/handheld/MiniSpooder.pak.zip` whose `unzip -l` lists exactly `MiniSpooder.pak/launch.sh`, `MiniSpooder.pak/minispooder` and `MiniSpooder.pak/lib/libSDL2_gfx-1.0.so.0`, both files mode 755

## 8. Documentation

- [ ] 8.1 Write `docs/handheld.md` (install into `Tools/tg5040/`, join the robot's network, the control table, the screen, leaving, running the desktop build against the fake robot), and verify each `mise run` task it names exists in `mise.toml` (`mise tasks` lists it)

## 9. On-device check (with the user, robot on this project's firmware)

- [ ] 9.1 Install `MiniSpooder.pak` into `Tools/tg5040/` beside `Spooder.pak`, and verify both appear in NextUI's Tools, `ldd` of the binary on the device shows no missing library, and `Spooder.pak`'s files are unchanged (checksums before and after)
- [ ] 9.2 Join the robot's network, start MiniSpooder, and verify the link chip reads LINKED, the signal shows in dBm, and lost and invalid counts stay at 0 over one minute at 1 m from the robot
- [ ] 9.3 Press every control of the mapping table, and verify the robot plays each motion 1–15 and 100 named on the screen, and B stops a running Dance and the screen shows `IDLE`
- [ ] 9.4 Leave by Menu during Forward, by a short Power press and by a 2 s Power hold, and verify each time the screen shows the stop confirmed by telemetry (not the 3 s timeout), the robot is at standby before the pak exits, suspends or powers off, and `iw dev wlan0 get power_save` shows the value it had before the pak started
