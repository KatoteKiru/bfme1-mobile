# E1-25, шаг 1: аудит мест, где используются высоты террейна (u8 → u16)

Задача: хранить сырые высоты как `u16`. Один сырой шаг теперь `MAP_XY_FACTOR/256` (BFME1 `HeightMapData` v5, OpenSAGE `VerticalScale` = 0.0390625). Карты Generals/ZH (v1–v4, `u8` по `MAP_XY_FACTOR/16`) при загрузке расширяются сдвигом `<< 4`.

Номера строк даны по дереву после правки. Сокращения путей:

| Сокращение | Путь |
|---|---|
| `CGC/` | `Core/GameEngineDevice/Source/W3DDevice/GameClient/` |
| `CGI/` | `Core/GameEngineDevice/Include/W3DDevice/GameClient/` |
| `MDC/` | `GeneralsMD/Code/GameEngineDevice/Source/W3DDevice/GameClient/` |
| `MDL/` | `GeneralsMD/Code/GameEngineDevice/Source/W3DDevice/GameLogic/` |
| `MGL/` | `GeneralsMD/Code/GameEngine/Source/GameLogic/` |
| `WB/` | `GeneralsMD/Code/Tools/WorldBuilder/src/` |

## Принцип

`raw_u16 = u8 << 4`, а `MAP_HEIGHT_SCALE` уменьшен в 16 раз (`10/16` → `10/256`). Масштабирование на степень двойки в float точное (нет переполнения и потери нормализованных чисел), поэтому для карт ZH побитово сохраняются:
- любое выражение вида `raw * MAP_HEIGHT_SCALE`;
- разности и интерполяция таких величин (`(p1 + fy*(p2-p1) + …) * SCALE`);
- min/max и сравнения сырых значений между собой;
- нормали: `l2r = (2*XY/SCALE, 0, Δraw)`. Обе компоненты растут в 16 раз, `Normalized_Cross_Product` и `WWMath::Inv_Sqrt` дают тот же результат. Проверено тестом `formula_get_height_map_height_and_normal`: 400 000 случайных клеток, сравнение побитовое.

Менять нужно четыре класса мест:
1. **Типы.** Хранилище, указатели, локальные переменные `UnsignedByte`, а также `Short`, куда не влезает 65 535.
2. **Целочисленная арифметика и константы в сырых единицах ZH.** Деление с округлением, `±1`, `255`. Их пересчитываем в единицах ZH: `raw/16`, затем `*16`. Так ZH остаётся побитово прежним, а BFME получает ту же семантику.
3. **Мировые константы, записанные через `MAP_HEIGHT_SCALE`** (например, «подъём на 1/8 шага»). Иначе они уменьшатся в 16 раз, поэтому переводятся на `MAP_HEIGHT_SCALE_ZH`, то есть на прежнее значение.
4. **Формат файлов и сейвов.** Чтение карт и `xfer`.

Итог: **94 места** (строки таблицы; однотипные соседние вхождения объединены, по grep это около 210 строк в `Core/` и `GeneralsMD/` без Tools). Изменено или добавлено **44**, проверено без изменений **40**, не портировано **10** (WorldBuilder, Generals, мёртвый код).

## Таблица

### A. Хранение, типы, константы

