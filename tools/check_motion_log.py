#!/usr/bin/env python3
"""Check a captured motion-demo log (motion-playback D9, task 4.2).

Requires the demo sequence in order: `complete` runs of motions 1-15, 99 and
100; an `interrupted` Forward whose `command stop` is 500 +/- 10 ms after its
start; a `complete` Dance 1 whose `command request 1` is 1000 +/- 10 ms after
its start; a `complete` Standby of 40 ticks that starts within 10 ms of Dance 1's
end; then a keyframe of 50 ticks. Each `complete` run's tick count must equal the
generated tables' (output is already active from the boot pose, so the first
motion has no enable tick of its own) and its duration
must be within 5% of design.md's figure (10 ms per table tick). The interrupted
Forward must have fewer than 165 ticks and no write after the stop. The 2 s
pauses must last 2000-2100 ms and the keyframe 500 ms within 5%. The logged
maximum service time (`demo done tick_max_us <n>`) must be under 2000 us.

Usage: check_motion_log.py <log> [--header core/motion/stock_motions.h]
Exit status 0 when the log passes, 1 otherwise (each failure is named).
"""

import argparse
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import extract_motions  # noqa: E402

SEQUENCE = list(range(1, 16)) + [99, 100]
DIRECT_POSE_TICKS = 38
PAUSE_MS = 2000
PAUSE_SLACK_MS = 100
MOTION_RE = re.compile(r"^motion (\d+) start (\d+) last_write (\d+) end (\d+) ticks (\d+) (complete|interrupted)\s*$")
COMMAND_RE = re.compile(r"^command (stop|request (\d+)) (\d+)\s*$")
KEYFRAME_RE = re.compile(r"^keyframe start (\d+) end (\d+) ticks (\d+)\s*$")
DONE_RE = re.compile(r"^demo done tick_max_us (\d+)\s*$")


