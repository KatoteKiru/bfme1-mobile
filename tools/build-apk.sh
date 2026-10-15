#!/usr/bin/env bash
# BFME1 Mobile: воспроизводимая сборка APK (arm64-v8a) одной командой.
#
#   tools/build-apk.sh               # в Docker-образе (канонический путь, как в CI)
#   tools/build-apk.sh --native      # без Docker, тулчейн из tools/install-toolchain.sh (запасной путь)
#   tools/build-apk.sh --image-only  # только собрать образ
#   tools/build-apk.sh --print-image-tag   # тег образа для этого дерева (для других workflow)
#
# Переменные окружения:
#   BFME_JOBS=4                    параллелизм ninja/vcpkg (бокс: 15 ГБ ОЗУ без swap -> 3-4)
#   BFME_MEM_LIMIT=6g              лимит памяти контейнера (OOM убивает сборку, а не бокс)
#   BFME_CACHE=<dir>               кэши ccache/vcpkg/gradle (по умолчанию <repo>/../cache или ~/.cache/bfme)
#   BFME_SECRETS=<dir>             каталог ключа подписи ВНЕ git
#                                  (бокс: /workspace/bfme-mobile/secrets; иначе <repo>/../secrets)
#   BFME_KEYSTORE_FILE             PKCS12-ключ (по умолчанию $BFME_SECRETS/bfme-debug.keystore)
#   BFME_KEYSTORE_PASSWORD         пароль хранилища (на боксе: $BFME_SECRETS/keystore.pass, chmod 600)
#   BFME_KEY_ALIAS                 алиас (по умолчанию bfmedebug)
#   BFME_KEY_PASSWORD              пароль ключа (по умолчанию равен BFME_KEYSTORE_PASSWORD)
#   BFME_DEBUG_KEYSTORE_B64        запасной путь: keystore в base64, только ручной запуск вне бокса
#   BFME_REQUIRE_KEYSTORE=1        не генерировать ключ, а падать, если его нет (CI)
#   GX_ANDROID_VERSION_NAME        versionName; по умолчанию 0.1.0-<короткий SHA HEAD>
#   BFME_DOCKER="sudo -n /usr/local/sbin/bfme-ci-docker"   как вызывать docker (раннер CI)
#   BFME_RELRO_WAIVE="libc++_shared.so ..."   готовые .so, которым прощается конец RELRO не на 16 КБ
#   BFME_TOOLCHAIN=/opt/bfme-toolchain   префикс нативного тулчейна (--native)
# Результат: out/bfme-zh-<sha>.apk, журнал: logs/build-apk-<время>.log
set -euo pipefail
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO"
set -a; . docker/versions.env; set +a
MODE=docker
for a in "$@"; do case "$a" in
  --native) MODE=native ;; --image-only) MODE=image ;; --inside) MODE=inside ;; --print-image-tag) MODE=tag ;;
  *) echo "неизвестный аргумент $a"; exit 2 ;; esac; done

JOBS="${BFME_JOBS:-4}"
PRESET=android-vulkan
BUILD_DIR="build/${PRESET}"
IMAGE_TAG="bfme-android-build:$(cat docker/android-build.Dockerfile docker/versions.env docker/apt-packages.txt tools/install-toolchain.sh | sha256sum | cut -c1-12)"

[ "$MODE" = tag ] && { echo "$IMAGE_TAG"; exit 0; }

# ---------------------------------------------------------------- журнал
if [ -z "${BFME_LOG:-}" ]; then
  mkdir -p logs
  export BFME_LOG="$REPO/logs/build-apk-$(date +%Y%m%d-%H%M%S).log"
  echo "Журнал: $BFME_LOG"
  set +e
  "$0" "$@" 2>&1 | tee "$BFME_LOG"
  rc=${PIPESTATUS[0]}
  echo "exit=$rc  журнал: $BFME_LOG"
  exit "$rc"
fi
ts() { echo "=== [$(date '+%F %T %Z')] $*"; }

# ---------------------------------------------------------------- ключ подписи (вне git)
secrets_dir() {
  if [ -n "${BFME_SECRETS:-}" ]; then
    printf '%s\n' "$BFME_SECRETS"
    return
  fi
  # Бокс: репозиторий в /workspace/bfme-mobile, ключ рядом, не в git.
  if [ -d /workspace/bfme-mobile ]; then
    printf '%s\n' /workspace/bfme-mobile/secrets
    return
  fi
  printf '%s\n' "$(cd "$REPO/.." && pwd)/secrets"
}

