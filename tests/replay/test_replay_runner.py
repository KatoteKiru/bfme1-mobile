#!/usr/bin/env python3
# Self-tests for the replay CRC runner (no engine, no game data): python3 -m unittest -v
# (or, from the repo root, all unit tests under tests/: tools/replay-test selftest)
import contextlib
import io
import json
import os
import struct
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import crclog  # noqa: E402
import run_replay_tests as rrt  # noqa: E402


def fake_case(name="selftest-match", **env):
    _, cases = rrt.load_manifest(HERE / "manifest.toml", None)
    c = next(c for c in cases if c.name == name)
    c.env = dict(c.env, **{k: str(v) for k, v in env.items()})
    return c


class FormatTests(unittest.TestCase):
    def test_roundtrip(self):
        log = crclog.CRCLog(meta={"replay": "x.rep"})
        log.add(0, 0x1234ABCD, {"Objects": 1, "AI": 2})
        log.add(1, 0xDEADBEEF)
        log.end = {"frames": "2", "status": "ok"}
        again = crclog.parse_crclog(crclog.format_crclog(log))
        self.assertEqual(again.frames[0].crc, 0x1234ABCD)
        self.assertEqual(again.frames[0].parts, {"Objects": 1, "AI": 2})
        self.assertEqual(again.end["status"], "ok")

    def test_gx_stderr(self):
        text = ("[GX-NET] crc parts frame 7: afterObjects=00000001 seed=00000009\n"
                "[GX-NET] crc parts frame 7: afterAI=00000004  (objects counted: 3)\n"
                "noise\n[GX-NET] crc every frame 7: 0000ABCD\n")
        log = crclog.parse_gx_stderr(text)
        self.assertEqual(log.frames[7].crc, 0xABCD)
        self.assertEqual(log.frames[7].parts["AI"], 4)

    def test_first_section(self):
        a, b = crclog.CRCLog(), crclog.CRCLog()
        a.add(0, 1); b.add(0, 1)
        a.add(1, 2, {"Objects": 5, "Partition": 6, "AI": 7})
        b.add(1, 3, {"Objects": 5, "Partition": 9, "AI": 8})
        d = crclog.compare(a, b).divergence
        self.assertEqual((d.frame, d.first_section, d.last_match), (1, "Partition", 0))

    def test_rep_recorded_crcs(self):
        # minimal synthetic replay: GENREP + junk header + "S=" + command records
        hdr = b"GENREP" + b"\0" * 40 + b"S=X;;\0" + b"0\0"
        recs = b""
        for frame in range(0, 700, 100):
            recs += struct.pack("<iiiB", frame, 1001, 0, 1) + bytes([0, 1]) + struct.pack("<i", 5)  # some command
            recs += struct.pack("<iiiB", frame + 1, crclog.MSG_LOGIC_CRC, 0, 2) + bytes([0, 1, 2, 1]) \
                + struct.pack("<I", 0xC0DE0000 + frame) + b"\x01"
        log = crclog.rep_recorded_crcs(hdr + recs)          # default offset 1: label 101 -> frame 100
        self.assertEqual(log.frames[100].crc, 0xC0DE0000 + 100)
        self.assertEqual(len(log.frames), 7)
        self.assertEqual(log.meta["frame_offset"], "1")
        raw = crclog.rep_recorded_crcs(hdr + recs, frame_offset=0)
        self.assertEqual(sorted(raw.frames)[:2], [1, 101])

    def test_logic_hz_header_roundtrip(self):
        log = crclog.CRCLog(meta={"replay": "x.rep", "logic_hz": "30"})
        log.add(0, 1)
        text = crclog.format_crclog(log)
        self.assertEqual(text.splitlines()[1], "# logic_hz=30")   # right after the format tag
        self.assertEqual(crclog.parse_crclog(text).logic_hz, 30)
        self.assertIsNone(crclog.CRCLog().logic_hz)

    def test_logic_hz_mismatch_is_divergence(self):
        a = crclog.CRCLog(meta={"logic_hz": "30"}); a.add(0, 1)
        b = crclog.CRCLog(meta={"logic_hz": "5"}); b.add(0, 1)
        self.assertEqual(crclog.compare(a, b).divergence.kind, "logic_hz")
        b.meta.pop("logic_hz")
        self.assertTrue(crclog.compare(a, b).ok)   # unknown rate on one side: compare frames

    def test_shifted(self):
        a = crclog.CRCLog(); a.add(1, 7, {"AI": 3})
        self.assertEqual(crclog.shifted(a, -1).frames[0].parts, {"AI": 3})


class RunnerTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.out = Path(self.tmp.name)

    def tearDown(self):
        self.tmp.cleanup()

    def test_pass(self):
        r = rrt.run_case(fake_case(), "linux-x86_64", self.out)
        self.assertEqual(r.status, "pass", r.message)

    def test_divergence_reports_first_frame_and_section(self):
        r = rrt.run_case(fake_case(FAKE_DIVERGE_AT=432), "linux-x86_64", self.out)
        self.assertEqual(r.status, "mismatch")
        self.assertEqual(r.divergence["frame"], 432)
        self.assertEqual(r.divergence["last_match"], 431)
        self.assertEqual(r.divergence["first_section"], "AI")

    def test_crash_is_engine_error(self):
        r = rrt.run_case(fake_case(FAKE_CRASH_AT=100), "linux-x86_64", self.out)
        self.assertEqual(r.status, "engine_error")
        self.assertIn("139", r.message)
        self.assertEqual(r.compared, 100)

    def test_timeout(self):
        c = fake_case(FAKE_HANG_AT=5)
        c.timeout_s = 2
        r = rrt.run_case(c, "linux-x86_64", self.out)
        self.assertEqual(r.status, "timeout")

    def test_short_run_is_mismatch(self):
        r = rrt.run_case(fake_case(FAKE_FRAMES=500), "linux-x86_64", self.out)
        self.assertEqual(r.status, "mismatch")
        self.assertEqual(r.divergence["kind"], "missing_in_actual")
        self.assertEqual(r.divergence["frame"], 500)

    def test_missing_golden(self):
        r = rrt.run_case(fake_case(), "no-such-arch", self.out)
        self.assertEqual(r.status, "no_golden")

    def test_bless_refuses_nondeterministic_engine(self):
        # second run gets another seed -> runs disagree -> bless must refuse
        calls = {"n": 0}
        real = rrt.run_engine

        def flaky(case, out_dir, *rest):
            calls["n"] += 1
            case.env = dict(case.env, FAKE_SEED=str(calls["n"]))
            return real(case, out_dir, *rest)
        with mock.patch.object(rrt, "run_engine", flaky), \
                mock.patch.object(rrt, "golden_path", lambda arch, case: self.out / "g.crclog"):
            rc = rrt.main(["--out", str(self.out), "bless", "--case", "selftest-match", "--reason", "t"])
        self.assertEqual(rc, rrt.EXIT_MISMATCH)
        self.assertFalse((self.out / "g.crclog").exists())

    def test_replay_without_rep_extension_is_config_error(self):
        c = fake_case()
        c.replay = "fixtures/selftest-a.txt"
        with self.assertRaises(ValueError):
            rrt.run_case(c, "linux-x86_64", self.out)

    def test_replay_path_passed_absolute(self):
        rrt.run_case(fake_case(), "linux-x86_64", self.out)
        cmdline = (self.out / "selftest-match.engine.log").read_text().splitlines()[0]
        self.assertIn(str((HERE / "fixtures/selftest-a.rep").resolve()), cmdline)

    def test_crclog_mode_per_worker_file_and_logic_hz(self):
        r = rrt.run_case(fake_case("selftest-crclog-mode"), "linux-x86_64", self.out)
        self.assertEqual(r.status, "pass", r.message)
        log = crclog.parse_crclog((self.out / "selftest-crclog-mode.actual.crclog").read_text())
        self.assertEqual(log.logic_hz, 30)

    def test_crclog_logic_hz_disagrees_with_manifest(self):
        with self.assertRaises(ValueError):
            rrt.run_case(fake_case("selftest-crclog-mode", FAKE_LOGIC_HZ=5), "linux-x86_64", self.out)

    def test_two_worker_files_means_jobs_gt_1(self):
        req = self.out / "c.actual.crclog"
        for pid in (11, 12):
            (self.out / f"c.actual.x.{pid}.crclog").write_text("# bfme-crclog/1\n")
        with self.assertRaises(ValueError) as e:
            rrt.find_crclog(req)
        self.assertIn("-jobs 1", str(e.exception))

    def test_gx_stderr_gets_logic_hz_from_manifest(self):
        rrt.run_case(fake_case(), "linux-x86_64", self.out)
        log = crclog.parse_crclog((self.out / "selftest-match.actual.crclog").read_text())
        self.assertEqual(log.logic_hz, 30)

    def _rep_from_fake_run(self, label_shift: int) -> Path:
        """A synthetic .rep whose MSG_LOGIC_CRC records hold the fake engine's CRC of frame f
        under label f+label_shift (every 100 frames), like the ZH replays (1, 101, 201...)."""
        rep = self.out / "selftest-rec.rep"
        rep.write_bytes(b"GENREP")
        c = fake_case()
        c.replay = str(rep)
        log, _ = rrt.run_engine(c, self.out / "probe")
        recs = b""
        for f in range(0, 900, 100):
            recs += struct.pack("<iiiB", f + label_shift, crclog.MSG_LOGIC_CRC, 0, 2) + bytes([0, 1, 2, 1]) \
                + struct.pack("<I", log.frames[f].crc) + b"\x01"
        rep.write_bytes(b"GENREP" + b"\0" * 40 + b"S=X;;\0" + b"0\0" + recs)
        return rep

    def test_check_against_recorded_crcs_with_offset(self):
        rep = self._rep_from_fake_run(label_shift=1)
        c = fake_case()
        c.replay, c.check = str(rep), "rep-recorded"
        c.rep_crc_offset = 1
        r = rrt.run_case(c, "linux-x86_64", self.out)
        self.assertEqual(r.status, "pass", r.message)
        self.assertEqual(r.compared, 9)
        c.rep_crc_offset = 0          # without the shift every checkpoint is off by one frame
        r = rrt.run_case(c, "linux-x86_64", self.out)
        self.assertEqual(r.status, "mismatch")

    def test_cli_exit_codes(self):
        rc = rrt.main(["--out", str(self.out), "run", "--stage", "selftest",
                       "--junit", str(self.out / "j.xml"), "--summary", str(self.out / "s.md")])
        self.assertEqual(rc, rrt.EXIT_OK)
        self.assertIn('failures="0"', (self.out / "j.xml").read_text())
        with mock.patch.dict(os.environ, {"FAKE_DIVERGE_AT": "10"}):
            rc = rrt.main(["--out", str(self.out), "run", "--case", "selftest-match"])
        self.assertEqual(rc, rrt.EXIT_MISMATCH)


def run_cli(*argv: str, env: dict | None = None) -> int:
    with mock.patch.dict(os.environ, env or {}):
        return rrt.main(list(argv))


