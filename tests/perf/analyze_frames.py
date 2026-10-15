#!/usr/bin/env python3
# BFME1 Mobile: perf-run analysis (QA, docs/TEST-PLAN.md §5.3–5.6). GPL v3, part of bfme-mobile.
#
# Reads perf/frames.csv (one row per rendered frame) and optionally perf/samples.csv and
# perf/summary.json from a phone report, recomputes the §5.4 metrics and prints a
# Markdown table "прошло / не прошло" for tests/reports/E1-pixel8pro.md.
#
#   analyze_frames.py frames.csv [--samples samples.csv] [--summary summary.json]
#                     [--skip-s 60] [--duration-s 300] [--json out.json] [--md out.md]
#                     [--logic-window 1801-10800] [--preliminary]
#                     [--run-kind perf|load|soak|crc-cost|other]
#   analyze_frames.py --synthetic out.csv [--seed 1] [--jank-every N] [--logic-hz 38]  # test data
#
# Thresholds approved by the Lead 02.10.2026 (TEST-PLAN §5.4). Also (Лид 02.10.2026):
#   - thermal status is recorded at the start and end of every run; a run that STARTED at
#     status >= MODERATE (2) is invalid (not a pass, not a fail) -- exit code 2;
#   - p95 cpu_frame_ms > 30 ms is a reported RISK in E1, not a failure;
#   - logic tick budget p95 logic_ms <= 10 ms: reported as RISK here; the horde scenario
#     from Gameplay will make it a criterion.
# Thermal status comes from the optional frames.csv column `thermal_status` or from
# samples.csv (`t_ns`, `thermal_status`), PowerManager.getCurrentThermalStatus() 0..6.
#
# Logic pace sanity rule (TEST-PLAN §5.4, Лид 02.10.2026): with 2.22 data
# (FramesPerSecondLimit = 38) single-player/skirmish logic currently runs at ~38 Hz wall-clock
# until the pace fix of E1-35 lands (FramePacer: logic time scale off -> one tick per render
# frame). Measured logic ticks per wall-clock second over the window, from the frames.csv
# columns `logic_frame` and `t_ns`, must be 30 +- 1; otherwise the run is invalid (exit 2).
# With --preliminary (runs for FPS, memory and heat before E1-35) a pace outside 30 +- 1 is only a
# warning: exit by the thresholds (0/1), "valid": null in the JSON, not for acceptance (Лид, ревью #11).
# Without a `logic_frame` column (or with < 2 rows carrying it) the pace cannot be checked:
# in a perf/load/soak/crc-cost run (--run-kind, default perf) that makes the run INVALID
# (exit 2, reason "logic pace not verifiable", TEST-PLAN §5.4.1: such a run does not go into
# acceptance); with --preliminary it is only a warning (no exit 2). --run-kind other: old
# behaviour, pace "не проверен" without consequences.
# --logic-window A-B selects the window by logic frame (inclusive) instead of seconds;
# --preliminary marks wall-clock results as preliminary (before the pace fix): pace problems are warnings.
#
# Exit codes: 0 all thresholds pass, 1 some fail, 2 run invalid (thermal or logic pace),
# 4 bad input (empty file, missing column in the header or in any row, non-numeric value,
# t_ns going backwards, no frames in the window, zero time interval).
from __future__ import annotations

import argparse
import csv
import json
import math
import random
import sys
from pathlib import Path

T_MS = 1000.0 / 30.0          # 30 FPS cap (ADR-0002 §7)
JANK_MS = 1.5 * T_MS           # > 50 ms
BIG_JANK_MS = 100.0
FREEZE_MS = 250.0

