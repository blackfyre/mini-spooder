# mini-spooder

Replacement firmware for a small 8-servo ESP8266 quadruped robot, and **MiniSpooder.pak**, a remote for it on the TrimUI Smart Pro.

The robot is a Chinese clone of the LAFVIN Quadruped Spider Robot kit. Its stock firmware is an Arduino port of Jason Workshop's Q1 mini, and the vendor offers no support. This project reproduces the stock firmware's functionality, rebuilt so that the main loop never blocks and a lost link always stops the robot. It adds a handheld remote on top.

- **Stock motions:** all 15 (Standby, walking, turning, Say Hi, Push up, three dances and so on), plus Centre and Zero, taken byte-exact from the stock flash image.
- **Phone portal:** the stock pages (controller, zero check, servo calibration and motion editor) on the robot's own Wi-Fi, restyled in the Blackfyre design system.
- **TrimUI remote:** MiniSpooder.pak joins the robot's network by itself and drives the robot with the TrimUI's controls. While a direction is held, the walking motion repeats.
- **Failsafe:** if the remote's frames stop, the robot stops and its servos go limp. While the remote has control, the phone portal can't move the robot.

Sister project: [spooder](https://github.com/blackfyre/spooder), an 18-servo hexapod with a gamepad bridge, the source of this project's toolchain and link design.

## Hardware

| Part | Detail |
|---|---|
| Board | ESP8266EX, 4 MB flash, CP2102 USB-to-UART adapter |
| Servos | 8 × MG90, servo indices 0–7 on GPIO 15, 13, 12, 14, 2, 4, 5, 16 (the stock firmware's start-up order) |
| Power | One 18650 cell for the servos; USB powers the logic when it is plugged in |
| Remote | TrimUI Smart Pro running NextUI |

The stock firmware is the reference for the hardware: anything it doesn't use, such as battery sensing, is treated as absent. [`baseline/README.md`](baseline/README.md) records what the stock image does, corrected against its disassembly.

## Quick start

You need [mise](https://mise.jdx.dev). For the pak, you also need rootless Podman with qemu-user-static.

```sh
mise install          # Python, PlatformIO, esptool, Go
mise run test         # host tests (no robot needed)
mise run backup       # read the robot's flash first: keep this image
mise run upload       # build, check and flash the firmware to $ROBOT_PORT
mise run handheld-package   # build MiniSpooder.pak.zip (arm64, under Podman)
```

`ROBOT_PORT` defaults to the robot's CP2102 `/dev/serial/by-id/` path. Set it in the shell for another adapter. `mise run restore` writes the original stock image back.

Then:

1. On a phone, join the Wi-Fi network `mini-spooder` with the password `87654321`.
2. Open `http://192.168.4.1/`.
3. To use the TrimUI, save `mini-spooder` once in NextUI's Wi-Fi settings.
4. Unzip the pak into `Tools/tg5040/` on the card.
5. Start **MiniSpooder** from Tools.

[docs/handheld.md](docs/handheld.md) has the full pak guide.

## Repository layout

| Path | Contents |
|---|---|
| `firmware/` | PlatformIO project (Arduino ESP8266 core): `robot`, `robot-check`, `robot-trace` and `motion-demo` builds, and the portal page sources |
| `core/` | Hardware-free C++: servo model and output gate, calibration, motion player, drive-link protocol and session, portal request handler and sender. Host-tested with Unity, with fuzz targets for the link decoder |
| `handheld/` | MiniSpooder.pak (Go, gabagool and SDL2), a fake robot, end-to-end tests, and the Wi-Fi probe pak |
| `tools/` | Motion extraction from the stock image, the drive-link probe, and the portal page generator and checks |
| `baseline/` | Verified flash dump of the stock firmware, with its analysis |
| `openspec/` | The specification: one OpenSpec change per part, each with proposal, design, specs and tasks |
| `docs/` | Guides and findings (below) |

## Documentation

- [Architecture](docs/architecture.md): how the firmware loop, the output gate, the motion player, the drive link, the portal and the pak fit together.
- [Field notes](docs/field-notes.md): what testing on the real robot and TrimUI turned up, and how each problem was diagnosed.
- [MiniSpooder.pak](docs/handheld.md): building, installing and using the remote.
- [Stock motions](docs/stock-motions.md): where the motion tables come from, and how they are checked against the stock image.
- [LAFVIN comparison](docs/lafvin-comparison.md) and [motion tables](docs/lafvin-motions.md): the stock image compared with the vendor's source download.

## Status

Tested on the robot: the portal, Standby and walking, driving from the TrimUI, stopping with B, leaving, and the portal lock. The other motions are host-tested against the stock tables but not yet watched on the robot. The robot checks still open in `openspec/changes/*/tasks.md` are the joint and direction map, the mechanical stops, calibration on the robot, comparing motions against stock, and timing the link-loss failsafe.