def table_ticks(header_text):
    """Ticks per motion id (1-15, 99, 100) from the generated header."""
    tables, _poses = extract_motions.parse_header(header_text)
    own = {}
    for pm, sym, _name, _table, _count in extract_motions.MOTIONS:
        own[pm] = sum(max(1, ms // 10) for _angles, ms in tables[sym])
    ticks = dict(own)
    ticks[9] = own[9] + own[1]   # Say Hi, then Standby
    ticks[12] = own[1] + own[12]  # Standby, then Sleep
    ticks[99] = ticks[100] = DIRECT_POSE_TICKS
    return ticks


def parse(text):
    runs, commands, keyframes, done = [], [], [], None
    for raw in text.splitlines():
        line = raw.strip()
        m = MOTION_RE.match(line)
        if m:
            runs.append({"id": int(m.group(1)), "start": int(m.group(2)), "last_write": int(m.group(3)),
                         "end": int(m.group(4)), "ticks": int(m.group(5)), "status": m.group(6)})
            continue
        m = COMMAND_RE.match(line)
        if m:
            commands.append({"what": "stop" if m.group(1) == "stop" else "request", "id": int(m.group(2) or 0),
                             "t": int(m.group(3))})
            continue
        m = KEYFRAME_RE.match(line)
        if m:
            keyframes.append({"start": int(m.group(1)), "end": int(m.group(2)), "ticks": int(m.group(3))})
            continue
        m = DONE_RE.match(line)
        if m:
            done = int(m.group(1))
    return runs, commands, keyframes, done


def check(text, ticks):
    """Returns a list of failures (empty when the log passes)."""
    runs, commands, keyframes, done = parse(text)
    fails = []
    want = [(i, "complete") for i in SEQUENCE] + [(2, "interrupted"), (13, "complete"), (1, "complete")]
    got = [(r["id"], r["status"]) for r in runs]
    if got != want:
        fails.append(f"motion runs {got} are not the demo sequence {want}")
        return fails
    for n, r in enumerate(runs):
        if r["status"] != "complete":
            continue
        expect = ticks[r["id"]]
        if r["ticks"] != expect:
            fails.append(f"run {n + 1} (motion {r['id']}): {r['ticks']} ticks, expected {expect}")
        figure = 10 * ticks[r["id"]]
        duration = r["end"] - r["start"]
        if abs(duration - figure) > 0.05 * figure:
            fails.append(f"run {n + 1} (motion {r['id']}): {duration} ms, not within 5% of {figure} ms")
    # The 2 s pauses: after each catalogue run, after the stopped Forward and
    # after the Standby that follows Dance 1 (before the keyframe).
    starts = [r["start"] for r in runs[1:19]] + ([keyframes[0]["start"]] if len(keyframes) == 1 else [])
    ends = [r["end"] for r in runs[0:18]] + ([runs[19]["end"]] if len(keyframes) == 1 else [])
    for n, (end, start) in enumerate(zip(ends, starts)):
        if not PAUSE_MS <= start - end <= PAUSE_MS + PAUSE_SLACK_MS:
            fails.append(f"pause {n + 1}: {start - end} ms between a run's end and the next start, expected {PAUSE_MS}-{PAUSE_MS + PAUSE_SLACK_MS}")
    fwd, dance, standby = runs[17], runs[18], runs[19]
    stops = [c for c in commands if c["what"] == "stop"]
    if len(stops) != 1:
        fails.append(f"{len(stops)} stop commands, expected 1")
    else:
        stop = stops[0]["t"]
        if abs(stop - fwd["start"] - 500) > 10:
            fails.append(f"stop at {stop} is {stop - fwd['start']} ms after Forward's start, expected 500 +/- 10")
        if fwd["last_write"] > stop:
            fails.append(f"interrupted Forward wrote at {fwd['last_write']}, after the stop at {stop}")
    if fwd["ticks"] >= 165:
        fails.append(f"interrupted Forward ran {fwd['ticks']} ticks, expected fewer than 165")
    reqs = [c for c in commands if c["what"] == "request"]
    if len(reqs) != 1 or reqs[0]["id"] != 1:
        fails.append(f"request commands {reqs}, expected one `command request 1`")
    elif abs(reqs[0]["t"] - dance["start"] - 1000) > 10:
        fails.append(f"request 1 at {reqs[0]['t']} is {reqs[0]['t'] - dance['start']} ms after Dance 1's start, expected 1000 +/- 10")
    if standby["ticks"] != 40:
        fails.append(f"Standby after Dance 1 ran {standby['ticks']} ticks, expected 40")
    if abs(standby["start"] - dance["end"]) > 10:
        fails.append(f"Standby starts at {standby['start']}, not within 10 ms of Dance 1's end {dance['end']}")
    if len(keyframes) != 1:
        fails.append(f"{len(keyframes)} keyframe lines, expected 1")
    else:
        kf = keyframes[0]
        if kf["ticks"] != 50:
            fails.append(f"keyframe ran {kf['ticks']} ticks, expected 50")
        if abs(kf["end"] - kf["start"] - 500) > 0.05 * 500:
            fails.append(f"keyframe took {kf['end'] - kf['start']} ms, not within 5% of 500 ms")
        if kf["start"] < standby["end"]:
            fails.append("the keyframe starts before the Standby after Dance 1 ends")
    if done is None:
        fails.append("no `demo done tick_max_us` line")
    elif done >= 2000:
        fails.append(f"maximum service time {done} us, expected under 2000")
    return fails


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("log", type=Path)
    ap.add_argument("--header", type=Path, default=extract_motions.DEFAULT_HEADER)
    args = ap.parse_args(argv)
    try:
        text = args.log.read_text(encoding="utf-8", errors="replace")
        ticks = table_ticks(args.header.read_text(encoding="utf-8"))
    except (OSError, KeyError) as e:
        print(f"check_motion_log: {e}", file=sys.stderr)
        return 1
    fails = check(text, ticks)
    for f in fails:
        print(f"check_motion_log: {f}", file=sys.stderr)
    if fails:
        return 1
    print("check_motion_log: the log passes")
    return 0


if __name__ == "__main__":
    sys.exit(main())
