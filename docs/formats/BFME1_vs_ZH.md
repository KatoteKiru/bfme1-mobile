# BFME1 (патч 2.22) против Zero Hour: совместимость форматов

Статус: **v0.4** (карты проверены на реальных данных; W3D и шейдеры пока по исходникам, w3d.big и shaders.big ещё копируются). Автор: «Форматы и данные». Дата: 2026-10-02.

Обозначения вердиктов:
- ✅ **как есть**: код ZH читает без правок.
- 🟡 **доработка**: парсер ZH есть, но нужна поддержка новой версии или полей.
- 🔴 **новый код**: в ZH нет ни парсера, ни функциональности.
- ⚠️ **тихая порча**: ZH прочитает без ошибки, но данные будут неверными. Самый опасный случай.

## 0. Резюме
1. **Карты: ни одна карта BFME1 не загрузится кодом ZH без правок.** Проверено на всех 115 картах `maps.big`. Раскладка всех чанков известна и подтверждена (разбор по раскладке, описанной в OpenSAGE, даёт остаток 0 байт; матрица версий совпала с оракулом `tools/oracle`), поэтому чтение карт BFME1 — понятная работа: **E1-25 занимает 6.75–9 дней** под решения Лида (§6.1, вместе с u16-высотами по движку), плюс 2–3 дня на остальные новые чанки. Подробная раскладка BlendTileData v8→v15 в §2.4, HeightMapData v4→v5 в §2.5.
2. Жёсткий отказ ZH: BlendTileData v15 (32-битные индексы + 4 новых битовых слоя), PolygonTriggers v5 (6 текстур реки и параметры воды на полигон), SidesList v6 (bool и вынос Teams и скриптов в отдельные чанки).
3. **Тихая порча** (опаснее всего): HeightMapData v5 (высоты u16, масштаб 10/256, диапазон ×10), GlobalLighting v7 (9 источников на время суток вместо 6, плюс infantry-light). ZH читает их без ошибок и получает неверные данные.
4. **Переполнение лимитов террейна**: blends до 34 143 (ZH 16 192), cliffs до 102 443 (ZH 32 384), тайлов до 2 036 (ZH 1 024).
5. Как есть: контейнер (`EAR`/RefPack, CkMp, Dict, NameKey), ObjectsList/Object v3, WorldInfo v1, WaypointsList v1, бинарный формат Script v2 / Condition v4 / Action v2.
6. Новые чанки BFME1 (ZH молча пропускает): Teams, PlayerScriptsList и BuildLists на верхнем уровне, MPPositionList, LibraryMapLists, EnvironmentData, NamedCameras, SkyboxSettings. Скрипты в BFME1 используют **126 действий и 41 условие, которых нет в ZH** (18 % и 13 % вызовов).
7. **W3D (по исходникам, w3d.big ещё копируется)**: модели, скелеты, HLod, ящики и сжатые анимации ожидаются совместимыми. Возможные доработки: канал анимации типа 15, AdaptiveDelta8, DetailColorFunc 10–12, частицы W3D_*. FX-шейдеры и тангенты по источникам относятся к BFME2. Неизвестные чанки ZH пропускает молча.
8. **Шейдеры**: ни DX8 `.pso` ZH, ни DX9-шейдеры BFME1 на Android не выполняются (d3d8gles работает как fixed-function, 2 стадии). Минимальный набор GLSL ES 3.0: террейн (+атлас), вода и реки, скайбокс, тени, частицы — **3–5 недель**. Главный риск Э1.
9. Текстуры DXT на Mali/PowerVR распаковываются на CPU (×4–8 памяти), нужен конвертер в ASTC (3–5 дн).
10. Итого форматы + рендер: **около 7–10 недель-человек**, первая MP-карта (только террейн) — около 2 недель. ScriptEngine BFME отдельно: 3–6 недель (геймплей).

## 1. Кодовая база и источники

| Что | Где | Ревизия |
|---|---|---|
| Основа порта (ZH + Android: d3d8gles, SDL3) | `/workspace/bfme-mobile/repo` (ветка `bfme`, форк MYSOREZ/GeneralsZH-Android-Port, сам основан на TheSuperHackers/GeneralsGameCode) | `e29eade` поверх `8ef2349` |
| Эталон ZH | `src/GeneralsGameCode` (TheSuperHackers) | main @2817000 |
| Эталон форматов BFME | `src/OpenSAGE` (OpenSAGE/OpenSAGE) | master @588ac477 |
| Инструменты проверки | `tools/formatprobe/{probe.py,mapdump.py,allblend.py}` (Python, только чтение, распаковка в память; собственная реализация, код OpenSAGE не копировался) | этот документ |
| Оракул OpenSAGE | `tools/oracle` (`./oracle.sh big\|map\|maps`, JSON в `tools/oracle/out/`) | OpenSAGE @588ac477 |

