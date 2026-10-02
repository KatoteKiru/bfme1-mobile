#!/usr/bin/env bash
# Поднимает dockerd на общем боксе BFME1 Mobile с минимальным вмешательством в сеть хоста.
# Бокс без systemd; корневая ФС бокса может откатываться при восстановлении ВМ,
# поэтому data-root лежит в /workspace (переживает откат), а пакеты при нужде ставятся заново.
#  --iptables=false --bridge=none : dockerd не трогает iptables и не создаёт docker0
#                                   (сборки идут с --network host).
#  fuse-overlayfs                  : overlay2 на overlay-корне бокса не монтируется.
set -euo pipefail
DATA_ROOT="${BFME_DOCKER_DATA_ROOT:-/workspace/bfme-mobile/.docker-data}"
if ! command -v docker >/dev/null || [ ! -x /usr/sbin/dockerd ] || ! command -v fuse-overlayfs >/dev/null; then
  sudo apt-get update -qq
  sudo DEBIAN_FRONTEND=noninteractive apt-get install -y -qq -o Dpkg::Options::=--force-confold docker.io fuse-overlayfs
fi
if sudo docker info >/dev/null 2>&1; then echo "dockerd уже работает"; exit 0; fi
sudo mkdir -p "$DATA_ROOT"
sudo ip link del docker0 2>/dev/null || true
sudo nohup /usr/sbin/dockerd --data-root "$DATA_ROOT" --storage-driver=fuse-overlayfs \
  --iptables=false --ip6tables=false --bridge=none --ip-forward=false --ip-masq=false \
  > /tmp/dockerd.log 2>&1 &
for i in $(seq 1 30); do sudo docker info >/dev/null 2>&1 && { echo "dockerd запущен (data-root=$DATA_ROOT)"; exit 0; }; sleep 1; done
echo "dockerd не поднялся, см. /tmp/dockerd.log"; tail -20 /tmp/dockerd.log; exit 1
