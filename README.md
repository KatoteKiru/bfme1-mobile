# BFME1 Mobile

Android-версия «Властелин колец: Битва за Средиземье» (BFME1, баланс патча 2.22) на движке
Command & Conquer Generals / Zero Hour. Адаптация под телефон, сетевая игра и свой сервер.

Неофициальный некоммерческий фанатский проект. Не связан с Electronic Arts, Westwood, New Line
Cinema, Middle-earth Enterprises и другими правообладателями и не одобрен ими.

## Основа и лицензия

- Код движка: исходники Generals/Zero Hour, которые EA опубликовала под **GPL v3**. Мы берём их
  в Android-сборке сообщества [MYSOREZ/GeneralsZH-Android-Port](https://github.com/MYSOREZ/GeneralsZH-Android-Port).
- Лицензия всего репозитория: **GNU GPL v3**. Полный текст лежит в [`LICENSE`](LICENSE), а
  [`LICENSE.md`](LICENSE.md) содержит его же вместе с дополнительными условиями EA из исходного релиза.
  Каждому, кому передаём APK, даём и исходники.
- Описание апстрима сохранено без изменений в [`README-upstream.md`](README-upstream.md).

### Цепочка апстрима

```
EA (GPL v3 source release)
  → TheSuperHackers/GeneralsGameCode
    → Fighter19/CnC_Generals_Zero_Hour → fbraz3/GeneralsX
      → ammaarreshi/Generals-Mac-iOS-iPad
        → MYSOREZ/GeneralsZH-Android-Port   ← наш апстрим
          → KatoteKiru/bfme-mobile, ветка bfme
```

Ветки и теги апстрима перенесены сюда с полной историей. Ветка `main` повторяет апстрим, а вся наша
работа идёт в `bfme`. Обновлять так:

```sh
git remote add upstream https://github.com/MYSOREZ/GeneralsZH-Android-Port.git
git fetch upstream
git checkout main && git merge --ff-only upstream/main && git push origin main
git checkout bfme && git merge main   # слияние делает Лид движка, через ревью
```

## Файлы игры EA в репозиторий не кладём никогда

Ресурсы BFME1 (`*.big`, видео, модели, строки, карты из игры, патч 2.22) **никогда** не попадают
в git, APK, на сервер и в открытый доступ. Каждый игрок кладёт на телефон свою законную копию игры.
На боксе для анализа есть копия только для чтения в `/workspace/bfme-mobile/gamedata/`, она лежит вне репозитория.
Часть правил `.gitignore` подстраховывает от случайного коммита (`*.big`, `*.w3d` вне `tests/`, `*.csf`, `*.bik`, `/gamedata/`, `/incoming/`).
Тестовые фикстуры делаем сами или генерируем синтетически.

## Структура (наши каталоги)

| Каталог | Что внутри | Владелец |
|---|---|---|
| `engine/bfme/` | слой геймплея BFME1 поверх движка | Геймплей (архитектура и ревью: Лид движка) |
| `formats/` | BIG/VFS, INI-схема BFME1, MAP, W3D, STR/CSF, текстуры | Форматы и данные |
| `net/` | lockstep, транспорт (GNS + релей), журнал команд | Сеть и сервер |
| `server/` | аккаунты, лобби, квикматч, релей | Сеть и сервер |
| `ui/` | мобильный HUD и меню (вместо APT) | Мобильный UI |
| `tests/` | реплей-тесты, CRC по кадрам, профилирование | QA |
| `tools/` | инструменты апстрима и наш `tools/datacheck` | Форматы и данные |
| `docs/adr/` | архитектурные решения (ADR) | Лид движка |
| `android/`, `ci/` | Gradle/NDK, CI, Docker-образ | Android-платформа |

Остальные каталоги (`Core/`, `Generals/`, `GeneralsMD/` и др.) взяты из апстрима как есть.
Правки в чужой области вносятся только после ревью её владельца.

## Цели устройств

arm64-v8a, Android 10 и новее, по умолчанию GLES 3.0 (Vulkan включается по желанию). Первый приоритет —
Google Pixel (GPU Mali в Tensor G1–G4, PowerVR в Tensor G5). Эталонное устройство — Pixel 8 Pro.
Окончательные решения записываются в `docs/adr/`.
