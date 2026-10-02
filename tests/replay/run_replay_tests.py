#!/usr/bin/env python3
# BFME1 Mobile QA: headless replay tests with per-frame CRC (design: docs/REPLAY-TESTS.md).
#
#   run_replay_tests.py [--manifest M] [--arch A] [--engine PATH] [--out DIR] [--timeout-scale X]
#                       [--case-timeout S] <command>
#   run      [--case NAME ...] [--stage S ...] [--junit FILE] [--summary FILE] [--record-missing] [--deadline-min M]
#   bless    --case NAME --reason "why" [--deadline-min M]
#   compare  EXPECTED ACTUAL [--rep-offset K]  (crclog, fork stderr log or .rep)
#   rep-crcs FILE.rep [--offset K]           (CRCs the replay recorded itself)
#   selftest [-q] [--only DIR ...]           (stdlib unittest of tests/replay, tests/perf, tests/terrain, ...)
#
# --out is created (with parents) by run/bless; --junit/--summary parents too.
#
# Timeouts: the case timeout is --case-timeout S > timeout_s of the case > [defaults.timeout_s_arch]
# for --arch > [defaults].timeout_s; it is multiplied by the timeout scale. Scale precedence:
# --timeout-scale X > $BFME_REPLAY_TIMEOUT_SCALE (digits[.digits]) > 20 when --arch contains "arm64"
# and the engine (--engine / $BFME_HEADLESS_ENGINE) starts with "qemu-" (qemu user mode is 10-30x
# slower) > 1.0. A scale above 50 is a configuration error (exit 3).
#
# --deadline-min M (run, bless): wall-clock limit for the whole run. Each engine start gets
# min(case timeout x scale, time left). A case killed because the deadline came first is
# "timeout" with note "deadline"; cases not started before the deadline are "deadline"
# (JUnit <skipped message="deadline">). results.json, JUnit and summary are still written.
#
# Scenario cases (args contain -qaScenario, REPLAY-TESTS §9.1): no prerecorded .rep is needed.
# The scenario writes its own .rep to the case out dir (-qaOut, default {case_out} =
# <out>/<case>/, file name `scenario_rep`, default "last.rep") and the CRC log; the runner
# compares with the golden (or, with check = "rep-recorded", with the CRCs inside that .rep).
# A missing golden is recorded (two identical runs, like bless) with `run --record-missing`.
# If `replay` is set and the file exists it is used as {replay} (prerecorded) instead.
#
# A case whose input is missing (replay not found) or whose configuration is broken gets
# status missing_input / config_error and the run goes on with the other cases.
#
# Engine writes to the read-only game-data cwd (/gamedata:ro in CI) fail with EROFS on
# purpose; the runner flags them in the result and appends a "[replay-test] EROFS" line
# to <case>.engine.log.
#
# Replay path (Лид 02.10.2026, fork Recorder.cpp L916-925, CommandLine.cpp L477): the engine
# accepts an absolute path; a relative path with a directory is resolved from the CWD; a bare
# file name is looked up in the user-data Replays folder; the ".rep" extension is mandatory
# (otherwise the engine exits with 1). The runner always passes an absolute path and refuses
# replay names without ".rep" as a configuration error.
#
# Exit codes: 0 all passed, 1 CRC mismatch (or failing unit tests in selftest), 2 engine
# failure (crash, timeout, no CRC output), 3 configuration error / missing input, 4 golden
# file missing or run incomplete (--deadline-min reached). With several problems the first in this
# order wins: 2, 1, 3, 4 (a timeout caused only by the deadline counts as 4, not 2).
from __future__ import annotations

import argparse
import dataclasses
import datetime as dt
import hashlib
import json
import os
import re
import shlex
import subprocess
import sys
import time
import tomllib
from pathlib import Path
from xml.sax.saxutils import escape

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import crclog  # noqa: E402

EXIT_OK, EXIT_MISMATCH, EXIT_ENGINE, EXIT_CONFIG, EXIT_NO_GOLDEN = 0, 1, 2, 3, 4
EXIT_INCOMPLETE = 4            # --deadline-min reached (same priority as a missing golden)
QEMU_TIMEOUT_SCALE = 20.0      # default for arm64 under qemu user mode (ci/README.md)
MAX_TIMEOUT_SCALE = 50.0       # the CI wrapper passes at most 50 (Platform 02.10.2026)
TIMEOUT_SCALE_ENV = "BFME_REPLAY_TIMEOUT_SCALE"
TIMEOUT_SCALE_RE = re.compile(r"[0-9]+(\.[0-9]+)?")   # format the wrapper lets through
DEADLINE_NOTE = "deadline"
# errno 30: the engine tried to write into the read-only game-data cwd (/gamedata:ro)
EROFS_RE = re.compile(r"read-only file system|\bEROFS\b|errno[ =:]*30\b", re.IGNORECASE)


class MissingInput(ValueError):
    """An input file of one case is missing (the case fails, the run goes on)."""


