# ADR-0003: детерминизм логики (плавающая точка, математика, RNG, порядок)

- **Статус:** Предложено, 02.10.2026. Черновик Геймплея, приведён к решениям Лида движка от 02.10.2026 и к уточнению интерфейса `fp::`, согласованному с Android-платформой в тот же день. Утверждают Лид движка и QA.
- **Авторы:** Геймплей (черновик), Лид движка (решения), Android-платформа (интерфейс `fp::`).
- **Владельцы:** Лид движка (правила, ревью хуков в апстримных файлах); Геймплей (своя GameMath, E1-49, 5–7 дней; перевод логики; заглушки в `wwmath.h`; вызовы `fp::` в `GameLogic`; `Stats` в оверлее; флаг `-gxFpCheck`; хелперы `int32_t`; запрет `long double` / `lroundf` / `long` в логике и сериализации); Android-платформа (проверка, что флаги из `cmake/compilers.cmake` попали в `compile_commands.json`; запрет `-ffast-math`/`-Ofast` с проверкой в CI; реализации `fp::` по архитектурам; `fp::Guard` на `newDrawable` и создании ресурсов рендера из логики; хаос-режим `-gxFpChaos`); QA (E1-45 после E1-49, синтетический сценарий, разбор `StealthUpdate` и `SpectreGunshipUpdate` вместе с Геймплеем).
- **Проверка:** E1-45 идёт **после** E1-49 (`docs/E1-BACKLOG.md`). До готовности GameMath — контрольный прогон с флагами компилятора при `SAGE_USE_DETERMINISTIC_MATH=OFF`. Сравнение CRC архитектур — headless Linux x86_64 против arm64, на устройстве прогон на Pixel 8 Pro. Реплея ZH нет: планируем без покупки (E1-BACKLOG §6 п. 6). Затем реплей-тесты QA в CI с Э2 (`PLAN.md` §3 «QA: детерминизм», §5 риск «Детерминизм и рассинхроны»; `docs/TEST-PLAN.md`).
- **Связано:** NET-DESIGN.md §1.5 (CRC в ZH), §2 (детерминизм в MYSOREZ по DEV_BLOG), §3.2 (CRC N=30, отчёт о рассинхроне); ADR-0001 §3 п. 9 (своя GameMath на libm из musl), §5 (cherry-pick из TSH через QA); ADR-0002 §2 п. 1 (только `arm64-v8a`, эмулятор x86_64 не делаем; GameMath сверяем под `qemu-aarch64`, CRC — на Pixel 8 Pro после E1-49), §7 (логика 30 Гц), §10 (ярус C); GAMEPLAY-GAP.md §4. Аудит FP-режима (`docs/fpcr-audit.md`, в этой ветке файла нет, он приходит с PR E1-03) — источник рекомендаций R1–R6; интерфейс §8 того файла в ветке отсутствует, поэтому он записан здесь, в §2.3.
- Пути ниже относительно корня форка на коммите `f79e624` этой ветки `bfme`, если не сказано иначе. TSH сверен `gh api` 02.10.2026: `838f9d0fd1c658ec795d3903a4f3f7e265b6e6d5` и `28170001f408d8827c5200ef2af0f814926cfe21`. В `Core/` обоих коммитов каталоги `GameEngine`, `GameEngineDevice`, `Libraries`, `Tools`; каталога `Core/GameMath` нет.

---

## 1. Контекст