class OutDirTests(unittest.TestCase):
    """Fix 1: --out (and the parents of --junit/--summary) are created even if no engine runs."""

    def test_run_creates_missing_out_dir_when_all_cases_skipped(self):
        with tempfile.TemporaryDirectory() as d:
            out = Path(d) / "a" / "b" / "rt-e1"
            rc = run_cli("--engine", "fake", "--arch", "linux-x86_64", "--out", str(out), "run", "--stage", "e1",
                         "--junit", str(Path(d) / "j" / "junit.xml"), "--summary", str(Path(d) / "s" / "summary.md"))
            self.assertEqual(rc, rrt.EXIT_OK)
            res = json.loads((out / "results.json").read_text(encoding="utf-8"))
            self.assertTrue(res and all(r["status"] == "skipped" for r in res))
            self.assertIn('skipped="%d"' % len(res), (Path(d) / "j" / "junit.xml").read_text())
            self.assertTrue((Path(d) / "s" / "summary.md").is_file())

    def test_run_with_no_golden_creates_out_dir(self):
        with tempfile.TemporaryDirectory() as d:
            out = Path(d) / "new"
            rc = run_cli("--arch", "no-such-arch", "--out", str(out), "run", "--case", "selftest-match")
            self.assertEqual(rc, rrt.EXIT_NO_GOLDEN)
            self.assertTrue((out / "results.json").is_file())

    def test_wrapper_script_e1_exits_0(self):
        with tempfile.TemporaryDirectory() as d:
            out = Path(d) / "rt-e1"
            p = subprocess.run([str(HERE.parent.parent / "tools" / "replay-test"), "--engine", "fake",
                                "--arch", "linux-x86_64", "--out", str(out), "run", "--stage", "e1"],
                               capture_output=True, text=True, timeout=60)
            self.assertEqual(p.returncode, 0, p.stdout + p.stderr)
            self.assertTrue((out / "results.json").is_file())


class SelftestCommandTests(unittest.TestCase):
    """Fix 2: `selftest` runs tests/<dir>/test_*.py with stdlib unittest, exit 0/1."""

    def _tree(self, d: Path, failing: bool) -> Path:
        for name, body in (("alpha", "def f():\n    return 1\n"), ("beta", "def g():\n    return 2\n")):
            (d / name).mkdir()
            (d / name / f"{name}_mod.py").write_text(body, encoding="utf-8")
        (d / "alpha" / "test_alpha.py").write_text(
            "import unittest\nimport alpha_mod\n\nclass T(unittest.TestCase):\n"
            "    def test_a(self):\n        self.assertEqual(alpha_mod.f(), 1)\n", encoding="utf-8")
        (d / "beta" / "test_beta.py").write_text(
            "import unittest\nimport beta_mod\n\nclass T(unittest.TestCase):\n"
            f"    def test_b(self):\n        self.assertEqual(beta_mod.g(), {3 if failing else 2})\n"
            "    def test_c(self):\n        pass\n", encoding="utf-8")
        (d / "empty").mkdir()            # no test_*.py: ignored
        return d

    def _run(self, *extra: str, failing: bool = False) -> tuple[int, str]:
        with tempfile.TemporaryDirectory() as d:
            root = self._tree(Path(d), failing)
            buf = io.StringIO()
            with contextlib.redirect_stdout(buf), contextlib.redirect_stderr(io.StringIO()):
                rc = rrt.main(["selftest", "-q", "--root", str(root), *extra])
            return rc, buf.getvalue()

    def test_all_pass(self):
        rc, out = self._run()
        self.assertEqual(rc, rrt.EXIT_OK, out)
        self.assertIn("alpha: 1 run", out)
        self.assertIn("beta: 2 run", out)
        self.assertIn("total 3 -> OK", out)

    def test_failure_gives_exit_1(self):
        rc, out = self._run(failing=True)
        self.assertEqual(rc, 1)
        self.assertIn("FAILED", out)

    def test_only(self):
        rc, out = self._run("--only", "alpha", failing=True)
        self.assertEqual(rc, rrt.EXIT_OK, out)
        self.assertNotIn("beta:", out)
        self.assertEqual(self._run("--only", "nope")[0], 1)

    def test_real_tree_has_the_three_suites(self):
        dirs = {d.name for d in rrt.SELFTEST_ROOT.iterdir() if d.is_dir() and any(d.glob("test_*.py"))}
        self.assertTrue({"replay", "perf", "terrain"} <= dirs, dirs)

    def test_ci_yaml_calls_selftest_through_the_wrapper(self):
        yml = (HERE / "ci" / "replay-tests.yml").read_text(encoding="utf-8")
        self.assertIn("$BFME_DOCKER replay tools/replay-test selftest", yml)
        # Platform contract: the replay mode accepts no -e/-v/--network, only whitelisted commands
        allowed = ("tools/replay-test", "tests/replay/run_replay_tests.py", "tools/ci/build-headless.sh")
        calls = [ln.split("$BFME_DOCKER replay", 1)[1] for ln in yml.replace("\\\n", " ").splitlines()
                 if "$BFME_DOCKER replay" in ln and not ln.lstrip().startswith("#")]
        self.assertGreaterEqual(len(calls), 6)
        for c in calls:
            self.assertNotRegex(c, r"(^|\s)(-e|-v|--network)(\s|=|$)", c)
            self.assertTrue(c.split()[0] in allowed, c)
        self.assertIn("--timeout-scale 20", yml)
        self.assertNotIn("tools/ci/check-artifacts.sh", yml.split("jobs:", 1)[1])

    def test_ci_yaml_deadlines_and_one_artifact_list_per_job(self):
        yml = (HERE / "ci" / "replay-tests.yml").read_text(encoding="utf-8")
        jobs = {}
        for chunk in yml.split("\njobs:\n", 1)[1].split("\n  ")[1:]:
            if chunk and not chunk.startswith((" ", "#")):
                jobs[chunk.split(":", 1)[0]] = chunk
        body = yml.split("\njobs:\n", 1)[1]
        parts = {}
        names = [n for n in ("replay-selftest", "replay-crc", "replay-crc-arm64")]
        for i, n in enumerate(names):
            start = body.index(f"\n  {n}:\n")
            end = body.index(f"\n  {names[i + 1]}:\n") if i + 1 < len(names) else len(body)
            parts[n] = body[start:end]
        self.assertIn("timeout-minutes: 10\n", parts["replay-selftest"])
        self.assertIn("timeout-minutes: 90\n", parts["replay-crc"])
        self.assertIn("--deadline-min 75 ", parts["replay-crc"])
        self.assertIn("timeout-minutes: 360\n", parts["replay-crc-arm64"])
        self.assertIn("--deadline-min 345 ", parts["replay-crc-arm64"])
        for n, p in parts.items():
            self.assertEqual(p.count("REPLAY_ARTIFACTS: |"), 1, n)            # defined once, in the job env
            lst = p.split("REPLAY_ARTIFACTS: |\n", 1)[1].split("\n    steps:", 1)[0]
            self.assertNotIn(".rep", lst, n)
            self.assertEqual(len([x for x in lst.splitlines() if x.strip()]), 5, n)
            self.assertIn("files=( $REPLAY_ARTIFACTS )", p, n)
            self.assertIn('/usr/local/sbin/bfme-check-artifacts "${files[@]}"', p, n)
            self.assertIn("path: ${{ env.REPLAY_ARTIFACTS }}", p, n)
            self.assertNotIn("/*.crclog\n            ", p.split("steps:", 1)[1], n)   # no second list in steps


