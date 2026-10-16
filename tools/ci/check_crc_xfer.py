#!/usr/bin/env python3
"""E1-49: no ::crc(Xfer*) body may call Snapshot::xfer.

Walks function bodies by braces. No compile. A call is a violation when the
callee is exactly xfer( with any argument name: xfer(xfer), xfer(x),
this->xfer(...), (*this).xfer(...), Class::xfer(...).

Not a violation: xfer->..., xferSnapshot, and *.xfer(...) / ptr->xfer(...)
on a foreign object (bit flags).

Allowlist: only AIUpdateInterface::crc in AIUpdate.cpp, the bare xfer(x)
around line 5086. engine/bfme/ has an empty allowlist; an entry there is
itself a failure.
"""

from __future__ import annotations

import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
SCAN_ROOTS = (
    "GeneralsMD/Code/GameEngine",
    "engine/bfme",
)
EXTENSIONS = {".cpp", ".h", ".hpp", ".inl"}

# The one ZH exception. Line window is "around :5086", not a second call.
ALLOWLIST = (
    {
        "path_suffix": "GeneralsMD/Code/GameEngine/Source/GameLogic/Object/Update/AIUpdate.cpp",
        "function": "AIUpdateInterface::crc",
        "line_lo": 5060,
        "line_hi": 5110,
    },
)


def tokenize(text: str) -> list[tuple[str, str, int]]:
    tokens: list[tuple[str, str, int]] = []
    i = 0
    n = len(text)
    line = 1
    while i < n:
        c = text[i]
        if c == "\n":
            line += 1
            i += 1
            continue
        if c.isspace():
            i += 1
            continue
        if text.startswith("//", i):
            while i < n and text[i] != "\n":
                i += 1
            continue
        if text.startswith("/*", i):
            i += 2
            while i < n and not text.startswith("*/", i):
                if text[i] == "\n":
                    line += 1
                i += 1
            i = min(n, i + 2)
            continue
        if c == '"':
            i += 1
            while i < n and text[i] != '"':
                if text[i] == "\\":
                    i += 2
                    continue
                if text[i] == "\n":
                    line += 1
                i += 1
            i = min(n, i + 1)
            continue
        if c == "'":
            i += 1
            while i < n and text[i] != "'":
                if text[i] == "\\":
                    i += 2
                    continue
                if text[i] == "\n":
                    line += 1
                i += 1
            i = min(n, i + 1)
            continue
        if c.isalpha() or c == "_":
            j = i + 1
            while j < n and (text[j].isalnum() or text[j] == "_"):
                j += 1
            tokens.append(("ident", text[i:j], line))
            i = j
            continue
        if text.startswith("->", i) or text.startswith("::", i):
            tokens.append(("op", text[i : i + 2], line))
            i += 2
            continue
        tokens.append(("op", c, line))
        i += 1
    return tokens


def _matching_paren(tokens: list[tuple[str, str, int]], open_at: int) -> int:
    depth = 0
    for i in range(open_at, len(tokens)):
        if tokens[i][1] == "(":
            depth += 1
        elif tokens[i][1] == ")":
            depth -= 1
            if depth == 0:
                return i
    return -1


def _matching_brace(tokens: list[tuple[str, str, int]], open_at: int) -> int:
    depth = 0
    for i in range(open_at, len(tokens)):
        if tokens[i][1] == "{":
            depth += 1
        elif tokens[i][1] == "}":
            depth -= 1
            if depth == 0:
                return i
    return -1


def _is_this_receiver(tokens: list[tuple[str, str, int]], op_at: int) -> bool:
    if op_at <= 0:
        return False
    prev = tokens[op_at - 1]
    if prev[0] == "ident" and prev[1] == "this":
        return True
    if prev[1] != ")":
        return False
    depth = 0
    k = op_at - 1
    while k >= 0:
        if tokens[k][1] == ")":
            depth += 1
        elif tokens[k][1] == "(":
            depth -= 1
            if depth == 0:
                inner = [t[1] for t in tokens[k + 1 : op_at - 1]]
                return inner == ["*", "this"]
        k -= 1
    return False


def _xfer_param(tokens: list[tuple[str, str, int]], start: int, end: int) -> str | None:
    """Xfer * name inside the parameter list, or None."""
    i = start
    while i < end - 2:
        if tokens[i][0] == "ident" and tokens[i][1] == "Xfer" and tokens[i + 1][1] == "*":
            j = i + 2
            if j < end and tokens[j][0] == "ident":
                return tokens[j][1]
        i += 1
    return None


