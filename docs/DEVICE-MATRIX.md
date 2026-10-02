# Матрица устройств BFME1 Mobile (QA)

- **Автор:** QA (1b796e73), 02.10.2026. **Статус:** черновик для Оркестратора и Лида движка.
- **Опирается на:** TEAM.md («Приоритет устройств»), `docs/adr/ADR-0002-target-devices.md` (§4 и §10 главнее этого файла при расхождении), `docs/gpu-driver-issues.md`.
- Этот файл раскладывает ADR-0002 §10 по ярусам для QA, добавляет проверенные данные о GPU, облачные фермы с ценами и место для телефонов Андрея и друзей.
- Пометки: **[проверено]** (источник открыт в этой сессии), **[не проверено]** (по общим данным, нужно сверить по `GL_RENDERER` на устройстве).

## 1. Ярусы

Ярусы QA совпадают с ADR-0002 §10 и уточняют порядок внутри яруса A (указание Лида от 02.10.2026).

| Ярус QA | ≈ ADR-0002 | Смысл | Правило серьёзности (подробно в TEST-PLAN §3) |
|---|---|---|---|
| **0 (эталон)** | A, эталон | Pixel 8 Pro Андрея. Здесь меряем базовые FPS, PSS, нагрев и принимаем все критерии Э1 | любое падение или артефакт рендера = **блокер** |
| **1a** | A | Pixel с **PowerVR**: Tensor G5 (линейка Pixel 10), затем Tensor G6 (Pixel 11) | падение или артефакт = **блокер** |
| **1b** | A | остальные Pixel с **Mali** (G1, G2, G4) | падение или артефакт = **блокер** |
| **2** | B | **Adreno** (Snapdragon): флагманы и средний класс | падение = major, артефакт = major (приоритет 2 по ADR-0002 §3.4) |
| **3** | B | прочие: Samsung Xclipse (только GLES), Mali и PowerVR в бюджетных MediaTek/Exynos/Unisoc | падение = major, артефакт = minor/major по видимости; < 6 ГБ ОЗУ без гарантий (ADR-0002 §4.3) |
| **Э** | C | эмулятор x86_64, десктоп Linux на боксе, облачные фермы | только smoke-тесты, реплеи, автоматизация. Рендер эмулятора **не** заменяет проверку Mali/PowerVR |

Телефон друга Андрея, на котором реально будут играть, поднимается минимум до яруса 2, независимо от GPU (§4).

## 2. Google Pixel (ярусы 0 и 1)

| Модель | SoC | GPU | ОЗУ | Ярус | Есть у команды | Firebase Test Lab (model id) [проверено по зеркалу каталога] |
|---|---|---|---|---|---|---|
| **Pixel 8 Pro** | Tensor G3 | **Mali-G715 MP7** (890 МГц) | 12 ГБ | **0** | **да (Андрей)** | `husky`, API 34–35 |
| Pixel 8 / 8a | Tensor G3 | Mali-G715 MP7 | 8 ГБ | 1b | нет | `shiba` (API 34–36), `akita` (API 34–35) |
| **Pixel 10 / 10 Pro / 10 Pro XL / 10 Pro Fold** | **Tensor G5** | **PowerVR D-Series DXT-48-1536** (без RT; драйвер ≥ 25.1 / v1.634.2906 пришёл с Android 16 QPR3) | 12–16 ГБ | **1a (первый после эталона)** | нет | `frankel`, `blazer`, `mustang`, `rango` (API 36) |
| Pixel 11 / 11 Pro / 11 Pro Fold | Tensor G6 | PowerVR **CXTP-48-1536** [не проверено: только обзоры, ADR-0002 №9] | [не проверено] | 1a (после Pixel 10, по решению Лида и платформы) | нет | `cubs`, `grizzly`, `yogi` (API 37) |
| Pixel 9 / 9 Pro / 9 Pro XL / 9 Pro Fold / 9a | Tensor G4 | Mali-G715 MP7 (940 МГц) | 8–16 ГБ | 1b | нет | `tokay`, `caiman`, `komodo`, `comet`, `tegu` |
| Pixel 10a | Tensor G4 | Mali-G715 MP7 | 8 ГБ | 1b | нет | `stallion` (API 36) |
| Pixel 7 / 7 Pro / 7a | Tensor G2 | Mali-G710 MP7 | 8–12 ГБ | 1b | нет | `panther`, `cheetah`, `lynx` (API 33) |
| Pixel 6 / 6 Pro / 6a | Tensor (G1) | Mali-G78 MP20 | 6–12 ГБ | 1b (нижняя граница Pixel по CPU и ОЗУ) | нет | `oriole` (API 31–33), `bluejay` (API 32) |

