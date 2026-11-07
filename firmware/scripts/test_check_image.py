"""Tests for check_image.py's parsing and comparison (task 2.2)."""

import unittest

import check_image as ci

IMAGE_INFO_OK = """esptool v5.2.0
Image size: 269344 bytes
Detected image type: ESP8266

ESP8266 Image Header
====================
Image version: 1
Entry point: 0x4010f480
Segments: 2
Flash size: 4MB
Flash freq: 40m
Flash mode: DIO
"""

NM_OK = """405fb000 A _EEPROM_start
405fb000 A _FS_end
405fb000 A _FS_start
40201010 T setup
40201020 T ms::OutputGate::enable(ms::Origin, ms::Pose const&)
         U memcpy
"""


def header(size="4MB", freq="40m", mode="DIO"):
    return IMAGE_INFO_OK.replace("Flash size: 4MB", "Flash size: " + size).replace(
        "Flash freq: 40m", "Flash freq: " + freq
    ).replace("Flash mode: DIO", "Flash mode: " + mode)


def check(info=IMAGE_INFO_OK, nm=NM_OK, size=300000):
    symbols, names = ci.parse_nm(nm)
    return ci.check_layout(ci.parse_image_info(info), symbols, names, size)


class ParseTest(unittest.TestCase):
    def test_image_info(self):
        self.assertEqual(ci.parse_image_info(IMAGE_INFO_OK), {"size": "4MB", "freq": "40m", "mode": "DIO"})

    def test_nm(self):
        symbols, names = ci.parse_nm(NM_OK)
        self.assertEqual(symbols["_EEPROM_start"], 0x405FB000)
        self.assertIn("ms::OutputGate::enable(ms::Origin, ms::Pose const&)", symbols)
        self.assertIn("memcpy", names)


class LayoutTest(unittest.TestCase):
    def test_matching_header_and_layout(self):
        self.assertEqual(check(), [])

    def test_flash_size_mismatch(self):
        errors = check(info=header(size="1MB"))
        self.assertEqual(len(errors), 1)
        self.assertIn("flash size is 1MB", errors[0])

    def test_flash_mode_mismatch(self):
        errors = check(info=header(mode="QIO"))
        self.assertEqual(len(errors), 1)
        self.assertIn("flash mode is QIO", errors[0])

    def test_flash_frequency_mismatch(self):
        errors = check(info=header(freq="80m"))
        self.assertEqual(len(errors), 1)
        self.assertIn("flash frequency is 80m", errors[0])

    def test_header_field_missing(self):
        errors = check(info=IMAGE_INFO_OK.replace("Flash mode: DIO\n", ""))
        self.assertEqual(len(errors), 1)
        self.assertIn("flash mode not found", errors[0])

    def test_eeprom_moved(self):
        nm = NM_OK.replace("405fb000 A _EEPROM_start", "402fb000 A _EEPROM_start")
        errors = check(nm=nm)
        self.assertEqual(len(errors), 1)
        self.assertIn("EEPROM sector at 0x0FB000", errors[0])
        self.assertIn("0x402FB000", errors[0])

    def test_filesystem_not_empty(self):
        nm = NM_OK.replace("405fb000 A _FS_start", "404fb000 A _FS_start")
        errors = check(nm=nm)
        self.assertEqual(len(errors), 1)
        self.assertIn("filesystem range 0x2FB000-0x3FB000", errors[0])

    def test_image_reaching_eeprom_sector(self):
        self.assertEqual(check(size=0x3FB000 - 1), [])
        errors = check(size=0x3FB000)
        self.assertEqual(len(errors), 1)
        self.assertIn("would reach 0x3FB000", errors[0])

    def test_missing_symbol(self):
        for sym in ("_EEPROM_start", "_FS_start", "_FS_end"):
            nm = "\n".join(l for l in NM_OK.splitlines() if not l.endswith(" " + sym))
            errors = check(nm=nm)
            self.assertEqual(errors, ["linker symbol %s is missing" % sym], sym)

    def test_console_symbol_present(self):
        for line in (
            "40202000 T CheckConsole::poll(unsigned int)",
            "40202000 T ms::check::parseLine(char const*, unsigned int, ms::check::Command&)",
        ):
            errors = check(nm=NM_OK + line + "\n")
            self.assertEqual(len(errors), 1, line)
            self.assertIn("check console symbols", errors[0])

    def test_similar_names_are_not_console(self):
        nm = NM_OK + "40202000 T MyCheckConsoleLike\n40202010 T ms::checker()\n"
        self.assertEqual(check(nm=nm), [])


if __name__ == "__main__":
    unittest.main()
