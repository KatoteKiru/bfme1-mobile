# ADR-0002 (вклад Android-платформы): целевые устройства, сборка, рендер, частота кадров

- Статус: **черновик, на согласовании с Лидом движка**
- Дата: 02.10.2026
- Автор: «Android-платформа»
- Основной документ: `ADR-0002-target-devices.md` пишет Лид движка (на 17:20 02.10.2026 в `docs/adr/` его ещё нет). Этот файл является вкладом в него: при сведении в основной ADR текст Лида главнее.
- Связано: `docs/gpu-driver-issues.md`, `docs/game-files-access.md`, TEAM.md (приоритет Pixel, эталон Pixel 8 Pro).

Ссылки на форк ниже ведут на коммит `8ef2349` (28.09.2026 00:39 МСК):
`B = https://github.com/MYSOREZ/GeneralsZH-Android-Port/blob/8ef2349`

---

## 1. Что делает базовый форк MYSOREZ (факты)

| Параметр | Значение в форке | Где |
|---|---|---|
| compileSdk / minSdk / targetSdk | 35 / 28 / 35 | B/android/app/build.gradle#L7, #L62-L63 |
| ABI | только `arm64-v8a` | B/android/app/build.gradle#L79 |
| AGP | 8.5.2 | B/android/build.gradle#L2 |
| NDK | 27.2.12479018 (закреплён в CI), в пресете «r26+» | B/.github/workflows/build-android.yml#L78, B/CMakePresets.json#L297 |
| Нативный API / STL | `android-28`, `c++_shared` (и в пресете, и в триплете vcpkg) | B/CMakePresets.json#L305-L307, B/cmake/triplets/arm64-android.cmake#L13 |
| SDK в CI | `platforms;android-35`, `build-tools;35.0.0` | B/.github/workflows/build-android.yml#L155 |
| SDL | **SDL3 3.4.2**, FetchContent из релизного tar.gz с SHA256; Java SDLActivity копируется из той же версии | B/cmake/sdl3.cmake#L22-L24 |
| Рендер по умолчанию | свой D3D8→GLES 3.0 (`Core/Libraries/Source/d3d8gles`), контекст ES 3.0 | B/GeneralsMD/Code/Main/SDL3Main.cpp#L1172-L1174 |
| Другие бэкенды | ANGLE (собранный `libGLESv2_angle.so` в репо, LOAD-сегменты выровнены 0x4000) и Vulkan через DXVK 2.6: только по явному выбору | B/Core/Libraries/Source/d3d8gles/src/d3d8gles.cpp#L96-L126 |
| Текстуры BC/DXT | программный декодер в RGBA8, если нет `GL_EXT_texture_compression_s3tc` | B/Core/Libraries/Source/d3d8gles/src/gles_pipeline.cpp#L436, #L1235 |
| Точность шейдеров | VS `highp`, FS `mediump` | gles_pipeline.cpp#L843, #L962 |
| VSync | `SDL_GL_SetSwapInterval(1)`, Swappy не используется («unexplored» в ANDROID_PORT.md §6) | gles_pipeline.cpp#L394 |
| Внутреннее разрешение | = размер окна в пикселях (попытку уменьшать откатили: интерфейс .wnd масштабируется относительно разрешения, выигрыша для UI не было; производительность не мерили) | SDL3Main.cpp#L1231-L1380 |
| Частота логики | 30 Гц (`LOGICFRAMES_PER_SECOND = WWSyncPerSecond`); 60 Гц только отдельной сборкой `libmain60.so` (`SAGE_HIGH_FPS_SIM`) | B/Core/GameEngine/Include/Common/GameCommon.h#L68, B/Core/Libraries/Source/WWVegas/WWLib/WWCommon.h#L71-L73, B/cmake/config-build.cmake#L31, B/scripts/build/android/build-dual-hz.sh |
| 16 КБ страницы | флагов `max-page-size` / `ANDROID_SUPPORT_FLEXIBLE_PAGE_SIZES` нет ни в CMake, ни в Gradle, ни в CI | поиск по репо |
| Упаковка .so | `useLegacyPackaging true` (нужно libadrenotools), DXVK не стрипается | B/android/app/build.gradle#L127, #L142 |
| Подпись | отладочный keystore **лежит в репо** (у нас запрещено: ключ в git не кладём) | B/android/app/build.gradle#L20-L27 |
| Файлы игры | `MANAGE_EXTERNAL_STORAGE` + свой файловый браузер, SAF сознательно не используется | B/android/app/src/main/AndroidManifest.xml#L38, #L87; B/docs/port/ANDROID_PORT.md §4, §6 |

