#!/usr/bin/env bash
# Тулчейн десктопной Linux-сборки (linux64-deploy) в PREFIX: CMake и Ninja той же версии, что для Android,
# и vcpkg на пине build-linux.yml форка (VCPKG_LINUX_COMMIT). Всё из docker/versions.env.
# Используется в docker/linux-build.Dockerfile; системные пакеты — docker/linux-apt-packages.txt.
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck disable=SC1091
set -a; . "${HERE}/../docker/versions.env"; set +a
PREFIX="${1:-/opt/bfme-linux-toolchain}"
DL="${PREFIX}/.dl"
mkdir -p "$PREFIX/bin" "$DL"

fetch() { # url sha256 out
  local url="$1" sum="$2" out="$DL/$3"
  [ -f "$out" ] && echo "$sum  $out" | sha256sum -c --quiet - 2>/dev/null || { curl -fL --retry 3 --retry-delay 5 -o "$out.part" "$url"; mv "$out.part" "$out"; }
  echo "$sum  $out" | sha256sum -c -
}

echo "== CMake ${CMAKE_VERSION}"
fetch "https://github.com/Kitware/CMake/releases/download/v${CMAKE_VERSION}/cmake-${CMAKE_VERSION}-linux-x86_64.tar.gz" "$CMAKE_SHA256" cmake.tar.gz
rm -rf "$PREFIX/cmake"; mkdir -p "$PREFIX/cmake"; tar -xzf "$DL/cmake.tar.gz" -C "$PREFIX/cmake" --strip-components=1

echo "== Ninja ${NINJA_VERSION}"
fetch "https://github.com/ninja-build/ninja/releases/download/v${NINJA_VERSION}/ninja-linux.zip" "$NINJA_SHA256" ninja.zip
unzip -o -q "$DL/ninja.zip" -d "$PREFIX/bin"; chmod +x "$PREFIX/bin/ninja"

echo "== vcpkg (linux64-deploy) @ ${VCPKG_LINUX_COMMIT}"
# Полный клон: манифест с builtin-baseline требует историю версий.
[ -d "$PREFIX/vcpkg/.git" ] || git clone -q https://github.com/microsoft/vcpkg.git "$PREFIX/vcpkg"
git -C "$PREFIX/vcpkg" -c advice.detachedHead=false checkout -q "$VCPKG_LINUX_COMMIT"
[ "$(git -C "$PREFIX/vcpkg" rev-parse HEAD)" = "$VCPKG_LINUX_COMMIT" ] || { echo "vcpkg: не тот коммит"; exit 1; }
# bootstrap скачивает vcpkg-tool, версия и SHA-512 зафиксированы в scripts/vcpkg-tool-metadata.txt этого коммита.
"$PREFIX/vcpkg/bootstrap-vcpkg.sh" -disableMetrics

rm -rf "$DL"
echo "== Тулчейн Linux готов: $PREFIX"
