# Аудит FP-режима (FPCR на arm64, MXCSR/x87 на x86_64) в потоке GameLogic

- Автор: Android-платформа, 02.10.2026, первая версия (18:05 МСК). Для E1-10/E1-11, E1-45 и ADR-0003 (детерминизм).
- База: форк MYSOREZ `8ef2349` (`B = https://github.com/MYSOREZ/GeneralsZH-Android-Port/blob/8ef2349`), SDL 3.4.2, OpenAL Soft 1.24.2 (версия из `B/cmake/openal.cmake`), DXVK — сабмодуль `fbraz3/dxvk@46a3bc0` (Android) и `dxvk-native 2.6` (Linux), Mesa 25.0.7 (на боксе).
- Метод: (1) grep исходников на `fesetround|fesetenv|feholdexcept|fesetexceptflag|_MM_SET_FLUSH_ZERO_MODE|_MM_SET_DENORMALS_ZERO_MODE|_mm_setcsr|ldmxcsr|fpcr|_controlfp|fldcw`; (2) **сканирование машинного кода** arm64 `.so` на инструкции `MSR FPCR` / `MRS FPCR` / `MSR FPSR` (кодировки `0xD51B4400|Rt`, `0xD53B4400|Rt`, `0xD51B4420|Rt`) и на импорт `fe*` из libm; (3) дизассемблирование найденных мест. Скрипты: `tools/fpcr/fpcr_scan.py`, `tools/fpcr/fe_callsites.py`, `tools/fpcr/disrange.py`.
- Что просканировано: все 16 `.so` из APK форка (`apk/GeneralsXZH-android-textsize2.apk`), а также драйверы GPU из публичных наборов vendor-блобов LineageOS (TheMuppets): **Mali r54p1-11eac0** (Pixel 8 Pro, `proprietary_vendor_google_husky`, ветка lineage-23.2), **PowerVR «OpenGL ES 3.2 build 24.3@6660496»** (Pixel 10, `proprietary_vendor_google_frankel`, BP4A.260205.001), **Adreno V@0762.41** (OnePlus 12, `proprietary_vendor_oneplus_sm8650-common`, OOS 11.F.56). Блобы скачаны только для анализа и удалены с бокса, в репозиторий не попадают. Версия драйвера на телефоне Андрея может отличаться — повторить скан на его прошивке (§6, T4).

## 1. Итог