| № | Файл:строка | Что делает | Что меняется |
|---|---|---|---|
| 1 | `Core/GameEngine/Include/Common/MapObject.h:58-66` | `MAP_HEIGHT_SCALE` = мировая z одного сырого шага | **Изменено:** `MAP_XY_FACTOR/256`; добавлены `MAP_HEIGHT_ZH_SHIFT 4`, `MAP_HEIGHT_ZH_STEP 16`, `MAP_HEIGHT_SCALE_ZH` (= старый масштаб) |
| 2 | `GeneralsMD/Code/GameEngine/Include/Common/MapReaderWriterInfo.h:35` | версии чанка `HeightMapData` | **Изменено:** `K_HEIGHT_MAP_VERSION_5` (BFME1, только чтение) |
| 3 | `CGI/WorldHeightMap.h:50-51` | `K_MIN_HEIGHT`/`K_MAX_HEIGHT` | **Изменено:** `K_MAX_HEIGHT` 255 → `0xFFFF` |
| 4 | `CGI/WorldHeightMap.h:53-54` | тип сэмпла | **Изменено:** `typedef UnsignedShort HeightSampleType` (перенесён из `BaseHeightMap.h`, где был `UnsignedByte`) |
| 5 | `CGI/WorldHeightMap.h:141` | `m_data` | **Изменено:** `HeightSampleType*` |
| 6 | `CGI/WorldHeightMap.h:244-245` | `getMin/MaxHeightValue()` | без изменений в коде (значение из №3); потребители: №49, 50, 66, 80, в WorldBuilder №89, 90, 92 |
| 7 | `CGI/WorldHeightMap.h:247` | `getDataPtr()` | **Изменено:** возвращает `HeightSampleType*` |
| 8 | `CGI/WorldHeightMap.h:265` | `getDisplayHeight()` | **Изменено:** возвращает `HeightSampleType` |
| 9 | `CGI/WorldHeightMap.h:268-275` | `getHeight()`, вне карты 0 | **Изменено:** возвращает `HeightSampleType` |
| 10 | `CGI/WorldHeightMap.h:333` | `setRawHeight()` | **Изменено:** параметр `HeightSampleType` (раньше `Int` обрезался до u8 при записи) |
| 11 | `CGI/BaseHeightMap.h:40` | typedef `HeightSampleType` | **Изменено:** удалён (оставлен комментарий), см. №4 |
| 12 | `CGI/BaseHeightMap.h:137-152` | `getClipHeight()` с клампом координат | **Изменено:** возвращает `HeightSampleType` |
| 13 | `Core/GameEngine/Include/GameClient/TerrainVisual.h:277-278`, `CGI/W3DTerrainVisual.h:122-123` | `set/getRawMapHeight(Int)` | без изменений: `Int` вмещает u16 |
| 14 | `MGL/Object/Update/TensileFormationUpdate.cpp:53` | локальный `#define MAP_HEIGHT_SCALE` (в файле не используется) | **Изменено:** приведён к `/256`, чтобы не расходиться с №1 |

### B. Загрузка карт

| № | Файл:строка | Что делает | Что меняется |
|---|---|---|---|
| 15 | `CGC/WorldHeightMap.cpp:851-862` | новое: `readHeightSamples()` | **Добавлено:** v≥5 читается как u16 LE как есть; v1–v4 читаются как u8 в верхнюю половину буфера и расширяются на месте `<< 4` |
| 16 | `CGC/WorldHeightMap.cpp:901` | `ParseHeightMapData`: аллокация `m_data` | **Изменено:** `new HeightSampleType[m_dataSize]` |
| 17 | `CGC/WorldHeightMap.cpp:915` | `ParseHeightMapData`: чтение сэмплов | **Изменено:** `readHeightSamples()` вместо `readArrayOfBytes(m_dataSize)` |
| 18 | `CGC/WorldHeightMap.cpp:917-926` | v1: прореживание 2× на месте | без изменений (работает с типом массива) |
| 19 | `CGC/WorldHeightMap.cpp:975` | `ParseSizeOnly`: аллокация | **Изменено:** как №16 |
| 20 | `CGC/WorldHeightMap.cpp:979` | `ParseSizeOnly`: чтение | **Изменено:** как №17 |
| 21 | `CGC/WorldHeightMap.cpp:981-992` | `ParseSizeOnly` v1: прореживание | без изменений |
| 22 | `CGC/WorldHeightMap.cpp:1248` | `ParseObjectData`: потолок z объектов `(255*10)*SCALE` | **Изменено:** через `MAP_HEIGHT_SCALE_ZH`, это мировая константа (1593.75) |
| 23 | `Core/GameEngine/Source/GameClient/MapUtil.cpp:206-235` | `MapCache` `ParseSizeOnly`: чтение высот | без изменений: мёртвый код после `return true` |

