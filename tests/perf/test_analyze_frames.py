# Self-test of analyze_frames.py on synthetic data: python3 -m unittest -v test_analyze_frames
import tempfile
import unittest
from pathlib import Path

import analyze_frames as af


class AnalyzeFramesTest(unittest.TestCase):
    def run_case(self, jank_every: int, **kw) -> tuple[int, dict]:
        with tempfile.TemporaryDirectory() as d:
            csv_path, js = Path(d) / "frames.csv", Path(d) / "out.json"
            af.synthetic(csv_path, seed=1, seconds=360, jank_every=jank_every, **kw)
            rc = af.main([str(csv_path), "--json", str(js)])
            import json
            return rc, json.loads(js.read_text(encoding="utf-8"))

    def test_smooth_run_passes(self):
        rc, out = self.run_case(0)
        self.assertEqual(rc, 0)
        self.assertTrue(out["pass"])
        self.assertAlmostEqual(out["metrics"]["fps_avg"], 30.0, delta=0.2)
        self.assertGreater(out["metrics"]["frames"], 8900)  # 300 s window after 60 s warm-up

    def test_jank_fails(self):
        rc, out = self.run_case(30)  # one 66 ms frame per second = 3.3 % jank > 2 %
        self.assertEqual(rc, 1)
        self.assertGreater(out["metrics"]["jank_pct"], 2.0)

    def test_thermal_start_moderate_is_invalid(self):
        rc, out = self.run_case(0, thermal_start=2)
        self.assertEqual(rc, 2)
        self.assertFalse(out["valid"])
        self.assertEqual(out["thermal"]["thermal_start"], "MODERATE")

    def test_thermal_rising_during_run_stays_valid(self):
        rc, out = self.run_case(0, thermal_start=1, thermal_end=3)
        self.assertEqual(rc, 0)
        self.assertTrue(out["valid"])
        self.assertEqual((out["thermal"]["thermal_start"], out["thermal"]["thermal_end"]), ("LIGHT", "SEVERE"))

    def test_no_thermal_column_is_unverified(self):
        rc, out = self.run_case(0)
        self.assertEqual(rc, 0)
        self.assertIsNone(out["valid"])

    def test_thermal_from_samples_csv(self):
        with tempfile.TemporaryDirectory() as d:
            fr, sm, js = Path(d) / "f.csv", Path(d) / "s.csv", Path(d) / "o.json"
            af.synthetic(fr, seed=1, seconds=360, jank_every=0)
            sm.write_text("t_ns,thermal_status\n0,3\n100000000000,3\n", encoding="utf-8")
            self.assertEqual(af.main([str(fr), "--samples", str(sm), "--json", str(js)]), 2)

    def test_cpu_risk_is_not_failure(self):
        rc, out = self.run_case(0, cpu_ms=31.0)   # p95 cpu ~36 ms > 30 ms, frame pacing still fine
        self.assertEqual(rc, 0)
        self.assertTrue(any("cpu_frame_ms" in r for r in out["risks"]))
        self.assertTrue(any("logic_ms" in r for r in out["risks"]))  # logic = 0.4*cpu ~ 14 ms > 10 ms

    def test_logic_pace_30hz_is_valid(self):
        rc, out = self.run_case(0)
        self.assertEqual(rc, 0)
        self.assertTrue(out["pace"]["valid"])
        self.assertAlmostEqual(out["pace"]["logic_hz"], 30.0, delta=0.2)

    def test_logic_pace_38hz_is_invalid(self):
        # 2.22 skirmish before the E1-35 pace fix: logic ticks every render frame, ~38 Hz
        rc, out = self.run_case(0, logic_hz=38.0)
        self.assertEqual(rc, 2)
        self.assertFalse(out["pace"]["valid"])
        self.assertFalse(out["valid"])
        self.assertTrue(out["pass"])  # FPS thresholds alone would pass: the pace rule must catch it

    def test_logic_pace_edges(self):
        def rows(hz, n=301):
            return [{"t_ns": i * 1e9 / 30, "logic_frame": round(i * hz / 30)} for i in range(n)]
        self.assertTrue(af.logic_pace(rows(31.0))["valid"])
        self.assertTrue(af.logic_pace(rows(29.0))["valid"])
        self.assertFalse(af.logic_pace(rows(31.5))["valid"])
        self.assertFalse(af.logic_pace(rows(28.4))["valid"])

    def _no_logic_frame(self, *extra: str) -> tuple[int, dict, str]:
        import contextlib
        import io
        import json
        with tempfile.TemporaryDirectory() as d:
            fr, js = Path(d) / "f.csv", Path(d) / "o.json"
            lines = ["t_ns,present_interval_ms"] + [f"{int(i * 1e9 / 30)},33.333" for i in range(30 * 360)]
            fr.write_text("\n".join(lines) + "\n", encoding="utf-8")
            outbuf, errbuf = io.StringIO(), io.StringIO()
            with contextlib.redirect_stdout(outbuf), contextlib.redirect_stderr(errbuf):
                rc = af.main([str(fr), "--json", str(js), *extra])
            return rc, json.loads(js.read_text(encoding="utf-8")), outbuf.getvalue() + errbuf.getvalue()

    def test_no_logic_frame_column_makes_perf_run_invalid(self):
        # TEST-PLAN §5.4.1: without logic_frame the pace cannot be checked -> run invalid (exit 2)
        rc, out, text = self._no_logic_frame()
        self.assertEqual(rc, 2)
        self.assertFalse(out["pace"]["valid"])
        self.assertEqual(out["pace"]["reason"], "logic pace not verifiable")
        self.assertFalse(out["valid"])
        self.assertTrue(out["pass"])            # FPS thresholds alone would pass
        self.assertIn("logic pace not verifiable", text)

    def test_no_logic_frame_invalid_for_every_pace_run_kind(self):
        for kind in ("perf", "load", "soak", "crc-cost"):
            rc, out, _ = self._no_logic_frame("--run-kind", kind)
            self.assertEqual(rc, 2, kind)
            self.assertEqual(out["run_kind"], kind)

    def test_no_logic_frame_preliminary_is_warning_only(self):
        rc, out, text = self._no_logic_frame("--preliminary")
        self.assertEqual(rc, 0)
        self.assertIsNone(out["pace"]["valid"])
        self.assertEqual(out["pace"]["reason"], "logic pace not verifiable")
        self.assertIsNone(out["valid"])
        self.assertTrue(out["preliminary"])
        self.assertIn("warning: logic pace not verifiable", text)

    def test_no_logic_frame_other_run_kind_is_unverified(self):
        rc, out, _ = self._no_logic_frame("--run-kind", "other")
        self.assertEqual(rc, 0)
        self.assertIsNone(out["pace"]["valid"])
        self.assertNotIn("reason", out["pace"])

    def test_logic_pace_not_verifiable_function(self):
        rows = [{"t_ns": i * 1e9 / 30} for i in range(100)]
        self.assertFalse(af.logic_pace(rows, required=True)["valid"])
        self.assertIsNone(af.logic_pace(rows, required=True, preliminary=True)["valid"])
        self.assertIsNone(af.logic_pace(rows)["valid"])
        one = rows[:1] + [{"t_ns": 1e9, "logic_frame": 30}] + rows[1:]   # a single row with logic_frame
        self.assertEqual(af.logic_pace(one[1:2], required=True)["reason"], "logic pace not verifiable")

    def test_pace_out_of_range_with_preliminary_is_still_invalid(self):
        # --preliminary only softens "not verifiable"; a measured 38 Hz is invalid in any case
        with tempfile.TemporaryDirectory() as d:
            fr = Path(d) / "f.csv"
            af.synthetic(fr, seed=1, seconds=360, jank_every=0, logic_hz=38.0)
            self.assertEqual(af.main([str(fr), "--preliminary"]), 2)

    def test_thermal_and_pace_both_invalid(self):
        rc, out = self.run_case(0, thermal_start=3, logic_hz=38.0)
        self.assertEqual(rc, 2)
        self.assertFalse(out["thermal"]["valid"])
        self.assertFalse(out["pace"]["valid"])

    def test_logic_window(self):
        with tempfile.TemporaryDirectory() as d:
            fr, js = Path(d) / "f.csv", Path(d) / "o.json"
            af.synthetic(fr, seed=1, seconds=370, jank_every=0)
            rc = af.main([str(fr), "--logic-window", "1801-10800", "--preliminary", "--json", str(js)])
            import json
            out = json.loads(js.read_text(encoding="utf-8"))
            self.assertEqual(rc, 0)
            self.assertEqual(out["metrics"]["frames"], 9000)  # 30 Hz: one logic tick per render frame
            self.assertTrue(out["preliminary"])
            self.assertTrue(out["pace"]["valid"])

    def test_logic_window_38hz_covers_less_wall_time_and_is_invalid(self):
        with tempfile.TemporaryDirectory() as d:
            fr, js = Path(d) / "f.csv", Path(d) / "o.json"
            af.synthetic(fr, seed=1, seconds=370, jank_every=0, logic_hz=38.0)
            rc = af.main([str(fr), "--logic-window", "1801-10800", "--json", str(js)])
            import json
            out = json.loads(js.read_text(encoding="utf-8"))
            self.assertEqual(rc, 2)
            self.assertAlmostEqual(out["metrics"]["window_s"], 9000 / 38, delta=1.0)

    def test_bad_logic_window_is_bad_input(self):
        with tempfile.TemporaryDirectory() as d:
            fr = Path(d) / "f.csv"
            af.synthetic(fr, seed=1, seconds=10, jank_every=0)
            self.assertEqual(af.main([str(fr), "--logic-window", "500-100"]), 4)
            self.assertEqual(af.main([str(fr), "--logic-window", "abc"]), 4)

    def test_percentile(self):
        self.assertEqual(af.percentile([1.0, 2.0, 3.0, 4.0, 5.0], 50), 3.0)
        self.assertAlmostEqual(af.percentile([10.0, 20.0], 95), 19.5)


if __name__ == "__main__":
    unittest.main()