Источники GPU:
- Tensor G5 = PowerVR DXT-48-1536, Mali-G715 у G3/G4, Mali-G710 MP7 у G2: Android Authority, https://www.androidauthority.com/google-tensor-g5-benchmarks-3590355/ ; спецификация DXT: https://www.imaginationtech.com/product/img-dxt-48-1536/ **[проверено]**.
- Драйвер PowerVR 25.1 (v1.602.400 → v1.634.2906) в Android 16 QPR3: https://www.xda-developers.com/google-pixel-10-fixes-key-performance-bottleneck-with-new-gpu-drivers/ , https://www.androidpolice.com/google-pixel-10-gets-a-long-overdue-gpu-driver-upgrade/ **[проверено]**.
- Pixel 6 = Mali-G78 MP20, Pixel 7 = Mali-G710 MP7: https://www.androidauthority.com/google-tensor-g2-explained-3216087/ **[проверено по выдаче поиска]**.
- Pixel 10a = Tensor G4, Mali-G715 MP7: https://www.gsmarena.com/google_pixel_10a-review-2943p4.php **[проверено по выдаче поиска]**.
- Tensor G6 = PowerVR CXTP-48-1536: https://www.androidauthority.com/tensor-g6-deep-dive-3695968/ **[только выдача поиска, в ADR-0002 тоже не проверено]**.
- Каталог Firebase Test Lab: зеркало `gcloud firebase test android models list`, https://github.com/SimonMarquis/Firebase-Test-Lab-Devices (снято 02.10.2026). Официальный список: https://firebase.google.com/docs/test-lab/android/available-testing-devices. Перед использованием сверить командой `gcloud firebase test android models describe <id>`.

**Пиксели в облаке.** В Firebase Test Lab есть **физические** Pixel 6–11, включая Pixel 10 (PowerVR). Это единственный известный нам способ получить PowerVR-Pixel без устройства у друзей. Ограничение по данным EA описано в §6.

## 3. Ярус 2: Adreno (Snapdragon)

Модели взяты как примеры. Конкретные телефоны друзей в §4 главнее. GPU указан по общим данным **[не проверено]**: сверяем по `GL_RENDERER` в отчёте устройства.

| Класс | Пример | SoC / GPU | Зачем |
|---|---|---|---|
| Флагман 2025 | Galaxy S25 / S25 Ultra, OnePlus 13 | Snapdragon 8 Elite / Adreno 830 | лучший случай, Vulkan по выбору |
| Флагман 2024 | Galaxy S24 Ultra, Xiaomi 14 | Snapdragon 8 Gen 3 / Adreno 750 | массовый флагман у друзей |
| Флагман 2023 | Galaxy S23 (все регионы на Snapdragon), OnePlus 11 | Snapdragon 8 Gen 2 / Adreno 740 | драйверы Adreno 7xx, на них опирался fadi-labib |
| Верхний средний | Poco F6 | Snapdragon 8s Gen 3 / Adreno 735 | |
| Средний | Galaxy A52s 5G, Redmi Note 13 Pro 5G | SD 778G / Adreno 642L; SD 7s Gen 2 / Adreno 710 | старые драйверы Adreno 6xx, 6–8 ГБ |

В FTL есть Galaxy S22–S26, OnePlus 11 (`CPH2449`), Xiaomi 14 (`houji`), Galaxy A52s (`a52sxq`) и др. **[проверено по зеркалу каталога]**. Важно: Galaxy S24 и S24+ в Европе продаются на Exynos 2400 (Xclipse), поэтому их ярус определяем по `GL_RENDERER`, а не по названию модели.

## 4. Телефоны Андрея и друзей (ЗАПОЛНИТЬ)

> Место для моделей, которые пришлёт Андрей (через Оркестратора). Добавляем строку на каждый телефон. GPU и драйвер заполняем **из отчёта устройства** (TEST-PLAN §6, файл `device.json`), а не по названию модели.