1. Lockstep ZH обменивается только командами. Состояние сверяется по CRC (`GameLogic::getCRC(CRC_RECALC)`, `GeneralsMD/Code/GameEngine/Source/GameLogic/System/GameLogic.cpp:4709`), в нашей сети каждые N=30 кадров в release и N=1 в QA-сборке (NET-DESIGN §3.2). Любое расхождение логики хотя бы в одном бите float даёт рассинхрон.
2. Играют одной сборкой arm64 (ADR-0002 §2 п. 1). Кросс-архитектурную проверку делаем headless на Linux x86_64 против arm64; на устройстве запускаем на Pixel 8 Pro (E1-45). Побитовую сверку GameMath на arm64 до появления телефона в CI гоняем под `qemu-aarch64` (E1-49, ADR-0002 §2 п. 1 и §10, ярус C). Эмулятор x86_64 не используем.
3. Вся логика ZH на `Real` = float. Подсчёт `\bReal\b` на `f79e624`: `PhysicsUpdate.cpp` — 145, `Core/GameEngine/Source/GameLogic/AI/AIPathfind.cpp` — 145, `Locomotor.cpp` — 252, `Weapon.cpp` — 89. Переход на fixed-point не рассматриваем (XL, GAMEPLAY-GAP §4.1).
4. Что уже есть в форке, сверено с деревом `f79e624`:
   - `cmake/compilers.cmake`, ветка не-MSVC и не-VS6: строка 58 — `-ffp-contract=off`; строка 69 — `-fno-strict-aliasing -fwrapv -fno-delete-null-pointer-checks`. В `CMakePresets.json` этих флагов нет. В TSH (`cmake/compilers.cmake` на указанных коммитах) состав флагов этим проходом не сравнивался: достаточно того, что в нашем дереве они уже стоят и в пресеты их не копируем.
   - `cmake/gamemath.cmake`: опция `SAGE_USE_DETERMINISTIC_MATH` по умолчанию **OFF** (строка 23). При ON — `FetchContent` репозитория TSH, тег `838f9d0fd1c658ec795d3903a4f3f7e265b6e6d5`, `SOURCE_SUBDIR "Core/GameMath"`, и дефайн `USE_DETERMINISTIC_MATH`. Каталога `Core/GameMath` на этом теге нет, нет его и на `28170001`. Комментарии файла по-прежнему называют источник fdlibm; решение Лида — libm из musl (MIT), см. §2.2.
   - В `Core/Libraries/Source/WWVegas/WWMath/wwmath.h` ветки `#ifdef USE_DETERMINISTIC_MATH` — заглушки `// TODO: return GameMath::…` с откатом на CRT (`Atan`/`Atan2`/`Sin`/`Cos`/`Tan`/`Acos`/`Asin`, строки 153–222, и `SqrtOrigin`, строка 230). Готовой GameMath в дереве нет. Заглушку `Sqrt` на GameMath не переводим: `sqrt` остаётся нативным (§2.2).
   - `Trig.cpp` на не-MSVC уже считает `sin`/`cos`/`tan`/`acos`/`asin` в double и сужает к `Real` (`GeneralsMD/Code/GameEngine/Source/Common/System/Trig.cpp:80–122`). `sinf`/`cosf`/`tanf`/`acosf`/`asinf` остаются только в ветке `_MSC_VER && _M_IX86`.
   - По DEV_BLOG форка (NET-DESIGN §2): `sinf` расходится на 1,236% углов, `lroundf` 32/64 бит (UCRT против bionic), NaN в `REAL_TO_INT_FLOOR`, при `-ffp-contract=off` 12 трансцендентных функций совпали бит в бит на x86_64/aarch64.

## 2. Решение

### 2.1 Флаги компилятора (решение Лида п. 3)

- `-ffp-contract=off`, `-fno-strict-aliasing`, `-fwrapv` и `-fno-delete-null-pointer-checks` **уже стоят** в `cmake/compilers.cmake` (строки 58 и 69) для компиляторов не-MSVC. В `CMakePresets.json` их не добавляем.
- Android-платформа только проверяет, что эти флаги попадают в `compile_commands.json` Android-сборки (E1-10). При слиянии с MYSOREZ/TSH строки 58 и 69 нельзя потерять.
- Запрещены для всех целей: `-ffast-math`, `-Ofast`, `-funsafe-math-optimizations`, `-ffinite-math-only`, `-fassociative-math`, `-freciprocal-math`, `-march=native`, `-mfpmath=387`. На x86_64 — только SSE2 по умолчанию, без `-mfma`. Запрет `-ffast-math`/`-Ofast` с проверкой в CI — задача Android-платформы (R4 аудита FPCR): grep по `compile_commands.json` и логам meson, включая порты vcpkg и кросс-файл DXVK. На Linux x86_64 дополнительно проверять, что ни одна наша `.so` не тянет `crtfastmath` (`ldmxcsr` в `.init_array`).
- Почему `-ffp-contract=off`: clang по умолчанию `-ffp-contract=on`, и на AArch64 (FMA есть всегда) склеивает `a*b+c` в `fmadd`, а на x86_64 без `-mfma` — нет. Последний бит различается, и CRC расходится.
- `SAGE_USE_DETERMINISTIC_MATH` эта проверка не включает: опция остаётся OFF до E1-49 (§2.2).

### 2.2 Детерминированная математика (решение Лида п. 2)

