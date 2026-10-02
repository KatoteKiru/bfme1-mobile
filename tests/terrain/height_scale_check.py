#!/usr/bin/env python3
# BFME1 Mobile QA: E1-25 heights 8 -> 16 bit, fallback check without ZH data
# (docs/REPLAY-TESTS.md §9.2, docs/formats/BFME1_vs_ZH.md §2.5).
#
# Old (ZH):  world = float(h8)  * 0.625f        (MAP_HEIGHT_SCALE = MAP_XY_FACTOR/16)
# New (E1-25): h16 = h8 << 4 at load, world = float(h16) * 0.0390625f (one scale engine-wide)
#
# float32 is emulated exactly: one IEEE op on float32 inputs computed in binary64 and rounded
# once to float32 equals the float32 op (53 >= 2*24+2, so double rounding is innocuous for
# + - * /). No third-party deps.
#
# Checks:
#   load      all 256 h8: old and new world height bit-for-bit equal          (must pass)
#   interp    bilinear ground height on random grids, raw-first and world-first (must pass)
#   flatten   TerrainLogic::flattenTerrain re-quantises an AVERAGE world height back to raw
#             (called by DozerAIUpdate/WorkerAIUpdate/ObjectCreationList on building placement).
#             Upstream (fork TerrainLogic.cpp:2709, 2713, 2798): floor(0.5f + avg/MAP_HEIGHT_SCALE).
#             Naive port with the new step 0.0390625 is NOT (old << 4) in general (QA model: 18748
#             of 20000) -- kept as `naive` to show why the fix is needed. FIXED in bfme 4db8b77 (PR #4,
#             TerrainLogic.cpp:2710, 2714, 2800): floor(0.5f + avg/MAP_HEIGHT_SCALE_ZH) * 16 and
#             floor(center/MAP_HEIGHT_SCALE_ZH) * 16 on ALL maps (Лид: one rule in E1). Must match.
#   crater    TerrainLogic::createCraterInTerrain, upstream MAX(1, raw8 - disp); 4db8b77
#             (TerrainLogic.cpp:2897-2898) MAX(1, raw16/16.0f - disp) * 16. Must match.
#   seismic   W3DTerrainVisual::updateSeismicSimulations, upstream (Int)(h8 + speed), clamp 255;
#             4db8b77 (W3DTerrainVisual.cpp:408, 420) (Int)(h16/16.0f + speed) * 16, clamp 0xFFFF.
#             Equal except when the old clamp to 255 fires (client height map only, and the whole
#             block is under #ifdef DO_SEISMIC_SIMULATIONS, never defined): reported, not a failure.
#   These are models of the formulas, not the real TerrainLogic (that needs the E1-43a headless
#   build, REPLAY-TESTS §9.2).
#
#   height_scale_check.py [--samples N] [--seed S] [--json out.json]
# Exit: 0 load, interp, flatten (4db8b77) and crater (4db8b77) bit-exact; 1 otherwise
# (mismatching inputs are printed / written to --json).
from __future__ import annotations

import argparse
import json
import math
import random
import struct
import sys

OLD_SCALE = 0.625        # 5/8
NEW_SCALE = 0.0390625    # 5/128 = 0.625/16


def f32(x: float) -> float:
    return struct.unpack("<f", struct.pack("<f", x))[0]


def bits(x: float) -> int:
    return struct.unpack("<I", struct.pack("<f", x))[0]


def mul(a: float, b: float) -> float:
    return f32(f32(a) * f32(b))


def add(a: float, b: float) -> float:
    return f32(f32(a) + f32(b))


def sub(a: float, b: float) -> float:
    return f32(f32(a) - f32(b))


def div(a: float, b: float) -> float:
    return f32(f32(a) / f32(b))


def floor_int(x: float) -> int:          # REAL_TO_INT_FLOOR
    return math.floor(x)


def check_load() -> list[int]:
    """h8 values whose old and new world heights differ in any bit (expected: none)."""
    return [h for h in range(256) if bits(mul(h, OLD_SCALE)) != bits(mul(h << 4, NEW_SCALE))]


