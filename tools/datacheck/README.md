# tools/datacheck

Инвентаризация данных BFME1 (патч 2.22) для движка Generals: BIG/VFS с приоритетом как в игре, разбор всех INI по схемам OpenSAGE, сверка с C++ движком Generals ZH, дифф «патч 2.22 против базы».
Python 3.10+, только стандартная библиотека.

## Запуск

Каталог с архивами игры задаётся только через `--game DIR` или переменную `BFME_GAMEDATA`. Пути по умолчанию нет.
На боксе это `/workspace/bfme-mobile/gamedata`.

```sh
cd tools/datacheck
export BFME_GAMEDATA=/path/to/gamedata         # каталог с ini.big, maps.big, _patch222*.big …
# только база (ini.big + maps.big), ~40 с:
./run.sh --archives ini.big,maps.big --out out
# всё, что лежит в gamedata/ (база + _patch222*.big, в т.ч. _patch222maps.big): ~2,5 мин (карты ~2 мин):
./run.sh --out out-all
python3 -m unittest discover -s tests -v        # 21 тест; сверка эталона с gamedata — только при BFME_GAMEDATA
```

Ключи: `--archives a.big,b.big` (монтировать только эти BIG; по умолчанию все `*.big`), `--no-loose` (игнорировать
loose-файлы рядом с архивами; `.exe/.dll/.md/.sha256/.part` и dot-каталоги не считаются данными никогда),
`--no-maps` (без разбора карт), `--gp-scratch DIR` (сверка с подсчётом геймплея, по умолчанию `$BFME_GP_SCRATCH`),
`--reference-out DIR` / `--reference-only` (эталон, см. ниже), `--gp-lua-blocks N` (число
BeginScript-блоков у Геймплея для сверки, по умолчанию 2092), `--gp-script-tsv FILE` (TSV имён скриптов, которым
пользовался Геймплей; по умолчанию `docs/formats/data/bfme1_script_names_maps_big.tsv`, только чтение), `--top N`.

Результат в `out/` (и `out-all/`): `report.md`, `report.json` (все списки целиком), `patch222_diff.md/json`,
`gameplay.md/json` (механики, скирмиш, сверка с gp-scratch), `maps.md/json` (BlendTileData по картам для E1-25),
`map_content.md/json` + `objects_1v1.tsv` + `script_names.tsv` (карты 1v1: объекты, ресурсы, скрипты; пересчёт имён
скриптов против ZH и базы), `bfme1_script_names_maps_big.tsv` (имена скриптов карт базы, тот же формат, что
`docs/formats/data/…tsv`; теперь строится здесь, в git не идёт), `beginscript.tsv` / `beginscript_blocks.tsv` /
`syntax.json` (Lua-блоки и расширения синтаксиса BFME против парсера ZH, раздел 8 report.md). Раздел 9 report.md —
как читается `Modifier = PRODUCTION …`. В `out*/` попадают значения и тексты EA. Эти каталоги в git не идут (`.gitignore`).

## Карты для QA: путевые точки, слоты, mapcache (`datacheck.qamaps`)

```
python3 -m datacheck.qamaps --out out-all --map "<путь .map в VFS>" [--point x,y ...] \
    [--slots-map "<путь .map>" ...] [--no-mapcache]
```
- `--map`: выгружает все путевые точки (`qa-waypoints.tsv`) и PolygonTriggers (`qa-polygontriggers.tsv`, с bbox
  и центроидом). `--point`: в какие триггеры попадает точка.
- `--slots-map`: раскладывает CastleTemplates из `bases\*\*.bse` на каждый флаг с CastleBehavior
  (по `CastleToUnpackForFaction`, для всех фракций): `world = flag + R(PlacementViewAngle)·(dx, dy)`.
  Пишет `qa-slots.tsv` (всё) и `qa-slots-build.tsv` (слоты/цитадели). Роль элемента берётся по KindOf (`slot_kind`).
- mapcache: для каждой карты сравнивает размер и CRC ZH (`zh_crc`, как MapUtil.cpp calcCRC) с записью
  `maps\MapCache.ini`, какую версию файла описывает запись, а также numPlayers и старты (`qa-mapcache.tsv`, ~100 с).
- Сводка: `qa-maps.json`. Отчёт с выводами: `out-all/qa-maps.md` (пишется вручную). Всё в `out-*/` (gitignore).

## Эталон для харнесса E1-56 (`reference/`)

