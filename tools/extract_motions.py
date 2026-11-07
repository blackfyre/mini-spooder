#!/usr/bin/env python3
"""Extract the stock motion tables from the stock flash image (motion-playback D1).

Checks the SHA-256 of baseline/original-flash-4MB.bin against baseline/README.md,
reads the 15 step counts, the 15 keyframe tables, the centre pose and the zero
pose at fixed offsets, checks them, and writes core/motion/stock_motions.h.

Usage:
  extract_motions.py                 write the header
  extract_motions.py --check         re-extract in memory and compare with the
                                     checked-in header; name the motion and
                                     keyframe that differ
Options: --image PATH, --readme PATH, --header PATH.

Exit status: 0 on success, 1 on any failed check (nothing is written then).
Run with mise's Python: `mise exec -- python -I tools/extract_motions.py`.
"""

import argparse
import hashlib
import re
import struct
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
DEFAULT_IMAGE = REPO / "baseline" / "original-flash-4MB.bin"
DEFAULT_README = REPO / "baseline" / "README.md"
DEFAULT_HEADER = REPO / "core" / "motion" / "stock_motions.h"

# Flash offset x maps to the instruction-ROM address 0x40200000 + x.
FLASH_MAP_BASE = 0x40200000
# The step counts are 32-bit integers at RAM 0x3FFE84CC (pm 15) .. 0x3FFE8504
# (pm 1), initialised from flash 0x4AE84.
COUNT_RAM_BASE = 0x3FFE84CC
COUNT_FLASH_BASE = 0x4AE84

# The motion dispatch (flash 0x5084-0x51E3) loads one table address and one
# step-count address per pm value from the literal pairs at flash 0x500C-0x5083
# (pm 1 first). The offsets below are those literals, fixed here, not searched
# for; the script also checks that the literals still say the same.
DISPATCH_PAIRS = 0x500C
# (pm, symbol, name, table flash offset, step-count flash offset)
MOTIONS = [
    (1, "kStandby", "Standby", 0x40DA0, 0x4AEBC),
    (2, "kForward", "Forward", 0x40C14, 0x4AEB8),
    (3, "kBackward", "Backward", 0x40A88, 0x4AEB4),
    (4, "kLeftShift", "Left shift", 0x408FC, 0x4AEB0),
    (5, "kRightShift", "Right shift", 0x40770, 0x4AEAC),
    (6, "kTurnLeft", "Turn left", 0x40650, 0x4AEA8),
    (7, "kTurnRight", "Turn right", 0x40530, 0x4AEA4),
    (8, "kLie", "Lie", 0x4050C, 0x4AEA0),
    (9, "kSayHi", "Say Hi", 0x40410, 0x4AE9C),
    (10, "kFighting", "Fighting", 0x40284, 0x4AE98),
    (11, "kPushUp", "Push up", 0x400F8, 0x4AE94),
    (12, "kSleep", "Sleep", 0x400B0, 0x4AE90),
    (13, "kDance1", "Dance 1", 0x3FF48, 0x4AE8C),
    (14, "kDance2", "Dance 2", 0x3FE04, 0x4AE88),
    (15, "kDance3", "Dance 3", 0x3FC9C, 0x4AE84),
]
# The 15 tables tile this region exactly (the end is exclusive).
TABLE_REGION = (0x3FC9C, 0x40DE8)
# The direct poses of pm 99 (dispatch 0x5106-0x5115, writer 0x1380) and pm 100
# (dispatch 0x51C4-0x51D3, writer 0x13D0); their ninth integer (200) is ignored.
CENTRE_POSE = 0x40DE8
ZERO_POSE = 0x40E0C
ROW_BYTES = 36


class ExtractError(Exception):
    pass


def recorded_hash(readme_text):
    """The SHA-256 that baseline/README.md records for the image."""
    m = re.search(r"SHA-256 of `original-flash-4MB\.bin`:\s*```\s*([0-9a-f]{64})\s*```", readme_text)
    if not m:
        raise ExtractError("baseline/README.md records no SHA-256 for original-flash-4MB.bin")
    return m.group(1)


