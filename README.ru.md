# BFME1 Mobile

**Открытый порт «Властелин колец: Битва за Средиземье» (2004) на Android с сенсорным управлением и сетевой игрой. Основа: исходный код движка SAGE (*Command & Conquer: Generals – Zero Hour*) под GPL v3.**

[English](README.md) · [Лицензия](LICENSE) · [Авторы и правовая информация](NOTICE.md) · [Как участвовать](CONTRIBUTING.md)

> [!IMPORTANT]
> В репозитории **только исходный код движка**. Данных игры «Битва за Средиземье» здесь нет: ни архивов `.big`, ни моделей, текстур, карт, видео, музыки и текстов.
> Чтобы играть, нужна **своя законно купленная копия** BFME1 (версия 1.03 с фанатским патчем 2.22). При первом запуске приложение копирует файлы игры из вашей копии на телефон.

> [!NOTE]
> Проект в разработке и **пока не играбелен**. Карты BFME1 уже загружаются движком, а запуск матча на телефоне, бой, ИИ и оригинальные меню ещё делаются. Текущее состояние: [`docs/GAMEPLAY-GAP.md`](docs/GAMEPLAY-GAP.md).

## Что это

BFME1 работает на SAGE, том же движке, что и *Command & Conquer: Generals – Zero Hour*. В 2025 году Electronic Arts опубликовала исходники Generals/Zero Hour под GPL v3, и сообщество перенесло их на современные 64-битные платформы. Мы берём Android-порт Zero Hour от сообщества и учим движок читать и играть данные BFME1:

- **Движок:** форматы BFME1 (BIG, диалект INI, карты, W3D, строки) и игровые системы поверх движка Zero Hour.
- **Телефон:** arm64, Android 10 и новее, по умолчанию OpenGL ES 3.0 (Vulkan по желанию), интерфейс под пальцы.
- **Сеть:** детерминированный lockstep через релей-сервер, с проверкой реплеями и CRC по кадрам.

## Состояние

| Часть | Состояние |
|---|---|
| Загрузка данных BFME1 (BIG/VFS, INI, карты) | работает, покрыто тестами |
| Сенсорное управление (из Android-порта Zero Hour) | работает |
| Запуск карты BFME1 на телефоне | в работе |
| Юниты, бой, ИИ | в работе |
| Меню и HUD BFME1 | в работе |
| Сетевая игра | спроектирована ([`docs/NET-DESIGN.md`](docs/NET-DESIGN.md)), не реализована |

Основные тестовые устройства: Google Pixel (Tensor G1–G5).

## Сборка

Все сборки идут в Docker, видеокарта не нужна.

| Цель | Команда | Подробности |
|---|---|---|
| APK для Android (arm64-v8a) | `tools/build-apk.sh` | [`docs/bfme/ANDROID_BUILD.md`](docs/bfme/ANDROID_BUILD.md) |
| Linux x86_64 (отладка движка, DXVK → Vulkan, SDL3) | `tools/build-linux.sh` | [`docs/bfme/LINUX_BUILD.md`](docs/bfme/LINUX_BUILD.md) |
| Дым-тест без экрана на Mesa lavapipe | `tools/build-linux.sh --smoke-only` | там же |

Клонируйте репозиторий и подтяните единственный нужный для сборки сабмодуль:

```sh
git clone https://github.com/KatoteKiru/bfme1-mobile.git
cd bfme-mobile
git submodule update --init --depth 1 references/fbraz3-dxvk
```

APK подписывается ключом, который лежит вне репозитория (см. документ по сборке Android). Ключи подписи и учётные данные сервисов в git не попадают никогда.

## Структура

| Каталог | Что внутри |
|---|---|
| `engine/bfme/` | игровой слой BFME1 поверх движка |
| `formats/` | BIG/VFS, схема INI BFME1, MAP, W3D, STR/CSF, текстуры |
| `net/`, `server/` | lockstep, транспорт, релей, лобби |
| `ui/` | мобильный HUD и меню |
| `tests/` | юнит-тесты, реплей-тесты, CRC по кадрам (только синтетические фикстуры) |
| `tools/` | скрипты сборки и `tools/datacheck` (инвентаризация данных BFME1, работает по вашей копии) |
| `android/` | Gradle/NDK, оболочка приложения |
| `docs/` | заметки, архитектурные решения (`docs/adr/`), документы по сборке |
| `Core/`, `Generals/`, `GeneralsMD/` и др. | код движка из апстрима |

## Апстрим

Это форк работы сообщества, без которой проекта бы не было:

```
EA, исходники под GPL v3 (2025)
  → TheSuperHackers/GeneralsGameCode
    → Fighter19/CnC_Generals_Zero_Hour → fbraz3/GeneralsX
      → ammaarreshi/Generals-Mac-iOS-iPad
        → MYSOREZ/GeneralsZH-Android-Port
          → KatoteKiru/bfme1-mobile (этот репозиторий, ветка bfme)
```

README апстрима сохранён в [`README-upstream.md`](README-upstream.md), полный список авторов в [`NOTICE.md`](NOTICE.md).

## Лицензия

GNU General Public License v3.0, как и исходники EA, от которых проект происходит. Текст в [`LICENSE`](LICENSE). [`LICENSE.md`](LICENSE.md) содержит тот же текст вместе с дополнительными условиями из оригинального релиза EA, они тоже действуют.

## Правовая информация

BFME1 Mobile — неофициальный некоммерческий фанатский проект. Он не связан с Electronic Arts Inc., Warner Bros. Entertainment, New Line Cinema, Middle-earth Enterprises и наследниками Толкина, не одобрен и не спонсируется ими. *The Lord of the Rings*, *The Battle for Middle-earth*, *Command & Conquer* и связанные названия и логотипы принадлежат их владельцам. Проект не распространяет файлы игры. Подробнее в [`NOTICE.md`](NOTICE.md).
