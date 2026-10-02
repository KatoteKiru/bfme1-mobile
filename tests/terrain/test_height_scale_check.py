# python3 -m unittest -v test_height_scale_check   (E1-25 fallback check, no ZH data)
import random
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

import height_scale_check as h

HERE = Path(__file__).resolve().parent


class HeightScaleTest(unittest.TestCase):
    def test_all_256_values_bit_exact(self):
        self.assertEqual(h.check_load(), [])

    def test_bilinear_bit_exact(self):
        self.assertEqual(h.check_interp(random.Random(7), 3000), 0)

    def test_flatten_4db8b77_matches_upstream(self):
        # bfme 4db8b77 (PR #4): ZH step, then * 16 -> bit-identical to upstream on ZH maps
        r = h.check_flatten(random.Random(7), 2000)
        self.assertEqual(r["differ_4db8b77"], 0, r["examples"])
        # the pre-fix rounding with the new step would have changed the terrain (why the fix exists)
        self.assertGreater(r["differ_naive_new_step"], 0)

    def test_seismic_4db8b77_differs_only_at_clamp(self):
        r = h.check_seismic(random.Random(7), 20000)
        self.assertTrue(r["only_at_255_clamp"], r["examples"])
        # reproducible example: client h8 250, logic 200, speed +10.5 -> upstream 255 (clamped), 4db8b77 260
        self.assertEqual(h.seismic_old(250, 200, 10.5) * 16, 4080)
        self.assertEqual(h.seismic_4db8b77(250 << 4, 200 << 4, 10.5), 4160)
        self.assertEqual(h.seismic_4db8b77(100 << 4, 50 << 4, 7.25), h.seismic_old(100, 50, 7.25) * 16)

    def test_crater_4db8b77_matches_upstream(self):
        r = h.check_crater(random.Random(7), 20000)
        self.assertEqual(r["differ_4db8b77"], 0, r["examples"])

    def test_f32_emulation(self):
        self.assertEqual(h.bits(h.mul(0.1, 3.0)), 0x3E99999A)  # 0.1f*3.0f in IEEE float32
        self.assertEqual(h.bits(h.add(0.1, 0.2)), 0x3E99999A)

    @unittest.skipUnless(shutil.which("g++"), "no g++")
    def test_cpp_hardware(self):
        with tempfile.TemporaryDirectory() as d:
            exe = Path(d) / "hsc"
            subprocess.run(["g++", "-std=c++17", "-O2", "-ffp-contract=off", "-fno-strict-aliasing", "-fwrapv",
                            "-o", str(exe), str(HERE / "height_scale_check.cpp")], check=True)
            self.assertEqual(subprocess.run([str(exe)], capture_output=True).returncode, 0)


if __name__ == "__main__":
    unittest.main()
