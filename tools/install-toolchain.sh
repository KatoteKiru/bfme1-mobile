#!/usr/bin/env bash
# Ставит тулчейн сборки Android в PREFIX (по умолчанию /opt/bfme-toolchain) строго по docker/versions.env.
# Один и тот же скрипт используется в Dockerfile и для нативной установки (запасной путь без Docker),
# поэтому версии в образе и на хосте совпадают. Системные пакеты (apt) ставятся отдельно:
# см. docker/apt-packages.txt.
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck disable=SC1091
set -a; . "${HERE}/../docker/versions.env"; set +a
PREFIX="${1:-/opt/bfme-toolchain}"
DL="${BFME_DL_CACHE:-${PREFIX}/.dl}"
mkdir -p "$PREFIX" "$DL"

fetch() { # url sha256 out
  local url="$1" sum="$2" out="$DL/$3"
  if [ ! -f "$out" ] || ! echo "$sum  $out" | sha256sum -c --quiet - 2>/dev/null; then
    curl -fL --retry 3 --retry-delay 5 -o "$out.part" "$url"
    mv "$out.part" "$out"
  fi
  echo "$sum  $out" | sha256sum -c -
}

echo "== JDK 17"
if [ ! -x "$PREFIX/jdk/bin/javac" ]; then
  fetch "$JDK_URL" "$JDK_SHA256" jdk.tar.gz
  rm -rf "$PREFIX/jdk"; mkdir -p "$PREFIX/jdk"
  tar -xzf "$DL/jdk.tar.gz" -C "$PREFIX/jdk" --strip-components=1
fi

echo "== CMake ${CMAKE_VERSION}"
if [ ! -x "$PREFIX/cmake/bin/cmake" ] || ! "$PREFIX/cmake/bin/cmake" --version | grep -q "$CMAKE_VERSION"; then
  fetch "https://github.com/Kitware/CMake/releases/download/v${CMAKE_VERSION}/cmake-${CMAKE_VERSION}-linux-x86_64.tar.gz" "$CMAKE_SHA256" cmake.tar.gz
  rm -rf "$PREFIX/cmake"; mkdir -p "$PREFIX/cmake"
  tar -xzf "$DL/cmake.tar.gz" -C "$PREFIX/cmake" --strip-components=1
fi

echo "== Ninja ${NINJA_VERSION}"
if [ ! -x "$PREFIX/bin/ninja" ]; then
  fetch "https://github.com/ninja-build/ninja/releases/download/v${NINJA_VERSION}/ninja-linux.zip" "$NINJA_SHA256" ninja.zip
  mkdir -p "$PREFIX/bin"; unzip -o -q "$DL/ninja.zip" -d "$PREFIX/bin"; chmod +x "$PREFIX/bin/ninja"
fi

echo "== Gradle ${GRADLE_VERSION}"
if [ ! -x "$PREFIX/gradle/bin/gradle" ]; then
  fetch "https://services.gradle.org/distributions/gradle-${GRADLE_VERSION}-bin.zip" "$GRADLE_SHA256" gradle.zip
  rm -rf "$PREFIX/gradle" "$PREFIX/gradle-${GRADLE_VERSION}"
  unzip -q "$DL/gradle.zip" -d "$PREFIX"; mv "$PREFIX/gradle-${GRADLE_VERSION}" "$PREFIX/gradle"
fi

echo "== Android SDK: cmdline-tools ${CMDLINE_TOOLS_VERSION}, NDK ${NDK_VERSION}, ${ANDROID_PLATFORM_PKG}, build-tools ${ANDROID_BUILD_TOOLS}"
SDK="$PREFIX/android-sdk"
if [ ! -x "$SDK/cmdline-tools/latest/bin/sdkmanager" ]; then
  fetch "https://dl.google.com/android/repository/commandlinetools-linux-${CMDLINE_TOOLS_VERSION}_latest.zip" "$CMDLINE_TOOLS_SHA256" cmdline-tools.zip
  rm -rf "$SDK/cmdline-tools"; mkdir -p "$SDK/cmdline-tools"
  unzip -q "$DL/cmdline-tools.zip" -d "$SDK/cmdline-tools"
  mv "$SDK/cmdline-tools/cmdline-tools" "$SDK/cmdline-tools/latest"