| # | Владелец (имя или ник) | Модель | SoC / GPU (из `GL_RENDERER`) | Драйвер (`GL_VERSION`) | ОЗУ | Android | Ярус | Бэкенды для проверки | Статус |
|---|---|---|---|---|---|---|---|---|---|
| 1 | Андрей | Pixel 8 Pro | Tensor G3 / Mali-G715 MP7 | _из отчёта_ | 12 ГБ | _из отчёта_ | 0 | GLES, ANGLE (диаг.), Vulkan (опц.) | эталон |
| 2 | _?_ | _?_ | | | | | | | ожидаем данные |
| 3 | _?_ | _?_ | | | | | | | ожидаем данные |

Нужно в первую очередь (ADR-0002 №8): **любой Pixel 10 (PowerVR)** и Pixel 6/7 (нижняя граница Mali).

## 5. Ярус 3: бюджетные Mali, PowerVR и прочие

| Класс | Пример | SoC / GPU **[не проверено]** | Комментарий |
|---|---|---|---|
| Бюджет Mali | Galaxy A15, Redmi Note 13 5G | Helio G99 / Mali-G57 MC2; Dimensity 6080 / Mali-G57 MC2 | обычно 4–6 ГБ: только низкий пресет, без гарантий |
| Средний Mali | Galaxy A35 / A53 | Exynos 1380 / Mali-G68 MP5; Exynos 1280 / Mali-G68 | Vulkan 1.1, по данным форка упирается в CPU |
| Бюджет PowerVR | moto g54 5G | Dimensity 7020 / IMG BXM-8-256 | дешёвый PowerVR, проверка совместимости шейдеров GLES |
| Xclipse (AMD RDNA) | Galaxy S24 / S24+ (EU), A55 | Exynos 2400 / Xclipse 940; Exynos 1480 / Xclipse 530 | только GLES (ADR-0002 §4.2: магента на Vulkan) |

Все примеры, кроме Xclipse S24 EU, есть в каталоге FTL (`a15`, `a35x`, `cancunf` = moto g54, `a55xzh`) **[проверено по зеркалу каталога]**.

## 6. Как тестировать без физических устройств

### 6.1 Главное ограничение: данные EA
По TEAM.md и ADR-0001 §3.6 файлы EA и всё, что из них получено, **не попадают на сервер**. Облачная ферма является чужим сервером. Значит, без отдельного решения Андрея в облаке мы **не можем** гонять реальную карту BFME1. В облаке и эмуляторе доступно только то, что работает без данных EA:
- запуск APK, жизненный цикл, сборщик отчётов, экран импорта;
- **синтетический GPU-тест**: встроенная сцена из сгенерированных в коде текстур и мешей, которая прогоняет d3d8gles по тем же путям (DXT-декод, mediump/highp UV, динамические буферы, мипы с высотой больше ширины, FBO и invalidate). Выдаёт скриншот и CRC кадра. Сцену делает Android-платформа, критерии даёт QA. Это и есть способ ловить артефакты PowerVR на Pixel 10 из FTL;
- дамп возможностей GPU (`device.json`, TEST-PLAN §6.3);
- тест крашей: намеренный SIGSEGV и ANR из скрытого меню, чтобы проверить сборщик.

