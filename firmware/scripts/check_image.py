#!/usr/bin/env python3
"""Check the built robot image against the stock flash layout (design D2).

Reads the image header with `esptool image-info` and the linker symbols with the
toolchain's `nm`, then fails, naming the mismatch, when:

- the header's flash size is not 4 MB, its mode not DIO or its frequency not 40 MHz;
- `_EEPROM_start` is not 0x405FB000 (the EEPROM sector at flash 0x3FB000);
- the filesystem range `_FS_start`.. `_FS_end` is not empty;
- the image, written from 0x000000, would reach 0x3FB000;
- a required symbol is missing;
- any symbol of the check console is present.

Usage: check_image.py <firmware.elf> <firmware.bin> [--nm <path>] [--esptool <cmd>]
The parsing and comparison are pure functions, tested by test_check_image.py.
"""

import argparse
import os
import re
import shlex
import subprocess
import sys

EXPECTED_FLASH_SIZE = "4MB"
EXPECTED_FLASH_MODE = "DIO"
EXPECTED_FLASH_FREQ = "40m"
EXPECTED_EEPROM_START = 0x405FB000
FLASH_MAP_BASE = 0x40200000
EEPROM_SECTOR = 0x3FB000
REQUIRED_SYMBOLS = ("_EEPROM_start", "_FS_start", "_FS_end")
# Demangled names of the check console (firmware/src/check_console.*) and of its
# parser (core/check/console_parse.*).
CONSOLE_SYMBOL = re.compile(r"(^|[^A-Za-z0-9_])(CheckConsole|ms::check::)")


def parse_image_info(text):
    """Returns {"size", "mode", "freq"} from `esptool image-info` output."""
    fields = {}
    patterns = {
        "size": r"^\s*Flash size:\s*(\S+)",
        "freq": r"^\s*Flash freq:\s*(\S+)",
        "mode": r"^\s*Flash mode:\s*(\S+)",
    }
    for key, pattern in patterns.items():
        m = re.search(pattern, text, re.MULTILINE)
        if m:
            fields[key] = m.group(1)
    return fields


def parse_nm(text):
    """Returns (symbols {name: address}, names [demangled name, ...]) from `nm -C`."""
    symbols = {}
    names = []
    for line in text.splitlines():
        parts = line.split(None, 2)
        if len(parts) == 3 and re.fullmatch(r"[0-9a-fA-F]+", parts[0]):
            name = parts[2].strip()
            symbols[name] = int(parts[0], 16)
            names.append(name)
        elif len(parts) == 2:  # undefined symbols: "U name"
            names.append(parts[1].strip())
    return symbols, names


def check_layout(header, symbols, names, image_size):
    """Returns a list of mismatch messages; empty when the image is acceptable."""
    errors = []
    expected = {"size": EXPECTED_FLASH_SIZE, "mode": EXPECTED_FLASH_MODE, "freq": EXPECTED_FLASH_FREQ}
    labels = {"size": "flash size", "mode": "flash mode", "freq": "flash frequency"}
    for key in ("size", "mode", "freq"):
        found = header.get(key)
        if found is None:
            errors.append("image header: %s not found in esptool image-info output" % labels[key])
        elif found.upper() != expected[key].upper():
            errors.append("image header: %s is %s, expected %s" % (labels[key], found, expected[key]))

    missing = [s for s in REQUIRED_SYMBOLS if s not in symbols]
    for s in missing:
        errors.append("linker symbol %s is missing" % s)

    eeprom = symbols.get("_EEPROM_start")
    if eeprom is not None and eeprom != EXPECTED_EEPROM_START:
        errors.append(
            "EEPROM sector at 0x%06X (_EEPROM_start 0x%08X), expected 0x%06X (0x%08X)"
            % (eeprom - FLASH_MAP_BASE, eeprom, EEPROM_SECTOR, EXPECTED_EEPROM_START)
        )

    fs_start = symbols.get("_FS_start")
    fs_end = symbols.get("_FS_end")
    if fs_start is not None and fs_end is not None and fs_end != fs_start:
        errors.append(
            "filesystem range 0x%06X-0x%06X is not empty (_FS_start 0x%08X, _FS_end 0x%08X)"
            % (fs_start - FLASH_MAP_BASE, fs_end - FLASH_MAP_BASE, fs_start, fs_end)
        )

    if image_size >= EEPROM_SECTOR:
        errors.append(
            "image is %d bytes and would reach 0x%06X (the EEPROM sector)" % (image_size, EEPROM_SECTOR)
        )

    console = sorted({n for n in names if CONSOLE_SYMBOL.search(n)})
    if console:
        errors.append("check console symbols in the image: %s" % ", ".join(console[:5]))
    return errors


def default_nm():
    core_dir = os.environ.get("PLATFORMIO_CORE_DIR", os.path.expanduser("~/.platformio"))
    return os.path.join(core_dir, "packages", "toolchain-xtensa", "bin", "xtensa-lx106-elf-nm")


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("elf")
    ap.add_argument("bin")
    ap.add_argument("--nm", default=default_nm())
    ap.add_argument("--esptool", default="esptool")
    args = ap.parse_args(argv)

    try:
        info = subprocess.run(
            shlex.split(args.esptool) + ["image-info", args.bin], capture_output=True, text=True, check=True
        ).stdout
        nm = subprocess.run([args.nm, "-C", args.elf], capture_output=True, text=True, check=True).stdout
    except (OSError, subprocess.CalledProcessError) as e:
        print("image check: FAILED: could not run a tool: %s" % e, file=sys.stderr)
        return 1

    header = parse_image_info(info)
    symbols, names = parse_nm(nm)
    size = os.path.getsize(args.bin)
    errors = check_layout(header, symbols, names, size)
    if errors:
        for e in errors:
            print("image check: FAILED: %s" % e, file=sys.stderr)
        return 1
    print(
        "image check: ok: %s %s %s, EEPROM sector 0x%06X, no filesystem, image %d bytes (< 0x%06X), no check console"
        % (header["size"], header["mode"], header["freq"], EEPROM_SECTOR, size, EEPROM_SECTOR)
    )
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
