# REPLAY-TESTS — реплей-тесты CRC (QA, черновик v0.1, 02.10.2026)

Владелец: QA. Код: `tests/replay/`. Связанные документы (не дублирую, ссылаюсь):
`PLAN.md` (Э2), `docs/E1-BACKLOG.md` (E1-43, E1-45), `docs/NET-DESIGN.md` §1.5, §1.7, §3.2 (CRC в сети,
отчёт о рассинхроне), `docs/adr/ADR-0001-base-engine.md` (форк, владение каталогами, правила патчей),
`docs/TEST-PLAN.md` (общий план, серьёзность), `docs/game-files-access.md` (данные EA).

## 0. Кратко

- **Цель Э2** (PLAN.md): «два батальона разных фракций сходятся и бьются до конца, без падений за 10 минут,
  а реплей этого боя воспроизводится с совпадением CRC». Реплей-тест — автоматическая проверка второй половины.
- **Как:** headless-движок (E1-43) проигрывает `.rep`, на каждом кадре логики пишет CRC состояния игры
  (и бегущие CRC секций), раннер сравнивает с эталоном (golden) и выдаёт первый расходящийся кадр и секцию.
- **Работает уже сейчас** с форком MYSOREZ (`-gxCrcEveryFrame` + `GX_NET_TRACE=1`, разбор stderr).
  Хук `-gxCrcLog` пишет тот же CRC в отдельный файл без сетевого трейса (§8). Парсер — `tests/replay/crclog.py`.
- **Данные EA** в CI-облако не уходят никогда: джобы с реальными реплеями идут только на self-hosted
  раннере на боксе. На каждый PR — самопроверка раннера на фейковом движке (без данных).
- Нужно от других: headless-бинарник и раннер (Android-платформа), принятие хука и ответы по `-replay`
  (Лид), реплей Э2 (Геймплей). См. §10.

## 1. Что уже есть в движке и форке

Подробно про CRC в сети — `NET-DESIGN.md` §1.5; здесь только то, что нужно тестам.

| Что | Где | Для тестов |
|---|---|---|
| `GameLogic::getCRC(CRC_RECALC)` — XferCRC по секциям: объекты, seed RNG, PartitionManager, PlayerList, AI | `GameLogic.cpp` (форк ~4846–4990) | CRC кадра; бегущий CRC после каждой секции = где разошлось |
| `REPLAY_CRC_INTERVAL = 100`, `MSG_LOGIC_CRC` (1095) в реплее; при проигрывании Recorder сверяет | `Recorder.cpp` | «встроенная» проверка: реплей сам содержит CRC каждые 100 кадров |
| `-headless -replay <file>` (ReplaySimulation) | TSH/форк, работает не только на Windows | основа E1-43 |
| `GXReplayCheck`: `-gxFastTo`, `-gxAutoQuit`, `-gxCrcEveryFrame` (или маркер `gx_crc_every_frame.txt`), итог в `gx_replay_check_result.txt` | форк `Common/GXReplayCheck.{h,cpp}`, `CommandLine.cpp` | выход по концу реплея, CRC каждого кадра |
| `[GX-NET] crc every frame N: X`, `crc parts frame N: afterObjects=… seed=…`, `afterPartition`, `afterPlayerList`, `afterAI` | `GameLogic.cpp` ~4232 и в `getCRC`, только при `GX_NET_TRACE` | режим `gx-stderr` раннера |
| `-gxCrcLog <path>`: файл `bfme-crclog/1`, CRC каждого кадра и бегущие CRC секций, без `GX_NET_TRACE` | `GXReplayCheck`, `CommandLine.cpp`, `GameLogic.cpp`; воркерам `-jobs` путь пробрасывает `ReplaySimulation` | режим `crclog`; **рекомендуется `-jobs 1`** |
| `ReplayCheckActivity`, `scripts/tooling/replay/rep_crc_every_frame.py`, `compare_gamestats.py` | форк | экран «Replay check» на телефоне — для прогона на устройстве (§5.3) |

Фиксы MYSOREZ по сдвигу первого чекпоинта (Recorder.cpp:122, `m_skippedOne`) — обязательны и для нас
(NET-DESIGN §3.2); реплей-тесты их ловят: без фикса «встроенная» проверка падает на первом чекпоинте.

## 2. Предположения о реплее BFME1 [проверить на Э2]

