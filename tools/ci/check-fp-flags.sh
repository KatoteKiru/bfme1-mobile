#!/usr/bin/env bash
# BFME1 Mobile: R4 из docs/fpcr-audit.md -- запрет -ffast-math / -Ofast / -funsafe-math-optimizations.
#   tools/ci/check-fp-flags.sh <build-dir> [extra-dir-with-logs ...]
# Ищет флаги в:
#   - всех compile_commands.json под <build-dir> (CMake: игра + FetchContent; meson: DXVK d3d8/d3d9),
#   - логах meson (meson-logs/*.txt) и vcpkg (vcpkg-manifest-install.log, buildtrees/**/*.log в extra-dir),
#   - .pc-файлах vcpkg_installed и триплетах cmake/triplets/*.cmake.
# Порты vcpkg, взятые из бинарного кэша, логов компиляции не оставляют: для них проверка полная
# только при холодном кэше (раз в смену VCPKG_COMMIT/триплета) -- см. docs/bfme/ANDROID_BUILD.md.
set -euo pipefail
B="${1:?build dir}"; shift || true
RE='(^|[[:space:]"'"'"'=,;])(-ffast-math|-Ofast|-funsafe-math-optimizations)([[:space:]"'"'"',;]|$)'
mapfile -t files < <(
  find "$B" \( -name compile_commands.json -o -path '*/meson-logs/*.txt' -o -name 'vcpkg-manifest-install.log' \) -type f 2>/dev/null
  find "$B" -path '*/vcpkg_installed/*' -name '*.pc' -type f 2>/dev/null
  for d in "$@"; do [ -d "$d" ] && find "$d" -name '*.log' -type f 2>/dev/null; done
  ls cmake/triplets/*.cmake 2>/dev/null || true
)
ncc=0; for f in "${files[@]}"; do [[ "$f" == *compile_commands.json ]] && ncc=$((ncc+1)); done
echo "  R4: файлов ${#files[@]} (compile_commands.json: $ncc)"
[ "$ncc" -ge 2 ] || { echo "  R4 FAIL: ожидалось >= 2 compile_commands.json (CMake + meson DXVK), найдено $ncc"; exit 1; }
hits=0
for f in "${files[@]}"; do
  if out="$(grep -nE -- "$RE" "$f" 2>/dev/null)"; then
    hits=1; echo "  R4 FAIL: $f"; printf '%s\n' "$out" | cut -c1-300 | head -5
  fi
done
[ "$hits" = 0 ] && echo "  R4 ok: -ffast-math/-Ofast/-funsafe-math-optimizations не найдены"
exit "$hits"