# Thresholds of TEST-PLAN §5.4 [предложение QA, утверждает Лид]. None = record only.
CPU_RISK_MS = 30.0             # p95 cpu_frame_ms above this: risk (E1), not a blocker
LOGIC_BUDGET_MS = 10.0         # p95 logic_ms, logic tick budget
THERMAL_MODERATE = 2           # PowerManager.THERMAL_STATUS_MODERATE
LOGIC_HZ = 30.0                # LOGICFRAMES_PER_SECOND, ADR-0002
LOGIC_HZ_TOL = 1.0             # sanity rule: measured logic ticks/s must be 30 +- 1
PACE_RUN_KINDS = ("perf", "load", "soak", "crc-cost")   # TEST-PLAN §5.4.1: pace rule applies
PACE_NOT_VERIFIABLE = "logic pace not verifiable"
PACE_OUT_OF_RANGE = "logic pace out of range"
THERMAL_NAMES = ["NONE", "LIGHT", "MODERATE", "SEVERE", "CRITICAL", "EMERGENCY", "SHUTDOWN"]

THRESHOLDS = {
    "fps_avg": (">=", 29.5),
    "p50_ms": ("<=", 34.0),
    "p95_ms": ("<=", 36.0),
    "p99_ms": None,
    "fps_1pct_low": None,
    "jank_pct": ("<=", 2.0),
    "big_jank_count": ("<=", 5),
    "freeze_count": ("==", 0),
    "cpu_p95_ms": None,
}
LABELS = {
    "fps_avg": "Средний FPS", "p50_ms": "p50 интервала, мс", "p95_ms": "p95 интервала, мс",
    "p99_ms": "p99 интервала, мс", "fps_1pct_low": "FPS 1% low (1000/p99)",
    "jank_pct": "Jank > 50 мс, %", "big_jank_count": "Big jank > 100 мс, шт",
    "freeze_count": "Фризы > 250 мс, шт", "cpu_p95_ms": "p95 cpu_frame_ms, мс",
    "frames": "Кадров в окне", "window_s": "Окно замера, с",
}


def percentile(sorted_vals: list[float], p: float) -> float:
    """Linear interpolation between closest ranks (same as numpy 'linear')."""
    if not sorted_vals:
        return math.nan
    k = (len(sorted_vals) - 1) * p / 100.0
    lo, hi = math.floor(k), math.ceil(k)
    if lo == hi:
        return sorted_vals[lo]
    return sorted_vals[lo] + (sorted_vals[hi] - sorted_vals[lo]) * (k - lo)


REQUIRED_COLUMNS = ("t_ns", "present_interval_ms")   # TEST-PLAN §5.3, in every row
CHECKED_COLUMNS = ("logic_frame",)                    # optional, but if the header has it: every row


def load_frames(path: Path) -> list[dict[str, float]]:
    """Rows of frames.csv as floats. Bad input raises ValueError (exit 4): an empty file, a missing
    required column, a row without a numeric t_ns / present_interval_ms (or logic_frame, when the
    header has that column), t_ns going backwards."""
    rows = []
    with path.open(newline="", encoding="utf-8") as f:
        rd = csv.DictReader(f)
        header = rd.fieldnames
        if not header:
            raise ValueError(f"{path}: empty file (no header; TEST-PLAN §5.3 needs t_ns, present_interval_ms)")
        missing = [c for c in REQUIRED_COLUMNS if c not in header]
        if missing:
            raise ValueError(f"{path}: column(s) missing in the header: {', '.join(missing)} "
                             "(TEST-PLAN §5.3 needs t_ns and present_interval_ms)")
        checked = REQUIRED_COLUMNS + tuple(c for c in CHECKED_COLUMNS if c in header)
        for line, r in enumerate(rd, start=2):          # line 1 is the header
            row = {}
            for k, v in r.items():
                if k is None or v is None or v == "":
                    continue
                try:
                    row[k] = float(v)
                except ValueError:
                    pass
            bad = [c for c in checked if c not in row or not math.isfinite(row[c])]
            if bad:
                raise ValueError(f"{path}:{line}: no numeric value in column(s) {', '.join(bad)}")
            if rows and row["t_ns"] < rows[-1]["t_ns"]:
                raise ValueError(f"{path}:{line}: t_ns goes backwards ({row['t_ns']:.0f} < {rows[-1]['t_ns']:.0f})")
            rows.append(row)
    if not rows:
        raise ValueError(f"{path}: no data rows (only the header)")
    return rows


