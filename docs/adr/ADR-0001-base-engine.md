# ADR-0001: основа движка BFME1 Mobile

- **Статус:** Принято, 02.10.2026
- **Автор:** Лид движка (c5172cc3-b570-4f73-8762-66fbfe36e726)
- **Связано:** PLAN.md §1, §5; RESEARCH.md §3, §6; ADR-0002-target-devices.md; ADR-0003-determinism.md (черновик Геймплея); docs/E1-BACKLOG.md; docs/formats/BFME1_vs_ZH.md; [docs/UPSTREAM-PATCHES.md](../UPSTREAM-PATCHES.md) (реестр наших правок в коде апстрима; файл приходит с PR E1-21)
- **Изменено:** 02.10.2026 после ревью Android-платформы и Форматов; 02.10.2026, раунд 2: детерминизм, реестр модулей, решения Оркестратора (разделы в конце).
- **Источник фактов о форке:** удалённое чтение `gh api` 02.10.2026 16:45–17:30 МСК (аккаунт KatoteKiru). Ссылки на файлы ведут на коммит `8ef2349`: `B = https://github.com/MYSOREZ/GeneralsZH-Android-Port/blob/8ef2349`.

---

## 1. Контекст

1. Нужна Android-версия BFME1 (баланс Patch 2.22) для узкого круга: телефон, сенсор, сеть со своим сервером. Ресурсы игры пользователь кладёт сам.
2. Исходников BFME EA не публиковала. 27.02.2025 EA выложила код C&C Generals / Zero Hour под GPL v3 с дополнительными условиями: прав на товарные знаки нет (RESEARCH §3, https://github.com/electronicarts/CnC_Generals_Zero_Hour). BFME1 сделан на доработанном движке SAGE из Generals. Общие части: WW3D/W3D, BIG, INI, GameLogic, модули, AI, pathfinding, lockstep. Различаются орды, слоты застройки, APT-интерфейс и версии форматов карт и моделей (RESEARCH §3, Revora). **[Объём различий не измерен: это главный риск Э1.]**
3. Generals уже запускается на Android нативно. Самый полный порт сейчас — MYSOREZ/GeneralsZH-Android-Port (RESEARCH §3).
4. OpenSAGE (C#, GPL-3.0; на код, перенесённый из Generals, у него действуют дополнительные условия EA из `LICENSE-EA.md`) разбирает все INI, map и W3D из BFME1, но геймплея и Android в нём нет. С декабря 2025 в master коммитит только dependabot (RESEARCH §2).
5. Правила команды: ресурсы EA никогда не попадают в git, APK, на сервер или в открытый доступ. Open-BFME-1 не клонируем и не используем (TEAM.md).

### 1.1 Факты о базовом форке MYSOREZ/GeneralsZH-Android-Port (gh api, 02.10.2026)

| Параметр | Значение | Источник |
|---|---|---|
| Видимость, статус | публичный, не архивирован; создан 06.07.2026 21:28 МСК, последний push 02.10.2026 16:27 МСК | `gh api repos/MYSOREZ/GeneralsZH-Android-Port` |
| **Родитель на GitHub** | `fork: true`, `parent` = `source` = **ammaarreshi/Generals-Mac-iOS-iPad** (порт для macOS и iOS, 1645 звёзд, на GitHub сам не форк). GeneralsX и TheSuperHackers в сеть форков GitHub **не входят**. Цепочка по README: EA → TheSuperHackers/GeneralsGameCode → Fighter19 → fbraz3/GeneralsX → ammaarreshi → MYSOREZ | `.parent`, `.source`; B/README.md «Lineage & credits» |
| Расхождение с родителем | на 1116 коммитов впереди ammaarreshi/main, позади на 0; база слияния `c5c8c4d` от 05.07.2026 20:23 МСК | `gh api repos/ammaarreshi/Generals-Mac-iOS-iPad/compare/main...MYSOREZ:GeneralsZH-Android-Port:main` |
| Корень истории | `2e338c0` «Initial commit» (LFeenanEA, 27.02.2025 19:34 МСК), то есть полная история EA и TheSuperHackers | `gh api .../commits?page=3119&per_page=1` |
| Коммитов в `main` | **3119** (по пагинации commits API на `8ef2349`). У Оркестратора и платформы фигурирует 3319: эту цифру я не воспроизвёл, она может включать другие ветки **[сверить после клона]** | `Link: …page=3119; rel="last"` |
| Последний коммит `main` | `8ef2349` от 28.09.2026 00:39 МСК, «docs(readme): support the project with USDT donations» | `gh api .../commits` |
| Ветки | `main`, `updates` и 8 веток `claude/*` (audio-fixes, native-touch-input, network-diagnostics, opengl-mobile-optimization и др.) | `gh api .../branches` |
| Релизы | v1.3.0 (27.09.2026 23:03 МСК), v1.2.2, v1.2.1, v1.2.0, v1.1.5. APK в релизе называется `GeneralsXZH-android-v1.3.0.apk`, рядом символы `libmain` и `libmain60` | `gh api .../releases` |
| **Размер** | `size` = 14 967 970 КБ ≈ **14,3 ГиБ** (≈15 ГБ). У родителя ammaarreshi 45 МБ | `.size` |
| Почему такой большой | в `apk/` коммитят собранные APK: **395 коммитов** меняют `apk/`, сейчас там лежит `GeneralsXZH-android-textsize2.apk` на 62,9 МБ. По подсчёту платформы это около 160 разных APK **[не сверял]**. Рабочее дерево `main` без истории весит ~521 МБ, из них 356 МБ занимает `GeneralsOnlineRelease/` | `gh api .../commits?path=apk`, `git/trees/main?recursive=1` |
| `GeneralsOnlineRelease/` | **сторонние закрытые бинарники**, снимок Windows-установки клиента GeneralsOnline: `EAC_LaunchGeneralsOnline.exe`, каталог `EasyAntiCheat/`, `EOSSDK-Win32-Shipping.dll`, `GeneralsOnlineZH*.exe`, MSI-пакеты среды .NET 10 и сама среда, `Users/AntiKeks/.../Maps/*` (карты сообщества). Итого 484 dll, 9 exe, 8 msi. Добавлены коммитом `7847eb0` от 10.07.2026 | дерево `main` |
| Сабмодули (6) | `GeneralsReplays` (fbraz3/GeneralsXReplays), `references/fbraz3-dxvk` (ветка generalsx-macos-v2.6, **нужен для сборки**: `SAGE_DXVK_USE_LOCAL_FORK=ON`), `references/thesuperhackers-main`, `references/fighter19-dxvk-port`, `references/jmarshall-win64-modern`, `references/generals-online-client` (только как справочник) | B/.gitmodules |
| Лицензия | `LICENSE.md`: GPL v3 и «ADDITIONAL TERMS per GNU GPL Section 7» от EA (прав на товарные знаки нет). Поэтому GitHub показывает `NOASSERTION/Other` | `gh api .../license`, B/LICENSE.md |
| Языки | C++ 58,7 МБ, C 1,6 МБ, CMake 0,5 МБ, Java 0,4 МБ | `gh api .../languages` |
| Сборка Android | Gradle + AGP 8.5.2. Нативная часть собирается CMake-пресетом `android-vulkan` **вне Gradle** (Ninja, vcpkg, NDK chainload, `arm64-v8a`, `android-28`, `c++_shared`, `RTS_BUILD_GENERALS=OFF`, то есть только ZH). CI на `ubuntu-latest` с NDK `27.2.12479018` и JDK 17. Docker-файла для Android **нет**: `resources/dockerbuild/Dockerfile` предназначен для VC6 под wine | B/android/*.gradle, B/CMakePresets.json, B/.github/workflows/build-android.yml#L78, B/resources/dockerbuild/Dockerfile |
| Платформа | SDL3 3.4.2 (FetchContent) + SDLActivity, OpenAL Soft, FFmpeg 8.1.1, FreeType, GameNetworkingSockets 1.6.0 (vcpkg) | B/cmake/sdl3.cmake#L22, B/vcpkg.json |
| Рендер | по умолчанию свой D3D8→GLES 3.0 `Core/Libraries/Source/d3d8gles`, портированный из Lolendor/Generals-WebAssembly. **Собирается только под Android** (`if(NOT ANDROID) return()`). Через явный выбор доступны ANGLE (готовый `libGLESv2_angle.so` лежит в репо) и Vulkan через DXVK 2.6 | B/Core/Libraries/Source/d3d8gles/CMakeLists.txt, d3d8gles.cpp#L96-L126 |
| Десктоп | пресет `linux64-deploy`: DXVK/SDL3/OpenAL, рендер **через DXVK→Vulkan, не через d3d8gles** | B/CMakePresets.json |
| BIG | `StdBIGFileSystem` принимает только сигнатуру `"BIGF"`, а у BFME1 `BIG4` (наши `ini.big` и `maps.big`). В заголовке `BIG4` поле размера архива little-endian, число файлов и смещение первого файла big-endian. Все 115 карт в `maps.big` сжаты RefPack (`EAR\0`) (Форматы, `tools/oracle`, E1-20). RefPack распаковывает штатный `CompressionManager` ZH, свой не нужен: все 115 карт распаковываются побайтно как в OpenSAGE. `BIG4` принимается правкой `StdBIGFileSystem.cpp` `openArchiveFile` (+6/−1, `// BFME:`), наш слой — `formats/vfs/` (E1-21 реализована, на ревью; подключение в игре — E1-21b) | B/Core/GameEngineDevice/Source/StdDevice/Common/StdBIGFileSystem.cpp#L58 |
| Детерминизм | **детерминированной математики в форке нет.** `cmake/gamemath.cmake` (опция `SAGE_USE_DETERMINISTIC_MATH`, по умолчанию OFF) подтягивает через FetchContent каталог `Core/GameMath` из TSH на коммите `838f9d0f` (PR #2670), но такого каталога там нет. В `wwmath.h` ветки `USE_DETERMINISTIC_MATH` — заглушки `TODO` с откатом на CRT. Кросс-платформенный детерминизм с Windows «remains unverified». Наше решение — §3 п. 9 | B/cmake/gamemath.cmake, B/Core/Libraries/Source/WWVegas/WWMath/wwmath.h, B/docs/port/ANDROID_PORT.md §6; ADR-0003 §1 п. 4 |
| Разработка | коммиты в основном от «Claude» (Claude Code), всё ещё активна: 25 звёзд, 20 форков, 16 открытых issue | `gh api` |

Локальный клон `src/GeneralsZH-Android-Port` на боксе неглубокий (`depth 1`, HEAD `8ef2349`). Для импорта с историей он не подходит.

## 2. Рассмотренные варианты

### (а) OpenSAGE + .NET for Android + Veldrid
- **Плюсы:** форматы BFME1 уже разбираются (все INI, map, W3D, APT частично), карты грузятся и рисуются. GPL v3. Хорошие инструменты (data viewer, BigEditor).
- **Минусы:** геймплея BFME нет, геймплей Generals недописан (pathfinding в WIP-ветке). Проект фактически заморожен с весны 2025. Под Android не собирался: десктопный GL-бэкенд требует GL 4.3, шейдеры собираются через glslangValidator только для x64, окно на SDL2, Mono или экспериментальный NativeAOT, сборщик мусора в RTS. Lockstep на float в C# пришлось бы делать с нуля.
- **Срок до прототипа:** 6–12 месяцев **[оценка]** (RESEARCH §6).

### (б) Движок Generals/ZH (C++, GPL v3) в Android-порте MYSOREZ + свой слой BFME1 — **выбран**
- **Плюсы:** Android-платформа готова (SDL3, свой GLES 3.0 и опционально Vulkan, сенсорное управление, OpenAL, обработка крашей, лаунчер). По заявлению форка GLES работает «on every device tested so far» (B/README.md). Есть настоящая логика SAGE: модули, AI, pathfinding, lockstep, CRC, реплеи. Есть транспорт GNS с ICE. BFME вырос из этого же кода. Код OpenSAGE в движок не переносим (§3 п. 4), он нужен только как оракул и справочник.
- **Минусы:** весь слой BFME пишем сами: орды, слоты, стены, герои, силы, кольцо, новые модули INI, а также различия форматов BIG4, map, W3D и террейна. Совместимости с ретейлом и реплеями ПК не будет. Кодовая база 1,6 млн строк, это форк форка с историей ~15 ГБ. d3d8gles есть только под Android, поэтому десктопная сборка рисует другим рендером.
- **Срок до прототипа:** 3–5 месяцев **[оценка]**.

### (б′) fadi-labib/Generals-Android — запасной вариант (б)
- **Плюсы:** тоже основан на GeneralsX, скирмиш против AI работает, есть опыт перекодировки текстур (TexturesZH.big в RGBA8).
- **Минусы:** рендер только через Vulkan с Turnip, то есть только Adreno 6xx/7xx. Для Pixel (Mali и PowerVR) не подходит без своего GLES-рендера (RESEARCH §3).
- **Когда переходим:** MYSOREZ закрыт, сменил лицензию или заброшен (см. §7), при этом d3d8gles переносим к себе.

### (б″) Взять TheSuperHackers/GeneralsGameCode или GeneralsX напрямую и написать Android-слой самим
- **Плюсы:** чище апстрим, меньше история, нет сторонних бинарников.
- **Минусы:** придётся повторить то, что MYSOREZ уже сделал за 3 месяца: SDLActivity-оболочку, d3d8gles, сенсорное управление, жизненный цикл, хранилище, обработку крашей. Минус 4–8 недель **[оценка]**. Отклонён. TSH держим как источник точечных исправлений.

### (в) Свой движок, читающий форматы SAGE
- 12–24 месяца до прототипа **[оценка]**. Отклонён (RESEARCH §6).

### (г) Open-BFME-1 (декомпиляция `lotrbfme.exe`)
- В репозитории лежат ретейл-бинарники EA, сама декомпиляция юридически серая. **Не клонируем и не используем** (TEAM.md). Пересмотр возможен только по решению Андрея и только как справочник по поведению.

### Наблюдаем: LibreSAGE (feliwir)
- «BFME… being worked on», но кода BFME в дереве пока нет (RESEARCH §3). Раз в месяц проверяем, нет ли форматов BFME, которые можно перенести.

## 3. Решение

1. **Основа:** движок Generals/ZH под GPL v3 в Android-порте **MYSOREZ/GeneralsZH-Android-Port**, импортированный в приватный **KatoteKiru/bfme-mobile** с полной историей.
2. **Ветки:** `main` остаётся зеркалом `MYSOREZ/main` (только fast-forward, своих коммитов там нет). `bfme` — наша интеграционная ветка, работа идёт в ветках задач с PR в `bfme` и ревью владельца области.
3. **Наш код лежит в отдельных каталогах:**

| Каталог | Владелец | Содержимое |
|---|---|---|
| `engine/bfme/` | Геймплей (архитектура: Лид) | модули BFME, регистрация в фабриках движка |
| `formats/` | Форматы и данные | BIG4, INI-схемы BFME1, map, W3D, STR/CSF, перекодировка текстур |
| `net/` | Сеть и сервер | транспорт, run-ahead, реконнект |
| `server/` | Сеть и сервер | сервер (Go, см. ADR-NET-001) |
| `ui/` | Мобильный UI | HUD, меню, жесты |
| `tests/` | QA | реплей-тесты, CRC, скриншот-тесты, профилирование |
| `tools/` | Форматы и данные, QA | `datacheck`, утилиты |
| `docs/adr/` | Лид | решения |
| `android/`, `ci/`, `docker/` | Android-платформа | оболочка, CI, образ сборки |

   Правки в коде апстрима (`Core/`, `GeneralsMD/`, `cmake/`) допускаются только минимальные: точки расширения, хуки, `#if`. Каждая правка помечается `// BFME:` и вносится в [`docs/UPSTREAM-PATCHES.md`](../UPSTREAM-PATCHES.md) (файл, причина, как убрать; приходит с PR E1-21). Любая правка апстрима проходит ревью Лида, в том числе в областях других владельцев: например, `ModifierList` и weapon nuggets принадлежат Геймплею и подключаются через хуки `// BFME:`.
4. **Схемы INI и форматы BFME1 описываем и реализуем сами** (C++, наш код в `formats/`). **Код OpenSAGE в движок не переносим:** OpenSAGE распространяется под GPL-3.0, и на код, взятый из Generals, у него есть дополнительные условия EA (`LICENSE-EA.md`). OpenSAGE используем только как **внешний оракул** для сверки (`tools/oracle/` на OpenSAGE `588ac47`, .NET 8, без GPU, E1-20) и как **справочник**: в документах и комментариях ссылаемся на его файлы как на источник сведений о формате. Что читать: `src/OpenSage.FileFormats.Big/BigArchive.cs` (BIGF и BIG4), `src/OpenSage.Game/Data/Map/{MapFile,HeightMapData,BlendTileData}.cs`, `src/OpenSage.FileFormats.W3d/`, `src/OpenSage.Game/Data/Ini/`. Эталон для C++-реализации `datacheck` — Python-прототип `tools/datacheck` от Форматов. Различия форматов BFME1 и ZH сведены в `docs/formats/BFME1_vs_ZH.md`.
5. **Импорт истории без переписывания** (решение Оркестратора, 02.10.2026). Смысл: сохранить общую базу с апстримом для обычных `git merge`.
   - История ~14,3 ГиБ превышает лимит одного push на GitHub в 2 ГиБ, поэтому пушим **порциями** по диапазонам коммитов, каждая заведомо < 2 ГиБ (https://docs.github.com/en/get-started/using-git/troubleshooting-the-2-gb-push-limit).
   - Риск: GitHub рекомендует держать репозиторий на диске в пределах 10 ГБ, крайне желательно < 5 ГБ, а при чрезмерной нагрузке поддержка может потребовать исправлений (https://docs.github.com/en/repositories/creating-and-managing-repositories/repository-limits). Наши ~14 ГиБ выше этой рекомендации, см. §7.
   - В `bfme` (обычными коммитами, без переписывания) удаляем `GeneralsOnlineRelease/`, `apk/*.apk` и `android/app/debug.keystore`. В `.gitignore` добавляем `*.apk`, `*.aab`, `*.keystore`, `*.jks`, `*.big`, `gamedata/`. В CI ставим проверку запрещённых файлов (`.big`, `.apk`, `.keystore`, `.exe`, `.dll`, `.msi`).
6. **Ресурсы EA** (BIG, W3D, DDS/TGA, map, STR/CSF и производные от них, включая перекодированные текстуры и кэши) **никогда не попадают в git, APK, на сервер или в открытый доступ**. В APK можно класть только метаданные: манифест имён, размеров и хешей (`docs/game-files-access.md` §4).
7. **Интерфейс APT не переносим**, делаем свой мобильный HUD (PLAN §1).
8. **Точка «идём или откатываемся»** стоит в конце Э1 (≤ 5 недель от старта Э1). Если карта BFME1 не открывается на Pixel 8 Pro по критериям Э1 из PLAN §4, откатываемся на **путь (а)** OpenSAGE + .NET Android. Решение фиксируем в ADR-0004 (ADR-0003 занят под детерминизм логики).
9. **Детерминированная математика — своя** (решение Лида 02.10.2026, правила в ADR-0003, задача E1-49). Пишем GameMath на libm из **musl** (MIT, совместима с GPL v3) в `Core/Libraries/Source/GameMath/`. `cmake/gamemath.cmake` переводим с FetchContent на `add_subdirectory`. Пока GameMath не готова, опция `SAGE_USE_DETERMINISTIC_MATH` остаётся **OFF**: FetchContent форка ссылается на несуществующий `Core/GameMath` TSH. Флаги компилятора `-ffp-contract=off -fno-strict-aliasing -fwrapv` действуют сразу: они уже стоят в `cmake/compilers.cmake` форка, E1-10 проверяет их в Android-сборке. Оценка E1-49 — 5–7 р. д. Каталог лежит в дереве апстрима, поэтому это исключение из п. 3: правку `gamemath.cmake` и новый каталог вносим в [`docs/UPSTREAM-PATCHES.md`](../UPSTREAM-PATCHES.md) с пометкой `// BFME:`.
10. **Реестры модулей BFME** (решение Лида 02.10.2026, E1-50). Логические модули: явный реестр всех 123 имён через `BfmeModuleRegistry::registerAll` из `ModuleFactory::init` (код в `engine/bfme/`, владелец Геймплей). Дубли имён ломают сборку, намеренная замена модуля ZH только через явный список `overrides`, мягкий режим `BFME_SOFT_MODULES` только в dev. Модули отрисовки получают **отдельный реестр в `GameEngineDevice`, владелец Лид**; в Э1 они мапятся на `W3DModelDraw` (E1-34).

## 4. Последствия

- **Плюсы:** Э1 сводится к форматам и рендеру террейна поверх уже работающей платформы. Логика, lockstep и реплеи достаются сразу.
- **Обязательства по GPL v3:** тем, кому передаём APK, даём исходники. Сохраняем `LICENSE.md` с дополнительными условиями EA, а для кода из musl в GameMath — уведомление MIT. В названии и иконке не используем товарные знаки EA и Tolkien. README пишем без логотипов.
- **Клон весит ~15 ГБ.** В CI и Docker используем `--filter=blob:none` или `--depth` для сборочных задач. Полный клон нужен только для слияний с апстримом, его держим на боксе в `/workspace/bfme-mobile/repo` **[оценка: 15 ГБ из 84 ГБ свободных]**.
- **Сторонние бинарники и данные апстрима** (GeneralsOnlineRelease, ANGLE prebuilt `.so`, `GroupPanel.wnd`) проходят проверку происхождения. ANGLE (BSD) оставляем как диагностический бэкенд, `GroupPanel.wnd` проверяют «Форматы и данные» (E1-BACKLOG).
- **Код GeneralsOnline** (`GeneralsMD/Code/GameEngine/.../GeneralsOnline`, NGMP) по умолчанию не считаем пригодным, пока «Сеть и сервер» не проверят его лицензию (E1-BACKLOG). Наша сеть строится на транспорте GNS и своём сервере (NET-DESIGN.md).
- **Десктоп и телефон рисуют разными бэкендами** (DXVK против d3d8gles). Чтобы сверка «десктоп = телефон» была честной, переносим d3d8gles на Linux через EGL/Mesa (E1-17, идёт до рендера террейна E1-32). Mesa не повторяет Mali (llvmpipe объявляет S3TC, `mediump` считается в fp32), поэтому в CI включён принудительный CPU-декод текстур, а точность шейдеров проверяем только на устройстве (ADR-0002 §10).

## 5. Политика слияний с апстримом

| Апстрим | Remote | Как вливаем |
|---|---|---|
| MYSOREZ/GeneralsZH-Android-Port `main` | `mysorez` | основной. `git fetch mysorez`, затем fast-forward нашего `main`, затем `git merge --no-ff main` в `bfme` |
| TheSuperHackers/GeneralsGameCode | `tsh` | только `cherry-pick -x` точечных исправлений (детерминизм, падения) с пометкой в `UPSTREAM-PATCHES.md` |
| fbraz3/GeneralsX, ammaarreshi | `gx`, `ammaar` | только для чтения |

Правила:
1. **Частота:** раз в 2 недели **[оценка]**, а также внепланово при важном исправлении (рендер на Mali/PowerVR, падения, безопасность). Во время стабилизации перед точкой «идём или откатываемся» слияния замораживаем.
2. **Слияние делает Лид движка** в ветке `merge/mysorez-YYYYMMDD`, PR в `bfme`. Rebase и force-push в `bfme` и `main` запрещены.
3. **Перед слиянием** CI должен пройти: сборка APK и десктопная сборка, `datacheck` по INI (с Э1), скриншот-тест карты (с M1), реплей-тесты CRC (с Э2). Если апстрим меняет детерминизм (математику, порядок обновления), задача уходит в QA до слияния.
4. **Конфликты** решаем в пользу апстрима, наши правки переносим в точки расширения. Наша правка в файле апстрима обязана иметь строку в `UPSTREAM-PATCHES.md` (файл, причина, как убрать).
5. **Пути, которые при слиянии не возвращаем в `bfme`:** `apk/`, `GeneralsOnlineRelease/`, `android/app/debug.keystore` (удаляем повторно, в CI это проверяется).
6. **Наши исправления в публичный апстрим** не отправляем без согласия Андрея (это публичное действие). Репозиторий приватный.

## 6. Что сделано и что дальше (Э0)
- [ ] Создать `KatoteKiru/bfme-mobile` (private) и пушить историю порциями.
- [ ] Создать ветку `bfme`, README, LICENSE (GPL v3 + доп. условия EA из апстрима), каталоги из §3, CODEOWNERS.
- [ ] Почистить `bfme`, добавить `.gitignore` и CI-проверку запрещённых файлов.
- Подробности и критерии готовности: `docs/E1-BACKLOG.md` (E1-01…E1-05).

## 7. Критерии пересмотра

ADR пересматривается, если наступит одно из событий ниже:
1. **Точка «идём или откатываемся»** (конец Э1): не выполнен хотя бы один из критериев Э1 PLAN §4 (1–3) и нет плана закрыть его за ≤ 1 неделю **[оценка]**. Тогда откат на путь (а).
2. **Расхождения форматов:** чтобы рендерить террейн BFME1, приходится переписать больше половины `W3DDevice/GameClient` (HeightMap, TerrainTex, W3DWater, W3DRoadBuffer) **[порог-оценка]**. Тогда сравниваем с (а) и OpenSAGE-рендером.
3. **Апстрим:** MYSOREZ без коммитов > 60 дней, сменил лицензию, закрыл репозиторий или влил то, что ломает GLES на Mali/PowerVR без исправления > 30 дней. Тогда переходим на (б′) или (б″), перенеся d3d8gles к себе.
4. **GitHub ограничил репозиторий из-за размера.** Решение Оркестратора 02.10.2026: историю (~14,4 ГБ) оставляем, `filter-repo` не делаем. Если GitHub всё же ограничит репозиторий, вариант с `git filter-repo` (удаление `apk/` и `GeneralsOnlineRelease/` с той же детерминированной фильтрацией каждого нового снимка апстрима **[не проверено]**) снова выносится на решение Оркестратора.
5. **Open-BFME-1** собирается полностью и юридический вопрос решён (решение Андрея). Тогда пересматриваем его роль как справочника.

---

## Изменения 02.10.2026 (после ревью Android-платформы и Форматов)

1. **§3 п. 4 переписан.** Было: «Схемы INI и форматы BFME1 переносим из OpenSAGE (C#→C++)». Стало: схемы и форматы описываем и реализуем сами, код OpenSAGE в движок не переносим. Причина (Форматы): OpenSAGE под GPL-3.0, а не LGPL, и на код, взятый из Generals, у него есть дополнительные условия EA (`LICENSE-EA.md`). OpenSAGE остаётся внешним оракулом (`tools/oracle/`, E1-20 сделана) и справочником, ссылки на него как на источник сведений сохраняем. Эталон для `datacheck` — Python-прототип `tools/datacheck`.
2. **§1 п. 4 и §2 (б):** уточнена лицензия OpenSAGE. Фраза «Лицензии с OpenSAGE совместимы» заменена на «код OpenSAGE не переносим».
3. **§1.1, строка BIG:** добавлены факты Форматов: порядок байт в заголовке `BIG4`, сжатие RefPack (`EAR\0`) у всех 115 карт `maps.big`.
4. **§1.1, строка «Детерминизм»:** добавлена пометка «уточнить» со ссылкой на ADR-0003 §1 п. 4 (по проверке Геймплея готовой GameMath в форке нет).
5. **§3 п. 8:** решение «идём или откатываемся» фиксируем в **ADR-0004**, а не в ADR-0003. ADR-0003 теперь про детерминизм логики (черновик Геймплея).
6. **§4, последний пункт:** перенос d3d8gles на Linux (E1-17) стоит перед рендером террейна (E1-32). Добавлено правило про Mesa и Mali: в CI принудительный CPU-декод, точность шейдеров проверяем только на устройстве.
7. **Шапка:** добавлены ссылки на ADR-0003-determinism.md и docs/formats/BFME1_vs_ZH.md.

---

## Изменения 02.10.2026, раунд 2

Пункты 8–10 внесены в 18:00 МСК, пункты 11–16 — в 21:30 МСК.


8. **§1.1, строка «Детерминизм», переписана.** Утверждение «интегрирована GameMath на основе fdlibm» было неверным. Новый факт: `cmake/gamemath.cmake` подтягивает `Core/GameMath` из TSH `838f9d0f` (PR #2670), но такого каталога там нет, а в `wwmath.h` стоят заглушки `TODO`. Пометка «уточнить» из п. 4 снята.
9. **Новый §3 п. 9:** своя GameMath на libm из musl (MIT) в `Core/Libraries/Source/GameMath/`, `gamemath.cmake` переходит на `add_subdirectory`. `SAGE_USE_DETERMINISTIC_MATH` остаётся OFF до готовности GameMath, флаги компилятора включены сразу. Записано как исключение из §3 п. 3, потому что каталог лежит в дереве апстрима.
10. **§4:** в обязательства добавлено уведомление MIT для кода из musl.
11. **Шапка и §3 п. 3:** добавлена ссылка на [`docs/UPSTREAM-PATCHES.md`](../UPSTREAM-PATCHES.md). Сам файл приходит с PR E1-21 (в ветке `e1-21-vfs-big4` он уже есть), в этом коммите его нет.
12. **§3 п. 9 уточнён:** `SAGE_USE_DETERMINISTIC_MATH` остаётся OFF, потому что FetchContent форка ссылается на несуществующий `Core/GameMath` TSH. Флаги `-ffp-contract=off -fno-strict-aliasing -fwrapv` уже есть в `cmake/compilers.cmake`, E1-10 только проверяет, что они действуют в Android-сборке. E1-49 — 5–7 р. д. вместо 3–4.
13. **Новый §3 п. 10:** реестры модулей. Логика — `BfmeModuleRegistry::registerAll` из `ModuleFactory::init` на все 123 имени, дубли ломают сборку, замена только через `overrides`, `BFME_SOFT_MODULES` только в dev. Отрисовка — отдельный реестр в `GameEngineDevice` (владелец Лид), в Э1 мапится на `W3DModelDraw`.
14. **§3 п. 3:** любая правка апстрима проходит ревью Лида; `ModifierList` и weapon nuggets принадлежат Геймплею через хуки `// BFME:`.
15. **§1.1, строка BIG:** RefPack идёт через штатный `CompressionManager` ZH, проверено на всех 115 картах в E1-21. Правка `StdBIGFileSystem.cpp` +6/−1 для `BIG4` на ревью, подключение в игре — отдельный PR E1-21b.
16. **§7 п. 4:** решение Оркестратора 02.10.2026 — историю репозитория (~14,4 ГБ) оставляем, `filter-repo` не делаем.
