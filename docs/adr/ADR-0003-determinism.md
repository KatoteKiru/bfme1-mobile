# ADR-0003: детерминизм логики (плавающая точка, математика, RNG, порядок)

- **Статус:** Предложено, 02.10.2026. Черновик Геймплея, приведён к решениям Лида движка от 02.10.2026 и к уточнению интерфейса `fp::`, согласованному с Android-платформой в тот же день. Утверждают Лид движка и QA.
- **Авторы:** Геймплей (черновик), Лид движка (решения), Android-платформа (интерфейс `fp::`).
- **Владельцы:** Лид движка (правила, ревью хуков в апстримных файлах); Геймплей (своя GameMath, E1-49, 5–7 дней; перевод логики; заглушки в `wwmath.h`; вызовы `fp::` в `GameLogic`; `Stats` в оверлее; флаг `-gxFpCheck`; хелперы `int32_t`; запрет `long double` / `lroundf` / `long` в логике и сериализации); Android-платформа (проверка, что флаги из `cmake/compilers.cmake` попали в `compile_commands.json`; запрет `-ffast-math`/`-Ofast` с проверкой в CI; реализации `fp::` по архитектурам; `fp::Guard` на `newDrawable` и создании ресурсов рендера из логики; хаос-режим `-gxFpChaos`); QA (E1-45 после E1-49, синтетический сценарий, разбор `SpectreGunshipUpdate` вместе с Геймплеем; `m_pulsePhase` закрыт).
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

Регистры управления FP у каждого потока свои. Поток логики — тот, который первым вызвал `GameLogic::init()` (`GeneralsMD/Code/GameEngine/Source/GameLogic/System/GameLogic.cpp:430`). Его `std::thread::id` запоминается там же, в `fp::` или в `GameLogic`. `checkAndReset`, `assertExpected` и `getCRC` сверяют id с запомненным. В debug и в CI несовпадение фатально. К имени потока не привязываемся: игровой цикл запускает SDL или `main` в зависимости от платформы, а правило «кто инициализировал, тот и тикает» одинаково везде. Счётчики не атомарные: их трогает только этот поток.

`docs/fpcr-audit.md` в ветке нет (придёт с PR E1-03). Интерфейс ниже — согласованное с Android-платформой описание, которое иначе лежало бы в §8 того файла.

**Норма (R3 аудита), после сброса:**

- arm64: `FPCR == 0`;
- x86_64: `(MXCSR & 0xFFC0) == 0x1F80` и `(x87 CW & 0x0F3F) == 0x003F`.

`State` хранит только управляющие регистры (arm64 — FPCR; x86_64 — MXCSR и x87 CW), без FPSR. Чтение — `asm volatile` с клоббером `"memory"`. Что `fesetenv(FE_DFL_ENV)` на bionic arm64 пишет FPCR=0, в этом дереве не проверено: bionic здесь не лежит, это вывод аудита. В коде проверено другое: не-Windows ветка `setFPMode()` (`GameLogic.cpp:239–241`) вызывает `fesetenv(FE_DFL_ENV)`, затем `feclearexcept(FE_ALL_EXCEPT)` и `fesetround(FE_TONEAREST)`; на x86 следом правит x87 CW (`:243–248`, маска `~0x0F00`) и MXCSR (`:250–254`).

**Почему проверка строится на `setFPMode`, а не рядом.** `fp::checkAndReset` нельзя вызывать после `setFPMode()`: после сброса он всегда видит норму. `fp::reset()` — единственная реализация сброса. `setFPMode()` остаётся обёрткой над `fp::reset()`, чтобы нынешние вызовы сами начали сбрасывать через `fp::`.

Сейчас `setFPMode()` в `GameLogic::update` — не первая строка функции. Функция начинается на строке 4110; до вызова на строке 4126 стоят таймер, профайлер, ветка `GENERALS_ONLINE_HIGH_FPS_SERVER`, `LatchRestore` и при `DO_UNIT_TIMINGS` — `unitTimings()`. Заменяем именно этот вызов (первый, который трогает FP-режим) на `fp::checkAndReset("tick-start")`. Он читает состояние, при отклонении пишет в лог, увеличивает счётчик и вызывает `reset()`.