class Deadline:
    """Wall-clock limit of the whole run (--deadline-min); None = no limit."""

    def __init__(self, minutes: float | None, clock=time.monotonic):
        self.minutes = minutes
        self._clock = clock
        self._end = None if minutes is None else clock() + minutes * 60.0

    def remaining(self) -> float | None:
        return None if self._end is None else self._end - self._clock()

    @property
    def passed(self) -> bool:
        r = self.remaining()
        return r is not None and r <= 0

    def limit(self, timeout_s: float) -> tuple[float, bool]:
        """(timeout for the next engine start, True if the deadline is the binding limit)."""
        r = self.remaining()
        if r is None or timeout_s <= r:
            return timeout_s, False
        return max(r, 0.0), True


NO_DEADLINE = Deadline(None)


@dataclasses.dataclass
class Case:
    name: str
    replay: str
    stage: str
    engine: str
    args: list[str]
    env: dict[str, str]
    mode: str            # "gx-stderr" (works with the fork today) | "crclog" (engine-hooks patch)
    timeout_s: float
    cwd: str | None
    frames: int | None   # expected frame count (None: take the golden's)
    enabled: bool
    note: str
    logic_hz: int = 30           # Лид 02.10.2026: logic runs at 30 Hz; written as "# logic_hz=" in crclogs
    check: str = "golden"        # "golden" | "rep-recorded" (compare with the CRCs inside the .rep)
    rep_crc_offset: int = 1      # recorded label L <-> engine frame L-K (REPLAY-TESTS §2) [проверить]
    scenario_rep: str = "last.rep"   # scenario cases: file the scenario records in its -qaOut dir
    timeout_explicit: bool = False   # timeout_s set in the case itself (beats [defaults.timeout_s_arch])

    @property
    def is_scenario(self) -> bool:
        return "-qaScenario" in self.args


@dataclasses.dataclass
class Result:
    case: str
    status: str          # pass | recorded | mismatch | engine_error | timeout | no_golden | skipped
                         # | missing_input | config_error | deadline (not started: --deadline-min reached)
    message: str
    seconds: float
    compared: int = 0
    divergence: dict | None = None
    actual_path: str | None = None
    log_path: str | None = None
    warnings: list[str] = dataclasses.field(default_factory=list)
    note: str = ""       # "deadline": a timeout caused by --deadline-min, not by the case timeout

    @property
    def incomplete(self) -> bool:
        return self.status == "deadline" or (self.status == "timeout" and self.note == DEADLINE_NOTE)


def deadline_result(case: Case, deadline: Deadline) -> Result:
    return Result(case.name, "deadline",
                  f"not run: --deadline-min {deadline.minutes:g} reached before this case", 0.0, note=DEADLINE_NOTE)


def expand(value: str, ctx: dict[str, str]) -> str:
    for k, v in ctx.items():
        value = value.replace("{" + k + "}", v)
    return value


def load_manifest(path: Path, engine_override: str | None) -> tuple[dict, list[Case]]:
    data = tomllib.loads(path.read_text(encoding="utf-8"))
    d = data.get("defaults", {})
    cases = []
    for c in data.get("case", []):
        merged_env = dict(d.get("env", {}))
        merged_env.update(c.get("env", {}))
        engine = engine_override or c.get("engine", d.get("engine", ""))
        cases.append(Case(
            name=c["name"],
            replay=c.get("replay", ""),
            stage=c.get("stage", d.get("stage", "e2")),
            engine=engine,
            args=list(c.get("args", d.get("args", []))),
            env=merged_env,
            mode=c.get("mode", d.get("mode", "gx-stderr")),
            timeout_s=float(c.get("timeout_s", d.get("timeout_s", 600))),
            cwd=c.get("cwd", d.get("cwd")),
            frames=c.get("frames"),
            enabled=bool(c.get("enabled", True)),
            note=c.get("note", ""),
            logic_hz=int(c.get("logic_hz", d.get("logic_hz", 30))),
            check=c.get("check", d.get("check", "golden")),
            rep_crc_offset=int(c.get("rep_crc_offset", d.get("rep_crc_offset", 1))),
            scenario_rep=c.get("scenario_rep", d.get("scenario_rep", "last.rep")),
            timeout_explicit="timeout_s" in c,
        ))
    return d, cases


def golden_path(arch: str, case: Case) -> Path:
    return HERE / "golden" / arch / f"{case.name}.crclog"


def sha256(path: Path) -> str | None:
    if not path.is_file():
        return None
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def resolve_engine(engine: str) -> list[str]:
    if engine == "fake":
        return [sys.executable, str(HERE / "fake_engine.py")]
    if not engine:
        raise ValueError("no engine configured (set [defaults].engine, --engine or $BFME_HEADLESS_ENGINE)")
    return shlex.split(os.path.expandvars(engine))


def replay_path(case: Case) -> Path:
    """Absolute replay path for -replay; ".rep" is mandatory (the engine exits otherwise)."""
    if not case.replay:
        raise ValueError("case has no replay (only -qaScenario cases may omit it)")
    replay = Path(os.path.expandvars(case.replay))
    if replay.suffix.lower() != ".rep":
        raise ValueError(f"replay must end with .rep (engine CommandLine.cpp parseReplay): {case.replay}")
    if not replay.is_absolute():
        replay = HERE / replay   # never hand a bare name to the engine (it would look in ReplayDir)
    replay = replay.resolve()
    if case.engine != "fake" and not replay.is_file():
        raise MissingInput(f"replay not found: {replay}")
    return replay


