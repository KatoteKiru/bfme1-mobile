# tools — инструменты

Здесь уже лежат инструменты апстрима (`dxvk-refcount-litmus/` и др.), их не трогаем без нужды.

Инструменты BFME1 Mobile:

- `tools/cloud/` — сборка в облачном контейнере Claude Code за агент-прокси: `prepare-cloud-build.sh` готовит образы
  (CA прокси, vcpkg-архивы GitHub через `gh-archive.sh`, прокси для Gradle), openal-soft и шрифты; затем обычные
  `tools/build-linux.sh` / `tools/build-apk.sh`. Правит `docker/versions.env` только локально (skip-worktree).
- `tools/datacheck/` — «инвентаризация данных»: читает все INI/BIG BFME1 и выдаёт отчёт по
  неизвестным полям и модулям с частотой использования. Владелец: **Форматы и данные**.

Данные игры инструменты читают из внешнего каталога (на боксе `/workspace/bfme-mobile/gamedata/`),
в git ничего из них не попадает.