- `SAGE_USE_DETERMINISTIC_MATH` остаётся **OFF** до готовности своей GameMath (E1-49), а не ON с Э1. Включение сейчас запускает `FetchContent` каталога, которого в TSH нет (§1 п. 4).
- GameMath пишем сами, на **libm из musl (MIT)**. MIT совместима с GPLv3. musl выросла из fdlibm / FreeBSD msun; в тексте решения источник называется только «libm из musl (MIT)». TSH не ждём.
- Состав — только то, что вызывает логика, из набора `sinf`/`cosf`/`tanf`, `asinf`/`acosf`, `atanf`/`atan2f`, `expf`/`logf`/`powf`, `fmodf`, плюс double-версии, если они используются. В этом дереве логика на не-MSVC вызывает double `sin`/`cos`/`tan`/`asin`/`acos` (`Trig.cpp`), double `atan`/`atan2` (`wwmath.h`, прямой `atan2` в `PhysicsUpdate.cpp:793`) и double `pow` (`POWTruckAIUpdate.cpp:483`, `PartitionManager.cpp:5881`). Эти double-версии в GameMath входят. Вызовов `exp`/`log`/`fmod` и `expf`/`logf`/`fmodf` в `GeneralsMD/Code/GameEngine/Source/GameLogic` нет, поэтому в первый набор их не кладём. `powf` в `GameLogic` не найден (отдельный вызов есть в `WWMath/vp.cpp:538`); float-`powf` добавляется, только если перевод логики дойдёт до вызывающего кода. `sqrt`/`floor`/`ceil`/`trunc` остаются нативными: по IEEE 754 они точные.
- Расположение: `Core/Libraries/Source/GameMath/`. Файлы musl кладём с исходными заголовками, коммит musl записываем в README каталога, текст лицензии — в `LICENSES/musl-MIT.txt`. Каталог лежит в дереве апстрима: это исключение из ADR-0001 §3 п. 3, правка идёт в `docs/UPSTREAM-PATCHES.md` с пометкой `// BFME:` (ADR-0001 §3 п. 9).
- `cmake/gamemath.cmake` переводим с `FetchContent` на `add_subdirectory`. Имя опции `SAGE_USE_DETERMINISTIC_MATH` и дефайн `USE_DETERMINISTIC_MATH` сохраняем ради возможного слияния с TSH #2670.
- Заглушки `TODO` в `wwmath.h` заменяет Геймплей. Исключение: `SqrtOrigin` (строка 230) остаётся на `sqrt`. Общий заголовок не подменяем макросом на весь файл: логике — `GameMath`, клиенту — прежние `WWMath::*`.
- GameMath применяется только в логике (GameLogic и всё, что она вызывает). Рендер и клиент (`GameClient`, `W3DDevice`, частицы, звук) остаются на libm.
- Точки перевода, сверенные со строками дерева: `Trig.cpp` (`Sin`/`Cos`/`Tan`/`ACos`/`ASin`); обёртки `WWMath::Sin`/`Cos`/`Atan`/`Atan2`/`Acos`/`Asin` и `SinTrig`/`CosTrig`/`TanTrig` в `wwmath.h`; `PhysicsUpdate.cpp:793` (`atan2(xvec.Z, xy)`). Остальные прямые вызовы libm в `GameLogic/` и в общих заголовках, которые она включает, находит CI-греп (§2.5).
- Таблицы `_FastSinTable` и прочие (заполняются в `WWMath::Init()`) в логике не используем, либо заполняем через GameMath.
- Обязательный тест E1-49: побитовое совпадение таблицы входов (не меньше 10^6 случайных плюс граничные случаи) на Linux x86_64 и arm64. arm64 до появления телефона в CI гоняем под `qemu-aarch64`. Владелец — Геймплей, оценка 5–7 дней. `SAGE_USE_DETERMINISTIC_MATH=ON` во всех пресетах — только когда этот тест зелёный.

### 2.3 Плавающая точка в потоке логики (решение Лида п. 7 и уточнение платформы)

Регистры управления FP у каждого потока свои. На Android вся игра идёт в одном потоке `SDLThread` (аудит FPCR; исходников SDL в дереве нет, SDL3 приходит через FetchContent, имя `SDLThread` в репозитории не встречается). Вызовы `fp::` — только из этого потока. В debug — assert по id потока. Счётчики не атомарные.

`docs/fpcr-audit.md` в ветке нет (придёт с PR E1-03). Интерфейс ниже — согласованное с Android-платформой описание, которое иначе лежало бы в §8 того файла.