prepare_keystore() {
  local dir; dir="$(secrets_dir)"
  mkdir -p "$dir"; chmod 700 "$dir"
  export BFME_KEY_ALIAS="${BFME_KEY_ALIAS:-bfmedebug}"
  if [ -z "${BFME_KEYSTORE_FILE:-}" ]; then
    export BFME_KEYSTORE_FILE="$dir/bfme-debug.keystore"
  fi
  if [ -n "${BFME_DEBUG_KEYSTORE_B64:-}" ]; then
    echo "$BFME_DEBUG_KEYSTORE_B64" | base64 -d > "$BFME_KEYSTORE_FILE"
    chmod 600 "$BFME_KEYSTORE_FILE"
    : "${BFME_KEYSTORE_PASSWORD:?BFME_KEYSTORE_PASSWORD обязателен вместе с BFME_DEBUG_KEYSTORE_B64}"
    ts "ключ подписи: из секрета BFME_DEBUG_KEYSTORE_B64"
  else
    if [ -z "${BFME_KEYSTORE_PASSWORD:-}" ]; then
      [ -f "$dir/keystore.pass" ] || { head -c 24 /dev/urandom | base64 | tr -d '/+=' > "$dir/keystore.pass"; }
      [ -f "$dir/keystore.pass" ] && chmod 600 "$dir/keystore.pass"
      BFME_KEYSTORE_PASSWORD="$(cat "$dir/keystore.pass")"
    fi
    if [ ! -f "$BFME_KEYSTORE_FILE" ] && [ "${BFME_REQUIRE_KEYSTORE:-0}" = 1 ]; then
      echo "ОШИБКА: нет ключа $BFME_KEYSTORE_FILE (BFME_REQUIRE_KEYSTORE=1: CI не генерирует новый ключ)"; exit 1
    elif [ ! -f "$BFME_KEYSTORE_FILE" ]; then
      ts "ключ подписи: генерирую новый отладочный ключ $BFME_KEYSTORE_FILE"
      keytool -genkeypair -noprompt -keystore "$BFME_KEYSTORE_FILE" -storetype PKCS12 \
        -storepass "$BFME_KEYSTORE_PASSWORD" -keypass "${BFME_KEY_PASSWORD:-$BFME_KEYSTORE_PASSWORD}" -alias "$BFME_KEY_ALIAS" \
        -keyalg RSA -keysize 3072 -validity 10000 -dname "CN=BFME1 Mobile Debug, O=bfme-mobile, C=RU"
      chmod 600 "$BFME_KEYSTORE_FILE"
    else
      chmod 600 "$BFME_KEYSTORE_FILE"
      ts "ключ подписи: $BFME_KEYSTORE_FILE"
    fi
  fi
  export BFME_KEYSTORE_PASSWORD BFME_KEY_PASSWORD="${BFME_KEY_PASSWORD:-$BFME_KEYSTORE_PASSWORD}"
  ts "ключ подписи: alias=${BFME_KEY_ALIAS} file=${BFME_KEYSTORE_FILE}"
}

