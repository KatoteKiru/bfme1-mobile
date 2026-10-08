# Образ десктопной Linux-сборки BFME1 Mobile (linux64-deploy, x86_64), E1-05.
# Основа: resources/dockerbuild/Dockerfile.linux форка; та же база Ubuntu, CMake и Ninja, что в android-build.Dockerfile.
# Плюс Mesa lavapipe + Xvfb для запуска без GPU. Сборка и тег: tools/build-linux.sh --image-only / --print-image-tag.
ARG UBUNTU_IMAGE=ubuntu:24.04@sha256:a853f94d226358a79c740cfc7bce0c289748f3fe3488d921d038ccd752c61b60
FROM ${UBUNTU_IMAGE}

ENV DEBIAN_FRONTEND=noninteractive LANG=C.UTF-8 LC_ALL=C.UTF-8
COPY docker/linux-apt-packages.txt /tmp/apt-packages.txt
RUN set -eux; apt-get update; \
    apt-get install -y --no-install-recommends $(grep -v '^#' /tmp/apt-packages.txt); \
    rm -rf /var/lib/apt/lists/* /tmp/apt-packages.txt

COPY docker/versions.env /opt/bfme-src/docker/versions.env
COPY tools/install-linux-toolchain.sh /opt/bfme-src/tools/install-linux-toolchain.sh
RUN /opt/bfme-src/tools/install-linux-toolchain.sh /opt/bfme-linux-toolchain \
 && chmod -R a+rwX /opt/bfme-linux-toolchain/vcpkg \
 && dpkg-query -W -f '${Package}=${Version}\n' > /opt/bfme-linux-toolchain/apt-manifest.txt

ENV BFME_TOOLCHAIN=/opt/bfme-linux-toolchain \
    VCPKG_ROOT=/opt/bfme-linux-toolchain/vcpkg \
    VCPKG_DISABLE_METRICS=1 \
    BFME_IN_CONTAINER=1 \
    PATH=/opt/bfme-linux-toolchain/bin:/opt/bfme-linux-toolchain/cmake/bin:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
RUN git config --system --add safe.directory '*'
WORKDIR /src