1. **Поток.** На Android вся игра идёт в одном потоке: Java-поток `SDLThread`, созданный SDL (`SDLActivity.java` L854–856, SDL 3.4.2) → `nativeRunMain` → `SDL_main` (`SDL3Main.cpp`) → `GameEngine::execute()`. В одном кадре `GameEngine::update()` (`B/GeneralsMD/Code/GameEngine/Source/Common/GameEngine.cpp` L1204–1270) последовательно вызывает `TheRadar`, `TheAudio->UPDATE()` (вызовы OpenAL), `TheGameClient->UPDATE()` (опрос событий SDL, отрисовка через d3d8gles/DXVK, `SDL_GL_SwapWindow`), `TheNetwork->UPDATE()`, online-сервисы, затем **`TheGameLogic->UPDATE()`**. Отдельного потока логики в форке нет (`std::thread`/`pthread_create` в движке только для скриншотов GeneralsOnline, DNS и UpdateChecker).
2. **Сброс в начале тика уже есть.** `GameLogic::update()` (L4110–4467) первой строкой вызывает `setFPMode()` (L4126). Не на Windows это `fesetenv(FE_DFL_ENV); feclearexcept; fesetround(FE_TONEAREST)` плюс на x86 правка x87 CW и MXCSR (`GameLogic.cpp` L215–256). В bionic arm64 `FE_DFL_ENV` = `{0,0}`, а `fesetenv` пишет FPCR, если он отличается (`bionic/libm/fenv-arm64.c` L34, L60–66). **Значит, на arm64 в начале каждого тика FPCR = 0** (FZ=0, RMode=RN, DN=0, AH=0, FIZ=0, ловушки выключены), а FPSR очищается. Всё, что меняет FPCR в клиентской части кадра (звук, рендер, ввод), до логики не доходит.
3. **Что может испортить симуляцию — только изменения внутри тика.** Внутри тика логика вызывает клиентский код: создание Drawable из обновлений объектов (`ThingFactory::newDrawable` из `GarrisonContain.cpp` L199, `StealthUpdate.cpp` L1005/L1063, `GameLogic.cpp` L2575/L5129) → загрузка W3D → возможны вызовы d3d8gles/GL (создание текстур, буферов, компиляция шейдеров). Звук логика только ставит в очередь (`GameSounds.cpp` L141–144, `appendAudioRequest`), сами `al*` вызываются в `TheAudio->UPDATE()` до тика.
4. **На arm64 никто из проверенных не меняет FPCR надолго.** Временное изменение нашлось одно — компилятор шейдеров PowerVR (`libusc.so`): `fegetround` → `fesetround(FE_TONEAREST)` → свёртка констант → `fesetround(сохранённое)`. Это пара «сохранить/восстановить», и устанавливается тот же режим RN. OpenAL Soft на arm64 FPU не трогает вообще. SDL3 трогает FP-окружение только в NEON-конвертерах звука, парой `feholdexcept`/`fesetenv`; форк их не вызывает. DXVK на arm64 FPU не настраивает. ANGLE (prebuilt), Mali r54p1 и Adreno V@0762.41 — ни одной записи FPCR и ни одного вызова `fesetround`/`fesetenv`.
5. **На x86_64 (десктоп и headless-CI)** есть три места: OpenAL `FPUCtl` (FTZ|DAZ в MXCSR, RAII, в потоке игры — только на время `alcCreateContext`/`alcResetDeviceSOFT`/`alcReopenDeviceSOFT`); Mesa llvmpipe `draw_vbo` (FTZ|DAZ на время вызова, RAII); DXVK `SetupFPU` при `CreateDevice` — x87 CW: точность 24 бит, RN, исключения замаскированы, **без восстановления**, MXCSR не трогает. Это совпадает с тем, что выставляет `setFPMode()` (x87 PC=24, RN), поэтому расхождения нет.
6. **Документированных случаев, когда драйверы Mali/Adreno/PowerVR или аудио-HAL Android меняют FPCR в потоке приложения, я не нашёл** (поиск по веб и по коду). Известные случаи — все на x86/Windows (см. §4).

## 2. Таблица «вызов — поток — меняет ли FP-режим — источник»