def prerecorded_replay(case: Case) -> Path | None:
    """The .rep given in the manifest, if it is usable; scenario cases may do without one."""
    if not case.is_scenario:
        return replay_path(case)
    if not case.replay:
        return None
    try:
        rep = replay_path(case)
    except MissingInput:
        return None              # scenario records its own .rep
    return rep if rep.is_file() else None


def case_out_dir(case: Case, out_dir: Path) -> Path:
    return out_dir / case.name


def scenario_args(case: Case) -> list[str]:
    """Scenario cases always get a -qaOut dir (default: the case out dir)."""
    if case.is_scenario and "-qaOut" not in case.args:
        return list(case.args) + ["-qaOut", "{case_out}"]
    return list(case.args)


def scenario_rep_path(args: list[str], case: Case) -> Path:
    qa_out = Path(args[args.index("-qaOut") + 1])
    if not qa_out.is_absolute():
        qa_out = (Path(os.path.expandvars(case.cwd)) if case.cwd else Path.cwd()) / qa_out
    rep = (qa_out / case.scenario_rep).resolve()
    if rep.suffix.lower() != ".rep":
        raise ValueError(f"scenario_rep must end with .rep: {case.scenario_rep}")
    return rep


def erofs_lines(text: str, limit: int = 3) -> list[str]:
    """Engine log lines about writes into a read-only file system (EROFS, errno 30)."""
    out = []
    for line in text.splitlines():
        if line.startswith("[replay-test]"):
            continue
        if EROFS_RE.search(line):
            out.append(line.strip())
            if len(out) >= limit:
                break
    return out


def find_crclog(requested: Path) -> Path | None:
    """The engine hook writes "<stem>.<replay>.<pid><ext>" (unique per -jobs worker, engine-hooks/0001).
    Accept that or the exact path; more than one worker file means -jobs > 1 (use -jobs 1)."""
    if requested.is_file():
        return requested
    found = sorted(requested.parent.glob(f"{requested.stem}.*{requested.suffix}"))
    if len(found) > 1:
        raise ValueError(f"{len(found)} crclog files for one case ({', '.join(f.name for f in found)}): "
                         "run the engine with -jobs 1")
    return found[0] if found else None


def run_engine(case: Case, out_dir: Path, deadline: Deadline = NO_DEADLINE) -> tuple[crclog.CRCLog | None, Result]:
    if deadline.passed:
        return None, deadline_result(case, deadline)
    out_dir.mkdir(parents=True, exist_ok=True)
    prerec = prerecorded_replay(case)
    case_out = case_out_dir(case, out_dir)
    actual_crclog = out_dir / f"{case.name}.actual.crclog"
    if case.mode == "crclog":   # leftovers of a previous run would be picked up by find_crclog
        for old in out_dir.glob(f"{actual_crclog.stem}.*{actual_crclog.suffix}"):
            old.unlink()
        actual_crclog.unlink(missing_ok=True)
    stderr_log = out_dir / f"{case.name}.engine.log"
    ctx = {"crclog": str(actual_crclog), "out": str(out_dir), "case_out": str(case_out)}
    raw_args = scenario_args(case)
    scen_rep = None
    if case.is_scenario:
        case_out.mkdir(parents=True, exist_ok=True)
        scen_rep = scenario_rep_path([expand(x, ctx) for x in raw_args], case)
        scen_rep.parent.mkdir(parents=True, exist_ok=True)
        scen_rep.unlink(missing_ok=True)      # never compare against a .rep of a previous run
    replay = prerec if prerec is not None else scen_rep
    ctx.update({"replay": str(replay), "replay_name": replay.name})
    cmd = resolve_engine(expand(case.engine, ctx)) + [expand(a, ctx) for a in raw_args]
    env = dict(os.environ)
    env.update({k: expand(v, ctx) for k, v in case.env.items()})
    cwd = os.path.expandvars(case.cwd) if case.cwd else None
    timeout, by_deadline = deadline.limit(case.timeout_s)
    t0 = dt.datetime.now()
    try:
        with stderr_log.open("w", encoding="utf-8", errors="replace") as logf:
            logf.write("$ " + shlex.join(cmd) + "\n")
            logf.flush()
            proc = subprocess.run(cmd, cwd=cwd, env=env, stdout=logf, stderr=subprocess.STDOUT,
                                  timeout=timeout)
        rc = proc.returncode
    except subprocess.TimeoutExpired:
        secs = (dt.datetime.now() - t0).total_seconds()
        if by_deadline:
            return None, Result(case.name, "timeout",
                                f"killed at --deadline-min {deadline.minutes:g} after {secs:.0f} s "
                                f"(case timeout {case.timeout_s:g} s not reached)",
                                secs, log_path=str(stderr_log), note=DEADLINE_NOTE)
        return None, Result(case.name, "timeout", f"engine did not finish in {case.timeout_s:g} s",
                            secs, log_path=str(stderr_log))
    except OSError as e:
        return None, Result(case.name, "engine_error", f"cannot start engine: {e}", 0.0, log_path=str(stderr_log))
    secs = (dt.datetime.now() - t0).total_seconds()

    text = stderr_log.read_text(encoding="utf-8", errors="replace")
    warnings = []
    erofs = erofs_lines(text)
    if erofs:
        warnings.append(f"EROFS: engine tried to write into a read-only file system (cwd {cwd or '.'} "
                        f"is read-only on purpose): {erofs[0]}")
        with stderr_log.open("a", encoding="utf-8") as logf:
            logf.write(f"[replay-test] EROFS: the engine tried to write into a read-only file system "
                       f"(cwd {cwd or '.'}; /gamedata is mounted read-only on purpose). Lines:\n")
            logf.writelines(f"[replay-test]   {x}\n" for x in erofs)
    if case.mode == "crclog":
        written = find_crclog(actual_crclog)
        log = crclog.parse_crclog(written.read_text(encoding="utf-8")) if written else crclog.CRCLog()
    else:
        log = crclog.parse_gx_stderr(text)
        log.meta.update({"replay": replay.name})
    rsha = sha256(prerec) if prerec is not None else None
    if rsha:
        log.meta["replay_sha256"] = rsha
    if scen_rep is not None:
        ssha = sha256(scen_rep)
        if ssha:   # informational only: a .rep header carries timestamps, it is not compared
            log.meta["scenario_rep"] = scen_rep.name
            log.meta["scenario_rep_sha256"] = ssha
        elif rc == 0:
            warnings.append(f"scenario wrote no .rep at {scen_rep}")
    if log.logic_hz is None:          # fork stderr has no tick rate: take it from the manifest
        log.meta["logic_hz"] = str(case.logic_hz)
    elif log.logic_hz != case.logic_hz:
        raise ValueError(f"engine reports logic_hz={log.logic_hz}, manifest says {case.logic_hz}")
    if log.frames:   # normalised copy next to the engine log: <case>.actual.crclog
        actual_crclog.write_text(crclog.format_crclog(log), encoding="utf-8")
    res = Result(case.name, "pass", "", secs, actual_path=str(actual_crclog), log_path=str(stderr_log),
                 warnings=warnings)
    if rc != 0:
        res.status, res.message = "engine_error", f"engine exit code {rc} after frame {log.last_frame}"
        if erofs:
            res.message += " (EROFS: wrote into the read-only game-data cwd, see engine log)"
    elif not log.frames:
        res.status, res.message = "engine_error", "engine produced no CRC output (is -gxCrcEveryFrame / GX_NET_TRACE=1 set?)"
    return log, res