**Норма (R3 аудита), после сброса:**

- arm64: `FPCR == 0`;
- x86_64: `(MXCSR & 0xFFC0) == 0x1F80` и `(x87 CW & 0x0F3F) == 0x003F`.

`State` хранит только управляющие регистры (arm64 — FPCR; x86_64 — MXCSR и x87 CW), без FPSR. Чтение — `asm volatile` с клоббером `"memory"`. Что `fesetenv(FE_DFL_ENV)` на bionic arm64 пишет FPCR=0, в этом дереве не проверено: bionic здесь не лежит, это вывод аудита. В коде проверено другое: не-Windows ветка `setFPMode()` (`GameLogic.cpp:239–241`) вызывает `fesetenv(FE_DFL_ENV)`, затем `feclearexcept(FE_ALL_EXCEPT)` и `fesetround(FE_TONEAREST)`; на x86 следом правит x87 CW (`:243–248`, маска `~0x0F00`) и MXCSR (`:250–254`).

**Почему проверка строится на `setFPMode`, а не рядом.** `fp::checkAndReset` нельзя вызывать после `setFPMode()`: после сброса он всегда видит норму. `fp::reset()` — единственная реализация сброса. `setFPMode()` остаётся обёрткой над `fp::reset()`, чтобы нынешние вызовы сами начали сбрасывать через `fp::`.

Сейчас `setFPMode()` в `GameLogic::update` — не первая строка функции. Функция начинается на строке 4110; до вызова на строке 4126 стоят таймер, профайлер, ветка `GENERALS_ONLINE_HIGH_FPS_SERVER`, `LatchRestore` и при `DO_UNIT_TIMINGS` — `unitTimings()`. Заменяем именно этот вызов (первый, который трогает FP-режим) на `fp::checkAndReset("tick-start")`. Он читает состояние, при отклонении пишет в лог, увеличивает счётчик и вызывает `reset()`.

Другие вызовы `setFPMode()`, которые остаются обёрткой над `reset()`: `GameLogic::init` (`:433`), сброс в `init` (`:488`), загрузка карты (`:1440`), кадр затухания при загрузке (`:2380`), `INI::load` (`Core/GameEngine/Source/Common/INI/INI.cpp:416`), `W3DModelDraw` (`:507`, `:603`, `:727`, `:865`). Тем же путём идут `GameClient.cpp:682`, `ScoreScreen.cpp:900`, `W3DView.cpp:654`, `MapUtil.cpp:441`, `LoadScreen.cpp` (`:191`, `:682`, `:1187`).

**Конец тика и CRC.** `fp::assertExpected("tick-end")` — перед выходом из `GameLogic::update` (функция заканчивается на строке 4467). В `getCRC` сейчас первая строка тела после проверки режима — снова `setFPMode()` (`:4714`); её заменяем на `fp::assertExpected("getCRC")`, а не ставим проверку следом: сброс перед проверкой спрятал бы отклонение. Фатален только `assertExpected`. В CI и debug режим Fatal, в release — Log (§ ниже, `setMode`).

**Интерфейс `fp::`.** Заголовок `Core/Libraries/Source/GameMath/include/fp.h`, пространство имён `fp`. Реализации: `Core/Libraries/Source/GameMath/fp/fp_arm64.cpp`, `fp_x86_64.cpp`, `fp_msvc.cpp` (MSVC — через `_controlfp_s`).

- `State`, `read()`, `isExpected()`, `reset()`, `checkAndReset(where)`, `assertExpected(where)`;
- RAII `Guard`: восстанавливает состояние, увеличивает `guardRestores` и пишет в лог, не падает;
- `Stats { tickStartDeviations, tickEndFailures, guardRestores, lastWhere, lastValue, lastFrame }`, `stats()`, `resetStats()`;
- `setMode(Off/Log/Fatal)` по флагу `-gxFpCheck=off|log|fatal`: в CI и debug — Fatal, в release — Log.

**Кто что делает.**

- Геймплей: вызовы в `GameLogic` (`tick-start`, `tick-end`, `getCRC`), `Stats` в оверлее, флаг `-gxFpCheck`. Это R1.
- Android-платформа: реализации по архитектурам; `fp::Guard` вокруг `ThingFactory::newDrawable` (`GeneralsMD/Code/GameEngine/Source/Common/Thing/ThingFactory.cpp:355`) и создания ресурсов рендера из логики (R2); хаос-режим `-gxFpChaos` для теста T4 аудита. Страж не фатален. Обёртку вокруг каждого вызова GL/AL не делаем.

