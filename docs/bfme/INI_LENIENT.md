# Мягкий режим INI (E1-23a, E1-26b)

Данные BFME1 2.22 читаются парсером INI Zero Hour. В режиме BFME (`BFME_GAMEDATA` → `BfmeBIGFileSystem`,
`SDL3GameEngine::createArchiveFileSystem` вызывает `INILenient::setEnabled(TRUE)`) парсер не бросает `INIException`, а:

- неизвестное поле (первое — `ShowProps` в GameData.ini) пропускает вместе с его подблоком/Lua-скриптом; предупреждение
  `[ini-lenient] skip field …` одно на имя поля;
- неизвестный блок верхнего уровня и блок, чей парсер бросил исключение, пропускает до парного `End`;
- значение, которое не разбирается, оставляет значение ZH по умолчанию (`skip value …`);
- выполняет `#define NAME value` и подставляет макросы;
- `Class = Type Grass NEXT Region Edoras` (terrain.ini) → класс ZH по слову после `Type` (иначе `TERRAIN_NONE`), хвост `NEXT Region` отбрасывается.

Вне режима BFME все ветки мёртвые, поведение ZH прежнее. Правила классификации строк — `Common/INILenientRules.h`
(без зависимостей от движка), тесты — `tests/ini` (правила) и `ini.*` в `tests/CMakeLists.txt` (`INI.cpp` в режимах ZH и BFME). Список пропусков в TSV: `BFME_INI_LENIENT_REPORT=<файл>`
(`tools/build-linux.sh --smoke` пишет его в `logs/`). `REAL_TO_INT` не трогается (ADR-0003 §2.7).