## 2. Решения (предложение Лида → позиция платформы)

| # | Предложение Лида | Позиция | Почему |
|---|---|---|---|
| 1 | ABI только arm64-v8a; x86_64 лишь для эмулятора в CI | **Согласен** | Как в форке; одна сборка arm64 у всех нужна для детерминизма lockstep (RESEARCH §5). DXVK и ANGLE в форке только под arm64, поэтому эмулятор x86_64 годится лишь для GLES-пути. |
| 2a | minSdk 29 | **Согласен** | Все Pixel на Tensor выходили с Android 12+. Java SDL3 и так вызывает API 29 (`InputDevice#isExternal`) без проверки. Выставить `android-29` **и** в CMakePresets, **и** в триплете vcpkg. API 30+ (`ANativeWindow_setFrameRate`) вызываем через `dlsym` или из Java с проверкой `SDK_INT`. |
| 2b | targetSdk 35, compileSdk 35+ | **Меняем на targetSdk 36 / compileSdk 36 (AGP ≥ 8.9.1)** | Google Play: «Starting August 31 2026: New apps and app updates must target Android 16 (API level 36)» (https://developer.android.com/google/play/requirements/target-sdk). Мы ставим APK вручную, так что это не обязательно, но мигрировать сейчас дешевле. Последствия Android 16 (https://developer.android.com/about/versions/16/behavior-changes-16): (1) predictive back: `KEYCODE_BACK` больше не приходит, а в форке «Назад» открывает паузу, поэтому нужен `android:enableOnBackInvokedCallback="false"` или `OnBackInvokedCallback`; (2) отказаться от edge-to-edge нельзя (обрабатываем вырезы и отступы); (3) на sw≥600dp (Fold, Tablet) ориентация и ресайз игнорируются, исключение даёт `android:appCategory="game"`. План: в Э0 воспроизводим форк как есть (35), после первой зелёной сборки переходим на 36. |
| 2c | NDK r27 LTS, c++_shared | **Согласен, плюс 16 КБ** | r27 совпадает с CI форка. В r27 16 КБ выключено: добавить `-Wl,-z,max-page-size=16384 -Wl,-z,common-page-size=16384` для всех .so (libmain, DXVK через cross-файл meson, shared-зависимости vcpkg). Для r27 и ниже Google отдельно предупреждает о SIGSEGV из-за невыровненного GNU_RELRO. r28+ выравнивает по умолчанию. Проверка в CI: `llvm-objdump -p *.so | grep LOAD` (не ниже 2**14) и `zipalign -c -P 16 -v 4`. Play: для targetSdk ≥ 35 требуется поддержка 16 КБ, обновления без неё блокируются с 01.02.2027 (https://developer.android.com/guide/practices/page-sizes). На Pixel 8 / 8 Pro есть режим «Boot with 16KB page size» (Android 15 QPR1+): тестируем на эталоне. Вопрос Лиду: r27 + флаги или сразу r28+. |
| 3 | GLES 3.0 без обязательных 3.1/3.2; Vulkan 1.1 опционально, без автопереключения в Э1 | **Согласен, с уточнениями** | (а) На PowerVR (Pixel 10) Vulkan скрыт по умолчанию, разрешаем только по списку при `driverVersion ≥ 25.1`: форк фиксирует магенту и искажения на PowerVR BXM и Xclipse (README «Known issues»), Flutter отключал Vulkan на Pixel 10 и требует драйвер ≥ 25.1 (https://github.com/flutter/flutter/issues/179812), Vulkan на G5 медленный даже после драйвера 1.634.2906 (https://www.androidauthority.com/android-16-qpr3-beta-1-game-test-3626393/). (б) ANGLE оставляем диагностическим переключателем. (в) Порядок выбора: GLES (системный драйвер), затем GLES+ANGLE, затем Vulkan. |
| 4 | Приоритет Pixel Mali/PowerVR, артефакты на них — блокеры | **Согласен** | Таблица GPU по поколениям ниже. Pixel на G1/G2 и G5 у нас нет, нужны телефоны друзей. |
| 5a | Минимум 4 ГБ ОЗУ | **Меняем на «официально ≥ 6 ГБ, 4 ГБ без гарантий (низкий пресет)», окончательно после замера** | У всех Pixel на Tensor ≥ 6 ГБ (Pixel 6a: 6 ГБ). Программный декод DXT→RGBA8 в d3d8gles увеличивает текстуры в 4 раза (DXT3/5) или в 8 раз (DXT1), а на телефоне это общая ОЗУ. У fadi-labib TexturesZH.big после перекодировки в RGBA8 занял 1014 МБ (https://github.com/fadi-labib/Generals-Android/issues/9). Объём текстур BFME1 не мерили. Нужен замер PSS на Pixel 8 Pro (QA). |
| 5b | Данные в getExternalFilesDir, SAF-импорт позже | **Корень в getExternalFilesDir: согласен. SAF-импорт с копированием делаем в Э1, а не позже** | С Android 11 SAF не даёт выбрать `Android/data` (https://developer.android.com/training/data-storage/shared/documents-files), так что без импорта файлы туда положить можно только через ПК (MTP/adb). Подробно: `docs/game-files-access.md`. |

## 3. Целевые устройства

### 3.1 Первый эшелон: Google Pixel

| SoC | GPU | Модели | Источник |
|---|---|---|---|
| Tensor (G1) | Mali-G78 MP20 (Valhall) | Pixel 6, 6 Pro, 6a | notebookcheck (Tensor G2 vs G1) |
| Tensor G2 | Mali-G710 MP7 (Valhall 3) | Pixel 7, 7 Pro, 7a, Fold, Tablet | там же |
| Tensor G3 | Mali-G715 MP7 (Valhall 4), 890 МГц | Pixel 8, **8 Pro (эталон)**, 8a | notebookcheck Mali-G715 MP7, gizmochina G4 vs G3 |
| Tensor G4 | Mali-G715 MP7, 940 МГц | Pixel 9, 9 Pro, 9 Pro XL, 9 Pro Fold, 9a, 10a | там же |
| Tensor G5 | **PowerVR D-Series DXT-48-1536** (Imagination) | Pixel 10, 10 Pro, 10 Pro XL, 10 Pro Fold | Android Authority; отчёт Eden #398 («PowerVR D-Series DXT-48-1536 MC1») |

Immortalis-G715 в Pixel **не используется** (это MediaTek). Источники:
https://www.notebookcheck.net/Google-Tensor-G2-Chipset-details-revealed-following-Pixel-7-and-Pixel-7-Pro-launches.660056.0.html,
https://www.notebookcheck.net/ARM-Mali-G715-MP7-GPU-Benchmarks-and-Specs.762528.0.html,
https://www.gizmochina.com/2024/09/12/tensor-g4-vs-tensor-g3-benchmarks-and-key-differences/,
https://www.androidauthority.com/pixel-10-gpu-update-android-16-qpr3-beta-1-3626234/,
https://github.com/eden-emulator/Issue-Reports/issues/398

Драйвер Pixel 10: на старте 1.602.400 (v24.3), в Android 16 QPR3 Beta 1 обновлён до 1.634.2906 (v25.1, Vulkan 1.4).

### 3.2 Второй эшелон (коротко)
- **Adreno 7xx/8xx (Snapdragon):** по форку лучший случай (Adreno 830, Vulkan 1.3). Стоковые драйверы тоже не дают BC: fadi-labib полагается на Turnip, а форк декодирует программно. Turnip через libadrenotools в форке есть, для нас не в приоритете.
- **Samsung Xclipse (Exynos):** Vulkan даёт магенту и искажения (форк), `textureCompressionBC = FALSE` на Xclipse 920 (fadi-labib #9). Только GLES.
- **Mali других производителей (Bifrost/Valhall, Vulkan 1.1):** по форку упираются в CPU, работают через GLES.

### 3.3 Требования к устройству
- Android 10+ (API 29), arm64-v8a, GLES 3.0 (обязательно), Vulkan 1.1+ (опционально).
- ОЗУ: официально ≥ 6 ГБ, 4 ГБ без гарантий (п. 5a).
- Хранилище: размер копии BFME1 + 2.22 плюс запас на импорт (см. `docs/game-files-access.md`, где замер). По официальным требованиям PC-версии 3,5–4 ГБ.

## 4. Масштаб рендера на эталоне (Pixel 8 Pro, 2992×1344)

Факт: Pixel 8 Pro по умолчанию работает в режиме «High resolution» **2244×1008**, а 2992×1344 («Full resolution») включается в настройках вручную (https://www.reddit.com/r/GooglePixel/comments/1795vmj/, https://www.tomsguide.com/phones/android-phones/how-to-change-screen-resolution-on-google-pixel-phones). Поверхность игры будет одного из двух размеров.

Решение (предложение):
- Масштаб задаём **целевой высотой 3D-сцены**, а не долей от нативного, чтобы результат не зависел от режима экрана.
- **По умолчанию ~756p (≈1680×756)**: это 0,75 от 1008 и 0,5625 от 1344. Пресеты: 50 / 67 / 75 / 100% от High (504p / 672p / 756p / 1008p) и «натив». Ширина считается по реальному соотношению сторон и округляется до чётного.
- Настройка в лаунчере («Масштаб рендера»), хранится в нашем конфиге, применяется при следующем запуске (пересоздание поверхности).
- Реализация:
  - **Э1 (для замеров):** `SurfaceHolder.setFixedSize(w, h)`: растягивает аппаратный композитор (HWC), кода почти нет, но мыльным становится и интерфейс (https://developer.android.com/reference/android/view/SurfaceHolder, https://github.com/google/grafika/blob/master/app/src/main/java/com/android/grafika/HardwareScalerActivity.java).
  - **Э4 (цель):** 3D-сцена рендерится в FBO на внутреннем разрешении, затем один blit-апскейл в поверхность, а наш HUD рисуется поверх в нативном разрешении. Правила для тайловых GPU: FBO привязываем один раз за кадр, делаем `glClear` всех вложений, `glInvalidateFramebuffer(DEPTH, STENCIL)` до unbind (см. `docs/gpu-driver-issues.md` №4).
- Критерии Э1 (≥ 30 FPS, без магенты) проверяем на значении по умолчанию и на 100%; нагрев измеряет QA.

## 5. Частота кадров на 120-Гц дисплее

Факты:
- Логика движка Generals тикает 30 раз в секунду: `BaseFps = 30 // must never change`, `LOGICFRAMES_PER_SECOND = WWSyncPerSecond = 30` (60 только в отдельной сборке форка для GeneralsOnline). BFME1-ретейл, по данным Open-BFME-1, держит логику на 5 Гц, а движок около 30 Гц (RESEARCH §5, данные сообщества).
- `setFrameRate()` (API 30+) не гарантирует переключение; платформа может выбрать кратную частоту (60 → 120 Гц). Тогда без меток времени показа будет джиттер (https://developer.android.com/media/optimize/performance/frame-rate).
- Swappy ставит метки (`EGL_ANDROID_presentation_time`, `VK_GOOGLE_display_timing`), подбирает ближайшую частоту дисплея и, по документации, экономит батарею. Рекомендуемая настройка: `swappyAutoSwapInterval(false); swappyAutoPipelineMode(false); swappySwapIntervalNS(1e9/fps)` (https://developer.android.com/games/sdk/frame-pacing).

Решение (предложение):
- **По умолчанию 30 FPS**: кадр рендера равен тику логики.
- Режим **60 FPS** добавляем только если движок интерполирует отрисовку между тиками логики (в форке не проверено, вопрос Лиду). Логику на 60 Гц не берём: другая сборка, несовместимая по сети и реплеям.
- При старте один раз вызываем `ANativeWindow_setFrameRate(window, 30 или 60, ANATIVEWINDOW_FRAME_RATE_COMPATIBILITY_DEFAULT)` (из Java: `Surface.setFrameRate`). Подача кадров через Swappy GL: интервал 33,3 или 16,7 мс, без авторежимов. Вместо прямого `SDL_GL_SetSwapInterval(1)` нужна интеграция Swappy со SDL3 (EGL-контекст SDL).
- Нагрев и батарея: 30 FPS ожидаемо примерно вдвое снижают нагрузку CPU и GPU против 60, а понижение разрешения сокращает работу фрагментов и трафик памяти **[оценка, замер QA на Pixel 8 Pro]**.

## 6. Прочее
- Проверка разработчиков Google: с 30.09.2026 действует только для магазинов в Бразилии, Индонезии, Сингапуре и Таиланде. Прямую установку APK пока не затрагивает, глобальный запуск в 2027, установка через adb исключена из проверки (https://developer.android.com/developer-verification, FAQ). К 2027 году понадобится решение по аккаунту limited distribution (вопрос Оркестратору и Андрею).
- Android 16, локальная сеть: доступ к LAN закрывают новым разрешением в одном из следующих релизов (`NEARBY_WIFI_DEVICES` в режиме opt-in). Касается LAN-игры, к сведению бота «Сеть и сервер».

## 7. Открытые вопросы к Лиду
1. Есть ли в форке интерполяция отрисовки между тиками (нужна для 60 FPS)?
2. 16 КБ: NDK r27 + флаги или r28+?
3. Перекодировка DXT→ETC2/ASTC (зона «Форматов и данных» или платформы) и допустимость формата, у которого нет токена D3D8 (пройдёт только через d3d8gles, не через DXVK).
4. Порог ОЗУ: 6 против 4 ГБ, решаем после замера PSS.
5. targetSdk 36 сразу после первой зелёной сборки?

---

## Решение Лида 02.10.2026

Основной документ: `docs/adr/ADR-0002-target-devices.md`, статус «Принято, согласовано с Android-платформой 02.10.2026». По §2 и §7 этого файла:

| # | Решение |
|---|---|
| 1 | Только arm64-v8a; x86_64 только для эмулятора в CI (только GLES). **Принято.** |
| 2a | minSdk 29 в Gradle, `CMakePresets.json` и триплете vcpkg; API 30+ через проверку версии. **Принято** (E1-10). |
| 2b | targetSdk 35 в Э0 для воспроизведения форка; после первой зелёной сборки targetSdk/compileSdk 36 (AGP ≥ 8.9.1) вместе с NDK r28+; `OnBackInvokedCallback`, edge-to-edge, `appCategory=game`. **Принято** (E1-19, не блокирует Э1). |
| 2c | NDK r27.2 + флаги 16 КБ на все `.so` + проверка в CI сейчас; r28+ вместе с targetSdk 36. **Принято** (E1-11). |
| 3 | GLES 3.0 по умолчанию; Vulkan 1.1 опционально, без автопереключения в Э1; на PowerVR Vulkan скрыт или по списку с драйвера ≥ 25.1; ANGLE как диагностика; порядок GLES → GLES+ANGLE → Vulkan. **Принято.** |
| 4 | Приоритет Pixel; артефакты на Mali/PowerVR — блокеры. **Принято.** В таблицу добавлен Pixel 11 / Tensor G6 (PowerVR CXTP-48-1536 по Wikipedia, **[не проверено]**). |
| 5a | ОЗУ: официально ≥ 6 ГБ, 4 ГБ без гарантий с низким пресетом; окончательно после замера PSS (E1-41). **Принято предварительно**, вынесено Оркестратору (меняет «4 ГБ» из задания). |
| 5b | `getExternalFilesDir` + SAF-импорт с копированием в Э1; `MANAGE_EXTERNAL_STORAGE` убираем, когда заработает импорт. **Принято** (E1-12a, E1-12b). |
| §4 | Масштаб по целевой высоте сцены, по умолчанию ~756p, пресеты 50/67/75/100%; `setFixedSize` в Э1, FBO + апскейл + нативный HUD к Э4. **Принято** (E1-13). |
| §5 | 30 FPS по умолчанию, логика только 30 Гц, `libmain60` не берём; `setFrameRate` один раз при старте + Swappy с фиксированным интервалом. 60 FPS только при интерполяции. **Принято** (E1-14). |

Ответы на §7: (1) интерполяцию проверяет Лид (E1-35). Предварительно: `GENERALS_ONLINE_HIGH_FPS_RENDER=1` отвязывает рендер и держит частицы и FX на legacy-30 Гц (`GameClient::m_frameLegacy`), а интерполяции позиций Drawable между тиками grep не нашёл. (2) r27.2 + флаги, затем r28+. (3) Перекодировщик ETC2 и кэш делают «Форматы и данные», загрузку в d3d8gles делает Android-платформа; внутренний FOURCC только внутри d3d8gles; CPU-декод остаётся запасным; ASTC позже; триггер — PSS выше бюджета или загрузка карты > 30 с (E1-18, E1-27). (4) После замера PSS. (5) Да, после первой зелёной сборки.