Другие вызовы `setFPMode()`, которые остаются обёрткой над `reset()`: `GameLogic::init` (`:433`), сброс в `init` (`:488`), загрузка карты (`:1440`), кадр затухания при загрузке (`:2380`), `INI::load` (`Core/GameEngine/Source/Common/INI/INI.cpp:416`), `W3DModelDraw` (`:507`, `:603`, `:727`, `:865`). Тем же путём идут `GameClient.cpp:682`, `ScoreScreen.cpp:900`, `W3DView.cpp:654`, `MapUtil.cpp:441`, `LoadScreen.cpp` (`:191`, `:682`, `:1187`).

**Конец тика и CRC.** `fp::assertExpected("tick-end")` — перед выходом из `GameLogic::update` (функция заканчивается на строке 4467). В `getCRC` сейчас первая строка тела после проверки режима — снова `setFPMode()` (`:4714`); её заменяем на `fp::assertExpected("getCRC")`, а не ставим проверку следом: сброс перед проверкой спрятал бы отклонение. Оба вызова, как и `checkAndReset`, сначала сверяют `std::thread::id` с потоком, который первым вошёл в `GameLogic::init()`. Фатален `assertExpected` и несовпадение id: в CI и debug режим Fatal, в release — Log (§ ниже, `setMode`). Несовпадение id в debug и CI фатально и в Log не переводится.

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
2. **Без `unordered_*` и `hash_map` в логике**, без обхода контейнеров, упорядоченных по адресам указателей. Ключи — `ObjectID`, `NameKeyType`, индекс INI. Сейчас в логике ZH `unordered_*`/`hash_map` не найдено. `AttackPriorityMap` — это `std::map<const ThingTemplate*, Int, std::less<const ThingTemplate*>>` (`GeneralsMD/Code/GameEngine/Include/GameLogic/ScriptEngine.h:102`): порядок обхода — адреса указателей. В логике чтение — `find` (`ScriptEngine.cpp:189`), это допустимо. Запись в сейв идёт в ветке `XFER_SAVE` функции `AttackPriorityInfo::xfer` (`:292–307`): в файл пишутся имя шаблона и `Int`, но пары идут в порядке адресов. Загрузка (`:319–345`) читает имя, ищет шаблон и вызывает `setPriority`; порядок чтения на содержимое карты не влияет. Отладочный `dumpPriorityInfo` (`:206`) обходит так же, как сейв. **Решение:** при записи в `xfer` пары сортируются по имени шаблона. Загрузка не меняется. Схема записи не меняется: версия 1, счётчик, имя шаблона, приоритет. Меняется только порядок пар в сейве. Причина — побайтно воспроизводимый сейв, чтобы QA сравнивал сейвы диффом. `AttackPriorityInfo::crc` (`:236`) остаётся пустым, как в ZH: иначе изменится лёгкая CRC и сломается сравнение с апстримом. Deep CRC карту не видит: `ScriptEngine::crc` (`ScriptEngine.cpp:8616`) пустой и `m_priorityMap` не хеширует. Правка `// BFME:`, ревью Лида. Отдельный PR — только эта сортировка.
3. **Без времени стены в логике:** только номер кадра. `timeGetTime()` в `GameLogic.cpp` стоит в загрузке объектов карты (`:2004`, `:2100`, `:2105`) и в таймауте прогресса загрузки сети (`lastHeardFrom` `:5384`, `testTimeOut` `:5395` и `:5425`), не в шаге симуляции. Запрещены `time()`, `clock()`, `std::chrono`, `timeGetTime` в логике.
4. **Ничья по расстоянию/приоритету** разрешается по `ObjectID`. Порядок суммирования float (модификаторы, урон) фиксирован.
5. **CRC и сейвы.** Обе CRC строятся по `crc()`, не по `xfer()` модулей. У `XferDeepCRC` своего `xferSnapshot` нет: он наследует `XferCRC::xferSnapshot` (`XferCRC.cpp:110–121`), а тот вызывает `snapshot->crc()`. `XFER_SAVE` (`XferCRC.cpp:185` и `:212`) задаёт только ветку сохранения в `GameState::xferSaveData` (`GameState.cpp:1416`, проверка режима `:1424`). `friend_xferSaveDataForCRC` приходит туда и на `:1463` снова зовёт `xfer->xferSnapshot`, то есть снова `crc()`. `Xfer.h` и `XferCRC.cpp` не трогаем. Флаг `Xfer::isLogicCRCDump()` не заводим.

   Лёгкая CRC — `getCRC` без файла дампа, `XferCRC` (`GameLogic.cpp:4745–4748`): объекты (`:4806–4811`) и `TheAI` (`:4937`), оба через `crc()`. В том же проходе до `TheAI` код ещё хеширует сид логики, и только при режиме `XFER_CRC` (`:4842–4844`), `ThePartitionManager` (`:4869`) и `ThePlayerList` (`:4922`). Deep CRC — тот же обход плюс блоки `SNAPSHOT_DEEPCRC_LOGICONLY` (`:4943–4947`, список `GameState.cpp:347–352`). `GameLogic::crc` пустой (`:5547`), поэтому `GameLogic::xfer` и `Object::xfer` в дамп не входят.

   **Правило.** Поля клиентского RNG в `crc()` не пишем. В `xfer()` они остаются: формат сейва их хранит. Инвариант для новых модулей BFME: ни один `crc()` не вызывает `xfer()`. На ревью это проверяется по телу `crc()`. Дешёвый контроль в CI — один проход по `*.cpp` без компиляции: у каждой функции `::crc(Xfer*)` разобрать тело по скобкам и упасть, если внутри есть вызов `xfer(` с тем же указателем `Xfer*` (`xfer(xfer)`, `xfer(x)`, `this->xfer(...)`), и это не `xfer->` и не `xferSnapshot`. Вызовы `*.xfer(xfer)` у чужого типа (битовые флаги) не считать нарушением `Snapshot::xfer`. Список исключений ZH, в него файлы BFME не добавлять: `AIUpdate.cpp:5086`. Каталоги: `GeneralsMD/Code/GameEngine/` и `engine/bfme/`. Для `engine/bfme/` список исключений пустой.

   Контрпример в ZH, из-за которого список исключений не пуст. Поиск вызова `xfer(xfer)` по буквальному имени аргумента его не видит: в `AIUpdateInterface::crc` аргумент назван `x`, строка `xfer(x)` (`AIUpdate.cpp:5086`). `xfer` виртуальный (`Snapshot.h:61`), поэтому вызывается `xfer()` динамического типа. `getCRC` сегодня эту функцию не достигает: обход объектов идёт в `Object::crc` (`Object.cpp:4010`), а тот модули поведения не снимает. `ChinookAIUpdate::crc` (`:1402`) и `HackInternetAIUpdate::crc` (`:206`) до базы доходят, так что если их `crc()` когда-нибудь вызовут из дампа, в CRC попадёт их `xfer()`. Клиентский RNG этих двух модулей полем не является, см. таблицу.

   `StructureToppleUpdate::m_nextBurstFrame` уже сейчас не входит ни в лёгкую CRC, ни в Deep CRC. `StructureToppleUpdate::crc` (`:564`) вызывает только `UpdateModule::crc`. Поле пишется в сейв `xferInt` (`:610`) и там остаётся. Отдельного пропуска не нужно.

   Единый список, сверенный с `GameClientRandomValue` / `GameClientRandomValueReal` в `GeneralsMD/Code/GameEngine/Source/GameLogic` на `bf173c8`. Вызовов 19 на 17 строках: `StructureCollapseUpdate.cpp:188` и `:216` содержат по два. Три пишут `m_nextBurstFrame`. Строка `ActiveBody.cpp:1025` — комментарий, не вызов. Колонка «Поле в crc()» сверена с `crc()` того же модуля.

   | Вызов | Куда девается значение | Поле в `crc()` |
   |---|---|---|
   | `StructureToppleUpdate.cpp:173`, `:223`, `:271`, поле `m_nextBurstFrame` | `now + GameClientRandomValue(...)`. Комментарии `:222` и `:270`: вспышки задержки — визуальный и звуковой эффект. Чтения — только `now >= m_nextBurstFrame` перед `doToppleDelayBurstFX()` (`:220`, `:268`); функция зовёт `FXList::doFXPos` и создаёт частицы (`:472–496`). Старт завала — `m_toppleFrame`. Угол рядом — `GameLogicRandomValueReal` (`:159`), в список не входит | нет. `crc()` (`:564`) — только `UpdateModule::crc`. В сейве поле есть: `xferInt` (`:610`) |
   | `StealthUpdate.cpp:135`, поле `m_pulsePhase` | `GameClientRandomValueReal(0, PI)` в конструкторе. Единственное чтение — `:691`, внутри `if (draw)` (`:643`): `Drawable::setEffectiveOpacity`. Дальше фаза только увеличивается (`:694`, `:696`). Обнаружение — `m_detectionExpiresFrame` (`:782`, `:912–916`) | нет. `StealthUpdate::crc` (`:1131`) вызывает только `UpdateModule::crc`, поле не хеширует. В сейве модуля поле есть: `xferReal` (`:1168`). Закрыто QA 02.10.2026: на логику не влияет, перевод на `GameLogicRandomValue` не нужен. Прозрачность читают рендер (`W3DScene.cpp:485`, `:1653`, `:1966`; `W3DDependencyModelDraw.cpp:109` через `imitateStealthLook`) и клиентский сейв (`Drawable::xfer`, `m_effectiveStealthOpacity`, `:5072`). Проверка реплеем: два прогона, клиентский сид 0 и 12345 либо фаза 0 и π/2; CRC логики по кадрам должны совпасть |
   | `SpectreGunshipUpdate.cpp:656–657` | локальный `impactPosition`, дальше `ParticleSystem::setPosition` (`:659`). В `xfer` (`:855–900`) смещения нет | нет. `SpectreGunshipUpdate::crc` (`:840`) — только `UpdateModule::crc`. Поля нет. На разбор с QA: если по этой точке наносится урон, вызов переводится на `GameLogicRandomValue`; совместимость с реплеями ZH не нужна |
   | `ActiveBody.cpp:1026`, `boneIndex` | индекс кости для частиц урона, локальная переменная | нет. `ActiveBody::crc` (`:1540`) — только `BodyModule::crc` |
   | `ChinookAIUpdate.cpp:1199` | условие, выпускать ли частицы потока от винта | нет, поля нет. `ChinookAIUpdate::crc` (`:1402`) выходит на `AIUpdateInterface::crc`, см. контрпример выше; само условие в `xfer` не пишется |
   | `HackInternetAIUpdate.cpp:563–564` | локальный сдвиг `pos` перед `addFloatingText`; комментарий `:548` запрещает в блоке что-либо ещё | нет, поля нет. `HackInternetAIUpdate::crc` (`:206`) выходит на `AIUpdateInterface::crc`; координаты в `xfer` модуля не пишутся |
   | `AutoDepositUpdate.cpp:190–191` | то же для текста денег; комментарий `:179` | нет. `AutoDepositUpdate::crc` (`:233`) — только `UpdateModule::crc`. `m_depositOnFrame` и флаги пишутся в `xfer` (`:258–263`) и от этого RNG не зависят |
   | `StructureCollapseUpdate.cpp:188`, `:216` (по два вызова) | локальный `shudder` уходит в матрицу `Drawable` (`getInstanceMatrix`, `:190`) | нет. `StructureCollapseUpdate::crc` (`:356`) — только `UpdateModule::crc`. `m_currentHeight` в `xfer` (`:393`) считается из скорости обрушения, не из этого RNG |
   | `EMPUpdate.cpp:328` | `offs.z` уходит в `ParticleSystem::setPosition` | нет. `EMPUpdate::crc` (`:388`) пустой. `EMPUpdate::xfer` (`:398`) пишет только версию. `m_startFrame` на `:572` — `LeafletDropBehavior::xfer` в том же файле |
   | `EMPUpdate.cpp:353` | `ParticleSystem::setInitialDelay`. Ветка `GENERALS_ONLINE_HIGH_FPS_SERVER` на `:350` для той же задержки зовёт `GameLogicRandomValue`; это другой вызов | нет. Тот же пустой `EMPUpdate::crc` |
   | `ParticleUplinkCannonUpdate.cpp:676` | локальный `scorchID` в `TheGameClient->addScorch` («client fluff»). `m_scorchMarksMade` увеличивается до броска (`:673`) и от id не зависит | нет. `ParticleUplinkCannonUpdate::crc` (`:1396`) — только `UpdateModule::crc`. `m_scorchMarksMade` остаётся в `xfer` (`:1491`) |

   Рядом, не из этих 19: `ClientRandomValueClass()` в логике на трёх местах. Случайное значение в `crc()` не пишется.

   - `EMPUpdate.cpp:318`: смещение следа искры, дальше `ParticleSystem::setPosition` (`:343`). `EMPUpdate::crc` пустой.
   - `SpecialAbilityUpdate.cpp:1415`: то же для частиц отключения. `m_doDisableFXParticles` переключается до броска (`:1401`) и пишется в `xfer` (`:2067`), не в `crc()`. `SpecialAbilityUpdate::crc` (`:2003`) — только `UpdateModule::crc`.
   - `TransitionDamageFX.cpp:401`: позиция эффекта, дальше `setPosition` (`:409`). `TransitionDamageFX::crc` (`:432`) — только `DamageModule::crc`. В `xfer` (`:457`) лежит `m_particleSystemID` от `getSystemID()` (`:415`), не от этого броска.
