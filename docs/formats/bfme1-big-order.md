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

Рекурсивный обход корня не заходит в `Command and Conquer Generals Zero Hour Data` (каталог userdata ZH)
и в `GeneralsOnlineGameData` на любой глубине, без учёта регистра. Иначе
`500_900_CommunityPatch_CoreINI.big` из этих каталогов попадает в слой base и перекрывает `ini.big`
для путей, которых нет в `_patch222` (`5` раньше `i` при `_stricmp`, внутри слоя побеждает первый).
Файл `gx_no_community_patch.txt` обход не читает: это выключатель лаунчера ZH. `-mod` и `-modDir`
монтирует `loadMods()` отдельно, исключение каталогов их не отключает.

## Факты (проверено на копии игры)
- Сигнатуры: `ini.big`, `maps.big` — `BIG4`; `_patch222.big`, `_patch222maps.big` и остальные `_patch222*.big` — `BIGF`.
  Раскладка одинаковая (`formats/vfs/README.md`).
- `_patch222.big` перекрывает 241 из 242 файлов `ini.big`, не перекрыт только `data\ini\neutralunits.ini`.
- Через VFS `data\ini\gamedata.ini` берётся из `_patch222.big`: `GoodCommandPointLimit = 600`, `EvilCommandPointLimit = 1400`
  (в базе 300 и 600).
- Для 2.22 правило даёт порядок `_patch222.big`, `_patch222bases.big`, `_patch222libraries.big`, `_patch222maps.big`,
  `_patch222textures.big`, затем архивы базы (первый побеждает). Тот же порядок у `tools/datacheck` там, где имя патча
  само по себе раньше букв; слой `mobile` от алфавита не зависит.
- Ключ сортировки внутри слоя — относительный путь с разделителем `/` (`normalizeSlashes`, затем `_stricmp`). ZH на Windows
  сравнивает `\`. После общего префикса `/` (0x2F) и `\` (0x5C) стоят по разные стороны от `.` (0x2E) и от цифры (0x30),
  поэтому порядок может разойтись. На 53 путях `.big` из `docs/bfme1-manifest.json` обе сортировки дают одну последовательность.
- Имена архивов, которые отличаются только регистром, монтируются один раз: `FilenameList` ZH — это `std::set`. Если на диске
  лежат оба, остаётся путь, меньший побайтно (`INI.BIG` раньше `ini.big`, потому что `I` < `i`).
- OpenSAGE (`SkudefReader.GetBigFiles`, `BigFileSystem`) сортирует иначе (`OrdinalIgnoreCase`, приведение к верхнему регистру,
  `_` после букв) и отдаёт последний. На этой копии внутри слоя между разными архивами нет общих путей, поэтому «первый» и
  «последний» выбирают один и тот же файл. За основу взято поведение ZH.

## Факты о пересечениях (полная копия 2.22, манифест `patch-2.22v5.5.4`)
- Слой `patch222` (приоритет 100, плоский корень, маска `_patch222*.big`): пять архивов —
  `_patch222.big`, `_patch222bases.big`, `_patch222libraries.big`, `_patch222maps.big`, `_patch222textures.big`.
  `_patch222maps.big` и `_patch222textures.big` в копии есть.
- Слой `base` (приоритет 0, рекурсивно `*.big`): 48 архивов. 33 лежат в `apt/` (по алфавиту), ещё 15 в корне:
  `ambientstreams.big`, `audio.big`, `bases.big`, `data.big`, `ini.big`, `lang/english.big`, `lang/englishaudio.big`,
  `libraries.big`, `maps.big`, `music.big`, `shaders.big`, `terrain.big`, `textures.big`, `w3d.big`, `window.big`.
- Между архивами одного слоя общих путей нет (0 пересечений). Три повтора лежат внутри одного `data.big` и байт в байт
  совпадают: `albertusmt.otf`, `omnialtstd.ttf`, `sachwt__.ttf`. Это не пересечение архивов.
- Между слоями общих путей 1221, все выигрывает `patch222`. Тест `gamedata_full_mount_follows_plan` печатает эти два
  числа раздельно. Раньше 3 повтора внутри `data.big` и 1221 пересечение складывались в одну строку «1224».

## Что осталось E1-22
1. Место `lang\*.big` и нужен ли отдельный слой для других патчей (`_patch201*` сейчас попадает в `base`).
2. Правки — в `formats/vfs/bfme1-mount-order.cfg` и во вкомпилированном тексте `MountOrder.cpp` (тест
   `mount_rules_cfg_equals_builtin` следит, чтобы они совпадали).
