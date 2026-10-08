#!/usr/bin/env bash
# BFME1 Mobile: десктопная Linux-сборка (preset linux64-deploy, x86_64, DXVK native -> Vulkan) одной командой, E1-05.
# Устроен по образцу tools/build-apk.sh: тот же способ звать docker, те же каталоги кэша, журнал в logs/.
#
#   tools/build-linux.sh                    # образ (если нет) + configure + сборка GeneralsXZH в контейнере
#   tools/build-linux.sh --smoke            # то же, затем запуск под Xvfb на Mesa lavapipe (журнал в logs/)
#   tools/build-linux.sh --smoke-only       # только запуск под Xvfb уже собранного бинарника
#   tools/build-linux.sh --image-only       # только собрать образ
#   tools/build-linux.sh --print-image-tag  # тег образа для этого дерева
#
# Переменные окружения:
#   BFME_JOBS=3              параллелизм ninja/vcpkg (бокс: 15 ГБ ОЗУ без swap)
#   BFME_MEM_LIMIT=5g        лимит памяти контейнера
#   BFME_CACHE=<dir>         кэши (по умолчанию <repo>/../cache, как у build-apk.sh); свои подкаталоги linux-*
#   BFME_DOCKER="..."        как вызывать docker (как в build-apk.sh)
#   BFME_SMOKE_GAMEDIR=<dir> каталог данных игры для --smoke, монтируется только на чтение (по умолчанию пустой)
#   BFME_SMOKE_TIMEOUT=60    сколько секунд даём движку под Xvfb
#   BFME_SMOKE_ARGS="-win"   аргументы движка
#   BFME_SMOKE_REQUIRE_RENDER=1  считать дым успешным только при DX8Wrapper::Init на llvmpipe
# Результат: build/linux64-deploy/GeneralsMD/GeneralsXZH (RUNPATH на _deps/dxvk-src/lib, sdl3*-build, openal_soft-build),
# журнал: logs/build-linux-<время>.log, журнал запуска: logs/smoke-linux-<время>.log
set -euo pipefail
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO"
set -a; . docker/versions.env; set +a
MODE=docker; SMOKE=0
for a in "$@"; do case "$a" in
  --smoke) SMOKE=1 ;; --smoke-only) MODE=smoke ;; --image-only) MODE=image ;; --print-image-tag) MODE=tag ;;
  --inside) MODE=inside ;; --smoke-inside) MODE=smoke-inside ;;
  *) echo "неизвестный аргумент $a"; exit 2 ;; esac; done

JOBS="${BFME_JOBS:-3}"
PRESET=linux64-deploy
BUILD_DIR="build/${PRESET}"
BIN="$BUILD_DIR/GeneralsMD/GeneralsXZH"
IMAGE_TAG="bfme-linux-build:$(cat docker/linux-build.Dockerfile docker/linux-apt-packages.txt docker/versions.env tools/install-linux-toolchain.sh | sha256sum | cut -c1-12)"
# lavapipe задаём явно: иначе FilterSoftwareVulkanICDs() (GeneralsMD/Code/Main/SDL3Main.cpp) отбрасывает
# программные ICD и при отсутствии GPU Vulkan-устройства не будет вовсе.
# Имя файла зависит от сборки Mesa: Ubuntu 24.04 кладёт lvp_icd.json, другие дистрибутивы lvp_icd.<arch>.json.
LVP_ICD=""
for f in /usr/share/vulkan/icd.d/lvp_icd.x86_64.json /usr/share/vulkan/icd.d/lvp_icd.json; do [ -f "$f" ] && { LVP_ICD="$f"; break; }; done

[ "$MODE" = tag ] && { echo "$IMAGE_TAG"; exit 0; }

# ---------------------------------------------------------------- журнал
if [ -z "${BFME_LOG:-}" ]; then
  mkdir -p logs
  kind=build; [ "$MODE" = smoke ] && kind=smoke
  export BFME_LOG="$REPO/logs/${kind}-linux-$(date +%Y%m%d-%H%M%S).log"
  echo "Журнал: $BFME_LOG"
  set +e
  "$0" "$@" 2>&1 | tee "$BFME_LOG"
  rc=${PIPESTATUS[0]}
  echo "exit=$rc  журнал: $BFME_LOG"
  exit "$rc"
