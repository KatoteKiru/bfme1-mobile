# ADR-0003: детерминизм логики (плавающая точка, математика, RNG, порядок)

- **Статус:** Предложено, 02.10.2026. Черновик Геймплея по решениям Лида движка от 02.10.2026 (пункты 2, 3, 7, 8). Утверждают Лид движка и QA.
- **Авторы:** Геймплей (черновик), Лид движка (решения).
- **Владельцы:** Лид движка (правила, ревью хуков в апстримных файлах); Геймплей (перевод логики на GameMath, проверка FPCR/MXCSR, хелперы float→int, линтер `engine/bfme/`); Android-платформа (флаги компилятора в `CMakePresets.json`, проверка FTZ у SDL3, звука и драйвера); QA (реплей-тесты, E1-45, корпус реплеев).
- **Проверка:** E1-45 (`docs/E1-BACKLOG.md`: CRC Linux x86_64 против arm64 на одном реплее ZH, С + Q), затем реплей-тесты QA в CI с Э2 (`PLAN.md` §3 «QA: детерминизм», §5 риск «Детерминизм и рассинхроны»; `docs/TEST-PLAN.md`). В формулировке E1-45 сказано «GameMath на fdlibm в форке», но её там нет (§1 п. 4), поэтому E1-45 нужно уточнить: при OFF проверяем флаги, при ON — нашу GameMath, как только она появится.
- **Связано:** NET-DESIGN.md §1.5 (CRC в ZH), §2 (детерминизм в MYSOREZ по DEV_BLOG), §3.2 (CRC N=30, отчёт о рассинхроне); ADR-0001 §3 (наш код в `engine/bfme/`, правки апстрима с `// BFME:`), §5 (cherry-pick из TSH через QA); ADR-0002 §2 п. 1 (Android только `arm64-v8a`, x86_64-сборку Android и эмулятор x86_64 не делаем), §7 (логика 30 Гц); GAMEPLAY-GAP.md §4.
- Пути ниже относительно корня форка (`src/GeneralsZH-Android-Port`, коммит `8ef2349`, далее «форк») или `src/GeneralsGameCode` (TSH, `2817000`, далее «TSH»), если не сказано иначе. Всё проверено `rg` на боксе 02.10.2026.

---

## 1. Контекст

1. Lockstep ZH обменивается только командами. Состояние сверяется по CRC (`GameLogic::getCRC(CRC_RECALC)`, `GeneralsMD/Code/GameEngine/Source/GameLogic/System/GameLogic.cpp`), в нашей сети каждые N=30 кадров в release и N=1 в QA-сборке (NET-DESIGN §3.2). Любое расхождение логики хотя бы в одном бите float даёт рассинхрон.
2. Играют одной сборкой arm64 (ADR-0002 §2 п. 1), но CI и десктопная отладка идут на Linux x86_64, а реплеи должны проверяться и на x86_64, и на aarch64 (NET-DESIGN §3.2 п. 5, «Replay check»; E1-45: Linux x86_64 против arm64, эмулятор или телефон). Значит, нужна побитовая одинаковость логики на arm64 и x86_64.
3. Вся логика ZH на `Real` = float: `PhysicsUpdate.cpp` (143 упоминания `Real`), `Core/GameEngine/Source/GameLogic/AI/AIPathfind.cpp` (145), `Locomotor.cpp` (244), `Weapon.cpp` (90). Переход на fixed-point не рассматриваем (XL, GAMEPLAY-GAP §4.1).
4. Что уже есть в форке:
   - `cmake/compilers.cmake` (ветка не-MSVC): `-ffp-contract=off`, `-fno-strict-aliasing -fwrapv -fno-delete-null-pointer-checks`. В TSH (`cmake/compilers.cmake`) этих флагов нет.
   - `cmake/gamemath.cmake`: опция `SAGE_USE_DETERMINISTIC_MATH` (по умолчанию **OFF**, строка 23), при ON тянет `FetchContent` из TSH по SHA `838f9d0f…` с `SOURCE_SUBDIR "Core/GameMath"` и включает `USE_DETERMINISTIC_MATH`.
   - **Факт, важный для решения 2:** в TSH на коммите `838f9d0f` (04.07.2026) каталога `Core/GameMath` нет (`git ls-tree 838f9d0f Core/`: только `GameEngine`, `GameEngineDevice`, `Libraries`, `Tools`), нет его и в текущем `2817000`. А в `Core/Libraries/Source/WWVegas/WWMath/wwmath.h` форка все ветки `#ifdef USE_DETERMINISTIC_MATH` — заглушки `// TODO: return GameMath::…` с откатом на CRT (`atan`, `atan2`, `Sin`, `Cos`, `tan`). То есть **готовой GameMath в форке нет**: включение флага сейчас либо сломает конфигурацию CMake, либо ничего не изменит. Запись ADR-0001 §1.1 «интегрирована GameMath на основе fdlibm» надо уточнить.
   - По DEV_BLOG форка (NET-DESIGN §2): `sinf` расходится на 1,236% углов, `lroundf` 32/64 бит (UCRT против bionic), NaN в `REAL_TO_INT_FLOOR`, при `-ffp-contract=off` 12 трансцендентных функций совпали бит в бит на x86_64/aarch64.

