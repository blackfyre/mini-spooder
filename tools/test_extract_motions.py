"""Tests of tools/extract_motions.py against the stock image (task 1.1, 1.2)."""

import io
import contextlib
import shutil
import tempfile
import unittest
from pathlib import Path

import extract_motions as em


def run(*args):
    err = io.StringIO()
    with contextlib.redirect_stderr(err), contextlib.redirect_stdout(io.StringIO()):
        code = em.main(list(args))
    return code, err.getvalue()


class ExtractMotions(unittest.TestCase):
    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp())

    def tearDown(self):
        shutil.rmtree(self.tmp)

    def test_check_passes_on_clean_checkout(self):
        self.assertEqual(run("--check")[0], 0)

    def test_generated_header_is_reproduced(self):
        out = self.tmp / "stock_motions.h"
        self.assertEqual(run("--header", str(out))[0], 0)
        self.assertEqual(out.read_text(), em.DEFAULT_HEADER.read_text())

    def test_keyframe_counts(self):
        data = em.extract(em.DEFAULT_IMAGE.read_bytes())
        counts = {pm: len(rows) for pm, _s, _n, _t, _c, rows in data["motions"]}
        self.assertEqual(counts, {1: 2, 2: 11, 3: 11, 4: 11, 5: 11, 6: 8, 7: 8, 8: 1, 9: 7, 10: 11,
                                  11: 11, 12: 2, 13: 10, 14: 9, 15: 10})
        self.assertEqual(data["centre"], [135, 45, 135, 45, 45, 135, 45, 135])
        self.assertEqual(data["zero"], [135, 45, 135, 45, 45, 135, 45, 135])

    def test_changed_image_byte_fails_without_writing(self):
        image = bytearray(em.DEFAULT_IMAGE.read_bytes())
        image[0x40DA0] ^= 0x01  # Standby keyframe 1, servo 0
        bad = self.tmp / "image.bin"
        bad.write_bytes(bytes(image))
        out = self.tmp / "out.h"
        code, err = run("--image", str(bad), "--header", str(out))
        self.assertNotEqual(code, 0)
        self.assertIn("SHA-256", err)
        self.assertFalse(out.exists())

    def test_wrong_image_check_fails_before_comparing(self):
        image = bytearray(em.DEFAULT_IMAGE.read_bytes())
        image[-1] ^= 0xFF
        bad = self.tmp / "image.bin"
        bad.write_bytes(bytes(image))
        code, err = run("--check", "--image", str(bad))
        self.assertNotEqual(code, 0)
        self.assertIn("SHA-256", err)
        self.assertNotIn("keyframe", err)

    def test_edited_angle_names_motion_and_keyframe(self):
        text = em.DEFAULT_HEADER.read_text()
        old = "    {{110, 90, 90, 70, 70, 90, 90, 110}, 500},  // 1"  # Lie keyframe 1
        self.assertIn(old, text)
        edited = self.tmp / "stock_motions.h"
        edited.write_text(text.replace(old, old.replace("{{110,", "{{111,")))
        code, err = run("--check", "--header", str(edited))
        self.assertNotEqual(code, 0)
        self.assertIn("pm 8 (Lie) keyframe 1", err)

    def test_edited_duration_names_motion_and_keyframe(self):
        text = em.DEFAULT_HEADER.read_text()
        lines = text.splitlines(keepends=True)
        i = next(n for n, l in enumerate(lines) if "constexpr Keyframe kForward" in l) + 3  # keyframe 3
        lines[i] = lines[i].replace("}, 150},", "}, 160},")
        edited = self.tmp / "stock_motions.h"
        edited.write_text("".join(lines))
        code, err = run("--check", "--header", str(edited))
        self.assertNotEqual(code, 0)
        self.assertIn("pm 2 (Forward) keyframe 3", err)

    def test_tiling_check(self):
        image = bytearray(em.DEFAULT_IMAGE.read_bytes())
        # Pretend pm 1 has three keyframes: the tables no longer tile the region.
        image[0x4AEBC] = 3
        with self.assertRaises(em.ExtractError):
            em.extract(bytes(image))

    def test_angle_range_check(self):
        image = bytearray(em.DEFAULT_IMAGE.read_bytes())
        image[0x40DA0] = 181
        with self.assertRaises(em.ExtractError):
            em.extract(bytes(image))


if __name__ == "__main__":
    unittest.main()
