# Образ сборки BFME1 Mobile (Android arm64-v8a). Канонический для CI и бокса.
# Версии и контрольные суммы: docker/versions.env (передаются через --build-arg из tools/build-apk.sh).
# Сборка:  tools/build-apk.sh --image-only
ARG UBUNTU_IMAGE=ubuntu:24.04@sha256:a853f94d226358a79c740cfc7bce0c289748f3fe3488d921d038ccd752c61b60
FROM ${UBUNTU_IMAGE}

# Снимок архива Ubuntu (snapshot.ubuntu.com): одинаковые версии apt-пакетов при любой пересборке.
# Опционально: снимок архива (например 20261001T000000Z). На 02.10.2026 в образе noble
# источники без поддержки snapshot, поэтому по умолчанию выключено; версии фиксирует apt-manifest.txt.
ARG UBUNTU_SNAPSHOT=
ENV DEBIAN_FRONTEND=noninteractive LANG=C.UTF-8 LC_ALL=C.UTF-8
COPY docker/apt-packages.txt /tmp/apt-packages.txt
RUN set -eux; \
    if [ -n "${UBUNTU_SNAPSHOT}" ]; then apt-get update --snapshot "${UBUNTU_SNAPSHOT}"; SNAP="--snapshot ${UBUNTU_SNAPSHOT}"; \
    else apt-get update; SNAP=""; fi; \
    apt-get install -y --no-install-recommends ${SNAP} $(grep -v '^#' /tmp/apt-packages.txt); \
    rm -rf /var/lib/apt/lists/* /tmp/apt-packages.txt

COPY docker/versions.env /opt/bfme-src/docker/versions.env
COPY tools/install-toolchain.sh /opt/bfme-src/tools/install-toolchain.sh
RUN /opt/bfme-src/tools/install-toolchain.sh /opt/bfme-toolchain \
 && chmod -R a+rwX /opt/bfme-toolchain/vcpkg /opt/bfme-toolchain/vcpkg-linux \
 && dpkg-query -W -f '${Package}=${Version}\n' > /opt/bfme-toolchain/apt-manifest.txt

ENV BFME_TOOLCHAIN=/opt/bfme-toolchain \
    JAVA_HOME=/opt/bfme-toolchain/jdk \
    ANDROID_HOME=/opt/bfme-toolchain/android-sdk \
    ANDROID_SDK_ROOT=/opt/bfme-toolchain/android-sdk \
    ANDROID_NDK_HOME=/opt/bfme-toolchain/android-sdk/ndk/27.2.12479018 \
    ANDROID_NDK_ROOT=/opt/bfme-toolchain/android-sdk/ndk/27.2.12479018 \
    VCPKG_ROOT=/opt/bfme-toolchain/vcpkg \
    BFME_VCPKG_LINUX_ROOT=/opt/bfme-toolchain/vcpkg-linux \
    VCPKG_DISABLE_METRICS=1 \
    BFME_IN_CONTAINER=1 \
    PATH=/opt/bfme-toolchain/bin:/opt/bfme-toolchain/cmake/bin:/opt/bfme-toolchain/gradle/bin:/opt/bfme-toolchain/jdk/bin:/opt/bfme-toolchain/android-sdk/build-tools/35.0.0:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
# Git внутри контейнера работает с примонтированным чужим (по uid) деревом.
RUN git config --system --add safe.directory '*'
WORKDIR /src