# ---------------------------------------------------------------- сборка (в контейнере или нативно)
build_inside() {
  local git_sha
  git_sha="$(git rev-parse --short=7 HEAD 2>/dev/null || true)"
  if [ -z "$git_sha" ]; then
    echo "ОШИБКА: не удалось прочитать короткий git SHA (versionName)"
    exit 1
  fi
  export GX_ANDROID_VERSION_NAME="${GX_ANDROID_VERSION_NAME:-0.1.0-${git_sha}}"
  ts "git ${git_sha}  versionName ${GX_ANDROID_VERSION_NAME}"
  ts "E1-04: нет URL апстрима в исходниках"
  tools/ci/check-no-upstream-urls.sh

  ts "окружение"; uname -a; nproc; free -m | head -2 || true
  echo "NDK: $(grep Pkg.Revision "$ANDROID_NDK_HOME/source.properties")"
  cmake --version | head -1; ninja --version; java -version 2>&1 | head -1; gradle --version 2>/dev/null | grep '^Gradle'
  echo "vcpkg: $(git -C "$VCPKG_ROOT" rev-parse HEAD)"; meson --version; ccache --version | head -1
  echo "repo: $(git rev-parse HEAD 2>/dev/null) $(git status --porcelain 2>/dev/null | wc -l) изменённых файлов"

  local cache="${BFME_CACHE_INNER:-${HOME}/.cache/bfme}"
  mkdir -p "$cache"/{ccache,vcpkg-bin,vcpkg-dl,gradle,staging}
  export CCACHE_DIR="$cache/ccache" VCPKG_DEFAULT_BINARY_CACHE="$cache/vcpkg-bin" VCPKG_DOWNLOADS="$cache/vcpkg-dl"
  export GRADLE_USER_HOME="$cache/gradle" GX_ANDROID_STAGING="$cache/staging"
  export VCPKG_MAX_CONCURRENCY="$JOBS" CMAKE_BUILD_PARALLEL_LEVEL="$JOBS" VCPKG_KEEP_ENV_VARS="VCPKG_MAX_CONCURRENCY"
  ccache --set-config=max_size=5G; ccache --set-config=compiler_check=content; ccache -z >/dev/null

  prepare_keystore

  ts "сабмодуль DXVK"
  if [ ! -f references/fbraz3-dxvk/include/spirv/include/spirv/unified1/spirv.hpp ]; then
    git submodule update --init --depth 1 references/fbraz3-dxvk
    git -C references/fbraz3-dxvk submodule update --init --depth 1
  fi

  ts "configure (preset ${PRESET}; vcpkg собирает зависимости при первом запуске)"
  cmake --preset "$PRESET" -DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache \
    -DANDROID_CI_BUILD_NUMBER="${BFME_BUILD_NUMBER:-0}" -DVCPKG_INSTALL_OPTIONS="--x-buildtrees-root=$cache/vcpkg-buildtrees;--clean-after-build"

  ts "build libmain.so (30 Гц) + DXVK d3d8/d3d9 + хуки adrenotools, -j${JOBS}"
  touch GeneralsMD/Code/Main/AndroidCrashHandler.cpp
  cmake --build "$BUILD_DIR" -j "$JOBS" --target z_generals dxvk_d3d8_install main_hook file_redirect_hook gsl_alloc_hook hook_impl
  ccache -s | head -8 || true

  ts "R4 (docs/fpcr-audit.md): нет -ffast-math/-Ofast/-funsafe-math-optimizations в compile_commands.json, логах meson DXVK и vcpkg"
  tools/ci/check-fp-flags.sh "$BUILD_DIR" "$cache/vcpkg-buildtrees"

  local TB; TB="$(ls -d "$ANDROID_NDK_HOME"/toolchains/llvm/prebuilt/*/bin)"
  local GAME_LIB="$BUILD_DIR/GeneralsMD/Code/Main/libmain.so"
  # Сначала в переменную, потом grep: при pipefail `readelf | grep -q` ловит SIGPIPE (урок форка).
  local hdr; hdr="$("$TB/llvm-readelf" -h "$GAME_LIB")"
  grep -q AArch64 <<< "$hdr" || { echo "libmain.so не AArch64"; exit 1; }
  local dx; dx="$(strings "$BUILD_DIR/libdxvk_d3d9.so")"
  grep -q "SDL3 WSI:" <<< "$dx" || { echo "libdxvk_d3d9.so без SDL3 WSI"; exit 1; }

  ts "таблица символов libmain (до strip)"
  mkdir -p out/symbols
  "$TB/llvm-nm" --defined-only --numeric-sort --print-size --demangle "$GAME_LIB" | gzip -9 > "out/symbols/libmain-$(git rev-parse --short HEAD).symbols.txt.gz"

  ts "strip (кроме DXVK, см. build.gradle форка)"
  for lib in "$GAME_LIB" "$BUILD_DIR/_deps/sdl3-build/libSDL3.so" "$BUILD_DIR/_deps/sdl3_image-build/libSDL3_image.so" \
             "$BUILD_DIR/_deps/openal_soft-build/libopenal.so" "$BUILD_DIR/libgamespy.so" \
             "$BUILD_DIR/_deps/adrenotools-build/libadrenotools.so" \
             "$BUILD_DIR"/_deps/adrenotools-build/src/hook/lib*_hook.so "$BUILD_DIR/_deps/adrenotools-build/src/hook/libhook_impl.so"; do
    [ -f "$lib" ] && "$TB/llvm-strip" --strip-unneeded "$lib"
  done

  ts "упаковка APK (Gradle)"
  rm -rf android/app/build
  ./scripts/build/android/package-android-zh.sh

  local APK=android/app/build/outputs/apk/debug/app-debug.apk
  [ -f "$APK" ] || { echo "APK не найден: $APK"; exit 1; }
  local OUT="out/bfme-zh-${git_sha}.apk"
  cp "$APK" "$OUT"
  local sum
  sum="$(sha256sum "$OUT" | awk '{print $1}')"
  printf '%s  %s\n' "$sum" "$(basename "$OUT")" > "out/$(basename "$OUT").sha256"
  ts "APK: ${REPO}/${OUT}"
  ts "sha256: ${sum}"
  ts "versionName: ${GX_ANDROID_VERSION_NAME}"
  verify_apk "$OUT" "$TB"
  ts "ГОТОВО: ${REPO}/${OUT} ($(du -h "$OUT" | cut -f1)) sha256=${sum}"
}

