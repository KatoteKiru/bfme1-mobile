#!/usr/bin/env bash
# BFME1 Mobile: подготовка облачного контейнера Claude Code к tools/build-linux.sh и tools/build-apk.sh.
#
# В облаке весь HTTPS идёт через агент-прокси ($HTTPS_PROXY, CA /root/.ccr/ca-bundle.crt), и прокси
# отвечает 403 на архивы GitHub (github.com/<o>/<r>/archive/...), хотя git clone работает. Скрипт:
#   1. собирает локальную базу bfme-ubuntu-ca:24.04 (пиновый ubuntu + CA прокси + HTTPS_PROXY)
#      и направляет на неё UBUNTU_IMAGE в docker/versions.env (git update-index --skip-worktree, не коммитить);
#   2. собирает образы сборки и добавляет к ним слой: X_VCPKG_ASSET_SOURCES -> tools/cloud/gh-archive.sh
#      (архив пересобирается из git, SHA512 совпадает с GitHub), для Android ещё CA и прокси для JVM (Gradle);
#   3. кладёт архив openal-soft 1.24.2 (FetchContent, тоже /archive/) туда, где его ищет ExternalProject;
#   4. кладёт шрифты Liberation из системы в staging (ссылки stage-fonts.sh отвечают 403/404).
#
#   tools/cloud/prepare-cloud-build.sh            # всё
#   BFME_CACHE=<dir>                              # как в build-*.sh, по умолчанию <repo>/../cache
#
# dockerd в облаке не запущен и умирает, когда сессия простаивает между ходами. Запускайте его в той же
# фоновой команде, что и сборку:
#   dockerd --data-root /home/claude/docker-data --iptables=false --ip6tables=false --bridge=none &
set -euo pipefail
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$REPO"
CACHE="${BFME_CACHE:-$(cd "$REPO/.." && pwd)/cache}"
CA=/root/.ccr/ca-bundle.crt
[ -n "${HTTPS_PROXY:-}" ] && [ -f "$CA" ] || { echo "нет HTTPS_PROXY или $CA: это не облачный контейнер"; exit 1; }
PORT="${HTTPS_PROXY##*:}"
ts() { echo "=== [$(date '+%F %T %Z')] $*"; }
W="$(mktemp -d)"; trap 'rm -rf "$W"' EXIT
mkdir -p "$CACHE"
cp tools/cloud/gh-archive.sh "$CACHE/gh-archive.sh"

ts "база bfme-ubuntu-ca:24.04"
PIN="$(sed -n 's/^UBUNTU_IMAGE=//p' <(git show HEAD:docker/versions.env))"
cp "$CA" "$W/ca-bundle.crt"
cat > "$W/Dockerfile" <<EOF
FROM $PIN
COPY ca-bundle.crt /tmp/ca-bundle.crt
RUN mkdir -p /usr/local/share/ca-certificates /etc/ssl/certs && cd /usr/local/share/ca-certificates \\
 && awk 'BEGIN{n=0} /BEGIN CERTIFICATE/{n++} {print > ("proxy-" n ".crt")}' /tmp/ca-bundle.crt \\
 && cp /tmp/ca-bundle.crt /etc/ssl/certs/proxy-bundle.pem
ENV SSL_CERT_FILE=/etc/ssl/certs/proxy-bundle.pem CURL_CA_BUNDLE=/etc/ssl/certs/proxy-bundle.pem \\
    GIT_SSL_CAINFO=/etc/ssl/certs/proxy-bundle.pem REQUESTS_CA_BUNDLE=/etc/ssl/certs/proxy-bundle.pem \\
    HTTPS_PROXY=$HTTPS_PROXY https_proxy=$HTTPS_PROXY
EOF
# HTTP_PROXY не задаём: прокси обслуживает только CONNECT, apt по http ходит напрямую.
docker build --network host -q -t bfme-ubuntu-ca:24.04 "$W"
sed -i 's|^UBUNTU_IMAGE=.*|UBUNTU_IMAGE=bfme-ubuntu-ca:24.04|' docker/versions.env
git update-index --skip-worktree docker/versions.env

ts "образ Linux-сборки"
T="$(tools/build-linux.sh --print-image-tag)"
tools/build-linux.sh --image-only
printf 'FROM %s\nENV X_VCPKG_ASSET_SOURCES="x-script,/cache/gh-archive.sh {url} {sha512} {dst}"\n' "$T" > "$W/Dockerfile.linux"
docker build --network host -q -f "$W/Dockerfile.linux" -t "$T" "$W"

ts "образ Android-сборки"
T="$(tools/build-apk.sh --print-image-tag)"
tools/build-apk.sh --image-only
cp /root/.ccr/agent-proxy-ca.crt "$W/agent-proxy-ca.crt"
cat > "$W/Dockerfile.android" <<EOF
FROM $T
COPY agent-proxy-ca.crt /tmp/agent-proxy-ca.crt
RUN (/opt/bfme-toolchain/jdk/bin/keytool -delete -alias ccr-agent-proxy -keystore /opt/bfme-toolchain/jdk/lib/security/cacerts -storepass changeit || true) \\
 && /opt/bfme-toolchain/jdk/bin/keytool -importcert -noprompt -alias ccr-agent-proxy -file /tmp/agent-proxy-ca.crt \\
      -keystore /opt/bfme-toolchain/jdk/lib/security/cacerts -storepass changeit
ENV X_VCPKG_ASSET_SOURCES="x-script,/cache/gh-archive.sh {url} {sha512} {dst}" \\
    JAVA_TOOL_OPTIONS="-Dhttps.proxyHost=127.0.0.1 -Dhttps.proxyPort=$PORT -Dhttp.nonProxyHosts=localhost|127.0.0.1"
EOF
docker build --network host -q -f "$W/Dockerfile.android" -t "$T" "$W"

ts "openal-soft 1.24.2 для FetchContent (cmake/openal.cmake)"
OAL="$CACHE/prefetch/openal-soft-1.24.2.tar.gz"
mkdir -p "$(dirname "$OAL")"
[ -f "$OAL" ] || tools/cloud/gh-archive.sh https://github.com/kcat/openal-soft/archive/refs/tags/1.24.2.tar.gz "" "$OAL"
echo "7efd383d70508587fbc146e4c508771a2235a5fc8ae05bf6fe721c20a348bd7c  $OAL" | sha256sum -c -
for p in linux64-deploy android-vulkan; do
  d="build/$p/_deps/openal_soft-subbuild/openal_soft-populate-prefix/src"
  mkdir -p "$d"; cp "$OAL" "$d/1.24.2.tar.gz"
done

ts "шрифты Liberation (scripts/build/ios/stage-fonts.sh)"
S=/usr/share/fonts/truetype/liberation D="$CACHE/staging/fonts"
if [ -f "$S/LiberationSans-Regular.ttf" ]; then
  mkdir -p "$D"
  cp "$S/LiberationSans-Regular.ttf" "$D/arial.ttf"; cp "$S/LiberationSans-Bold.ttf" "$D/arialbold.ttf"
  cp "$S/LiberationMono-Regular.ttf" "$D/couriernew.ttf"; cp "$S/LiberationSerif-Regular.ttf" "$D/timesnewroman.ttf"
else
  echo "нет $S (apt install fonts-liberation), build-apk.sh попробует скачать сам"
fi

ts "готово: tools/build-linux.sh и tools/build-apk.sh. Maven Central иногда отвечает 429, тогда повторите сборку."