### 2.4 Целые типы, float→int и `long double` (решение Лида п. 8 и R6)

- В логике и сериализации запрещены `lroundf`, `lrintf`, `lround`, `lrint`, типы `long` / `unsigned long` и `long double`. `long` — 32 бита на Windows и 64 бита на arm64/x86_64 Linux и Android. `long double` на arm64 Android — программный fp128 (compiler-rt), на x86 — x87; это отдельный источник расхождения (R6). Запрет входит в E1-49 и проверяется в CI.
- Вместо `lroundf` и приведений к `long` — хелперы в GameMath с `int32_t`: `GameMath::roundToInt32`, `floorToInt32`, `ceilToInt32`, `truncToInt32`. Поведение на краях, которое фиксируем в коде и тестах: насыщение до `INT32_MIN`/`INT32_MAX`, NaN → 0.
- Сейчас `fast_float2long_round` лежит в `Core/Libraries/Include/Lib/BaseType.h:180` и возвращает `long`. Ветка `_MSC_VER && _M_IX86` вызывает `lroundf` (`:190`). Не-Windows ветка (`:192–208`) уже не вызывает `lroundf`: она берёт `roundf` и при выходе за диапазон возвращает 0, а не насыщение. `REAL_TO_INT_CEIL`/`FLOOR` определены там же (`:292–296`). Пример в логике: `Core/GameEngine/Include/GameLogic/AIPathfind.h:1015–1016`. Перевод `REAL_TO_INT_*` на хелперы — правка апстрима с `// BFME:`, ревью Лида. Файла `Core/Libraries/Include/Lib/BaseFunctions.h` в дереве нет.
- Тип `long` в `GeneralsMD/Code/GameEngine/Source/GameLogic` на `f79e624` — две локальные переменные `PartitionManager.cpp:95–96`. Поиск слова `long` даёт 40 строк в `*.cpp` этого каталога, почти все — английское слово в комментариях. CI-шаблон не должен считать комментарий нарушением. Ищем тип и функции: `\b(lroundf|lrintf|lround|lrint)\b`, объявления `long`/`unsigned long`, `\blong\s+double\b`, по каталогам `GameLogic/`, `engine/bfme/` и включаемым ими общим заголовкам, с явным списком исключений.

### 2.5 Общие правила для логики (из GAMEPLAY-GAP §4)