def recorded_golden(rep: Path, case: Case) -> crclog.CRCLog:
    """The replay's own CRCs (every ~100 frames), shifted by rep_crc_offset."""
    golden = crclog.rep_recorded_crcs(rep.read_bytes(), frame_offset=case.rep_crc_offset)
    golden.meta["logic_hz"] = str(case.logic_hz)
    return golden


def record_golden(case: Case, arch: str, out1: Path, out2: Path, reason: str,
                  deadline: Deadline = NO_DEADLINE) -> tuple[int, str, crclog.CRCLog | None, Result | None]:
    """Two runs must agree frame by frame; then the first one becomes golden/<arch>/<case>.crclog.
    Returns (exit code, message, golden log, the failed engine Result if a run failed)."""
    first, r1 = run_engine(case, out1, deadline)
    if r1.status != "pass":
        return (EXIT_INCOMPLETE if r1.incomplete else EXIT_ENGINE), r1.message, None, r1
    second, r2 = run_engine(case, out2, deadline)
    if r2.status != "pass":
        if r2.status == "deadline":
            r2.message = f"second run not started: --deadline-min {deadline.minutes:g} reached"
        return (EXIT_INCOMPLETE if r2.incomplete else EXIT_ENGINE), r2.message, None, r2
    cr = crclog.compare(first, second, allow_actual_longer=False)
    if not cr.ok:
        return EXIT_MISMATCH, (f"two runs disagree at frame {cr.divergence.frame} "
                               "(engine is not deterministic)"), None, None
    first.meta.update({
        "case": case.name, "arch": arch, "blessed_at": dt.datetime.now().astimezone().isoformat(timespec="seconds"),
        "reason": reason, "logic_hz": str(case.logic_hz),
        "engine_build_id": os.environ.get("BFME_ENGINE_BUILD_ID", "unknown"),
        "git_sha": os.environ.get("GITHUB_SHA") or os.environ.get("BFME_ENGINE_BUILD_ID", "unknown"),
    })
    first.meta.pop("scenario_rep_sha256", None)   # differs every run (timestamps in the .rep header)
    first.end = {"frames": str(len(first.frames)), "status": "ok"}
    gpath = golden_path(arch, case)
    gpath.parent.mkdir(parents=True, exist_ok=True)
    gpath.write_text(crclog.format_crclog(first), encoding="utf-8")
    try:
        shown = gpath.relative_to(HERE)
    except ValueError:
        shown = gpath
    return EXIT_OK, f"golden written: {shown} ({len(first.frames)} frames, 2 identical runs)", first, None