fi
export JAVA_HOME="$PREFIX/jdk"
if [ ! -f "$SDK/ndk/${NDK_VERSION}/source.properties" ] || [ ! -x "$SDK/build-tools/${ANDROID_BUILD_TOOLS}/zipalign" ] || [ ! -d "$SDK/platforms/android-35" ]; then
  yes | "$SDK/cmdline-tools/latest/bin/sdkmanager" --sdk_root="$SDK" --licenses >/dev/null || true
  # sdkmanager сам сверяет SHA-1 пакетов с манифестом Google (repository2-3.xml).
  "$SDK/cmdline-tools/latest/bin/sdkmanager" --sdk_root="$SDK" --install \
    "ndk;${NDK_VERSION}" "${ANDROID_PLATFORM_PKG}" "build-tools;${ANDROID_BUILD_TOOLS}"
fi
grep -q "Pkg.Revision = ${NDK_VERSION}" "$SDK/ndk/${NDK_VERSION}/source.properties" || { echo "NDK revision mismatch"; exit 1; }

echo "== vcpkg @ ${VCPKG_COMMIT}"
if [ ! -d "$PREFIX/vcpkg/.git" ]; then
  # Полный клон: манифест с builtin-baseline требует историю версий.
  git clone -q https://github.com/microsoft/vcpkg.git "$PREFIX/vcpkg"
fi
if [ "$(git -C "$PREFIX/vcpkg" rev-parse HEAD)" != "$VCPKG_COMMIT" ]; then
  git -C "$PREFIX/vcpkg" fetch -q origin "$VCPKG_COMMIT" || git -C "$PREFIX/vcpkg" fetch -q origin
  git -C "$PREFIX/vcpkg" -c advice.detachedHead=false checkout -q "$VCPKG_COMMIT"
  rm -f "$PREFIX/vcpkg/vcpkg"
fi
# bootstrap скачивает vcpkg-tool, версия и SHA-512 зафиксированы в scripts/vcpkg-tool-metadata.txt этого коммита.
[ -x "$PREFIX/vcpkg/vcpkg" ] || "$PREFIX/vcpkg/bootstrap-vcpkg.sh" -disableMetrics

echo "== vcpkg (linux64-deploy) @ ${VCPKG_LINUX_COMMIT}"
if [ ! -d "$PREFIX/vcpkg-linux" ]; then
  git -C "$PREFIX/vcpkg" cat-file -e "${VCPKG_LINUX_COMMIT}^{commit}" 2>/dev/null || git -C "$PREFIX/vcpkg" fetch -q origin "$VCPKG_LINUX_COMMIT"
  git -C "$PREFIX/vcpkg" -c advice.detachedHead=false worktree add -q --detach "$PREFIX/vcpkg-linux" "$VCPKG_LINUX_COMMIT"
fi
[ -x "$PREFIX/vcpkg-linux/vcpkg" ] || "$PREFIX/vcpkg-linux/bootstrap-vcpkg.sh" -disableMetrics

cat > "$PREFIX/env.sh" <<ENV
# source this: окружение тулчейна BFME1 Mobile
export BFME_TOOLCHAIN="$PREFIX"
export JAVA_HOME="$PREFIX/jdk"
export ANDROID_HOME="$SDK" ANDROID_SDK_ROOT="$SDK"
export ANDROID_NDK_HOME="$SDK/ndk/${NDK_VERSION}" ANDROID_NDK_ROOT="$SDK/ndk/${NDK_VERSION}"
export VCPKG_ROOT="$PREFIX/vcpkg"
export BFME_VCPKG_LINUX_ROOT="$PREFIX/vcpkg-linux"
export PATH="$PREFIX/bin:$PREFIX/cmake/bin:$PREFIX/gradle/bin:$PREFIX/jdk/bin:$SDK/build-tools/${ANDROID_BUILD_TOOLS}:$SDK/cmdline-tools/latest/bin:\$PATH"
ENV
rm -rf "$PREFIX/.dl"
echo "== Тулчейн готов: source $PREFIX/env.sh"
