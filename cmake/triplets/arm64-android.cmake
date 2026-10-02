# Overlay triplet: pin the Android API level so vcpkg-built static libs
# (freetype, curl, openssl, ffmpeg, ...) match the engine's ANDROID_PLATFORM
# (android-29, see the android-vulkan preset; BFME1 Mobile minSdk 29). Without the pin, vcpkg builds
# against the community triplet's default API level and the final link can
# pull in symbols missing on the oldest supported devices (Redmi Note 8 Pro
# tops out at Android 11 stock; Android 9 = API 28 keeps headroom for LineageOS).
# Requires ANDROID_NDK_HOME in the environment (vcpkg reads it to find the NDK).
set(VCPKG_TARGET_ARCHITECTURE arm64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_CMAKE_SYSTEM_NAME Android)
set(VCPKG_MAKE_BUILD_TRIPLET "--host=aarch64-linux-android")
set(VCPKG_CMAKE_CONFIGURE_OPTIONS -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-29 -DANDROID_SUPPORT_FLEXIBLE_PAGE_SIZES=ON -DANDROID_STL=c++_shared)
# BFME1 Mobile @build Android 02/10/2026 16 KB page alignment for any shared object vcpkg links.
set(VCPKG_LINKER_FLAGS "-Wl,-z,max-page-size=16384 -Wl,-z,common-page-size=16384")
# BFME1 Mobile @build Android 02/10/2026 only release variants of vcpkg deps: the engine
# is built RelWithDebInfo (maps to release libs), so the -dbg build only doubled CI/box time.
set(VCPKG_BUILD_TYPE release)
