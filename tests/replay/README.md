# tests/replay — реплей-тесты CRC

Методика и правила: [`docs/REPLAY-TESTS.md`](../../docs/REPLAY-TESTS.md). Python 3.11+, без зависимостей.

```
cd tests/replay
python3 -m unittest -v test_replay_runner                    # самопроверка раннера (без движка)
python3 run_replay_tests.py run --stage selftest              # кейсы на фейковом движке
# настоящий движок (headless, E1-43), только на боксе с данными:
export BFME_HEADLESS_ENGINE=/path/to/headless BFME_GAMEDATA=/path/to/data BFME_REPLAYS=/path/to/reps
python3 run_replay_tests.py run --stage e2 --junit out/junit.xml --summary out/summary.md
python3 run_replay_tests.py bless --case e2-two-battalions-10min --reason "новый INI урона"
python3 run_replay_tests.py compare golden/linux-x86_64/X.crclog out/X.actual.crclog
python3 run_replay_tests.py rep-crcs some.rep --offset 1      # CRC из реплея; метка L -> кадр L-1 (REPLAY-TESTS §2)
python3 run_replay_tests.py compare X.crclog some.rep --rep-offset 1
python3 run_replay_tests.py run --stage e1 --record-missing  # нет эталона -> записать (2 одинаковых прогона)
python3 run_replay_tests.py --timeout-scale 20 run ...        # timeout_s x20 (авто для --arch *arm64* + qemu-*)
python3 run_replay_tests.py --case-timeout 600 run ...       # таймаут кейса до множителя (иначе timeout_s / timeout_s_arch)
python3 run_replay_tests.py run --deadline-min 75 ...         # лимит всего прогона: остальные кейсы -> deadline, код 4
# из корня репозитория (так же в CI, через $BFME_DOCKER replay): все юнит-тесты tests/*/test_*.py
tools/replay-test selftest                                     # код 0/1; -q, --only replay|perf|terrain
tools/replay-test selftest --test-timeout 60                  # лимит на каждый тест (по умолчанию 120 с; 0 = выкл.)
```

Кейсы с `-qaScenario` готовый `.rep` не требуют: сценарий пишет свой `.rep` в `-qaOut` (по умолчанию
`{case_out}` = `<out>/<case>/last.rep`) и CRC-лог. Нет входного файла у кейса — `missing_input`, прогон идёт
дальше. Запись движка в `cwd` только на чтение (EROFS) отмечается в результате и в `<case>.engine.log`.
Подробности для CI: `ci/README.md`.

| Файл | Что |
|---|---|
| `crclog.py` | формат `bfme-crclog/1`, разбор stderr форка и `.rep`, сравнение |
| `run_replay_tests.py` | раннер: run / bless / compare / rep-crcs / selftest; JUnit, сводка, коды выхода 0–4 |
| `manifest.toml` | кейсы |
| `fake_engine.py`, `test_replay_runner.py` | фейковый движок и юнит-тесты |
| `golden/<arch>/` | эталоны (только числа CRC) |
| `fixtures/` | фейковые реплеи для самопроверки (данных EA нет) |
| `engine-hooks/README.md` | история хука `-gxCrcLog` (влит в `bfme`, PR #7); снимок патча — только на боксе |
| `ci/replay-tests.yml` | предложение CI для Android-платформы (self-hosted раннер бокса; в `.github/` не кладём) |

Реплеи (`.rep`, только команды игроков) можно класть в `fixtures/`: файл ≤ 1 МБ, без git LFS (решение Оркестратора).
Файлы игры EA и карты сюда не кладутся никогда (см. `docs/game-files-access.md`).
Движку всегда передаётся абсолютный путь к `.rep`; для `-gxCrcLog` — `-jobs 1`; шапка crclog содержит `# logic_hz=30`.