6. **Парсинг INI** без зависимости от локали (`std::from_chars`, решение Форматов).
7. **Проверка вызовов клиентского RNG** сведена в п. 5 (19 вызовов на 17 строках в `GeneralsMD/Code/GameEngine/Source/GameLogic`, плюс комментарий `ActiveBody.cpp:1025`). Ни одно из этих значений в `crc()` своего модуля не пишется. `m_nextBurstFrame` и `m_pulsePhase` уже вне обеих CRC и остаются в `xfer()`. `m_pulsePhase` закрыт QA: на логику не влияет, перевод на `GameLogicRandomValue` не нужен. `SpectreGunshipUpdate.cpp:656–657` — на разбор с QA. Если QA подтвердит влияние на состояние логики, вызов переводится на `GameLogicRandomValue`. Совместимость с реплеями ZH для этого не нужна.
8. **ClientBehavior сохраняется с `Drawable`, не с `Object`, и в CRC логики не входит.** В BFME это синоним ZH-ключа `ClientUpdate`. В этом дереве ключ INI — `ClientUpdate`, тип `MODULETYPE_CLIENT_UPDATE` (`ThingTemplate.cpp:182`, `Module.h:67`). Модули создаются на `Drawable` (`Drawable.cpp:484–500`), не на `Object`. `Object` собирает и пишет только `MODULETYPE_BEHAVIOR` (`Object.cpp:412`, цикл `m_behaviors` в `Object::xfer` `:4404–4432`). Проверено на `bf173c8`: ни тело `Drawable`, ни client-update модули не попадают ни в лёгкую CRC, ни в `XferDeepCRC`. Находки «попадают» нет.

   Сейв идёт отдельным блоком клиента. `GameClient::xfer` (`GameClient.cpp:1633`) обходит drawable и зовёт `xferSnapshot` (`:1701`). `Drawable::xfer` (`:4970`) в конце зовёт `xferDrawableModules` (`:5211`, функция с `:4836`). Та пишет все типы модулей drawable, включая `MODULETYPE_CLIENT_UPDATE` (`NUM_DRAWABLE_MODULE_TYPES`, цикл `:4859–4887`). `GameClient` зарегистрирован только как `SNAPSHOT_SAVELOAD` (`GameState.cpp:339`). В списке Deep CRC его нет (`:347–352`: TeamFactory, Players, GameLogic, ScriptEngine, SidesList, Partition).

   Лёгкая CRC. `GameLogic::getCRC` без файла дампа создаёт `XferCRC` (`GameLogic.cpp:4745–4748`) и обходит объекты через `xferSnapshot` (`:4806–4811`). `XferCRC::xferSnapshot` всегда зовёт `crc()` (`XferCRC.cpp:121`), не `xfer()`. `Object::crc` (`Object.cpp:4010`) drawable и client-update не пишет. `Drawable::crc` (`Drawable.cpp:4826`) и `GameClient::crc` (`GameClient.cpp:1881`) пустые. `getCRC` `TheGameClient` не обходит.

   Deep CRC. С файлом дампа `getCRC` создаёт `XferDeepCRC` (`GameLogic.cpp:4722–4725`). `XferDeepCRC` `xferSnapshot` не переопределяет, поэтому обход объектов снова идёт в `crc()`, не в `xfer()`. Дополнительный блок `friend_xferSaveDataForCRC(..., SNAPSHOT_DEEPCRC_LOGICONLY)` (`:4943–4947`) тоже зовёт `xferSnapshot`, то есть `crc()` перечисленных блоков. `GameLogic::crc` пустой (`:5547`), поэтому `GameLogic::xfer` и лежащий в нём `Object::xfer` в дамп не входят. `DrawableID` объект пишет только в `Object::xfer` (`Object.cpp:4192–4195`); в `Object::crc` этого поля нет, в обе CRC оно не попадает.

   Под `DEBUG_CRC` `getCRC` может снять `TheModuleFactory` (`GameLogic.cpp:4875–4881`). `ModuleFactory::crc` (`ModuleFactory.cpp:699–704`) хеширует шаблоны `ModuleData`, не живые экземпляры `Drawable`. В обычную лёгкую CRC и в `XferDeepCRC` этот путь не входит.