**Лицензии.** OpenSAGE распространяется под **GPL-3.0** (не LGPL), а портированные из EA части — с дополнительными условиями `LICENSE-EA.md`. В этом документе OpenSAGE служит **только эталоном и ссылкой** на описание форматов и источником данных для сверки через оракул. Код OpenSAGE (C#) в движок переносить **не предлагается**: парсеры BFME1 пишутся на C++ в коде ZH, по описанным здесь раскладкам.

Ключевые файлы ZH (пути в `repo/`):
- DataChunk: `GeneralsMD/Code/GameEngine/Source/Common/System/DataChunk.cpp`. Неизвестный чанк молча пропускается (seek на конец чанка). **Версия известного чанка не проверяется сверху**: парсер видит `info->version` и сам ветвится по `>=`.
- Версии: `Core/GameEngine/Include/Common/MapReaderWriterInfo.h`, `GeneralsMD/Code/GameEngine/Source/GameLogic/Map/SidesList.cpp`, `.../GameLogic/ScriptEngine/Scripts.cpp`.
- Парсеры террейна: `Core/GameEngineDevice/Source/W3DDevice/GameClient/WorldHeightMap.cpp` (HeightMapData, BlendTileData, GlobalLighting, WorldInfo, ObjectsList, PolygonTriggers), лимиты в `Core/GameEngineDevice/Include/W3DDevice/GameClient/WorldHeightMap.h`.
- W3D: `GeneralsMD/Code/Libraries/Source/WWVegas/WW3D2/w3d_file.h`, `meshmdlio.cpp`, `hanim*.cpp`, `hlod.cpp`.
- Шейдеры: `GeneralsMD/Code/GameEngineDevice/Source/W3DDevice/GameClient/W3DShaderManager.cpp`; GLES-слой `Core/Libraries/Source/d3d8gles/`.

Ключевые файлы OpenSAGE (пути в `src/OpenSAGE/src/`):
- Карты: `OpenSage.Game/Data/Map/*.cs` (HeightMapData, BlendTileData, BlendDescription, GlobalLighting, EnvironmentData, PolygonTrigger, MPPositionList, NamedCameras, SkyboxSettings, CastleTemplates, LibraryMapLists), `OpenSage.Game/Logic/Map/SidesList.cs`, `OpenSage.Game/Scripting/*`.
- W3D: `OpenSage.FileFormats.W3d/*` (W3dChunkType.cs и парсеры по чанкам).
- Рендер BFME: `OpenSage.Game/Graphics/Shaders/*`, `ShaderResourceManager.cs`, `ModelMesh.W3d.cs`.

## 2. Карты (.map, DataChunk)

### 2.1 Контейнер
| Элемент | ZH | BFME1 (факт, 115 карт maps.big) | Вердикт |
|---|---|---|---|
| Сжатие файла | `EAR\0`+RefPack, `ZL5\0`, raw (CachedFileInputStream + CompressionManager) | 115/115 `EAR\0` (RefPack) | ✅ |
| TOC `CkMp` + заголовок чанка (id u32, ver u16, size i32) | DataChunkInput | тот же | ✅ |
| NameKey в чанках (`(tocId<<8) \| type`) | `readNameKey` | тот же (проверено: все 432 имени скриптов разрешаются по TOC) | ✅ |
| Dict (типы 0..4: bool, int, real, ascii, unicode) | `readDict` | тот же (WorldInfo разобран без остатка) | ✅ |
| Неизвестный чанк верхнего уровня | пропускается молча | — | ✅ (но данные теряются) |
| Известный чанк новее, чем знает ZH | **версия не проверяется**, парсер идёт по ветке `>=` | — | ⚠️ / исключение |

### 2.2 Чанки: версии и вердикты
Версии BFME1 ниже **фактические**, по всем 115 картам из `gamedata/maps.big` (кампания и MP базовой игры). Карты патча 2.22 лежат в `_patch222*.big`, их ещё не скопировали: **проверить повторно**.

| Чанк | ZH (макс. версия) | BFME1 факт | Что нового в BFME1 (OpenSAGE + проверка) | Что сделает код ZH | Вердикт | Объём |
|---|---|---|---|---|---|---|
| HeightMapData | v4, высоты **u8**, `MAP_HEIGHT_SCALE = 10/16 = 0.625` | **v5** (115/115) | высоты **u16** (`dataSize == w*h`, реально 2 байта на сэмпл); масштаб 0.0390625 (OpenSAGE `HeightMapData.cs`); max сырое 38400, то есть 1500 ед. мира против 159 в ZH; карты до 600×770 | `dataSize==w*h` проходит, читает **половину** массива как u8 и молча пропускает остаток | ⚠️ тихая порча | парсер 0.5 дн; u16 по всему коду (`m_data` UnsignedByte: WorldHeightMap, HeightMapRenderObj, TerrainLogic, pathfinder, WorldBuilder-утилиты) 3–5 дн |
| BlendTileData | v8 | **v15** (114), v14 (1) | индексы blend/3-way/cliff **32 бит** при 14≤v<24 (было 16); после cliffState новые битовые слои: impassToPlayers (v10), passageWidths (v11), taintability (v14), extraPassability (v15); таблицы текстур, BlendDescription (18 байт) и CliffInfo (38 байт) **по раскладке совпадают с ZH**; проверено: разбор по OpenSAGE даёт остаток 0 на 115/115 | читает 16-битные массивы, сдвигается, на `FLAG_VAL` бросает `ERROR_CORRUPT_FILE_FORMAT` | 🟡 доработка (жёсткий отказ) | парсер 1 дн; **лимиты** (см. ниже) 1–3 дн |
| — лимиты BlendTileData | `NUM_SOURCE_TILES 1024`, `NUM_BLEND_TILES 16192`, `NUM_CLIFF_INFO 32384`, `NUM_TEXTURE_CLASSES 256` | max: textureCells **2036**, blends **34143**, cliffs **102443**, textures 115 | — | статические массивы переполнятся (порча памяти) | 🟡 | поднять лимиты, переделать атлас тайлов террейна (2036 тайлов 64×64 не влезут в текущий атлас); вместе с рендером террейна |
| WorldInfo | v1 (Dict) | v1 | новые ключи Dict: `cameraGroundMinHeight/MaxHeight`, `cameraScrollSpeedScalar`, `compression`, `isScenarioMultiplayer` | Dict читается; новые ключи игнорируются | ✅ формат / 🟡 смысл | 0.5 дн, применить ключи камеры |
| ObjectsList / Object | v3 / v3 | v3 / v3 (225 301 объект) | раскладка та же (pos, angle, flags, name, Dict) | читается; шаблоны объектов BFME в ZH не существуют | ✅ формат; объекты зависят от INI и геймплея | — |
| PolygonTriggers | v4 (layerName) | **v5** (115/115) | у каждого полигона после riverStart: 6 строк текстур воды (river, noise, alphaEdge, sparkle, bump, sky), additive u8, RGB, u8 0, uvScroll vec2, alpha f32; проверено на реальной карте (остаток 0) | первую строку текстуры читает как numPoints, получает мусорный размер и падает (OOM или выход за чанк) | 🟡 доработка парсера (жёсткий отказ); 🔴 рендер рек | парсер 0.5 дн; рендер воды и рек см. §4 |
| GlobalLighting | v3: на время суток 6 источников (T0, O0, O1, O2, T1, T2) + shadowColor | **v7**, размер 1348 на 115/115 | на время суток **9** источников (terrain/object/**infantry** × sun/accent1/accent2) + shadowColor + 44 неизвестных байта (v7–8); раскладка 4+4·9·36+4+44 = 1348 подтверждена | читает 6 из 9 и съезжает: огни следующих времён суток перепутаны, без ошибки | ⚠️ тихая порча | 0.5 дн парсер + infantry-light в рендер 1 дн |
| WaypointsList | v1 | v1 (часто пустой, 4 байта) | — | читается | ✅ | — |
| SidesList | v3 (стороны, buildlists, **teams и PlayerScriptsList внутри**) | **v6** (115/115) | v6: ведущий bool; при v≥5 teams и скрипты **вынесены** в отдельные чанки верхнего уровня (OpenSAGE `Logic/Map/SidesList.cs`) | bool+int читает как `count`, и дальше мусор → исключение | 🟡 доработка | 1 дн |
| Teams (верхний уровень) | нет (внутри SidesList) | v1 (115/115) | список Dict команд | пропускается: команд нет, скрипты ссылаются в пустоту | 🟡 перенести парсер | 0.5 дн |
| PlayerScriptsList (верхний уровень) | только вложенный в SidesList | v5 (115/115) | внутри ScriptList v1 → Script **v2**, ScriptGroup **v3** (**вложенные группы** до 5 уровней), OrCondition v1, Condition **v4**, ScriptAction/ScriptActionFalse **v2** | на верхнем уровне не зарегистрирован, поэтому пропускается: **на карте нет ни одного скрипта** | 🟡 регистрация + вложенные группы | 1 дн |
| — ScriptGroup v3 | v2 (`if version == 2` читает subroutine) | v3 | вложенные ScriptGroup; флаг subroutine | v3 не равно 2, поэтому subroutine не читается; вложенные группы пропускаются (теряется не менее 3 300 скриптов) | 🟡 | 0.5 дн |
| — Script / Condition / Action | v2 / v4 / v2 | v2 / v4 / v2 | **бинарно совпадает с ZH** (новые поля OpenSAGE Script v3+, Condition v5, Action v3 относятся к BFME2+) | читается | ✅ формат | — |
| — набор условий и действий | 343 действия, 103 условия (`ScriptEngine.cpp`) | 321 действие / 111 условий используется в картах | **126 действий (17.9 % вызовов) и 41 условие (13 %) в ZH отсутствуют**: BUILD_BASE_BUILDING_PER_TACTICAL_MARKER, PLAY_SOUND_EFFECT_AT_TEAM, TEAM_GUARD_FOR_SECONDS, MARK_MISSION_OBJECTIVE_COMPLETED, COUNTER_COUNTER, IS_GAME_IN_SKIRMISH_OR_MULTIPLAYER, CAN_BUILD_AT_BASE… Полный список: `data/bfme1_script_names_maps_big.tsv` | неизвестное имя: `DEBUG_CRASH` + условие становится FALSE; число параметров не сверяется | 🔴 новый код (геймплей и ИИ) | вне форматов; 3–6 нед на сторону ScriptEngine (команда «Геймплей») |
| BuildLists (верхний уровень) | нет (внутри SidesList) | v1 | build list игроков-ИИ | пропускается | 🟡 | 0.5 дн |
| MPPositionList / MPPositionInfo | нет | v0 / v1 | позиции старта MP: isHuman, isComputer, loadAIScript, team, sideRestrictions | пропускается | 🔴 (небольшой) | парсер 0.5 дн + логика лобби и старта |
| LibraryMapLists / LibraryMaps | нет | v1 / v1 | ссылки на библиотечные карты (libraries.big, ИИ-базы) | пропускается | 🔴 | 1 дн + загрузка библиотек |
| EnvironmentData | нет | v3 | waterMaxAlphaDepth, deepWaterAlpha, isMacroTextureStretched, macro texture (`TSNoiseUrb.tga`), cloud texture (`TSCloudMed.tga`); проверено (остаток 0) | пропускается | 🔴 | парсер 0.5 дн; применение в рендере §4 |
| NamedCameras | нет | v2 | именованные камеры для синематиков | пропускается | 🔴 | 0.5 дн + скрипт-действия камер |
| SkyboxSettings | нет | v1 | pos, scale, rotation, textureScheme (`DefaultSky`); проверено | пропускается | 🔴 | 0.5 дн + скайбокс 1–2 дн |
| CastleTemplates | нет | **не встречен** в maps.big (поддержка в OpenSAGE с BFME) | — | — | проверить на `_patch222*.big` | — |
| MapPreview | v1 | **нет** (превью лежит рядом в `.tga`, 215 tga в maps.big) | — | — | ✅ (UI берёт tga) | — |
| StandingWaterAreas, RiverAreas, TriggerAreas, StandingWaveAreas | нет | **нет** (появились в BFME2; в BFME1 вода и реки живут в PolygonTriggers v5) | — | — | не нужно | — |

Итог по картам: **ни одна карта BFME1 не загрузится кодом ZH без правок**. Отказ будет жёсткий (BlendTileData, PolygonTriggers, SidesList), к нему добавится тихая порча (HeightMapData, GlobalLighting) и потеря данных (Teams, PlayerScriptsList, BuildLists на верхнем уровне). Зато бинарная раскладка во всех случаях известна и подтверждена на 115/115 картах с нулевым остатком. Map-reader BFME1 — это **1–1.5 недели** одного инженера (парсеры с проверкой версий плюс u16-высоты по коду). Новые сущности (MP-позиции, библиотеки, камеры, скайбокс, environment) — ещё около недели на функциональность, без учёта ScriptEngine.

### 2.3 Новые/изменённые поля: кратко по раскладке (для реализации)
- **HeightMapData v5**: `w i32, h i32, border i32, numBorders i32, {x i32, y i32}×numBorders, dataSize i32, u16[dataSize]`. Высота в мире = raw × 0.0390625 (= 10/256).
- **BlendTileData v14–v15**: `n u32 (=w*h); tiles u16[n]; blends u32[n]; threeWay u32[n]; cliff u32[n]; cliffState bits[(w+7)/8 × h]; impassToPlayers bits; passageWidths bits; taint bits; [v15] extraPassability bits;` далее как в ZH v8: `numBitmapTiles, numBlendedTiles, numCliffInfo, numTextureClasses, {first,num,width,legacy,name}×, numEdgeTiles, numEdgeTextureClasses(0), blend×(numBlended-1) по 18 Б (…, customBlendEdgeClass, 0x7ADA0000), cliffInfo×(numCliff-1) по 38 Б`.
- **GlobalLighting v7**: `tod i32; 4 × [T0,O0,I0, T1,O1,I1, T2,O2,I2] (порядок по OpenSAGE GlobalLighting.cs, свет = ambient rgb, diffuse rgb, dir xyz); shadowColor u32; 44 байта unknown`.
- **PolygonTriggers v5**: `name a, layer a, id i32, isWater u8, isRiver u8, riverStart i32, riverTex a, noise a, alphaEdge a, sparkle a, bump a, sky a, additive u8, r u8 g u8 b u8, 0 u8, uvScroll f32×2, alpha f32, numPoints i32, {x,y,z i32}×n`.
- **SidesList v6**: `unknown bool; numSides; {Dict, numBuild, BuildListInfo×}` (BuildListInfo как ZH v3; в BFME1 по OpenSAGE без дополнительных полей); teams и скрипты не внутри.

### 2.4 BlendTileData v8 → v15: подробно (для E1-25 / M1a)

Источники: ZH `repo/Core/GameEngineDevice/Source/W3DDevice/GameClient/WorldHeightMap.cpp` @ `e29eade`, функция `WorldHeightMap::ParseBlendTileData`, строки 1038–1195. Версии из `repo/GeneralsMD/Code/GameEngine/Include/Common/MapReaderWriterInfo.h:35-42`. BFME1: раскладка из OpenSAGE `src/OpenSage.Game/Data/Map/BlendTileData.cs`, `BlendDescription.cs`, `BlendTileTexture.cs`, `CliffTextureMapping.cs` (только ссылка и эталон). **Проверено на 115/115 картах `maps.big`**: остаток 0 байт, `FLAG_VAL` совпал у всех 2 184 798 описаний blend (`tools/formatprobe/blendstats.py`). Матрица версий совпала с оракулом.

Обозначения: `N = w*h` из HeightMapData (`m_dataSize`), `B = (w+7)/8` (байт на строку битовой маски, LSB-first: бит `x&7` байта `y*B + (x>>3)`, как в `getCliffState`, `WorldHeightMap.cpp:706`), `a` — ascii-строка `u16 len + bytes`. Порядок байтов везде little-endian.

#### 2.4.1 Побайтная раскладка: ZH v8 и BFME1 v15 рядом

| # | Поле | ZH v8: тип × кол-во, куда кладётся (строка) | BFME1 v15: тип × кол-во | Отличие |
|---|---|---|---|---|
| 1 | numTiles | `i32`, должен быть `== m_dataSize`, иначе `throw` (1041–1044) | `u32 == w*h` | = |
| 2 | tiles | `i16 × N` → `m_tileNdxes` (1062) | `u16 × N` | = (значения ≤ 24 839 влезают в Short; см. 2.4.4) |
| 3 | blends | `i16 × N` → `m_blendTileNdxes` (1063) | **`u32 × N`** | **ширина 16 → 32** |
| 4 | threeWayBlends | `i16 × N` → `m_extraBlendTileNdxes` (v≥6, 1064–1068) | **`u32 × N`** | **16 → 32** |
| 5 | cliffTextures | `i16 × N` → `m_cliffInfoNdxes` (v≥5, 1071–1073) | **`u32 × N`** | **16 → 32** |
| 6 | cliffState / impassability | `u8 × B×h` → `m_cellCliffState` (v≥7; при v==7 ширина `(w+1)/8`; при v<7 `initCliffFlagsFromHeights()`, 1074–1089) | `u8 × B×h` | = |
| 7 | impassabilityToPlayers | — | **`u8 × B×h`** (v≥10) | новое |
| 8 | passageWidths | — | **`u8 × B×h`** (v≥11) | новое |
| 9 | taintability | — | **`u8 × B×h`** (14≤v<25) | новое |
| 10 | extraPassability | — | **`u8 × B×h`** (v≥15) | новое |
| 11 | numBitmapTiles (textureCellCount) | `i32` → `m_numBitmapTiles`, assert `<2048` (1090–1091) | `u32` (max 2 036) | = (лимит, см. 2.4.5) |
| 12 | numBlendedTiles | `i32` → `m_numBlendedTiles`, assert `<NUM_BLEND_TILES+1` (1092–1093) | `u32` (= описаний + 1; max 34 143) | = (лимит) |
| 13 | numCliffInfo | `i32` → `m_numCliffInfo` (v≥5, 1094–1098) | `u32` (= записей + 1; max 102 443) | = (лимит) |
| 14 | numTextureClasses | `i32` → `m_numTextureClasses`, assert `<200` (1100–1101) | `u32` (max 115) | = |
| 15 | textureClass × numTextureClasses | `firstTile i32, numTiles i32, width i32, legacy i32, name a` → `m_textureClasses[i]` + `readTexClass` (1102–1114) | `cellStart u32, cellCount u32, cellSize u32 (cellSize² == cellCount), magic u32 == 0, name a` | = (legacy = 0 на 115/115; cellSize до 16, т.е. 1024×1024 px) |
| 16 | numEdgeTiles | `i32` (v≥4, 1118) | `u32` (= 0 на 115/115; OpenSAGE `MagicValue1`) | = |
| 17 | numEdgeTextureClasses | `i32` + `{first, num, width, name}` × n (v≥4, 1119–1127) | `u32` (= 0 на 115/115; OpenSAGE `MagicValue2`, `throw` если ≠ 0) | = (в BFME1 всегда 0) |
| 18 | blendDesc × (numBlendedTiles−1) | 18 байт → `m_blendedTiles[1..]` (1129–1158): `blendNdx i32, horiz u8, vert u8, rightDiagonal u8, leftDiagonal u8, inverted u8, longDiagonal u8 (v≥3), customBlendEdgeClass i32 (v≥4), flag i32 == FLAG_VAL 0x7ADA0000, иначе throw` | 18 байт: `secondaryTextureTile u32, blendDirection u8×4, flags u8, twoSided u8, magic1 u32, magic2 u32 == 0x7ADA0000` | **= бинарно**: имена OpenSAGE соответствуют ZH один к одному (`secondaryTextureTile`=blendNdx, 4 байта направления=horiz/vert/rDiag/lDiag, `flags`=inverted, `twoSided`=longDiagonal, `magic1`=customBlendEdgeClass) |
| 19 | cliffInfo × (numCliffInfo−1) | 38 байт → `m_cliffInfo[1..]` (v≥5, 1159–1173): `tileIndex i32, u0 v0 u1 v1 u2 v2 u3 v3 f32×8, flip u8, mutant u8` | 38 байт: `u32 + vec2×4 + u8 + u8` | = |
| 20 | конец | `DEBUG_ASSERTCRASH(file.atEndOfChunk())` (1193) | остаток 0 на 115/115 | = |

**Итог: от ZH v8 BFME1 v15 отличается ровно двумя вещами**: (а) поля 3–5 расширены до 32 бит; (б) после поля 6 вставлены 4 битовые маски по `B×h` байт (поля 7–10). Всё остальное бинарно совпадает, включая 18-байтовые описания blend и 38-байтовые cliffInfo.

#### 2.4.2 Изменения по версиям v9…v15

| Версия | Что меняется в раскладке (по OpenSAGE) | Тип и размер | Назначение | Встречается в BFME1 | Как читать в ZH и куда класть |
|---|---|---|---|---|---|
| v8 (ZH max) | исправлена ширина маски v7 → `(w+7)/8` | `u8 × B×h` | клетки, закрашенные как непроходимые (cliff state) | — | как сейчас → `m_cellCliffState` |
| v9 | изменений раскладки нет | — | (внутренняя версия WB BFME; семантика неизвестна) | нет | читать как v8 |
| v10 | + `impassabilityToPlayers` после cliffState | `u8 × B×h` | (предп., по имени поля OpenSAGE) непроходимо для юнитов игроков, но не для скриптовых/ИИ | через v14/v15 | новый буфер `m_impassableToPlayers` (bitmask, тот же stride `m_flipStateWidth`); в E1-25 только хранить; в pathfinder — позже (Геймплей) |
| v11 | + `passageWidths` | `u8 × B×h` | (предп.) разметка узких проходов для pathfinder | через v14/v15 | `m_passageWidths` (bitmask); только хранить |
| v12, v13 | изменений раскладки нет | — | — | нет | читать как v11 |
| v14 | + `taintability` (до v24 включительно); **blends/threeWay/cliff → `u32`** (GetBlendBitSize: 14 ≤ v < 24) | `u8 × B×h`; поля 3–5 по `4×N` | (предп.) клетки, где допустим «taint» (порча земли у Мордора/Изенгарда) | **1 карта: `maps\shellmap1\shellmap1.map`** (52×52, textureCells 4, blends 1, cliffs 1: фоновая карта меню) | `m_taintable` (bitmask); индексы читать в `Int`/`UnsignedInt` массивы |
| v15 | + `extraPassability` | `u8 × B×h` | (предп.) дополнительная проходимость (переопределение маски) | **114 карт** | `m_extraPassable` (bitmask); только хранить |
| v16+ | flammability (`u8 × N`, v16–24), visibility (v17), buildability/impassToAir/tiberium (v24), 16-битные индексы снова (v≥24), shrubbery (v25) | — | BFME2 / C&C3+ | нет | **отвергать с ошибкой** «unsupported BlendTileData vN» (по решению «жёсткая проверка версий») |

Особенность **v14 / shellmap1**: единственная карта v14, без маски `extraPassability`. Значит, порядок масок задаётся строго версией: `cliffState, [v≥10] impassToPlayers, [v≥11] passageWidths, [14≤v<25] taint, [v≥15] extraPassability`, и «съесть все маски до конца» нельзя. Индексы при v14 уже 32-битные. Этой карте нужен фон главного меню (M1a тоже затрагивает: shellmap грузится при старте).

#### 2.4.3 Псевдокод чтения (для ParseBlendTileData, без отдельного класса карты BFME)
```
v = info->version;  if (v > 15) throw/err "unsupported";
idxBits = (v >= 14 && v < 24) ? 32 : 16;
read tiles i16[N];
read blends, (v>=6) threeWay, (v>=5) cliff  as idxBits[N] -> Int arrays;
(v>=7) cliffState  bytes[(v==7 ? (w+1)/8 : B) * h];
(v>=10) impassToPlayers bytes[B*h]; (v>=11) passageWidths bytes[B*h];
(v>=14 && v<25) taint bytes[B*h];    (v>=15) extraPassability bytes[B*h];
дальше без изменений: numBitmapTiles ... cliffInfo (как ZH v8)
```

#### 2.4.4 Особенности данных, которые нужно учесть
- **Индексы tile**: max 24 839 (влезает в `Short`), но на 2 картах (`map mp rhun`, `map sps good rhun`) `tile>>2` больше `numBitmapTiles` (6 209 при 608). В данных остались висячие индексы. `getSourceTile()` проверяет только `< NUM_SOURCE_TILES` (`WorldHeightMap.h:206`). **Нужен clamp к `numBitmapTiles`** (или к тайлу 0) с логом, иначе нулевой указатель или чужая текстура.
- **Индексы blend/cliff**: max 34 142 и 102 442. **Не влезают в `Short`** (≤ 32 767), поэтому `m_blendTileNdxes`, `m_extraBlendTileNdxes`, `m_cliffInfoNdxes` надо сделать `Int` и проверить потребителей (`getBlendTileNdx` и др.).
- **`inverted` (flags)**: 0–3 у 99.99 %. 126 описаний (по 42 на трёх картах Central Ithilien: `map mp`, `map sps evil`, `map sps good`; на кастомных blend-краях) несут значения 8–61, а байты направления там бывают 12–52. Семантика неизвестна (в OpenSAGE это TODO). ZH использует только `& INVERTED_MASK (0x1)` и `& FLIPPED_MASK (0x2)` (`TileData.h:46-47`). **Читать как есть и маскировать при использовании**, редкие значения логировать.
- **`customBlendEdgeClass`**: −1 у 99.99 %, но 129 описаний (по 43 на тех же трёх картах Central Ithilien) несут значения 0–62 при `numEdgeTextureClasses == 0`. ZH должен трактовать индекс ≥ numEdgeTextureClasses как −1 (alpha), иначе выход за `m_edgeTextureClasses`.
- **cliffInfo `flip/mutant`**: все 4 комбинации встречаются (как в ZH).

#### 2.4.5 Лимиты массивов (`WorldHeightMap.h:53-55, 85, 160-178`; `TileData.h:58`)
| Константа ZH | Значение | BFME1 max (115 карт) | Где лопнет | Предложение |
|---|---|---|---|---|
| `NUM_SOURCE_TILES` (`m_sourceTiles[]`, `m_edgeTiles[]`) | 1 024 | **2 036** textureCells | `readTexClass` пишет за край массива (порча памяти) | 4 096 (или динамический массив) |
| `TEXTURE_WIDTH` атласа | 2 048 px; шаг слота 64 + 8 = 72 px (`TILE_PIXEL_EXTENT` + поле `TILE_OFFSET`), 2048/72 = 28 в строке, сетка 28×28 = **784** слота тайлов 64 px (`WorldHeightMap.cpp:1760`, `:1762`; `TileData.h:48`, `:58`; `TerrainTex.h:38` @ `382223fc7`). 32×32 = 1 024 — это сетка без поля 8 px, пакер её не использует | 3 396 ячеек (Trilogy), 5 карт больше 2 036 | атлас террейна (`TerrainTextureClass`) | E1-51: 4096×2048, иначе страницы 2048² (см. абзац ниже) |
| `NUM_BLEND_TILES` (`m_blendedTiles[]`, `m_extraBlendedTiles[]`) | 16 192 | **34 143** | цикл 1129–1158 пишет за край | 65 536, **в куче** (2 × 20 Б × 64K ≈ 2.6 МБ) |
| `NUM_CLIFF_INFO` (`m_cliffInfo[]`) | 32 384 | **102 443** | цикл 1159–1173 | 131 072, в куче (около 5.2 МБ) |
| `NUM_TEXTURE_CLASSES` | 256 (assert < 200) | 115 | — | ок |
| `numBitmapTiles` assert | < 2 048 | 2 036 | впритык | поднять assert до лимита |

Сейчас массивы — члены класса `WorldHeightMap`. При лимитах BFME их надо вынести в кучу (`std::vector`), иначе объект занимает десятки МБ.

**Сделано в E1-25 (blend):** таблицы вынесены в кучу и размеряются по счётчикам карты, индексы клеток стали `Int`. `NUM_SOURCE_TILES` / `NUM_BLEND_TILES` / `NUM_CLIFF_INFO` (4 096 / 65 536 / 131 072) остались размером массивов WorldBuilder. Загрузчик отвергает счётчик, только если записи не влезают в остаток чанка или счётчик выше потолка от мусора (262 144 / 1 048 576 / 1 048 576): The Trilogy (Updated) выше старых трёх чисел. Тесты лежат в `tests/blend`.

**Атлас (E1-51):** слот 64+8, на странице 2048² это 28×28 = 784, не 32×32. Fords of Isen (984 ячейки) на одной такой странице теряет 17 классов. Если `GL_MAX_TEXTURE_SIZE` ≥ 4096 и карта влезает, одна текстура 4096×2048 (56×28). 4096² (3136 слотов) не вмещает Trilogy (3396 ячеек, 175 классов). Иначе страницы 2048²: класс со стороной ≥ 15 занимает страницу целиком (Eastfold — 4, Trilogy — 5). Мипы атласа — `MIP_LEVELS_3`. UV обрыва — локальные единицы класса; `getCliffClassInfo` отдаёт страницу, origin и size, шейдер считает `origin + fract(local) * size`. Тесты в `tests/atlas`.

### 2.5 HeightMapData v4 → v5: кратко

ZH: `WorldHeightMap::ParseHeightMapData`, `WorldHeightMap.cpp:857-910` @ `e29eade`. BFME1: OpenSAGE `HeightMapData.cs` (эталон). Проверено на 115/115.

| Поле | ZH v4 (строки) | BFME1 v5 | Отличие |
|---|---|---|---|
| width, height | `i32, i32` (859–860) | `i32, i32` (max 600×770) | = |
| borderSize | `i32` (v≥3, 861–865) | `i32` | = |
| numBorders + {x,y} | `i32` + `i32×2 × n` → `m_boundaries` (v≥4, 867–874) | то же (corner1 добавлен только в v6, C&C3) | = |
| dataSize | `i32`, `== w*h`, иначе `throw` (880–884) | `i32 == w*h` | = |
| heights | **`u8 × dataSize`** → `UnsignedByte *m_data` (881, 895) | **`u16 × dataSize`** | **8 → 16 бит** |
| масштаб | `MAP_HEIGHT_SCALE = MAP_XY_FACTOR/16 = 0.625` (`Core/GameEngine/Include/Common/MapObject.h:58`) | `0.0390625 = 0.625/16` | ×1/16 |

Под решения Лида:
- Единый тип отсчёта (например, `typedef UnsignedShort HeightSampleType`) для `m_data`, `getHeight()` и `getDisplayHeight()` (`WorldHeightMap.h:138, 244-270`). Масштаб выбирается **по версии чанка**: v≤4 → 0.625, v5 → 0.0390625.
- **Предлагаемая реализация, совместимая с этим решением**: при загрузке v≤4 конвертировать `h16 = h8 << 4`, тогда внутри всегда используется один масштаб 0.0390625. Конвертация **без потерь и бит-в-бит**: `h8 × 0.625 == (h8×16) × 0.0390625` точно в float (оба множителя — двоичные дроби, 5/8 и 5/128; произведения целых < 2²⁴ точны; проверено для всех h8 = 0…255 в float32). Поэтому CRC и реплеи ZH не меняются. Это нужно подтвердить реплей-тестом QA. Хранить масштаб в объекте тоже можно, но `<<4` убирает ветвление из ~90 мест.
- Масштаб используется в ~24 файлах (`rg MAP_HEIGHT_SCALE`): 12 вне WorldBuilder. Основные — `HeightMap.cpp` (33), `BaseHeightMap.cpp` (33), `W3DRoadBuffer.cpp` (8), `WorldHeightMap.cpp` (6), тени, `TerrainLogic.cpp`, `W3DTerrainLogic.cpp`, `W3DTerrainBackground.cpp`. `getDataPtr()` используется в 10 файлах.
- Особенность v1 (уменьшение массива вдвое, 897–905) остаётся для ZH-карт.
- Диапазон: raw до 38 400 (= 1 500 ед.) против ZH 255 (= 159 ед.). Проверить ограничения камеры и `m_mapMinZ/MaxZ`, а также любые `UnsignedByte`-кэши высот (seismic, `getDisplayHeight` в рендер-буферах).

## 3. W3D

Статус: **по исходникам**. `w3d.big` ещё не скопирован, поэтому таблица ниже — ожидание. Проверка запускается одной командой (§5.3), и после неё таблицу надо обновить.

Общее правило ZH: `MeshModelClass::read_chunks` (`GeneralsMD/Code/Libraries/Source/WWVegas/WW3D2/meshmdlio.cpp`) и загрузчики HTree, HAnim и HLod **пропускают неизвестные чанки** (`default: break`). Поэтому неподдержанная фича обычно не роняет игру, а даёт неправильную картинку: пустой материал, белая или magenta-поверхность, отсутствие анимации.

| Чанк / поле | ID | ZH (WW3D2) | BFME1 (ожидание по OpenSAGE `OpenSage.FileFormats.W3d`) | Вердикт | Объём |
|---|---|---|---|---|---|
| MESH + MESH_HEADER3 | 0x0 / 0x1F | v4.2 | v4.2 | ✅ | — |
| VERTICES, VERTEX_NORMALS, TRIANGLES, VERTEX_INFLUENCES, VERTEX_SHADE_INDICES | 0x2/0x3/0x20/0xE/0x22 | есть | есть | ✅ | — |
| VERTEX_MATERIALS / SHADERS / TEXTURES / MATERIAL_PASS (классический материал) | 0x2A/0x29/0x30/0x38 | есть | основной путь BFME1 | ✅ | — |
| Shader `DetailColorFunc` 10–12 (DetailAlt, ScaleAlt, InvScaleAlt) | поле W3dShaderStruct | нет (0..9) | ScaleAlt встречается в `armyantsglow.w3d` (BFME), остальное BFME2/EnB | 🟡 точечно | 0.5 дн (маппинг на ближайший режим) |
| PRELIT_* | 0x23–0x26 | есть | редко | ✅ | — |
| AABTREE | 0x90 | есть | есть | ✅ | — |
| FX_SHADERS / FX_SHADER / INFO / CONSTANT, FXSHADER_IDS | 0x50–0x53, 0x3F | **ID объявлены** (forward-port TheSuperHackers), **но парсера нет** в meshmdlio | по комментарию в `w3d_file.h` и по OpenSAGE это **BFME2**; в BFME1 ожидается 0 вхождений | ✅ если 0, иначе 🔴 | если есть: 1–2 нед (маппинг FX → GLSL-материал) |
| VERTEX_TANGENTS / BINORMALS | 0x60/0x61 | ID есть, не читаются | BFME2 (normal mapping) | ✅ если 0 | — |
| VERTICES_2 / NORMALS_2 (двухкостный скиннинг) | 0xC00/0xC01 | нет | более поздние SAGE-игры (OpenSAGE: dual bone skinning) | ✅ если 0 | иначе 3–5 дн |
| HIERARCHY / PIVOTS / PIVOT_FIXUPS | 0x100–0x103 | v4.1 | v4.1 | ✅ | — |
| ANIMATION (raw) + ANIMATION_CHANNEL | 0x200–0x203 | v4.1, типы каналов 0..6 | тип канала **15** (`UnknownBfme`, вероятно opacity/visibility) в OpenSAGE помечен как BFME | 🟡 | 0.5–1 дн (прочитать и игнорировать, либо применять как альфу) |
| COMPRESSED_ANIMATION: TimeCoded (flavor 0), AdaptiveDelta4 (flavor 1) | 0x280–0x283 | есть (`Core/Libraries/Source/WWVegas/WW3D2/hcanim.cpp`) | основной формат анимаций BFME1 | ✅ | — |
| COMPRESSED_ANIMATION flavor 2 (AdaptiveDelta8) | 0x281 header | **нет**: `WWASSERT(flavor == 0 or 1)`, в релизе не декодируется | OpenSAGE знает; по какой игре — не указано | 🟡 если встретится | 1–2 дн |
| COMPRESSED_ANIMATION_MOTION_CHANNEL | 0x284 | нет | OpenSAGE: «Added in BFME II» | ✅ если 0 | — |
| MORPH_ANIMATION | 0x2C0 | есть | редко | ✅ | — |
| HMODEL / LODMODEL / COLLECTION | 0x300/0x400/0x420 | есть (устаревшее) | вероятно 0 | ✅ | — |
| HLOD (+ header, LOD array, sub-object, aggregate, proxy) | 0x700–0x706 | v1.0 | v1.0 | ✅ | — |
| BOX (OBB коллизий/выделения) | 0x740 | есть | есть | ✅ | — |
| SPHERE / RING | 0x741/0x742 | есть (ZH) | редко | ✅ | — |
| NULL_OBJECT | 0x750 | есть | есть | ✅ | — |
| EMITTER / AGGREGATE / DAZZLE / LIGHT | 0x500/0x600/0x900/0x460 | есть | редко (частицы BFME в INI) | ✅ | — |
| SHDMESH (тень-меш) | 0xB00 | есть | ? | ✅ | — |

Соседние ассеты, связанные с W3D:
- **Текстуры**: BFME1 почти целиком на `.dds` (DXT1/3/5) в `textures.big`. d3d8gles при отсутствии `GL_EXT_texture_compression_s3tc` **распаковывает DXT в RGBA8 на CPU** (`gles_pipeline.cpp: DecodeDXTLevel`). На Mali-G715 и PowerVR S3TC нет, поэтому видеопамять растёт в 4–8 раз. Нужна офлайн-транскодировка в ASTC/ETC2 при импорте данных (см. §4, §7).
- **Шейдеры частиц** в INI BFME1 (`ini.big`, проверено): `Shader = ALPHA 447, ADDITIVE 185, ALPHA_TEST 86, W3D_EMISSIVE 38, W3D_ALPHA 16, W3D_DIFFUSE 2, MULTIPLY 1`. Типы `W3D_EMISSIVE`, `W3D_ALPHA` и `W3D_DIFFUSE` в ZH (ParticleSystem `ShaderType`) отсутствуют: 🟡 добавить в INI-парсер и рендер частиц (1–2 дн).
- **asset.dat**: кэш метаданных ассетов BFME. В ZH его нет; судя по OpenSAGE, для чтения W3D он не нужен. Проверить наличие на полном наборе.

Итог по W3D (предварительно): модели, скелеты, HLod, ящики и сжатые анимации BFME1 **в основном читаются WW3D2 из ZH как есть**. Главные кандидаты на доработку: канал анимации типа 15, флейвор AdaptiveDelta8 (если встретится), режимы DetailColorFunc 10–12 и новые шейдеры частиц. FX-шейдеры, тангенты и VERTICES_2 ожидаются только в BFME2+. Если на реальном w3d.big найдётся 0x50 или 0x60, оценка вырастет на 1–2 недели.

## 4. Шейдеры террейна и рендер

Статус: **по исходникам**. `shaders.big` и `terrain.big` ещё не скопированы, листинг будет в §5.3.

### 4.1 Что есть сейчас
| Слой | ZH на ПК (DX8) | Наш Android-порт (d3d8gles, `Core/Libraries/Source/d3d8gles/`) | BFME1 на ПК (DX9) |
|---|---|---|---|
| API | Direct3D 8 | эмуляция D3D8 поверх GLES 3.0 (`d3d8gles.cpp` + `gles_pipeline.cpp`, генерация GLSL под состояние fixed function); второй путь — DXVK→Vulkan | Direct3D 9 |
| Программируемые шейдеры | DX8-байткод `shaders\*.pso/*.vso`: terrain, terrainnoise, terrainnoise2, fterrain, fterrain0, fterrainnoise, fterrainnoise2, roadnoise2, monochrome, wave (.pso/.vso), Trees (.pso/.vso); грузятся в `W3DShaderManager.cpp` при PS ≥ 1.1 | **не используются**: caps `VertexShaderVersion=0`, `PixelShaderVersion=0`, `MaxTextureBlendStages=2`, `CreatePixelShader/CreateVertexShader` — заглушки | эффекты/шейдеры DX9 в `shaders.big` (формат .fx или DX9-байткод — **проверить**) |
| Террейн | `TerrainShader2Stage` / 8-stage / pixel-shader-варианты: базовый тайл + альфа-блэнд + 3-way + cloud/noise + shroud | всегда 2-stage fixed-function путь | 3-way blend, macro-texture (`EnvironmentData`), cloud-shadow, shroud, по-тайловое освещение с infantry-light |
| Вода/реки | `W3DWater.cpp`: стоячая вода + реки по PolygonTrigger(isRiver), текстуры из `water.ini` | fixed-function вариант | по-полигонные текстуры реки (6 шт.), UV-scroll, additive, alpha (PolygonTriggers v5) + `EnvironmentData` (deep water alpha) |
| Частицы | ADDITIVE / ALPHA / ALPHA_TEST / MULTIPLY | то же | + W3D_EMISSIVE / W3D_ALPHA / W3D_DIFFUSE |

Вывод: **ни DX8 .pso из ZH, ни DX9-шейдеры BFME1 на Android не выполняются**. d3d8gles сознательно работает как fixed-function DX8-ускоритель. Поэтому вопрос не в том, «прочитает ли ZH .fx BFME», а в том, **каким минимальным набором GLSL ES 3.0 воспроизвести картинку BFME1**. Оригинальные `.fx` пригодятся только как спецификация (формулы, порядок слоёв), если это HLSL-исходник. Транслировать их автоматически (HLSL DX9 effect → GLSL ES) нереально: нужны semantics, sampler states и техники/проходы.

### 4.2 Минимальный набор (Э1–Э2), под GLES 3.0 (Mali-G715 как эталон), с Vulkan как опцией
| # | Шейдер/путь | Что воспроизводит | Можно ли через fixed-function d3d8gles | Объём |
|---|---|---|---|---|
| 1 | **Terrain** | тайловый атлас + blend (alpha) + 3-way + cliff UV + macro/noise texture + cloud-shadow (скролл) + shroud + dir-light ×3 | частично (2 стадии: base+blend за 2 прохода, cloud/shroud ещё проходами); macro-texture и 3-way дадут 4–5 проходов на Mali: дорого | 5–8 дн (свой GLSL-путь в обход эмуляции, uniforms из GlobalLighting v7/EnvironmentData) |
| 2 | **Terrain atlas** | 2036 тайлов, blends 34k, cliffs 102k (факт §2) | — | 2–3 дн (2D texture array или несколько атласов, ASTC) |
| 3 | **Roads** | дороги поверх террейна (roadnoise) | да | 1 дн |
| 4 | **Water/River** | per-polygon текстуры, UV-scroll, alpha edge, sparkle, bump (опционально), sky reflection (cube/2D) | частично | 4–6 дн |
| 5 | **Object (W3D mesh)** | diffuse × vertex/dir lighting ×3, house color (team color), alpha test/blend, emissive, DetailColorFunc | **да**: ZH-путь W3D работает через эмуляцию | 0–2 дн (house color, ScaleAlt) |
| 6 | **Skinning** | CPU-скиннинг WW3D2 (как в ZH) | да | 0 (GPU-скиннинг — оптимизация на потом) |
| 7 | **Shadows** | ZH: stencil/volume и decal-тени; BFME1 по-видимому тоже decal + проекционные | частично | 2–4 дн (упрощённые) |
| 8 | **Particles** | + emissive/alpha/diffuse типы | да | 1–2 дн |
| 9 | **Skybox** | SkyboxSettings + схема неба | да | 1–2 дн |
| 10 | **Post/monochrome/fog** | туман, затемнение | да | 1 дн |

Итого рендер: **3–5 недель** одного графического инженера до «узнаваемой картинки BFME1» на GLES 3.0. Объектный рендер в основном уже есть (W3D через d3d8gles). Основной объём — террейн, вода и атлас.

### 4.3 Текстуры
- BFME1 использует DXT1/3/5. На Mali/PowerVR S3TC нет, и d3d8gles декодирует DXT в RGBA8 на CPU: в 4–8 раз больше памяти и дольше загрузка. **Рекомендация**: офлайн-конвертер при первом запуске или при импорте данных, DDS(DXT) → KTX2 (ASTC 6×6/8×8 для террейна, 4×4 для UI). Делается на устройстве из оригинальных файлов пользователя, не в APK.
- Оценка: 3–5 дн (basisu/astc-encoder + кэш + проверка памяти).

## 5. Проверка на реальных данных

### 5.1 Что проверено (2026-10-02, 17:30–18:30 МСК)
| Архив | SHA-256 (из `gamedata/MANIFEST.sha256`) | Что сделано | Результат |
|---|---|---|---|
| `maps.big` (BIG4, 372 записи: 115 .map, 215 .tga, 33 .ini, 9 .wak) | `499d60eb…68d4` | все 115 карт распакованы (RefPack) **в память**; TOC и все чанки верхнего уровня; эвристический обход вложенных чанков; полный разбор HeightMapData, BlendTileData (по OpenSAGE), PolygonTriggers v5, EnvironmentData, SkyboxSettings, WorldInfo; имена условий и действий скриптов | 0 ошибок; разбор BlendTileData даёт остаток 0 байт на 115/115 картах; версии в §2.2 |
| `ini.big` (BIG4, 242 записи) | `f5220a93…8156` | поиск ссылок на `.fx/.pso/.vso` и `Shader =` | ссылок на шейдерные файлы в INI нет; новые типы шейдеров частиц (§3) |

Сводка по 115 картам (вывод `probe.py maps --deep`):
```
BlendTileData {14: 1, 15: 114}   BuildLists {1}        EnvironmentData {3}   GlobalLighting {7} (size 1348)
HeightMapData {5} (2 B/sample)   LibraryMapLists {1}   MPPositionList {0}    NamedCameras {2}
ObjectsList {3} / Object {3}     PlayerScriptsList {5} PolygonTriggers {5}   SidesList {6}
SkyboxSettings {1}               Teams {1}             WaypointsList {1}     WorldInfo {1}
ScriptList 1 / Script 2 / ScriptGroup 3 (вложенные) / OrCondition 1 / Condition 4 / ScriptAction(+False) 2
max: map 600×770, height raw 38400 (=1500.0 ед.), textureCells 2036, blends 34143, cliffs 102443, textures 115
```
Не встретились: MapPreview, CastleTemplates, StandingWaterAreas, RiverAreas, TriggerAreas.

**Сверка с оракулом OpenSAGE** (`tools/oracle/out/maps.big.maps.json`, `out/map mp fords of isen.json`): OpenSAGE разобрал 115/115 карт (225 301 объект). Матрица «чанк → версия → число карт» **полностью совпадает** с `formatprobe`. По карте M1 (`map mp fords of isen`) совпадают: 415×353, border 20, blendElementBits 32, textureCellCount 984, 63 текстуры, 16 700 blend descriptions (= 16 701 − 1), 577 cliff mappings (= 578 − 1), необязательные слои Impassability, ImpassabilityToPlayers, PassageWidths, Taintability, ExtraPassability. Замечание к оракулу: в `heightMap` поля `minZ/maxZ` (3519.375/7278.75) равны raw × 0.625 (масштаб ZH), хотя `verticalScale` = 0.0390625. При масштабе BFME получается 219.96/454.92. Стоит поправить или пояснить в README оракула.

### 5.2 Что ещё не проверено
- `_patch222*.big` (карты, базы, библиотеки и текстуры патча 2.22). Версии чанков в патч-картах могут быть выше: World Builder версии 2.22 мог писать v6+ или CastleTemplates. **Повторить `probe.py maps` на `_patch222maps.big` сразу после копирования.**
- `w3d.big` (копируется, 486 МБ), `shaders.big`, `terrain.big`, `textures.big`: §3 и §4 пока по исходникам.

### 5.3 Как повторить (только чтение, распаковка в память)
```
cd /workspace/bfme-mobile
python3 tools/formatprobe/probe.py exts gamedata/<archive>.big          # расширения и объёмы
python3 tools/formatprobe/probe.py maps gamedata/maps.big --deep --names /tmp/names.tsv
python3 tools/formatprobe/mapdump.py gamedata/maps.big "map mp fords of isen"
python3 tools/formatprobe/allblend.py                                     # лимиты BlendTileData по всем картам
python3 tools/formatprobe/probe.py w3d gamedata/w3d.big                  # гистограмма ID чанков W3D, версии заголовков, флейворы анимаций, имена FX
python3 tools/formatprobe/probe.py list gamedata/shaders.big
```

## 6. Оценка объёма (один инженер, дни работы)

| Блок | Что | Оценка | Уверенность |
|---|---|---|---|
| Map-reader BFME1 (E1-25 + новые чанки) | E1-25 (§6.1): HeightMap v5 вместе с u16-типом по движку, BlendTile v14/15 и лимиты, GlobalLighting v7, PolygonTriggers v5, SidesList v6, Teams/PlayerScriptsList/BuildLists; отдельно ScriptGroup v3 и парсеры EnvironmentData/MPPositionList/LibraryMapLists/NamedCameras/SkyboxSettings (+2–3 дн) | **6.75–9 дн (E1-25) + 2–3 дн** | высокая (раскладка подтверждена на 115/115 картах) |
| u16-высоты по движку | входит в E1-25 (§6.1, п. 3–5); дополнительно ограничения камеры и диапазон ×10 в E1-32 | (в E1-25) | средняя |
| Лимиты террейна | массивы входят в E1-25 (§6.1, п. 7); атлас тайлов 4096 или texture array в E1-32 | **1–2 дн** (атлас) | средняя |
| Функциональность новых чанков | MP-позиции и лобби, библиотеки карт, камеры, скайбокс, environment | **5–8 дн** | средняя |
| W3D | канал анимации 15, AdaptiveDelta8 (если есть), DetailColorFunc 10–12, частицы W3D_* | **2–4 дн**; **+1–2 нед**, если в w3d.big найдутся FX-шейдеры (0x50) | низкая до проверки w3d.big |
| Рендер GLES 3.0 | террейн, вода, реки, атлас, тени, скайбокс (§4.2) | **15–25 дн** | средняя; главный риск |
| Текстуры | DXT → ASTC/ETC2 офлайн на устройстве | **3–5 дн** | высокая |
| ScriptEngine BFME | 126 действий + 41 условие (только то, что встречается в картах) | **15–30 дн** (команда «Геймплей») | низкая |

**Итого форматы + рендер (без ScriptEngine и INI-модулей): примерно 7–10 недель-человек.** На первую загрузку одной MP-карты (террейн без воды, объекты-заглушки) — **около 2 недель**.

### 6.1 E1-25 (разбор `.map` BFME1) под решения Лида

Принятые решения: (1) высоты в `WorldHeightMap` хранятся через единый 16-битный тип отсчёта, 8-битные карты ZH конвертируются при загрузке, масштаб выбирается по версии чанка; (2) отдельного класса карты BFME нет, ветвление по `info->version` в существующих парсерах; (3) неизвестные чанки пропускаются с логом; (4) RefPack распаковывает загрузчик карт через `CompressionManager` ZH. Последнее **уже работает**: `CachedFileInputStream::open` (`GeneralsMD/Code/GameEngine/Source/Common/System/DataChunk.cpp:64-80`) распознаёт `EAR\0` (`Core/Libraries/Source/Compression/CompressionManager.cpp:125`), так что остаётся только тест.

Критерий E1-25 (backlog): все 115 карт разбираются без исключений; для M1 высоты и blend-тайлы побайтно совпадают с выгрузкой оракула.

| # | Работа | Дни |
|---|---|---|
| 1 | DataChunk: лог неизвестного чанка (имя, версия, размер) вместо молчаливого пропуска | 0.25 |
| 2 | RefPack: только тест на 115 картах (код ZH уже есть) | 0.25 |
| 3 | HeightMapData v5: парсер, `HeightSampleType` (u16), конвертация v≤4 `<<4`, масштаб по версии | 0.5 |
| 4 | Перевод потребителей `m_data`/`getHeight`/`getDisplayHeight`/`MAP_HEIGHT_SCALE` (12 файлов движка, около 90 мест; WorldBuilder не трогаем) | 1.5–2 |
| 5 | Регресс ZH: карта ZH грузится, CRC реплея не меняется (с QA) | 0.5 |
| 6 | BlendTileData v14/v15: парсер по 2.4.3, `Int`-массивы индексов blend/3-way/cliff и их потребители, 4 новые маски (хранить), отказ при v>15 | 1–1.5 |
| 7 | Лимиты: `NUM_SOURCE_TILES/BLEND/CLIFF` → vector в куче; clamp висячих tile-индексов (Rhun) и `customBlendEdgeClass` | 0.75–1 |
| 8 | Чанки, без которых парсинг бросает исключение: GlobalLighting v7 (9 источников, infantry пока хранить), PolygonTriggers v5 (параметры рек хранить), SidesList v6 + верхнеуровневые Teams/PlayerScriptsList/BuildLists (регистрация парсеров) | 1–1.5 |
| 9 | Тесты: 115/115 без исключений + побайтная сверка высот и blend-массивов M1 с выгрузкой `tools/oracle --dump` (данные EA вне git) | 1–1.5 |
| | **Итого** | **6.75–9 дн** |

**Оценка Лида 6–9 дней подтверждается** при таких условиях:
- ScriptGroup v3 (вложенные группы, ~0.5 дн) и использование infantry-light и параметров рек в рендере вынесены из E1-25: первое в «Геймплей», второе в E1-32/E1-29.
- Атлас террейна на 2 036 тайлов не входит в E1-25 (это E1-32).
- Если вложенные группы скриптов нужны уже в E1-25 (критерий «без исключений» их не требует, ZH их просто пропустит), получится 7–9.5 дн.

## 7. Риски и рекомендации Лиду движка

### Риски
1. **Тихая порча данных** (высокий). HeightMapData v5 и GlobalLighting v7 код ZH читает **без ошибок**, но получает неверные данные. Без явной проверки версий такие баги будут маскироваться под «странный рельеф» или «странный свет».
2. **Переполнение статических массивов террейна** (высокий). Реальные карты BFME1 превышают `NUM_BLEND_TILES` в 2.1 раза, `NUM_CLIFF_INFO` в 3.2, `NUM_SOURCE_TILES` в 2. Это порча памяти, на Android SIGSEGV в случайном месте.
3. **Рендер террейна и воды на GLES** (высокий, главный риск Э1). Через 2 текстурные стадии d3d8gles BFME-террейн (3-way + macro + cloud + shroud) превращается в 4–6 проходов. На Mali при 1080p+ это под вопросом. Нужен свой GLSL-путь в обход эмуляции fixed-function.
4. **Память текстур** (высокий). DXT на Mali/PowerVR распаковывается на CPU в RGBA8: ×4–8 по памяти, на 8 ГБ устройствах возможен OOM на больших картах.
5. **Скрипты карт** (высокий, вне форматов). 18 % вызовов действий и 13 % условий неизвестны ZH. Кампания и ИИ без этого не заработают. Скирмиш частично зависит от `libraries.big` и LibraryMapLists.
6. **Патч 2.22** (средний). Карты и ассеты патча ещё не проверены, версии чанков в них могут быть выше.
7. **W3D** (средний, до проверки). Если BFME1 всё же использует FX-шейдеры (0x50) или новые флейворы анимаций, без доработок модели станут белыми или magenta, а анимации застынут, причём **без ошибок** (чанки молча пропускаются).

### Рекомендации
1. **Слой чтения карт BFME1 с жёсткой проверкой версий.** Каждый парсер объявляет поддерживаемый диапазон версий. Если версия неизвестна, карта не грузится с понятной ошибкой, без тихого fallback. Реализовать как отдельные `Parse*BFME` рядом с ZH-парсерами (по `info->version`), не ломая загрузку ZH-карт: это нужно для регрессии на эталонных реплеях ZH.
2. **OpenSAGE только как оракул, не как источник кода** (GPL-3.0 + LICENSE-EA). Юнит-тесты разбирают карты нашим C++-парсером и сверяют числа (размеры, количество, SHA-256 чанков) с JSON из `tools/oracle` и `tools/formatprobe`. Карты EA и выгрузки оракула (`--dump`) в git не коммитить: тесты читают `gamedata/` и `cache/` при наличии.
3. **u16-высоты сделать первым коммитом map-reader'а** (решение Лида: единый `HeightSampleType`, масштаб по версии чанка). Предлагаю конвертацию ZH-карт через `<<4` (§2.5): она бит-в-бит сохраняет высоты в float, а значит, и CRC. Подтвердить реплей-тестом QA.
4. **Ранний вертикальный срез рендера**: одна MP-карта BFME1 (например, `map mp fords of isen`), только террейн, атлас + blend + cloud на собственном GLSL ES 3.0 на Pixel 8 Pro. Это проверка главного риска до вложений в остальное.
5. **Офлайн-конвертер ассетов на устройстве** (DDS → KTX2/ASTC, плюс, возможно, предразобранные карты в кэше). Решение оформить ADR: `docs/adr/ADR-000X-asset-pipeline.md`.
6. **ADR по рендеру**: «свой GLSL-путь для террейна, воды и скайбокса поверх d3d8gles» против «расширения d3d8gles до DX9-подобных шейдеров». Я рекомендую первое: меньше объём и полный контроль над Mali.
7. Сразу после копирования `w3d.big`, `shaders.big`, `terrain.big`, `_patch222*.big` прогнать `tools/formatprobe` и обновить §3–§5 (делает «Форматы и данные»).