def window(rows: list[dict[str, float]], skip_s: float, duration_s: float | None) -> list[dict[str, float]]:
    if not rows:
        raise ValueError("frames.csv has no data rows")
    t0 = rows[0]["t_ns"] + skip_s * 1e9
    t1 = t0 + duration_s * 1e9 if duration_s else math.inf
    return [r for r in rows if t0 <= r["t_ns"] < t1]


def parse_logic_window(spec: str) -> tuple[int, int]:
    """'1801-10800' -> (1801, 10800), inclusive logic frames."""
    try:
        a, b = (int(x) for x in spec.replace(":", "-").split("-", 1))
    except ValueError:
        raise ValueError(f"--logic-window: expected A-B (logic frames), got {spec!r}") from None
    if a < 0 or b < a:
        raise ValueError(f"--logic-window: need 0 <= A <= B, got {spec!r}")
    return a, b


def logic_window(rows: list[dict[str, float]], first: int, last: int) -> list[dict[str, float]]:
    if not rows or any("logic_frame" not in r for r in rows):   # load_frames checks every row
        raise ValueError("--logic-window needs the logic_frame column in every row of frames.csv (TEST-PLAN §5.3)")
    return [r for r in rows if first <= r["logic_frame"] <= last]


def logic_pace(rows: list[dict[str, float]], required: bool = False, preliminary: bool = False) -> dict[str, object]:
    """Logic ticks per wall-clock second over the window: (Δlogic_frame) / (Δt_ns).
    required: perf/load/soak/crc-cost run (TEST-PLAN §5.4.1) -- a pace that cannot be measured makes
    the run invalid, unless preliminary (then only a warning). A measured pace outside 30 +- 1 makes the run
    invalid (exit 2); with preliminary it is a warning too (TEST-PLAN §5.4.1, Лид, ревью #11 п. 2)."""
    lf = [(r["t_ns"], r["logic_frame"]) for r in rows if "logic_frame" in r]
    if len(lf) < 2 or lf[-1][0] <= lf[0][0]:
        if not lf:
            cause, why = "column_missing", "нет колонки logic_frame в frames.csv"
        elif len(lf) < 2:
            cause, why = "too_few_rows", f"в окне меньше двух строк с logic_frame ({len(lf)})"
        else:
            cause, why = "zero_interval", "нулевой интервал t_ns между первой и последней строкой окна"
        base = {"logic_hz": None, "cause": cause}
        if required and not preliminary:
            return {**base, "valid": False, "reason": PACE_NOT_VERIFIABLE,
                    "note": f"{why}: темп логики не проверить, прогон недействителен (TEST-PLAN §5.4.1)"}
        if required:
            return {**base, "valid": None, "reason": PACE_NOT_VERIFIABLE,
                    "warning": f"{PACE_NOT_VERIFIABLE}: {why}",
                    "note": f"{why}: темп логики не проверен; предварительный прогон, в приёмку не идёт"}
        return {**base, "valid": None, "note": f"{why}: темп логики не проверен"}
    hz = (lf[-1][1] - lf[0][1]) / ((lf[-1][0] - lf[0][0]) / 1e9)
    valid = abs(hz - LOGIC_HZ) <= LOGIC_HZ_TOL
    out: dict[str, object] = {"logic_hz": round(hz, 3), "valid": valid, "note": ""}
    if valid:
        return out
    out["reason"] = PACE_OUT_OF_RANGE
    off = f"темп логики {hz:.2f} тик/с вне {LOGIC_HZ:.0f} ± {LOGIC_HZ_TOL:.0f}"
    if preliminary:
        # Лид (ревью #11 п. 2): до фикса E1-35 скирмиш с 2.22 идёт ≈ 38 Гц, а предварительные прогоны
        # нужны для FPS, памяти и нагрева -> только предупреждение, код по порогам (0/1), в приёмку не идёт
        out["valid"] = None
        out["warning"] = f"{PACE_OUT_OF_RANGE}: {off}"
        out["note"] = f"{off}: предварительный прогон (--preliminary), в приёмку не идёт (TEST-PLAN §5.4.1)"
    else:
        out["note"] = f"{off}: прогон недействителен (до фикса темпа E1-35 скирмиш с 2.22 идёт ≈ 38 Гц)"
    return out