### 2.6 Явные правила

1. **E1-45 идёт после E1-49.** Сравнение CRC между архитектурами имеет смысл только когда логика уже на своей GameMath. До этого — контрольный прогон с флагами §2.1 при `SAGE_USE_DETERMINISTIC_MATH=OFF`. Он не считается доказательством побитового совпадения.
2. **Расхождение CRC между архитектурами — Major. Расхождение на одной архитектуре — блокер.**
3. **Поля клиентского RNG в `crc()` не пишем, в `xfer()` они остаются.** Обе CRC идут через `crc()`. `xfer()` модулей в CRC не участвует (§2.5 п. 5).
4. **Уровень героя BFME — отдельный счётчик 1–10** в модуле BFME, со своей сериализацией и CRC. `enum VeterancyLevel` не трогаем: в `Core/GameEngine/Include/Common/GameCommon.h:198–210` это `LEVEL_REGULAR`, `LEVEL_VETERAN`, `LEVEL_ELITE`, `LEVEL_HEROIC`. Для старых эффектов ZH — одностороннее отображение уровня героя в `VeterancyLevel`. Обратного отображения нет. Это правило Э2: E1-BACKLOG (раунд 2, п. 4) не добавляет под него строку в таблицы Э1.
5. **ClientBehavior (ZH-ключ `ClientUpdate`, `MODULETYPE_CLIENT_UPDATE`) сохраняется с `Drawable` и в CRC логики не входит** (§2.5 п. 8). Ни лёгкая CRC, ни `XferDeepCRC` его не хешируют.