- Формат `.rep` у BFME1 — тот же Recorder SAGE (журнал команд, CRC `MSG_LOGIC_CRC` каждые C кадров).
  Наш парсер (`crclog.rep_recorded_crcs`) проверен только на реплеях ZH (тестовые файлы OpenSAGE
  `src/OpenSAGE/src/OpenSage.Game.Tests/Data/Rep/Assets/*.rep`, проходят без ошибок). **[не проверено на BFME1]**
- Логика 30 Гц (ADR-0002, NET-DESIGN §3.0 — решение Лида/Геймплея): 10 минут = 18 000 кадров.
  Если выберут 5 Гц — 3000 кадров; в манифесте меняется только `frames`.
- Записанные в реплей CRC стоят на кадрах 1, 101, 201, … (а не 0, 100, …): CRC, посчитанный в конце кадра N,
  уходит в поток команд и исполняется на кадре N+1. Раннер при сравнении «реплей против движка»
  должен учитывать сдвиг +1. **[проверить на первом же прогоне headless-сборки]**
- Реплей содержит seed, карту, игроков, версию; от данных игры зависит результат (INI) — поэтому golden
  привязан к хешу данных (`ini_crc` в шапке crclog).

## 3. Формат `bfme-crclog/1`

Текстовый, построчный, легко диффается и хранится в git (содержит только числа — данных EA в нём нет):

```
# bfme-crclog/1
# logic_hz=30              (кадров логики в секунду; в заголовке .rep частоты нет. 30, если сборка не 60 Гц)
# source=engine            (engine | gx-stderr | rep-recorded | golden)
# replay=e2-two-battalions-10min.rep
# replay_sha256=…          # engine_build_id=…  # arch=linux-x86_64  # ini_crc=…  # exe_crc=…
0 1A2B3C4D Objects=… Partition=… PlayerList=… AI=…
1 5E6F7A8B Objects=… Partition=… PlayerList=… AI=…
…
# end frames=18000 status=ok        (ok | mismatch | crash | timeout | exit)
```

- Значения секций — **бегущие** CRC после секции (как печатает форк). Первая секция, где значение
  разошлось, — место первого расхождения состояния.
- Парсер и сериализатор: `tests/replay/crclog.py`. Тот же модуль читает stderr форка и CRC из `.rep`.

## 4. Golden: эталоны и правила bless

- Путь: `tests/replay/golden/<arch>/<case>.crclog`, `arch` ∈ `linux-x86_64`, `linux-arm64`, `android-arm64`.
- `bless` (создание/обновление эталона) **требует двух одинаковых прогонов подряд** (иначе это не эталон,
  а случайность) и обязательной причины `--reason`; причина, дата (МСК), build id движка (`$BFME_ENGINE_BUILD_ID`) и коммит (`$GITHUB_SHA`) пишутся в шапку.
- Эталон обновляется только осознанным изменением логики (новый INI, фикс геймплея), в том же PR, что
  изменение, с пометкой в описании PR. «Перебless, потому что упало» без объяснения — запрещено.
- Хранятся в git: это только числа CRC. Сами `.rep` BFME1 хранятся вне репозитория (`$BFME_REPLAYS` на
  боксе), если в них есть что-либо из данных EA [решить: реплей = журнал команд, данных EA в нём нет, но
  имена карт/юнитов есть — до решения Андрея держим вне git].

## 5. Режимы сравнения

### 5.1 Против эталона (основной, каждый PR с изменениями движка)
Прогон → сравнение со своим golden по всем кадрам. Итог: `pass` или `mismatch` с первым расходящимся кадром,
первой разошедшейся секцией и последним совпавшим кадром.

### 5.2 Два прогона подряд (E1-43: «совпадающие между двумя прогонами»)
Ловит недетерминизм внутри одной машины (неинициализированная память, порядок в хеш-таблицах, время,
потоки). Используется при bless и раз в сутки на всех кейсах.

### 5.3 arm64 против x86_64 (E1-45)
- Сравнение `golden/linux-x86_64` и `golden/linux-arm64` (или `android-arm64` с телефона через
  экран «Replay check» форка).
- PLAN.md (риски): «одна сборка arm64 у всех» — между игроками кросс-архитектурный детерминизм не нужен.
  Но если x86_64 и arm64 расходятся, **x86_64-CI перестаёт быть заменой телефону**: тогда реплей-тесты Э2
  гонятся на arm64-раннере, а расхождение заводится как дефект «серьёзный» (TEST-PLAN §3.2), не блокер.
