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
- `tests/reports/` — отчёты прогонов на устройствах.

Все юнит-тесты (`tests/*/test_*.py`, stdlib unittest), из корня репозитория: `tools/replay-test selftest`.
