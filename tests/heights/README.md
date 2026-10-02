# Тесты E1-25: 16-битные высоты террейна

Отдельный CMake-проект без vcpkg. Список исходников движка, базовая ревизия и опция `BFME_TESTS_ASAN` вынесены в `cmake/engine.cmake`, его же подключает `tests/blend`. Компилирует **настоящие** исходники движка:
- `WorldHeightMap.cpp`, `DataChunk.cpp`, `TileData.cpp`, `Dict.cpp`;
- `AsciiString`/`UnicodeString`, `GameMemoryNull`;
- RefPack `refdecode.cpp`.

Флаги и дефайны те же, что у `linux64`/`android`: `RTS_ZEROHOUR`, `-ffp-contract=off`, `-fno-strict-aliasing`, `-fwrapv`. Недостижимое линкер выбрасывает (`--gc-sections`), а оставшиеся глобалы заглушены в `src/EngineStubs.cpp`. Данные EA в репозиторий не попадают: синтетические карты генерируются в коде (`src/SyntheticMaps.h`).

```sh
cmake -S tests/heights -B build/tests-heights -G Ninja \
      -DBFME_DXVK_INCLUDE_DIR=references/fbraz3-dxvk/include [-DBFME_TESTS_ASAN=ON]
cmake --build build/tests-heights -j2
ctest --test-dir build/tests-heights -L unit
BFME_GAMEDATA=/path/to/bfme1 BFME_ORACLE_DIR=tools/oracle/out BFME_ORACLE_DUMP_DIR=cache/oracle-dumps/e1-25 \
  ctest --test-dir build/tests-heights -L gamedata
```

Нужны gcc/clang с C++20, CMake ≥ 3.25, git (для базовой ревизии) и заголовки DXVK (`git submodule update --init references/fbraz3-dxvk`).

## Что проверяется

| Тест | Метка | Что |
|---|---|---|
| `constants_u16_scale` | unit | `sizeof(HeightSampleType)==2`, `K_MAX_HEIGHT`, `MAP_HEIGHT_SCALE == 10/256 == 0.0390625`, `SCALE*16 == SCALE_ZH` |
| `zh_v1_v4_samples_widened_bit_identical_world` | unit | синтетические карты ZH v1–v4 (full и size-only): `raw == u8 << 4`, `raw*MAP_HEIGHT_SCALE` побитово равно `u8*10/16`; прореживание v1 как в апстриме |
| `zh_all_256_values` | unit | все 256 значений u8 |
| `bfme_v5_u16_samples_exact` | unit | v5: u16 LE как есть, включая 0, 1, 15, 16, 4081, 0x7FFF, 0xFFFF; мировая высота `raw*0.0390625` |
| `corrupt_height_chunk_rejected` | unit | `dataSize != w*h` и файл без `CkMp` отвергаются |
| `cliff_flags_threshold` | unit | `setCellCliffFlagFromHeights`: ZH 15/16 единиц (9.375/10.0) как в апстриме; BFME 250/251 raw (9.766/9.805) |
| `zh_repo_maps_widened` | unit | 43 карты ZH, уже лежащие в репозитории (`GeneralsOnlineRelease/…/Maps`, `BFME_ZH_MAPS_DIR`): v≤4, все raw кратны 16 и ≤ 4080 |
| `formula_*` | unit | см. ниже |
| `zh_baseline_bit_identical` | unit | сверка «до/после», см. ниже |
| `gamedata_bfme1_maps_minmax_vs_oracle` | gamedata | все 115 карт `maps.big`: версия 5, размер, бордер, min/max raw и мировые min/max равны `maps.big.maps.json` оракула OpenSAGE (full и size-only) |
| `gamedata_bfme1_maps_all_samples_vs_oracle_dump` | gamedata | все сэмплы всех 115 карт равны `Elevations` OpenSAGE (`<map>.heights.u16le.bin`) |

### До и после (`heights.zh_baseline_bit_identical`)

