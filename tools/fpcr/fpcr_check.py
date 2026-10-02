#!/usr/bin/env python3
# BFME1 Mobile: T5 из docs/fpcr-audit.md -- статическая проверка FP-режима в arm64 .so (CI, без зависимостей).
#
#   fpcr_check.py --baseline tools/fpcr/fpcr-baseline.json [--warn-only GLOB ...] lib1.so lib2.so ...
#
# Для каждой .so считает в исполняемых секциях инструкции MSR FPCR (запись), MRS FPCR (чтение),
# MSR FPSR (запись флагов) -- те же кодировки, что fpcr_scan.py (0xD51B4400/0xD53B4400/0xD51B4420 | Rt),
# и собирает импорты fe* (неопределённые символы .dynsym).
# Падение (код 1), если у библиотеки по сравнению с базой:
#   - есть хотя бы одна MSR FPCR, которой нет в базе (больше, чем в базе);
#   - появился импорт функции, меняющей FP-окружение (fesetenv, fesetround, feholdexcept, feupdateenv,
#     fesetexceptflag, feenableexcept, fedisableexcept), которого нет в базе;
#   - библиотеки нет в базе, и у неё есть MSR FPCR или такой импорт.
# Чтения (MRS FPCR) и MSR FPSR только печатаются (compiler-rt в libmain: __fe_getround / __fe_raise_inexact).
# Библиотеки, подходящие под --warn-only (драйверы, например Turnip), дают предупреждение вместо падения.
import fnmatch, json, os, struct, sys

SETTERS = {"fesetenv", "fesetround", "feholdexcept", "feupdateenv", "fesetexceptflag", "feenableexcept", "fedisableexcept"}
SHF_EXECINSTR = 0x4
SHT_DYNSYM = 11

def scan(path):
    with open(path, "rb") as f:
        data = f.read()
    if data[:4] != b"\x7fELF" or data[4] != 2 or data[5] != 1:
        raise ValueError("не ELF64 little-endian")
    e_machine = struct.unpack_from("<H", data, 18)[0]
    if e_machine != 183:
        raise ValueError(f"не AArch64 (e_machine={e_machine})")
    e_shoff, = struct.unpack_from("<Q", data, 0x28)
    e_shentsize, e_shnum = struct.unpack_from("<HH", data, 0x3A)
    secs = []
    for i in range(e_shnum):
        o = e_shoff + i * e_shentsize
        name, typ, flags, addr, off, size, link, info, align, entsize = struct.unpack_from("<IIQQQQIIQQ", data, o)
        secs.append(dict(type=typ, flags=flags, off=off, size=size, link=link, entsize=entsize))
    hits = {"msr_fpcr": 0, "mrs_fpcr": 0, "msr_fpsr": 0}
    for s in secs:
        if not (s["flags"] & SHF_EXECINSTR) or s["type"] == 8:  # SHT_NOBITS
            continue
        n = s["size"] // 4
        for (w,) in struct.iter_unpack("<I", data[s["off"]:s["off"] + n * 4]):
            k = w & 0xFFFFFFE0
            if k == 0xD51B4400: hits["msr_fpcr"] += 1
            elif k == 0xD53B4400: hits["mrs_fpcr"] += 1
            elif k == 0xD51B4420: hits["msr_fpsr"] += 1
    fe = set()
    for s in secs:
        if s["type"] != SHT_DYNSYM:
            continue
        strtab = secs[s["link"]]
        for o in range(s["off"], s["off"] + s["size"], s["entsize"] or 24):
            st_name, st_info, st_other, st_shndx, st_value, st_size = struct.unpack_from("<IBBHQQ", data, o)
            if st_shndx != 0 or st_name == 0:
                continue
            p = strtab["off"] + st_name
            nm = data[p:data.index(b"\0", p)].decode(errors="replace").split("@")[0]
            if nm.startswith("fe") and nm not in ("feof", "ferror", "fexecve"):
                fe.add(nm)
    return hits, sorted(fe)

def main(argv):
    baseline, warn_only, files = {}, [], []
    it = iter(argv)
    for a in it:
        if a == "--baseline": baseline = json.load(open(next(it)))["libs"]
        elif a == "--warn-only": warn_only.append(next(it))
        else: files.append(a)
    bad = 0
    for path in files:
        name = os.path.basename(path)
        hits, fe = scan(path)
        base = baseline.get(name)
        problems = []
        allowed_msr = base["msr_fpcr"] if base else 0
        allowed_set = set(base["fe_setters"]) if base else set()
        if hits["msr_fpcr"] > allowed_msr:
            problems.append(f"MSR FPCR: {hits['msr_fpcr']} (база {allowed_msr})")
        new_set = sorted((set(fe) & SETTERS) - allowed_set)
        if new_set:
            problems.append("новые импорты, меняющие FP-окружение: " + ",".join(new_set))
        line = f"{name}: msr_fpcr={hits['msr_fpcr']} mrs_fpcr={hits['mrs_fpcr']} msr_fpsr={hits['msr_fpsr']} fe=[{','.join(fe)}]"
        if base and (hits["mrs_fpcr"] != base.get("mrs_fpcr", 0) or hits["msr_fpsr"] != base.get("msr_fpsr", 0)):
            line += f"  (чтения/FPSR отличаются от базы: mrs={base.get('mrs_fpcr',0)} fpsr={base.get('msr_fpsr',0)})"
        if not problems:
            print("  ok   " + line)
        elif any(fnmatch.fnmatch(name, g) for g in warn_only):
            print("  WARN " + line + "  <- " + "; ".join(problems))
        else:
            print("  FAIL " + line + "  <- " + "; ".join(problems))
            bad = 1
    return bad

if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
