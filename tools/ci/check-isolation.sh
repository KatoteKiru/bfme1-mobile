#!/usr/bin/env bash
# BFME1 Mobile CI: проверка из задания, что служебные порты бокса недоступны (см. tools/ci/firewall.sh).
#   tools/ci/check-isolation.sh                     # на хосте, от пользователя раннера
#   tools/ci/check-isolation.sh --container <tag>   # то же внутри контейнера через $BFME_DOCKER (--network host)
# Падает (exit 1), если доступен Docker API (2375/2376) по 127.0.0.1, IP хоста или шлюзу,
# или любой слушающий tcp-порт бокса из диапазона 0-32767 / 50052.
set -uo pipefail
if [ "${1:-}" = "--container" ]; then
  tag="${2:?tag}"
  # Скрипт передаём через stdin: монтировать рабочую копию ради одного файла незачем.
  exec ${BFME_DOCKER:-docker} run --rm --network host -u "$(id -u):$(id -g)" -w /tmp "$tag" \
    bash -c "$(cat "${BASH_SOURCE[0]}")" check-isolation --inside
fi

where="хост, uid $(id -u)"; [ "${1:-}" = "--inside" ] && where="контейнер, uid $(id -u)"
probe() { timeout 3 bash -c "exec 3<>/dev/tcp/$1/$2" 2>/dev/null; }
hex2ip() { local h=$1; printf '%d.%d.%d.%d' 0x${h:6:2} 0x${h:4:2} 0x${h:2:2} 0x${h:0:2}; }

host_ips="$(awk '/32 host/ {print f} {f=$2}' /proc/net/fib_trie | sort -u | tr '\n' ' ')"
gw="$(awk 'NR>1 && $2=="00000000" {print $3; exit}' /proc/net/route)"; gw_ip=""; [ -n "$gw" ] && gw_ip="$(hex2ip "$gw")"
echo "[$where] адреса хоста: $host_ips шлюз: ${gw_ip:-нет}"
fail=0
for a in $(printf '%s\n' 127.0.0.1 $host_ips $gw_ip | sort -u); do for p in 2375 2376; do
  if probe "$a" "$p"; then echo "::error::[$where] ДОСТУПЕН $a:$p (Docker API)"; fail=1; else echo "[$where] $a:$p закрыт"; fi
done; done
# Лидовская проверка дословно
if curl -s -m 2 127.0.0.1:2375/version >/dev/null; then echo "::error::[$where] curl 127.0.0.1:2375/version отвечает"; fail=1
else echo "[$where] ! curl -s -m 2 127.0.0.1:2375/version: ok"; fi

# Все слушающие tcp-порты сетевого пространства хоста (/proc/net/tcp{,6}, состояние 0A = LISTEN)
ports="$(for f in /proc/net/tcp /proc/net/tcp6; do while read -r _ l _ st _; do
  [ "$st" = 0A ] && echo $((16#${l##*:})); done < <(tail -n +2 "$f" 2>/dev/null); done | sort -nu)"
open=(); for p in $ports; do probe 127.0.0.1 "$p" && open+=("$p"); done
svc=(); eph=(); for p in "${open[@]}"; do if [ "$p" -lt 32768 ] || [ "$p" = 50052 ]; then svc+=("$p"); else eph+=("$p"); fi; done
echo "[$where] слушающих портов: $(wc -w <<<"$ports"), доступны с 127.0.0.1: служебные [${svc[*]:-}] эфемерные [${eph[*]:-}]"
[ ${#svc[@]} -eq 0 ] || { echo "::error::[$where] доступны служебные порты бокса: ${svc[*]}"; fail=1; }
[ $fail = 0 ] && echo "[$where] ИТОГ: изоляция ok" || echo "[$where] ИТОГ: FAIL"
exit $fail
