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

    def test_pace_out_of_range_with_preliminary_is_warning_exit_0(self):
        # Лид, ревью #11 п. 2: before E1-35 skirmish logic runs at ~38 Hz; preliminary runs are needed for FPS,
        # memory and heat -> with --preliminary a pace mismatch is a warning and exit 0; without the flag exit 2
        import contextlib
        import io
        import json
        with tempfile.TemporaryDirectory() as d:
            fr, js = Path(d) / "f.csv", Path(d) / "o.json"
            af.synthetic(fr, seed=1, seconds=360, jank_every=0, logic_hz=38.0)
            outbuf, errbuf = io.StringIO(), io.StringIO()
            with contextlib.redirect_stdout(outbuf), contextlib.redirect_stderr(errbuf):
                rc = af.main([str(fr), "--preliminary", "--json", str(js)])
            self.assertEqual(rc, 0)
            out = json.loads(js.read_text(encoding="utf-8"))
            self.assertIsNone(out["pace"]["valid"])
            self.assertIsNone(out["valid"])
            self.assertEqual(out["pace"]["reason"], "logic pace out of range")
            self.assertAlmostEqual(out["pace"]["logic_hz"], 38.0, delta=0.3)
            self.assertIn("warning: logic pace out of range", errbuf.getvalue())
            self.assertIn("ПРЕДВАРИТЕЛЬНО", outbuf.getvalue())
            self.assertNotIn("НЕДЕЙСТВИТЕЛЕН", outbuf.getvalue())
            with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
                self.assertEqual(af.main([str(fr)]), 2)                       # without the flag: invalid
                self.assertEqual(af.main([str(fr), "--logic-window", "1801-10800", "--preliminary"]), 0)

    def test_pace_out_of_range_with_preliminary_still_fails_thresholds(self):
        # the flag only softens the pace rule; FPS thresholds still give exit 1
        import contextlib
        import io
        with tempfile.TemporaryDirectory() as d:
            fr = Path(d) / "f.csv"
            af.synthetic(fr, seed=1, seconds=360, jank_every=30, logic_hz=38.0)
            with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
                self.assertEqual(af.main([str(fr), "--preliminary"]), 1)

    def test_logic_pace_function_preliminary(self):
        rows = [{"t_ns": i * 1e9 / 30, "logic_frame": round(i * 38 / 30)} for i in range(301)]
        self.assertFalse(af.logic_pace(rows)["valid"])
        self.assertFalse(af.logic_pace(rows, required=True)["valid"])
        soft = af.logic_pace(rows, required=True, preliminary=True)
        self.assertIsNone(soft["valid"])
        self.assertIn("warning", soft)

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

    # --- bad input: exit 4 with a clear message, never a traceback (Лид, ревью #11 п. 1) ---
    def _main_text(self, content: str, *extra: str) -> tuple[int, str]:
        import contextlib
        import io
        with tempfile.TemporaryDirectory() as d:
            fr = Path(d) / "f.csv"
            fr.write_text(content, encoding="utf-8")
            outbuf, errbuf = io.StringIO(), io.StringIO()
            with contextlib.redirect_stdout(outbuf), contextlib.redirect_stderr(errbuf):
                rc = af.main([str(fr), *extra])
            return rc, outbuf.getvalue() + errbuf.getvalue()

    def test_empty_file_is_bad_input(self):
        rc, text = self._main_text("")
        self.assertEqual(rc, 4)
        self.assertIn("empty file", text)

    def test_header_only_is_bad_input(self):
        rc, text = self._main_text("t_ns,present_interval_ms,logic_frame\n")
        self.assertEqual(rc, 4)
        self.assertIn("no data rows", text)

    def test_missing_column_in_header_is_bad_input(self):
        rc, text = self._main_text("t_ns,logic_frame\n0,0\n33333333,1\n")
        self.assertEqual(rc, 4)
        self.assertIn("present_interval_ms", text)

    def test_empty_t_ns_in_a_later_row_is_bad_input(self):
        # was KeyError in window() -> exit 1; every row is checked now, with the line number
        lines = ["t_ns,present_interval_ms,logic_frame"] + [f"{int(i * 1e9 / 30)},33.333,{i}" for i in range(100)]
        lines[51] = ",33.333,50"
        rc, text = self._main_text("\n".join(lines) + "\n", "--skip-s", "0")
        self.assertEqual(rc, 4)
        self.assertIn(":52: no numeric value in column(s) t_ns", text)
        rc, text = self._main_text("t_ns,present_interval_ms\n,33.3\n1,33.3\n", "--skip-s", "0")
        self.assertEqual(rc, 4)
        self.assertIn(":2:", text)

    def test_empty_logic_frame_in_some_row_is_bad_input(self):
        lines = ["t_ns,present_interval_ms,logic_frame"] + [f"{int(i * 1e9 / 30)},33.333,{i}" for i in range(100)]
        lines[10] = f"{int(9 * 1e9 / 30)},33.333,"
        rc, text = self._main_text("\n".join(lines) + "\n", "--logic-window", "0-50")
        self.assertEqual(rc, 4)
        self.assertIn("logic_frame", text)

    def test_zero_present_intervals_is_bad_input(self):
        # was ZeroDivisionError in metrics() -> exit 1
        lines = ["t_ns,present_interval_ms,logic_frame"] + [f"{int(i * 1e9 / 30)},0,{i}" for i in range(100)]
        rc, text = self._main_text("\n".join(lines) + "\n", "--skip-s", "0")
        self.assertEqual(rc, 4)
        self.assertIn("zero time interval", text)

    def test_identical_t_ns_is_bad_input(self):
        lines = ["t_ns,present_interval_ms,logic_frame"] + [f"5000,33.333,{i}" for i in range(100)]
        rc, text = self._main_text("\n".join(lines) + "\n", "--skip-s", "0", "--duration-s", "0")
        self.assertEqual(rc, 4)
        self.assertIn("same t_ns", text)
        rc, text = self._main_text("\n".join(lines) + "\n", "--logic-window", "0-99")
        self.assertEqual(rc, 4)

    def test_t_ns_going_backwards_is_bad_input(self):
        rc, text = self._main_text("t_ns,present_interval_ms\n0,33.3\n66666666,33.3\n33333333,33.3\n", "--skip-s", "0")
        self.assertEqual(rc, 4)
        self.assertIn("backwards", text)

    def test_time_gap_is_measured_not_a_crash(self):
        # 10 s pause in the middle (app in background): no traceback; the window and the logic pace are taken
        # over wall-clock time, gap included -> one freeze, FPS below 29.5, and the logic pace (3599 ticks in
        # 129.9 s = 27.7/s) is outside 30 +- 1, so the run is invalid (exit 2), not bad input
        lines, t = ["t_ns,present_interval_ms,logic_frame"], 0
        for i in range(30 * 120):
            iv = 10000.0 if i == 30 * 60 else 1000.0 / 30
            t += int(iv * 1e6)
            lines.append(f"{t},{iv:.3f},{i}")
        rc, text = self._main_text("\n".join(lines) + "\n", "--skip-s", "0", "--duration-s", "0")
        self.assertEqual(rc, 2, text)
        self.assertRegex(text, r"Фризы > 250 мс, шт \| 1 ")
        self.assertIn("Окно замера, с | 129.90", text)
        self.assertIn("темп логики 27.70 тик/с вне 30 ± 1", text)
        # the same file split at the gap is fine: before the pause everything passes
        rc, text = self._main_text("\n".join(lines[:30 * 60]) + "\n", "--skip-s", "0", "--duration-s", "0")
        self.assertEqual(rc, 0, text)

    def test_gap_leaves_window_empty_is_bad_input(self):
        rows = "t_ns,present_interval_ms\n0,33.3\n33333333,33.3\n" + f"{int(400e9)},33.3\n"
        rc, text = self._main_text(rows, "--skip-s", "60", "--duration-s", "300")
        self.assertEqual(rc, 4)
        self.assertIn("no frames in the measurement window", text)

    def test_invalid_message_tells_missing_column_from_too_few_rows(self):
        # message at the end (ревью #11 п. 1): "нет колонки" vs "меньше двух строк в окне"
        lines = ["t_ns,present_interval_ms"] + [f"{int(i * 1e9 / 30)},33.333" for i in range(30 * 10)]
        rc, text = self._main_text("\n".join(lines) + "\n", "--skip-s", "0", "--duration-s", "0")
        self.assertEqual(rc, 2)
        self.assertIn("logic pace not verifiable: нет колонки logic_frame", text)
        lines = ["t_ns,present_interval_ms,logic_frame"] + [f"{int(i * 1e9 / 30)},33.333,{i}" for i in range(30 * 10)]
        rc, text = self._main_text("\n".join(lines) + "\n", "--logic-window", "5-5")
        self.assertEqual(rc, 2)
        self.assertIn("logic pace not verifiable: в окне меньше двух строк с logic_frame", text)
        rows1 = [{"t_ns": 0.0, "logic_frame": 0.0}]
        self.assertEqual(af.logic_pace(rows1, required=True)["cause"], "too_few_rows")
        self.assertEqual(af.logic_pace([{"t_ns": 0.0}], required=True)["cause"], "column_missing")
        same = [{"t_ns": 0.0, "logic_frame": 0.0}, {"t_ns": 0.0, "logic_frame": 1.0}]
        self.assertEqual(af.logic_pace(same, required=True)["cause"], "zero_interval")

    def test_percentile(self):
        self.assertEqual(af.percentile([1.0, 2.0, 3.0, 4.0, 5.0], 50), 3.0)
        self.assertAlmostEqual(af.percentile([10.0, 20.0], 95), 19.5)


if __name__ == "__main__":
    unittest.main()
