# BFME1 Mobile — E1-48: software S3TC/BC1-BC3 decode of d3d8gles (bc_decode.h)
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Standalone: the real Core/Libraries/Source/d3d8gles/src/bc_decode.h (header only) on synthetic
# blocks. No D3D, GL or game data. It is the decode the Pixel 8 Pro uses for every DXT texture
# ("WEBGL_compressed_texture_s3tc missing; using software BC1-3 decode fallback").
#
#   cmake -S tests/bcdecode -B build/tests-bcdecode && cmake --build build/tests-bcdecode
#   ctest --test-dir build/tests-bcdecode --output-on-failure
cmake_minimum_required(VERSION 3.25)
project(bfme_bcdecode_tests CXX)

include(CTest)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

get_filename_component(BFME_REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)

add_executable(bfme_bcdecode_tests
    ${BFME_REPO_ROOT}/tests/heights/harness/TestMain.cpp
    src/TestBcDecode.cpp
)
target_include_directories(bfme_bcdecode_tests PRIVATE
    ${BFME_REPO_ROOT}/Core/Libraries/Source/d3d8gles/src
    ${BFME_REPO_ROOT}/tests/heights/harness
)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(bfme_bcdecode_tests PRIVATE -Wall -Wextra)
endif()
option(BFME_TESTS_ASAN "Build with AddressSanitizer + UBSan" OFF)
if(BFME_TESTS_ASAN)
    target_compile_options(bfme_bcdecode_tests PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(bfme_bcdecode_tests PRIVATE -fsanitize=address,undefined)
endif()

set(BFME_BCDECODE_TESTS
    rgb565_expands_to_full_range
    bc1_four_color_palette
    bc1_texel_order_is_row_major_low_bits_first
    bc1_three_color_mode_has_transparent_black
    bc1_equal_endpoints_solid_block_stays_opaque
    bc2_explicit_alpha_nibbles
    bc3_eight_alpha_mode
    bc3_six_alpha_mode_has_0_and_255
    bc3_color_block_never_punch_through
    level_clips_partial_blocks
    level_sub_4x4_mip_reads_one_block
    level_short_buffer_fails_cleanly
)
foreach(t IN LISTS BFME_BCDECODE_TESTS)
    add_test(NAME bcdecode.${t} COMMAND bfme_bcdecode_tests ${t})
    set_tests_properties(bcdecode.${t} PROPERTIES SKIP_RETURN_CODE 77 LABELS "unit")
endforeach()
add_test(NAME bcdecode.registry_complete COMMAND ${CMAKE_COMMAND}
    -DBIN=$<TARGET_FILE:bfme_bcdecode_tests> "-DEXPECTED=${BFME_BCDECODE_TESTS}"
    -P ${BFME_REPO_ROOT}/tests/heights/cmake/check_registry.cmake)
set_tests_properties(bcdecode.registry_complete PROPERTIES LABELS "unit")
