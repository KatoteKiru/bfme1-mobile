# Порядок .big в BFME1 и 2.22

Статус: правило принято Лидом 02.10.2026 (поведение ZH). На этот текст ссылаются C++ (`formats/vfs`: `MountOrder.h`,
`bfme1-mount-order.cfg`) и `tools/datacheck`. Остальное уточняет E1-22.

## Правило
Архивы `.big` делятся на слои с приоритетом: база BFME1 (0), патч 2.22 `_patch222*.big` в корне игры (100), `MobileLayer/` (200).
Слой с большим приоритетом всегда перекрывает слой с меньшим, независимо от имён архивов. Внутри одного слоя архивы
сортируются по относительному пути через `_stricmp` (без учёта регистра, с приведением к нижнему регистру, поэтому `_` идёт
раньше букв), и из одинаковых путей побеждает архив, **первый** в этом порядке. Так ведёт себя движок ZH: `FilenameList` —
это `std::set` с `rts::less_than_nocase` (`AsciiString::compareNoCase`, то есть `_stricmp`;
`Core/GameEngine/Include/Common/FileSystem.h:66`, `Core/GameEngine/Include/Common/STLTypedefs.h:238–244`),
`StdBIGFileSystem::loadBigFilesFromDirectory` монтирует архивы в этом порядке с `overwrite=FALSE`
(`Core/GameEngineDevice/Source/StdDevice/Common/StdBIGFileSystem.cpp:656–695`), а
`ArchiveFileSystem::loadIntoDirectoryTree` без `overwrite` дописывает архив в конец списка владельцев пути, так что
файл отдаёт первый смонтированный (`Core/GameEngine/Source/Common/System/ArchiveFileSystem.cpp:185–196`; поиск берёт
первый элемент диапазона, `getArchiveFile`, там же `:424–436`). Регистр и вид
слэшей путей внутри архивов на победителя не влияют.

## Факты (проверено на копии игры)
- Сигнатуры: `ini.big`, `maps.big` — `BIG4`; `_patch222.big`, `_patch222maps.big` и остальные `_patch222*.big` — `BIGF`.
  Раскладка одинаковая (`formats/vfs/README.md`).
- `_patch222.big` перекрывает 241 из 242 файлов `ini.big`, не перекрыт только `data\ini\neutralunits.ini`.
- Через VFS `data\ini\gamedata.ini` берётся из `_patch222.big`: `GoodCommandPointLimit = 600`, `EvilCommandPointLimit = 1400`
  (в базе 300 и 600).
- Для 2.22 правило даёт порядок `_patch222.big`, `_patch222maps.big`, `ini.big`, `maps.big` (первый побеждает). Тот же порядок
  у `tools/datacheck` (`reference/2.22/manifest.json`, `archives_mount_order`).
- OpenSAGE (`SkudefReader.GetBigFiles`, `BigFileSystem`) сортирует иначе (`OrdinalIgnoreCase`, приведение к верхнему регистру,
  `_` после букв) и отдаёт последний. Для `_patch222*.big` поверх `ini.big` обе модели дают одно и то же, внутри слоя — нет.
  За основу взято поведение ZH.

## Что осталось E1-22
1. Пересечения путей внутри патча и с базой на полном наборе архивов 2.22 (тест `gamedata_full_mount_follows_plan` печатает
   число перекрытых записей).
2. Место `lang\*.big` и нужен ли отдельный слой для других патчей (`_patch201*` сейчас попадает в `base`).
3. Правки — в `formats/vfs/bfme1-mount-order.cfg` и во вкомпилированном тексте `MountOrder.cpp` (тест
   `mount_rules_cfg_equals_builtin` следит, чтобы они совпадали).
