#!/usr/bin/env python3
"""Desktop probe for the drive link (drive-link, design D9).

Sends drive link protocol version 1 command frames every 50 ms to the robot and
prints every telemetry frame it gets back, with the interval between telemetry
frames and the round-trip time from the echoed sequence number and its age.
Standard library only. A test tool, not a second remote.

  mise exec -- python tools/drive-probe.py --request 1@1000 --pause 3000:4000
  mise exec -- python tools/drive-probe.py --self-test

Times are milliseconds from the probe's start. The first frame goes out 350 ms
after the start (more than the robot's 300 ms liveness plus one period).
"""

import argparse
import binascii
import pathlib
import random
import select
import socket
import struct
import sys
import time

START = 0xA5
VERSION = 1
TYPE_COMMAND = 1
TYPE_TELEMETRY = 2
COMMAND_PAYLOAD = 2
TELEMETRY_PAYLOAD = 26
COMMAND_PERIOD_S = 0.050
FIRST_FRAME_S = 0.350
RESULTS = {0: "none", 1: "accepted", 2: "stop requested", 3: "rejected"}
TELEMETRY_FIELDS = (
    ["playing", "pending", "counter", "result", "commandSeq", "ageMs"]
    + [f"angle{i}" for i in range(8)]
    + ["accepted", "malformed", "outOfSequence", "otherSender", "rateLimited", "failsafes"]
)
TELEMETRY_STRUCT = struct.Struct("<6B8B6H")
GOLDEN = pathlib.Path(__file__).resolve().parents[1] / "core/test/golden/drive_link_v1.txt"


def crc16(data: bytes) -> int:
    """CRC-16/CCITT-FALSE: polynomial 0x1021, initial value 0xFFFF."""
    return binascii.crc_hqx(data, 0xFFFF)


def frame(ftype: int, seq: int, payload: bytes) -> bytes:
    body = bytes([VERSION, ftype, seq & 0xFF, len(payload)]) + payload
    return bytes([START]) + body + struct.pack("<H", crc16(body))


def encode_command(seq: int, motion: int, counter: int) -> bytes:
    return frame(TYPE_COMMAND, seq, bytes([motion & 0xFF, counter & 0xFF]))


def encode_telemetry(seq: int, fields: dict) -> bytes:
    return frame(TYPE_TELEMETRY, seq, TELEMETRY_STRUCT.pack(*(fields[k] for k in TELEMETRY_FIELDS)))


def decode(data: bytes):
    """One whole frame, or None: (type, seq, fields)."""
    if len(data) < 7 or data[0] != START or data[1] != VERSION:
        return None
    ftype, seq, n = data[2], data[3], data[4]
    want = {TYPE_COMMAND: COMMAND_PAYLOAD, TYPE_TELEMETRY: TELEMETRY_PAYLOAD}.get(ftype)
    if want is None or n != want or len(data) != 7 + n:
        return None
    if crc16(data[1 : 5 + n]) != struct.unpack_from("<H", data, 5 + n)[0]:
        return None
    payload = data[5 : 5 + n]
    if ftype == TYPE_COMMAND:
        return ftype, seq, {"motion": payload[0], "counter": payload[1]}
    return ftype, seq, dict(zip(TELEMETRY_FIELDS, TELEMETRY_STRUCT.unpack(payload)))


def self_test() -> int:
    lines = [l.split() for l in GOLDEN.read_text().splitlines() if l.strip() and not l.startswith("#")]
    failures = 0
    for f in lines:
        name, raw = f[0], bytes.fromhex(f[1])
        if f[2:] == ["rejected"]:
            ok = decode(raw) is None
        else:
            kv = dict(x.split("=", 1) for x in f[2:])
            ftype = TYPE_COMMAND if kv["type"] == "command" else TYPE_TELEMETRY
            seq = int(kv["seq"])
            fields = {k: int(v) for k, v in kv.items() if k not in ("version", "type", "seq")}
            if ftype == TYPE_COMMAND:
                enc = encode_command(seq, fields["motion"], fields["counter"])
            else:
                enc = encode_telemetry(seq, fields)
            ok = int(kv["version"]) == VERSION and enc == raw and decode(raw) == (ftype, seq, fields)
        print(f"{'ok  ' if ok else 'FAIL'} {name}")
        failures += not ok
    print(f"self-test: {len(lines) - failures}/{len(lines)} golden lines")
    return 1 if failures or not lines else 0


