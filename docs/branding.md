# Брендинг BFME1 Mobile

- Название приложения: `BFME1 Mobile` (`app_name`), подпись на рабочем столе: `BFME1` (`app_name_short`, `android:label` у `SetupActivity`). `applicationId` `io.github.katotekiru.me1mobile`, пакеты, классы, JNI, теги логов и имена файлов не менялись.
- Иконка от Дизайнера: башня с зубцами на скале (старое золото `#C9A24A`, бронза `#8A6A2E`) на тёмном фоне `#2A2622`; монохром — белый силуэт, в теме Android 13+ система красит его сама. Собственная графика, без логотипов и ассетов EA / Warner Bros.

## Слои adaptive icon
- Каждый слой 108×108 dp, безопасная зона — круг 66 dp в центре (радиус 33 dp от точки 54,54); маска лаунчера может обрезать всё, что снаружи 72 dp.
- `android/app/src/main/res/drawable/ic_launcher_foreground.xml`, `ic_launcher_background.xml`, `ic_launcher_monochrome.xml` — VectorDrawable, pathData перенесены из SVG 1:1 (рендер совпал с `*_432.png` Дизайнера попиксельно).
- `mipmap-anydpi-v26/ic_launcher.xml` и `ic_launcher_round.xml` — `<background>`, `<foreground>`, `<monochrome>`. `minSdk` 29, поэтому они используются всегда; PNG `mipmap-{mdpi..xxxhdpi}/ic_launcher*.png` (48–192 px) лежат как запасной вариант по комплекту Дизайнера.
- Исходники: `docs/branding/*.svg`, `preview.png` (проверка под масками), `playstore-512.png` (для стора, в APK не входит).

## Как заменить арт
1. Новые SVG 108×108 сконвертировать в VectorDrawable (Android Studio → Vector Asset) и положить поверх трёх файлов в `res/drawable/` под теми же именами. Если конвертация с потерями — PNG 432×432 в `res/drawable-xxxhdpi/` с теми же именами (и удалить XML).
2. Заменить `mipmap-*/ic_launcher.png` и `ic_launcher_round.png`.
3. Имена ресурсов не менять: манифест и `mipmap-anydpi-v26` ссылаются на них.