def check_hash(image, readme_text):
    want = recorded_hash(readme_text)
    got = hashlib.sha256(image).hexdigest()
    if got != want:
        raise ExtractError(f"image SHA-256 {got} differs from the recorded {want}")


def read_row(image, off):
    vals = struct.unpack_from("<9i", image, off)
    return list(vals[:8]), vals[8]


def extract(image):
    """Returns {"motions": [(pm, symbol, name, table, countoff, [(angles, ms)])],
    "centre": angles, "zero": angles}. Raises ExtractError on a failed check."""
    for i, (pm, _sym, name, table, count_off) in enumerate(MOTIONS):
        lit_table, lit_count = struct.unpack_from("<2I", image, DISPATCH_PAIRS + 8 * i)
        if lit_table != FLASH_MAP_BASE + table:
            raise ExtractError(f"pm {pm} ({name}): dispatch table literal 0x{lit_table:08X} is not 0x{FLASH_MAP_BASE + table:08X}")
        if lit_count != COUNT_RAM_BASE + (count_off - COUNT_FLASH_BASE):
            raise ExtractError(f"pm {pm} ({name}): dispatch step-count literal 0x{lit_count:08X} does not match flash 0x{count_off:X}")
    motions = []
    spans = []
    for pm, sym, name, table, count_off in MOTIONS:
        (count,) = struct.unpack_from("<i", image, count_off)
        if count <= 0 or count > 255:
            raise ExtractError(f"pm {pm} ({name}): step count {count} out of range")
        rows = []
        for k in range(count):
            angles, ms = read_row(image, table + ROW_BYTES * k)
            for s, a in enumerate(angles):
                if not 0 <= a <= 180:
                    raise ExtractError(f"pm {pm} ({name}) keyframe {k + 1}: servo {s} angle {a} outside 0-180")
            if ms <= 0 or ms > 65535:
                raise ExtractError(f"pm {pm} ({name}) keyframe {k + 1}: duration {ms} not positive or too long")
            rows.append((angles, ms))
        spans.append((table, table + ROW_BYTES * count, pm))
        motions.append((pm, sym, name, table, count_off, rows))
    spans.sort()
    pos = TABLE_REGION[0]
    for start, end, pm in spans:
        if start != pos:
            raise ExtractError(f"tables do not tile 0x{TABLE_REGION[0]:X}-0x{TABLE_REGION[1] - 1:X}: pm {pm} starts at 0x{start:X}, expected 0x{pos:X}")
        pos = end
    if pos != TABLE_REGION[1]:
        raise ExtractError(f"tables end at 0x{pos:X}, expected 0x{TABLE_REGION[1]:X}")
    poses = {}
    for key, off in (("centre", CENTRE_POSE), ("zero", ZERO_POSE)):
        angles, _ignored = read_row(image, off)
        for s, a in enumerate(angles):
            if not 0 <= a <= 180:
                raise ExtractError(f"{key} pose: servo {s} angle {a} outside 0-180")
        poses[key] = angles
    return {"motions": motions, "centre": poses["centre"], "zero": poses["zero"]}


def render(data, sha):
    out = []
    w = out.append
    w("// GENERATED by tools/extract_motions.py from baseline/original-flash-4MB.bin")
    w(f"// (SHA-256 {sha}). Do not edit by hand:")
    w("// run `mise run extract-motions`; `mise run check-motions` compares this file")
    w("// with the image. Angles are commanded angles for servos 0-7, before calibration.")
    w("#pragma once")
    w("")
    w('#include "motion/keyframe.h"')
    w("")
    w("namespace ms {")
    w("namespace stock {")
    for pm, sym, name, table, count_off, rows in data["motions"]:
        w("")
        w(f"// pm {pm} {name}: table flash 0x{table:05X}, step count flash 0x{count_off:05X}, {len(rows)} keyframes")
        w(f"inline constexpr Keyframe {sym}[] = {{")
        for k, (angles, ms) in enumerate(rows):
            w(f"    {{{{{', '.join(str(a) for a in angles)}}}, {ms}}},  // {k + 1}")
        w("};")
    w("")
    w(f"// pm 99 Centre: flash 0x{CENTRE_POSE:05X}")
    w(f"inline constexpr uint8_t kCentrePose[8] = {{{', '.join(str(a) for a in data['centre'])}}};")
    w(f"// pm 100 Zero position: flash 0x{ZERO_POSE:05X}")
    w(f"inline constexpr uint8_t kZeroPose[8] = {{{', '.join(str(a) for a in data['zero'])}}};")
    w("")
    w("}  // namespace stock")
    w("}  // namespace ms")
    return "\n".join(out) + "\n"