def parse_args(argv):
    # --stop@<ms> is one token; turn it into --stop-at <ms> for argparse.
    fixed = []
    for a in argv:
        if a.startswith("--stop@"):
            fixed += ["--stop-at", a[len("--stop@") :]]
        else:
            fixed.append(a)
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--addr", default="192.168.4.1:52041", help="robot address (default 192.168.4.1:52041)")
    p.add_argument("--request", action="append", default=[], metavar="MOTION@MS", help="request a motion at a time")
    p.add_argument("--stop-at", action="append", default=[], type=int, metavar="MS", help="written --stop@<ms>")
    p.add_argument("--pause", action="append", default=[], metavar="FROM:TO", help="send no frames in [FROM, TO)")
    p.add_argument("--flood", type=float, default=0, metavar="N", help="extra datagrams per second (copies)")
    p.add_argument("--corrupt", type=float, default=0, metavar="FRACTION", help="share of frames sent corrupted")
    p.add_argument("--sender-port", type=int, default=0, help="local UDP port (default: any)")
    p.add_argument("--duration", type=float, default=6.0, help="seconds to run (default 6)")
    p.add_argument("--self-test", action="store_true", help="check the codec against the golden file")
    args = p.parse_args(fixed)
    events = []
    for r in args.request:
        motion, _, at = r.partition("@")
        events.append((int(at), int(motion)))
    events += [(int(at), 0) for at in args.stop_at]
    args.events = sorted(events)
    args.pauses = []
    for r in args.pause:
        a, _, b = r.partition(":")
        args.pauses.append((int(a), int(b)))
    host, _, port = args.addr.rpartition(":")
    args.target = (host, int(port))
    return args


def run(args) -> int:
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind(("", args.sender_port))
    sock.setblocking(False)
    print(f"probe: from port {sock.getsockname()[1]} to {args.target[0]}:{args.target[1]}")

    t0 = time.monotonic()
    ms = lambda: (time.monotonic() - t0) * 1000.0  # noqa: E731
    seq = random.randrange(256)
    motion, counter = 0, random.randrange(256)
    pending_events = list(args.events)
    sent_at = {}  # seq -> send time (ms)
    next_send = FIRST_FRAME_S * 1000.0
    flood_interval = 1000.0 / args.flood if args.flood > 0 else None
    next_flood = next_send
    last_frame = None
    last_telemetry = None
    end = args.duration * 1000.0

    while ms() < end:
        now = ms()
        if now >= next_send:
            while pending_events and pending_events[0][0] <= now:
                _, motion = pending_events.pop(0)
                counter = (counter + 1) & 0xFF
                print(f"{now:8.0f} ms  request {'stop' if motion == 0 else motion} (counter {counter})")
            paused = any(a <= now < b for a, b in args.pauses)
            if not paused:
                seq = (seq + 1) & 0xFF
                data = encode_command(seq, motion, counter)
                last_frame = data
                if args.corrupt and random.random() < args.corrupt:
                    data = data[:-1] + bytes([data[-1] ^ 0xFF])
                sent_at[seq] = now
                sock.sendto(data, args.target)
            next_send += COMMAND_PERIOD_S * 1000.0
            if next_send < now:
                next_send = now + COMMAND_PERIOD_S * 1000.0
        if flood_interval and last_frame and now >= next_flood:
            if not any(a <= now < b for a, b in args.pauses):
                try:
                    sock.sendto(last_frame, args.target)
                except BlockingIOError:
                    pass
            next_flood += flood_interval
            if next_flood < now - 100:
                next_flood = now
        wait = max(0.0, (min(next_send, next_flood if flood_interval else next_send) - ms()) / 1000.0)
        ready, _, _ = select.select([sock], [], [], min(wait, 0.01))
        while ready:
            try:
                data, _ = sock.recvfrom(64)
            except BlockingIOError:
                break
            got = ms()
            d = decode(data)
            if d is None or d[0] != TYPE_TELEMETRY:
                print(f"{got:8.0f} ms  undecodable datagram {data.hex()}")
                continue
            _, tseq, f = d
            interval = f"{got - last_telemetry:5.0f}" if last_telemetry is not None else "    -"
            last_telemetry = got
            rtt = "-"
            if f["ageMs"] < 255 and f["commandSeq"] in sent_at:
                rtt = f"{got - sent_at[f['commandSeq']] - f['ageMs']:.0f} ms"
            angles = " ".join(f"{f[f'angle{i}']:3d}" for i in range(8))
            print(
                f"{got:8.0f} ms  telemetry seq {tseq:3d} interval {interval} ms rtt {rtt}  "
                f"playing {f['playing']} pending {f['pending']} counter {f['counter']} "
                f"result {RESULTS.get(f['result'], f['result'])} echo {f['commandSeq']} age {f['ageMs']}  "
                f"angles {angles}  accepted {f['accepted']} malformed {f['malformed']} "
                f"outOfSequence {f['outOfSequence']} otherSender {f['otherSender']} "
                f"rateLimited {f['rateLimited']} failsafes {f['failsafes']}"
            )
    return 0


def main(argv) -> int:
    args = parse_args(argv)
    if args.self_test:
        return self_test()
    return run(args)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