| Вызов / компонент | Поток | arm64: FPCR | x86_64: MXCSR / x87 | Источник |
|---|---|---|---|---|
| `setFPMode()` (форк): начало `GameLogic::update`, `GameLogic::init`, `getCRC`, `INI::load`, загрузка карты, `W3DModelDraw` (кости/турели) | SDLThread | **да, пишет FPCR=0** (сброс к норме) | **да**: MXCSR RN (`_mm_setcsr`), x87 PC=24/RN (`fldcw`) | `B/GeneralsMD/Code/GameEngine/Source/GameLogic/System/GameLogic.cpp` L215–256, L4126; `INI.cpp` L416; `W3DModelDraw.cpp` L507/603/727/865; bionic `libm/fenv-arm64.c` L34, L60–66, L113–121 |
| Опрос событий SDL: `SDL_PollEvent` в `SDL3GameEngine::serviceWindowsOS` | SDLThread | нет | нет | `B/GeneralsMD/Code/GameEngineDevice/Source/SDL3GameEngine.cpp` L2169–2213, вызов из `GameClient.cpp` L1004; grep SDL 3.4.2 `src/` — `fe*` только в `audio/SDL_audiotypecvt.c`; `libSDL3.so`: 0 `MSR FPCR` |
| `SDL_GL_SwapWindow` → `eglSwapBuffers` (d3d8gles Present) | SDLThread | SDL — нет; драйвер — см. строки Mali/PowerVR/Adreno | Mesa — см. ниже | `B/Core/Libraries/Source/d3d8gles/src/gles_pipeline.cpp` L2843 |
| SDL3 NEON-конвертеры звука (`SDL_Convert_*_NEON`) | поток SDL-аудио / вызывающий `SDL_*AudioStream*` | временно: `feholdexcept` (сохранить, очистить флаги) … `fesetenv` (восстановить); FPCR в итоге тот же | x86-версии `fe*` не используют | SDL 3.4.2 `src/audio/SDL_audiotypecvt.c` L552–776 (16 вызовов). **Форк SDL-аудио не использует** (нет `SDL_OpenAudioDevice*`/`SDL_*AudioStream*`) |
| d3d8gles `gl*` → **Mali** `libGLES_mali.so` r54p1 (GLES и EGL), `vulkan.mali.so` | SDLThread (и потоки драйвера) | **нет**: 0 `MSR FPCR`; из `fe*` импортирует только `feclearexcept` (флаги FPSR) | — | скан блоба Pixel 8 Pro (husky, lineage-23.2) |
| d3d8gles `gl*` → **PowerVR** 24.3@6660496: `libGLESv2_powervr`, `libEGL_powervr`, `libIMGegl`, `libsrv_um`, `vulkan.powervr`, компилятор `libusc.so` | SDLThread (компиляция шейдеров) | **временно, с восстановлением**: в `libusc.so` 2 пары `fegetround` → `fesetround(0=RN)` → `fesetround(saved)` (0x8093c…0x80988, 0x80c88…0x80cd4); в остальных библиотеках 0 `MSR FPCR` и нет `fe*` | — | скан блоба Pixel 10 (frankel, BP4A.260205.001) + дизассемблирование |
| d3d8gles `gl*` → **Adreno** V@0762.41: `libGLESv2_adreno`, `libEGL_adreno`, `libgsl`, `libllvm-glnext`, `libllvm-qgl`, `libadreno_utils`, `vulkan.adreno` | SDLThread | **нет**: 0 `MSR FPCR`, нет `fe*` | — | скан блоба OnePlus 12 (sm8650, OOS 11.F.56) |
| **ANGLE** (prebuilt `libGLESv2_angle.so`, `libEGL_angle.so`) | SDLThread (+ потоки ANGLE) | **нет**: 0 `MSR FPCR`, нет `fe*` | — | скан `B/Core/Libraries/Source/d3d8gles/angle-prebuilt/arm64-v8a/` |
| **DXVK** d3d8/d3d9 (`libdxvk_d3d8.so`, `libdxvk_d3d9.so`) | SDLThread (API), отдельные CS/submit-потоки | **нет**: `SetupFPU` на arm64 только пишет предупреждение; в бинарниках 0 `MSR FPCR`, нет `fe*` | **да, надолго**: при `CreateDevice` без `D3DCREATE_FPU_PRESERVE` x87 CW `&= 0xF0C0; |= 0x003F` (24 бит, RN, всё замаскировано); MXCSR не трогает | `fbraz3/dxvk@46a3bc0` `src/d3d9/d3d9_device.cpp` L93–94, L5721–5763; движок по умолчанию флаг не ставит: `dx8wrapper.cpp` L515 (`DX8Wrapper_PreserveFPU = 0`), L1081–1086 |
| **OpenAL Soft 1.24.2** — вызовы `al*`/`alc*` из `TheAudio->UPDATE()`; `alcOpenDevice`/`alcCreateContext` при инициализации | SDLThread | **нет**: `FPUCtl` собирается только при `HAVE_SSE*`; в `libopenal.so` 0 `MSR FPCR`, нет `fe*` | **временно, RAII**: `alcCreateContext` (L2750), `alcResetDeviceSOFT` (L3536), `alcReopenDeviceSOFT` (L3655) → `UpdateDeviceParams` → `FPUCtl mixer_mode{}` (L1719): FTZ|DAZ на время вызова. Через EFX (`alAuxiliaryEffectSloti`, `auxeffectslot.cpp` L602/633/913) — тоже RAII, но форк EFX не использует | `openal-soft-1.24.2/core/fpu_ctrl.cpp`, `fpu_ctrl.h`; `alc/alc.cpp` |
| **Микшер OpenAL** (бэкенд OpenSL ES: `OpenSLPlayback::mixerProc`) | свой поток OpenAL (`std::thread`) | нет (FPUCtl пустой на arm64) | FTZ|DAZ на время каждого `renderSamples` (RAII) — **только в этом потоке** | `alc/backends/opensl.cpp` L258–311, L539; `alc/alu.cpp` L2237, L2263 |
| **Oboe** | — | не используется: в `libopenal.so` форка есть только бэкенд OpenSL (строки `OpenSL`, `libOpenSLES.so`), Oboe/AAudio нет | — | `strings` по `libopenal.so` из APK |
| **Mesa llvmpipe** (десктопный GLES для E1-17; lavapipe для DXVK) | вызывающий поток (draw) и потоки `llvmpipe-N` | на arm64 `util_fpstate_*` пустые | `draw_vbo`: FTZ|DAZ на время вызова, восстанавливает (`draw_pt.c` L515–635); потоки растеризатора `llvmpipe-N` держат FTZ|DAZ постоянно (`lp_rast.c` L1165–1180); без потоков (`num_threads==0`) — RAII (L1105–1125) | Mesa 25.0.7 `src/util/u_math.c` L96–123 |
| compiler-rt (soft-float `long double`, fp128) внутри `libmain.so` | SDLThread | только **читает** FPCR.RMode (`__fe_getround`) и ставит FPSR.IXC (`__fe_raise_inexact`); FPCR не пишет | — | дизассемблирование `libmain.so` 0x25d2c04 / 0x25d2c40 (единственные FPCR/FPSR-инструкции в libmain, включая статически слинкованные FFmpeg, GNS и прочие) |
| `libc++_shared`, `libgamespy`, `libSDL3_image`, adrenotools и 3 хука | SDLThread | нет (0 `MSR FPCR`, нет `fe*`) | — | скан APK |
| Системные библиотеки Android: загрузчик `libEGL`/`libGLESv2`, `libOpenSLES`, `libaaudio`, ART/JNI | SDLThread (при JNI и инициализации) | **не проверено** (нет образа системы) | — | закрываем проверкой на устройстве (§5, T3) |

