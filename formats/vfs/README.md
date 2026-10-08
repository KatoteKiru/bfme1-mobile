# formats/vfs: BIG4 + BIGF и порядок монтирования BFME1 (E1-21)

Владелец: Форматы и данные. Ревью правок апстрима: Лид движка.

## Что здесь

| Файл | Что делает | Зависит от движка |
|---|---|---|
| `include/bfme/vfs/BigArchiveReader.h`, `src/BigArchiveReader.cpp` | независимый от движка разбор заголовка и каталога BIGF/BIG4; эталон для тестов и основа для `tools/datacheck` | нет (C++17, std) |
| `include/bfme/vfs/MountOrder.h`, `src/MountOrder.cpp` | правило порядка: слои, классификация архивов, порядок разрешения | нет |
| `bfme1-mount-order.cfg` | правило по умолчанию в виде данных (тот же текст вкомпилирован, тест `mount_rules_cfg_equals_builtin` следит за совпадением) | — |
| `include/bfme/vfs/BfmeBIGFileSystem.h`, `src/BfmeBIGFileSystem.cpp` | адаптер к движку: наследник `StdBIGFileSystem`, монтирует архивы по `MountPlan` вместо поиска ассетов ZH | да |

Разбор самих архивов (BIGF и BIG4) остаётся в `StdBIGFileSystem::openArchiveFile`. Правка апстрима одна:
признать сигнатуру `BIG4` (см. `docs/UPSTREAM-PATCHES.md`).

## Формат BIG4 (BFME1)

Сверено с OpenSAGE `BigArchive` на `ini.big` и `maps.big` (tools/oracle, E1-20), закреплено тестами
`big4_header_endianness` (синтетика) и `gamedata_*_matches_oracle` (реальные архивы):

| Смещение | Поле | Порядок байт |
|---|---|---|
| 0x00 | `"BIG4"` (ZH: `"BIGF"`) | — |
| 0x04 | размер архива (движок не использует) | **little-endian** |
| 0x08 | число записей | big-endian |
| 0x0C | смещение данных первой записи (= конец каталога; движок не использует) | big-endian |
| 0x10… | записи: смещение u32, размер u32, путь с `\`, завершённый NUL | big-endian |

Раскладка BIG4 совпадает с BIGF байт в байт, поэтому парсеру движка хватает одной проверки сигнатуры.
Архивы патча 2.22 (`_patch222*.big`) на копии Андрея имеют сигнатуру **BIGF**, а базовые (`ini.big`,
`maps.big`) **BIG4**. В одном VFS смешиваются оба формата, тест `gamedata_full_mount_follows_plan`.

## Порядок монтирования

### Как это делает движок ZH (то, что мы переиспользуем)
- `ArchiveFileSystem` хранит для каждого пути `multimap` архивов, а `getArchiveFile(path, instance 0)` берёт **первый**.
- `loadIntoDirectoryTree(archive, overwrite=FALSE)` дописывает архив в **конец** списка (значит, раньше смонтированный побеждает),
  `overwrite=TRUE` ставит его в **начало** (так работают `-mod` и `-modDir` в `loadMods()`).
- `StdBIGFileSystem::init()` → `loadBigFilesFromDirectory(dir, "*.big", FALSE)`: рекурсивный список файлов в `std::set`
  с `strcasecmp` (без учёта регистра, с приведением к нижнему: `_` идёт **раньше** букв), монтирование по порядку.
  Итог: в ZH из одноимённых путей побеждает архив, первый по алфавиту.
- OpenSAGE (`SkudefReader.GetBigFiles` + перезапись в `BigFileSystem`) сортирует `OrdinalIgnoreCase` (приведение к **верхнему**
  регистру, `_` идёт **после** букв, как NTFS/FindFirstFile), и побеждает **последний**.

Для `_patch222*.big` поверх `ini.big` обе модели случайно дают одно и то же, а для `MobileLayer/` и порядка внутри патча уже нет.
Поэтому для BFME1 слои задаются явно, а не через имена файлов, а внутри слоя действует поведение ZH (решение Лида).
Правило одним абзацем: [`docs/formats/bfme1-big-order.md`](../../docs/formats/bfme1-big-order.md), на него же ссылается `tools/datacheck`.

### Правило BFME1 (`bfme1-mount-order.cfg`)
```
layer base      0    .            recursive  first-wins  *.big
layer patch222  100  .            flat       first-wins  _patch222*.big
layer mobile    200  MobileLayer  recursive  first-wins  *.big
```
1. Каждый `.big` под корнем (`BFME_GAMEDATA`, рекурсивно) и в абсолютных каталогах слоёв относится к слою с **наибольшим
   приоритетом**, чьи каталог и маска подошли. Маска проверяется по имени файла без учёта регистра.
2. Слой с большим приоритетом всегда перекрывает меньший, независимо от имён (тест `engine_layer_priority_beats_names`).
3. Внутри слоя архивы сортируются по относительному пути через `_stricmp` (приведение к нижнему регистру, `_` раньше букв), как в ZH,
   и побеждает **первый** (`first-wins`, тест `engine_intra_layer_first_by_stricmp_wins`). `last-wins` остался в формате файла
   только для опытов. Слои: `_patch222` поверх базы, даже если архив базы раньше по имени (тест `engine_layer_patch222_over_base`).
4. `BfmeBIGFileSystem` строит `MountPlan::resolutionOrder` (индекс 0 побеждает) и монтирует в этом порядке с `overwrite=FALSE`.
   `-mod`/`-modDir` из `loadMods()` и дальше монтируются поверх всего.

Настройки окружения: `BFME_GAMEDATA` (корень, обязателен), `BFME_VFS_MOUNT_ORDER` (свой файл правил, например от E1-22),
`BFME_MOBILE_LAYER` (каталог слоя `mobile`, по умолчанию `<корень>/MobileLayer`). В лог пишутся строки `[bfmevfs] mounted #N (слой, приоритет): путь`.

