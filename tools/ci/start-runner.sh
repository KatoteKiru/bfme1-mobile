#!/usr/bin/env bash
# BFME1 Mobile: запуск раннера фоновым супервизором с автоперезапуском (на боксе нет systemd).
#   tools/ci/start-runner.sh          # старт (идемпотентно)
#   tools/ci/start-runner.sh stop     # остановка (текущее задание раннер не доделает)
#   tools/ci/start-runner.sh status
# После перезапуска бокса: tools/start-dockerd.sh && tools/ci/setup-runner.sh && tools/ci/start-runner.sh
# Fail closed: без правил tools/ci/firewall.sh (закрыт Docker API 0.0.0.0:2375 и служебные порты бокса)
# раннер не стартует, а супервизор перед каждым запуском run.sh повторяет проверку изоляции.
set -euo pipefail
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
CI_ROOT=/workspace/bfme-mobile/ci
CHECK=/usr/local/sbin/bfme-ci-check-isolation
PIDF="$CI_ROOT/logs/supervisor.pid"
running() { [ -f "$PIDF" ] && sudo kill -0 "$(cat "$PIDF")" 2>/dev/null; }
case "${1:-start}" in
  status) if running; then echo "супервизор работает (pid $(cat "$PIDF"))"; else echo "не запущен"; fi
          sudo tail -5 "$CI_ROOT/logs/supervisor.log" 2>/dev/null || true ;;
  stop)   if running; then sudo touch "$CI_ROOT/logs/stop"; sudo kill -TERM -- "-$(cat "$PIDF")" 2>/dev/null || sudo kill -TERM "$(cat "$PIDF")"; echo остановлен; else echo "не запущен"; fi ;;
  start)
    running && { echo "уже работает (pid $(cat "$PIDF"))"; exit 0; }
    sudo test -f "$CI_ROOT/actions-runner/.runner" || { echo "раннер не зарегистрирован: tools/ci/register-runner.sh"; exit 1; }
    sudo docker info >/dev/null 2>&1 || { echo "dockerd не запущен: tools/start-dockerd.sh"; exit 1; }
    "$REPO/tools/ci/firewall.sh" apply || { echo "ОТКАЗ: правила nft не применились, раннер не запускаю"; exit 1; }
    sudo test -x "$CHECK" || { echo "нет $CHECK: tools/ci/setup-runner.sh"; exit 1; }
    sudo -u ghrunner -H bash -c "cd /tmp && $CHECK" >/dev/null || { echo "ОТКАЗ: ghrunner достаёт служебные порты (см. sudo -u ghrunner $CHECK)"; exit 1; }
    sudo rm -f "$CI_ROOT/logs/stop"
    sudo -u ghrunner -H setsid nohup bash -c '
      exec >> '"$CI_ROOT"'/logs/supervisor.log 2>&1
      echo $$ > '"$PIDF"'
      cd '"$CI_ROOT"'/actions-runner
      delay=5
      while [ ! -f '"$CI_ROOT"'/logs/stop ]; do
        if ! (cd /tmp && '"$CHECK"') > /dev/null; then
          echo "[$(date "+%F %T %Z")] ОТКАЗ: изоляция нарушена (правила nft пропали?), run.sh не запускаю; tools/ci/firewall.sh apply"
          sleep 60; continue
        fi
        echo "[$(date "+%F %T %Z")] run.sh старт"
        start=$(date +%s); ./run.sh; rc=$?
        echo "[$(date "+%F %T %Z")] run.sh вышел rc=$rc"
        # быстрые падения подряд -> экспоненциальная пауза до 5 минут
        if [ $(( $(date +%s) - start )) -lt 60 ]; then delay=$(( delay*2 > 300 ? 300 : delay*2 )); else delay=5; fi
        sleep "$delay"
      done' > /dev/null 2>&1 < /dev/null &
    sleep 2; running && echo "запущен (pid $(cat "$PIDF")), журнал $CI_ROOT/logs/supervisor.log" || { echo "не стартовал"; sudo tail -20 "$CI_ROOT/logs/supervisor.log"; exit 1; } ;;
esac