def metrics(rows: list[dict[str, float]]) -> dict[str, float]:
    iv = sorted(r["present_interval_ms"] for r in rows)
    if not iv:
        raise ValueError("no frames in the measurement window (check --skip-s / --duration-s / --logic-window)")
    if len(rows) >= 2 and rows[-1]["t_ns"] <= rows[0]["t_ns"]:
        raise ValueError(f"all {len(rows)} frames of the window have the same t_ns ({rows[0]['t_ns']:.0f}): "
                         "zero time interval, the window length and the logic pace cannot be computed")
    total_ms = sum(iv)
    if not total_ms > 0:
        raise ValueError(f"sum of present_interval_ms over the {len(iv)} frames of the window is {total_ms:g} ms: "
                         "zero time interval, FPS cannot be computed")
    p99 = percentile(iv, 99)
    m = {
        "frames": len(iv),
        "window_s": round((rows[-1]["t_ns"] - rows[0]["t_ns"]) / 1e9, 1),
        "fps_avg": len(iv) / (total_ms / 1000.0),
        "p50_ms": percentile(iv, 50),
        "p95_ms": percentile(iv, 95),
        "p99_ms": p99,
        "fps_1pct_low": 1000.0 / p99 if p99 > 0 else math.nan,
        "jank_pct": 100.0 * sum(x > JANK_MS for x in iv) / len(iv),
        "big_jank_count": sum(x > BIG_JANK_MS for x in iv),
        "freeze_count": sum(x > FREEZE_MS for x in iv),
    }
    cpu = sorted(r["cpu_frame_ms"] for r in rows if "cpu_frame_ms" in r)
    m["cpu_p95_ms"] = percentile(cpu, 95) if cpu else math.nan
    for k in ("logic_ms", "client_ms", "gpu_ms"):
        vals = sorted(r[k] for r in rows if k in r)
        if vals:
            m[f"{k}_p50"] = percentile(vals, 50)
            m[f"{k}_p95"] = percentile(vals, 95)
    return m


def thermal_validity(rows: list[dict[str, float]], samples: Path | None) -> dict[str, object]:
    """Thermal status at the start and end of the measured window; validity of the run."""
    series: list[tuple[float, int]] = [(r["t_ns"], int(r["thermal_status"])) for r in rows if "thermal_status" in r]
    if not series and samples is not None:
        with samples.open(newline="", encoding="utf-8") as f:
            for r in csv.DictReader(f):
                if r.get("thermal_status") not in (None, "") and r.get("t_ns") not in (None, ""):
                    series.append((float(r["t_ns"]), int(float(r["thermal_status"]))))
    if not series:
        return {"thermal_start": None, "thermal_end": None, "valid": None,
                "note": "нет теплового статуса (колонка thermal_status): действительность прогона не проверена"}
    series.sort()
    t0 = rows[0]["t_ns"] if rows else series[0][0]
    before = [s for t, s in series if t <= t0]
    start = before[-1] if before else series[0][1]
    end = series[-1][1]
    valid = start < THERMAL_MODERATE
    note = "" if valid else f"прогон начат при {THERMAL_NAMES[start]} (>= MODERATE): недействителен, повторить после остывания"
    return {"thermal_start": THERMAL_NAMES[start], "thermal_end": THERMAL_NAMES[end], "valid": valid, "note": note}


def risks(m: dict[str, float]) -> list[str]:
    out = []
    cpu = m.get("cpu_p95_ms", math.nan)
    if not math.isnan(cpu) and cpu > CPU_RISK_MS:
        out.append(f"риск: p95 cpu_frame_ms = {cpu:.1f} мс > {CPU_RISK_MS:.0f} мс (нет запаса CPU, для Э1 не блокер)")
    lg = m.get("logic_ms_p95")
    if lg is not None and lg > LOGIC_BUDGET_MS:
        out.append(f"риск: p95 logic_ms = {lg:.1f} мс > бюджета тика логики {LOGIC_BUDGET_MS:.0f} мс")
    return out