def ground_height(raw: list[list[int]], scale: float, x: float, y: float, world_first: bool) -> float:
    """Bilinear height at cell coords (x, y) in float32; two plausible engine orders."""
    ix, iy = int(x), int(y)
    fx, fy = sub(x, ix), sub(y, iy)
    c = [raw[iy][ix], raw[iy][ix + 1], raw[iy + 1][ix], raw[iy + 1][ix + 1]]
    if world_first:
        c = [mul(v, scale) for v in c]
    top = add(c[0], mul(sub(c[1], c[0]), fx))
    bot = add(c[2], mul(sub(c[3], c[2]), fx))
    z = add(top, mul(sub(bot, top), fy))
    return z if world_first else mul(z, scale)


def check_interp(rnd: random.Random, samples: int) -> int:
    bad = 0
    for _ in range(samples):
        g8 = [[rnd.randrange(256) for _ in range(3)] for _ in range(3)]
        g16 = [[v << 4 for v in row] for row in g8]
        x, y = f32(rnd.uniform(0, 1.999)), f32(rnd.uniform(0, 1.999))
        for wf in (False, True):
            if bits(ground_height(g8, OLD_SCALE, x, y, wf)) != bits(ground_height(g16, NEW_SCALE, x, y, wf)):
                bad += 1
    return bad


def flatten_raw(avg: float, center: float, scale: float) -> int:
    """TerrainLogic::flattenTerrain: target raw height for the building footprint."""
    raw = floor_int(add(0.5, div(avg, scale)))
    centre_raw = floor_int(div(center, scale))
    return min(raw, centre_raw)


def flatten_raw_4db8b77(avg: float, center: float) -> int:
    """bfme 4db8b77 TerrainLogic.cpp:2710/2714: ZH step (MAP_HEIGHT_SCALE_ZH = 0.625f), then * 16."""
    raw = floor_int(add(0.5, div(avg, OLD_SCALE))) * 16
    centre_raw = floor_int(div(center, OLD_SCALE)) * 16
    return min(raw, centre_raw)


def crater_old(raw8: int, disp: float) -> int:
    """Upstream: Int targetHeight = MAX(1, getRawMapHeight() - displacementAmount) (float, truncated)."""
    return int(max(1.0, sub(float(raw8), disp)))


def crater_4db8b77(raw16: int, disp: float) -> int:
    """4db8b77 TerrainLogic.cpp:2897-2898: MAX(1, raw16 / (Real)16 - disp), then *= 16."""
    return int(max(1.0, sub(div(float(raw16), 16.0), disp))) * 16