### C. Мировая высота, нормали, сравнения (инвариантны, без изменений кроме типа)

| № | Файл:строка | Что делает | Что меняется |
|---|---|---|---|
| 24 | `CGC/WorldHeightMap.cpp:1770` | `getUVForTileIndex`: `HEIGHT_SCALE = SCALE/XY` (2⁻⁴ → 2⁻⁸, оба точные) | без изменений: `deltaH*HEIGHT_SCALE`, `dx`, `dy`, `divisor` побитово те же |
| 25 | `CGC/WorldHeightMap.cpp:1790-1802` | min/max/deltaH четырёх углов | без изменений |
| 26 | `CGC/WorldHeightMap.cpp:1816, 1844, 1853, 1894, 1903-1919` | пороги растяжения скал, UV-сдвиги | без изменений (через №24) |
| 27 | `CGC/WorldHeightMap.cpp:2303-2333` | `initCliffFlagsFromHeights` / `setCellCliffFlagFromHeights`: скала, если разброс > 9.8 | без изменений; для BFME порог теперь с шагом u16 (тест `cliff_flags_threshold`) |
| 28 | `CGC/BaseHeightMap.cpp:809-821` | `Cast_Ray`: треугольники клетки `SCALE*getClipHeight` | без изменений |
| 29 | `CGC/BaseHeightMap.cpp:928` | `getHeightMapHeight` вне карты | без изменений |
| 30 | `CGC/BaseHeightMap.cpp:933-945` | интерполяция по треугольнику | без изменений (тест `formula_get_height_map_height_and_normal`) |
| 31 | `CGC/BaseHeightMap.cpp:978-1005` | сглаженная нормаль, `2*XY/SCALE` | без изменений (тот же тест) |
| 32 | `CGC/BaseHeightMap.cpp:1107-1114` | `isClearLineOfSight`: max из 4 сэмплов `* SCALE` | без изменений |
| 33 | `CGC/BaseHeightMap.cpp:1242-1245` | `getMaxCellHeight` | без изменений |
| 34 | `CGC/BaseHeightMap.cpp:1325-1328` | `evaluateAsVisibleCliff`: `INT_TO_REAL(bytes[i])*SCALE` | без изменений |
| 35 | `CGC/BaseHeightMap.cpp:1603-1608` | береговая линия: сравнение с водой | без изменений |
| 36 | `CGC/BaseHeightMap.cpp:1824-1825` | `m_minHeight/m_maxHeight = ht*SCALE` | без изменений |
| 37 | `CGC/BaseHeightMap.cpp:1975` | скорчи: `amtToFloat + clip*SCALE` | без изменений (константа: №62) |
| 38 | `CGC/BaseHeightMap.cpp:2122-2131` | `getStaticDiffuse`/вершина: нормаль и z | без изменений |
| 39 | `CGC/HeightMap.cpp:382-472` | `updateVB`: нормали и z вершин (4 варианта разбиения клетки) | без изменений |
| 40 | `CGC/HeightMap.cpp:647-687` | `updateVBForLight`: нормали | без изменений |
| 41 | `CGC/HeightMap.cpp:813-860` | `updateVBForLightOptimized`: нормали | без изменений |
| 42 | `CGC/HeightMap.cpp:1590-1591` | позиция точки света | без изменений |
| 43 | `CGC/HeightMap.cpp:2285-2288` | extra blend: z четырёх углов | без изменений (тип указателя: №58) |
| 44 | `CGC/W3DTerrainBackground.cpp:170, 526, 570` | z вершин фона | без изменений |
| 45 | `CGC/W3DTerrainBackground.cpp:291-299, 424-432` | «плоский ли патч»: сравнение raw | без изменений |
| 46 | `MDC/W3DCustomEdging.cpp:222` | z кастомных краёв | без изменений |
| 47 | `MDC/Shadow/W3DProjectedShadow.cpp:430, 1071, 1090` | z сетки проекционной тени | без изменений |
| 48 | `MDC/Shadow/W3DVolumetricShadow.cpp:457` | z вершины объёмной тени | без изменений (тип: №59) |
| 49 | `MDC/W3DRoadBuffer.cpp:643-644, 866-867` | начальные min/max: `getMax/MinHeightValue()*SCALE` | без изменений: стартовое значение для поиска минимума и максимума (2559.96 вместо 159.375), результат тот же |
| 50 | `MDL/W3DTerrainLogic.cpp:144` | начальный `minHt = getMaxHeightValue()` | без изменений (то же) |
| 51 | `MDL/W3DTerrainLogic.cpp:154-155` | `m_mapMinZ/MaxZ = ht*SCALE` | без изменений |
| 52 | `CGC/W3DTerrainVisual.cpp:886-926` | `setRawMapHeight` (только опускает), `getRawMapHeight` | без изменений: `Int`, сравнения raw |
| 53 | `Core/GameEngine/Source/GameClient/Terrain/TerrainVisual.cpp` (`DomeStyleSeismicFilter`) | скорости сейсмики в единицах ZH, `MIN 9.0f` | без изменений (единицы учтены в №70) |