`src/ZhDump.cpp` собирается дважды:
- против апстрима `BFME_HEIGHTS_BASELINE_REV` (по умолчанию 8ef2349, последний коммит движка GeneralsX; движок в `katotekiru/bfme` ebd6c90 тот же). Его извлекает `git archive` при configure, либо можно передать готовый checkout в `BFME_HEIGHTS_BASELINE_SOURCE_DIR`;
- против этого дерева.

Обе сборки грузят одни и те же карты: 25 синтетических ZH v1–v4 и 43 карты из репозитория. Каждая выгружает всё, что движок выводит из высот:
- мировую высоту каждого сэмпла (биты float, full и size-only парсинг);
- скальные флаги pathfinding;
- `getHeight` за краем карты.

Сырые значения не выгружаются: они различаются по замыслу. `cmake/compare_dumps.cmake` требует побайтного совпадения.

### Формулы (`formula_*`)

`BaseHeightMap`, `HeightMap`, `TerrainLogic` и `W3DTerrainVisual` тянут за собой рендер-объекты WW3D и GameLogic, и в автономный проект их не слинковать. Для изменённой арифметики из этих файлов тест держит **копии выражений** «апстрим (u8, 10/16)» и «E1-25 (u16, 10/256)» и сравнивает их побитово перебором диапазона ZH:
- `getHeightMapHeight`: интерполяция и сглаженная нормаль, 400 000 клеток;
- пороги `getUVForTileIndex`: полный перебор;
- `flattenTerrain`: 2 млн случайных высот и все границы шагов;
- `formula_flatten_rounding_boundaries` (по просьбе QA в #4): границы x.5 у `floor(0.5f + avg/0.625)`. Для k = −100…300 берутся ровно (k+0.5)·0.625 и k·0.625 (оба точны во float) и по 4 соседних float в каждую сторону (`nextafterf`). При k ≥ 0 значение апстрима проверяется явно: k+1 и k. Ниже нуля `REAL_TO_INT_FLOOR` (`fast_float_floor`) переводит целое −n в −n−1 при n ≥ 2. Это особенность апстрима, одинаковая до и после, а высоты не бывают отрицательными;
- `formula_crater_integer_displacements` (QA): `displacementAmount` = 0…300 целые и по 2 соседних float в каждую сторону, для всех 256 высот ZH. На целых проверяется явно `MAX(1, h − d)`;
- `createCraterInTerrain`, сейсмика, раскладка сейва v3;
- боксы `Cast_Ray`, `updateCenter`.

Если одна из этих функций поменяется, копию нужно обновить.

## Данные оракула

`BFME_ORACLE_DIR` указывает на `tools/oracle/out`, там лежит `maps.big.maps.json`. Полные массивы высот выгружаются так (только вне git, это производные данных EA):

```sh
D=cache/oracle-dumps/e1-25; mkdir -p $D/json
python3 -c "import json;[print(m['source']['entry']) for m in json.load(open('tools/oracle/out/maps.big.maps.json'))['maps']]" |
while IFS= read -r e; do b=$(basename "${e//\\//}" .map)
  tools/oracle/oracle.sh map "$G/maps.big" "$e" -o "$D/json/$b.json" --dump $D >/dev/null
  find $D -maxdepth 1 -name '*.bin' ! -name '*.heights.u16le.bin' -delete
done
```

Без `BFME_GAMEDATA`, `BFME_ORACLE_DIR` или `BFME_ORACLE_DUMP_DIR` тесты `gamedata` пропускаются (код 77).

## Результат (2026-10-02, gcc 13.3, образ `bfme-android-build`)

19/19 passed, в обычной сборке и под ASan (`-DBFME_TESTS_ASAN=ON`, без ошибок):
- ZH «до/после»: 68 карт, 4 743 768 сэмплов, 377 895 скальных клеток, 42 464 345 байт идентичны.
- BFME1: 115/115 карт, 25 374 931 сэмпл (raw 0…38 400, мир 0…1500) совпадают с OpenSAGE.
- Мутационная проверка: расширение `(u8<<4)|(u8>>4)` вместо `u8<<4` роняет 4 теста, в том числе сверку «до/после».

Харнесс (`harness/`) скопирован из `tests/vfs` (E1-21), чтобы E1-25 от него не зависел.
