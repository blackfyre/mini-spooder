"""Write the drive link fuzz seeds from the golden file (design D8).

Each golden line gives one seed per target, named golden-<name>:
- corpus/decoder/: the frame's bytes;
- corpus/session/: one datagram record holding the frame's bytes intact,
  [flags 0x80 (sender 0, run a loop pass)] [time step 0] [length] [bytes].

Run from the repository root: mise exec -- python -I core/fuzz/drive_link/make_seeds.py
"""

import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[3]
GOLDEN = ROOT / "core/test/golden/drive_link_v1.txt"
CORPUS = ROOT / "core/fuzz/drive_link/corpus"
MAX_RECORD = 40


def main() -> int:
    lines = [l.split() for l in GOLDEN.read_text().splitlines() if l.strip() and not l.startswith("#")]
    for target in ("decoder", "session"):
        d = CORPUS / target
        d.mkdir(parents=True, exist_ok=True)
        for old in d.glob("golden-*"):
            old.unlink()
    for fields in lines:
        name, raw = fields[0], bytes.fromhex(fields[1])
        if len(raw) > MAX_RECORD:
            print(f"{name}: {len(raw)} bytes exceed a record", file=sys.stderr)
            return 1
        (CORPUS / "decoder" / f"golden-{name}").write_bytes(raw)
        (CORPUS / "session" / f"golden-{name}").write_bytes(bytes([0x80, 0, len(raw)]) + raw)
    print(f"{len(lines)} golden lines, {2 * len(lines)} seeds")
    return 0


if __name__ == "__main__":
    sys.exit(main())
