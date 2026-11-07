"""Tests of tools/check_motion_log.py on a log made by the host player (task 4.2)."""

import os
import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

import check_motion_log as cml
import extract_motions as em

REPO = Path(__file__).resolve().parent.parent
SOURCES = [
    "core/fuzz/motion_demo_log/main.cpp",
    "core/motion/catalogue.cpp",
    "core/motion/player.cpp",
    "core/motion/gate_output.cpp",
    "core/servo/output_gate.cpp",
    "core/servo/pulse.cpp",
    "core/servo/servo_bank.cpp",
]


def host_log(tmp):
    exe = tmp / "motion_demo_log"
    cxx = os.environ.get("CXX", "c++")
    subprocess.run([cxx, "-std=gnu++17", "-Wall", "-Wextra", "-Werror", "-I", str(REPO / "core"),
                    *[str(REPO / s) for s in SOURCES], "-o", str(exe)], check=True)
    return subprocess.run([str(exe)], check=True, capture_output=True, text=True).stdout


class CheckMotionLog(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = Path(tempfile.mkdtemp())
        cls.log = host_log(cls.tmp)
        cls.ticks = cml.table_ticks(em.DEFAULT_HEADER.read_text())

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.tmp)

    def fails(self, text):
        return cml.check(text, self.ticks)

    def test_host_log_passes(self):
        self.assertEqual(self.fails(self.log), [])

    def test_altered_ticks_fail(self):
        text = re.sub(r"^(motion 6 .* ticks )120 complete$", r"\g<1>119 complete", self.log, flags=re.M)
        self.assertNotEqual(text, self.log)
        self.assertTrue(any("motion 6" in f and "ticks" in f for f in self.fails(text)))

    def test_missing_motion_fails(self):
        text = re.sub(r"^motion 11 .*\n", "", self.log, flags=re.M)
        self.assertNotEqual(text, self.log)
        self.assertTrue(any("demo sequence" in f for f in self.fails(text)))

    def test_late_stop_fails(self):
        fwd = re.search(r"^motion 2 start (\d+) .* interrupted$", self.log, flags=re.M)
        start = int(fwd.group(1))
        text = re.sub(r"^command stop \d+$", f"command stop {start + 600}", self.log, flags=re.M)
        self.assertTrue(any("500 +/- 10" in f for f in self.fails(text)))

    def test_slow_service_fails(self):
        text = re.sub(r"tick_max_us \d+", "tick_max_us 2500", self.log)
        self.assertTrue(any("2000" in f for f in self.fails(text)))

    def test_stretched_duration_fails(self):
        m = re.search(r"^motion 11 start (\d+) last_write (\d+) end (\d+)", self.log, flags=re.M)
        start, end = int(m.group(1)), int(m.group(3))
        text = self.log.replace(m.group(0), m.group(0).replace(f"end {end}", f"end {start + 7600}"))
        self.assertTrue(any("5%" in f for f in self.fails(text)))

    def test_compressed_keyframe_fails(self):
        m = re.search(r"^keyframe start (\d+) end (\d+) ticks 50$", self.log, flags=re.M)
        text = self.log.replace(m.group(0), f"keyframe start {m.group(1)} end {m.group(1)} ticks 50")
        self.assertTrue(any("keyframe took" in f for f in self.fails(text)))

    def test_missing_pause_fails(self):
        # Motion 3 starting 10 ms after motion 2's end, both shifted consistently.
        m2 = re.search(r"^motion 2 start \d+ last_write \d+ end (\d+) .*complete$", self.log, flags=re.M)
        m3 = re.search(r"^motion 3 start (\d+) last_write (\d+) end (\d+) (.*)$", self.log, flags=re.M)
        shift = int(m3.group(1)) - (int(m2.group(1)) + 10)
        line = (f"motion 3 start {int(m3.group(1)) - shift} last_write {int(m3.group(2)) - shift} "
                f"end {int(m3.group(3)) - shift} {m3.group(4)}")
        text = self.log.replace(m3.group(0), line)
        self.assertTrue(any("pause 2" in f for f in self.fails(text)))


if __name__ == "__main__":
    unittest.main()