## 3. OpenAL Soft: FTZ/DAZ только в микшере?

Не совсем, но для детерминизма это безопасно.
- **arm64:** `FPUCtl::Set()/Reset()` — пустые функции: код есть только под `HAVE_SSE_INTRINSICS`/`HAVE_SSE` (`core/fpu_ctrl.cpp`). На телефоне OpenAL FPCR не трогает ни в каком потоке. Бинарник подтверждает: в `libopenal.so` форка нет ни `MSR FPCR`, ни вызовов `fe*`.
- **x86_64:** FTZ|DAZ ставится (а) в потоке микшера на время каждого `renderSamples` и (б) **в вызывающем потоке**, то есть в нашем `SDLThread`, внутри `alcCreateContext`, `alcResetDeviceSOFT`, `alcReopenDeviceSOFT` (и EFX-вызовов, которых у нас нет). Везде это RAII: деструктор `FPUCtl` восстанавливает MXCSR при выходе из области видимости, в том числе при исключении. Утечки режима в наш поток нет. `SampleConverter` (L188/240/331) используется только бэкендами WASAPI и CoreAudio.

## 4. Известные случаи смены FP-режима сторонним кодом (не Android)

- Direct3D 9 без `D3DCREATE_FPU_PRESERVE` переводит x87 в 24 бит — поэтому в движке и есть `setFPMode()`. DXVK повторяет это поведение (§2).
- Драйвер Intel HD (D3D9, 2010–2011) терял точность x87 даже с `FPU_PRESERVE`: https://community.intel.com/t5/Developing-Games-on-Intel/HD-graphics-DirectX-bug-report-FPU-precision-loss/m-p/833959
- Havok ставил FTZ/DAZ в игровом потоке Unreal, и параллельные расчёты разошлись; помогло восстановление MXCSR после вызова: https://forums.unrealengine.com/t/non-deterministic-sse-halftofloat-function/2650230
- Разделяемая библиотека, собранная с `-ffast-math`/`-Ofast`, тянет `crtfastmath.o`, и её конструктор при загрузке ставит FTZ/DAZ: https://moyix.blogspot.com/2022/09/someones-been-messing-with-my-subnormals.html, https://github.com/llvm/llvm-project/issues/57589. В форке `-ffast-math` нет; есть `-ffp-contract=off` (`B/cmake/compilers.cmake` L58); в `meson.build` DXVK `-ffast-math` тоже нет.
- Практика lockstep-RTS (Gas Powered Games): режим FPU выставляется при старте и проверяется assert'ом в каждом тике, потому что «Direct3D, printer drivers, sound libraries» его меняют: https://gafferongames.com/post/floating_point_determinism/
- **Для Android (Mali, Adreno, PowerVR, аудио-HAL) подтверждённых случаев не найдено.** Встречаются только приложения, которые сами ставят FZ в **своём** аудиопотоке (DSP), — нас это не касается.