def check(name: str, value: float) -> str:
    rule = THRESHOLDS.get(name)
    if rule is None or (isinstance(value, float) and math.isnan(value)):
        return "фикс."
    op, lim = rule
    ok = {">=": value >= lim, "<=": value <= lim, "==": value == lim}[op]
    return "✅" if ok else "❌"


def throttling(samples: Path, first_last_s: float = 60.0) -> dict[str, object]:
    """samples.csv: t_ns, thermal_status (0..6), [battery_temp_c, pss_kb, rss_kb, fps_1s ...]."""
    rows = []
    with samples.open(newline="", encoding="utf-8") as f:
        for r in csv.DictReader(f):
            rows.append({k: float(v) for k, v in r.items() if v not in (None, "")})
    out: dict[str, object] = {}
    if not rows:
        return out
    t0 = rows[0]["t_ns"]
    for key, name in (("pss_kb", "pss_peak_mb"), ("rss_kb", "rss_peak_mb")):
        vals = [r[key] for r in rows if key in r]
        if vals:
            out[name] = round(max(vals) / 1024.0, 1)
    if any("thermal_status" in r for r in rows):
        out["thermal_max"] = int(max(r.get("thermal_status", 0) for r in rows))
        for lvl, nm in ((1, "first_light_s"), (2, "first_moderate_s")):
            hit = next((r for r in rows if r.get("thermal_status", 0) >= lvl), None)
            out[nm] = round((hit["t_ns"] - t0) / 1e9) if hit else None
    temps = [r["battery_temp_c"] for r in rows if "battery_temp_c" in r]
    if temps:
        out["battery_temp_start_c"], out["battery_temp_max_c"] = temps[0], max(temps)
    fps = [(r["t_ns"], r["fps_1s"]) for r in rows if "fps_1s" in r]
    if fps and (fps[-1][0] - fps[0][0]) / 1e9 >= 3 * first_last_s:
        first = [v for t, v in fps if (t - fps[0][0]) / 1e9 < first_last_s]
        last = [v for t, v in fps if (fps[-1][0] - t) / 1e9 < first_last_s]
        drop = 100.0 * (1 - (sum(last) / len(last)) / (sum(first) / len(first)))
        out["fps_drop_first_last_pct"] = round(drop, 1)
        out["throttling"] = drop > 5.0 and out.get("thermal_max", 0) >= 2  # TEST-PLAN §5.6
    return out


def fmt(v: object) -> str:
    if isinstance(v, float):
        return "—" if math.isnan(v) else f"{v:.2f}"
    return "—" if v is None else str(v)


def to_markdown(m: dict, thr: dict, phone: dict | None, therm: dict | None = None,
                risk: list[str] | None = None, pace: dict | None = None,
                preliminary: bool = False) -> str:
    lines = ["| Метрика | Значение | Порог | Итог |" + (" На телефоне |" if phone else ""),
             "|---|---|---|---|" + ("---|" if phone else "")]
    for k in ("frames", "window_s", "fps_avg", "p50_ms", "p95_ms", "p99_ms", "fps_1pct_low",
              "jank_pct", "big_jank_count", "freeze_count", "cpu_p95_ms"):
        rule = THRESHOLDS.get(k)
        lim = f"{rule[0]} {rule[1]}" if rule else "—"
        res = check(k, m[k]) if k in THRESHOLDS else ""
        row = f"| {LABELS[k]} | {fmt(m[k])} | {lim} | {res} |"
        if phone:
            pv = phone.get(k)
            mark = "" if pv is None or not isinstance(pv, (int, float)) or abs(pv - m[k]) <= max(0.02 * abs(m[k]), 0.05) else " ⚠ расходится"
            row += f" {fmt(pv)}{mark} |"
        lines.append(row)
    for k in sorted(x for x in m if x.endswith(("_p50", "_p95"))):
        lines.append(f"| {k} | {fmt(m[k])} | — | фикс. |" + (" — |" if phone else ""))
    if therm:
        lines += ["", f"Тепловой статус: старт {fmt(therm['thermal_start'])}, конец {fmt(therm['thermal_end'])}; "
                  f"прогон {'действителен' if therm['valid'] else ('НЕДЕЙСТВИТЕЛЕН' if therm['valid'] is False else 'не проверен')}"
                  + (f" ({therm['note']})" if therm.get("note") else "")]
    if pace:
        lines += ["", f"Темп логики: {fmt(pace['logic_hz'])} тик/с (норма {LOGIC_HZ:.0f} ± {LOGIC_HZ_TOL:.0f}); "
                  f"прогон {'действителен' if pace['valid'] else ('НЕДЕЙСТВИТЕЛЕН' if pace['valid'] is False else ('⚠ предварительный' if pace.get('warning') else 'не проверен'))}"
                  + (f" ({pace['note']})" if pace.get("note") else "")]
    if preliminary:
        lines += ["", "**Предварительно:** замер по реальному времени до фикса темпа логики (E1-35), "
                  "в вердикт критерия не идёт (TEST-PLAN §5.4)"]
    for r in risk or []:
        lines.append(f"- ⚠ {r}")
    if thr:
        lines += ["", "| Память и нагрев | Значение |", "|---|---|"]
        lines += [f"| {k} | {fmt(v)} |" for k, v in thr.items()]
    return "\n".join(lines) + "\n"