## 3. Варианты, которые не выбраны

- **Fixed-point для всей логики:** XL-переписывание ZH, не берём (§1 п. 3).
- **Ждать GameMath из TSH #2670:** на `838f9d0f` и на свежем `28170001` каталога `Core/GameMath` нет. Имя опции и дефайн сохраняем (§2.2), исходник берём свой.
- **libm платформы как единственная математика логики:** headless-сборка CI — Linux x86_64, телефон — arm64, bionic на разных версиях Android может отличаться **[разницу между версиями bionic не проверял]**.
- **Подмена `sinf` и др. макросом во всём бинаре:** замедлит рендер и частицы. GameMath только в логике, рендер и клиент на libm.
- **Включать `SAGE_USE_DETERMINISTIC_MATH` с Э1, до своей GameMath:** `FetchContent` указывает на несуществующий каталог.
- **Класть флаги детерминизма в `CMakePresets.json`:** они уже в `cmake/compilers.cmake`.
- **Вызывать `checkAndReset` после `setFPMode()`:** проверка увидела бы уже сброшенное состояние.
- **Флаг `Xfer::isLogicCRCDump()`:** не нужен, Deep CRC идёт через `crc()`, а не через `xfer()`. К нему вернёмся, только если появится `crc()`, который зовёт `xfer()`. В ZH такой вызов уже есть у `AIUpdateInterface::crc` (`AIUpdate.cpp:5086`); это исключение, не образец для модулей BFME (§2.5 п. 5).
- **Привязка потока логики к имени `SDLThread`:** поток — тот, который первым вызвал `GameLogic::init()`, по `std::thread::id`.
- **Заполнять `AttackPriorityInfo::crc()`:** лёгкая CRC должна остаться как в ZH. Порядок имён задаётся только в записи `xfer`.

