#!/usr/bin/env bash
# BFME1 Mobile: регистрация раннера в KatoteKiru/bfme-mobile. Токен регистрации живёт 1 час, выдаёт админ репозитория:
#   Settings -> Actions -> Runners -> New self-hosted runner (строка --token ...), или
#   gh api -X POST repos/KatoteKiru/bfme-mobile/actions/runners/registration-token --jq .token
#   RUNNER_TOKEN=<token> tools/ci/register-runner.sh
set -euo pipefail
: "${RUNNER_TOKEN:?нужен RUNNER_TOKEN (токен регистрации, см. шапку)}"
CI_ROOT=/workspace/bfme-mobile/ci
URL="${RUNNER_URL:-https://github.com/KatoteKiru/bfme-mobile}"
# Токен передаётся через stdin sudo -> env, а не в командной строке (её видно в ps).
printf '%s' "$RUNNER_TOKEN" | sudo -u ghrunner -H bash -c '
  cd '"$CI_ROOT"'/actions-runner && T="$(cat)" &&
  ./config.sh --unattended --replace --url "'"$URL"'" --token "$T" \
    --name "${RUNNER_NAME:-bfme-box}" --labels bfme,android-build --work _work'
echo "Зарегистрирован. Метки: self-hosted, Linux, X64, bfme, android-build. Запуск: tools/ci/start-runner.sh"
