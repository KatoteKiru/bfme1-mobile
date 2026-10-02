# CI BFME1 Mobile: self-hosted раннер на боксе (E1-03)

GitHub-машины не используем: все наши workflow — `runs-on: [self-hosted, linux, bfme]`, `actions/checkout` с `fetch-depth: 1`.

## Установка (один раз и после отката корневой ФС бокса)
```
tools/start-dockerd.sh                                   # dockerd (data-root в /workspace)
tools/ci/setup-runner.sh                                 # пользователь, обёртка, sudoers, nft-изоляция, раннер, .env
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
| `/usr/local/sbin/bfme-ci-check-isolation` | root 0755 | копия `tools/ci/check-isolation.sh` (её зовут супервизор и workflow) |
| nft `table inet bfme_ci` | ядро | правила `tools/ci/firewall.sh` (не переживают перезапуск бокса, ставит `start-runner.sh`) |

## Почему не группа docker
Доступ к `docker.sock` равен root на боксе (`docker run -v /:/host`), а раннер исполняет код из PR.
Поэтому ghrunner не в группе docker, а через sudo вызывает только `bfme-ci-docker`, которая пропускает ровно вызовы
`tools/build-apk.sh` (`BFME_DOCKER="sudo -n /usr/local/sbin/bfme-ci-docker"` в `.env` раннера):
- `info`, `image inspect bfme-android-build:<12 hex>`;
- `build` только `docker/android-build.Dockerfile`: обёртка сама копирует 5 файлов контекста в свой каталог;
- `run` только образа `bfme-android-build:<12 hex>`, `-u` = uid:gid вызывающего, `--cap-drop=ALL`,
  `no-new-privileges`, `--pids-limit`; `-v` только из `ci/{actions-runner/_work,cache,secrets}`. Путь открывается
  по компонентам с `O_NOFOLLOW` и монтируется через `/proc/self/fd`, так что подмена симлинком не выводит за эти каталоги.
- Сеть контейнеров: только `--network host`. У нашего dockerd `--bridge=none`, bridge-сети нет вовсе, так что
  пути «контейнер → шлюз/IP хоста через INPUT» нет. Все соединения раннера и контейнеров проходят хук OUTPUT хоста.

## Сетевая изоляция (tools/ci/firewall.sh)
На боксе `0.0.0.0:2375` отдаёт Docker API **без аутентификации** (инфраструктура бокса, отдельный демон с
`DockerRootDir=/var/lib/docker`; не трогаем). Через него задание обошло бы `bfme-ci-docker`. `iptables -m owner` на боксе
не работает (iptables-nft без `xt_owner`), поэтому правила — нативный nftables, таблица `inet bfme_ci`, хук output:

| Правило | Кого режет |
|---|---|
| `tcp dport {2375,2376} meta skuid 1990` | шаги на хосте от ghrunner, контейнеры обёртки (`-u 1990`) |
| `tcp dport {2375,2376} socket cgroupv2 level 1 "docker"` | любой процесс в контейнерах нашего dockerd, в т.ч. root и `RUN` в `docker build` |
| `fib daddr type local tcp dport {0-32767,50052}` × (uid 1990 \| cgroup docker) | служебные порты бокса: CDP браузера, VNC, websockify, агентские сервисы |

Эфемерный диапазон 32768–60999 открыт: в нём слушают демоны самого задания (Gradle, Kotlin). Другие пользователи бокса
не затронуты. Fail closed: `start-runner.sh` применяет правила и не стартует, если `firewall.sh check` или
`bfme-ci-check-isolation` от ghrunner не проходят; супервизор повторяет проверку перед каждым `run.sh`; workflow
проверяет `! curl -s -m 2 127.0.0.1:2375/version` и полный набор (127.0.0.1, IP хоста, шлюз) на хосте и в контейнере.

Остаточные риски (вне TCP, nft их не видит; это инфраструктура бокса, решать её владельцам):
- X11 без авторизации: `/tmp/.X11-unix/X*` (0777) и абстрактные сокеты `@/tmp/.X11-unix/X*` доступны ghrunner и
  контейнерам с `--network host` → можно управлять рабочими столами и браузерами агентов. Нужен `-nolisten local` + xauth
  у Xvfb, либо Runner.Listener и контейнеры в отдельном netns с userspace-сетью (pasta/slirp4netns).
- `/run/host-services/ssh-auth.sock` (root, 0666): ssh-agent с 1 ключом доступен ghrunner на хосте (в контейнеры не монтируется).

## Задания
- `.github/workflows/bfme-android-apk.yml`: `tools/build-apk.sh` → APK, символы, журнал в артефакты. PR из форков не запускаются.
- Тесты в образе: `$BFME_DOCKER run --rm --network host -u "$(id -u):$(id -g)" -e HOME=/tmp -v "$PWD:/src" -w /src <tag> bash -c '…'`,
  тег — та же формула, что в `tools/build-apk.sh` (sha256 Dockerfile + versions.env + apt-packages.txt + install-toolchain.sh, 12 символов).
