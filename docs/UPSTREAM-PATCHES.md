# Наши правки в коде апстрима (ADR-0001 §3, §5)

Каждая правка в `Core/`, `GeneralsMD/`, `Generals/`, `cmake/` помечена `// BFME:` и описана здесь.

| Файл | Правка | Причина | Задача | Как убрать |
|---|---|---|---|---|
| `Core/GameEngine/Include/Common/MapObject.h` | `MAP_HEIGHT_SCALE` = `MAP_XY_FACTOR/256` (было `/16`); новые `MAP_HEIGHT_ZH_SHIFT` (4), `MAP_HEIGHT_ZH_STEP` (16), `MAP_HEIGHT_SCALE_ZH` (старый масштаб) | высоты BFME1 (`HeightMapData` v5): u16 с шагом 10/256; u8 ZH расширяются `<< 4`, так что мировые высоты ZH побитово прежние | E1-25 | вместе со всей правкой E1-25 (не убирается, пока нужны карты BFME) |
| `GeneralsMD/Code/GameEngine/Include/Common/MapReaderWriterInfo.h` | `K_HEIGHT_MAP_VERSION_5` | версия чанка высот BFME1 | E1-25 | — |
| `Core/GameEngineDevice/Include/W3DDevice/GameClient/WorldHeightMap.h` | `K_MAX_HEIGHT` 255 → `0xFFFF`; `typedef UnsignedShort HeightSampleType` (перенесён из `BaseHeightMap.h`); `m_data`, `getDataPtr`, `getHeight`, `getDisplayHeight`, `setRawHeight` на `HeightSampleType` | хранение u16 | E1-25 | — |
| `Core/GameEngineDevice/Include/W3DDevice/GameClient/BaseHeightMap.h` | typedef удалён, `getClipHeight` возвращает `HeightSampleType` | то же | E1-25 | — |
| `Core/GameEngineDevice/Source/W3DDevice/GameClient/WorldHeightMap.cpp` | `readHeightSamples()` (v5: u16 LE; v1–v4: u8 `<< 4`) в `ParseHeightMapData` и `ParseSizeOnly`; `ParseObjectData` потолок z через `MAP_HEIGHT_SCALE_ZH`; `getUVForTileIndex` пороги `below/aboveLimit` округляются в единицах ZH | загрузка карт BFME1 и ZH; сохранение поведения ZH | E1-25 | — |
| `Core/GameEngineDevice/Source/W3DDevice/GameClient/BaseHeightMap.cpp` | типы сэмплов, `Short` → `Int`; начальный `m_maxHeight` и `amtToFloat` через `MAP_HEIGHT_SCALE_ZH`; `Cast_Ray`: верх бокса `MAX(255*SCALE_ZH, m_maxHeight)`, паддинг клеток `±MAP_HEIGHT_ZH_STEP` | u16 не влезает в `UnsignedByte`/`Short`; мировые константы и бокс луча прежние на ZH | E1-25 | — |
| `Core/GameEngineDevice/Source/W3DDevice/GameClient/HeightMap.cpp` | `updateCenter`: `Short` → `Int`, `intersectionZ = minHt/MAP_HEIGHT_ZH_STEP`; тип `data` в extra blend | то же; значение апстрима (raw u8 как мировая z) сохранено | E1-25 | — |
| `Core/GameEngineDevice/Source/W3DDevice/GameClient/W3DTerrainVisual.cpp` | сейсмика в единицах ZH, clamp `K_MAX_HEIGHT`; `xfer` версия 4 (u16), но при сохранении v3, если все высоты в раскладке ZH (`heightsFitZHLayout`); v2/v3 `>>4`/`<<4` | сейвы ZH побайтно прежние, сейвы BFME сохраняют u16 | E1-25 | — |
| `GeneralsMD/Code/GameEngine/Source/GameLogic/Map/TerrainLogic.cpp` | `flattenTerrain` (2 места) и `createCraterInTerrain` считают в единицах ZH, затем `* MAP_HEIGHT_ZH_STEP` | целочисленное округление апстрима побитово на ZH | E1-25 | — |
| `GeneralsMD/Code/GameEngineDevice/Source/W3DDevice/GameClient/W3DRoadBuffer.cpp` | `FLOAT_AMOUNT`, `MAX_ERROR` через `MAP_HEIGHT_SCALE_ZH` (2 функции) | мировые константы прежние | E1-25 | — |
| `GeneralsMD/Code/GameEngineDevice/Source/W3DDevice/GameClient/Shadow/W3DVolumetricShadow.cpp` | тип `data`; debug-assert `(Real)K_MAX_HEIGHT*MAP_HEIGHT_SCALE` | u16 | E1-25 | — |
| `GeneralsMD/Code/GameEngineDevice/Source/W3DDevice/GameLogic/W3DTerrainLogic.cpp` | `Short` → `Int` | u16 не влезает в `Short` | E1-25 | — |
| `GeneralsMD/Code/GameEngine/Source/GameLogic/Object/Update/TensileFormationUpdate.cpp` | локальный `#define MAP_HEIGHT_SCALE` приведён к `/256` (в файле не используется) | согласованность с `MapObject.h` | E1-25 | — |

E1-25: полный аудит (94 места) в `docs/formats/e1-25-heights-audit.md`. Не портировано: WorldBuilder
(`GeneralsMD/Code/Tools/WorldBuilder`, пишет `HeightMapData` v4 u8, после правки не собирается) и vanilla Generals (`Generals/`).
В пресетах Linux и Android обе сборки выключены.