1. **RNG только `GameLogicRandomValue*`.** Генераторы ZH (`Core/GameEngine/Source/Common/RandomValue.cpp`): логический `theGameLogicSeed` (`GameLogicRandomValue`/`…Real`, макросы в `Core/GameEngine/Include/GameLogic/LogicRandomValue.h`), клиентский `theGameClientSeed` (`GameClientRandomValue`, `Core/GameEngine/Include/GameClient/ClientRandomValue.h`), звуковой `theGameAudioSeed`. Сид матча общий: `InitRandom(seed)` (`SkirmishGameOptionsMenu.cpp:444`), реплей `GeneralsMD/Code/GameEngine/Source/Common/Recorder.cpp:1586`, сеть `GeneralsMD/Code/GameEngine/Source/GameNetwork/GameSpyGameInfo.cpp:566` (`InitGameLogicRandom`). `InitRandom()` без аргумента в каталоге `GameLogic` не найден. Запрещены `rand()`, `std::random_device`, `std::mt19937` и `GameClientRandomValue` в коде, который меняет состояние логики. Число вызовов логического RNG не зависит от клиента (видимость, камера, звук, локальный игрок). `GameLogicRandomValueUnchanged` в логике BFME не используем.
2. **Без `unordered_*` и `hash_map` в логике**, без обхода контейнеров, упорядоченных по адресам указателей. Ключи — `ObjectID`, `NameKeyType`, индекс INI. Сейчас в логике ZH `unordered_*`/`hash_map` не найдено. `AttackPriorityMap` — это `std::map<const ThingTemplate*, Int, std::less<const ThingTemplate*>>` (`GeneralsMD/Code/GameEngine/Include/GameLogic/ScriptEngine.h:102`): порядок обхода — адреса указателей. В логике чтение — `find` (`ScriptEngine.cpp:189`), это допустимо. Запись в сейв идёт в `AttackPriorityInfo::xfer` (`:292–307`): в файл пишутся имя шаблона и `Int`, но пары идут в порядке адресов. Отладочный `dumpPriorityInfo` (`:206`) обходит так же. `AttackPriorityInfo::crc` (`:236`) пустой, поэтому лёгкая CRC карту не хеширует. Deep CRC хеширует её через тот же `xfer` (см. п. 5). **Решение:** сериализация в `xfer` идёт в порядке ключей (имя шаблона), а не адресов. Это правка самого `xfer`, не отдельная сортировка только дампа. Схема записи не меняется: версия 1, счётчик, имя шаблона, приоритет. Меняется только порядок пар. Правка `// BFME:`, ревью Лида.
3. **Без времени стены в логике:** только номер кадра. `timeGetTime()` в `GameLogic.cpp` стоит в загрузке объектов карты (`:2004`, `:2100`, `:2105`) и в таймауте прогресса загрузки сети (`lastHeardFrom` `:5384`, `testTimeOut` `:5395` и `:5425`), не в шаге симуляции. Запрещены `time()`, `clock()`, `std::chrono`, `timeGetTime` в логике.
4. **Ничья по расстоянию/приоритету** разрешается по `ObjectID`. Порядок суммирования float (модификаторы, урон) фиксирован.
5. **CRC и сейвы.** Каждый модуль логики BFME реализует `crc()` и `xfer()` со всем состоянием логики. Deep CRC отражает только состояние логики: `XferDeepCRC` выставляет режим `XFER_SAVE` (`Core/GameEngine/Source/Common/System/XferCRC.cpp:185` и `:212`), `xferSnapshot` по-прежнему зовёт `crc()`, а дополнительно `getCRC` при режиме `XFER_SAVE` вызывает `friend_xferSaveDataForCRC(..., SNAPSHOT_DEEPCRC_LOGICONLY)` (`GameLogic.cpp:4943–4947`). Блоки логики перечислены в `GameState.cpp:347–352`, среди них `ScriptEngine`. Поля, которые зависят от клиентского RNG, в CRC не входят и в сейве остаются. Пример — `StructureToppleUpdate::m_nextBurstFrame`: `crc()` (`StructureToppleUpdate.cpp:564`) его не пишет, `xfer()` пишет безусловно (`:610`). Проверки одного только `getXferMode()==XFER_CRC` недостаточно, чтобы убрать поле из Deep CRC: дамп идёт в режиме `XFER_SAVE` и вызывает `xfer()`. Геймплей исключает такие поля из дампа логики и оставляет их в сейве. Формат сейва от этого не меняется.
6. **Парсинг INI** без зависимости от локали (`std::from_chars`, решение Форматов).
7. **Проверка вызовов клиентского RNG в логических файлах** (`GeneralsMD/Code/GameEngine/Source/GameLogic`, `rg GameClientRandomValue` на `f79e624`). 18 строк, из них одна — комментарий (`ActiveBody.cpp:1025`), вызовов 17. Классификация прежняя, строки сверены заново. `StealthUpdate.cpp:135` и `SpectreGunshipUpdate.cpp:656–657` разбирают Геймплей и QA вместе. Если клиентский RNG там влияет на состояние логики, переводим вызов на `GameLogicRandomValue`. Совместимость с реплеями ZH для этого не нужна.

| Файл:строка | Что | Оценка |
|---|---|---|
| `Object/Body/ActiveBody.cpp:1026` | кость для частиц урона (комментарий о переводе на клиентский RNG из-за рассинхрона, 2003, строка 1025) | визуал, безопасно |
| `Object/Update/AIUpdate/ChinookAIUpdate.cpp:1199` | частицы потока от винта | визуал |
| `Object/Update/AIUpdate/HackInternetAIUpdate.cpp:563–564` | позиция текста денег | визуал |
| `Object/Update/AutoDepositUpdate.cpp:190–191` | позиция текста денег | визуал (в BFME1 17 подключений) |
| `Object/Update/StealthUpdate.cpp:135` | `m_pulsePhase = GameClientRandomValueReal(0, PI)` | разбирают Геймплей и QA: влияет ли фаза на обнаружение (в BFME1 26 подключений) |
| `Object/Update/StructureCollapseUpdate.cpp:188, 216` | дрожание при обрушении | визуал: `shudder` идёт только в матрицу `Drawable` (`getInstanceMatrix`, строка 190), позиция объекта не меняется (в BFME1 241 подключение) |
| `Object/Update/StructureToppleUpdate.cpp:173, 223, 271` | кадр следующей «вспышки», дальше только `FXList::doFXPos` | визуал для симуляции; `m_nextBurstFrame` остаётся в сейве (`xfer`, строка 610) и не входит в CRC (п. 5; в BFME1 2 подключения) |
| `Object/Update/SpectreGunshipUpdate.cpp:656–657` | смещение точки частиц гатлинга (`setPosition` на строке 659) | разбирают Геймплей и QA: если по этой точке наносится урон, это состояние логики (в BFME1 не используется) |
| `Object/Update/EMPUpdate.cpp:328, 353` | высота и задержка частиц | визуал |
| `Object/Update/ParticleUplinkCannonUpdate.cpp:676` | тип подпалины («client fluff») | визуал |

