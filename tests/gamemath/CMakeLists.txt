# E1-49 GameMath tests. Standalone: no vcpkg, no game link.
#   cmake -S tests/gamemath -B build/tests-gamemath -G Ninja
#   cmake --build build/tests-gamemath
#   ctest --test-dir build/tests-gamemath --output-on-failure
cmake_minimum_required(VERSION 3.25)
project(bfme_gamemath_tests C CXX)

include(CTest)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

set(BFME_REPO_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../.. CACHE PATH "Repository root")

# The directory CMakeLists.txt is the fp gate. The musl target is this fragment.
include(${BFME_REPO_ROOT}/Core/Libraries/Source/GameMath/gamemath_lib.cmake)

add_executable(gamemath_bitexact bitexact.cpp)
target_link_libraries(gamemath_bitexact PRIVATE GameMath)
target_include_directories(gamemath_bitexact PRIVATE
    ${BFME_REPO_ROOT}/Core/Libraries/Include)
target_compile_definitions(gamemath_bitexact PRIVATE
    "GAMEMATH_HASH_FILE=\"${CMAKE_CURRENT_SOURCE_DIR}/host_hashes_x86_64.txt\"")
target_compile_options(gamemath_bitexact PRIVATE -ffp-contract=off)

# The failure mode was #include "GameMath.h" inside class WWMath. This target
# compiles that header the way core_wwmath does when the flag is ON.
set(BFME_DXVK_WINDOWS_DIR "" CACHE PATH "Directory that contains windows_base.h")
if(NOT EXISTS "${BFME_DXVK_WINDOWS_DIR}/windows_base.h")
    set(BFME_DXVK_WINDOWS_DIR "")
    foreach(_dxvk_candidate
        ${BFME_REPO_ROOT}/references/fbraz3-dxvk/include/dxvk
        ${BFME_REPO_ROOT}/references/fbraz3-dxvk/include/native/windows)
        if(EXISTS "${_dxvk_candidate}/windows_base.h")
            set(BFME_DXVK_WINDOWS_DIR "${_dxvk_candidate}")
            break()
        endif()
    endforeach()
endif()
if(NOT EXISTS "${BFME_DXVK_WINDOWS_DIR}/windows_base.h")
    include(FetchContent)
    FetchContent_Declare(bfme_dxvk_headers
        URL https://github.com/doitsujin/dxvk/releases/download/v2.6/dxvk-native-2.6-steamrt-sniper.tar.gz)
    FetchContent_GetProperties(bfme_dxvk_headers)
    if(NOT bfme_dxvk_headers_POPULATED)
        FetchContent_Populate(bfme_dxvk_headers)
    endif()
    set(BFME_DXVK_WINDOWS_DIR "${bfme_dxvk_headers_SOURCE_DIR}/include/dxvk")
endif()
if(NOT EXISTS "${BFME_DXVK_WINDOWS_DIR}/windows_base.h")
    message(FATAL_ERROR "windows_base.h not found; core_wwmath_on cannot compile wwmath.h")
endif()

add_library(core_wwmath_on STATIC wwmath_on.cpp)
target_compile_features(core_wwmath_on PRIVATE cxx_std_20)
target_compile_definitions(core_wwmath_on PRIVATE
    USE_DETERMINISTIC_MATH
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    _UNIX
    LINUX
    RTS_RELEASE
    NDEBUG
    DISABLE_MEMORYPOOL_CHECKPOINTING=1
    DISABLE_MEMORYPOOL_STACKTRACE=1
    HAVE_STRLCPY
    HAVE_STRLCAT
    HAVE_WCSLCPY
    HAVE_WCSLCAT)
target_include_directories(core_wwmath_on PRIVATE
    ${BFME_REPO_ROOT}/Generals/Code/CompatLib/Include
    ${BFME_REPO_ROOT}/Core/Libraries/Source/WWVegas
    ${BFME_REPO_ROOT}/Core/Libraries/Source/WWVegas/WW3D2
    ${BFME_REPO_ROOT}/Core/Libraries/Source/WWVegas/WWAudio
    ${BFME_REPO_ROOT}/Core/Libraries/Source/WWVegas/WWDebug
    ${BFME_REPO_ROOT}/Core/Libraries/Source/WWVegas/WWLib
    ${BFME_REPO_ROOT}/Core/Libraries/Source/WWVegas/WWMath
    ${BFME_REPO_ROOT}/Core/Libraries/Source/WWVegas/WWSaveLoad
    ${BFME_DXVK_WINDOWS_DIR}
    ${BFME_REPO_ROOT}/GeneralsMD/Code/CompatLib/Include
    ${BFME_REPO_ROOT}/Dependencies/Utility
    ${BFME_REPO_ROOT}/Core/Libraries/Include
    ${BFME_REPO_ROOT}/Core/Libraries/Source/GameMath/include)
target_compile_options(core_wwmath_on PRIVATE
    -ffp-contract=off
    -include ${BFME_REPO_ROOT}/Dependencies/Utility/Utility/CppMacros.h)
target_link_libraries(core_wwmath_on PRIVATE GameMath)

add_executable(real_to_int_zh real_to_int.cpp)
target_include_directories(real_to_int_zh PRIVATE
    ${BFME_REPO_ROOT}/Core/Libraries/Include
    ${BFME_REPO_ROOT}/Dependencies/Utility
)
target_compile_options(real_to_int_zh PRIVATE -ffp-contract=off)

add_test(NAME gamemath.helpers COMMAND gamemath_bitexact --helpers)
add_test(NAME gamemath.host_deterministic COMMAND gamemath_bitexact --self-check)
add_test(NAME gamemath.bitexact_x86_64_arm64
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/compare_bitexact.py
            --repo ${BFME_REPO_ROOT}
            --host $<TARGET_FILE:gamemath_bitexact>
            --build-dir ${CMAKE_CURRENT_BINARY_DIR})
# A missing qemu is a skip, not a failure. A hash mismatch is a failure.
set_tests_properties(gamemath.bitexact_x86_64_arm64 PROPERTIES
    SKIP_REGULAR_EXPRESSION "^SKIP:"
)
add_test(NAME gamemath.real_to_int_zh COMMAND real_to_int_zh)
