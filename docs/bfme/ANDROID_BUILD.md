# BFME1 Mobile: сборка APK (Э0)

Владелец: Android-платформа. Основа: рецепт MYSOREZ/GeneralsZH-Android-Port (`build-android.yml` форка, удалён из bfme в E1-02,
`scripts/build/android/build-local-sandboxed.sh`), коммит форка 8ef2349 (27.09.2026).

## Одна команда
```
tools/start-dockerd.sh      # только на боксе: dockerd без systemd (без iptables/docker0, data-root в /workspace)
tools/build-apk.sh          # образ (если нет) -> контейнер -> APK в out/, журнал в logs/build-apk-*.log
tools/build-apk.sh --native # запасной путь без Docker: тот же тулчейн через tools/install-toolchain.sh в /opt/bfme-toolchain
```
Переменные: `BFME_JOBS` (по умолчанию 4; бокс 15 ГБ без swap), `BFME_MEM_LIMIT` (лимит памяти контейнера, по умолчанию 6g),
`BFME_CACHE` (ccache, бинарный кэш vcpkg, Gradle), `BFME_SECRETS` (ключ подписи, вне git).

## Зафиксированные версии
Все версии и SHA-256 лежат в `docker/versions.env`, это единственный источник:
Ubuntu 24.04 (digest), NDK r27c 27.2.12479018, platform android-35, build-tools 35.0.0, cmdline-tools 13114758,
Temurin JDK 17.0.20.1+1, CMake 3.31.6, Ninja 1.12.1, Gradle 8.9 (AGP 8.5.2 из форка), vcpkg 42e4e33e (Android)
и ffc071e0 (linux64-deploy, `$BFME_VCPKG_LINUX_ROOT`), qemu-user-static (qemu-aarch64 для тестов GameMath), libegl-dev/libgles-dev.
Пакеты NDK, platform и build-tools ставит sdkmanager, он сверяет их SHA-1 с манифестом Google.
Список apt-пакетов образа с версиями: `/opt/bfme-toolchain/apt-manifest.txt` внутри образа.

## Целевые параметры (решения Лида, Э0)
- ABI только arm64-v8a, minSdk 29 (Gradle, `CMakePresets.json` ANDROID_PLATFORM android-29, триплет vcpkg), targetSdk и compileSdk 35.
- 16 КБ страницы: `-Wl,-z,max-page-size=16384 -Wl,-z,common-page-size=16384` в корневом CMakeLists (все цели CMake/FetchContent),
  в meson cross-файле DXVK и в триплете vcpkg. Плюс `ANDROID_SUPPORT_FLEXIBLE_PAGE_SIZES=ON`.
  Проверка в build-apk.sh по всем .so из `lib/arm64-v8a` и Turnip из `assets/default_driver`:
  все LOAD align >= 2**14 и конец GNU_RELRO кратен 0x4000. Готовые бинарники не из нашей сборки
  (NDK `libc++_shared.so`, ANGLE из AOSP) имеют RELRO на 4 КБ: это WARN (`BFME_RELRO_WAIVE`), так как
  следующий RW-сегмент у них начинается на другой 16-КБ странице; для всего, что собираем мы, это FAIL.
  `zipalign -c -P 16 -v 4` справочно (`useLegacyPackaging true`: .so в APK сжаты и распаковываются при установке).
- Только libmain на 30 Гц (dual-hz/libmain60 не собираем).
- Флаги детерминизма `-ffp-contract=off -fno-strict-aliasing -fwrapv -fno-delete-null-pointer-checks` ставит
  `cmake/compilers.cmake` (апстрим + форк) для всех компиляторов, кроме MSVC; по compile_commands.json Android
  они есть у всех 262 файлов GameLogic. `SAGE_USE_DETERMINISTIC_MATH` выключен до своей GameMath (E1-49).
- R4 (docs/fpcr-audit.md): сборка падает, если в compile_commands.json (CMake и meson DXVK), логах meson
  или vcpkg (`buildtrees/**/*.log`) встречается `-ffast-math`, `-Ofast` или `-funsafe-math-optimizations`
  (`tools/ci/check-fp-flags.sh`). Порты vcpkg из бинарного кэша логов не дают: полная проверка — при холодном кэше.
- T5: `tools/fpcr/fpcr_check.py` по всем .so APK против `tools/fpcr/fpcr-baseline.json`: новая `MSR FPCR`
  или новый импорт fesetenv/fesetround/feholdexcept/... валит сборку (в базе: libmain — setFPMode и compiler-rt,
  libSDL3 — пара feholdexcept/fesetenv в NEON-конвертерах звука; Turnip — только предупреждение).
- c++_shared (libc++_shared.so из NDK кладётся в APK).

## Подпись
Отладочный ключ форка (`android/app/debug.keystore`) удалён из дерева. Gradle берёт ключ из переменных `BFME_KEYSTORE_FILE`,
`BFME_KEYSTORE_PASSWORD`, `BFME_KEY_ALIAS` и `BFME_KEY_PASSWORD`, их выставляет build-apk.sh:
- бокс: `/workspace/bfme-mobile/secrets/bfme-debug.keystore` + `keystore.pass` (создаются при первой сборке, chmod 600);
- CI (self-hosted раннер на боксе): копия ключа в `/workspace/bfme-mobile/ci/secrets/` (владелец ghrunner, 0600),
  путь задаёт `.env` раннера (`BFME_SECRETS`), `BFME_REQUIRE_KEYSTORE=1` — без ключа сборка падает, новый не создаётся.
  В GitHub Secrets ключ не кладём. Переменные `BFME_DEBUG_KEYSTORE_B64`/`BFME_KEYSTORE_PASSWORD` остались только
  для ручного запуска вне бокса. Подробнее: `tools/ci/README.md`.

## Файлы игры
В APK их нет. В APK попадают только шрифты Liberation, dxvk.conf, DefaultOptions.ini и Turnip (драйвер для Adreno) из стейджинга форка.
