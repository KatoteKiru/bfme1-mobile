#!/usr/bin/env bash
# E1-49 checks that do not need the game or a device.
# FPCR R1 is not in this script: ADR-0003 §2.3 is a runtime hook (fp::), not a
# static check, and fp.h / bindLogicThread are a separate change.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"
python3 tools/ci/check_crc_xfer.py --self-test
python3 tools/ci/check_crc_xfer.py
python3 tools/ci/check_logic_int.py --self-test
python3 tools/ci/check_logic_int.py
python3 tools/ci/check_real_to_int.py --self-test
python3 tools/ci/check_real_to_int.py
BUILD="${1:-build/tests-gamemath}"
# gcc, not the default c++. On this host /usr/bin/c++ is clang and does not
# find libstdc++; the linux64 builds of the tree use gcc.
: "${CC:=gcc}"
: "${CXX:=g++}"
cmake -S tests/gamemath -B "$BUILD" -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_C_COMPILER="$CC" -DCMAKE_CXX_COMPILER="$CXX"
cmake --build "$BUILD" -j"$(nproc)"
# core_wwmath_on is part of this build: wwmath.h with USE_DETERMINISTIC_MATH.
LOG="$BUILD/ctest-e1-49.log"
set +e
ctest --test-dir "$BUILD" --output-on-failure | tee "$LOG"
status=${PIPESTATUS[0]}
set -e
if grep -E -q 'bitexact_x86_64_arm64.*\*\*\*Skipped|^SKIP:' "$LOG"; then
    echo "WARNING: bit-exact x86_64 vs arm64 was SKIPPED. SAGE_USE_DETERMINISTIC_MATH stays OFF. A skip is not a pass." >&2
    echo "::warning::bit-exact x86_64 vs arm64 was SKIPPED. SAGE_USE_DETERMINISTIC_MATH stays OFF. A skip is not a pass."
fi
exit "$status"