def case_out_rep(case: Case, out_dir: Path) -> Path:
    """Where the scenario of `case` recorded its .rep in `out_dir` (same rule as run_engine)."""
    ctx = {"crclog": str(out_dir / f"{case.name}.actual.crclog"), "out": str(out_dir),
           "case_out": str(case_out_dir(case, out_dir))}
    return scenario_rep_path([expand(x, ctx) for x in scenario_args(case)], case)


def run_case(case: Case, arch: str, out_dir: Path, record_missing: bool = False,
             deadline: Deadline = NO_DEADLINE) -> Result:
    if not case.enabled:
        return Result(case.name, "skipped", case.note or "disabled in manifest", 0.0)
    if deadline.passed:
        return deadline_result(case, deadline)
    prerec = prerecorded_replay(case)       # raises MissingInput / ValueError
    golden = None
    if case.check == "rep-recorded":
        if prerec is not None:
            golden = recorded_golden(prerec, case)
        elif not case.is_scenario:
            raise MissingInput("check = rep-recorded needs a replay")
        # else: scenario: the CRCs of the .rep it records in this run (read after the run)
    else:
        gpath = golden_path(arch, case)
        if not gpath.is_file():
            if not record_missing:
                return Result(case.name, "no_golden",
                              f"golden missing: {gpath.relative_to(HERE)} (run 'bless' or 'run --record-missing')", 0.0)
            t0 = dt.datetime.now()
            rc, msg, _, bad = record_golden(case, arch, out_dir, out_dir / f"{case.name}.record-2",
                                            "recorded by run --record-missing (golden was missing)", deadline)
            secs = (dt.datetime.now() - t0).total_seconds()
            status = {EXIT_OK: "recorded", EXIT_MISMATCH: "mismatch"}.get(rc, bad.status if bad else "engine_error")
            return Result(case.name, status, msg, secs, note=bad.note if bad else "",
                          actual_path=str(out_dir / f"{case.name}.actual.crclog"),
                          log_path=str(out_dir / f"{case.name}.engine.log"))
        golden = crclog.parse_crclog(gpath.read_text(encoding="utf-8"))
    actual, res = run_engine(case, out_dir, deadline)
    if golden is None and res.status == "pass":      # scenario + rep-recorded
        srep = actual.meta.get("scenario_rep")
        if not srep:
            res.status, res.message = "engine_error", "check = rep-recorded, but the scenario wrote no .rep"
            return res
        try:
            golden = recorded_golden(case_out_rep(case, out_dir), case)
        except ValueError as e:
            res.status, res.message = "engine_error", f"cannot read the CRCs of the scenario .rep: {e}"
            return res
    if res.status != "pass":
        # still say how far it got against the golden: useful for crashes
        if golden is not None and actual is not None and actual.frames:
            cr = crclog.compare(golden, actual)
            res.compared = cr.compared
            if cr.divergence and cr.divergence.kind == "crc":
                res.divergence = dataclasses.asdict(cr.divergence)
        return res
    # replay_sha256 exists only for prerecorded replays; a scenario .rep is recorded anew each run
    gsha = golden.meta.get("replay_sha256") if (case.check == "golden" and prerec is not None) else None
    if gsha and actual.meta.get("replay_sha256") and gsha != actual.meta["replay_sha256"]:
        res.status = "mismatch"
        res.message = "replay file changed since the golden was blessed (replay_sha256 differs)"
        return res
    cr = crclog.compare(golden, actual)
    res.compared = cr.compared
    if cr.ok:
        if case.check == "rep-recorded":
            res.message = (f"{cr.compared} recorded CRCs match (offset {case.rep_crc_offset}, "
                           f"last recorded frame {golden.last_frame}"
                           + (", scenario .rep" if prerec is None else "") + ")")
            return res
        expected_frames = case.frames or (golden.last_frame + 1 if golden.last_frame is not None else 0)
        res.message = f"{cr.compared} frames match (expected {expected_frames})"
        return res
    d = cr.divergence
    res.status = "mismatch"
    res.divergence = dataclasses.asdict(d)
    if d.kind == "logic_hz":
        res.message = f"logic_hz differs: golden {d.expected}, engine {d.actual}"
    elif d.kind == "missing_in_actual":
        res.message = f"engine stopped before frame {d.frame} (last matching frame {d.last_match})"
    else:
        sect = f", first differing section: {d.first_section}" if d.first_section else ""
        res.message = (f"first divergent logic frame {d.frame}: expected {d.expected:08X}, "
                       f"got {d.actual:08X}{sect}; last matching frame {d.last_match}")
    return res


ERROR_STATUSES = ("engine_error", "timeout", "missing_input", "config_error")


