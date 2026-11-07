# Proposal

## Why

The robot's value is its repertoire: the stock firmware plays fifteen motions and a zero position from the web portal, and the new firmware has to play them the same way before the portal or the handheld pak can drive anything. The stock motions exist only as data inside the stock flash image (`baseline/original-flash-4MB.bin`); the LAFVIN vendor source is a later, different version, so the stock image, not the vendor source, has to be the source of the motion data.

## What Changes

- Extract the keyframe tables of every stock motion (`pm` 1–15) and the zero pose (`pm` 100) from the stock image with a script that checks the image's hash, and generate a checked-in C++ table from them, with a check that fails when the generated table and the image disagree.
- Add a hardware-free keyframe format and motion player to `core/`: it reproduces the stock interpolation (10 ms ticks, the stock rounding, the stock step counts) and the stock compositions (Say Hi ends in Standby, Sleep starts with Standby), and is host-tested with Unity.
- Make playback non-blocking, which differs from stock: the firmware keeps serving the portal and the drive link while a motion plays, and a motion can be stopped, or replaced by a return to standby, at the next tick. Stock motions could not be interrupted. The drive link's safe state needs the change.
- Run the zero position at start-up, as stock does, and report the player's state (current motion, pending motion, last commanded angles) for the portal and the drive link's telemetry.
- Add an on-robot check: a demonstration build that plays every motion, logs its timing on the serial console and exercises stop and return to standby, plus a side-by-side comparison with the stock firmware.

## Capabilities

### New Capabilities

- `motion-playback`: the stock motion catalogue (ids 1–15 and 100), its keyframe data taken from the stock image, stock-faithful interpolation and timing, the zero-position routine, the standby pose, start-up pose, request queueing, stop and return to standby, and the player's reported state.

### Modified Capabilities

None. No specs exist yet. This change depends on `servo-output` and `servo-calibration`, which `firmware-foundation` owns, and references them by name only.

## Impact

- **New code:** `core/motion/` (keyframe types, the generated stock table, the player), host tests in the native test env, `tools/extract_motions.py`, and the firmware glue in `firmware/` that ticks the player and passes its angles to servo output.
- **mise:** two tasks, `extract-motions` (regenerate the table) and `check-motions` (fail when the table and the image differ). A PlatformIO env, `motion-demo`, for the on-robot check.
- **Docs:** `docs/stock-motions.md` records where each motion lives in the stock image and how the extraction was confirmed.
- **Consumers:** `web-portal` (the `pm` buttons and the Standby button) and `drive-link` (requested motion, stop, standby, telemetry) call the player. Neither is specified here.
- **Hardware:** the on-robot comparison with stock needs `mise run restore` and a firmware upload, both of which write flash and need the user's go-ahead.