fi
ts() { echo "=== [$(date '+%F %T %Z')] $*"; }

# ---------------------------------------------------------------- сборка (в контейнере)
build_inside() {
  ts "окружение"; uname -a; nproc; free -m | head -2 || true
  gcc --version | head -1; cmake --version | head -1; echo "ninja $(ninja --version)"; ccache --version | head -1
  echo "vcpkg: $(git -C "$VCPKG_ROOT" rev-parse HEAD) (ожидается $VCPKG_LINUX_COMMIT)"
  [ "$(git -C "$VCPKG_ROOT" rev-parse HEAD)" = "$VCPKG_LINUX_COMMIT" ] || { echo "ОШИБКА: vcpkg не на пине"; exit 1; }
  # В worktree файл .git указывает за пределы /src, поэтому ревизию считает хост (BFME_GIT_REV).
  echo "repo: ${BFME_GIT_REV:-$(git rev-parse HEAD 2>/dev/null || echo неизвестно)}"

  local cache="${BFME_CACHE_INNER:-${HOME}/.cache/bfme}"
  mkdir -p "$cache"/{linux-ccache,linux-vcpkg-bin,vcpkg-dl,linux-vcpkg-buildtrees}
  export CCACHE_DIR="$cache/linux-ccache" VCPKG_DEFAULT_BINARY_CACHE="$cache/linux-vcpkg-bin" VCPKG_DOWNLOADS="$cache/vcpkg-dl"
  export VCPKG_MAX_CONCURRENCY="$JOBS" CMAKE_BUILD_PARALLEL_LEVEL="$JOBS" VCPKG_KEEP_ENV_VARS="VCPKG_MAX_CONCURRENCY"
  ccache --set-config=max_size=5G; ccache --set-config=compiler_check=content; ccache -z >/dev/null

  # gperf 3.1#7 (зависимость fontconfig по baseline манифеста) качается по http://ftp.gnu.org,
  # а порт 80 этого хоста с бокса не отвечает (vcpkg: "Download timed out"). Кладём архив в кэш
  # загрузок vcpkg сами по https; vcpkg всё равно сверит SHA-512 из portfile.
  prefetch_dl gperf-3.1.tar.gz \
    855ebce5ff36753238a44f14c95be7afdc3990b085960345ca2caf1a2db884f7db74d406ce9eec2f4a52abb8a063d4ed000a36b317c9a353ef4e25e2cca9a3f4 \
    https://ftp.gnu.org/pub/gnu/gperf/gperf-3.1.tar.gz https://mirrors.kernel.org/gnu/gperf/gperf-3.1.tar.gz

  # Каталог сборки, сконфигурированный по другому пути (другой worktree, не /src), CMake не примет.
  if [ -f "$BUILD_DIR/CMakeCache.txt" ] && ! grep -q '^CMAKE_HOME_DIRECTORY:INTERNAL=/src$' "$BUILD_DIR/CMakeCache.txt"; then
    ts "CMakeCache.txt от другого дерева, удаляю $BUILD_DIR"; rm -rf "$BUILD_DIR"
  fi
  ts "configure (preset ${PRESET}; vcpkg собирает зависимости при первом запуске)"
  cmake --preset "$PRESET" -DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache \
    -DVCPKG_INSTALL_OPTIONS="--x-buildtrees-root=$cache/linux-vcpkg-buildtrees;--clean-after-build"

  ts "build z_generals (GeneralsXZH), -j${JOBS}"
  cmake --build "$BUILD_DIR" -j "$JOBS" --target z_generals
  ccache -s | head -8 || true

  [ -x "$BIN" ] || { echo "ОШИБКА: нет бинарника $BIN"; exit 1; }
  ts "проверка бинарника"
  file "$BIN"
  local miss; miss="$(LD_LIBRARY_PATH="$(lib_path)" ldd "$BIN" | grep 'not found' || true)"
  [ -z "$miss" ] || { echo "ОШИБКА: не найдены библиотеки:"; echo "$miss"; exit 1; }
  LD_LIBRARY_PATH="$(lib_path)" ldd "$BIN" | grep -E 'SDL3|openal|gamespy' || true
  # DXVK native (cmake/dx8.cmake, v2.6) не в NEEDED: DX8Wrapper::Init грузит libdxvk_d3d8.so через dlopen (RUNPATH).
  ls "$BUILD_DIR"/_deps/dxvk-src/lib/libdxvk_d3d8.so.* >/dev/null 2>&1 || { echo "ОШИБКА: нет libdxvk_d3d8.so в $BUILD_DIR/_deps/dxvk-src/lib"; exit 1; }
  echo "DXVK: $(ls "$BUILD_DIR"/_deps/dxvk-src/lib/libdxvk_d3d8.so.*.*)"
  ts "ГОТОВО: $BIN ($(du -h "$BIN" | cut -f1), ${BFME_GIT_REV:-?})"
}