## 5. Рекомендации

**R1. Сброс в начале тика — оставить. Добавить проверку до сброса и assert в конце тика.** Делает Геймплей в `GameLogic::update()`, помощник даёт платформа.
- В начале тика: `fp::checkAndReset("tick-start")` — прочитать регистр; если он отличается от нормы, увеличить счётчик и записать в лог значение и тег последнего внешнего вызова, затем вызвать `setFPMode()`. Это диагностика клиентской части кадра, **не ошибка**: сброс её уже нейтрализует.
- В конце тика (перед выходом из `GameLogic::update`, ~L4467) и в `GameLogic::getCRC`: `fp::assertExpected("tick-end")`. Если режим не совпал, значит, кто-то поменял его **внутри симуляции**. В CI и debug это фатальная ошибка; в release — запись в лог и в отчёт о рассинхроне.
- Обёртку вокруг **каждого** вызова GL/AL не делаем. На некоторых ядрах `msr fpcr` дорогой (сериализует конвейер), а GL-вызовов за кадр тысячи. При этом в вызовах, которые идут внутри тика, ни одной записи FPCR не найдено.

**R2. RAII-страж `fp::Guard` (сохранить и восстановить) — только на точках входа во внешний код, которые вызываются из тика.** Делает платформа:
- (а) создание ресурсов рендера из логики: `ThingFactory::newDrawable` → W3D → DX8Wrapper/d3d8gles (создание текстур/буферов, компиляция шейдеров; ради PowerVR `libusc`);
- (б) любой JNI-вызов из `SDLThread`, если он появится внутри тика;
- (в) прямые `al*`/`alc*`, если их начнут звать из логики (сейчас таких нет).

  Страж ставим сразу на (а), остальное — когда сработает assert из R1.

