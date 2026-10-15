# tests — детерминизм и производительность

Владелец: **QA** (детерминизм и производительность).

Реплей-тесты в CI (CRC по кадрам), прогоны на двух экземплярах, профилирование FPS, памяти и нагрева,
матрица устройств (первый приоритет — Google Pixel: Mali, PowerVR).

Фикстуры (`*.w3d` и т. п.) здесь допустимы, только если они самодельные или синтетические.
Файлы игры EA в репозиторий не кладём никогда.

`tests/replay/crclog.py` читает логи `bfme-crclog/1` (опция движка `-gxCrcLog`), stderr форка и CRC из `.rep`.
Проверка парсера: `cd tests/replay && python3 -m unittest test_crclog`.

Состав (QA):

- `tests/replay/` — реплей-тесты CRC: раннер `run_replay_tests.py`, манифест, фейковый движок, эталоны
  фейкового движка, шаблон CI `ci/replay-tests.yml` (не активен, переносит Платформа). Методика —
  `docs/REPLAY-TESTS.md`.
- `tests/perf/analyze_frames.py` — разбор `frames.csv` perf-run по TEST-PLAN §5.4, включая проверку темпа логики
  `logic_pace` (§5.4.1: 30 ± 1 тик/с); синтетические данные — `--synthetic`.
- `tests/terrain/` — модель высот 16 бит (E1-25) без данных игры.
- `tests/vfs/` — VFS BIG4/BIGF и порядок монтирования (E1-21, владелец кода: Форматы и данные), см. ниже.
- `tests/reports/` — отчёты прогонов на устройствах.

Все юнит-тесты (`tests/*/test_*.py`, stdlib unittest), из корня репозитория: `tools/replay-test selftest`.

## vfs (E1-21): VFS BIG4/BIGF и порядок монтирования
Отдельный CMake-проект без vcpkg и без линковки игры. Бинарник `bfme_vfs_tests` компилирует **настоящие** исходники VFS
апстрима (`StdBIGFileSystem`, `ArchiveFileSystem`, `StdLocalFileSystem`, `RAMFile`, `AsciiString`, `CompressionManager` …)
с дефайнами Linux-сборки ZH, плюс `formats/vfs`. Глобалы движка, которые эти файлы только упоминают, заглушены в `vfs/EngineStubs.cpp`.

```bash
git submodule update --init references/fbraz3-dxvk     # нужны только заголовки native/windows
cmake -S tests -B build/tests -G Ninja && cmake --build build/tests
ctest --test-dir build/tests -L unit                    # синтетика, для CI
BFME_GAMEDATA=/workspace/bfme-mobile/gamedata BFME_ORACLE_DIR=/workspace/bfme-mobile/tools/oracle/out \
  ctest --test-dir build/tests --output-on-failure      # + интеграционные (label gamedata)
```

- `unit`: синтетические BIG4/BIGF генерируются в тесте (`vfs/SyntheticBig.h`), данных EA нет.
- `gamedata`: копия игры из `BFME_GAMEDATA` и выгрузки оракула OpenSAGE из `BFME_ORACLE_DIR`. Без `BFME_ORACLE_DIR` тест пропускается (код 77). Запасного пути `<BFME_GAMEDATA>/../tools/oracle/out` нет, дампы в git не лежат. Каталог задаётся явно (`export BFME_ORACLE_DIR=/path/to/tools/oracle/out`); в нём после прогона оракула лежат `ini.big.json` и `maps.big.maps.json`.
  В `BFME_GAMEDATA` тесты ничего не пишут: отдельные архивы монтируются через симлинки во временном каталоге. Корень, где проверяется не сам `ini.big`, получает ещё симлинк на `ini.big` из той же копии: без него `init()` завершается, потому что корневой `ini.big` обязателен. Если в копии нет `ini.big`, такой тест пропускается (код 77). Весь корень игры при этом не монтируется, чтобы `_patch222` не перекрыл `maps.big`.
- `vfs.registry_complete` проверяет, что каждый `TEST()` зарегистрирован в ctest.
