# Запуск движка с аргументами через adb (BFME: debug-args)

Тестовые сборки принимают строковый extra `args` с аргументами командной строки движка. Они дописываются в argv после аргументов по умолчанию (`-gxSafeInsets …`, аргументы проверки реплеев), до разбора `CommandLine`. Итоговый argv пишется в logcat одной строкой с тегом `BFME-args`.

## Команды
```sh
adb shell am force-stop io.github.katotekiru.me1mobile
adb shell am start -n io.github.katotekiru.me1mobile/com.generalsx.zerohour.SetupActivity --es args "'-noshellmap -file maps\\map_name\\map_name.map'"
adb logcat -s BFME-args
```
- `force-stop` обязателен: игровая activity — `singleInstance`, работающий процесс получает новый intent, но уже разобранный argv не меняет.
- Лаунчер (`SetupActivity`) с extra `args` не показывает экран настроек и сразу запускает игру с этими аргументами. Если GameData не импортирована, в logcat уходит ошибка `BFME-args`, и открывается обычный экран.
- Двойной уровень кавычек: внешние `"…"` снимает shell на ПК, внутренние `'…'` — `sh` на телефоне. Внутри передаётся строка `-noshellmap -file maps\map_name\map_name.map`. Без `-file` режим BFME берёт карту по умолчанию.
- Игровая activity тоже exported (фильтр MAIN), поэтому `-n io.github.katotekiru.me1mobile/com.generalsx.zerohour.GeneralsZHActivity --es args …` тоже работает. Основной путь — через лаунчер.

## Разбор строки
- Токены разделяются пробелами. `'…'` и `"…"` группируют текст с пробелами; соседние куски склеиваются (`"a"'b'c` → `abc`, `""` → пустой токен).
- Экранирования нет: обратная косая черта — обычный символ (`maps\foo\foo.map` проходит как есть). Подстановок переменных нет.
- Пустая строка, незакрытая кавычка или длина больше 4096 символов дают предупреждение `BFME-args` в logcat. Игра запускается без дополнительных аргументов и не падает.

## Включение и выключение
- `BuildConfig.BFME_DEV_ARGS`, по умолчанию `true` (текущие тестовые сборки).
- Сборка для друзей: `BFME_DEV_ARGS=0 tools/build-apk.sh` (передаёт Gradle `-PbfmeDevArgs=false`). Тогда extra игнорируется целиком, в logcat одно предупреждение `args extra ignored: built with bfmeDevArgs=false`.
- В журнале `build-apk.sh` есть строка `BFME_DEV_ARGS: <значение>`.