def check_crater(rnd: random.Random, samples: int) -> dict[str, object]:
    bad = []
    n = 0
    for raw8 in range(256):
        for k in range(max(1, samples // 256)):
            radius = f32(rnd.uniform(1.0, 300.0))
            dist = f32(rnd.uniform(0.0, float(radius)))
            disp = mul(radius, sub(1.0, div(dist, radius)))
            if k < 4:                       # around integer steps of the displacement
                disp = f32(raw8 - 1 + (k - 1.5) * 1e-4)
            n += 1
            if crater_4db8b77(raw8 << 4, disp) != crater_old(raw8, disp) * 16:
                bad.append({"raw8": raw8, "disp": disp})
    return {"samples": n, "differ_4db8b77": len(bad), "examples": bad[:20]}


def seismic_old(h8: int, orig8: int, speed: float) -> int:
    """Upstream W3DTerrainVisual.cpp:407-421 (8ef2349): Int heightToUse = getHeight() + newSpeed."""
    use = int(add(float(h8), speed))
    if use <= orig8:
        return orig8
    return min(use, 255)


def seismic_4db8b77(h16: int, orig16: int, speed: float) -> int:
    """4db8b77 W3DTerrainVisual.cpp:408-421: (Int)(h16/(Real)16 + newSpeed)*16, clamp K_MAX_HEIGHT."""
    use = int(add(div(float(h16), 16.0), speed)) * 16
    if use <= orig16:
        return orig16
    return min(use, 0xFFFF)


def check_seismic(rnd: random.Random, samples: int) -> dict[str, object]:
    bad, clamp_only = [], True
    n = 0
    for h8 in range(256):
        for _ in range(max(1, samples // 256)):
            orig8 = rnd.randrange(256)
            speed = f32(rnd.uniform(-20.0, 20.0))
            n += 1
            old, new = seismic_old(h8, orig8, speed), seismic_4db8b77(h8 << 4, orig8 << 4, speed)
            if new != old * 16:
                bad.append({"h8": h8, "orig8": orig8, "speed": speed, "expected_raw16": old * 16,
                            "actual_raw16": new})
                clamp_only &= int(add(float(h8), speed)) > 255
    return {"samples": n, "differ_4db8b77": len(bad), "only_at_255_clamp": clamp_only, "examples": bad[:5]}


def check_flatten(rnd: random.Random, samples: int) -> dict[str, object]:
    diff = 0
    bad = []
    for _ in range(samples):
        g8 = [[rnd.randrange(256) for _ in range(4)] for _ in range(4)]
        g16 = [[v << 4 for v in row] for row in g8]
        pts = [(f32(rnd.uniform(0, 2.999)), f32(rnd.uniform(0, 2.999))) for _ in range(rnd.randrange(1, 9))]
        res = {}
        for name, g, s in (("old", g8, OLD_SCALE), ("new", g16, NEW_SCALE)):
            total = 0.0
            for x, y in pts:
                total = add(total, ground_height(g, s, x, y, False))
            avg = div(total, len(pts))
            centre = ground_height(g, s, pts[0][0], pts[0][1], False)
            res[name] = (avg, centre, s)
        old = flatten_raw(*res["old"])
        new = flatten_raw(*res["new"])
        if new != old << 4:
            diff += 1
        if flatten_raw_4db8b77(res["new"][0], res["new"][1]) != old << 4:
            bad.append({"avgHeight": res["new"][0], "center": res["new"][1]})
    # every rounding step of the ZH grid, just below / on / just above x.5 and x.0 (in ZH units)
    for h in range(256):
        for frac in (0.0, 0.5):
            for eps in (-1e-4, 0.0, 1e-4):
                avg = f32((h + frac + eps) * OLD_SCALE)
                for centre in (avg, f32(avg + OLD_SCALE)):
                    old = flatten_raw(avg, centre, OLD_SCALE)
                    if flatten_raw_4db8b77(avg, centre) != old << 4:
                        bad.append({"avgHeight": avg, "center": centre})
    return {"samples": samples, "differ_naive_new_step": diff, "differ_4db8b77": len(bad), "examples": bad[:20]}


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description="E1-25 height scale bit-exactness check")
    ap.add_argument("--samples", type=int, default=20000)
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--json")
    a = ap.parse_args(argv)
    rnd = random.Random(a.seed)
    load_bad = check_load()
    interp_bad = check_interp(rnd, a.samples)
    flat = check_flatten(rnd, a.samples)
    crater = check_crater(rnd, a.samples)
    seismic = check_seismic(rnd, a.samples)
    out = {"load_bad_h8": load_bad, "interp_bad": interp_bad, "flatten": flat, "crater": crater,
           "seismic": seismic}
    print(f"load:    {256 - len(load_bad)}/256 h8 values bit-exact" + (f"  BAD: {load_bad}" if load_bad else ""))
    print(f"interp:  {2 * a.samples - interp_bad}/{2 * a.samples} bilinear samples bit-exact")
    print(f"flatten: 4db8b77 formula != old<<4 in {flat['differ_4db8b77']} cases "
          f"({flat['samples']} random + 1536 step edges); naive new-step rounding (pre-fix) would differ in "
          f"{flat['differ_naive_new_step']}/{flat['samples']}")
    print(f"crater:  4db8b77 formula != old*16 in {crater['differ_4db8b77']}/{crater['samples']} cases")
    print(f"seismic: 4db8b77 formula != old*16 in {seismic['differ_4db8b77']}/{seismic['samples']} cases, "
          f"only where the upstream clamp to 255 fires: {seismic['only_at_255_clamp']} "
          f"(dead code: DO_SEISMIC_SIMULATIONS undefined; client height map)")
    for name, r in (("flatten", flat), ("crater", crater), ("seismic", seismic)):
        for ex in r["examples"]:
            print(f"  {name} mismatch: {ex}")
    if a.json:
        with open(a.json, "w", encoding="utf-8") as f:
            json.dump(out, f, indent=2)
    ok = not load_bad and interp_bad == 0 and flat["differ_4db8b77"] == 0 and crater["differ_4db8b77"] == 0
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