## 4. Последствия

- Геймплей: GameMath на libm из musl (MIT), `add_subdirectory` в `gamemath.cmake`, перевод `Trig.cpp` / `wwmath.h` / `PhysicsUpdate.cpp:793`, хелперы `int32_t`, запрет `long double`, вызовы `fp::` в `GameLogic`, флаг `-gxFpCheck`, `Stats` в оверлее. Оценка E1-49 — 5–7 дней. Уровень героя 1–10 — Э2 (§2.6 п. 4).
- Android-платформа: проверка флагов в `compile_commands.json` (E1-10), CI-запрет `-ffast-math`/`-Ofast` (R4), реализации `fp::`, `Guard`, `-gxFpChaos`. Флаги в `CMakePresets.json` не добавляются.
- QA: E1-45 после E1-49 и от покупки ZH не зависит. CRC логики снимаем на синтетическом сценарии (`-qaScenario` и `-seed`) на карте BFME1 или пустой карте. Ключи приняты в E1-41. Реплей ZH — только если покупка состоится, тогда через E1-43. На первом прогоне проверяем сдвиг CRC на +1 кадр.
- Все правки апстримных файлов помечаются `// BFME:` и записываются в `docs/UPSTREAM-PATCHES.md` (ADR-0001 §3). Каталог GameMath — туда же (ADR-0001 §3 п. 9). `Xfer.h` и `XferCRC.cpp` не меняем. Отдельный PR с кодом — только сортировка `AttackPriorityMap` в `xfer`.
- Любой cherry-pick из TSH, затрагивающий математику, порядок обновления или RNG, идёт через QA (ADR-0001 §5).
- Совместимость с ретейлом ZH и с GeneralsOnline мы теряем (у них своя математика), это не цель проекта. Поэтому перевод клиентского RNG на `GameLogicRandomValue`, если он задевает состояние логики, допустим.
- GameMath медленнее CRT. Стоимость на Pixel 8 Pro измеряет QA вместе с бюджетом тика логики 10 мс p95 (решение Лида п. 5 в GAMEPLAY-GAP §2a).
- Уведомление MIT для кода musl входит в обязательства GPLv3 (ADR-0001 §4).