prefetch_dl() { # имя sha512 url...
  local name="$1" sum="$2" out="$VCPKG_DOWNLOADS/$1"; shift 2
  [ -f "$out" ] && echo "$sum  $out" | sha512sum -c --quiet - 2>/dev/null && return 0
  local u; for u in "$@"; do
    curl -fsSL --retry 2 -m 120 -o "$out.part" "$u" && echo "$sum  $out.part" | sha512sum -c --quiet - && { mv "$out.part" "$out"; echo "prefetch: $name <- $u"; return 0; }
  done
  rm -f "$out.part"; echo "prefetch: $name не скачан (vcpkg попробует сам)"
}

# Каталоги .so, которые линкуются из дерева сборки (как копирует build-linux.yml форка).
lib_path() {
  local d p=""
  for d in "$BUILD_DIR" "$BUILD_DIR/_deps/sdl3-build" "$BUILD_DIR/_deps/sdl3_image-build" "$BUILD_DIR/_deps/openal_soft-build" \
           $(find "$BUILD_DIR/_deps/dxvk-src/lib" -maxdepth 0 -type d 2>/dev/null) $(dirname $(find "$BUILD_DIR" -name 'libgamespy.so*' 2>/dev/null | head -1) 2>/dev/null); do
    [ -d "$d" ] && p="${p:+$p:}$REPO/$d"
  done
  echo "$p"
}