`reference/2.22/` и `reference/base/` описывают, что наш разбор видит в зафиксированном наборе архивов. Это только
счётчики, имена (блоков, полей, модулей, тегов, шаблонов, макросов), номера строк, пути внутри архивов и sha256.
Значений INI, тел Lua, комментариев и текстов EA там нет. Наборы архивов зафиксированы в `datacheck/reference.py`
(`ARCHIVES`): это все архивы, где есть `*.ini`.
- `2.22`: `ini.big, maps.big, _patch222.big, _patch222maps.big`;
- `base`: `ini.big, maps.big`.
Какие именно архивы использованы (sha256, размер, число записей), записано в `manifest.json`.

Пересборка, одна команда на каталог (~30 с и ~10 с):

```sh
cd tools/datacheck
./run.sh --game "$BFME_GAMEDATA" --reference-out reference/2.22 --reference-only
./run.sh --game "$BFME_GAMEDATA" --reference-out reference/base --reference-only
BFME_GAMEDATA=… python3 -m unittest tests.test_datacheck.TestReference -v   # пересборка даёт те же байты + проверка «нет данных EA»
```

Без `--reference-only` эталон пишется вместе с обычными отчётами. Для каталога с другим именем нужен `--archives`.

| файл | что внутри |
|---|---|
| `manifest.json` | архивы в порядке монтирования (имя, байты, sha256, magic, записей), ревизии схем OpenSAGE/ZH, команда пересборки |
| `ini_files.tsv` | каждый INI: путь, архив, sha256, байты, строк, блоков верхнего уровня, предупреждений, CRLF/LF/CR, самая длинная строка |
| `counts.json` | счётчики анализатора и списки имён (как report.json, но без значений, сообщений и подписей): блоки по типам, неизвестные блоки/поля/модули с file:line, известные поля, модули до и после наследования, «нет в ZH» |
| `defines.json` | `#define`: строк, макросов, переопределений, подстановок в значения (всего и по имени макроса), неиспользуемых; что пропущено (неизвестные блоки, строки внутри них, неизвестные поля и модули); где определён каждый макрос |
| `objects.tsv` | каждое определение Object / ChildObject / ObjectReskin (data\ini и map.ini): file:line, родитель, своих полей и строк, полей после наследования, своих Add/Remove модулей, модулей после наследования (из них унаследовано) |
| `nested_openers.tsv` | какой ключ в каком контексте открывает вложенный блок и по какому правилу (`schema_block`, `schema_module`, `schema_block_elsewhere`, `no_equals`, `indent`, `raw_until_EndScript`; `opens=0` — ключ-блок по схеме, прочитанный как значение по числу токенов), есть ли такое имя поля в ZH, пример |
| `beginscript_modules.tsv` | модули с BeginScript: поле, тип, тег, обёртка, шаблон, file:line модуля, первая строка BeginScript, блоков |
| `beginscript_blocks.tsv` | каждый блок BeginScript, в том числе в состояниях, которые ZH пропустит: file:line, шаблон, модуль, тег, обёртка модуля, состояние (`AnimationState` / `IdleAnimationState` / `TransitionState`), `body_sha256`, `body_bytes` и признаки (см. ниже) |
| `syntax.json` | расширения синтаксиса BFME: вид, счётчик, file:line первого примера, что сделает ZH (`падает` / `молча ошибается` / …); сводка Lua-блоков: по типам модулей, обёрткам, состояниям, родителям, переводы строк, длины строк, блоки с `end` |

**Граница тела BeginScript** (`body_sha256`, `body_bytes`). Тело — это байты файла в том виде, в каком они лежат в
архиве, без нормализации (CRLF, табы, байты ≥0x80 сохраняются).
- **Начало:** первый байт после токена `BeginScript`. Если на строке BeginScript после токена есть пробелы или комментарий, они входят в тело, как и CRLF этой строки.
- **Конец:** первый байт токена `EndScript`, сам токен в тело не входит. Отступ перед `EndScript` на его строке входит в тело.
- **Строка `EndScript`:** первая строка после BeginScript, у которой первый идентификатор `EndScript` (без учёта регистра).
- **EOF:** если `EndScript` нет, тело идёт до конца файла (`closed=0`; в данных таких блоков 0).

Дополнительно `lines_sha256` и `lines_bytes` считаются только по целым строкам между строкой BeginScript и строкой
EndScript (с их CRLF). Это то же, что `syntax.lua_blocks()['body']`.