- MYSOREZ (DEV_BLOG 20.09, пересказ в NET-DESIGN §2) получил бит-в-бит на x86_64/aarch64 при
  `-ffp-contract=off`; это и проверяем. Флаги сборки — E1-45 (Лид + QA).

### 5.4 Против CRC, записанных в реплее
Быстрая проверка без эталона: каждые 100 кадров CRC движка = CRC из `.rep` (со сдвигом +1, §2).
Даёт ответ «совпадает с тем, как игралось», но не локализует расхождение точнее 100 кадров.
Команда: `run_replay_tests.py rep-crcs <file.rep>`.

### 5.5 Где искать причину расхождения
1. Первая разошедшаяся секция → подсистема (объекты / RNG / partition / игроки / AI).
2. Повторить с `-gxCrcEveryFrame` + `GX_NET_TRACE=1` на кадрах около расхождения.
3. Дамп `XferDeepCRC` на этом кадре на обеих сторонах и diff (тот же механизм, что в отчёте о рассинхроне,
   NET-DESIGN §3.2) — **[предложение для Лида: опция `-gxDeepCrcAt <frame>`, в 0001 не входит]**.

## 6. Раннер `tests/replay/run_replay_tests.py`

Python 3.11+, без внешних зависимостей. Кейсы — в `tests/replay/manifest.toml`.

| Команда | Что делает |
|---|---|
| `run [--stage S] [--case C] --junit J --summary M` | прогоны, сравнение с golden, JUnit XML, Markdown-сводка, `results.json` |
| `bless --case C --reason "…"` | два прогона, если совпали — записать golden |
| `compare A B` | сравнить два crclog/stderr/.rep, напечатать первый расходящийся кадр/секцию |
| `rep-crcs file.rep` | вытащить CRC, записанные в реплее, в формате crclog |

Коды выхода: `0` всё прошло; `1` расхождение CRC; `2` сбой движка (падение, нет CRC); `3` таймаут
(зависание); `4` ошибка конфигурации (нет golden, нет движка). CI различает «логика разошлась» и
«движок упал» — для триажа это разные дефекты.

Подстановки в аргументах: `{replay}`, `{replay_name}`, `{crclog}`, `{out}`; переменные окружения
`$BFME_HEADLESS_ENGINE`, `$BFME_GAMEDATA`, `$BFME_REPLAYS`.
Режимы: `gx-stderr` (форк как есть) и `crclog` (после хука 0001).

Самопроверка: `fake_engine.py` (имитирует движок: `FAKE_FRAMES`, `FAKE_SEED`, `FAKE_DIVERGE_AT`,
`FAKE_CRASH_AT`, `FAKE_HANG_AT`), 12 юнит-тестов (`python3 -m unittest test_replay_runner`, все проходят
02.10.2026), кейсы `selftest-*` с golden в `golden/linux-x86_64/`.

## 7. CI

Предложение: `tests/replay/ci/replay-tests.yml` (владелец `ci/` и `.github/` — Android-платформа, ADR-0001;
QA сам не включает).

| Джоба | Где | Когда | Данные EA |
|---|---|---|---|
| `selftest` | `ubuntu-latest` | каждый PR | нет |
| `replay-crc` | self-hosted `[linux, x64, bfme-gamedata]` на боксе | push в `bfme`, PR из своего репо, вручную | да, локально, только чтение |
| `replay-crc-arm64` | self-hosted `[linux, arm64, bfme-gamedata]` | вручную; `continue-on-error` | да, локально |

- GitHub-hosted arm64 (`ubuntu-24.04-arm`) для кейсов с данными **нельзя** — пришлось бы отдать туда данные.
  Он годится только для `selftest` и для сравнения на синтетике.
- Артефакты — только `*.crclog`, логи движка, JUnit, сводка. Логи движка надо проверить на отсутствие
  содержимого файлов игры (печатаются имена INI/объектов — это не данные EA, но решение за Андреем).
- PR из чужих форков на self-hosted не запускаются (репозиторий приватный, но правило в YAML есть).
- Время: реплей 18 000 кадров headless — оценка единицы минут на кейс **[замерить на E1-43]**.

## 8. Хук движка `-gxCrcLog`

Опция в движке, выключена по умолчанию. Симуляцию не меняет: CRC — тот же `getCRC(CRC_RECALC)` через
`XferCRC` (`XFER_CRC`), он только читает состояние. Seed — локальная копия `GetGameLogicRandomSeedCRC()`,
в поток CRC попадает чтением (`XferCRC::xferImplementation`).