class ScenarioCaseTests(unittest.TestCase):
    """Fix 3: -qaScenario cases record their own .rep; missing input = one failed case, not exit 3."""

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.out = Path(self.tmp.name)

    def tearDown(self):
        self.tmp.cleanup()

    def test_scenario_without_prerecorded_rep_passes_against_golden(self):
        c = fake_case("selftest-scenario")
        self.assertTrue(c.is_scenario)
        self.assertEqual(c.replay, "")
        r = rrt.run_case(c, "linux-x86_64", self.out)
        self.assertEqual(r.status, "pass", r.message)
        rep = self.out / "selftest-scenario" / "last.rep"
        self.assertTrue(rep.is_file())
        log = crclog.parse_crclog((self.out / "selftest-scenario.actual.crclog").read_text())
        self.assertEqual(log.meta["scenario_rep_sha256"], rrt.sha256(rep))
        self.assertNotIn("replay_sha256", log.meta)
        # the scenario got its own -qaOut and {replay} = that .rep
        cmdline = (self.out / "selftest-scenario.engine.log").read_text().splitlines()[0]
        self.assertIn(str(self.out / "selftest-scenario"), cmdline)

    def test_rep_header_changes_every_run_but_case_still_passes(self):
        c = fake_case("selftest-scenario")
        rrt.run_case(c, "linux-x86_64", self.out)
        sha1 = rrt.sha256(self.out / "selftest-scenario" / "last.rep")
        r = rrt.run_case(fake_case("selftest-scenario"), "linux-x86_64", self.out)
        self.assertEqual(r.status, "pass", r.message)
        self.assertNotEqual(sha1, rrt.sha256(self.out / "selftest-scenario" / "last.rep"))

    def test_scenario_divergence_is_mismatch(self):
        r = rrt.run_case(fake_case("selftest-scenario", FAKE_DIVERGE_AT=250), "linux-x86_64", self.out)
        self.assertEqual(r.status, "mismatch")
        self.assertEqual(r.divergence["frame"], 250)

    def test_qaout_added_when_missing(self):
        c = fake_case("selftest-scenario")
        i = c.args.index("-qaOut")
        del c.args[i:i + 2]
        r = rrt.run_case(c, "linux-x86_64", self.out)
        self.assertEqual(r.status, "pass", r.message)
        self.assertTrue((self.out / "selftest-scenario" / "last.rep").is_file())

    def test_scenario_rep_recorded_check(self):
        r = rrt.run_case(fake_case("selftest-scenario-rep-recorded"), "linux-x86_64", self.out)
        self.assertEqual(r.status, "pass", r.message)
        self.assertEqual(r.compared, 6)
        r = rrt.run_case(fake_case("selftest-scenario-rep-recorded", FAKE_NO_REP=1), "linux-x86_64", self.out)
        self.assertEqual(r.status, "engine_error")
        self.assertIn("no .rep", r.message)

    def test_stale_rep_of_previous_run_is_removed(self):
        rrt.run_case(fake_case("selftest-scenario-rep-recorded"), "linux-x86_64", self.out)
        r = rrt.run_case(fake_case("selftest-scenario-rep-recorded", FAKE_NO_REP=1), "linux-x86_64", self.out)
        self.assertEqual(r.status, "engine_error")   # not a pass against last run's .rep

    def test_missing_scenario_rep_is_warning_for_golden_check(self):
        r = rrt.run_case(fake_case("selftest-scenario", FAKE_NO_REP=1), "linux-x86_64", self.out)
        self.assertEqual(r.status, "pass", r.message)
        self.assertTrue(any("no .rep" in w for w in r.warnings))

    def test_missing_golden_without_record_mode(self):
        r = rrt.run_case(fake_case("selftest-scenario"), "no-such-arch", self.out)
        self.assertEqual(r.status, "no_golden")
        self.assertIn("--record-missing", r.message)

    def test_record_mode_writes_golden_then_passes(self):
        g = self.out / "golden" / "selftest-scenario.crclog"
        with mock.patch.object(rrt, "golden_path", lambda arch, case: g):
            r = rrt.run_case(fake_case("selftest-scenario"), "linux-x86_64", self.out / "o", record_missing=True)
            self.assertEqual(r.status, "recorded", r.message)
            self.assertTrue(g.is_file())
            golden = crclog.parse_crclog(g.read_text())
            self.assertEqual(len(golden.frames), 600)
            self.assertNotIn("scenario_rep_sha256", golden.meta)
            r = rrt.run_case(fake_case("selftest-scenario"), "linux-x86_64", self.out / "o2", record_missing=True)
            self.assertEqual(r.status, "pass", r.message)

    def test_record_mode_refuses_nondeterministic_scenario(self):
        g = self.out / "g.crclog"
        calls = {"n": 0}
        real = rrt.run_engine

        def flaky(case, out_dir, *rest):
            calls["n"] += 1
            case.env = dict(case.env, FAKE_SEED=str(calls["n"]))
            return real(case, out_dir, *rest)
        with mock.patch.object(rrt, "golden_path", lambda arch, case: g), mock.patch.object(rrt, "run_engine", flaky):
            r = rrt.run_case(fake_case("selftest-scenario"), "linux-x86_64", self.out, record_missing=True)
        self.assertEqual(r.status, "mismatch")
        self.assertFalse(g.exists())

    def test_record_mode_cli(self):
        g = self.out / "g.crclog"
        with mock.patch.object(rrt, "golden_path", lambda arch, case: g):
            rc = run_cli("--out", str(self.out / "o"), "run", "--case", "selftest-scenario", "--record-missing")
        self.assertEqual(rc, rrt.EXIT_OK)
        self.assertEqual(json.loads((self.out / "o" / "results.json").read_text())[0]["status"], "recorded")

    def test_prerecorded_scenario_rep_is_used_when_present(self):
        rep = self.out / "pre.rep"
        rep.write_bytes(b"GENREP-prerecorded")
        c = fake_case("selftest-scenario")
        c.replay = str(rep)
        self.assertEqual(rrt.prerecorded_replay(c), rep.resolve())
        c.replay = str(self.out / "absent.rep")
        self.assertIsNone(rrt.prerecorded_replay(c))      # absent -> the scenario records its own

    def test_missing_input_marks_case_and_run_continues(self):
        manifest = self.out / "m.toml"
        manifest.write_text((HERE / "manifest.toml").read_text(encoding="utf-8") + '''
[[case]]
name = "selftest-missing-replay"
stage = "selftest"
engine = "$NO_SUCH_ENGINE_VAR_FOR_TEST"
replay = "/nonexistent/dir/absent.rep"

[[case]]
name = "selftest-bad-replay-name"
stage = "selftest"
engine = "fake"
replay = "fixtures/not-a-replay.txt"
''', encoding="utf-8")
        out = self.out / "o"
        rc = rrt.main(["--manifest", str(manifest), "--out", str(out), "run", "--stage", "selftest",
                       "--junit", str(out / "junit.xml")])
        res = {r["case"]: r for r in json.loads((out / "results.json").read_text())}
        self.assertEqual(res["selftest-missing-replay"]["status"], "missing_input")
        self.assertEqual(res["selftest-bad-replay-name"]["status"], "config_error")
        for name in ("selftest-match", "selftest-crclog-mode", "selftest-scenario", "selftest-scenario-rep-recorded"):
            self.assertEqual(res[name]["status"], "pass", res[name]["message"])
        self.assertEqual(rc, rrt.EXIT_CONFIG)             # reported after all cases ran
        junit = (out / "junit.xml").read_text()
        self.assertIn('errors="2"', junit)
        self.assertIn('type="missing_input"', junit)

    def test_mismatch_outranks_missing_input(self):
        rs = [rrt.Result("a", "missing_input", "", 0), rrt.Result("b", "mismatch", "", 0)]
        self.assertEqual(rrt.exit_code(rs), rrt.EXIT_MISMATCH)
        self.assertEqual(rrt.exit_code([rrt.Result("a", "recorded", "", 0)]), rrt.EXIT_OK)


