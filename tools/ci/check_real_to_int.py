#!/usr/bin/env python3
"""E1-49: REAL_TO_INT_FLOOR / CEIL in the ZH branch must stay as they are.

ADR-0003 §2.7. The check fails if those two macros are "fixed" to floorf/ceilf
or if the «не исправлять» note next to them disappears.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
HEADER = REPO / "Core/Libraries/Include/Lib/BaseType.h"

CEIL = "#define REAL_TO_INT_CEIL(x) (fast_float2long_round(fast_float_ceil(x)))"
FLOOR = "#define REAL_TO_INT_FLOOR(x) (fast_float2long_round(fast_float_floor(x)))"


def norm_define(line: str) -> str:
    line = line.split("//", 1)[0]
    return re.sub(r"\s+", " ", line).strip()


def zh_branch(text: str) -> str | None:
    """The #else of `#if RTS_GENERALS` that defines the two macros."""
    lines = text.splitlines()
    for i, line in enumerate(lines):
        if re.match(r"\s*#\s*if\s+RTS_GENERALS\b", line):
            depth = 1
            j = i + 1
            else_at = None
            while j < len(lines) and depth:
                s = lines[j].strip()
                if s.startswith("#if"):
                    depth += 1
                elif s.startswith("#endif"):
                    depth -= 1
                    if depth == 0:
                        end = j
                        break
                elif s.startswith("#else") and depth == 1 and else_at is None:
                    else_at = j
                j += 1
            else:
                return None
            if else_at is None:
                return None
            return "\n".join(lines[else_at:end])
    return None


def check_text(text: str) -> list[str]:
    problems: list[str] = []
    branch = zh_branch(text)
    if branch is None:
        return ["ZH #else branch of `#if RTS_GENERALS` not found"]
    defines = [norm_define(line) for line in branch.splitlines() if norm_define(line).startswith("#define REAL_TO_INT_")]
    if CEIL not in defines:
        problems.append(f"REAL_TO_INT_CEIL is not the ZH macro: {defines}")
    if FLOOR not in defines:
        problems.append(f"REAL_TO_INT_FLOOR is not the ZH macro: {defines}")
    # The marker sits on the macro lines (trailing comment) and in the §2.7 note.
    if "не исправлять" not in text:
        problems.append("missing «не исправлять»")
    if "ADR-0003 §2.7" not in text and "ADR-0003 2.7" not in text:
        # The comment uses "ADR-0003 §2.7".
        if "§2.7" not in text:
            problems.append("missing ADR-0003 §2.7 reference")
    if "GameMath::floorToInt32" not in text:
        problems.append("missing pointer to GameMath::floorToInt32")
    # Only the #define lines. The §2.7 note names the macros and
    # fast_float_floor/ceil in prose and must not grow a marker on every sentence.
    for line in text.splitlines():
        if not line.strip().startswith("#define"):
            continue
        if "REAL_TO_INT_CEIL" in line and "fast_float_ceil" in line and "не исправлять" not in line:
            problems.append("REAL_TO_INT_CEIL line lost «не исправлять»")
        if "REAL_TO_INT_FLOOR" in line and "fast_float_floor" in line and "не исправлять" not in line:
            problems.append("REAL_TO_INT_FLOOR line lost «не исправлять»")
    return problems


def self_test() -> int:
    good = """
#if RTS_GENERALS
#define REAL_TO_INT_CEIL(x) (fast_float2long_round(ceilf(x)))
#define REAL_TO_INT_FLOOR(x) (fast_float2long_round(floorf(x)))
#else
#define REAL_TO_INT_CEIL(x) (fast_float2long_round(fast_float_ceil(x))) // BFME: не исправлять
#define REAL_TO_INT_FLOOR(x) (fast_float2long_round(fast_float_floor(x))) // BFME: не исправлять
#endif
// BFME: не исправлять. ADR-0003 §2.7.
// REAL_TO_INT_FLOOR — это fast_float2long_round(fast_float_floor(x)).
// GameMath::floorToInt32
"""
    bad = good.replace("fast_float_ceil", "ceilf").replace("fast_float_floor", "floorf")
    unmarked = good.replace(" // BFME: не исправлять", "", 2)
    if check_text(good):
        print("SELF-TEST FAIL: good header rejected", check_text(good))
        return 1
    if not check_text(bad):
        print("SELF-TEST FAIL: fixed macros were accepted")
        return 1
    lost = check_text(unmarked)
    if not any("lost «не исправлять»" in msg for msg in lost):
        print("SELF-TEST FAIL: defines without the marker were accepted", lost)
        return 1
    print("self-test ok")
    return 0


def main() -> int:
    if "--self-test" in sys.argv:
        return self_test()
    problems = check_text(HEADER.read_text(encoding="utf-8"))
    if problems:
        print("FAIL")
        for msg in problems:
            print(msg)
        return 1
    print("ok: REAL_TO_INT_FLOOR / CEIL still the ZH macros, marked не исправлять")
    return 0


if __name__ == "__main__":
    sys.exit(main())