ROW_RE = re.compile(r"^\s*\{\{([0-9,\s]+)\},\s*(\d+)\},\s*//\s*(\d+)\s*$")
TABLE_RE = re.compile(r"^inline constexpr Keyframe (k\w+)\[\] = \{$")
POSE_RE = re.compile(r"^inline constexpr uint8_t (kCentrePose|kZeroPose)\[8\] = \{([0-9,\s]+)\};$")


def parse_header(text):
    """Returns ({symbol: [(angles, ms)]}, {"kCentrePose": angles, "kZeroPose": angles})."""
    tables = {}
    poses = {}
    current = None
    for line in text.splitlines():
        m = TABLE_RE.match(line)
        if m:
            current = m.group(1)
            tables[current] = []
            continue
        if line.strip() == "};":
            current = None
            continue
        m = ROW_RE.match(line)
        if m and current is not None:
            tables[current].append(([int(v) for v in m.group(1).split(",")], int(m.group(2))))
            continue
        m = POSE_RE.match(line)
        if m:
            poses[m.group(1)] = [int(v) for v in m.group(2).split(",")]
    return tables, poses


def compare(data, header_text):
    """Returns a list of differences between the extracted data and a header."""
    tables, poses = parse_header(header_text)
    diffs = []
    for pm, sym, name, _t, _c, rows in data["motions"]:
        got = tables.get(sym)
        if got is None:
            diffs.append(f"pm {pm} ({name}): table {sym} missing from the header")
            continue
        if len(got) != len(rows):
            diffs.append(f"pm {pm} ({name}): header has {len(got)} keyframes, image has {len(rows)}")
        for k in range(min(len(got), len(rows))):
            if got[k] != rows[k]:
                diffs.append(f"pm {pm} ({name}) keyframe {k + 1}: header {got[k][0]} {got[k][1]} ms, image {rows[k][0]} {rows[k][1]} ms")
    for pm, key, sym in ((99, "centre", "kCentrePose"), (100, "zero", "kZeroPose")):
        if poses.get(sym) != data[key]:
            diffs.append(f"pm {pm} ({key} pose): header {poses.get(sym)}, image {data[key]}")
    return diffs


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--image", type=Path, default=DEFAULT_IMAGE)
    ap.add_argument("--readme", type=Path, default=DEFAULT_README)
    ap.add_argument("--header", type=Path, default=DEFAULT_HEADER)
    ap.add_argument("--check", action="store_true", help="compare with the checked-in header; write nothing")
    args = ap.parse_args(argv)
    try:
        image = args.image.read_bytes()
        readme = args.readme.read_text(encoding="utf-8")
        check_hash(image, readme)
        data = extract(image)
    except (OSError, ExtractError, struct.error) as e:
        print(f"extract_motions: {e}", file=sys.stderr)
        return 1
    text = render(data, recorded_hash(readme))
    if args.check:
        try:
            current = args.header.read_text(encoding="utf-8")
        except OSError as e:
            print(f"check-motions: {e}", file=sys.stderr)
            return 1
        diffs = compare(data, current)
        if not diffs and current != text:
            diffs.append(f"{args.header}: values agree but the text differs from the generator's output; run `mise run extract-motions`")
        if diffs:
            for d in diffs:
                print(f"check-motions: {d}", file=sys.stderr)
            return 1
        print(f"check-motions: {args.header.name} agrees with the image")
        return 0
    args.header.parent.mkdir(parents=True, exist_ok=True)
    args.header.write_text(text, encoding="utf-8")
    print(f"extract_motions: wrote {args.header}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