verify_apk() {
  local APK="$1" TB="$2" W; W="$(mktemp -d)"
  ts "проверка APK: apksigner verify"
  if ! apksigner verify "$APK"; then
    echo "ОШИБКА: apksigner verify не прошёл: $APK"
    exit 1
  fi
  ts "проверка APK: apksigner verify --print-certs"
  if ! apksigner verify --print-certs "$APK"; then
    echo "ОШИБКА: apksigner verify --print-certs не прошёл: $APK"
    exit 1
  fi
  ts "проверка APK: нет URL апстрима (classes.dex, .so, resources.arsc)"
  "$REPO/tools/ci/check-no-upstream-urls.sh" "$APK"
  ts "проверка APK: DT_NEEDED libmain.so"
  unzip -q "$APK" 'lib/arm64-v8a/*' -d "$W"
  local missing=0
  for lib in $("$TB/llvm-readelf" -d "$W/lib/arm64-v8a/libmain.so" | sed -nE 's/.*NEEDED.*\[(.*)\]/\1/p'); do
    case "$lib" in libc.so|libm.so|libdl.so|liblog.so|libandroid.so|libGLESv*|libEGL.so|libvulkan.so|libOpenSLES.so|libaaudio.so|libmediandk.so|libcamera2ndk.so|libjnigraphics.so|libnativewindow.so|libsync.so|libz.so) continue;; esac
    [ -f "$W/lib/arm64-v8a/$lib" ] || { echo "НЕТ В APK: $lib"; missing=1; }
  done
  [ "$missing" = 0 ] || exit 1
  ts "проверка 16 КБ: все LOAD align >= 2**14 и конец GNU_RELRO кратен 0x4000 (lib/ в APK + Turnip из assets)"
  local bad=0
  unzip -q -o "$APK" 'assets/default_driver/*.so' -d "$W" 2>/dev/null || true
  for so in "$W"/lib/arm64-v8a/*.so "$W"/assets/default_driver/*.so; do
    [ -f "$so" ] || continue
    local name="${so#$W/}" aligns relro okline=1
    aligns="$("$TB/llvm-objdump" -p "$so" | awk '/LOAD/{for(i=1;i<=NF;i++) if($i=="align") print $(i+1)}' | sort -u | tr '\n' ' ')"
    for a in $aligns; do [ "${a#2\*\*}" -ge 14 ] 2>/dev/null || okline=0; done
    # GNU_RELRO: VirtAddr (кол. 3) + MemSiz (кол. 6) из llvm-readelf -lW
    # (в образе mawk без strtonum: awk только печатает hex-поля, сумму считает bash)
    local rl; rl="$("$TB/llvm-readelf" -lW "$so" | awk '$1=="GNU_RELRO"{print $3, $6; exit}')"
    relro=""; [ -n "$rl" ] && { set -- $rl; relro=$(( $1 + $2 )); }
    local relro_s="нет"
    local relro_warn=0
    if [ -n "$relro" ]; then
      relro_s="$(printf '0x%x' "$relro")"
      if [ $((relro % 16384)) -ne 0 ]; then
        # Готовые бинарники не из нашей сборки (NDK libc++_shared, ANGLE из AOSP): их линковали
        # с common-page-size 4096. С LOAD align 2**14 bionic их грузит (RW-сегмент после RELRO
        # lld сдвигает на следующую страницу), поэтому это предупреждение, а не ошибка.
        # Для всего, что собираем мы, конец RELRO обязан быть кратен 16 КБ.
        case " ${BFME_RELRO_WAIVE:-libc++_shared.so libEGL_angle.so libGLESv2_angle.so} " in
          *" $(basename "$so") "*) relro_warn=1 ;; *) okline=0 ;; esac
      fi
    fi
    if [ "$okline" = 0 ]; then echo "  FAIL $name: LOAD $aligns RELRO_end=$relro_s"; bad=1
    elif [ "$relro_warn" = 1 ]; then echo "  WARN $name: LOAD $aligns RELRO_end=$relro_s (готовый бинарник, конец RELRO не кратен 0x4000)"
    else echo "  ok   $name: LOAD $aligns RELRO_end=$relro_s"; fi
  done
  ts "T5 (docs/fpcr-audit.md): MSR FPCR и импорты fe* во всех .so против tools/fpcr/fpcr-baseline.json"
  python3 tools/fpcr/fpcr_check.py --baseline tools/fpcr/fpcr-baseline.json --warn-only 'vulkan.*.so' \
    "$W"/lib/arm64-v8a/*.so $(ls "$W"/assets/default_driver/*.so 2>/dev/null) || { echo "ОШИБКА: T5 -- новая запись FP-режима в .so"; exit 1; }
  ts "zipalign -c -P 16 -v 4 (справочно: useLegacyPackaging=true, .so в APK сжаты, проверка их выравнивание не доказывает)"
  if zipalign -c -P 16 -v 4 "$APK" > "$W/zipalign.txt" 2>&1; then echo "  zipalign: Verification successful"; else tail -5 "$W/zipalign.txt"; bad=1; fi
  echo "  сжатие .so в APK: $(unzip -v "$APK" 'lib/arm64-v8a/*' | awk '/lib\/arm64-v8a/{print $2}' | sort | uniq -c | tr '\n' ' ')"
  rm -rf "$W"
  if [ "$bad" != 0 ]; then
    if [ "${BFME_ALLOW_4K:-0}" = 1 ]; then echo "ПРЕДУПРЕЖДЕНИЕ: есть .so без 16 КБ (BFME_ALLOW_4K=1)"; else echo "ОШИБКА: 16 КБ-проверка не пройдена"; exit 1; fi
  fi
}

case "$MODE" in
  inside) build_inside ;;
  native)
    TC="${BFME_TOOLCHAIN:-/opt/bfme-toolchain}"
    [ -f "$TC/env.sh" ] || { ts "ставлю тулчейн в $TC"; sudo mkdir -p "$TC"; sudo chown "$(id -u):$(id -g)" "$TC"; tools/install-toolchain.sh "$TC"; }
    . "$TC/env.sh"
    BFME_CACHE_INNER="${BFME_CACHE:-$HOME/.cache/bfme}" build_inside ;;
  image|docker)
    # BFME_DOCKER: как вызывать docker. Раннер CI задаёт BFME_DOCKER="sudo -n /usr/local/sbin/bfme-ci-docker"
    # (узкая обёртка вместо группы docker, см. tools/ci/README.md); на боксе по умолчанию docker или sudo docker.
    if [ -n "${BFME_DOCKER:-}" ]; then DOCKER="$BFME_DOCKER"
    elif docker info >/dev/null 2>&1; then DOCKER="docker"; else DOCKER="sudo docker"; fi
    if ! $DOCKER image inspect "$IMAGE_TAG" >/dev/null 2>&1; then
      ts "сборка образа $IMAGE_TAG"
      $DOCKER build --network host -f docker/android-build.Dockerfile --build-arg UBUNTU_IMAGE="$UBUNTU_IMAGE" -t "$IMAGE_TAG" .
    else ts "образ $IMAGE_TAG уже есть"; fi
    [ "$MODE" = image ] && exit 0
    CACHE="${BFME_CACHE:-$(cd "$REPO/.." && pwd)/cache}"; SECRETS="$(secrets_dir)"
    mkdir -p "$CACHE/home" "$SECRETS"
    ts "сборка в контейнере $IMAGE_TAG (jobs=$JOBS, mem=${BFME_MEM_LIMIT:-6g})"
    $DOCKER run --rm --network host --memory="${BFME_MEM_LIMIT:-6g}" --memory-swap="${BFME_MEM_LIMIT:-6g}" \
      -u "$(id -u):$(id -g)" -e HOME=/cache/home -e BFME_LOG="$BFME_LOG" -e BFME_JOBS="$JOBS" \
      -e BFME_CACHE_INNER=/cache -e BFME_SECRETS=/secrets -e BFME_BUILD_NUMBER="${BFME_BUILD_NUMBER:-0}" \
      -e BFME_DEBUG_KEYSTORE_B64="${BFME_DEBUG_KEYSTORE_B64:-}" \
      -e BFME_KEYSTORE_PASSWORD="${BFME_KEYSTORE_PASSWORD:-}" \
      -e BFME_KEY_ALIAS="${BFME_KEY_ALIAS:-}" \
      -e BFME_KEY_PASSWORD="${BFME_KEY_PASSWORD:-}" \
      -e GX_ANDROID_VERSION_NAME="${GX_ANDROID_VERSION_NAME:-}" \
      -e BFME_ALLOW_4K="${BFME_ALLOW_4K:-0}" -e BFME_REQUIRE_KEYSTORE="${BFME_REQUIRE_KEYSTORE:-0}" \
      -v "$REPO:/src" -v "$CACHE:/cache" -v "$SECRETS:/secrets" -w /src "$IMAGE_TAG" tools/build-apk.sh --inside ;;
esac