## 5. Открытые вопросы

1. ✅ **Закрыт 02.10.2026.** Источник GameMath — libm из musl (MIT). MIT совместима с GPLv3. TSH не ждём: каталога `Core/GameMath` нет ни на `838f9d0f`, ни на `28170001`. Файлы musl храним с исходными заголовками, коммит musl — в README каталога, лицензия — в `LICENSES/musl-MIT.txt`.
2. ✅ **Закрыт 02.10.2026.** Каталог — `Core/Libraries/Source/GameMath/`. Это исключение из ADR-0001 §3 п. 3 (каталог в дереве апстрима), записано в ADR-0001 §3 п. 9 и вносится в `docs/UPSTREAM-PATCHES.md`.
3. ✅ **Закрыт 02.10.2026 (QA), `m_pulsePhase`.** `StealthUpdate::crc` (`:1131`) поле не хеширует: только `UpdateModule::crc`. Единственное чтение — `StealthUpdate.cpp:691`, внутри `if (draw)`, в `Drawable::setEffectiveOpacity`. На логику не влияет, перевод на `GameLogicRandomValue` не нужен, в `xfer` (`:1168`) поле остаётся. Проверка реплеем: два прогона, клиентский сид 0 и 12345 либо фаза 0 и π/2; CRC логики по кадрам должны совпасть.
4. **На разбор с QA.** `SpectreGunshipUpdate.cpp:656–657`: смещения в `crc()` нет, оно локальное для частиц. Если QA подтвердит влияние на логику — перевод на `GameLogicRandomValue`. Совместимость с реплеями ZH не требуется. `m_nextBurstFrame` уже решён (§2.5 п. 5): значение от `GameClientRandomValue`, в сейве остаётся, в `crc()` его нет.
5. Политика на рассинхрон (стоп или продолжение) — NET-DESIGN §3.2 п. 1, решает Лид. Тяжесть уже задана: между архитектурами Major, на одной архитектуре блокер (§2.6 п. 2).

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