### D. Типы и сужения

| № | Файл:строка | Что делает | Что меняется |
|---|---|---|---|
| 54 | `CGC/BaseHeightMap.cpp:931` | `getHeightMapHeight`: `const UnsignedByte* data` | **Изменено:** `HeightSampleType*` |
| 55 | `CGC/BaseHeightMap.cpp:964` | `d0..d11` | **Изменено:** `HeightSampleType` |
| 56 | `CGC/BaseHeightMap.cpp:1093, 1241, 1315, 1789` | `data`/`bytes[4]` в LOS, `getMaxCellHeight`, `evaluateAsVisibleCliff`, `initHeightData` | **Изменено:** `HeightSampleType` |
| 57 | `CGC/BaseHeightMap.cpp:770, 1819` | `Short cur` в `Cast_Ray` и `initHeightData` | **Изменено:** `Int` (65 535 > `SHRT_MAX`) |
| 58 | `CGC/HeightMap.cpp:1259, 1715, 2255` | `data` в `updateBlock`, `Short cur` в `updateCenter`, `data` в extra blend | **Изменено:** `HeightSampleType` / `Int` |
| 59 | `MDC/Shadow/W3DVolumetricShadow.cpp:454` | `UnsignedByte *data` | **Изменено:** `HeightSampleType*` |
| 60 | `MDL/W3DTerrainLogic.cpp:149` | `Short cur` | **Изменено:** `Int` |
| 61 | `CGC/W3DTerrainVisual.cpp:400` | сейсмика: `UnsignedByte heightOfOriginal` | **Изменено:** `HeightSampleType` |

### E. Мировые константы через `MAP_HEIGHT_SCALE`

| № | Файл:строка | Что делает | Что меняется |
|---|---|---|---|
| 62 | `CGC/BaseHeightMap.cpp:1953` | скорчи над землёй на `SCALE/10` | **Изменено:** `MAP_HEIGHT_SCALE_ZH/10` (0.0625) |
| 63 | `MDC/W3DRoadBuffer.cpp:570-571` | дороги: `FLOAT_AMOUNT = SCALE/8`, `MAX_ERROR = SCALE*1.1` | **Изменено:** через `MAP_HEIGHT_SCALE_ZH` |
| 64 | `MDC/W3DRoadBuffer.cpp:789-790` | то же для освещённых секций | **Изменено:** то же |
| 65 | `CGC/BaseHeightMap.cpp:249` | начальный `m_maxHeight = (256^sizeof-1)*SCALE` | **Изменено:** `sizeof(UnsignedByte)`, `MAP_HEIGHT_SCALE_ZH` (159.375, как в апстриме; реальное значение ставит `initHeightData`) |
| 66 | `CGC/BaseHeightMap.cpp:700-704` | `Cast_Ray`: верх бокса `SCALE*getMaxHeightValue()+XY` | **Изменено:** `MAX(255*SCALE_ZH, m_maxHeight)+XY`. С `0xFFFF` бокс поднялся бы до 2570, выше камеры, а луч из бокса ничего не находит. На ZH значение прежнее |
| 67 | `MDC/Shadow/W3DVolumetricShadow.cpp:1261` | debug-assert: паддинг ≤ `K_MAX_HEIGHT*SCALE` | **Изменено:** `(Real)K_MAX_HEIGHT*MAP_HEIGHT_SCALE` (2559.96; только debug) |