### 2.6 Явные правила

1. **E1-45 идёт после E1-49.** Сравнение CRC между архитектурами имеет смысл только когда логика уже на своей GameMath. До этого — контрольный прогон с флагами §2.1 при `SAGE_USE_DETERMINISTIC_MATH=OFF`. Он не считается доказательством побитового совпадения.
2. **Расхождение CRC между архитектурами — Major. Расхождение на одной архитектуре — блокер.**
3. **CRC не включает поля, зависящие от клиентского RNG.** В сейве эти поля остаются (§2.5 п. 5).
4. **Уровень героя BFME — отдельный счётчик 1–10** в модуле BFME, со своей сериализацией и CRC. `enum VeterancyLevel` не трогаем: в `Core/GameEngine/Include/Common/GameCommon.h:198–210` это `LEVEL_REGULAR`, `LEVEL_VETERAN`, `LEVEL_ELITE`, `LEVEL_HEROIC`. Для старых эффектов ZH — одностороннее отображение уровня героя в `VeterancyLevel`. Обратного отображения нет. Это правило Э2: E1-BACKLOG (раунд 2, п. 4) не добавляет под него строку в таблицы Э1.

## 3. Варианты, которые не выбраны

- **Fixed-point для всей логики:** XL-переписывание ZH, не берём (§1 п. 3).
- **Ждать GameMath из TSH #2670:** на `838f9d0f` и на свежем `28170001` каталога `Core/GameMath` нет. Имя опции и дефайн сохраняем (§2.2), исходник берём свой.
- **libm платформы как единственная математика логики:** headless-сборка CI — Linux x86_64, телефон — arm64, bionic на разных версиях Android может отличаться **[разницу между версиями bionic не проверял]**.
- **Подмена `sinf` и др. макросом во всём бинаре:** замедлит рендер и частицы. GameMath только в логике, рендер и клиент на libm.
- **Включать `SAGE_USE_DETERMINISTIC_MATH` с Э1, до своей GameMath:** `FetchContent` указывает на несуществующий каталог.
- **Класть флаги детерминизма в `CMakePresets.json`:** они уже в `cmake/compilers.cmake`.
- **Вызывать `checkAndReset` после `setFPMode()`:** проверка увидела бы уже сброшенное состояние.

## 4. Последствия

- Геймплей: GameMath на libm из musl (MIT), `add_subdirectory` в `gamemath.cmake`, перевод `Trig.cpp` / `wwmath.h` / `PhysicsUpdate.cpp:793`, хелперы `int32_t`, запрет `long double`, вызовы `fp::` в `GameLogic`, флаг `-gxFpCheck`, `Stats` в оверлее. Оценка E1-49 — 5–7 дней. Уровень героя 1–10 — Э2 (§2.6 п. 4).
- Android-платформа: проверка флагов в `compile_commands.json` (E1-10), CI-запрет `-ffast-math`/`-Ofast` (R4), реализации `fp::`, `Guard`, `-gxFpChaos`. Флаги в `CMakePresets.json` не добавляются.
- QA: E1-45 после E1-49. Пока нет реплея ZH, CRC логики снимаем на синтетическом сценарии (`-qaScenario` и `-seed`) на карте BFME1 или пустой карте. Ключи приняты в E1-41. Когда появятся реплеи BFME, E1-45 идёт за E1-43 на них (бэклог: без ZH E1-43 уходит в Э2). На первом прогоне проверяем сдвиг CRC на +1 кадр.
- Все правки апстримных файлов помечаются `// BFME:` и записываются в `docs/UPSTREAM-PATCHES.md` (ADR-0001 §3). Каталог GameMath — туда же (ADR-0001 §3 п. 9).
- Любой cherry-pick из TSH, затрагивающий математику, порядок обновления или RNG, идёт через QA (ADR-0001 §5).
- Совместимость с ретейлом ZH и с GeneralsOnline мы теряем (у них своя математика), это не цель проекта. Поэтому перевод клиентского RNG на `GameLogicRandomValue`, если он задевает состояние логики, допустим.
- GameMath медленнее CRT. Стоимость на Pixel 8 Pro измеряет QA вместе с бюджетом тика логики 10 мс p95 (решение Лида п. 5 в GAMEPLAY-GAP §2a).
- Уведомление MIT для кода musl входит в обязательства GPLv3 (ADR-0001 §4).