class TimeoutScaleTests(unittest.TestCase):
    """Fix 4: --timeout-scale > $BFME_REPLAY_TIMEOUT_SCALE > 20 for arm64 under qemu-* > 1."""

    QEMU = "qemu-aarch64-static -L /usr/aarch64-linux-gnu /src/build/x/GeneralsXZH"

    def scale(self, cli=None, arch="linux-x86_64", engine=None, env=None):
        e = {k: v for k, v in os.environ.items() if k != rrt.TIMEOUT_SCALE_ENV}
        e.update(env or {})
        with mock.patch.dict(os.environ, e, clear=True):
            return rrt.timeout_scale(cli, arch, engine)[0]

    def test_default_is_1(self):
        self.assertEqual(self.scale(), 1.0)
        self.assertEqual(self.scale(arch="linux-arm64", engine="/src/build/GeneralsXZH"), 1.0)  # native arm64
        self.assertEqual(self.scale(arch="linux-x86_64", engine=self.QEMU), 1.0)

    def test_qemu_arm64_auto_20(self):
        self.assertEqual(self.scale(arch="linux-arm64", engine=self.QEMU), 20.0)
        self.assertEqual(self.scale(arch="android-arm64", engine="/usr/bin/qemu-aarch64 bin"), 20.0)

    def test_env_and_cli_override_auto(self):
        self.assertEqual(self.scale(arch="linux-arm64", engine=self.QEMU, env={rrt.TIMEOUT_SCALE_ENV: "7.5"}), 7.5)
        self.assertEqual(self.scale(cli=3.0, arch="linux-arm64", engine=self.QEMU,
                                    env={rrt.TIMEOUT_SCALE_ENV: "7.5"}), 3.0)
        self.assertEqual(self.scale(cli=1.0, arch="linux-arm64", engine=self.QEMU), 1.0)   # explicit 1 wins

    def test_bad_values_are_config_errors(self):
        for kw in ({"cli": 0.0}, {"cli": -2.0}, {"env": {rrt.TIMEOUT_SCALE_ENV: "fast"}}):
            with self.assertRaises(ValueError):
                self.scale(**kw)
        with tempfile.TemporaryDirectory() as d:
            self.assertEqual(run_cli("--out", d, "--timeout-scale", "0", "run", "--stage", "selftest"),
                             rrt.EXIT_CONFIG)

    def test_scale_reaches_the_engine_timeout(self):
        # timeout_s = 1 would kill the hanging fake engine after 1 s; x2.5 -> 2.5 s
        with tempfile.TemporaryDirectory() as d:
            manifest = Path(d) / "m.toml"
            manifest.write_text('''
[[case]]
name = "selftest-match"
stage = "selftest"
engine = "fake"
replay = "fixtures/selftest-a.rep"
cwd = "."
timeout_s = 1
env = { FAKE_HANG_AT = "5" }
''', encoding="utf-8")
            rc = run_cli("--manifest", str(manifest), "--out", d, "--timeout-scale", "2.5", "run",
                         "--summary", str(Path(d) / "s.md"))
            self.assertEqual(rc, rrt.EXIT_ENGINE)
            r = json.loads((Path(d) / "results.json").read_text())[0]
            self.assertEqual(r["status"], "timeout")
            self.assertIn("2.5 s", r["message"])
            self.assertGreaterEqual(r["seconds"], 2.4)
            self.assertIn("Множитель таймаута: 2.5", (Path(d) / "s.md").read_text())

    def test_auto_scale_from_cli_engine(self):
        _, cases = rrt.load_manifest(HERE / "manifest.toml", self.QEMU)
        ns = mock.Mock(timeout_scale=None, case_timeout=None)
        with mock.patch.dict(os.environ, {}, clear=False):
            os.environ.pop(rrt.TIMEOUT_SCALE_ENV, None)
            scale, src = rrt.apply_timeout_scale(ns, cases, "linux-x86_64-qemu", self.QEMU)
        self.assertEqual((scale, src), (1.0, "default"))       # no "arm64" in --arch: no auto scale
        _, cases = rrt.load_manifest(HERE / "manifest.toml", self.QEMU)
        with mock.patch.dict(os.environ, {}, clear=False):
            os.environ.pop(rrt.TIMEOUT_SCALE_ENV, None)
            scale, src = rrt.apply_timeout_scale(ns, cases, "linux-arm64", self.QEMU)
        self.assertEqual((scale, src), (20.0, "auto: arm64 under qemu"))
        # without [defaults] passed: the case's own / [defaults].timeout_s 900
        self.assertTrue(all(c.timeout_s == 900 * 20 for c in cases if c.engine == self.QEMU))

    def test_scale_above_50_is_config_error(self):
        self.assertEqual(self.scale(cli=50.0), 50.0)
        self.assertEqual(self.scale(env={rrt.TIMEOUT_SCALE_ENV: "50"}), 50.0)
        self.assertEqual(self.scale(env={rrt.TIMEOUT_SCALE_ENV: "2.5"}), 2.5)
        for kw in ({"cli": 50.5}, {"env": {rrt.TIMEOUT_SCALE_ENV: "51"}}, {"env": {rrt.TIMEOUT_SCALE_ENV: "1e3"}},
                   {"env": {rrt.TIMEOUT_SCALE_ENV: "-2"}}, {"env": {rrt.TIMEOUT_SCALE_ENV: "inf"}},
                   {"env": {rrt.TIMEOUT_SCALE_ENV: ".5"}}):
            with self.assertRaises(ValueError, msg=str(kw)):
                self.scale(**kw)
        with tempfile.TemporaryDirectory() as d:
            self.assertEqual(run_cli("--out", d, "run", "--stage", "selftest", env={rrt.TIMEOUT_SCALE_ENV: "51"}),
                             rrt.EXIT_CONFIG)
            self.assertEqual(run_cli("--out", d, "--timeout-scale", "60", "bless", "--case", "selftest-match",
                                     "--reason", "x"), rrt.EXIT_CONFIG)