def write_junit(results: list[Result], path: Path) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    fails = sum(r.status in ("mismatch", "no_golden") for r in results)
    errors = sum(r.status in ERROR_STATUSES for r in results)
    skipped = sum(r.status in ("skipped", "deadline") for r in results)
    lines = ['<?xml version="1.0" encoding="UTF-8"?>',
             f'<testsuite name="replay-crc" tests="{len(results)}" failures="{fails}" errors="{errors}" skipped="{skipped}">']
    for r in results:
        lines.append(f'  <testcase classname="replay" name="{escape(r.case)}" time="{r.seconds:.1f}">')
        if r.status in ("mismatch", "no_golden"):
            lines.append(f'    <failure message="{escape(r.message, {chr(34): "&quot;"})}"/>')
        elif r.status in ERROR_STATUSES:
            lines.append(f'    <error type="{r.status}" message="{escape(r.message, {chr(34): "&quot;"})}"/>')
        elif r.status == "skipped":
            lines.append(f'    <skipped message="{escape(r.message, {chr(34): "&quot;"})}"/>')
        elif r.status == "deadline":   # message is exactly "deadline"; the details go to system-out
            lines.append(f'    <skipped message="{DEADLINE_NOTE}"/>')
            lines.append(f'    <system-out>{escape(r.message)}</system-out>')
        if r.warnings:
            lines.append(f'    <system-err>{escape(chr(10).join(r.warnings))}</system-err>')
        lines.append("  </testcase>")
    lines.append("</testsuite>")
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


ICON = {"pass": "✅", "recorded": "📝", "mismatch": "❌", "engine_error": "💥", "timeout": "⏱", "no_golden": "❔",
        "skipped": "⏭", "missing_input": "🚫", "config_error": "⚙", "deadline": "⌛"}


def write_summary(results: list[Result], arch: str, path: Path, note: str = "") -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    lines = [f"### Реплей-тесты CRC ({arch})", ""]
    if note:
        lines += [note, ""]
    lines += ["| Тест | Итог | Подробности | Время, с |", "|---|---|---|---|"]
    for r in results:
        msg = r.message + "".join(f" ⚠ {w}" for w in r.warnings)
        msg = msg.replace("|", "\\|").replace("\n", " ")
        lines.append(f"| `{r.case}` | {ICON.get(r.status, '')} {r.status} | {msg} | {r.seconds:.1f} |")
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def exit_code(results: list[Result]) -> int:
    st = {r.status for r in results if not r.incomplete}
    if st & {"engine_error", "timeout"}:
        return EXIT_ENGINE
    if "mismatch" in st:
        return EXIT_MISMATCH
    if st & {"missing_input", "config_error"}:
        return EXIT_CONFIG
    if "no_golden" in st or any(r.incomplete for r in results):
        return EXIT_NO_GOLDEN      # == EXIT_INCOMPLETE
    return EXIT_OK


def timeout_scale(cli: float | None, arch: str, engine: str | None) -> tuple[float, str]:
    """(scale, where it came from): --timeout-scale > $BFME_REPLAY_TIMEOUT_SCALE > qemu/arm64 auto > 1."""
    if cli is not None:
        scale, src = cli, "--timeout-scale"
    elif os.environ.get(TIMEOUT_SCALE_ENV, "").strip():
        raw = os.environ[TIMEOUT_SCALE_ENV].strip()
        if not TIMEOUT_SCALE_RE.fullmatch(raw):
            raise ValueError(f"${TIMEOUT_SCALE_ENV} must look like 20 or 2.5 (digits[.digits]), got {raw!r}")
        scale = float(raw)
        src = f"${TIMEOUT_SCALE_ENV}"
    else:
        first = ""
        if engine:
            try:
                parts = shlex.split(os.path.expandvars(engine))
            except ValueError:
                parts = engine.split()
            first = os.path.basename(parts[0]) if parts else ""
        if "arm64" in (arch or "") and first.startswith("qemu-"):
            scale, src = QEMU_TIMEOUT_SCALE, "auto: arm64 under qemu"
        else:
            scale, src = 1.0, "default"
    if not (scale > 0) or scale != scale or scale == float("inf"):
        raise ValueError(f"timeout scale must be a positive number, got {scale} ({src})")
    if scale > MAX_TIMEOUT_SCALE:
        raise ValueError(f"timeout scale {scale:g} ({src}) is above the maximum {MAX_TIMEOUT_SCALE:g}")
    return scale, src


def apply_case_timeouts(defaults: dict, cases: list[Case], arch: str, cli: float | None) -> None:
    """Base case timeout before the scale: --case-timeout > case timeout_s > [defaults.timeout_s_arch][arch]
    > [defaults].timeout_s."""
    if cli is not None and not (0 < cli < float("inf")):
        raise ValueError(f"--case-timeout must be a positive number of seconds, got {cli:g}")
    by_arch = defaults.get("timeout_s_arch", {})
    if not isinstance(by_arch, dict):
        raise ValueError("[defaults.timeout_s_arch] must be a table: arch = seconds")
    for c in cases:
        if cli is not None:
            c.timeout_s = float(cli)
        elif not c.timeout_explicit and arch in by_arch:
            c.timeout_s = float(by_arch[arch])
        if not c.timeout_s > 0:
            raise ValueError(f"case {c.name}: timeout must be positive, got {c.timeout_s:g}")


