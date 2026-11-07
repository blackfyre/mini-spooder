# Proposal

## Why

The robot's value is its repertoire: the stock firmware plays fifteen motions, a centre and a zero position from the web portal, and its `/online` route plays single keyframes (no stock page sends it). The new firmware has to play them before the portal or the handheld pak can drive anything. The stock motions exist only as data inside the stock flash image (`baseline/original-flash-4MB.bin`); the LAFVIN vendor source is a different version, so the stock image, not the vendor source, has to be the source of the motion data.

## What Changes

- Extract the keyframe tables of every stock motion (`pm` 1–15), the centre pose (`pm` 99) and the zero pose (`pm` 100) from the stock image with a script that checks the image's hash, and generate a checked-in C++ table from them, with a check that fails when the generated table and the image disagree.
- Add a hardware-free keyframe format and motion player to `core/`: stock tick counts (10 ms ticks, `floor(T / 10)` per keyframe) and the stock compositions (Say Hi ends in Standby, Sleep starts with Standby), host-tested with Unity. Stock defects are not reproduced: each keyframe ends exactly on its angles, and the player takes its start pose from servo output's commanded angles, so direct servo moves and motions share one pose.
- Make playback non-blocking, which differs from stock: the firmware keeps serving the drive link's failsafe and the portal while a motion plays, and a motion can be stopped at the next tick. Each request plays once, as on stock; a request made during a motion waits in a single pending slot, latest wins. Ticks advance by the interval, so loop jitter does not accumulate.
- Add a single-keyframe call (eight angles and a duration) with an identifier and an "ended" query, for the `/online` route, queued like a motion request.
- Keep the robot still after a reset (stock ran the zero routine at boot): the player moves nothing until its first command, then sends servo output's enable request with the complete zero pose, and ends its motion when servo output enters its safe state or refuses a write. Whether output may be enabled again after a safe state is `firmware-foundation`'s rule.
- Report the player's state (what plays, current motion or keyframe, pending motion or keyframe) for the portal and the drive link's telemetry; the angles come from servo output's own report.
- Add an on-robot check: a demonstration build that plays every motion with the legs off the ground, logs its timing on the serial console and exercises stop, a request during a motion and a keyframe, plus a side-by-side comparison with the stock firmware.

## Capabilities

### New Capabilities

- `motion-playback`: the stock motion catalogue (ids 1–15, 99 and 100), its keyframe data taken from the stock image, interpolation with exact keyframe endpoints and stock tick counts, deadline-based timing, the direct-pose routine, the standby pose, stillness until the first command, the output-enable handshake and safe-state handling, single keyframes, request queueing and play-once, stop, and the player's reported state.

### Modified Capabilities

None. No specs exist yet. This change depends on `servo-output` and `servo-calibration`, which `firmware-foundation` owns, and references them by name only.

## Impact

- **New code:** `core/motion/` (keyframe types, the generated stock table, the player), host tests in the native test env, `tools/extract_motions.py`, and the firmware glue in `firmware/` that ticks the player and passes its angles to servo output.
- **mise:** two tasks, `extract-motions` (regenerate the table) and `check-motions` (fail when the table and the image differ). A PlatformIO env, `motion-demo`, for the on-robot check.
- **Docs:** `docs/stock-motions.md` records where each motion lives in the stock image and the on-robot results.
- **Consumers:** `web-portal` (the `pm` buttons, the Standby button, `/online`) and `drive-link` (requested motion, stop, telemetry) call the player; `handheld-pak`'s hold-to-repeat and its leave sequence rely on the play-once rule and the state report. None of them is specified here.
- **Hardware:** the on-robot comparison with stock needs `mise run restore` and a firmware upload, both of which write flash and need the user's go-ahead. The motion checks run only after `firmware-foundation`'s joint check and `drive-link`'s failsafe check have passed.