## 5. Открытые вопросы

1. ✅ **Закрыт 02.10.2026.** Источник GameMath — libm из musl (MIT). MIT совместима с GPLv3. TSH не ждём: каталога `Core/GameMath` нет ни на `838f9d0f`, ни на `28170001`. Файлы musl храним с исходными заголовками, коммит musl — в README каталога, лицензия — в `LICENSES/musl-MIT.txt`.
2. ✅ **Закрыт 02.10.2026.** Каталог — `Core/Libraries/Source/GameMath/`. Это исключение из ADR-0001 §3 п. 3 (каталог в дереве апстрима), записано в ADR-0001 §3 п. 9 и вносится в `docs/UPSTREAM-PATCHES.md`.
3. Влияют ли `StealthUpdate.cpp:135` и `SpectreGunshipUpdate.cpp:656–657` на состояние логики? Разбирают Геймплей и QA. Если да — перевод на `GameLogicRandomValue`; совместимость с реплеями ZH не требуется. Что делать с `m_nextBurstFrame`, уже решено (§2.5 п. 5): в сейве остаётся, в CRC не входит.
4. Политика на рассинхрон (стоп или продолжение) — NET-DESIGN §3.2 п. 1, решает Лид. Тяжесть уже задана: между архитектурами Major, на одной архитектуре блокер (§2.6 п. 2).

---

## Изменения 02.10.2026 (сверка с решениями Лида и кодом)

Черновик приведён к решениям Лида и к уточнению `fp::` от Android-платформы. Статус остаётся «Предложено». Код не менялся. Строки сверены с деревом `f79e624`; устаревшие номера предыдущего черновика (счётчики `Real`, `timeGetTime`, `InitRandom`, `atan2`, клиентский RNG) заменены на проверенные.

1. **§2.2 и §5 п. 1–2.** Источник GameMath — libm из musl (MIT). Формулировка «fdlibm/musl-подобная» убрана. `SAGE_USE_DETERMINISTIC_MATH` остаётся OFF до E1-49. Каталог, лицензия, `add_subdirectory`, сохранение имени опции и дефайна, тест 10^6 под `qemu-aarch64` записаны как решение. Вопросы §5 п. 1 и п. 2 закрыты.
2. **§2.1.** Флаги не переносятся в `CMakePresets.json`: они уже в `cmake/compilers.cmake:58` и `:69`. Платформа проверяет `compile_commands.json` и запрет `-ffast-math`/`-Ofast` (R4).
3. **§1 п. 2, шапка.** Убрана схема «x86_64 для эмулятора и CI». Сравнение CRC — headless Linux x86_64 против arm64 на Pixel 8 Pro. Побитовый тест GameMath на arm64 — `qemu-aarch64`, пока телефона нет в CI.
4. **§2.5 п. 5 и §2.6.** E1-45 после E1-49. До этого — контрольный прогон с флагами. Без реплея ZH — синтетический сценарий `-qaScenario` + `-seed`. Расхождение CRC между архитектурами — Major, на одной — блокер. CRC без полей клиентского RNG. Уровень героя 1–10 — отдельный счётчик Э2, `VeterancyLevel` не меняется.
5. **§2.5 п. 2.** Порядок `AttackPriorityMap` задаётся в самом `xfer` (имя шаблона), схема сейва не меняется.
6. **§2.3.** Интерфейс `fp::` записан в ADR, потому что `docs/fpcr-audit.md` §8 в ветке нет. `checkAndReset("tick-start")` заменяет вызов `setFPMode()` в `GameLogic::update` (`:4126`). `setFPMode()` становится обёрткой над `fp::reset()`. `assertExpected` — в конце тика и в `getCRC`, фатален в CI и debug.
7. **§2.4.** К запрету `lroundf`/`long` добавлен `long double` (R6) и хелперы `int32_t` в GameMath.