Ожидаемые значения в 2.22:
- 5948 блоков;
- состояния: AnimationState 5436, IdleAnimationState 484, TransitionState 28;
- модули: ReplaceModule 3785, напрямую 2092, AddModule 71;
- `zh_end_lines>0` у 1017 блоков (1674 строки).

`zh_end_lines` — это строки тела, у которых первый токен в разборе ZH ровно `end` без учёта регистра. Разбор ZH: строка
обрезается по `;`, затем `strtok` по `" \t\r\n="`. Наивный парсер (ZH, `INI.cpp:1642`) принял бы такую строку за End.
Эти 1017 блоков совпадают с блоками, где первый идентификатор строки — `end` (`lua_end_lines`). В base: 1783 блока,
состояния 1619/156/8, `end` у 169.

Поля эталона для харнесса E1-56:
- `beginscript_blocks.tsv` (`at`, `template`, `module_type`, `module_tag`, `state`, `body_sha256`, `body_bytes`);
- `nested_openers.tsv`;
- `objects.tsv` (`final_fields`, `final_modules`);
- `defines.json`;
- `syntax.json`;
- `counts.json` (`ini.*`);
- `ini_files.tsv` (sha256 входа).

Тест `TestReference.test_reference_has_no_ea_text` проверяет эталон и без gamedata:
- длина строк не больше 512;
- каждая ячейка TSV и строка JSON состоит только из букв, цифр, пробела и `_ . : \ - [ ] ' ( ) + / , ! & $`;
- нет `=`, `;`, кавычек;
- нет отдельного слова `End` и Lua-токенов (`CurDrawable`, `then`, `local`, `~=`).

### Тест под ru_RU
Тесты `TestValues.test_locale_*` и `TestRuLocaleFullParse` гоняют нормализацию и весь разбор INI под `LC_ALL=ru_RU.UTF-8`
(десятичная запятая) и сравнивают с `C`. Если системной локали нет, тест сам собирает её без root:
`localedef -i ru_RU -f UTF-8 tools/datacheck/.locale/ru_RU.UTF-8` и выставляет `LOCPATH` (нужны исходники локалей,
Debian-пакет `locales`; на боксе установлен). Весь прогон под ru_RU:
`LC_ALL=ru_RU.UTF-8 LOCPATH=$PWD/.locale ./run.sh ... --out /tmp/out-ru` — `report.json`, `gameplay.json`, `maps.json`
совпадают с прогоном под C побайтно (проверено 2026-10-02).
Файлы в `gamedata/` только читаются. В `out/` попадают имена и значения из INI EA — не публиковать, в git не класть (`out/` в `.gitignore`).

## Схемы (генерируются скриптами, вручную не править)

```sh
python3 schema/gen_opensage_schema.py /workspace/bfme-mobile/src/OpenSAGE schema/opensage_schema.json
python3 schema/gen_zh_schema.py /workspace/bfme-mobile/src/GeneralsGameCode schema/generals_zh_schema.json
```

- `opensage_schema.json`: `top_level` (блоки IniParser.BlockParsers), `tables` (поля каждой IniParseTable с видом value/block/module, дочерней таблицей и `game` из `[AddedIn]`), `module_tables` (Behavior/Draw/Body/ClientUpdate/ClientBehavior + модули частиц: тип модуля → таблица, game, ModuleKinds).
- `gen_block_openers.py`: таблица «ключ открывает блок» из `opensage_schema.json`, только имена схемы (для скиппера E1-56). Таблица того, что анализатор реально встретил в данных, лежит в `reference/*/nested_openers.tsv`.
- `generals_zh_schema.json`: блоки `theTypeTable`, модули `addModule`, имена полей всех `FieldParse` в Core + GeneralsMD.

## Устройство