class CaseTimeoutTests(unittest.TestCase):
    """Platform: arm64 case timeout 600 s (x20 = 3.3 h) via [defaults.timeout_s_arch] or --case-timeout."""

    MANIFEST = '''
[defaults]
timeout_s = 900
timeout_s_arch = { "linux-arm64" = 600 }
[[case]]
name = "plain"
engine = "fake"
replay = "fixtures/selftest-a.rep"
[[case]]
name = "own"
engine = "fake"
replay = "fixtures/selftest-a.rep"
timeout_s = 120
'''

    def timeouts(self, arch, cli=None, scale=None):
        with tempfile.TemporaryDirectory() as d:
            m = Path(d) / "m.toml"
            m.write_text(self.MANIFEST, encoding="utf-8")
            defaults, cases = rrt.load_manifest(m, None)
        ns = mock.Mock(timeout_scale=scale, case_timeout=cli)
        with mock.patch.dict(os.environ, {}, clear=False):
            os.environ.pop(rrt.TIMEOUT_SCALE_ENV, None)
            rrt.apply_timeout_scale(ns, cases, arch, None, defaults)
        return {c.name: c.timeout_s for c in cases}

    def test_per_arch_default(self):
        self.assertEqual(self.timeouts("linux-x86_64"), {"plain": 900, "own": 120})
        self.assertEqual(self.timeouts("linux-arm64"), {"plain": 600, "own": 120})   # the case's own wins
        self.assertEqual(self.timeouts("linux-arm64", scale=20.0), {"plain": 600 * 20, "own": 120 * 20})

    def test_cli_case_timeout_wins(self):
        self.assertEqual(self.timeouts("linux-arm64", cli=300.0, scale=2.0), {"plain": 600, "own": 600})

    def test_real_manifest_arm64_is_600(self):
        defaults, cases = rrt.load_manifest(HERE / "manifest.toml", None)
        rrt.apply_case_timeouts(defaults, cases, "linux-arm64", None)
        self.assertTrue(all(c.timeout_s == 600 for c in cases if not c.timeout_explicit))
        self.assertEqual(defaults["timeout_s_arch"]["linux-arm64"] * 20, 12000)     # 3.3 h < --deadline-min 345

    def test_bad_case_timeout_is_config_error(self):
        with tempfile.TemporaryDirectory() as d:
            self.assertEqual(run_cli("--out", d, "--case-timeout", "0", "run", "--stage", "selftest"),
                             rrt.EXIT_CONFIG)


