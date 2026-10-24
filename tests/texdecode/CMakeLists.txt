# BFME1 Mobile — E1-44: JPEG/PNG texture decode (BfmeImage::Decode_BGRA, stb_image v2.30)
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Standalone: compiles the real Core/.../WW3D2/bfmeimagedecode.cpp (the only stb_image
# implementation TU of the engine) without D3D or the engine file system. The test images are
# generated in memory with stb_image_write; no game data.
#
#   cmake -S tests/texdecode -B build/tests-texdecode && cmake --build build/tests-texdecode
#   ctest --test-dir build/tests-texdecode --output-on-failure
cmake_minimum_required(VERSION 3.25)
project(bfme_texdecode_tests CXX)

include(CTest)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

get_filename_component(BFME_REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(WW3D2 "${BFME_REPO_ROOT}/Core/Libraries/Source/WWVegas/WW3D2")

add_executable(bfme_texdecode_tests
    ${BFME_REPO_ROOT}/tests/heights/harness/TestMain.cpp
    src/TestTexDecode.cpp
    ${WW3D2}/bfmeimagedecode.cpp
)
target_include_directories(bfme_texdecode_tests PRIVATE
    ${WW3D2}
    ${BFME_REPO_ROOT}/Dependencies/stb
    ${BFME_REPO_ROOT}/tests/heights/harness
    # stb_image_write v1.15, only to synthesise test images
    ${BFME_REPO_ROOT}/GeneralsMD/Code/GameEngine/Include/GameNetwork/GeneralsOnline/Vendor/stb_image
)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(bfme_texdecode_tests PRIVATE -Wall -Wextra)
endif()
option(BFME_TESTS_ASAN "Build with AddressSanitizer + UBSan" OFF)
if(BFME_TESTS_ASAN)
    target_compile_options(bfme_texdecode_tests PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(bfme_texdecode_tests PRIVATE -fsanitize=address,undefined)
endif()

set(BFME_TEXDECODE_TESTS
    sniff_magic_bytes
    info_reads_dimensions
    png_rgba_alone_keeps_alpha_and_order
    png_rgb_alone_is_opaque
    jpg_alone_is_opaque_and_top_down
    jpg_takes_sibling_png_alpha
    jpg_takes_gray_png_luminance_as_alpha
    jpg_ignores_sibling_png_of_other_size
    jpg_ignores_unreadable_sibling
    png_input_ignores_sibling
    broken_input_fails_cleanly
)
foreach(t IN LISTS BFME_TEXDECODE_TESTS)
    add_test(NAME texdecode.${t} COMMAND bfme_texdecode_tests ${t})
    set_tests_properties(texdecode.${t} PROPERTIES SKIP_RETURN_CODE 77 LABELS "unit")
endforeach()
add_test(NAME texdecode.registry_complete COMMAND ${CMAKE_COMMAND}
    -DBIN=$<TARGET_FILE:bfme_texdecode_tests> "-DEXPECTED=${BFME_TEXDECODE_TESTS}"
    -P ${BFME_REPO_ROOT}/tests/heights/cmake/check_registry.cmake)
set_tests_properties(texdecode.registry_complete PROPERTIES LABELS "unit")