def synthetic(path: Path, seed: int, seconds: int, jank_every: int, thermal_start: int | None = None,
              thermal_end: int | None = None, cpu_ms: float = 18.0, logic_hz: float = LOGIC_HZ) -> None:
    """Fake 30 FPS run: Swappy-like intervals around 33.3 ms, a jank every N frames.
    logic_hz: wall-clock logic pace for the logic_frame column (38 = 2.22 skirmish before E1-35)."""
    rnd = random.Random(seed)
    t = 0
    with path.open("w", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        cols = ["frame", "logic_frame", "t_ns", "present_interval_ms", "cpu_frame_ms", "logic_ms",
                "client_ms", "gpu_ms", "swappy_late"]
        if thermal_start is not None:
            cols.append("thermal_status")
        w.writerow(cols)
        n = seconds * 30
        for i in range(n):
            iv = T_MS + rnd.gauss(0, 0.6)
            late = 0
            if jank_every and i and i % jank_every == 0:
                iv, late = 2 * T_MS, 1
            cpu = max(5.0, rnd.gauss(cpu_ms, 3))
            lfr = i if logic_hz == LOGIC_HZ else int(t * logic_hz / 1e9)
            row = [i, lfr, t, f"{iv:.3f}", f"{cpu:.3f}", f"{cpu * 0.4:.3f}", f"{cpu * 0.6:.3f}", "", late]
            if thermal_start is not None:
                end = thermal_start if thermal_end is None else thermal_end
                row.append(thermal_start if i < n // 2 else end)
            w.writerow(row)
            t += int(iv * 1e6)


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("frames", nargs="?")
    ap.add_argument("--samples")
    ap.add_argument("--summary", help="perf/summary.json computed on the phone, to cross-check")
    ap.add_argument("--skip-s", type=float, default=60.0, help="warm-up to skip (TEST-PLAN §5.3: 60 s)")
    ap.add_argument("--duration-s", type=float, default=300.0, help="measurement window (300 s); 0 = to the end")
    ap.add_argument("--json")
    ap.add_argument("--md")
    ap.add_argument("--synthetic", help="write a synthetic frames.csv here and exit")
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--seconds", type=int, default=360)
    ap.add_argument("--jank-every", type=int, default=0)
    ap.add_argument("--thermal-start", type=int, help="synthetic: thermal_status column, first half")
    ap.add_argument("--thermal-end", type=int, help="synthetic: thermal_status, second half")
    ap.add_argument("--cpu-ms", type=float, default=18.0, help="synthetic: mean cpu_frame_ms")
    ap.add_argument("--logic-hz", type=float, default=LOGIC_HZ, help="synthetic: logic ticks per wall-clock second")
    ap.add_argument("--logic-window", help="window by logic frame, inclusive, e.g. 1801-10800 "
                    "(overrides --skip-s/--duration-s; TEST-PLAN §5.4 until the E1-35 pace fix)")
    ap.add_argument("--preliminary", action="store_true",
                    help="mark wall-clock results as preliminary (before the E1-35 logic pace fix); "
                         "a pace outside 30 +- 1 or a missing logic_frame column is then only a warning (exit 0/1)")
    ap.add_argument("--run-kind", choices=PACE_RUN_KINDS + ("other",), default="perf",
                    help="scenario of the run; for perf/load/soak/crc-cost (default perf) the logic pace "
                         "must be verifiable (TEST-PLAN §5.4.1)")
    a = ap.parse_args(argv)
    if a.synthetic:
        synthetic(Path(a.synthetic), a.seed, a.seconds, a.jank_every, a.thermal_start, a.thermal_end, a.cpu_ms,
                  a.logic_hz)
        return 0
    if not a.frames:
        ap.error("frames.csv is required")
    try:
        allrows = load_frames(Path(a.frames))
        if a.logic_window:
            rows = logic_window(allrows, *parse_logic_window(a.logic_window))
        else:
            rows = window(allrows, a.skip_s, a.duration_s or None)
        m = metrics(rows)
    except (OSError, ValueError, KeyError, ZeroDivisionError) as e:   # all bad input: exit 4, never a traceback
        print(f"error: bad input: {e}", file=sys.stderr)
        return 4
    thr = throttling(Path(a.samples)) if a.samples else {}
    phone = json.loads(Path(a.summary).read_text(encoding="utf-8")) if a.summary else None
    therm = thermal_validity(rows, Path(a.samples) if a.samples else None)
    pace = logic_pace(rows, required=a.run_kind in PACE_RUN_KINDS, preliminary=a.preliminary)
    if pace.get("warning"):
        print(f"warning: {pace['warning']} (--preliminary: run not invalid, not for acceptance)", file=sys.stderr)
    risk = risks(m)
    md = to_markdown(m, thr, phone, therm, risk, pace, a.preliminary)
    print(md, end="")
    verdict = all(check(k, m[k]) != "❌" for k in THRESHOLDS)
    if a.md:
        Path(a.md).write_text(md, encoding="utf-8")
    if a.json:
        valid = False if False in (therm["valid"], pace["valid"]) else (
            None if None in (therm["valid"], pace["valid"]) else True)
        Path(a.json).write_text(json.dumps({"metrics": m, "thermal_memory": thr, "thermal": therm, "pace": pace,
                                            "risks": risk, "valid": valid, "preliminary": a.preliminary,
                                            "run_kind": a.run_kind,
                                            "logic_window": a.logic_window, "pass": verdict},
                                           ensure_ascii=False, indent=2, default=str), encoding="utf-8")
    if therm["valid"] is False or pace["valid"] is False:
        pace_why = {"column_missing": PACE_NOT_VERIFIABLE + ": нет колонки logic_frame",
                    "too_few_rows": PACE_NOT_VERIFIABLE + ": в окне меньше двух строк с logic_frame",
                    "zero_interval": PACE_NOT_VERIFIABLE + ": нулевой интервал t_ns в окне",
                    }.get(pace.get("cause"), f"темп логики {fmt(pace['logic_hz'])} тик/с вне 30 ± 1")
        why = [w for w, bad in (("начат при тепловом статусе >= MODERATE", therm["valid"] is False),
                                (pace_why, pace["valid"] is False)) if bad]
        print(f"\nИтог: прогон НЕДЕЙСТВИТЕЛЕН ({'; '.join(why)}), повторить")
        return 2
    print(f"\nИтог: {'прошло' if verdict else 'НЕ прошло'} (пороги TEST-PLAN §5.4, утверждены Лидом)"
          + (f"; рисков: {len(risk)}" if risk else "")
          + ("; ПРЕДВАРИТЕЛЬНО (до фикса темпа E1-35)" if a.preliminary else ""))
    return 0 if verdict else 1


if __name__ == "__main__":
    sys.exit(main())