DEADLINE_MANIFEST = '''
[defaults]
args = ["-headless", "-replay", "{{replay}}", "-gxCrcEveryFrame", "-gxAutoQuit"]
env = {{ GX_NET_TRACE = "1" }}
mode = "gx-stderr"
[[case]]
name = "slow-pass"
stage = "selftest"
engine = "fake"
replay = "fixtures/selftest-a.rep"
cwd = "."
timeout_s = 60
env = {{ FAKE_FRAMES = "30", FAKE_SLEEP = "{sleep}" }}
[[case]]
name = "after-1"
stage = "selftest"
engine = "fake"
replay = "fixtures/selftest-a.rep"
cwd = "."
env = {{ FAKE_FRAMES = "30" }}
[[case]]
name = "after-2"
stage = "selftest"
engine = "fake"
replay = "fixtures/selftest-b.rep"
cwd = "."
env = {{ FAKE_FRAMES = "30" }}
'''


class DeadlineTests(unittest.TestCase):
    """Platform: --deadline-min M = wall-clock limit of the whole run (run and bless)."""

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.d = Path(self.tmp.name)
        # goldens recorded by these tests go to the temp dir, never into tests/replay/golden/
        self.gold = mock.patch.object(rrt, "golden_path",
                                      lambda arch, case: self.d / "golden" / arch / f"{case.name}.crclog")
        self.gold.start()

    def tearDown(self):
        self.gold.stop()
        self.tmp.cleanup()

    def manifest(self, sleep="0", hang=False):
        text = DEADLINE_MANIFEST.format(sleep=sleep)
        if hang:
            text = text.replace('FAKE_SLEEP = "%s"' % sleep, 'FAKE_HANG_AT = "5"')
        m = self.d / "m.toml"
        m.write_text(text, encoding="utf-8")
        return m

    def run_deadline(self, m, minutes, *extra):
        out = self.d / "out"
        rc = run_cli("--manifest", str(m), "--out", str(out), *extra, "run", "--deadline-min", str(minutes),
                     "--record-missing", "--junit", str(self.d / "j" / "junit.xml"),
                     "--summary", str(self.d / "s" / "summary.md"))
        results = {r["case"]: r for r in json.loads((out / "results.json").read_text())}
        return rc, results

    def test_unit_deadline_limit(self):
        now = [100.0]
        dl = rrt.Deadline(1.0, clock=lambda: now[0])          # ends at 160
        self.assertEqual(dl.limit(30), (30, False))
        now[0] = 150.0
        self.assertEqual(dl.limit(30), (10.0, True))
        self.assertFalse(dl.passed)
        now[0] = 161.0
        self.assertTrue(dl.passed)
        self.assertEqual(dl.limit(30), (0.0, True))
        self.assertEqual(rrt.Deadline(None).limit(30), (30, False))
        self.assertFalse(rrt.Deadline(None).passed)

    def test_cases_after_the_deadline_are_deadline_and_reports_are_written(self):
        # slow-pass needs 2 x ~1.5 s (record-missing = two runs) > 0.02 min = 1.2 s: second run is never started
        rc, res = self.run_deadline(self.manifest(sleep="1.5"), 0.02)
        self.assertEqual(rc, rrt.EXIT_INCOMPLETE, res)
        self.assertEqual(res["after-1"]["status"], "deadline")
        self.assertEqual(res["after-2"]["status"], "deadline")
        self.assertEqual(res["after-2"]["note"], "deadline")
        junit = (self.d / "j" / "junit.xml").read_text()
        self.assertIn('skipped="2"', junit)
        self.assertEqual(junit.count('<skipped message="deadline"/>'), 2)
        self.assertIn("--deadline-min 0.02", (self.d / "s" / "summary.md").read_text())

    def test_case_killed_by_the_deadline_is_timeout_with_note(self):
        t0 = __import__("time").monotonic()
        rc, res = self.run_deadline(self.manifest(hang=True), 0.03)      # 1.8 s, case timeout 60 s
        self.assertLess(__import__("time").monotonic() - t0, 30)
        r = res["slow-pass"]
        self.assertEqual((r["status"], r["note"]), ("timeout", "deadline"))
        self.assertIn("deadline", r["message"])
        self.assertEqual(res["after-1"]["status"], "deadline")
        self.assertEqual(rc, rrt.EXIT_INCOMPLETE)                         # deadline alone -> 4, not 2
        self.assertIn('type="timeout"', (self.d / "j" / "junit.xml").read_text())

    def test_case_timeout_before_the_deadline_is_a_real_timeout(self):
        m = self.manifest(hang=True)
        rc, res = self.run_deadline(m, 5, "--case-timeout", "1")
        self.assertEqual((res["slow-pass"]["status"], res["slow-pass"]["note"]), ("timeout", ""))
        self.assertEqual(res["after-1"]["status"], "recorded")
        self.assertEqual(rc, rrt.EXIT_ENGINE)

    def test_no_deadline_hit(self):
        rc, res = self.run_deadline(self.manifest(sleep="0"), 5)
        self.assertEqual(rc, rrt.EXIT_OK, res)
        self.assertEqual({r["status"] for r in res.values()}, {"recorded"})

    def test_exit_code_priority_with_deadline(self):
        R = rrt.Result
        dl = R("x", "deadline", "", 0, note="deadline")
        killed = R("k", "timeout", "", 0, note="deadline")
        self.assertEqual(rrt.exit_code([dl]), rrt.EXIT_INCOMPLETE)
        self.assertEqual(rrt.exit_code([killed, dl]), rrt.EXIT_INCOMPLETE)
        self.assertEqual(rrt.exit_code([dl, R("a", "pass", "", 0)]), rrt.EXIT_INCOMPLETE)
        self.assertEqual(rrt.exit_code([dl, R("a", "config_error", "", 0)]), rrt.EXIT_CONFIG)
        self.assertEqual(rrt.exit_code([killed, R("a", "mismatch", "", 0)]), rrt.EXIT_MISMATCH)
        self.assertEqual(rrt.exit_code([dl, R("a", "timeout", "", 0)]), rrt.EXIT_ENGINE)

    def test_bless_with_deadline(self):
        m = self.manifest(sleep="1.5")
        rc = run_cli("--manifest", str(m), "--out", str(self.d / "b"), "bless", "--case", "slow-pass",
                     "--reason", "test", "--deadline-min", "0.02")
        self.assertEqual(rc, rrt.EXIT_INCOMPLETE)
        self.assertFalse((self.d / "golden" / "linux-x86_64" / "slow-pass.crclog").exists())
        self.assertFalse((HERE / "golden" / "linux-x86_64" / "slow-pass.crclog").exists())

    def test_bad_deadline_is_config_error(self):
        for v in ("0", "-1"):
            self.assertEqual(run_cli("--out", str(self.d), "run", "--stage", "selftest", "--deadline-min", v),
                             rrt.EXIT_CONFIG)


