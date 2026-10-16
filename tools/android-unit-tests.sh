#!/usr/bin/env bash
# BFME: debug-args. Runs :app:testDebugUnitTest on its own, without tools/build-apk.sh.
# The app module compiles against SDL3's Java glue (org.libsdl.app), which
# package-android-zh.sh normally stages into android/app/src/main/java-sdl/ (git-ignored)
# from the native build tree. This script stages it from that tree if present, otherwise
# from the pinned SDL3 release tarball (version and SHA-256 from cmake/sdl3.cmake),
# then runs Gradle in the build image. Unit tests do not sign anything, so the keystore
# check in build.gradle gets an existing placeholder path.
#
#   tools/android-unit-tests.sh            # BFME_DOCKER, BFME_CACHE as in build-apk.sh
set -euo pipefail
REPO="$(cd "$(dirname "$0")/.." && pwd)"; cd "$REPO"
CACHE="${BFME_CACHE:-$(cd "$REPO/.." && pwd)/cache}"
DOCKER="${BFME_DOCKER:-docker}"
DEST=android/app/src/main/java-sdl/org/libsdl/app

if ! ls "$DEST"/SDLActivity.java >/dev/null 2>&1; then
  mkdir -p "$DEST"
  TREE="build/android-vulkan/_deps/sdl3-src/android-project/app/src/main/java/org/libsdl/app"
  if [ -f "$TREE/SDLActivity.java" ]; then
    cp "$TREE"/*.java "$DEST/"
  else
    VER="$(sed -nE 's/.*set\(SDL3_VERSION "([^"]+)"\).*/\1/p' cmake/sdl3.cmake | head -1)"
    SHA="$(sed -nE 's/.*SDL3_URL_HASH "SHA256=([0-9a-f]+)".*/\1/p' cmake/sdl3.cmake | head -1)"
    TGZ="$CACHE/sdl3/SDL3-$VER.tar.gz"
    mkdir -p "$CACHE/sdl3"
    [ -f "$TGZ" ] || curl -fsSL -o "$TGZ" "https://github.com/libsdl-org/SDL/releases/download/release-$VER/SDL3-$VER.tar.gz"
    echo "$SHA  $TGZ" | sha256sum -c -
    tar -xzf "$TGZ" -C "$DEST" --strip-components=9 --wildcards "SDL3-$VER/android-project/app/src/main/java/org/libsdl/app/*.java"
  fi
  # same patches package-android-zh.sh applies to the copied glue (relative to the java/ root)
  for p in android/patches/*.patch; do [ -f "$p" ] && patch -p1 -s -d android/app/src/main/java-sdl < "$p"; done
  echo "staged SDL3 Java glue: $(ls "$DEST" | wc -l) files"
fi

IMAGE_TAG="$(tools/build-apk.sh --print-image-tag)"
mkdir -p "$CACHE/home" "$CACHE/gradle"
$DOCKER run --rm --network host -u "$(id -u):$(id -g)" \
  -e HOME=/cache/home -e GRADLE_USER_HOME=/cache/gradle -e BFME_KEYSTORE_FILE=/src/android/app/build.gradle \
  -v "$REPO:/src" -v "$CACHE:/cache" -w /src/android "$IMAGE_TAG" \
  bash -lc 'gradle -q :app:testDebugUnitTest; rc=$?; cd app/build/test-results/testDebugUnitTest 2>/dev/null && grep -ho "tests=\"[0-9]*\" skipped=\"[0-9]*\" failures=\"[0-9]*\" errors=\"[0-9]*\"" *.xml | awk -F\" "{t+=\$2;f+=\$6;e+=\$8} END{print \"tests\",t,\"failures\",f,\"errors\",e}"; exit $rc'