## 2. Решение

### 2.1 Флаги компилятора (решение Лида п. 3)
- `-ffp-contract=off -fno-strict-aliasing -fwrapv` — во **все цели и на все платформы** (Android arm64, десктоп Linux x86_64 и aarch64, CI, инструменты, которые исполняют код логики). Дополнительно сохраняем `-fno-delete-null-pointer-checks` из форка.
- Вносит Android-платформа в `CMakePresets.json` (общий базовый пресет), ревью Лида. Флаги остаются и в `cmake/compilers.cmake` форка; при слиянии с MYSOREZ/TSH их нельзя потерять.
- Запрещены для всех целей: `-ffast-math`, `-Ofast`, `-funsafe-math-optimizations`, `-ffinite-math-only`, `-fassociative-math`, `-freciprocal-math`, `-march=native`, `-mfpmath=387`. На x86_64 — только SSE2 по умолчанию, без `-mfma`.
- Почему: clang по умолчанию `-ffp-contract=on` и на AArch64 (FMA есть всегда) склеивает `a*b+c` в `fmadd`, а на x86_64 без `-mfma` — нет. Последний бит различается, и CRC расходится.
- Проверка: CI читает `compile_commands.json` и падает, если у файла логики нет `-ffp-contract=off` или есть запрещённый флаг.

