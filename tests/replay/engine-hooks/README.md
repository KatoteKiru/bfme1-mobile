# Хуки движка для реплей-тестов (QA)

> **Статус 02.10.2026, 23:00 МСК:** хук `-gxCrcLog` **влит в `bfme`** — PR #7
> (https://github.com/KatoteKiru/bfme-mobile/pull/7, squash `a6048b9`); описание — `docs/REPLAY-TESTS.md` §8,
> реестр — `docs/UPSTREAM-PATCHES.md`. Источник правды — код в `bfme`. Снимок `0001-bfme-qa-crc-frame-log.patch`
> и `stub_check.sh` остались только на боксе (`/workspace/bfme-mobile/tests/replay/engine-hooks/`), в git не
> переносятся и не правятся. QA сам исходники движка не меняет (ADR-0001). Ниже — история снимка.

| Патч | Против чего | Что делает |
|---|---|---|
| `0001-bfme-qa-crc-frame-log.patch` (снимок 17:58) | `src/GeneralsZH-Android-Port` (MYSOREZ) @ `8ef2349` | опция `-gxCrcLog <path>`: CRC каждого кадра логики + бегущие CRC секций в файл `bfme-crclog/1` |

## Что в снимке уже учтено (замечания Лида 02.10.2026)

- **(a)** `closeCrcLog()` из `atexit` больше не читает `TheGameLogic->getFrame()` (к этому моменту
  GameLogic уже уничтожен): `writeCrcLog()` запоминает `s_crcLastFrame`, подвал пишет
  `# end frames=<последний кадр + 1> status=...`.
- **(b)** `-jobs N > 1`: рабочие процессы получают одни и те же аргументы. Файл открывается лениво на первом
  кадре (у родителя, который только раздаёт реплеи, файла нет) под именем
  `<путь без расширения>.<имя реплея>.<pid><расширение>`, например
  `out/x.actual.crclog` → `out/x.actual.e2-battle.12345.crclog`. **Для реплей-тестов запускаем `-jobs 1`**
  (один реплей на процесс); раннер находит файл по маске и считает ошибкой конфигурации, если файлов больше одного.
- Шапка содержит `# logic_hz=<LOGICFRAMES_PER_SECOND>` (30; реплей частоту тика не хранит).
- Пометки `// BFME: QA` на всех изменениях.

Проверено на снимке: `git apply --check` на 8ef2349 проходит; `stub_check.sh` (вырезает блок
`// BFME: QA … begin/end`, собирает с заглушками `g++ -Wall -Wextra -Werror`, запускает два «рабочих» с одним
путём, разбирает вывод `crclog.parse_crclog`) — проходит: два отдельных файла, подвал из `atexit` без
`TheGameLogic`, `logic_hz=30`. В составе движка не собиралось — это сделает CI в PR.

## Зачем

`-gxCrcEveryFrame` форка печатает CRC только при `GX_NET_TRACE`, вперемешку с сетевым трейсом в stderr.
Раннер это понимает (режим `gx-stderr`), но это хрупко и шумно. `-gxCrcLog` пишет ровно то, что нужно
тестам, в отдельный файл, с буферизацией по строкам (после падения остаются все завершённые кадры).

## Влияние на симуляцию (ответ Лида 02.10.2026)

- `getCRC()` идёт через Xfer в режиме CRC и состояние симуляции не меняет.
- **Оговорка:** в трейсе форка счётчик «random consumers since last checkpoint» при CRC на каждом кадре
  становится покадровым (чекпоинт = каждый кадр). Сравнивать его с прогонами без флага нельзя.
- **Первая проверка на headless-сборке:** один и тот же реплей с `-gxCrcLog` и без него → совпадают
  итоговый CRC и gamestats (`scripts/tooling/replay/compare_gamestats.py` форка).
- Флаг выключен в сборках для игроков и **не включается в perf-run** (TEST-PLAN §5.3); стоимость CRC
  меряется отдельным сценарием `crc-cost`.

## Запуск

```
<engine> -headless -jobs 1 -replay /abs/path/file.rep -gxCrcLog /tmp/out.crclog -gxAutoQuit
python3 tests/replay/run_replay_tests.py compare golden/linux-x86_64/<case>.crclog /tmp/out.<replay>.<pid>.crclog
```
В `manifest.toml`: `mode = "crclog"`, `args = [..., "-gxCrcLog", "{crclog}"]` — раннер сам найдёт файл рабочего.

## Повторная проверка снимка

Не нужна: PR #7 влит, `stub_check.sh` лежит только на боксе (`sh /workspace/bfme-mobile/tests/replay/engine-hooks/stub_check.sh`).
