# CI BFME1 Mobile: self-hosted раннер на боксе (E1-03)

GitHub-машины не используем: все наши workflow — `runs-on: [self-hosted, linux, bfme]`, `actions/checkout` с `fetch-depth: 1`.

## Установка (один раз и после отката корневой ФС бокса)
```
tools/start-dockerd.sh                                   # dockerd (data-root в /workspace)
tools/ci/setup-runner.sh                                 # пользователь, обёртка, sudoers, раннер, .env
RUNNER_TOKEN=<токен> tools/ci/register-runner.sh         # регистрация в KatoteKiru/bfme-mobile
tools/ci/start-runner.sh                                 # супервизор; status | stop
```
Токен регистрации (живёт 1 час) выдаёт админ репозитория: Settings → Actions → Runners → New self-hosted runner,
или `gh api -X POST repos/KatoteKiru/bfme-mobile/actions/runners/registration-token --jq .token`.
Actions в репозитории включает Лид движка (Settings → Actions → General), только после регистрации раннера.

## Раскладка
| Путь | Владелец | Что |
|---|---|---|
| `/workspace/bfme-mobile/ci/` | root 0755 | корень CI (на /workspace: переживает откат ВМ) |
| `ci/actions-runner/` | ghrunner | actions/runner 2.337.0 (sha256 70920811…6613 сверяется), `_work/` |
| `ci/cache/` | ghrunner | ccache, бинарный кэш vcpkg, Gradle, staging (`BFME_CACHE`) |
| `ci/secrets/` | ghrunner 0700 | `bfme-debug.keystore`, `keystore.pass` (0600), копия ключа бокса (`BFME_SECRETS`) |
| `ci/logs/` | ghrunner | `supervisor.log`, pid |
| `/usr/local/sbin/bfme-ci-docker` | root 0755 | узкая обёртка над docker |
| `/etc/sudoers.d/bfme-ci-runner` | root 0440 | `ghrunner ALL=(root) NOPASSWD: /usr/local/sbin/bfme-ci-docker` |

## Почему не группа docker
Доступ к `docker.sock` равен root на боксе (`docker run -v /:/host`), а раннер исполняет код из PR.
Поэтому ghrunner не в группе docker, а через sudo вызывает только `bfme-ci-docker`, которая пропускает ровно вызовы
`tools/build-apk.sh` (`BFME_DOCKER="sudo -n /usr/local/sbin/bfme-ci-docker"` в `.env` раннера):
- `info`, `image inspect bfme-android-build:<12 hex>`;
- `build` только `docker/android-build.Dockerfile`: обёртка сама копирует 5 файлов контекста в свой каталог;
- `run` только образа `bfme-android-build:<12 hex>`, `-u` = uid:gid вызывающего, `--cap-drop=ALL`,
  `no-new-privileges`, `--pids-limit`; `-v` только из `ci/{actions-runner/_work,cache,secrets}`. Путь открывается
  по компонентам с `O_NOFOLLOW` и монтируется через `/proc/self/fd`, так что подмена симлинком не выводит за эти каталоги.
- Остаточный риск: `--network host` (dockerd бокса без моста) — задание видит сетевые сервисы бокса на localhost.

## Задания
- `.github/workflows/bfme-android-apk.yml`: `tools/build-apk.sh` → APK, символы, журнал в артефакты. PR из форков не запускаются.
- Тесты в образе: `$BFME_DOCKER run --rm --network host -u "$(id -u):$(id -g)" -e HOME=/tmp -v "$PWD:/src" -w /src <tag> bash -c '…'`,
  тег — та же формула, что в `tools/build-apk.sh` (sha256 Dockerfile + versions.env + apt-packages.txt + install-toolchain.sh, 12 символов).
