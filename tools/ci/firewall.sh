#!/usr/bin/env bash
# BFME1 Mobile CI: сетевая изоляция раннера от служебных портов бокса (nftables, таблица inet bfme_ci).
#   tools/ci/firewall.sh apply   # поставить/обновить правила (идемпотентно, одной транзакцией)
#   tools/ci/firewall.sh check   # правила на месте и реально режут; иначе exit 1 (fail closed)
#
# Зачем: на боксе 0.0.0.0:2375 отдаёт Docker API БЕЗ аутентификации (инфраструктура бокса, не наш dockerd;
# демон не трогаем). Шаги CI идут на хосте от ghrunner, контейнеры нашего dockerd -- с --network host
# (у нашего dockerd --bridge=none, bridge-сети нет вовсе), а RUN в docker build идёт от root тоже в сети хоста.
# Любой из них мог бы через 2375 запустить привилегированный контейнер и обойти bfme-ci-docker.
#
# Почему nft, а не iptables -m owner: на боксе iptables-nft без xt_owner
# ("Extension owner revision 0 not supported"), а нативные nft-выражения meta skuid / socket cgroupv2 работают.
# Хук output ловит и 127.0.0.1, и IP хоста (локальная доставка тоже проходит OUTPUT), IPv4 и IPv6 (inet).
#
# Правила (только наш uid и наши контейнеры, остальные пользователи бокса не затронуты):
#   1. ghrunner (uid 1990)          -> tcp 2375/2376 на любой адрес                 reject
#   2. процессы в cgroup /docker/*  -> tcp 2375/2376 на любой адрес                 reject
#      (все контейнеры и RUN-шаги docker build нашего dockerd, в т.ч. от root внутри)
#   3-4. то же для локальных адресов бокса (fib daddr type local) и tcp-портов 0-32767 + 50052:
#      служебные порты бокса (CDP браузера, VNC, websockify, агентские сервисы). Эфемерный диапазон
#      32768-60999 открыт: в нём слушают свои же демоны задания (Gradle daemon, Kotlin daemon).
set -euo pipefail
RUNNER_UID=1990
TABLE="inet bfme_ci"
NFT="$(command -v nft || echo /usr/sbin/nft)"
DOCKER_PORTS="2375, 2376"
SERVICE_PORTS="0-32767, 50052"

apply() {
  # cgroup /docker создаёт наш dockerd (cgroupfs) при первом контейнере; nft требует, чтобы путь существовал.
  sudo test -d /sys/fs/cgroup/docker || sudo mkdir /sys/fs/cgroup/docker
  sudo "$NFT" -f - <<NFT
table $TABLE
delete table $TABLE
table $TABLE {
	chain output {
		type filter hook output priority filter; policy accept;
		tcp dport { $DOCKER_PORTS } meta skuid $RUNNER_UID counter reject with tcp reset comment "bfme-ci: ghrunner -> Docker API"
		tcp dport { $DOCKER_PORTS } socket cgroupv2 level 1 "docker" counter reject with tcp reset comment "bfme-ci: containers -> Docker API"
		fib daddr type local tcp dport { $SERVICE_PORTS } meta skuid $RUNNER_UID counter reject with tcp reset comment "bfme-ci: ghrunner -> box services"
		fib daddr type local tcp dport { $SERVICE_PORTS } socket cgroupv2 level 1 "docker" counter reject with tcp reset comment "bfme-ci: containers -> box services"
	}
}
NFT
}

check() {
  local rules n
  rules="$(sudo "$NFT" list table $TABLE 2>/dev/null)" || { echo "firewall: нет таблицы $TABLE" >&2; return 1; }
  n="$(grep -c 'reject with tcp reset comment "bfme-ci:' <<<"$rules" || true)"
  [ "$n" = 4 ] || { echo "firewall: в $TABLE $n правил из 4" >&2; return 1; }
  # Функциональная проверка: ghrunner не достаёт 2375/2376 ни по 127.0.0.1, ни по IP хоста.
  local addrs a p bad=0
  addrs="127.0.0.1 $(awk '/32 host/ {print f} {f=$2}' /proc/net/fib_trie | sort -u | grep -v '^127\.' | tr '\n' ' ')"
  for a in $addrs; do for p in 2375 2376; do
    if sudo -u "#$RUNNER_UID" timeout 3 bash -c "exec 3<>/dev/tcp/$a/$p" 2>/dev/null; then
      echo "firewall: uid $RUNNER_UID достаёт $a:$p" >&2; bad=1
    fi
  done; done
  return $bad
}

case "${1:-}" in
  apply) apply; check && echo "firewall: $TABLE применена и проверена" ;;
  check) check && echo "firewall: $TABLE на месте" ;;
  *) echo "usage: $0 apply|check" >&2; exit 2 ;;
esac
