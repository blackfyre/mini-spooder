#!/usr/bin/env python3
"""Writes drive_link_v1_stand_in.txt: golden frames for drive-link protocol
version 1, encoded here from the drive-link specification (openspec change
drive-link, requirements Frame format, Command frames and Telemetry frames)
independently of the pak's Go codec.

It stands in for drive-link's own golden file, core/test/golden/drive_link_v1.txt,
which core/'s codec writes and which does not exist yet. Once it does, the Go
tests read that file instead (golden_test.go) and this stand-in can go.

Run: python3 -I gen_stand_in.py > drive_link_v1_stand_in.txt
"""
import struct


def crc16(b):
    crc = 0xFFFF
    for v in b:
        crc ^= v << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


def frame(version, typ, seq, payload, length=None):
    body = bytes([version, typ, seq, len(payload) if length is None else length]) + payload
    return bytes([0xA5]) + body + struct.pack('<H', crc16(body))


def command(name, seq, motion, counter):
    b = frame(1, 1, seq, bytes([motion, counter]))
    print(f"{name} {b.hex()} version=1 type=command seq={seq} motion={motion} counter={counter}")


TEL = ['playing', 'pending', 'counter', 'result', 'commandSeq', 'ageMs'] + [f'angle{i}' for i in range(8)]
COUNTS = ['accepted', 'malformed', 'outOfSequence', 'otherSender', 'rateLimited', 'failsafes']


def telemetry(name, seq, bytes_fields, counts):
    payload = bytes(bytes_fields) + struct.pack('<6H', *counts)
    b = frame(1, 2, seq, payload)
    fields = ' '.join(f'{k}={v}' for k, v in zip(TEL + COUNTS, list(bytes_fields) + list(counts)))
    print(f"{name} {b.hex()} version=1 type=telemetry seq={seq} {fields}")


def rejected(name, b):
    print(f"{name} {b.hex()} rejected")


print("""# Drive link protocol version 1: golden frames (STAND-IN).
# Written from the drive-link specification by testdata/gen_stand_in.py, not
# from core/'s codec: drive-link's own file (core/test/golden/drive_link_v1.txt)
# does not exist yet. The pak's tests read that file in place once it does.
#
# One frame per line, fields separated by single spaces:
#   <name> <frame bytes as lowercase hex, no separators> <field>=<value> ...
# Values are decimal. The header fields (version, type, seq) come first, then
# the payload fields in payload order. A line whose only field is "rejected"
# is a frame a decoder must discard. Lines starting with # are comments.""")

command('command_idle_zero', 0, 0, 0)
command('command_forward', 17, 2, 5)
command('command_stop', 42, 0, 6)
command('command_zero_position', 3, 100, 9)
command('command_centre', 4, 99, 10)
command('command_extremes_seq_wrap', 255, 255, 255)
telemetry('telemetry_baseline_none', 0, [0, 0, 0, 0, 0, 255] + [255] * 8, [0, 0, 0, 0, 0, 0])
telemetry('telemetry_playing_accepted', 7, [2, 0, 5, 1, 40, 10, 70, 90, 90, 110, 110, 90, 90, 70], [1000, 3, 2, 1, 0, 1])
telemetry('telemetry_pending_accepted', 8, [11, 9, 7, 1, 41, 0, 45, 135, 90, 0, 180, 1, 2, 3], [258, 0, 0, 0, 0, 0])
telemetry('telemetry_stop_requested', 9, [0, 0, 8, 2, 42, 254, 70, 90, 90, 110, 110, 90, 90, 70], [1, 2, 3, 4, 5, 6])
telemetry('telemetry_rejected', 10, [0, 0, 9, 3, 43, 12, 70, 90, 90, 110, 110, 90, 90, 70], [0, 0, 0, 0, 0, 0])
telemetry('telemetry_extremes_seq_wrap', 255, [255, 255, 255, 3, 255, 255] + [255] * 8, [65535] * 6)
telemetry('telemetry_centre_playing', 128, [99, 100, 1, 1, 0, 1] + [0] * 8, [65534, 1, 32768, 0, 0, 0])

good = frame(1, 1, 5, bytes([2, 5]))
bad = bytearray(good); bad[-1] ^= 0xFF
rejected('reject_bad_crc_command', bytes(bad))
tgood = bytearray(frame(1, 2, 6, bytes(26)))
tgood[-2] ^= 0x01
rejected('reject_bad_crc_telemetry', bytes(tgood))
bad = bytearray(good); bad[6] ^= 0x01  # a corrupted payload byte
rejected('reject_corrupt_payload_command', bytes(bad))
rejected('reject_version_2_command', frame(2, 1, 5, bytes([2, 5])))
rejected('reject_version_0_telemetry', frame(0, 2, 5, bytes(26)))
rejected('reject_unknown_type_3', frame(1, 3, 5, bytes([2, 5])))
rejected('reject_unknown_type_0', frame(1, 0, 5, bytes([2, 5])))
rejected('reject_command_length_3', frame(1, 1, 5, bytes([2, 5, 0])))
rejected('reject_command_length_1', frame(1, 1, 5, bytes([2])))
rejected('reject_telemetry_length_25', frame(1, 2, 5, bytes(25)))
rejected('reject_telemetry_length_26_as_command', frame(1, 1, 5, bytes(26)))