def apply_timeout_scale(a, cases: list[Case], arch: str, engine: str | None,
                        defaults: dict | None = None) -> tuple[float, str]:
    apply_case_timeouts(defaults or {}, cases, arch, getattr(a, "case_timeout", None))
    scale, src = timeout_scale(a.timeout_scale, arch, engine)
    for c in cases:
        c.timeout_s = c.timeout_s * scale
    return scale, src


def make_deadline(a) -> Deadline:
    m = getattr(a, "deadline_min", None)
    if m is not None and not (0 < m < float("inf")):
        raise ValueError(f"--deadline-min must be a positive number of minutes, got {m:g}")
    return Deadline(m)


def cmd_run(a) -> int:
    engine = a.engine or os.environ.get("BFME_HEADLESS_ENGINE")
    defaults, cases = load_manifest(Path(a.manifest), engine)
    arch = a.arch or defaults.get("arch", "linux-x86_64")
    try:
        scale, scale_src = apply_timeout_scale(a, cases, arch, engine, defaults)
        deadline = make_deadline(a)
    except ValueError as e:
        print(f"config error: {e}", file=sys.stderr)
        return EXIT_CONFIG
    if a.case:
        unknown = set(a.case) - {c.name for c in cases}
        if unknown:
            print(f"unknown case(s): {', '.join(sorted(unknown))}", file=sys.stderr)
            return EXIT_CONFIG
        cases = [c for c in cases if c.name in a.case]
    if a.stage:
        cases = [c for c in cases if c.stage in a.stage]
    out = Path(a.out)
    out.mkdir(parents=True, exist_ok=True)
    if scale != 1.0:
        print(f"timeout scale {scale:g} ({scale_src})", flush=True)
    if deadline.minutes is not None:
        print(f"run deadline: {deadline.minutes:g} min", flush=True)
    results = []
    for c in cases:
        try:
            r = run_case(c, arch, out, record_missing=a.record_missing, deadline=deadline)
        except MissingInput as e:      # this case cannot run; the others still do
            r = Result(c.name, "missing_input", str(e), 0.0)
        except ValueError as e:
            r = Result(c.name, "config_error", str(e), 0.0)
        results.append(r)
        print(f"[{r.status.upper():12}] {r.case}: {r.message}", flush=True)
        for w in r.warnings:
            print(f"    warning: {w}", flush=True)
    (out / "results.json").write_text(json.dumps([dataclasses.asdict(r) for r in results], indent=2), encoding="utf-8")
    if a.junit:
        write_junit(results, Path(a.junit))
    if a.summary:
        notes = []
        if scale != 1.0:
            notes.append(f"Множитель таймаута: {scale:g} ({scale_src}).")
        hit = [r for r in results if r.incomplete]
        if hit:
            notes.append(f"⌛ Достигнут лимит прогона --deadline-min {deadline.minutes:g} мин: прогон неполный, "
                         f"не запущено или прервано кейсов — {len(hit)}.")
        write_summary(results, arch, Path(a.summary), " ".join(notes))
    return exit_code(results)


def cmd_bless(a) -> int:
    engine = a.engine or os.environ.get("BFME_HEADLESS_ENGINE")
    defaults, cases = load_manifest(Path(a.manifest), engine)
    arch = a.arch or defaults.get("arch", "linux-x86_64")
    try:
        apply_timeout_scale(a, cases, arch, engine, defaults)
        deadline = make_deadline(a)
    except ValueError as e:
        print(f"config error: {e}", file=sys.stderr)
        return EXIT_CONFIG
    by_name = {c.name: c for c in cases}
    if a.case not in by_name:
        print(f"unknown case {a.case}", file=sys.stderr)
        return EXIT_CONFIG
    case = by_name[a.case]
    out = Path(a.out)
    if case.check != "golden":
        print(f"bless: case {case.name} compares with the CRCs inside the replay, it has no golden", file=sys.stderr)
        return EXIT_CONFIG
    try:
        prerecorded_replay(case)       # scenario cases record their own .rep
    except ValueError as e:
        print(f"bless refused: {e}", file=sys.stderr)
        return EXIT_CONFIG
    out.mkdir(parents=True, exist_ok=True)
    rc, msg, _, _ = record_golden(case, arch, out / "bless-1", out / "bless-2", a.reason, deadline)
    print(msg if rc == EXIT_OK else f"bless refused: {msg}", file=sys.stdout if rc == EXIT_OK else sys.stderr)
    return rc


def cmd_compare(a) -> int:
    exp = crclog.load_any(Path(a.expected), a.rep_offset)
    act = crclog.load_any(Path(a.actual), a.rep_offset)
    cr = crclog.compare(exp, act)
    if cr.ok:
        print(f"OK: {cr.compared} frames match")
        return EXIT_OK
    d = cr.divergence
    print(json.dumps(dataclasses.asdict(d), indent=2))
    return EXIT_MISMATCH


def cmd_rep_crcs(a) -> int:
    log = crclog.rep_recorded_crcs(Path(a.replay).read_bytes(), a.player, frame_offset=a.offset)
    sys.stdout.write(crclog.format_crclog(log))
    return EXIT_OK


SELFTEST_ROOT = HERE.parent      # tests/


