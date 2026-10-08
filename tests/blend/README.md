# Тесты E1-25: BlendTileData v14/v15

Отдельный CMake-проект по образцу `tests/heights`. Он берёт оттуда харнесс, загрузчик карт и список исходников движка (`../heights/cmake/engine.cmake`). Тесты гоняют **настоящий** `WorldHeightMap::ParseHeightMapData` + `ParseBlendTileData`, а затем потребителей таблиц: `getAlphaUVData`, `getExtraAlphaUVData`, `getUVData`, `getTextureClass` и патч-цикл конструктора (копия). Подкласс `TestBlendMap` (`src/BlendLoader.*`) только открывает доступ к protected-полям.

Движок `bfme_blend_tests` собран с `RELEASE_DEBUG_LOGGING`, поэтому `DEBUG_LOG` попадает в заглушку `DebugLog` (`src/BlendStubs.cpp`). Тесты проверяют построчный лог аномалий (первые 16 случаев каждого вида на карту, решение Лида по #10) и итоговую строку по карте. Счётчики всегда доступны через `getBlendTileDataStats()`.

Второй бинарь, `bfme_blend_ctor_tests` (ревью #10), гоняет **настоящий** загрузочный конструктор `WorldHeightMap(ChunkInputStream*, Bool)` (`src/CtorLoader.*`, заглушки `src/CtorStubs.cpp`). Его движок собран как релиз:
- без `RELEASE_DEBUG_LOGGING`: `DEBUG_LOG` вырезан, сводка по карте уходит одной строкой в stderr;
- с `DISABLE_GAMEMEMORY_NEW_OPERATORS`: `operator new`/`delete` даёт `src/CountingNew.cpp`. Он ведёт себя как `GameMemoryNull` (malloc, обнуление, `ERROR_OUT_OF_MEMORY`), считает живые блоки (проверка утечек и без ASan) и умеет уронить N-е выделение.

```sh
cmake -S tests/blend -B build/tests-blend -G Ninja \
      -DBFME_DXVK_INCLUDE_DIR=references/fbraz3-dxvk/include [-DBFME_TESTS_ASAN=ON]
cmake --build build/tests-blend -j2
ctest --test-dir build/tests-blend -L unit
BFME_GAMEDATA=/path/to/bfme1 BFME_ORACLE_DIR=tools/oracle/out BFME_ORACLE_DUMP_DIR=cache/oracle-dumps/e1-25 \
BFME_DATACHECK_MAPS_JSON=tools/datacheck/out/maps.json \
  ctest --test-dir build/tests-blend -L gamedata
```

Данные EA в репозиторий не попадают. Синтетические чанки v2–v15 и v16+ генерирует `src/SyntheticBlend.h`.

## Что проверяется

| Тест | Метка | Что |
|---|---|---|
| `blend_v15_layout_roundtrip` | unit | синтетический v15: 32-битные индексы, 4 маски, классы, описания blend, cliffInfo; чтение до конца чанка |
| `blend_versions_9_to_15_masks` | unit | v9–v15: какие маски читаются (v10 impassableToPlayers, v11 passageWidths, v14 taintable, v15 extraPassability) и в каком порядке |
| `blend_v16_and_above_rejected` | unit | v16, v17, v23, v24, v25, v30 отвергаются до чтения данных, в лог пишется «unsupported version» |
| `blend_anomalies_clamped_masked_counted` | unit | висячие тайлы → clamp, флаги вне `&0x3` → маска, `customBlendEdgeClass` вне диапазона → −1, байты направления ≠ 0/1 только считаются; построчный лог и сводка |
| `blend_anomaly_log_first_16_of_each_kind` | unit | ревью #10: по 17–40 случаев каждого из 7 видов (тайлы, флаги, класс рёбер, байты направления, висячие blend / 3-way / cliff); в логе ровно по 16 строк каждого вида и одна сводка с полными итогами |
| `blend_limits_heap_tables` | unit | таблицы в куче по счётчикам чанка: ровно на лимитах (4 096 тайлов, 65 536 blend, 131 072 cliff) читается, на единицу больше отвергается. Ёмкость таблицы тайлов `firstTile + max(numTiles, width²)`; неверные классы текстур (отрицательные, `width > 64`, конец > 4 096, больше 256 классов) отвергаются |
| `blend_index_widths_and_sign` | unit | v≤13: 16-битные индексы со знаком, как `Short` апстрима (0xFFFF = −1, конструктор сбрасывает); v14/v15: u32 |
| `blend_use3way_reset` | unit | `m_use3WayTerrainBlends` выключен: индексы 3-way обнуляются после подсчёта висячих |
| `blend_zh_repo_maps` | unit | 43 карты ZH из репозитория (`BFME_ZH_MAPS_DIR`) |
| `zh_baseline_bit_identical` | unit | «до/после»: `src/BlendZhDump.cpp` против апстрима 8ef2349 и против этого дерева, синтетика v2–v8 и 43 карты ZH. Выгружаются таблицы и выход потребителей, требуется побайтное совпадение |
| `gamedata_refpack_all_maps_vs_oracle` | gamedata | 115 карт `maps.big`: размер и sha256 распакованных данных равны оракулу |
| `gamedata_blend_all_maps_vs_oracle_and_datacheck` | gamedata | 115 карт: sha256 полезной нагрузки чанка, счётчики, классы текстур, sha таблиц tiles/blends/3-way/cliff, ненулевые индексы и наличие масок равны оракулу OpenSAGE. Счётчики аномалий равны `tools/datacheck/out/maps.json`. Строк на случай не больше 16 каждого вида (Rhun: 711 тайлов → 16 строк) |
| `ctor_corrupt_map_frees_arrays` | unit | ревью #10: конструктор бросает `ERROR_CORRUPT_FILE_FORMAT` на каждом виде порчи (`dataSize` высот, в т. ч. `logicalDataOnly`; BlendTileData v16; число клеток; счётчики вне лимита; класс текстур и класс рёбер; `FLAG_VAL`; второй чанк BlendTileData с порчей; файл без `CkMp`) и не оставляет ни одного блока |
| `ctor_out_of_memory_at_every_allocation` | unit | ревью #10: `ERROR_OUT_OF_MEMORY` по очереди на каждом выделении загрузки (v15, v15 + второй чанк, v8 + второй чанк, v7, v15 `logicalDataOnly`): ни утечки, ни двойного `delete[]` (под ASan его ловит сразу). Без обнуления после `delete[]` в `ParseBlendTileData` ASan падает с double-free |
| `ctor_release_log_one_summary_line` | unit | ревью #10, релиз: карта с аномалиями даёт ровно одну строку `WARNING: BlendTileData v15 …` в stderr, карта с одной аномалией тоже; чистые карты (v15 и синтетика ZH v2–v8) не пишут ничего |
| `ctor_zh_repo_maps` | unit | 43 карты ZH из репозитория через загрузочный конструктор: грузятся, в stderr пусто, утечек нет |

В ASan-сборке `AsanSizedDelete.cpp` из `tests/heights/src` убирает ложный alloc-dealloc-mismatch: `GameMemoryNull` не заменяет sized `operator delete`. Апстрим теряет буфер тайлов v7 (в этом дереве утечка исправлена), поэтому в `compare_dumps.cmake` поиск утечек выключен только для OLD-бинаря.

## Результат (2026-10-09, ревью #10, gcc 13.3 Debian, cmake 3.31, ninja; образа `bfme-android-build` на боксе больше нет)

18/18 passed, в обычной сборке и под ASan (`-DBFME_TESTS_ASAN=ON`, без ошибок и утечек); `tests/heights` 19/19 так же. Счётчики, 115/115 карт и «до/после» (1 670 369 167 байт) те же, что 2026-10-02. Отказ выделения памяти: 40 / 55 / 53 / 40 / 26 выделений, каждое уронено по разу.

## Результат #10 (2026-10-02, gcc 13.3, образ `bfme-android-build`)

12/12 passed, в обычной сборке и под ASan (без ошибок и утечек):
- 115/115 карт: v15 на 114, v14 на одной (shellmap1). RefPack и BlendTileData совпадают с оракулом.
- Счётчики совпадают с datacheck:
  - висячие тайлы: 1 422 на 2 картах (Rhun mp/sps, по 711);
  - флаги вне `&0x3`: 126 на 3 картах (Central Ithilien mp/sps evil/sps good, по 42);
  - `customBlendEdgeClass` без классов рёбер: 129 (по 43), столько же байтов направления ≠ 0/1;
  - висячих индексов blend, 3-way и cliff нет.
- ZH «до/после»: 1 670 369 167 байт идентичны.