### 6.2 Android Emulator (бесплатно, на боксе или в CI)
- Образ **x86_64 API 30+**: в нём есть трансляция ARM (native bridge), поэтому наш APK только с arm64-v8a в принципе запускается. По ADR-0002 §2.1 для эмулятора в CI собираем отдельную x86_64-сборку. Обе сборки **не детерминированы друг с другом**, поэтому golden-CRC для них разные (REPLAY-TESTS §5). Источник: https://developer.android.com/studio/releases/emulator , https://www.xda-developers.com/android-11-x86-system-images-android-emulator-arm-abis/
- GPU: `-gpu host` (через gfxstream на GPU хоста; на боксе GPU нет), `-gpu software` / `swiftshader` (SwiftShader: GLES 3.x и Vulkan на CPU), `swangle` (ANGLE поверх SwiftShader). Для headless CI: `emulator -no-window -gpu software`. Режимы `swiftshader_indirect`/`swangle_indirect` в новых версиях эмулятора объявлены устаревшими **[по выдаче поиска, сверить с https://developer.android.com/studio/run/emulator-acceleration]**.
- Чего эмулятор **не** даёт: поведения драйверов Mali и PowerVR (FP16 mediump, тайловость, баги мипов), реальных FPS и нагрева. Годится для smoke, сборщика отчётов, жизненного цикла и вёрстки HUD.
- Бокс: `/dev/kvm` есть, но пользователь `box` не входит в группу kvm (gid 103). Эмулятор запускаем в Docker с `--device /dev/kvm` (настраивает Android-платформа).
- Десктоп Linux (ярус Э, ADR-0002 §10 C): headless-реплеи и CRC (REPLAY-TESTS), Mesa lavapipe/llvmpipe для скриншот-теста террейна.

### 6.3 Облачные фермы (ничего не покупали и не регистрировали)

Цены сняты 02.10.2026 со страниц по ссылкам, в USD без налогов.

| Сервис | Что даёт нам | Бесплатно | Платно | Источник |
|---|---|---|---|---|
| **Firebase Test Lab** | физические Pixel 6–11, **включая Pixel 10 (PowerVR)**, Galaxy, Xiaomi; тип теста **Game Loop** (приложение само проигрывает сценарий и пишет результат в файл), Robo, instrumentation; логи и видео | Spark: 5 тестов в день на физических и 10 на виртуальных устройствах. Blaze: 30 мин в день на физических и 60 мин на виртуальных | Blaze: **$5 за час** физического устройства, **$1 за час** виртуального, поминутно с округлением вверх | https://firebase.google.com/docs/test-lab/usage-quotas-pricing , https://firebase.google.com/pricing/ |
| **Android Device Streaming** (Firebase, через Android Studio) | интерактивный удалённый Pixel, в том числе для ручного осмотра артефактов | 30 мин на проект в месяц | **$0,15 за минуту** сверх квоты | https://firebase.google.com/pricing/ |
| **AWS Device Farm** | автотесты и удалённый доступ к реальным устройствам | разово 1000 устройство-минут | **$0,17 за устройство-минуту**; безлимит **$250 в месяц за слот** | https://aws.amazon.com/device-farm/pricing/ |
| **BrowserStack** App Live / App Automate | ручной доступ / Appium и Espresso на реальных устройствах | нет (Freelancer: 100 мин App Live) | App Live Individual **$39/мес** (при оплате за год), Team **$150/мес**; App Automate Device Cloud **$199/мес** за 1 параллельный поток (за год), Pro **$249/мес**; Freelancer **$12,50/мес** за год или $19 помесячно | https://www.browserstack.com/pricing |
| **TestMu AI (бывш. LambdaTest)** | ручной и автоматический доступ к реальным устройствам | 5 сессий в месяц по 2 мин | Real Device Plus Live **$39/мес** за год или $49 помесячно; Automation **$199/мес** за год или $249 помесячно, за параллельный поток | https://www.testmuai.com/real-device-cloud/ |
| **Samsung Remote Test Lab** | только Samsung (Galaxy S26, A27, Z Fold8 и т. д.), есть ADB через Remote Debug Bridge | бесплатно, **20 кредитов в день по 15 мин** **[лимит по стороннему обзору, на официальной странице не сверял]** | нет | https://developer.samsung.com/remote-test-lab , https://www.itechguides.com/products/samsung-remote-test-lab/ |
| GitHub Actions arm64-раннеры (для CI реплеев, не телефоны) | Linux arm64 в приватных репо с 29.01.2026 | входят в минуты плана | **$0,005 за минуту** (2 ядра) сверх квоты | https://github.blog/changelog/2026-01-29-arm64-standard-runners-are-now-available-in-private-repositories/ , https://docs.github.com/en/billing/reference/actions-runner-pricing |

**Рекомендация QA:**
1. Сейчас (Э0–Э1) бесплатно: эмулятор в CI + Pixel 8 Pro Андрея.
2. Когда появится синтетический GPU-тест: **Firebase Test Lab на Spark (бесплатно)**, Game Loop на `frankel` (Pixel 10), `husky` (эталон для сравнения), `oriole` (Pixel 6), `cubs` (Pixel 11), плюс один Adreno (`pa1q`, Galaxy S25). Это 5 физических тестов в день, денег не требует. Нужен Google-аккаунт и проект Firebase: регистрирует только Андрей или с его согласия (через Оркестратора).
3. Платные фермы не нужны, пока не упрёмся в квоту Spark. Если упрёмся, Blaze с лимитом бюджета: 20 мин теста на 5 устройствах ≈ 1,7 ч ≈ $8,5 сверх бесплатных 30 мин в день **[оценка]**.
4. Реальную карту BFME1 в облаке не гоняем без явного решения Андрея (§6.1).
