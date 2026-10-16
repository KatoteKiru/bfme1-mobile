#!/usr/bin/env python3
"""E1-49: ban long, lroundf, lrintf, lround, lrint and long double in logic.

Comments and string literals are not hits. `long long` is not `long`
(it is 64 bits on both Windows and LP64). An explicit allowlist covers
ZH code this change does not rewrite. engine/bfme/ cannot be allowlisted.
Under GameMath, scan the public header and our wrappers only.
Vendored musl/ is upstream (libm.h uses long double) and is not scanned.
fp/ and include/fp.h are the logic-thread gate (#21), not this ban.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]

# Trees that are logic, plus the two shared headers ADR-0003 names for this ban.
SCAN_DIRS = (
    "GeneralsMD/Code/GameEngine/Source/GameLogic",
    "GeneralsMD/Code/GameEngine/Include/GameLogic",
    "Core/GameEngine/Include/GameLogic",
    "engine/bfme",
    "Core/Libraries/Source/GameMath/src",
)
SCAN_FILES = (
    "Core/Libraries/Include/Lib/BaseType.h",
    "Core/Libraries/Include/Lib/GameMathInt.h",
    "Core/Libraries/Source/GameMath/include/GameMath.h",
    "Core/Libraries/Source/WWVegas/WWMath/wwmath.h",
)
# One-level "quoted" includes from GameLogic that live in common / Lib headers.
INCLUDE_ROOTS = (
    "GeneralsMD/Code/GameEngine/Include",
    "Core/GameEngine/Include",
    "Core/Libraries/Include",
)
COMMON_MARKERS = ("/Include/Common/", "/Include/Lib/")

FUNCS = ("lroundf", "lrintf", "lround", "lrint")

# path suffix, substring of the offending line, why it stays.
# New lines in these files are still failures.
ALLOWLIST = (
    (
        "Core/Libraries/Include/Lib/BaseType.h",
        "__forceinline long fast_float2long_round(float f)",
        "ZH fast_float2long_round returns long; REAL_TO_INT_* must not change (ADR-0003 §2.7)",
    ),
    (
        "Core/Libraries/Include/Lib/BaseType.h",
        "long i;",
        "local in fast_float2long_round; same function, do not change",
    ),
    (
        "Core/Libraries/Include/Lib/BaseType.h",
        "i = lroundf(f);",
        "MSVC x86 branch of fast_float2long_round only (ADR-0003 §2.4)",
    ),
    (
        "Core/Libraries/Include/Lib/BaseType.h",
        "i = (long)(int)r;",
        "narrowing inside fast_float2long_round; the macro depends on it",
    ),
    (
        "GeneralsMD/Code/GameEngine/Source/GameLogic/Object/PartitionManager.cpp",
        "long s_countInClosestObjects",
        "existing ZH perf counters under DUMP_PERF_STATS (ADR-0003 §2.4)",
    ),
    (
        "Core/Libraries/Source/WWVegas/WWMath/wwmath.h",
        "Float_To_Long",
        "existing ZH Float_To_Long; new BFME code uses GameMath::*ToInt32",
    ),
    (
        "Core/Libraries/Source/WWVegas/WWMath/wwmath.h",
        "unsigned long * plong",
        "existing ZH exponent peel in wwmath.h, not a logic local",
    ),
    (
        "Core/Libraries/Source/WWVegas/WWMath/wwmath.h",
        "unsigned long exponent",
        "existing ZH exponent peel in wwmath.h",
    ),
    (
        "Core/Libraries/Source/WWVegas/WWMath/wwmath.h",
        "lrintf",
        "inside Float_To_Long, MSVC/x86 path; not a new call site",
    ),
    (
        "Core/Libraries/Source/WWVegas/WWMath/wwmath.h",
        "lrint",
        "inside Float_To_Long(double); not a new call site",
    ),
    (
        "Core/Libraries/Source/WWVegas/WWMath/wwmath.h",
        "long i;",
        "local in Float_To_Long(float); existing ZH",
    ),
    (
        "Core/Libraries/Source/WWVegas/WWMath/wwmath.h",
        "long retval;",
        "local in Float_To_Long(double); existing ZH",
    ),
    (
        "GeneralsMD/Code/GameEngine/Include/Common/BorderColors.h",
        "unsigned long m_borderColor",
        "existing ZH color table; not rewritten in E1-49",
    ),
    (
        "GeneralsMD/Code/GameEngine/Include/Common/BorderColors.h",
        "const long BORDER_COLORS_SIZE",
        "existing ZH array length; not rewritten in E1-49",
    ),
)


def blank_noncode(text: str) -> str:
    out = list(text)
    i = 0
    n = len(text)
    state = "code"
    while i < n:
        c = text[i]
        if state == "code":
            if text.startswith("//", i):
                while i < n and text[i] != "\n":
                    out[i] = " "
                    i += 1
                continue
            if text.startswith("/*", i):
                out[i] = out[i + 1] = " "
                i += 2
                state = "block"
                continue
            if c == '"':
                out[i] = " "
                i += 1
                state = "string"
                continue
            if c == "'":
                out[i] = " "
                i += 1
                state = "char"
                continue
            i += 1
        elif state == "block":
            if text.startswith("*/", i):
                out[i] = out[i + 1] = " "
                i += 2
                state = "code"
            else:
                if c != "\n":
                    out[i] = " "
                i += 1
        else:
            if c == "\\" and i + 1 < n:
                out[i] = out[i + 1] = " "
                i += 2
                continue
            end = '"' if state == "string" else "'"
            if c == end:
                out[i] = " "
                i += 1
                state = "code"
                continue
            if c != "\n":
                out[i] = " "
            i += 1
    return "".join(out)


def hits_in_text(text: str) -> list[tuple[int, str, str]]:
    """(line, kind, stripped line) for real type/function uses."""
    found: list[tuple[int, str, str]] = []
    for lineno, line in enumerate(blank_noncode(text).splitlines(), 1):
        tokens = re.findall(r"[A-Za-z_][A-Za-z0-9_]*", line)
        j = 0
        while j < len(tokens):
            tok = tokens[j]
            if tok in FUNCS:
                found.append((lineno, tok, line.strip()))
            elif tok == "long":
                nxt = tokens[j + 1] if j + 1 < len(tokens) else ""
                if nxt == "long":
                    j += 2
                    continue
                if nxt == "double":
                    found.append((lineno, "long double", line.strip()))
                else:
                    found.append((lineno, "long", line.strip()))
            j += 1
    return found


def game_math_skipped(rel: str) -> bool:
    """True for musl and the fp gate. Public GameMath.h and src/ are scanned."""
    rel = rel.replace("\\", "/")
    if "/GameMath/musl/" in f"/{rel}" or rel.startswith("Core/Libraries/Source/GameMath/musl/"):
        return True
    if "/GameMath/fp/" in f"/{rel}" or rel.startswith("Core/Libraries/Source/GameMath/fp/"):
        return True
    if rel.endswith("Core/Libraries/Source/GameMath/include/fp.h"):
        return True
    return False


def _is_bfme_logic(rel: str) -> bool:
    rel = rel.replace("\\", "/")
    return rel.startswith("engine/bfme/")


def allowed(rel: str, line: str) -> bool:
    if _is_bfme_logic(rel):
        return False
    for suffix, needle, _reason in ALLOWLIST:
        if rel.endswith(suffix) and needle in line:
            return True
    return False


def collect_files(root: Path) -> list[Path]:
    files: list[Path] = []
    for rel in SCAN_DIRS:
        base = root / rel
        if not base.exists():
            continue
        for path in base.rglob("*"):
            if not path.is_file() or path.suffix not in {".cpp", ".h", ".hpp", ".inl", ".c"}:
                continue
            rel_s = str(path.relative_to(root)).replace("\\", "/")
            if game_math_skipped(rel_s):
                continue
            files.append(path)
    for rel in SCAN_FILES:
        path = root / rel
        if path.is_file():
            files.append(path)
    # One-level common headers included by GameLogic.
    include_re = re.compile(r'^\s*#\s*include\s*"([^"]+)"', re.M)
    logic_dirs = [
        root / "GeneralsMD/Code/GameEngine/Source/GameLogic",
        root / "GeneralsMD/Code/GameEngine/Include/GameLogic",
    ]
    seen = {p.resolve() for p in files}
    for base in logic_dirs:
        if not base.exists():
            continue
        for src in base.rglob("*"):
            if not src.is_file() or src.suffix not in {".cpp", ".h", ".hpp", ".inl"}:
                continue
            text = src.read_text(encoding="utf-8", errors="replace")
            for name in include_re.findall(text):
                for inc_root in INCLUDE_ROOTS:
                    cand = root / inc_root / name
                    if not cand.is_file():
                        continue
                    rel = str(cand.relative_to(root)).replace("\\", "/")
                    if not any(mark in f"/{rel}" for mark in COMMON_MARKERS):
                        continue
                    if cand.resolve() not in seen:
                        seen.add(cand.resolve())
                        files.append(cand)
                    break
    return files


def scan(root: Path) -> tuple[list[str], list[tuple[str, str]]]:
    problems: list[str] = []
    used = [False] * len(ALLOWLIST)
    for path in collect_files(root):
        rel = str(path.relative_to(root)).replace("\\", "/")
        text = path.read_text(encoding="utf-8", errors="replace")
        for lineno, kind, line in hits_in_text(text):
            if allowed(rel, line):
                for i, (suffix, needle, _reason) in enumerate(ALLOWLIST):
                    if rel.endswith(suffix) and needle in line:
                        used[i] = True
                continue
            problems.append(f"{rel}:{lineno}: {kind}: {line}")
    for i, (suffix, needle, _reason) in enumerate(ALLOWLIST):
        if not used[i]:
            problems.append(f"allowlist unused: {suffix} :: {needle}")
    return problems, []


def self_test() -> int:
    samples = [
        ("// a long comment\n", []),
        ("const char* s = \"long time\";\n", []),
        ("/* how long it takes */\nint x;\n", []),
        ("long long n;\nunsigned long long u;\n", []),
        ("long n;\n", ["long"]),
        ("unsigned long n;\n", ["long"]),
        ("long double z;\n", ["long double"]),
        ("auto q = lroundf(1.0f);\n", ["lroundf"]),
        ("auto q = lrintf(1.0f);\n", ["lrintf"]),
    ]
    for text, kinds in samples:
        got = [k for _ln, k, _line in hits_in_text(text)]
        if got != kinds:
            print(f"SELF-TEST FAIL: {text!r} -> {got} expected {kinds}")
            return 1
    if allowed("engine/bfme/Foo.cpp", "long n;"):
        print("SELF-TEST FAIL: engine/bfme must not be allowlisted")
        return 1
    if not allowed(
        "Core/Libraries/Include/Lib/BaseType.h",
        "__forceinline long fast_float2long_round(float f)",
    ):
        print("SELF-TEST FAIL: BaseType exception missing")
        return 1
    if not game_math_skipped("Core/Libraries/Source/GameMath/fp/fp_thread.cpp"):
        print("SELF-TEST FAIL: fp/ must not be scanned")
        return 1
    if not game_math_skipped("Core/Libraries/Source/GameMath/include/fp.h"):
        print("SELF-TEST FAIL: fp.h must not be scanned")
        return 1
    if not game_math_skipped("Core/Libraries/Source/GameMath/musl/src/internal/libm.h"):
        print("SELF-TEST FAIL: musl must not be scanned")
        return 1
    if game_math_skipped("Core/Libraries/Source/GameMath/include/GameMath.h"):
        print("SELF-TEST FAIL: public GameMath.h must be scanned")
        return 1
    if game_math_skipped("GeneralsMD/Code/GameEngine/Source/GameLogic/Foo.cpp"):
        print("SELF-TEST FAIL: logic outside GameMath must be scanned")
        return 1
    print(f"self-test ok ({len(samples)} cases)")
    return 0


def main() -> int:
    if "--self-test" in sys.argv:
        return self_test()
    problems, _ = scan(REPO)
    if "--list" in sys.argv:
        for msg in problems:
            print(msg)
        return 0
    if problems:
        print(f"FAIL: {len(problems)}")
        for msg in problems:
            print(msg)
        return 1
    print("ok: no new long / lroundf / lrintf / long double in logic")
    return 0


if __name__ == "__main__":
    sys.exit(main())
