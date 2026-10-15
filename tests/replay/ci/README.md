# tests/replay/ci — CI реплей-тестов (предложение QA для Android-платформы)

`replay-tests.yml` переносит в `.github/workflows/` **Платформа** (владелец `.github/` и `ci/`, ADR-0001). QA в
GitHub ничего не пушит. Версия 2 от 02.10.2026: все шаги с кодом из checkout идут через `$BFME_DOCKER replay …`.
**v3 (02.10.2026, вечер):** контракт режима `replay` по ответу Платформы (без `-e`, `bfme-check-artifacts` из
`/usr/local/sbin`, `/out` на джобу), подкоманда `selftest`, `--timeout-scale` для arm64, правки раннера 1–4 сделаны.
Сверено с `katotekiru/bfme` @ `c70bfa151` (в ней PR #5 `4ddeb24`: `bfme-android-apk.yml`, `tools/ci/bfme-ci-docker`).
**v4 (02.10.2026, 23:00 МСК), по просьбе Платформы:** `--deadline-min` (общий лимит прогона), таймаут кейса arm64
600 с (`[defaults].timeout_s_arch`, `--case-timeout`), `timeout-minutes` arm64 360, один список артефакта на джобу
(`REPLAY_ARTIFACTS`), множитель таймаута не больше 50 — см. «Таймауты» и «Артефакты».
**v5 (08.10.2026, ревью #11):** интерфейс `bfme-check-artifacts` от Платформы (`--` и список файлов, `count` в
`$GITHUB_OUTPUT`, `if-no-files-found: ignore`), путь движка через `$HEADLESS_BIN`, раннер убивает группу процессов
по таймауту и не создаёт ссылок под `--out` — см. «Артефакты».

## Что поменялось против v1

| Было | Стало |
|---|---|
| `python3` хоста в каждом шаге | `$BFME_DOCKER replay tools/replay-test …` (selftest тоже) |
| `BFME_HEADLESS_ENGINE` / `…_ARM64` из `.env` | шаг сборки из коммита PR + `--engine` с абсолютным путём в контейнере |
| `qemu-aarch64 $BIN` | `--engine "qemu-aarch64-static -L /usr/aarch64-linux-gnu /src/<bin>"` |
| выгрузка всего `out/` в selftest | явный список, как в replay-crc |
| — | `tools/ci/check-artifacts.sh` перед каждой выгрузкой; выгрузка только если проверка прошла (`steps.artifacts_ok.outcome`) |
| `actions/*@v4` | `@<SHA-from-Platform> # v4` (SHA пришлёт Платформа) |
| `${{ inputs.stage }}` прямо в `run:` | через `env: BFME_STAGE` шага (без подстановки выражения в скрипт; обёртка пропускает его в контейнер) |
| `-e NAME=VALUE` у `$BFME_DOCKER replay` | **v3:** ни одного `-e`; `BFME_ENGINE_BUILD_ID`, `BFME_STAGE` — в `env:` шага, остальное ставит обёртка |
| `tools/ci/check-artifacts.sh` из checkout | **v3:** `/usr/local/sbin/bfme-check-artifacts` (root, не из checkout) |
| `/out` = `ci/out/<run_id>`, подкаталог на джобу | **v3:** `/out` = `ci/out/<run_id>/<job>` (создаёт обёртка), `REPLAY_OUT_HOST` в `env` джобы |
| шаг юнит-тестов — TODO | **v3:** `$BFME_DOCKER replay tools/replay-test selftest` |
| `timeout_s` под qemu без множителя | **v3:** `--timeout-scale 20` в arm64 (раннер ставит 20 и сам, см. «Таймауты») |
| `working-directory: tests/replay` | корень checkout (обёртка пускает пути от корня) |
| `timeout-minutes: 240` у arm64, 900 с × 20 на кейс | **v4:** `timeout-minutes: 360`, кейс 600 с × 20, `--deadline-min 345`; x86_64 — 90 и `--deadline-min 75` |
| список файлов в `path:` и каталог в `bfme-check-artifacts` | **v4:** один список `REPLAY_ARTIFACTS` в `env` джобы для обоих шагов |
| пустой список — `exit 0` в YAML, выгрузка по `outcome` | **v5:** `bfme-check-artifacts -- "${files[@]}"` (пустой → 0 и `::warning` от проверки), `count` в `$GITHUB_OUTPUT`, выгрузка при `count != 0`, `if-no-files-found: ignore` |

Без изменений: `on`, `permissions`, `concurrency`, проверка форков, `fetch-depth: 1`, `timeout-minutes` 10 и 90
(selftest, x86_64), `retention-days` (7/14/14), `continue-on-error` у arm64.

## Контракт режима `replay` (ответ Платформы 02.10.2026)

```
$BFME_DOCKER replay <команда> [аргументы...]
```
- **Никаких `-e`, `-v`, `--network`.** Обёртка сама ставит `BFME_GAMEDATA=/gamedata`, `BFME_REPLAYS=/replays`,
  `HOME=/tmp`, `SDL_VIDEODRIVER=dummy`, `SDL_AUDIODRIVER=dummy`, `CCACHE_DIR=/ccache` и пропускает из `env:` шага
  **только** `BFME_ENGINE_BUILD_ID` (40 hex, у нас `${{ github.sha }}`) и `BFME_STAGE` (`[a-z0-9_-]+`, у нас
  `${{ inputs.stage || 'e1' }}`; другое значение обёртка отвергнет). `GITHUB_SHA` в контейнер не попадает: раннер
  пишет в `git_sha` эталона `BFME_ENGINE_BUILD_ID`. `PYTHONDONTWRITEBYTECODE` из `env` workflow тоже не доходит
  (безвредно).
- Образ Платформы, `--network none`, `--cap-drop=ALL`, `no-new-privileges`, uid ghrunner; `-w /src`.
- Монтирования: checkout → `/src` (на запись: туда пишет `build/`), `$BFME_GAMEDATA_RO` → `/gamedata:ro`,
  `$BFME_REPLAYS` → `/replays:ro` (каталог создаёт Платформа, пустой — норма), `ci/out/<run_id>/<job>` → `/out`
  (создаёт обёртка; в YAML это `REPLAY_OUT_HOST` джобы), для сборки — `ci/cache/ccache-headless` → `/ccache`.
  `/tmp` в контейнере на запись. Подкаталоги `/out/selftest`, `/out/x86_64`, `/out/arm64` создаёт раннер (`--out`).
  **Путь на хосте:** `ci/out/<GITHUB_RUN_ID>/<GITHUB_JOB>` (полностью
  `/workspace/bfme-mobile/ci/out/<GITHUB_RUN_ID>/<GITHUB_JOB>`), `<GITHUB_JOB>` = id джобы (`replay-selftest`,
  `replay-crc`, `replay-crc-arm64`) — подтверждено Платформой 02.10.2026.
- Белый список команд: `tools/replay-test`, `tests/replay/run_replay_tests.py`, `tools/ci/build-headless.sh`
  (`--preset`, `--target`, `--arch arm64`). Юнит-тесты — `tools/replay-test selftest`.
- `cwd=/gamedata` только на чтение **намеренно**: запись движка туда даёт `EROFS`. Раннер ищет в выводе движка
  `Read-only file system` / `EROFS` / `errno 30`, кладёт предупреждение в результат кейса (`results.json`,
  `summary.md`, `<system-err>` в JUnit) и дописывает строку `[replay-test] EROFS: …` в `<case>.engine.log`; если
  движок из-за этого упал — `engine_error` с пометкой `EROFS` в сообщении.
- На хосте остаются: «Окружение раннера» (проверка `127.0.0.1:2375` и `test ! -w` — обёртка их повторяет, в YAML
  оставлены), `/usr/local/sbin/bfme-check-artifacts` (root, **не из checkout**; `tools/ci/` в репозитории — только
  исходник для `setup-runner.sh`), `cat` сводки в `$GITHUB_STEP_SUMMARY`, `upload-artifact`.
- SHA действий (`@<SHA-from-Platform>`) оставлены заглушками — их ставит Платформа.

Замечание к проверке сокета: голая строка `! curl -s -m 2 127.0.0.1:2375/version` при `bash -e` шаг **не валит**
(команда с `!` не вызывает выход по `-e`). В YAML: `! curl … || { echo "::error::…"; exit 3; }`.

## Сборка headless

- **Имя бинарника и пресет — предварительные** (Платформа 02.10.2026): `linux64-deploy`,
  `/src/build/linux64-deploy/GeneralsMD/GeneralsXZH`; arm64 — заглушка `linux-arm64-headless`.
- Пресета «headless» в `bfme` нет (`CMakePresets.json` @ `c70bfa151`). Ближайший настоящий — `linux64-deploy`
  (Ninja, RelWithDebInfo, SDL3/OpenAL, DXVK). Цель `z_generals`, имя `GeneralsXZH`
  (`GeneralsMD/Code/Main/CMakeLists.txt`) → `build/linux64-deploy/GeneralsMD/GeneralsXZH` (так же в
  `build-linux.yml` форка). Headless — это ключ `-headless` плюс SDL dummy, как в `replay-tests.yml` форка.
  **TODO(Платформа, E1-43a/E1-10):** пресет `linux64-headless` с флагами детерминизма → поменять `HEADLESS_*` в `env`
  джобы `replay-crc`.
- **Путь движка — только через `$HEADLESS_BIN`** (ревью #11 п. 6): `HEADLESS_PRESET`/`HEADLESS_BIN` заданы один раз в
  `env` джобы `replay-crc` (arm64 — `HEADLESS_*_ARM64` в `env` джобы `replay-crc-arm64`); и `test -x` после сборки, и
  `--engine "/src/$HEADLESS_BIN"` берут путь оттуда, литерального `build/…` в шагах нет.
- `--engine` абсолютный (`/src/build/...`): раннер запускает движок с `cwd=$BFME_GAMEDATA`, и относительный путь
  `build/...` искался бы от `/gamedata`.
- arm64 (E1-49): джоба только по `workflow_dispatch`, пока E1-49 не добавит в образ sysroot и кросс-g++.
  `g++-aarch64-linux-gnu` + `libc6-dev-arm64-cross`, триплет vcpkg arm64-linux. Полностью static
  мешают SDL3/OpenAL/DXVK, минимум — `-static-libstdc++ -static-libgcc`. Остальные `.so` подхватываются по RPATH
  `/src/build/...` (путь при сборке и прогоне одинаковый), libc — из `-L /usr/aarch64-linux-gnu`.
  **TODO(Платформа):** имя пресета, сейчас в YAML заглушка `linux-arm64-headless`.
- Многословный `--engine` раннер уже принимает: `resolve_engine()` = `shlex.split(os.path.expandvars(...))`
  (проверено фейковым движком через `env … python3 fake_engine.py`).

## Данные для кейсов e1 (ZH не куплен, только BFME1 2.22)

Все файлы лежат плоско в корне `BFME_GAMEDATA_RO=/workspace/bfme-mobile/gamedata` (в контейнере `/gamedata`),
SHA-256 в `gamedata/MANIFEST.sha256`, статус в `gamedata/COPY_LOG.md` (на 22:22 МСК 11 из 351 файла).

| Файл | Нужен e1 | Что внутри (пути в архиве) |
|---|---|---|
| `ini.big` (BIG4, 13,5 МБ) | да | 242 × `data\ini\…` (база 1.03) |
| `_patch222.big` (BIGF, 82 МБ) | да | 484 × `data\ini\…` (перекрывает 241 из 242), `data\lotr.str`, `data\scripts\scripts.lua`, `scriptevents.xml`, 759 × `data\audio\*.wav` |
| `w3d.big` (487 МБ) | да, как только в сценарии есть юниты | модели: headless всё равно создаёт Drawable и грузит модели (`W3DAssetManager.cpp:853` пропускает только перекраску), кости оружия влияют на логику |
| `maps.big`, `_patch222maps.big` | после E1-36 (загрузка карты BFME1) | `maps\map mp fords of isen\map mp fords of isen.map` — берётся из патча (520 124 Б против 518 519 в 1.03) |
| `_patch222libraries.big` | вместе с картами | `libraries\<имя>\<имя>.map` — AI- и скрипт-библиотеки MP-карт |
| `_patch222bases.big` | сценарий орд на лагерных плотах | `bases\camps\camps.map` |
| `textures.big`, `_patch222textures.big`, `terrain.big`, `shaders.big`, `lang\*`, аудио | нет | headless не рисует (`BaseHeightMap.cpp:293`) |

- До E1-36 сценарий идёт на пустой карте (E1-43a). Если нужен файл карты — только свой, не EA (`fixtures/`, ≤ 1 МБ).
- Монтируем каталог целиком, только на чтение: движок ZH монтирует все `*.big` из cwd, лишние архивы стоят лишь
  чтения оглавления. Порядок: в ZH сортировка `strcasecmp` и «побеждает первый», `_patch222*` идёт раньше букв →
  патч перекрывает 1.03. Это тот же итог, что у правила BFME1 (E1-21, в `bfme` ещё не влито). Проверить на первом
  прогоне по `data\ini\gamedata.ini` (CP 1v1 230/460 из патча).
- Чего движку может не хватить: записи в cwd (`/gamedata` только на чтение, например `gx_replay_check_result.txt`)
  и пользовательского каталога (`HOME=/tmp`). Проверить на первом прогоне.

## BFME_REPLAYS

Каталог создаёт Платформа → `/replays:ro`; пустой — норма. Подкаталоги `synthetic/`, `e2/`, `zh/` (только если
купят ZH). Для e1 он не нужен: кейсы e1 синтетические, `.rep` сценарий пишет сам в `/out/<arch>/<case>/last.rep`.
**Для `bfme-check-artifacts`:** в `/out/<arch>/` кроме логов и CRC теперь лежат подкаталоги `<case>/` с `.rep`
сценария (только команды игроков, не файлы EA). **Файлы `.rep` не выгружаются:** в `REPLAY_ARTIFACTS` их нет,
проверка получает тот же список и `.rep` не видит. Реплеи до 1 МБ лежат в git в `tests/replay/fixtures/<case>.rep`
(решение Оркестратора) и приходят с checkout.

## Артефакты (`REPLAY_ARTIFACTS`, `bfme-check-artifacts`)

### Интерфейс проверки (зафиксирован Платформой)

```
/usr/local/sbin/bfme-check-artifacts [--max-mb N] [--allow-missing] [--] FILE...
```

- Принимает **только обычные файлы**. Каталог в списке → **код 2** (ошибка вызова).
- **Код 1** (выгружать нельзя), если хоть один файл:
  - больше 5 МБ (`--max-mb N` меняет предел);
  - начинается с сигнатуры архива BIG: `BIGF`, `BIG4` или `BIG ` (с пробелом);
  - имеет расширение `.big` `.w3d` `.map` `.dds` `.tga` `.csf` `.str` `.wav` `.mp3` `.bik` `.ini` `.rep`;
  - является символьной ссылкой или его `realpath` лежит вне `ci/out/<run_id>/` и вне `$GITHUB_WORKSPACE`.
- Пустой список → **код 0** и `::warning`.
- `--` отделяет ключи от списка файлов. `--max-mb` и `--allow-missing` в YAML не передаём: предел по умолчанию
  5 МБ, а `nullglob` и так отдаёт только существующие файлы.
- Скрипт — root, `/usr/local/sbin`, **не из checkout** (`tools/ci/` — только исходник для `setup-runner.sh`).

### Как это в YAML (каждая джоба)

Список файлов артефакта задан **один раз** в `env:` джобы (`REPLAY_ARTIFACTS: |`, по строке на глоб, полный путь
на хосте — контекст `env` в `jobs.<id>.env` недоступен). Его используют оба шага:

```bash
shopt -s nullglob globstar
IFS=$'\n'; files=( $REPLAY_ARTIFACTS ); unset IFS
/usr/local/sbin/bfme-check-artifacts -- "${files[@]}"
echo "count=${#files[@]}" >> "$GITHUB_OUTPUT"
```

- шаг «Проверка артефактов» (`id: artifacts_ok`, `shell: bash`, `if: always()`): раскрывает глобы в массив, отдаёт
  его проверке (пустой массив — 0 и `::warning` от самой проверки), пишет `count` в `$GITHUB_OUTPUT`;
- `upload-artifact`: `if: always() && steps.artifacts_ok.outcome == 'success' && steps.artifacts_ok.outputs.count != '0'`,
  тот же `path: ${{ env.REPLAY_ARTIFACTS }}` и `if-no-files-found: ignore`. При `count == 0` выгрузка пропускается.

В списке: `<arch>/*.crclog`, `*.engine.log`, `junit.xml`, `summary.md`, `results.json`. **Ни один глоб не может
совпасть с запрещённым расширением**; `.rep` (в т. ч. `<case>/last.rep`) и подкаталоги `<case>/` не выгружаются.

### Что гарантирует раннер

- **Под `--out` раннер не создаёт символьных ссылок** и не пишет сквозь них: если по пути выходного файла
  (`<case>.engine.log`, `<case>.actual.crclog`, `results.json`, `junit.xml`, `summary.md`) лежит ссылка, она удаляется
  и пишется обычный файл (`plain_file()` в `run_replay_tests.py`).
- Тесты (`test_replay_runner.ArtifactTests`, входят в `tools/replay-test selftest`): после прогона фейкового движка
  под `--out` нет ни одной ссылки; подложенная ссылка заменяется, файл-цель не тронут; глобы `REPLAY_ARTIFACTS`
  каждой джобы после фейкового прогона дают только разрешённые расширения, меньше 5 МБ, без сигнатуры BIG и без
  `.rep`; ни один глоб не совпадает с именем с запрещённым расширением; шаги YAML совпадают с описанием выше.

## Время e1 при -jobs 1 (оценка)

- **Сейчас:** все 5 кейсов e1 выключены (`enabled = false`) → раннер ≈ 1 с плюс старт контейнера; код 0
  (проверено: `tools/replay-test --engine fake --arch linux-x86_64 --out /tmp/rt-e1 run --stage e1`).
- **Сборка headless:** около 5–10 мин без ccache, 1–3 мин с прогретым. Основание: полная сборка APK того же движка
  в образе Платформы на боксе (jobs=4) заняла 6 мин (`e1-03-report/ci-sim-build-apk-20261002-215423.log`,
  21:54–22:00 МСК).
- **Когда включим `synthetic-seed-determinism`:** 4 прогона (2 × один сид, другой сид, повтор `last.rep`) по
  ≤ 9000 кадров. Это около 3–8 мин. Основание: форк даёт headless-реплею ZH целого матча `timeout 180` на 2 vCPU,
  то есть не меньше ~5× реального времени, плюс загрузка INI (13,5 + 82 МБ). `e1-43a-heights-dump` добавит
  1–3 мин. Итого джоба `replay-crc` на e1 ≈ 5–20 мин при лимите 90.
- **arm64 под qemu:** в 10–30 раз медленнее, см. «Таймауты».
- Лимиты: x86_64 — `--deadline-min 75` при `timeout-minutes: 90`; arm64 — `--deadline-min 345` при `360`.

## Таймауты (`--case-timeout`, `--timeout-scale`, `--deadline-min`)

**Таймаут кейса** (до множителя), по старшинству:

1. `--case-timeout S` (общий ключ, до подкоманды) — для всех кейсов;
2. `timeout_s` самого кейса в `manifest.toml`;
3. `[defaults].timeout_s_arch` для `--arch`: сейчас `linux-arm64 = 600`, `android-arm64 = 600`;
4. `[defaults].timeout_s` = 900.

**Множитель**, по старшинству:

1. `--timeout-scale X` (общий ключ: `tools/replay-test --timeout-scale 20 … run …`);
2. `BFME_REPLAY_TIMEOUT_SCALE` — обёртка будет пропускать её в контейнер (Платформа 02.10.2026, формат
   `[0-9]+(\.[0-9]+)?`, не больше 50); раннер проверяет тот же формат;
3. **20**, если `--arch` содержит `arm64`, а движок (`--engine` или `$BFME_HEADLESS_ENGINE`) начинается с `qemu-`;
4. иначе 1.

`0 < X ≤ 50`, иначе код 3 (и для ключа, и для переменной). Множитель ≠ 1 печатается в лог и в `summary.md`.
arm64: 600 с × 20 = 12 000 с = 3,3 ч на кейс.

**Общий лимит прогона `--deadline-min M`** (у `run` и `bless`): время на весь прогон по часам. Каждый запуск
движка получает `min(таймаут кейса × множитель, остаток до дедлайна)`; остаток проверяется и перед каждым кейсом.

- Кейс, который убил дедлайн (а не собственный таймаут): статус `timeout`, `note = "deadline"`, в JUnit
  `<error type="timeout">` с сообщением про дедлайн.
- Кейсы, до которых не дошли: статус `deadline` (в JUnit `<skipped message="deadline"/>`, в `skipped`).
- `results.json`, `junit.xml`, `summary.md` пишутся всегда; в сводке строка «⌛ Достигнут лимит прогона».
- Код выхода: дедлайн сам по себе — **4** (прогон неполный); с другими проблемами старшинство прежнее 2 > 1 > 3 > 4
  (таймаут из-за дедлайна считается 4, а не 2).
- В YAML: x86_64 — `--deadline-min 75` (`timeout-minutes: 90`), arm64 — `--deadline-min 345`
  (`timeout-minutes: 360`). Запас 15 мин — на сборку headless, сводку, проверку и выгрузку; поэтому зависший
  кейс останавливает раннер, а не GitHub, и отчёты остаются.

## Правки раннера (QA) — сделаны 02.10.2026

1. `run`/`bless` создают `--out` (с родителями) и родителей `--junit`/`--summary`; `run --stage e1` при всех
   выключенных кейсах даёт код 0.
2. Подкоманда `selftest`: stdlib `unittest` всех `tests/<каталог>/test_*.py` (сейчас `replay`, `perf`,
   `terrain`), код 0/1, `-q`, `--only replay`.
3. Кейсы с `-qaScenario` без готового `.rep`: сценарий пишет свой `.rep` в `-qaOut` (по умолчанию
   `{case_out}` = `/out/<arch>/<case>/`, имя `last.rep`) и CRC-лог; сравнение с эталоном или, при
   `check = "rep-recorded"`, с CRC внутри этого `.rep`. Нет эталона → `no_golden` (код 4) или запись эталона по
   `run --record-missing` (два одинаковых прогона, как `bless`). Нет входного файла у одного кейса →
   `missing_input`, сломанная настройка → `config_error` (оба — `<error>` в JUnit); прогон идёт дальше, итоговый
   код 3, если нет более тяжёлых (2, 1).
4. `--timeout-scale` / `BFME_REPLAY_TIMEOUT_SCALE` / авто 20 для arm64 под qemu (раздел выше).
5. Многословный `--engine` правок не требовал.
6. **v4:** `--deadline-min`, `--case-timeout`, `[defaults].timeout_s_arch`, предел множителя 50 (раздел
   «Таймауты»); фейковый движок умеет `FAKE_SLEEP=S` (тесты дедлайна).
7. **v5 (ревью #11):** движок в своей группе процессов, по таймауту/дедлайну `killpg` SIGTERM, через 5 с SIGKILL
   (`FAKE_CHILD_PIDFILE` в фейковом движке для тестов); под `--out` нет символьных ссылок (раздел «Артефакты»).
