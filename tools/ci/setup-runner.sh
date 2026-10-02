#!/usr/bin/env bash
# BFME1 Mobile: подготовка self-hosted раннера GitHub Actions на боксе (идемпотентно; повторять после
# отката корневой ФС бокса: пользователь, sudoers и обёртка живут в /, раннер и кэши -- в /workspace).
#
#   tools/ci/setup-runner.sh                 # от пользователя box (нужен sudo)
#   RUNNER_TOKEN=<token> tools/ci/register-runner.sh
#   tools/ci/start-runner.sh                 # фоновый супервизор с автоперезапуском
#
# Раскладка (CI_ROOT=/workspace/bfme-mobile/ci, root:root 0755):
#   actions-runner/   раннер v$RUNNER_VERSION (ghrunner), _work/ -- рабочие копии заданий
#   cache/            ccache, бинарный кэш vcpkg, Gradle (ghrunner)
#   secrets/          bfme-debug.keystore + keystore.pass (ghrunner, 0700/0600) -- копия ключа бокса, вне git
#   logs/             журналы супервизора и раннера
# Доступ к docker: НЕ группа docker (это root на боксе), а sudo только на /usr/local/sbin/bfme-ci-docker.
set -euo pipefail
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
CI_ROOT=/workspace/bfme-mobile/ci
RUNNER_USER=ghrunner RUNNER_UID=1990
RUNNER_VERSION=2.337.0
RUNNER_SHA256=70920811a4f8ad4328818682bca5c6469c1c942fab52448868071d0063816613   # из release notes v2.337.0 (= asset digest API)
RUNNER_TGZ="actions-runner-linux-x64-${RUNNER_VERSION}.tar.gz"
BOX_SECRETS="${BFME_BOX_SECRETS:-/workspace/bfme-mobile/secrets}"

echo "== пользователь $RUNNER_USER (uid $RUNNER_UID, без пароля, без sudo кроме обёртки)"
getent group "$RUNNER_USER" >/dev/null || sudo groupadd -g "$RUNNER_UID" "$RUNNER_USER"
id "$RUNNER_USER" >/dev/null 2>&1 || sudo useradd -u "$RUNNER_UID" -g "$RUNNER_USER" -m -d "/home/$RUNNER_USER" -s /bin/bash "$RUNNER_USER"
sudo passwd -l "$RUNNER_USER" >/dev/null
if id -nG "$RUNNER_USER" | tr ' ' '\n' | grep -qx docker; then sudo gpasswd -d "$RUNNER_USER" docker; fi

echo "== обёртка docker и sudoers"
sudo install -o root -g root -m 0755 "$REPO/tools/ci/bfme-ci-docker" /usr/local/sbin/bfme-ci-docker
tmp="$(mktemp)"
cat > "$tmp" <<SUDO
# BFME1 Mobile CI: раннеру разрешена только узкая обёртка над docker (tools/ci/bfme-ci-docker).
Defaults:$RUNNER_USER env_reset, !setenv
$RUNNER_USER ALL=(root) NOPASSWD: /usr/local/sbin/bfme-ci-docker
SUDO
sudo visudo -cf "$tmp" >/dev/null && sudo install -o root -g root -m 0440 "$tmp" /etc/sudoers.d/bfme-ci-runner; rm -f "$tmp"

echo "== каталоги $CI_ROOT"
sudo install -d -o root -g root -m 0755 "$CI_ROOT"
sudo install -d -o "$RUNNER_USER" -g "$RUNNER_USER" -m 0755 "$CI_ROOT/actions-runner" "$CI_ROOT/cache" "$CI_ROOT/logs"
sudo install -d -o "$RUNNER_USER" -g "$RUNNER_USER" -m 0700 "$CI_ROOT/secrets"

echo "== ключ подписи (копия с бокса, вне git)"
for f in bfme-debug.keystore keystore.pass; do
  if sudo test -f "$BOX_SECRETS/$f" && ! sudo test -f "$CI_ROOT/secrets/$f"; then
    sudo install -o "$RUNNER_USER" -g "$RUNNER_USER" -m 0600 "$BOX_SECRETS/$f" "$CI_ROOT/secrets/$f"
  fi
done
sudo test -f "$CI_ROOT/secrets/bfme-debug.keystore" || echo "ВНИМАНИЕ: нет $CI_ROOT/secrets/bfme-debug.keystore -- CI упадёт (BFME_REQUIRE_KEYSTORE=1)"

echo "== actions/runner v$RUNNER_VERSION"
if ! sudo test -x "$CI_ROOT/actions-runner/config.sh" || [ "$(sudo cat "$CI_ROOT/actions-runner/.bfme-version" 2>/dev/null)" != "$RUNNER_VERSION" ]; then
  dl="$(mktemp -d)"
  curl -fsSL -o "$dl/$RUNNER_TGZ" "https://github.com/actions/runner/releases/download/v${RUNNER_VERSION}/${RUNNER_TGZ}"
  echo "${RUNNER_SHA256}  $dl/$RUNNER_TGZ" | sha256sum -c -
  sudo -u "$RUNNER_USER" tar xzf - -C "$CI_ROOT/actions-runner" < "$dl/$RUNNER_TGZ"
  echo "$RUNNER_VERSION" | sudo -u "$RUNNER_USER" tee "$CI_ROOT/actions-runner/.bfme-version" >/dev/null
  rm -rf "$dl"
fi
# Зависимости .NET раннера (libicu, libkrb5, liblttng-ust, libssl) -- скрипт из самого релиза.
# Ставим, только если раннер не стартует (apt-зеркало бокса бывает недоступно, а на Debian trixie всё уже есть).
if ! sudo -u "$RUNNER_USER" -H bash -c "cd '$CI_ROOT/actions-runner' && ./bin/Runner.Listener --version" >/dev/null 2>&1; then
  sudo "$CI_ROOT/actions-runner/bin/installdependencies.sh"
fi
echo "   Runner.Listener $(sudo -u "$RUNNER_USER" -H bash -c "cd '$CI_ROOT/actions-runner' && ./bin/Runner.Listener --version")"

echo "== окружение заданий (.env раннера; читается при старте run.sh)"
sudo -u "$RUNNER_USER" tee "$CI_ROOT/actions-runner/.env" >/dev/null <<ENV
LANG=C.UTF-8
BFME_DOCKER=sudo -n /usr/local/sbin/bfme-ci-docker
BFME_CACHE=$CI_ROOT/cache
BFME_SECRETS=$CI_ROOT/secrets
BFME_REQUIRE_KEYSTORE=1
BFME_JOBS=4
BFME_MEM_LIMIT=7g
ENV
echo "Готово. Дальше: RUNNER_TOKEN=... tools/ci/register-runner.sh && tools/ci/start-runner.sh"