### F. Целочисленная арифметика в сырых единицах ZH

| № | Файл:строка | Что делает | Что меняется |
|---|---|---|---|
| 68 | `CGC/BaseHeightMap.cpp:776-777` | `Cast_Ray`: бокс клеток `SCALE*(minHt-1)`, `SCALE*(maxHt+1)` | **Изменено:** `±MAP_HEIGHT_ZH_STEP`, то же поле 0.625 (тест `formula_cast_ray_boxes`) |
| 69 | `CGC/HeightMap.cpp:1719-1720` | `updateCenter`: `intersectionZ = (float)minHt` (баг апстрима: raw как мировая z) | **Изменено:** `minHt/MAP_HEIGHT_ZH_STEP`, сохраняет прежнее значение |
| 70 | `CGC/W3DTerrainVisual.cpp:407-408` | сейсмика: `getHeight + newSpeed` (скорость в единицах ZH) | **Изменено:** `(Int)(h/16.f + speed)*16` (мёртвый код: `DO_SEISMIC_SIMULATIONS` не определён) |
| 71 | `CGC/W3DTerrainVisual.cpp:420-421` | сейсмика: clamp 255 | **Изменено:** `K_MAX_HEIGHT` (вопрос к Лиду) |
| 72 | `CGC/W3DTerrainVisual.cpp:424` | `setRawHeight(heightToUse)` | без изменений (тип: №10) |
| 73 | `CGC/WorldHeightMap.cpp:1805-1807` | `getUVForTileIndex`: `belowLimit = minH+(2d+1)/3`, `aboveLimit = minH+(d+1)/3` | **Изменено:** округление в единицах ZH, `((2d+16)/48)*16` и `((d+16)/48)*16` (тест `formula_uv_cliff_limits`, полный перебор) |
| 74 | `MGL/Map/TerrainLogic.cpp:2709-2710` | `flattenTerrain` (прямоугольник): `rawDataHeight = floor(0.5+avg/SCALE)` | **Изменено:** `floor(0.5+avg/SCALE_ZH)*16` (тест `formula_flatten_and_crater`) |
| 75 | `MGL/Map/TerrainLogic.cpp:2714` | `centerHeight = floor(ground/SCALE)` | **Изменено:** то же, `*16` |
| 76 | `MGL/Map/TerrainLogic.cpp:2732-2758` | 9 × `setRawMapHeight(rawDataHeight)` | без изменений |
| 77 | `MGL/Map/TerrainLogic.cpp:2799-2800` | `flattenTerrain` (круг): `rawDataHeight` | **Изменено:** как №74 |
| 78 | `MGL/Map/TerrainLogic.cpp:2814-2840` | 9 × `setRawMapHeight` | без изменений |
| 79 | `MGL/Map/TerrainLogic.cpp:2895-2900` | `createCraterInTerrain`: `MAX(1, raw - displacement)`, смещение в единицах ZH | **Изменено:** `MAX(1, raw/16.f - d)`, затем `*16` |
| 80 | `CGC/BaseHeightMap.cpp:765`, `CGC/HeightMap.cpp:1711` | начальный `minHt = getMaxHeightValue()` | без изменений (стартовое значение поиска минимума) |

### G. Сейв

| № | Файл:строка | Что делает | Что меняется |
|---|---|---|---|
| 81 | `CGC/W3DTerrainVisual.cpp:1144-1157` | новое: `heightsFitZHLayout()` | **Добавлено:** все raw кратны 16 и ≤ 4080 |
| 82 | `CGC/W3DTerrainVisual.cpp:1176-1182` | `xfer`: версия 3 | **Изменено:** `currentVersion 4`. При сохранении пишется v3, если высоты укладываются в раскладку ZH (все карты ZH), поэтому сейвы ZH побайтно прежние |
| 83 | `CGC/W3DTerrainVisual.cpp:1234-1260` | `xfer`: `xferUser(data, len)` u8 | **Изменено:** v4 пишет u16 (`len*2`); v2/v3 пишут u8 (`>>4` при сохранении, `<<4` при загрузке) |
| 84 | `CGC/W3DTerrainVisual.cpp` `crc()` | CRC визуала | без изменений: в lockstep CRC `TerrainVisual` не входит |