### MobileLayer: регистр и слэши
Проверено тестом `engine_mobile_layer_case_and_slashes` (а также `engine_reads_big4_entries` и `engine_layer_priority_base_patch_mobile`).
- **Пути внутри архивов.** Движок (`StdBIGFileSystem::openArchiveFile`, `ArchiveFile::addFile`, `ArchiveFileSystem::loadIntoDirectoryTree`)
  приводит каждый путь к нижнему регистру и режет его по `\` **и** `/`. Поэтому запись `DATA/INI/GameData.INI` (или `Data\Ini\Object/Gondor.ini`,
  смешанные слэши) в архиве MobileLayer — это тот же файл, что `data\ini\gamedata.ini` в `ini.big`, и она его перекрывает по правилу слоёв.
  Отдельной копии с «другим» именем не появляется: в общем списке файлов путь один, в нижнем регистре и с `\`.
- **Запросы движка** (`openFile`, `doesFileExist`, `getFileInfo`) тоже не зависят от регистра и вида слэшей: `Data/Ini/GameData.ini` и
  `DATA\INI/GAMEDATA.INI` находят одну запись.
- **Один и тот же путь дважды в одном архиве** (отличается только регистром или слэшами): побеждает запись, **последняя** в каталоге архива
  (`ArchiveFile::addFile` перезаписывает). Между разными архивами действует только правило слоёв, а не регистр.
- **Имена каталога слоя и архивов** на диске сравниваются без учёта регистра: `mobilelayer/UI.BIG` попадает в слой `mobile` (правило `MobileLayer`, маска `*.big`).
  Исключение — абсолютный путь из `BFME_MOBILE_LAYER`: его открывает файловая система как есть, то есть на Linux и Android с учётом регистра.

### Что уточняет E1-22
- Место `lang\*.big` относительно корня. Порядок внутри слоя решён (поведение ZH, `docs/formats/bfme1-big-order.md`).
- Точный список архивов 2.22 (`_patch222maps.big`, `_patch222textures.big` ещё не приехали) и нужен ли отдельный слой для других
  патчей (`_patch201*` сейчас попадает в `base`).
- Всё это правится в `bfme1-mount-order.cfg` (+ вкомпилированный текст в `MountOrder.cpp`), код менять не нужно.

## RefPack (EAR\0)
VFS отдаёт **сохранённые** байты, без распаковки. Карты BFME1 (115 из 115 в `maps.big`) сжаты RefPack с заголовком `EAR\0` +
размер распакованных данных (u32 LE). Их распознаёт и распаковывает `CompressionManager` ZH
(`Core/Libraries/Source/Compression`, `getCompressionType` → `COMPRESSION_REFPACK`, `REF_decode`), как в загрузчике карт ZH
(`DataChunk.cpp`). Тесты `gamedata_m1_map_refpack` и `gamedata_all_maps_refpack` проверяют, что результат побайтно (SHA-256) совпадает с
`MapFile.Decompress` OpenSAGE. Свой распаковщик не нужен.

## Подключение в игре (E1-21b)
`SDL3GameEngine::createArchiveFileSystem()` (`GeneralsMD/Code/GameEngineDevice/Source/SDL3GameEngine.cpp`)
смотрит `BfmeBIGFileSystem::isRequestedByEnvironment()`:
- `BFME_GAMEDATA` задана и не пустая — возвращается `BfmeBIGFileSystem`. Во время работы `init()` читает
  **вкомпилированную** копию правил (`BfmeVfsConfig::rules`, по умолчанию `defaultBfme1MountRules()` — тот же
  текст, что в `bfme1-mount-order.cfg`). Файл правил с диска читается только если задан `BFME_VFS_MOUNT_ORDER`
  (`BfmeVfsConfig::fromEnvironment`). Сканируются все `*.big` под корнем и в абсолютных каталогах слоёв
  (`scanArchives`); каждый относится к слою по каталогу и маске, и монтируются все, кому слой нашёлся, а не
  только `ini.big`, `maps.big` и `_patch222*.big`. В stderr пишутся строки
  `[bfmevfs] mounted #N (слой, приоритет): путь`.
- переменная не задана или пустая — возвращается `StdBIGFileSystem`, как в апстриме ZH.

Исходники `formats/vfs/src` входят в `z_gameenginedevice` при `if(NOT IS_VS6_BUILD)` (у VS6 нет C++17 и
`StdBIGFileSystem`). Правки апстрима: `// BFME:` в `SDL3GameEngine.cpp` и в Core
(`ArchiveFileSystem`: в режиме `BfmeBIGFileSystem` community patch ZH не монтируется), `# BFME:` в
`CMakeLists.txt`, строки в `docs/UPSTREAM-PATCHES.md`. Если `TheLocalFileSystem` ещё нет, файл правил
не разобрался или не смонтирован ни один архив, `init()` пишет `[bfmevfs] FATAL` и завершает процесс.