class ErofsTests(unittest.TestCase):
    """Platform: cwd=/gamedata is read-only on purpose; an EROFS of the engine must be obvious."""

    def test_detects_typical_messages(self):
        text = ("ok line\n[GX] cannot open /gamedata/gx_replay_check_result.txt: Read-only file system (errno 30)\n"
                "fopen failed: EROFS\nerrno=30\nerrno 300 is not it\n")
        self.assertEqual(len(rrt.erofs_lines(text)), 3)
        self.assertEqual(rrt.erofs_lines("no problems\nerrno 300\n"), [])

    def test_erofs_is_flagged_in_result_and_case_log(self):
        with tempfile.TemporaryDirectory() as d:
            out = Path(d)
            r = rrt.run_case(fake_case(FAKE_WRITE_CWD="gx_replay_check_result.txt", FAKE_EROFS=1),
                             "linux-x86_64", out)
            self.assertEqual(r.status, "pass", r.message)       # the engine survived it: warning only
            self.assertTrue(any(w.startswith("EROFS") and "gx_replay_check_result.txt" in w for w in r.warnings),
                            r.warnings)
            log = (out / "selftest-match.engine.log").read_text()
            self.assertIn("[replay-test] EROFS", log)
            self.assertIn("read-only on purpose", log)

    def test_fatal_erofs_is_engine_error_with_clear_message(self):
        with tempfile.TemporaryDirectory() as d:
            out = Path(d)
            rc = run_cli("--out", str(out), "run", "--case", "selftest-match", "--summary", str(out / "s.md"),
                         env={"FAKE_WRITE_CWD": "gx_replay_check_result.txt", "FAKE_EROFS": "1",
                              "FAKE_WRITE_CWD_FATAL": "1"})
            self.assertEqual(rc, rrt.EXIT_ENGINE)
            r = json.loads((out / "results.json").read_text())[0]
            self.assertEqual(r["status"], "engine_error")
            self.assertIn("EROFS", r["message"])
            self.assertIn("EROFS", (out / "s.md").read_text())

    def test_plain_permission_error_is_not_erofs(self):
        with tempfile.TemporaryDirectory() as d:
            r = rrt.run_case(fake_case(FAKE_WRITE_CWD="/nonexistent-dir/x.txt"), "linux-x86_64", Path(d))
            self.assertEqual(r.status, "pass", r.message)
            self.assertFalse(any(w.startswith("EROFS") for w in r.warnings))


if __name__ == "__main__":
    unittest.main(verbosity=2)