def violations_in_text(path: str, text: str) -> tuple[list[str], int]:
    """Return (violation messages, number of ::crc(Xfer*) definitions)."""
    tokens = tokenize(text)
    found = 0
    out: list[str] = []
    i = 0
    while i < len(tokens) - 3:
        if not (
            tokens[i][0] == "ident"
            and tokens[i + 1][1] == "::"
            and tokens[i + 2][1] == "crc"
            and tokens[i + 3][1] == "("
        ):
            i += 1
            continue
        class_name = tokens[i][1]
        function = f"{class_name}::crc"
        paren_end = _matching_paren(tokens, i + 3)
        if paren_end < 0:
            i += 1
            continue
        param = _xfer_param(tokens, i + 4, paren_end)
        j = paren_end + 1
        while j < len(tokens) and tokens[j][1] in {"const", "override", "final", "volatile", "noexcept", "&", "&&"}:
            j += 1
        if j >= len(tokens) or tokens[j][1] != "{":
            i = paren_end + 1
            continue
        if param is None:
            i = paren_end + 1
            continue
        brace_end = _matching_brace(tokens, j)
        if brace_end < 0:
            i = j + 1
            continue
        found += 1
        body = tokens[j + 1 : brace_end]
        for b in range(len(body) - 1):
            if body[b][0] != "ident" or body[b][1] != "xfer" or body[b + 1][1] != "(":
                continue
            prev = body[b - 1][1] if b else ""
            call_end = _matching_paren(body, b + 1)
            arg_tokens = body[b + 2 : call_end] if call_end > b + 1 else []
            kind = "bare"
            if prev in {".", "->"}:
                if _is_this_receiver(body, b - 1):
                    kind = "this"
                else:
                    continue
            elif prev == "::":
                kind = "qualified"
            line = body[b][2]
            rendered = _render_call(body, b, call_end)
            out.append(f"{path}:{line}: {function} calls Snapshot::xfer ({kind}) {rendered} param={param}")
        i = brace_end + 1
    return out, found


def _render_call(body: list[tuple[str, str, int]], callee: int, end: int) -> str:
    if end < 0:
        return "xfer("
    parts = []
    start = callee
    if callee >= 2 and body[callee - 1][1] in {".", "->", "::"}:
        start = callee - 2
    for t in body[start : end + 1]:
        parts.append(t[1])
    return "".join(parts) if False else " ".join(t[1] for t in body[start : end + 1])


def apply_allowlist(path: str, messages: list[str]) -> list[str]:
    rel = path.replace("\\", "/")
    if "/engine/bfme/" in f"/{rel}" or rel.startswith("engine/bfme/"):
        return messages
    # One bare xfer(<param>) in the window. A second call in the same
    # function is still a violation.
    used = [False] * len(ALLOWLIST)
    kept: list[str] = []
    for msg in messages:
        allowed = False
        for i, entry in enumerate(ALLOWLIST):
            if used[i]:
                continue
            if not rel.endswith(entry["path_suffix"]):
                continue
            if entry["function"] not in msg:
                continue
            # only the bare call whose single argument is the parameter name
            if "(bare)" not in msg:
                continue
            line = int(msg.split(":", 2)[1])
            if not (entry["line_lo"] <= line <= entry["line_hi"]):
                continue
            # "xfer ( x )" with the parameter. The message ends with param=<name>
            # and the rendered call is "xfer ( <arg> )".
            param = msg.rsplit("param=", 1)[-1].strip()
            rendered = msg.split(" (bare) ", 1)[-1]
            rendered = rendered.rsplit(" param=", 1)[0].strip()
            if rendered == f"xfer ( {param} )":
                allowed = True
                used[i] = True
                break
        if not allowed:
            kept.append(msg)
    return kept


def allowlist_matched(path: str, raw_messages: list[str]) -> bool:
    rel = path.replace("\\", "/")
    for entry in ALLOWLIST:
        if not rel.endswith(entry["path_suffix"]):
            continue
        for msg in raw_messages:
            if entry["function"] not in msg or "(bare)" not in msg:
                continue
            line = int(msg.split(":", 2)[1])
            if entry["line_lo"] <= line <= entry["line_hi"]:
                param = msg.rsplit("param=", 1)[-1].strip()
                rendered = msg.split(" (bare) ", 1)[-1].rsplit(" param=", 1)[0].strip()
                if rendered == f"xfer ( {param} )":
                    return True
        return False
    return True


def iter_sources(root: Path) -> list[Path]:
    files: list[Path] = []
    for rel in SCAN_ROOTS:
        base = root / rel
        if not base.exists():
            continue
        for path in base.rglob("*"):
            if path.suffix in EXTENSIONS and path.is_file():
                files.append(path)
    return files