# ---------------------------------------------------------------- запуск под Xvfb (в контейнере без сети)
smoke_inside() {
  [ -x "$BIN" ] || { echo "ОШИБКА: нет бинарника $BIN, сначала tools/build-linux.sh"; exit 1; }
  local tmo="${BFME_SMOKE_TIMEOUT:-60}" gamedir=/game
  [ -n "$LVP_ICD" ] || { echo "ОШИБКА: нет lvp_icd*.json в /usr/share/vulkan/icd.d (mesa-vulkan-drivers)"; exit 1; }
  ts "Vulkan: VK_ICD_FILENAMES=$LVP_ICD"
  export VK_ICD_FILENAMES="$LVP_ICD" VK_DRIVER_FILES="$LVP_ICD"
  # Xvfb в своём сетевом пространстве контейнера (--network none): абстрактный сокет X не виден боксу.
  Xvfb :99 -screen 0 1280x720x24 -nolisten tcp -noreset >/tmp/xvfb.log 2>&1 &
  local xpid=$!
  export DISPLAY=:99
  for _ in $(seq 1 50); do xdpyinfo >/dev/null 2>&1 && break; sleep 0.1; done
  xdpyinfo >/dev/null 2>&1 || { echo "ОШИБКА: Xvfb не поднялся"; cat /tmp/xvfb.log; exit 1; }
  ts "Xvfb :99 запущен"
  vulkaninfo --summary 2>&1 | grep -E 'apiVersion|deviceName|driverName|driverInfo|deviceType' || true

  export LD_LIBRARY_PATH="$(lib_path)${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
  export SDL_VIDEO_DRIVER=x11 DXVK_WSI_DRIVER=SDL3 DXVK_LOG_LEVEL=info DXVK_LOG_PATH=none DXVK_HUD=0
  export ALSOFT_DRIVERS=null ALSOFT_DISABLE_CPU_EXTS=all SDL_AUDIO_DRIVER=dummy
  export HOME=/tmp/home XDG_CACHE_HOME=/tmp/home/.cache; mkdir -p "$HOME"
  # Рабочий каталог: данные только на чтение (/game) или пустой каталог; движок пишет в $HOME.
  local run=/tmp/run
  mkdir -p "$run"
  local have_data=0
  if [ -d "$gamedir" ] && [ -n "$(ls -A "$gamedir" 2>/dev/null)" ]; then
    for f in "$gamedir"/*; do ln -s "$f" "$run/"; done
    have_data=1
    # BFME_GAMEDATA включает BfmeBIGFileSystem (E1-21b): слои base -> patch222 -> MobileLayer вместо поиска ассетов ZH.
    export BFME_GAMEDATA="${BFME_GAMEDATA:-$gamedir}"
    ts "данные игры: $gamedir (только чтение), $(ls "$gamedir" | wc -l) записей, BFME_GAMEDATA=$BFME_GAMEDATA"
  else
    ts "данные игры: нет (пустой каталог)"
  fi
  ln -sf "$REPO/$BIN" "$run/GeneralsXZH"
  cd "$run"
  ts "запуск: GeneralsXZH ${BFME_SMOKE_ARGS:--win} (таймаут ${tmo} с)"
  # ReleaseCrash() на SDL3 показывает SDL_ShowSimpleMessageBox (Core/.../Debug.cpp) и ждёт нажатия, под Xvfb
  # это вечное ожидание. Поэтому следим за журналом: после строки ReleaseCrash даём 3 с на запись и снимаем процесс.
  ./GeneralsXZH ${BFME_SMOKE_ARGS:--win} > /tmp/game.log 2>&1 &
  local gpid=$! t=0 how=exit
  while kill -0 "$gpid" 2>/dev/null; do
    if grep -q 'ReleaseCrash reason' /tmp/game.log; then sleep 3; how=releasecrash; break; fi
    if [ "$t" -ge "$tmo" ]; then how=timeout; break; fi
    sleep 1; t=$((t + 1))
  done
  if [ "$how" != exit ]; then kill -TERM "$gpid" 2>/dev/null; sleep 2; kill -KILL "$gpid" 2>/dev/null || true; fi
  set +e; wait "$gpid"; local rc=$?; set -e
  cat /tmp/game.log
  kill "$xpid" 2>/dev/null || true
  case "$how" in
    exit) ts "движок завершился сам, код $rc" ;;
    timeout) ts "таймаут ${tmo} с: движок жив и крутит цикл, снят скриптом (код $rc)" ;;
    releasecrash) ts "ReleaseCrash: движок ждал нажатия в окне SDL, снят скриптом через 3 с (код $rc)" ;;
  esac

  ts "сводка: докуда дошёл движок"
  stage() { if grep -qE "$2" /tmp/game.log; then echo "  ok   $1"; else echo "  --   $1"; fi; }
  stage "окно SDL3 (Vulkan) создано"           'SDL3 window created successfully'
  stage "ФС архивов: BfmeBIGFileSystem"       'createArchiveFileSystem\(\) -> BfmeBIGFileSystem'
  stage "ФС архивов: StdBIGFileSystem (ZH)"   'createArchiveFileSystem\(\) -> StdBIGFileSystem'
  stage "INI: GameData прочитан"              'initSubsystem\(.TheWritableGlobalData.\) END'
  stage "DX8Wrapper::Init"                    'DX8Wrapper::Init'
  stage "Vulkan-устройство llvmpipe (DXVK)"   'llvmpipe'
  stage "ReleaseCrash"                        'ReleaseCrash reason'
  echo "  последняя подсистема: $(grep -oE "initSubsystem\('[^']+'\) (START|END)" /tmp/game.log | tail -1)"
  grep -E 'ReleaseCrash reason|INIException|ERROR: No files read|threw ' /tmp/game.log | head -5 | sed 's/^/  > /' || true

  # Критерий: процесс стартовал, окно SDL3 под Vulkan создано и движок дошёл до чтения INI.
  # Рендер (DX8Wrapper::Init + llvmpipe) на данных BFME сейчас недостижим: init падает раньше на terrain.ini (E1-26b);
  # BFME_SMOKE_REQUIRE_RENDER=1 делает его обязательным (для E1-26b/E1-32).
  grep -q 'SDL3 window created successfully' /tmp/game.log && grep -q 'loadFileDirectory' /tmp/game.log \
    || { ts "SMOKE: движок не дошёл до чтения INI"; return 3; }
  if [ "${BFME_SMOKE_REQUIRE_RENDER:-0}" = 1 ] && ! { grep -q 'DX8Wrapper::Init' /tmp/game.log && grep -q llvmpipe /tmp/game.log; }; then
    ts "SMOKE: рендер не достигнут (BFME_SMOKE_REQUIRE_RENDER=1)"; return 4
  fi
  [ "$have_data" = 1 ] || ts "без данных ожидаемо: остановка на отсутствии Data\\INI (ReleaseCrash)"
  ts "SMOKE OK"
}

# ---------------------------------------------------------------- docker
if [ -n "${BFME_DOCKER:-}" ]; then DOCKER="$BFME_DOCKER"
elif docker info >/dev/null 2>&1; then DOCKER="docker"; else DOCKER="sudo docker"; fi
CACHE="${BFME_CACHE:-$(cd "$REPO/.." && pwd)/cache}"
GIT_REV="$(git rev-parse --short HEAD 2>/dev/null || echo ?)"
[ -z "$(git status --porcelain --untracked-files=no 2>/dev/null)" ] || GIT_REV="${GIT_REV}+изменения"

ensure_image() {
  if ! $DOCKER image inspect "$IMAGE_TAG" >/dev/null 2>&1; then
    ts "сборка образа $IMAGE_TAG"
    $DOCKER build --network host -f docker/linux-build.Dockerfile --build-arg UBUNTU_IMAGE="$UBUNTU_IMAGE" -t "$IMAGE_TAG" .
  else ts "образ $IMAGE_TAG уже есть"; fi
}

# Общие ограничения контейнера (те же, что ставит обёртка bfme-ci-docker раннера).
HARDEN=(--cap-drop=ALL --security-opt no-new-privileges --pids-limit 4096 -u "$(id -u):$(id -g)")

run_smoke() {
  local gd=() ; [ -n "${BFME_SMOKE_GAMEDIR:-}" ] && gd=(-v "$(cd "$BFME_SMOKE_GAMEDIR" && pwd):/game:ro")
  ts "запуск под Xvfb в контейнере $IMAGE_TAG (--network none)"
  $DOCKER run --rm --network none --memory="${BFME_MEM_LIMIT:-5g}" --memory-swap="${BFME_MEM_LIMIT:-5g}" "${HARDEN[@]}" \
    -e BFME_LOG="$BFME_LOG" -e BFME_SMOKE_TIMEOUT="${BFME_SMOKE_TIMEOUT:-60}" -e BFME_SMOKE_ARGS="${BFME_SMOKE_ARGS:--win}" \
    -e BFME_SMOKE_REQUIRE_RENDER="${BFME_SMOKE_REQUIRE_RENDER:-0}" \
    -v "$REPO:/src" "${gd[@]}" -w /src "$IMAGE_TAG" tools/build-linux.sh --smoke-inside
}

case "$MODE" in
  inside) build_inside ;;
  smoke-inside) smoke_inside ;;
  image) ensure_image ;;
  smoke) ensure_image; run_smoke ;;
  docker)
    ensure_image
    mkdir -p "$CACHE/linux-home"
    ts "сборка в контейнере $IMAGE_TAG (jobs=$JOBS, mem=${BFME_MEM_LIMIT:-5g})"
    $DOCKER run --rm --network host --memory="${BFME_MEM_LIMIT:-5g}" --memory-swap="${BFME_MEM_LIMIT:-5g}" "${HARDEN[@]}" \
      -e HOME=/cache/linux-home -e BFME_LOG="$BFME_LOG" -e BFME_JOBS="$JOBS" -e BFME_CACHE_INNER=/cache -e BFME_GIT_REV="$GIT_REV" \
      -v "$REPO:/src" -v "$CACHE:/cache" -w /src "$IMAGE_TAG" tools/build-linux.sh --inside
    if [ "$SMOKE" = 1 ]; then run_smoke; fi ;;
esac
