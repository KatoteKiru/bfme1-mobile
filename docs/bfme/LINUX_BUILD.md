# BFME1 Mobile: десктопная сборка под Linux x86_64 (E1-05)

Владелец: Лид движка (ревью: Android-платформа). Основа: `build-linux.yml` и `resources/dockerbuild/Dockerfile.linux` форка.
Зачем: быстрый цикл отладки движка без телефона и база для E1-17 (d3d8gles на Linux), E1-26b (terrain.ini), E1-32 (рендер террейна).

## Одна команда
```
tools/start-dockerd.sh                 # только на боксе: dockerd без systemd (как для APK)
tools/build-linux.sh                   # образ (если нет) -> configure -> сборка GeneralsXZH в контейнере
tools/build-linux.sh --smoke-only      # запуск уже собранного бинарника под Xvfb на Mesa lavapipe, без данных
BFME_SMOKE_GAMEDIR=/workspace/bfme-mobile/gamedata tools/build-linux.sh --smoke-only   # то же с данными BFME1 (только чтение)
tools/build-linux.sh --smoke           # сборка и сразу дым
```
Результат: `build/linux64-deploy/GeneralsMD/GeneralsXZH` (ELF x86-64, ~230 МБ, `RelWithDebInfo`, с отладочной информацией).
Рядом в дереве сборки и по RUNPATH: `_deps/dxvk-src/lib/libdxvk_d3d8.so` (DXVK native 2.6, грузится через `dlopen` в `DX8Wrapper::Init`),
`_deps/sdl3-build/libSDL3.so.0`, `_deps/sdl3_image-build`, `_deps/openal_soft-build/libopenal.so.1`, `libgamespy.so`.
Журналы: `logs/build-linux-<время>.log`, `logs/smoke-linux-<время>.log` (каталог `logs/` в `.gitignore`).

Переменные: `BFME_JOBS` (по умолчанию 3; бокс 15 ГБ без swap, при параллельной сборке APK не выше 3–4), `BFME_MEM_LIMIT`
(лимит памяти контейнера, по умолчанию 5g), `BFME_CACHE` (ccache, бинарный кэш vcpkg; подкаталоги `linux-*`, общий с APK
только кэш загрузок `vcpkg-dl`), `BFME_SMOKE_GAMEDIR`, `BFME_SMOKE_TIMEOUT` (60 с), `BFME_SMOKE_ARGS` (`-win`),
`BFME_SMOKE_REQUIRE_RENDER=1` (считать дым успешным только при `DX8Wrapper::Init` на llvmpipe).

## Требования
- Docker (на боксе — `tools/start-dockerd.sh`), x86_64, ~6 ГБ свободной памяти при `-j3`, ~8 ГБ на диске под образ, `build/` и кэши.
- Сеть только на этапе образа и configure (vcpkg, FetchContent: SDL3, SDL3_image, OpenAL Soft, DXVK native, GameSpy).
  Дым идёт в контейнере `--network none`.
- GPU не нужен: Vulkan даёт Mesa lavapipe (llvmpipe) из образа.

## Зафиксированные версии
Всё в `docker/versions.env`: Ubuntu 24.04 (digest), CMake 3.31.6, Ninja 1.12.1, vcpkg `VCPKG_LINUX_COMMIT` (ffc071e0, пин
`build-linux.yml` форка). Компилятор — GCC 13 из Ubuntu 24.04, Mesa 25.2 (lavapipe, LLVM 20). Тег образа — хеш
`docker/linux-build.Dockerfile`, `docker/linux-apt-packages.txt`, `docker/versions.env`, `tools/install-linux-toolchain.sh`
(`tools/build-linux.sh --print-image-tag`). Список apt-пакетов с версиями: `/opt/bfme-linux-toolchain/apt-manifest.txt` в образе.

Особенности:
- `gperf` 3.1 (зависимость `fontconfig`) vcpkg качает по `http://ftp.gnu.org`, порт 80 с бокса не отвечает. Скрипт заранее кладёт архив
  в кэш загрузок по https; SHA-512 сверяет и скрипт, и vcpkg.
- В git worktree файл `.git` указывает за пределы `/src`, поэтому ревизию для журнала считает хост (`BFME_GIT_REV`).
- Контейнер запускается с `--cap-drop=ALL --security-opt no-new-privileges --pids-limit 4096` от UID пользователя.

## Дым под Xvfb
`--smoke-only` поднимает Xvfb `:99` внутри контейнера, задаёт `VK_ICD_FILENAMES` на `lvp_icd*.json` явно
(иначе `FilterSoftwareVulkanICDs()` в `GeneralsMD/Code/Main/SDL3Main.cpp` выкинет программный ICD), `SDL_VIDEO_DRIVER=x11`,
`DXVK_WSI_DRIVER=SDL3`, звук в null. Данные (если заданы) монтируются в `/game` только на чтение, `BFME_GAMEDATA=/game`
включает `BfmeBIGFileSystem` (E1-21b). Движок пишет только в `$HOME=/tmp/home` контейнера.

`ReleaseCrash()` на SDL3 показывает `SDL_ShowSimpleMessageBox` и под Xvfb ждёт нажатия вечно, поэтому скрипт следит за журналом:
после строки `ReleaseCrash reason` даёт 3 с и снимает процесс. В конце печатается сводка стадий (окно SDL3, файловая система
архивов, INI, `DX8Wrapper::Init`, llvmpipe, ReleaseCrash) и последняя подсистема `initSubsystem`.

Состояние на 09.10.2026:
- без данных: окно SDL3 (Vulkan) создаётся, движок останавливается на отсутствии `Data\INI\Default\GameData` (ReleaseCrash) — ожидаемо;
- с данными BFME1 + 2.22 (`BFME_GAMEDATA`): `BfmeBIGFileSystem` монтирует 53 архива (5 слоя patch222, 48 base),
  `Data\INI\Default\GameData.ini` читается, затем `Data\INI\GameData.ini` из `_patch222.big` бросает `INI_UNKNOWN_TOKEN`
  (в журнале `exception caught (unrecognized type)`) → ReleaseCrash в `TheWritableGlobalData`. В блоке `GameData` 2.22
  109 полей, которых нет в таблице ZH `GlobalData.cpp` (первое — `ShowProps`). До `terrain.ini` (E1-26b) и до рендера
  движок пока не доходит. Нужна терпимость к неизвестным полям (E1-23) или минимальная схема GameData для M1.
- Критерий E1-05 «доходит до `DX8Wrapper::Init` на llvmpipe» включается `BFME_SMOKE_REQUIRE_RENDER=1`, когда init пройдёт INI.

## Ограничения
- Файлы EA никогда не попадают в образ, git и `build/`: `.dockerignore` пропускает в контекст образа только `docker/` и два скрипта тулчейна.
- CI-задачи пока нет (Actions выключены); команда та же, что будет в CI.
- DXVK native 2.6 берётся `FetchContent` без `URL_HASH` (`cmake/dx8.cmake`, апстрим).
- ccache почти не работает из-за PCH (`Uncacheable calls` ~99 %); повторные сборки ускоряет инкрементальный `build/linux64-deploy`.