Файлы (пометки `// BFME:`), реестр — `docs/UPSTREAM-PATCHES.md`:

- `GeneralsMD/Code/GameEngine/Include/Common/GXReplayCheck.h`
- `GeneralsMD/Code/GameEngine/Source/Common/GXReplayCheck.cpp`
- `GeneralsMD/Code/GameEngine/Source/Common/CommandLine.cpp`
- `GeneralsMD/Code/GameEngine/Source/GameLogic/System/GameLogic.cpp`
- `Core/GameEngine/Include/Common/ReplaySimulation.h` и `Source/Common/ReplaySimulation.cpp` — проброс
  `-gxCrcLog` в воркеры `-jobs` (родитель только порождает процессы и сам файл не создаёт)

Формат `bfme-crclog/1` (§3), плюс `# logic_hz=<LOGICFRAMES_PER_SECOND>` (30, если сборка не `SAGE_HIGH_FPS_SIM`).
Парсер: `tests/replay/crclog.py`.

Файл на диске — не аргумент опции, а `<stem>.<имя реплея>.<pid><ext>`
(`out/x.crclog` → `out/x.e2-battle.12345.crclog`), чтобы воркеры не писали в один путь. Фактический путь
печатается в stderr: `[BFME-QA] crc log ...`.

**Рекомендуется `-jobs 1`.** При `-jobs N>1` каждый воркер пишет свой файл, и раннер, который ждёт один
путь, не соберёт все кадры. Без `-jobs` несколько `-replay` идут в одном процессе и попадают в один файл
(он открывается на первом кадре).

Подвал `# end frames=<последний записанный кадр + 1> status=...` не читает `TheGameLogic->getFrame()`:
`atexit` срабатывает после уничтожения `GameLogic`, номер кадра помнит `s_lastFrame`. Кадры с нуля, CRC
пишется до инкремента `m_frame`, поэтому `frames` совпадает с тем, что `getFrame()` вернул бы на обычном
выходе. `status=exit`, если лог закрыл `atexit` (headless-цикл `ReplaySimulation` не вызывает
`GXReplayCheck::update`, поэтому `finish()` не бежит). `status=match` или `mismatch` — если `finish()`
закрыл лог раньше.

На первом headless-прогоне один и тот же реплей гоняется с `-gxCrcLog` и без него: итоговый CRC
(чекпоинты `MSG_LOGIC_CRC`, код выхода) и gamestats, если прогон их пишет, должны совпасть. Записанные
в `.rep` CRC стоят на кадрах 1, 101, 201, … — подозреваемый сдвиг +1 (§2), его проверяют там же.

## 9. Кейсы (план)

| Кейс | Стадия | Реплей | Статус |
|---|---|---|---|
| `selftest-match`, `selftest-crclog-mode` | selftest | фейковый | работают |
| `zh-upstream-sanity` | E1-43 | реплей ZH из `GeneralsReplays` (сабмодуль форка) | выключен: нужна headless-сборка + данные ZH на раннере |
| `e2-two-battalions-10min` | Э2 | записать (Геймплей), карта BFME1 | выключен: нет реплея |
| `e2-*-fixed-seed` по одному на фракцию | Э2 | записать | план |
| корпус из рассинхронов MP | Э5 | автоматически с сервера (NET-DESIGN §3.2 п. 5) | план |

## 10. Зависимости и открытые вопросы

- **Android-платформа:** headless-бинарник Linux (E1-43) в Docker-образе; self-hosted раннер на боксе с
  `$BFME_GAMEDATA`/`$BFME_REPLAYS` (регистрация раннера — решение Андрея через Оркестратора); включение YAML.
- **Лид:** хук `-gxCrcLog` в движке (§8), на ревью. На первом headless-прогоне сверить итоговый CRC и
  gamestats с прогоном без флага и подтвердить сдвиг +1 (§2). Открыто: принимает ли `-replay` абсолютный
  путь на Linux (в TSH имя относительно папки Replays — иначе раннер копирует файл туда); частота логики
  (30/5 Гц); флаги детерминизма (E1-45).
- **Геймплей:** реплей Э2 (два батальона, 10 минут) с фиксированным seed.
- **Андрей:** можно ли хранить `.rep` BFME1 в приватном git (только журнал команд) или только на боксе.
- QA: замерить стоимость `getCRC(CRC_RECALC)` на Pixel 8 Pro (просьба NET-DESIGN §3.2) — в perf-run
  TEST-PLAN §5.3 добавляется прогон с `-gxCrcEveryFrame` против без него.
