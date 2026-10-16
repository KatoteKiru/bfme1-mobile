#!/usr/bin/env python3
"""Bit-exact GameMath: host x86_64 against arm64 under qemu-aarch64.

The host binary is built by CMake. If qemu-aarch64 (or a cross compiler) is
not on PATH, this prints a SKIP line and exits 0. A skipped run is not a
pass of the cross check, so SAGE_USE_DETERMINISTIC_MATH stays OFF.
A hash mismatch exits 1.
"""

from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
from pathlib import Path


def skip(msg: str) -> int:
    print(f"SKIP: {msg}")
    return 0


def run_hash(cmd: list[str]) -> dict[str, str]:
    proc = subprocess.run(cmd, check=False, text=True, capture_output=True)
    if proc.returncode != 0:
        sys.stderr.write(proc.stdout)
        sys.stderr.write(proc.stderr)
        raise SystemExit(f"FAIL: {' '.join(cmd)} exited {proc.returncode}")
    rows: dict[str, str] = {}
    for line in proc.stdout.splitlines():
        parts = line.split()
        if len(parts) == 2 and parts[0] == "count":
            rows["count"] = parts[1]
            continue
        if len(parts) == 2:
            rows[parts[0]] = parts[1]
    return rows


def find_qemu() -> str | None:
    for name in ("qemu-aarch64", "qemu-aarch64-static"):
        path = shutil.which(name)
        if path:
            return path
    return None


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo", required=True)
    parser.add_argument("--host", required=True)
    parser.add_argument("--build-dir", required=True)
    args = parser.parse_args()

    host = run_hash([args.host, "--hash"])
    # count is t.d.size() printed by the binary, not a compile-time constant.
    if int(host.get("count", "0")) < 1000000:
        print("FAIL: host input table is shorter than 10^6", file=sys.stderr)
        return 1

    qemu = find_qemu()
    cxx = shutil.which("aarch64-linux-gnu-g++")
    cc = shutil.which("aarch64-linux-gnu-gcc")
    if qemu is None:
        return skip(
            "qemu-aarch64 is not available; bit-exact x86_64 vs arm64 was not run. "
            "SAGE_USE_DETERMINISTIC_MATH stays OFF"
        )
    if cxx is None or cc is None:
        return skip(
            "qemu-aarch64 is available but aarch64-linux-gnu-g++ is not; "
            "bit-exact x86_64 vs arm64 was not run. SAGE_USE_DETERMINISTIC_MATH stays OFF"
        )

    build = Path(args.build_dir) / "aarch64"
    cfg = subprocess.run(
        [
            "cmake",
            "-S",
            str(Path(args.repo) / "tests/gamemath"),
            "-B",
            str(build),
            "-G",
            "Unix Makefiles",
            "-DCMAKE_BUILD_TYPE=RelWithDebInfo",
            "-DCMAKE_SYSTEM_NAME=Linux",
            "-DCMAKE_SYSTEM_PROCESSOR=aarch64",
            f"-DCMAKE_C_COMPILER={cc}",
            f"-DCMAKE_CXX_COMPILER={cxx}",
            "-DCMAKE_FIND_ROOT_PATH=/usr/aarch64-linux-gnu",
            "-DCMAKE_FIND_ROOT_PATH_MODE_PROGRAM=NEVER",
            "-DCMAKE_FIND_ROOT_PATH_MODE_LIBRARY=ONLY",
            "-DCMAKE_FIND_ROOT_PATH_MODE_INCLUDE=ONLY",
        ],
        check=False,
        text=True,
        capture_output=True,
    )
    if cfg.returncode != 0:
        sys.stderr.write(cfg.stdout)
        sys.stderr.write(cfg.stderr)
        print("FAIL: aarch64 configure failed", file=sys.stderr)
        return 1
    built = subprocess.run(
        ["cmake", "--build", str(build), "--target", "gamemath_bitexact", "-j"],
        check=False,
        text=True,
        capture_output=True,
    )
    if built.returncode != 0:
        sys.stderr.write(built.stdout)
        sys.stderr.write(built.stderr)
        print("FAIL: aarch64 build failed", file=sys.stderr)
        return 1
    guest_bin = build / "gamemath_bitexact"
    guest = run_hash([qemu, "-L", "/usr/aarch64-linux-gnu", str(guest_bin), "--hash"])
    names = sorted(set(host) | set(guest))
    mismatch = False
    for name in names:
        if host.get(name) != guest.get(name):
            mismatch = True
            print(f"FAIL: {name} host={host.get(name)} arm64={guest.get(name)}")
    if mismatch:
        return 1
    print(f"bit-exact ok ({len(names) - 1} functions, count {host['count']})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