def cmd_selftest(a) -> int:
    """stdlib unittest of every tests/<dir>/test_*.py (replay, perf, terrain, ...): 0 ok, 1 failed."""
    import unittest
    root = Path(a.root) if a.root else SELFTEST_ROOT
    dirs = sorted(d for d in root.iterdir() if d.is_dir() and any(d.glob("test_*.py")))
    if a.only:
        unknown = set(a.only) - {d.name for d in dirs}
        if unknown:
            print(f"selftest: no test_*.py in: {', '.join(sorted(unknown))}", file=sys.stderr)
            return EXIT_MISMATCH
        dirs = [d for d in dirs if d.name in a.only]
    if not dirs:
        print(f"selftest: no test_*.py under {root}", file=sys.stderr)
        return EXIT_MISMATCH
    total, ok, lines = 0, True, []
    for d in dirs:
        # one loader per directory: each one is its own top level (tests import their module by name);
        # afterwards drop that directory's modules and sys.path entry so the next one starts clean
        saved_path = list(sys.path)
        before = set(sys.modules)
        try:
            suite = unittest.TestLoader().discover(str(d), pattern="test_*.py", top_level_dir=str(d))
            print(f"== selftest: {d.name} ({d}) ==", flush=True)
            res = unittest.TextTestRunner(stream=sys.stdout, verbosity=1 if a.quiet else 2).run(suite)
        finally:
            sys.path[:] = saved_path
            for name in set(sys.modules) - before:
                f = getattr(sys.modules.get(name), "__file__", None) or ""
                if f and Path(f).resolve().parent == d.resolve():
                    del sys.modules[name]
        bad = len(res.failures) + len(res.errors) + len(getattr(res, "unexpectedSuccesses", []))
        lines.append(f"{d.name}: {res.testsRun} run, {bad} failed/errors, {len(res.skipped)} skipped")
        total += res.testsRun
        ok = ok and res.wasSuccessful() and res.testsRun > 0
    print("selftest summary: " + "; ".join(lines) + f"; total {total} -> {'OK' if ok else 'FAILED'}", flush=True)
    return EXIT_OK if ok else EXIT_MISMATCH


def main(argv: list[str] | None = None) -> int:
    p = argparse.ArgumentParser(description=__doc__ or "", formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--manifest", default=str(HERE / "manifest.toml"))
    p.add_argument("--engine", help="engine command (overrides manifest and $BFME_HEADLESS_ENGINE)")
    p.add_argument("--arch", help="golden set: linux-x86_64, linux-arm64, android-arm64, ...")
    p.add_argument("--out", default=str(HERE / "out"))
    p.add_argument("--timeout-scale", type=float, default=None,
                   help=f"multiply every timeout_s (default: ${TIMEOUT_SCALE_ENV}, else {QEMU_TIMEOUT_SCALE:g} "
                        f"for --arch *arm64* with a qemu-* engine, else 1; at most {MAX_TIMEOUT_SCALE:g})")
    p.add_argument("--case-timeout", type=float, default=None,
                   help="base timeout of every case in seconds, before the scale (default: timeout_s of the case, "
                        "else [defaults.timeout_s_arch] for --arch, else [defaults].timeout_s)")
    sub = p.add_subparsers(dest="cmd", required=True)
    r = sub.add_parser("run")
    r.add_argument("--case", action="append")
    r.add_argument("--stage", action="append", help="only cases of this stage (selftest, e2, e5, ...)")
    r.add_argument("--junit")
    r.add_argument("--summary")
    r.add_argument("--record-missing", action="store_true",
                   help="record a missing golden (two identical runs, like bless) instead of failing with no_golden")
    r.add_argument("--deadline-min", type=float, default=None,
                   help="wall-clock limit of the whole run in minutes; cases left at the deadline get status deadline")
    b = sub.add_parser("bless")
    b.add_argument("--case", required=True)
    b.add_argument("--reason", required=True, help="why the golden changes (goes into the file header)")
    b.add_argument("--deadline-min", type=float, default=None, help="wall-clock limit of both bless runs in minutes")
    c = sub.add_parser("compare")
    c.add_argument("expected")
    c.add_argument("actual")
    c.add_argument("--rep-offset", type=int, default=1,
                   help="for a .rep side: recorded frame label L = engine frame L-K (default 1, REPLAY-TESTS §2)")
    rc = sub.add_parser("rep-crcs")
    rc.add_argument("replay")
    rc.add_argument("--player", type=int)
    rc.add_argument("--offset", type=int, default=1, help="shift recorded labels by -K (default 1; 0 = raw labels)")
    st = sub.add_parser("selftest", help="unit tests under tests/ (stdlib unittest)")
    st.add_argument("-q", "--quiet", action="store_true")
    st.add_argument("--only", action="append", help="only this tests/<dir> (replay, perf, terrain, ...)")
    st.add_argument("--root", help=argparse.SUPPRESS)   # for the runner's own tests
    a = p.parse_args(argv)
    return {"run": cmd_run, "bless": cmd_bless, "compare": cmd_compare, "rep-crcs": cmd_rep_crcs,
            "selftest": cmd_selftest}[a.cmd](a)


if __name__ == "__main__":
    sys.exit(main())