### 2.2 Детерминированная математика (решение Лида п. 2)
- `SAGE_USE_DETERMINISTIC_MATH=ON` с Э1 **во всех целях**.
- Так как GameMath в форке нет (§1 п. 4), Геймплей **делает её сам** как библиотеку `GameMath` (C, fdlibm/musl-подобная float-реализация `sinf/cosf/tanf/atan2f/atanf/acosf/asinf/powf/expf/logf`, лицензия совместимая с GPL v3) и подключает локально вместо `FetchContent` из TSH. Лицензию и источник кода утверждает Лид. Если к тому времени TSH опубликует свою GameMath, сравниваем и берём её (в ADR-0001 §1.1 она связана с TSH PR #2670; что именно в этом PR, я не проверял).
- Переводим на GameMath **только логику** (GameLogic и всё, что она вызывает):
  - `GeneralsMD/Code/GameEngine/Source/Common/System/Trig.cpp`: `Sin/Cos/Tan/ACos/ASin` (сейчас `sinf/cosf/acosf/asinf`);
  - `Core/Libraries/Source/WWVegas/WWMath/wwmath.h`: `WWMath::Sin/Cos/Atan/Atan2/Acos/Asin` и обёртки `SinTrig/CosTrig/TanTrig`;
  - `GeneralsMD/Code/GameEngine/Source/GameLogic/Object/Update/PhysicsUpdate.cpp:737` (`atan2(xvec.Z, xy)`);
  - остальные прямые вызовы libm в `GameLogic/` и в общих заголовках, которые она включает, находит CI-греп (§2.5).
- Рендер и клиент (`GameClient`, `W3DDevice`, частицы, звук) остаются на libm ради скорости. Так как `wwmath.h` общий, разделяем вызовы явно: логике — `GameMath::*`/`WWMath::Det*`, клиенту — прежние `WWMath::*`. Подмены макросом для всего файла не делаем.
- Корректно округляемые по IEEE операции (`+ - * /`, `sqrtf`, `floorf/ceilf/truncf`, `fabsf`, `fminf/fmaxf`) остаются как есть.
- Таблицы `_FastSinTable` и прочие (заполняются в `WWMath::Init()`) в логике не используем, либо заполняем через GameMath.
- Владелец: Геймплей. Проверка: E1-45 вместе с QA (CRC x86_64 = arm64 на реплее ZH при ON).

### 2.3 Плавающая точка в потоке логики: FTZ и округление (решение Лида п. 7)
- Регистры FPCR (arm64) и MXCSR (x86_64) у каждого потока свои, важен только **поток логики**.
- В начало каждого тика логики (`GameLogic::update()`, `GeneralsMD/Code/GameEngine/Source/GameLogic/System/GameLogic.cpp:3679`, одна точка с `// BFME:`) Геймплей добавляет проверку:
  - arm64: `FPCR.FZ = 0`, `FPCR.FZ16 = 0`, `FPCR.DN` — как при старте, режим округления `RMode = 00` (к ближайшему);
  - x86_64: `MXCSR.FTZ = 0`, `MXCSR.DAZ = 0`, `RC = 00`;
  - **debug:** `DEBUG_ASSERTCRASH` при отклонении;
  - **release:** принудительная установка эталонного значения и счётчик срабатываний (в лог и в отчёт о рассинхроне NET-DESIGN §3.2).
- Эталон снимается один раз при старте потока логики до первой симуляции.
- Android-платформа проверяет, не меняют ли FPCR SDL3, звук (OpenAL/MiniAudio) и драйвер GPU в потоке логики. Остаток ловит QA реплеями.

### 2.4 Целые типы и float→int (решение Лида п. 8)
- В логике и сериализации запрещены `lroundf`, `lrintf`, `lround`, `lrint` и типы `long`/`unsigned long`: `long` — 32 бита на Windows и 64 бита на arm64/x86_64 Linux и Android.
- Известное место: `fast_float2long_round` в `Core/Libraries/Include/Lib/BaseFunctions.h` (`i = lroundf(f)`, возвращает `long`), на нём построены `REAL_TO_INT_CEIL/FLOOR` (например, `Core/GameEngine/Include/GameLogic/AIPathfind.h:493–494`).
- Вместо них — хелперы в GameMath с `int32_t`: `GameMath::roundToInt32`, `floorToInt32`, `ceilToInt32`, `truncToInt32` с определённым поведением на NaN, ±inf и выходе за диапазон (насыщение до `INT32_MIN/INT32_MAX`, NaN → 0, правило фиксируем в коде и тестах). `REAL_TO_INT_*` переводим на них (правка апстрима с `// BFME:`, ревью Лида).
- В `GeneralsMD/Code/GameEngine/Source/GameLogic` сейчас 37 вхождений слова `long` (подсчёт `rg -c`): их разбор и замена — Геймплей, ревью Лида.
- CI-греп по `GameLogic/`, `engine/bfme/` и включаемым ими общим заголовкам: `\b(lroundf|lrintf|lround|lrint)\b`, `\b(unsigned\s+)?long\b` (с явным списком исключений).

### 2.5 Общие правила для логики (из GAMEPLAY-GAP §4)
1. **RNG только `GameLogicRandomValue*`.** Генераторы ZH (`Core/GameEngine/Source/Common/RandomValue.cpp`): логический `theGameLogicSeed` (`GameLogicRandomValue/…Real`, макросы в `Core/GameEngine/Include/GameLogic/LogicRandomValue.h`), клиентский `theGameClientSeed` (`GameClientRandomValue`, `Core/GameEngine/Include/GameClient/ClientRandomValue.h`), звуковой `theGameAudioSeed`. Сид матча общий: `InitRandom(seed)` (`SkirmishGameOptionsMenu.cpp:444`, реплей `Common/Recorder.cpp:1257`, сеть `GameSpyGameInfo.cpp:562` `InitGameLogicRandom`). `InitRandom()` без аргумента (сид от `time(nullptr)`) в матче не вызывается. Запрещены `rand()`, `std::random_device`, `std::mt19937` и `GameClientRandomValue` в коде, который меняет состояние логики. Число вызовов логического RNG не зависит от клиента (видимость, камера, звук, локальный игрок). `GameLogicRandomValueUnchanged` в логике BFME не используем.
2. **Без `unordered_*` и `hash_map` в логике**, без обхода контейнеров, упорядоченных по адресам указателей. Ключи — `ObjectID`, `NameKeyType`, индекс INI. Сейчас в логике ZH `unordered_*`/`hash_map` не найдено. Есть `AttackPriorityMap = std::map<const ThingTemplate*, Int>` (`GeneralsMD/Code/GameEngine/Include/GameLogic/ScriptEngine.h:102`), в логике только `find` (`ScriptEngine.cpp:183`) — допустимо. Обход идёт в отладочном дампе (`:200`) и в `xfer` (`:265`, `:284`): порядок записи зависит от адресов, поэтому сейвы и дамп `XferDeepCRC` (NET-DESIGN §3.2 п. 2) у двух машин могут различаться без настоящего рассинхрона. Для дампа сортируем по имени шаблона (правка `// BFME:`, ревью Лида).
3. **Без времени стены в логике:** только номер кадра. `timeGetTime()` в `GameLogic.cpp` (строки 1866, 1962, 4613–4654) относится к загрузке и таймаутам прогресса, не к симуляции. Запрещены `time()`, `clock()`, `std::chrono`, `timeGetTime` в логике.
4. **Ничья по расстоянию/приоритету** разрешается по `ObjectID`. Порядок суммирования float (модификаторы, урон) фиксирован.
5. **Каждый модуль логики BFME** реализует `crc()` и `xfer()` со всем состоянием.
6. **Парсинг INI** без зависимости от локали (`std::from_chars`, решение Форматов).
7. **Проверка «18 вызовов» клиентского RNG в логических файлах** (`GeneralsMD/Code/GameEngine/Source/GameLogic`, `rg GameClientRandomValue` 02.10.2026). `rg` даёт 18 строк, но одна из них — комментарий (`ActiveBody.cpp:1025`), так что вызовов 17. Первичная классификация Геймплея по соседнему коду: 14 вызовов только визуальные, 3 требуют проверки (`StealthUpdate.cpp:136`, `SpectreGunshipUpdate.cpp:661–662`). QA подтверждает реплеем, а CI-греп следит, чтобы новых не появилось:

| Файл:строка | Что | Оценка |
|---|---|---|
| `Object/Body/ActiveBody.cpp:1026` | кость для частиц урона (комментарий о переводе на клиентский RNG из-за рассинхрона, 2003) | визуал, безопасно |
| `Object/Update/AIUpdate/ChinookAIUpdate.cpp:1199` | частицы потока от винта | визуал |
| `Object/Update/AIUpdate/HackInternetAIUpdate.cpp:563–564` | позиция текста денег | визуал |
| `Object/Update/AutoDepositUpdate.cpp:190–191` | позиция текста денег | визуал (в BFME1 17 подключений) |
| `Object/Update/StealthUpdate.cpp:136` | фаза пульсации | визуал, проверить, не влияет ли фаза на обнаружение (в BFME1 26 подключений) |
| `Object/Update/StructureCollapseUpdate.cpp:188, 216` | дрожание при обрушении | визуал: `shudder` идёт только в матрицу `Drawable` (`getInstanceMatrix`, строка 190), позиция объекта не меняется (в BFME1 241 подключение) |
| `Object/Update/StructureToppleUpdate.cpp:173, 223, 271` | кадр следующей «вспышки», дальше только `FXList::doFXPos` (`doToppleDelayBurstFX`) | визуал; но `m_nextBurstFrame` пишется в `xfer` (строка 610), так что сейвы между игроками различаются; в `crc()` не входит (в BFME1 2 подключения) |
| `Object/Update/SpectreGunshipUpdate.cpp:661–662` | точка попадания гатлинга | **проверить**: если по этой точке наносится урон, это рассинхрон (в BFME1 не используется) |
| `Object/Update/EMPUpdate.cpp:324, 341` | высота и задержка частиц | визуал |
| `Object/Update/ParticleUplinkCannonUpdate.cpp:688` | тип подпалины («client fluff») | визуал |

## 3. Варианты, которые не выбраны
- **Fixed-point для всей логики:** XL-переписывание ZH, не берём (§1 п. 3).
- **libm платформы + «одна сборка arm64»:** не закрывает CI на x86_64, а bionic на телефонах с разными версиями Android может отличаться **[разницу между версиями bionic не проверял]**.
- **Подмена `sinf` и др. макросом во всём бинаре:** замедлит рендер и частицы; Лид решил делить на логику и клиент.

## 4. Последствия
- Задачи: Геймплей — своя GameMath, перевод `Trig.cpp`/`wwmath.h`/`PhysicsUpdate.cpp:737`, хелперы `int32_t`, проверка FPCR/MXCSR, CI-греп; Android-платформа — флаги в `CMakePresets.json`, проверка FTZ; QA — E1-45 и реплей-тесты с Э2.
- Все правки апстримных файлов помечаются `// BFME:` и записываются в `docs/UPSTREAM-PATCHES.md` (ADR-0001 §3).
- Любой cherry-pick из TSH, затрагивающий математику, порядок обновления или RNG, идёт через QA (ADR-0001 §5).
- Совместимость с ретейлом ZH и с GeneralsOnline мы теряем (у них своя математика), это не цель проекта.
- GameMath медленнее CRT. Стоимость на Pixel 8 Pro измеряет QA вместе с бюджетом тика логики 10 мс p95 (решение Лида п. 5 в GAMEPLAY-GAP §2a).

## 5. Открытые вопросы
1. Источник и лицензия кода GameMath (fdlibm — Sun, разрешительная лицензия; musl — MIT; CORE-MATH — MIT/CeCILL?) — решает Лид **[лицензии не проверены]**.
2. Где хранить GameMath: `engine/bfme/` или `Core/Libraries/Source/GameMath` (общая для движка)? Это затрагивает границы владения ADR-0001 §3.
3. Что делать с `SpectreGunshipUpdate.cpp:661` и `StealthUpdate.cpp:136`, если QA подтвердит влияние на логику: переводить на логический RNG (меняется CRC относительно ZH, для нас допустимо).
4. Политика на рассинхрон (стоп или продолжение) — NET-DESIGN §3.2 п. 1, решает Лид.