def scan_repo(root: Path) -> tuple[list[str], int]:
    problems: list[str] = []
    definitions = 0
    seen_allow = {id(e): False for e in ALLOWLIST}
    for path in iter_sources(root):
        text = path.read_text(encoding="utf-8", errors="replace")
        rel = str(path.relative_to(root))
        raw, count = violations_in_text(rel, text)
        definitions += count
        if any(rel.endswith(e["path_suffix"]) for e in ALLOWLIST):
            if allowlist_matched(rel, raw):
                for e in ALLOWLIST:
                    if rel.endswith(e["path_suffix"]):
                        seen_allow[id(e)] = True
        problems.extend(apply_allowlist(rel, raw))
    for entry in ALLOWLIST:
        if not seen_allow[id(entry)]:
            problems.append(
                f"allowlist entry not found: {entry['function']} in {entry['path_suffix']} "
                f"(expected bare xfer(<param>) around line 5086)"
            )
    return problems, definitions


def self_test() -> int:
    cases = []

    def add(name: str, text: str, expect_violation: bool, path: str = "unit.cpp"):
        cases.append((name, text, expect_violation, path))

    body = """
void Foo::crc( Xfer *xfer )
{{
    {inner}
}}
void Foo::xfer( Xfer *xfer )
{{
    xfer(xfer);
}}
"""
    add("bare xfer(xfer)", body.format(inner="xfer(xfer);"), True)
    add("bare xfer(x)", body.format(inner="xfer(x);"), True)
    add("this->xfer", body.format(inner="this->xfer(xfer);"), True)
    add("star this", body.format(inner="(*this).xfer(x);"), True)
    add("qualified", body.format(inner="UpdateModule::xfer(xfer);"), True)
    add("foreign dot", body.format(inner="m_status.xfer(xfer);"), False)
    add("foreign arrow", body.format(inner="flags->xfer(xfer);"), False)
    add("xfer arrow method", body.format(inner="xfer->xferInt(&a);"), False)
    add("xferSnapshot", body.format(inner="xferSnapshot(xfer);"), False)
    add("comment", body.format(inner="// xfer(xfer)\n xfer->xferInt(&a);"), False)
    add("string", body.format(inner='const char* s = "xfer(xfer)";'), False)
    add("only in xfer()", "void Foo::xfer( Xfer *xfer ) { xfer(xfer); }\n", False)

    # The exception is the call around line 5086, so the fixture has to land there.
    ai = ("\n" * 5084) + """void AIUpdateInterface::crc( Xfer *x )
{
    UpdateModule::crc( x );
    xfer(x);
}
"""
    raw, _ = violations_in_text(ALLOWLIST[0]["path_suffix"], ai)
    if apply_allowlist(ALLOWLIST[0]["path_suffix"], raw):
        print("SELF-TEST FAIL: the AIUpdate exception was not allowed")
        return 1
    if not allowlist_matched(ALLOWLIST[0]["path_suffix"], raw):
        print("SELF-TEST FAIL: allowlist did not match AIUpdate")
        return 1
    extra = ai.replace("xfer(x);", "xfer(x);\n    xfer(x);")
    raw, _ = violations_in_text(ALLOWLIST[0]["path_suffix"], extra)
    if not apply_allowlist(ALLOWLIST[0]["path_suffix"], raw):
        print("SELF-TEST FAIL: a second xfer() in AIUpdate must fail")
        return 1

    bfme = "engine/bfme/Foo.cpp"
    raw, _ = violations_in_text(bfme, body.format(inner="xfer(x);"))
    if not apply_allowlist(bfme, raw):
        print("SELF-TEST FAIL: engine/bfme allowlist must stay empty")
        return 1

    for name, text, expect, path in cases:
        raw, _ = violations_in_text(path, text)
        got = apply_allowlist(path, raw)
        if expect and not got:
            print(f"SELF-TEST FAIL: {name} should be a violation")
            return 1
        if not expect and got:
            print(f"SELF-TEST FAIL: {name} should be clean: {got}")
            return 1
    print(f"self-test ok ({len(cases) + 2} cases)")
    return 0


def main() -> int:
    if "--self-test" in sys.argv:
        return self_test()
    root = REPO
    problems, definitions = scan_repo(root)
    print(f"crc definitions: {definitions}")
    if problems:
        print(f"FAIL: {len(problems)}")
        for msg in problems:
            print(msg)
        return 1
    print("ok: no ::crc(Xfer*) calls Snapshot::xfer, except AIUpdateInterface")
    return 0


if __name__ == "__main__":
    sys.exit(main())