### H. Не портировано (сборка выключена в пресетах `linux64*`/`android*`: `RTS_BUILD_ZEROHOUR_TOOLS=OFF`, `RTS_BUILD_GENERALS=OFF`)

| № | Файл:строка | Что делает | Статус |
|---|---|---|---|
| 85 | `WB/WHeightMapEdit.cpp:117, 222, 1214, 1802, 2226` | `m_data = new UnsignedByte[...]`, буферы обработки | **не меняется**, после правки не собирается (Windows). Вопрос к Лиду |
| 86 | `WB/WHeightMapEdit.cpp:579-597` | запись `HeightMapData` v4, u8 | не меняется (запись BFME v5 — отдельная задача) |
| 87 | `WB/WHeightMapEdit.cpp:1632, 2196, 2278-2283` | `setHeight`, `HEIGHT_SCALE`, авто-UV скал | не меняется |
| 88 | `WB/FeatherTool.cpp:114, 181-240` | `UnsignedByte*` данные, clamp 255 | не меняется |
| 89 | `WB/MoundTool.cpp:183-213`, `WB/MoundOptions.cpp:178`, `WB/brushoptions.cpp:178-199`, `WB/BrushTool.cpp` | кисти: clamp по `getMax/MinHeightValue`, шкала 255, подписи `*SCALE` | не меняется |
| 90 | `WB/MeshMoldTool.cpp:133, 248-251`, `WB/RampTool.cpp:171` | `/SCALE`, `(UnsignedByte)` | не меняется |
| 91 | `WB/WaterTool.cpp:198`, `WB/WaterOptions.cpp:354`, `WB/WorldBuilderView.cpp:555-565`, `WB/MapPreview.cpp:312` | вода, контуры до 255, превью | не меняется |
| 92 | `WB/WBHeightMap.cpp:173` | `Cast_Ray` WB: `SCALE*getMaxHeightValue()` | не меняется (аналог №66) |
| 93 | `Generals/Code/**` (около 20 файлов: `MAP_HEIGHT_SCALE`, `getDataPtr`) | vanilla Generals поверх общего `Core` | не поддерживается после правки (общий `Core/…/WorldHeightMap.h`) |
| 94 | `#ifdef DO_SEISMIC_SIMULATIONS` (№70–72, `TerrainVisual.cpp`) | сейсмика | мёртвый код, но поправлен и собран с `-DDO_SEISMIC_SIMULATIONS` (`-fsyntax-only`) |

## Проверка

См. `tests/heights/README.md`.
- Побитовая сверка ZH «до и после»: один и тот же дамп-инструмент собран против апстрима 8ef2349 (u8) и против этой ветки. Вход: 25 синтетических карт v1–v4 и 43 карты ZH из `GeneralsOnlineRelease/…/Maps`, всего 4 743 768 сэмплов. Сравниваются мировые высоты (full и size-only парсинг) и 377 895 скальных клеток: 42 464 345 байт идентичны.
- BFME1: 115/115 карт `maps.big` через настоящий парсер движка. Совпадают с оракулом OpenSAGE по версии, размеру, бордеру, min/max raw и мировой высоте, и по всем 25 374 931 сэмплам.
- Формулы, которые этот проект не линкует (`BaseHeightMap`, `TerrainLogic`, `W3DTerrainVisual`, `HeightMap`), проверены копиями выражений «апстрим против E1-25» перебором диапазона ZH.
- `-fsyntax-only` с флагами `android-vulkan` (NDK 27.2) на 66 TU, которые включают изменённые заголовки: все OK.