---

## Изменения 02.10.2026 (флаг Deep CRC, поток логики, строки)

Сверено с деревом `bf173c8`. Код не менялся.

1. **§2.5 п. 5, черновик.** Здесь предлагался флаг `Xfer::isLogicCRCDump()`. Следующий блок его снимает: Deep CRC идёт через `crc()`.
2. **§2.5 п. 5, «на разбор с QA».** На тот момент открыты были `StealthUpdate.cpp:135` и `SpectreGunshipUpdate.cpp:656–657`. `m_pulsePhase` закрыт следующим блоком.
3. **§2.5 п. 2.** Запись `AttackPriorityMap` сортируется по имени шаблона. Загрузка не меняется. `crc()` остаётся пустым, как в ZH.
4. **§2.3.** Поток логики — первый вызов `GameLogic::init()`. Имя `SDLThread` убрано. Несовпадение `std::thread::id` фатально в debug и CI.
5. **§2.5 п. 8.** ClientBehavior (`ClientUpdate`, `MODULETYPE_CLIENT_UPDATE`) пишется с `Drawable` через `GameClient`, блок `SNAPSHOT_SAVELOAD`. В лёгкую CRC и в `XferDeepCRC` не входит. `docs/E1-BACKLOG.md` этим PR не меняется: формулировки E1-45, E1-49 и E1-10 остаются в ADR по номеру задачи, без новых номеров строк бэклога.

---

## Изменения 02.10.2026 (`m_pulsePhase` закрыт QA)

`StealthUpdate::crc` (`:1131`) поле не хеширует. QA: единственное чтение — `:691`, на логику не влияет. Перевод на `GameLogicRandomValue` не нужен. Вопрос §5 п. 3 закрыт. Проверка реплеем: два прогона, клиентский сид 0 и 12345 либо фаза 0 и π/2; CRC по кадрам должны совпасть. `SpectreGunshipUpdate.cpp:656–657` остаётся открытым (§5 п. 4).