**R3. Норма регистров (после `setFPMode`):**
- arm64: `FPCR == 0x00000000`, сравниваем весь регистр. В `setFPMode` есть `fesetenv(FE_DFL_ENV)`, он записывает 0, так что любое ненулевое значение — изменение.
- x86_64: `(MXCSR & 0xFFC0) == 0x1F80` (все исключения замаскированы, RN, FTZ=0, DAZ=0) и `(x87 CW & 0x0F3F) == 0x003F` (маски, PC=24 бит, RN; совпадает с DXVK `SetupFPU`).
- Чтение: `mrs %0, fpcr` / `_mm_getcsr()` / `fnstcw`. Через `fegetenv` не читаем: в bionic он отдаёт FPCR, но лишний вызов libm не нужен.

**R4. Правила сборки:** запретить `-ffast-math`, `-Ofast`, `-funsafe-math-optimizations` для всех целей, включая vcpkg-порты и meson-кросс-файл DXVK (CI: grep по `compile_commands.json` и логам meson). На Linux x86_64 дополнительно проверять, что ни одна наша `.so` не содержит `crtfastmath`, то есть `ldmxcsr` в функциях `.init_array`.

**R5. Наследование при создании потока не используем.** FPCR выставляется явно в начале каждого тика; начальное значение `SDLThread` (поток создаёт ART) на результат не влияет.

**R6. Вне FPCR, но для ADR-0003:** `long double` на arm64 Android — это программный fp128 (compiler-rt), на x86_64 — x87 80 бит с PC=24 после `setFPMode`. Это верная причина расхождения, поэтому в коде симуляции `long double` запретить (CI: grep или `-Wdouble-promotion` по каталогам логики). Разницу NaN/конвертации float→int между x86 и ARM уже отслеживает трассировка флагов форка (`GameLogic.cpp` ~L4400). Это вопрос E1-45.

## 6. Тесты для CI

| # | Тест | Где | Ожидание |
|---|---|---|---|
| T1 | Юнит-тест `fp::` (чтение, норма, `checkAndReset`, `Guard`): выставить FZ (arm64) / FTZ (x86) → проверка ловит; после `Guard` значение восстановлено | x86_64 в Docker; arm64 — бинарник через `adb shell` на Pixel 8 Pro | все проверки проходят |
| T2 | Headless-реплей x86_64: `-headless -replay X.rep -gxFpCheck=fatal` (флаг новый; режим `-headless -replay` уже есть в форке, `replay-tests.yml` L249–300) | Docker на боксе; **нужны данные ZH** (E1-43); с BFME — в Э2 | `tick_end_failures = 0`; `tick_start_deviations` записаны в лог |
| T3 | Реплей на устройстве: `ReplayCheckActivity` форка, флаги `-gxFastTo -1 -gxAutoQuit` (есть в `GXReplayCheck.h`) + `-gxFpCheck`. Два прогона: быстрый (без отрисовки) и обычный (с отрисовкой); бэкенды GLES, ANGLE, Vulkan | Pixel 8 Pro; Pixel 10 (PowerVR), как только найдётся | в `gx_replay_check_result.txt` есть поля `fp_tick_start_deviations`, `fp_tick_end_failures` (=0), первый кадр и тег |
| T4 | «Тест теста»: `-gxFpChaos=client` ставит FZ/FTZ после каждого Present и `TheAudio->UPDATE` → ожидаем `tick_start_deviations > 0` и `tick_end_failures = 0`. `-gxFpChaos=logic` ставит FZ внутри тика (в `newDrawable`) → ожидаем срабатывание assert | T2 и T3 | без этого тест может молча ничего не проверять |
| T5 | Статический скан: `tools/fpcr/fpcr_scan.py` по всем `.so` APK (и по Linux-`.so` с поиском `ldmxcsr`), плюс список импортов `fe*`. Новая находка в наших библиотеках — падение сборки; в драйверах устройства (снятых через `adb pull /vendor/lib64/egl/*.so` с телефона Андрея) — предупреждение | CI (APK); вручную по устройству раз в обновление прошивки | сейчас: только compiler-rt в `libmain` (чтение FPCR, запись FPSR) |