| модуль | что делает |
|---|---|
| `datacheck/big.py` | BIGF/BIG4: magic, размер архива LE, число записей и смещение BE, записи (offset BE, size BE, имя\0) |
| `datacheck/vfs.py` | монтирование: loose > архивы; архивы по `_stricmp`-порядку пути, первый выигрывает; перекрытые версии сохраняются |
| `datacheck/ini.py` | структурный разбор INI по схеме, учёт неизвестного, BeginScript, #define/#include, сбор операций с модулями объектов |
| `datacheck/objects.py` | итоговые модули объекта после ChildObject/ObjectReskin/DefaultThingTemplate, Add/Remove/Replace/InheritableModule |
| `datacheck/values.py` | нормализация значений без локали (Decimal) |
| `datacheck/patchdiff.py` | дифф «патч против базы»: для `data\\ini` глобальный по блокам (база = старшая непатчевая версия каждого файла со своими #define, итог = выигравшие версии; перенос блока в другой файл — не удаление), плюс дифф #define; для `map.ini` — пофайлово; файл:строка с обеих сторон |
| `datacheck/report.py` | report.md/json |
| `datacheck/gameplay.py` | механики с файл:строка, держатели кнопки SELL, сверка с gp-scratch → gameplay.md/json |
| `datacheck/recruit.py` | граф «что фракция получает в скирмише» |
| `datacheck/maps.py` | счётчики BlendTileData по каждой `.map` (висячие индексы, флаги вне `&0x3`, customBlendEdgeClass при 0 классов), классы текстур; чтение карт (CkMp, RefPack) из `tools/formatprobe/probe.py` |
| `datacheck/chunks.py` | RefPack, TOC `CkMp`, заголовки чанков, эвристический обход подчанков. Это копия нужной части `tools/formatprobe/probe.py`, которого нет в git |
| `datacheck/reference.py` | эталон для E1-56 (`--reference-out`), см. раздел выше |
| `datacheck/qamaps.py` | карты для QA (`python3 -m datacheck.qamaps`), см. раздел выше |
| `datacheck/mapscan.py` | ObjectsList/Object + Dict, стартовые позиции `Player_N_Start` (как ZH MapUtil.cpp:325–341), имена скриптов (обход probe.walk внутри PlayerScriptsList), разбор `maps\mapcache.ini`; список 1v1, частоты, ресурсы, сравнение имён скриптов с ZH и базой |
| `datacheck/syntax.py` | сырые блоки BeginScript…EndScript (байты как в архиве), их контекст (модуль, тег, обёртка, объект), счётчики расширений синтаксиса BFME и что с ними делает парсер ZH; наборы «база» и «2.22» |
| `datacheck/modifiers.py` | строки `Modifier = TYPE value` всех версий attributemodifier.ini с подстановкой #define. Значение считается двумя способами: по гипотезе из данных (`%` слит с числом → /100, отдельный `%` → как есть; не подтверждено, ждёт замера в игре) и по OpenSAGE (всегда /100) |

Ограничения: см. `LIMITATIONS.md` (дублируются в конце report.md).

## Данные для геймплея (`out/gameplay.md`, `out/gameplay.json`)

- **Механики, файл:строка** (`datacheck/gameplay.py`, список `FEATURES`): продажа, стойки, пополнение орд, доход ферм. Поиск по итоговым (выигравшим в VFS) INI с владельцем (блок верхнего уровня) и модулем. Для 2.22 запускать без `--archives` (см. `out-all/`). Шаблоны легко дополнить.
- **Скирмиш** (`datacheck/recruit.py`): PlayerTemplate → герои/CastleToUnpackForFaction → CommandSet (с наследованием и CommandSetUpgrade) → CommandButton (UNIT_BUILD, FOUNDATION_CONSTRUCT …) → Object → HordeContain (RankInfo/InitialPayload), рекурсивно. По каждой фракции: достижимые объекты по классам и частоты модулей по итоговым объектам (`module_frequency(final, names=...)`).
- **Сверка с gp-scratch** (`--gp-scratch DIR` или `BFME_GP_SCRATCH`): расхождения с их подсчётом по регулярке и объяснение по каждой строке.

## E1-26, срез M1 (terrain.ini / water.ini / roads.ini)

`datacheck.terrainmap` разбирает три INI своей схемой и сопоставляет классы текстур
BlendTileData карты с файлами в `terrain.big` и `textures.big`. Это срез E1-26 под
критерий M1 (Fords of Isen), не весь E1-23 и не атлас (E1-51). OpenSAGE rev 588ac477
(GPL-3.0) — только оракул, код оракула не копируется.

```sh
cd tools/datacheck
BFME_GAMEDATA=/path/to/gamedata python3 -m datacheck.terrainmap --report /tmp/e1-26.md
python3 -m unittest tests.test_terrainmap -v
```

Отчёт, который можно коммитить (счётчики, имена текстур, пути внутри архивов):
`docs/bfme/E1-26-M1.md`. Тест на `BFME_GAMEDATA` сверяет его с повторным прогоном.
