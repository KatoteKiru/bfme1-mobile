#!/usr/bin/env bash
# BFME: E1-04 (QA §4.0). Запрещённые литералы:
#   MYSOREZ, raw.githubusercontent, GeneralsOnline, playgenerals.online
#
#   tools/ci/check-no-upstream-urls.sh            # исходники, которые попадают в APK
#   tools/ci/check-no-upstream-urls.sh <app.apk>  # то же плюс dex / .so / resources.arsc / AndroidManifest.xml
#
# Java/XML: комментарии отбрасываются (в dex и resources.arsc их нет).
# Два исходника онлайн-аккаунта исключены из сборки (android/app/build.gradle sourceSets)
# и из этой проверки: их имена классов иначе живут только на диске.
# C/C++: GeneralsOnline ищется в строковых литералах, не в #include и не в комментариях
# (путь каталога GeneralsOnline/ в include в .so не попадает). Остальные три литерала
# ищутся по всему файлу.
# В APK ищутся и UTF-8, и UTF-16LE: пул resources.arsc и бинарный манифест хранят строки в UTF-16.
set -euo pipefail
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$REPO"

python3 - "$REPO" "${1:-}" << 'PY'
import re
import sys
import zipfile
from pathlib import Path

repo = Path(sys.argv[1])
apk = sys.argv[2] if len(sys.argv) > 2 and sys.argv[2] else ""
needles = ("MYSOREZ", "raw.githubusercontent", "GeneralsOnline", "playgenerals.online")
other = ("MYSOREZ", "raw.githubusercontent", "playgenerals.online")
skip_java = {
    repo / "android/app/src/main/java/com/generalsx/zerohour/GeneralsOnlineActivity.java",
    repo / "android/app/src/main/java/com/generalsx/zerohour/GeneralsOnlineSession.java",
}
fail = False

def report(where, hits):
    global fail
    fail = True
    print(f"FAIL: {where}")
    for h in hits[:20]:
        print(h)

def strip_c_comments(text):
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    text = re.sub(r"//.*?$", "", text, flags=re.M)
    return text

def strip_xml_comments(text):
    return re.sub(r"<!--.*?-->", "", text, flags=re.S)

def java_xml_hits(path: Path):
    text = path.read_text(encoding="utf-8", errors="replace")
    if path.suffix == ".xml":
        text = strip_xml_comments(text)
    else:
        text = strip_c_comments(text)
    hits = []
    for i, line in enumerate(text.splitlines(), 1):
        if any(n in line for n in needles):
            hits.append(f"  {path.relative_to(repo)}:{i}: {line.strip()[:200]}")
    return hits

string_re = re.compile(r'(?:u8|u|U|L)?"(?:\\.|[^"\\])*"')

def cpp_hits(path: Path):
    text = path.read_text(encoding="utf-8", errors="replace")
    hits = []
    for i, line in enumerate(text.splitlines(), 1):
        if any(n in line for n in other):
            hits.append(f"  {path.relative_to(repo)}:{i}: {line.strip()[:200]}")
        if re.match(r"\s*#\s*include\b", line):
            continue
        for m in string_re.finditer(line):
            if "GeneralsOnline" in m.group(0):
                hits.append(f"  {path.relative_to(repo)}:{i}: {m.group(0)[:200]}")
    return hits

print("E1-04: исходники (android/app/src без исключённых классов; GeneralsMD; Core)")
src = repo / "android/app/src"
for p in src.rglob("*"):
    if not p.is_file():
        continue
    if p.suffix not in {".java", ".kt", ".xml"}:
        continue
    if p.resolve() in skip_java:
        continue
    hits = java_xml_hits(p)
    if hits:
        report(p.relative_to(repo), hits)

for base in ("GeneralsMD", "Core"):
    for p in (repo / base).rglob("*"):
        if not p.is_file() or p.suffix not in {".cpp", ".h", ".hpp", ".c", ".cc"}:
            continue
        hits = cpp_hits(p)
        if hits:
            report(p.relative_to(repo), hits)

if apk:
    apk_path = Path(apk)
    if not apk_path.is_file():
        print(f"ОШИБКА: APK не найден: {apk}")
        sys.exit(1)
    print(f"E1-04: APK {apk} (classes*.dex, *.so, resources.arsc, AndroidManifest.xml; UTF-8 и UTF-16LE)")
    encoded = [(n, n.encode("utf-8"), n.encode("utf-16le")) for n in needles]
    with zipfile.ZipFile(apk_path) as zf:
        names = [
            n for n in zf.namelist()
            if n == "AndroidManifest.xml"
            or n == "resources.arsc"
            or n.startswith("classes") and n.endswith(".dex")
            or n.endswith(".so")
        ]
        for name in names:
            data = zf.read(name)
            found = []
            for label, utf8, utf16 in encoded:
                if utf8 in data or utf16 in data:
                    found.append(label)
            if found:
                report(name, [f"  {', '.join(found)}"])

if fail:
    print("ОШИБКА: найдены MYSOREZ, raw.githubusercontent, GeneralsOnline или playgenerals.online")
    sys.exit(1)
print("E1-04 ok: MYSOREZ, raw.githubusercontent, GeneralsOnline и playgenerals.online не найдены")
PY