## 7. Открыто
1. Системные библиотеки Android (`libEGL`/`libGLESv2`-загрузчик, `libOpenSLES`, `libaaudio`, ART) не сканированы: нужен `adb pull` с Pixel 8 Pro (T5) или образ прошивки.
2. Версия драйвера Mali на телефоне Андрея может отличаться от r54p1. Повторить T5 по его прошивке.
3. Pixel 10 (PowerVR): `libusc` при компиляции шейдеров временно меняет режим округления. Восстановление видно в коде, но проверить его на устройстве (T3) нельзя, пока не найдётся Pixel 10.
4. Переход на Swappy (E1-14), Oboe (если появится) и d3d8gles на Linux (E1-17, Mesa llvmpipe `draw_vbo` в нашем потоке) — после внедрения повторить T5 и T3.

## 8. Интерфейс `fp::` (согласован с Геймплеем, 02.10.2026)

Заголовок `Core/Libraries/Source/GameMath/include/fp.h` (namespace `fp`). Реализации лежат в `Core/Libraries/Source/GameMath/fp/` и собираются в цель GameMath (E1-49, Геймплей); если GameMath к старту ещё не готов, каталог и CMakeLists заводит Android-платформа. Вызовы в GameLogic (tick-start, tick-end, getCRC), вывод Stats в оверлей и проброс `-gxFpCheck` делает Геймплей (ADR-0003, R1). Файлы реализаций: `fp_arm64.cpp` (`mrs`/`msr fpcr`), `fp_x86_64.cpp` (`_mm_getcsr`/`_mm_setcsr`, `fnstcw`/`fldcw`) и `fp_msvc.cpp` (`_controlfp_s` + `_mm_getcsr`), чтобы не ломать сборку апстрима под Windows. Регистр читаем через `asm volatile` с клоббером `"memory"`, чтобы компилятор не переставлял чтение относительно FP-кода.

```cpp
namespace fp {
struct State {            // снимок, без FPSR/флагов исключений
#if defined(__aarch64__)
  uint64_t fpcr;
#else
  uint32_t mxcsr; uint16_t x87cw;
#endif
};
State read();
bool  isExpected(const State&);          // норма R3
void  reset();                           // единственная реализация сброса; setFPMode() вызывает её
void  checkAndReset(const char* where);  // ЧИТАЕТ ДО сброса: отклонение -> счётчик + лог, затем reset()
void  assertExpected(const char* where); // по режиму: Off / Log / Fatal
class Guard { State saved_; const char* where_; public:
  explicit Guard(const char* where); ~Guard(); // при выходе: если состояние изменилось -> guardRestores++, лог, восстановить saved_
};
enum class Mode { Off, Log, Fatal };
void  setMode(Mode);                     // из -gxFpCheck=off|log|fatal; CI и debug = Fatal, release = Log
struct Stats { uint32_t tickStartDeviations, tickEndFailures, guardRestores;
               const char* lastWhere; State lastValue; uint32_t lastFrame; };
const Stats& stats(); void resetStats(); // для отладочного оверлея и отчётов QA
}
```

Правила:
- В `GameLogic::update` вызов `setFPMode()` в начале тика **заменяется** на `fp::checkAndReset("tick-start")`. Если вызвать проверку *после* `setFPMode()`, она всегда будет проходить и ничего не поймает.
- `assertExpected("tick-end")` ставится в конце `GameLogic::update` и в `getCRC`.
- `Guard` (R2, Android-платформа) ставится на `ThingFactory::newDrawable` и на создание ресурсов рендера из логики. `Guard` восстанавливает состояние и считает отклонения, но не падает: падение остаётся за `assertExpected`.
- Все функции вызываются только из потока логики (`SDLThread`). В debug-сборке это проверяет assert по id потока, поэтому счётчики не атомарные.
- Хаос-режим для T4 (`-gxFpChaos=client|logic`) реализует Android-платформа в том же модуле.
