# BFME1 Mobile — E1-25 tests: 16-bit terrain heights (WorldHeightMap u16, MAP_HEIGHT_SCALE = 10/256)
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Standalone project (no vcpkg, no DXVK build, no game link); see README.md.
#
#   cmake -S tests/heights -B build/tests-heights -G Ninja && cmake --build build/tests-heights -j2
#   ctest --test-dir build/tests-heights -L unit
#   BFME_GAMEDATA=... BFME_ORACLE_DIR=... [BFME_ORACLE_DUMP_DIR=...] ctest --test-dir build/tests-heights -L gamedata
#
# The REAL upstream sources under test (WorldHeightMap.cpp, DataChunk.cpp, ...) are compiled with the
# Linux desktop defines; the linker drops everything unreachable (--gc-sections), and
# src/EngineStubs.cpp provides the few globals that remain.
#
# The before/after check builds src/ZhDump.cpp a second time against the upstream tree
# (BFME_HEIGHTS_BASELINE_REV, extracted with `git archive` at configure time, or an existing checkout
# in BFME_HEIGHTS_BASELINE_SOURCE_DIR) and requires byte-identical ZH world heights and cliff flags.
cmake_minimum_required(VERSION 3.25)
project(bfme_heights_tests C CXX)

include(CTest)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/engine.cmake)

bfme_heights_engine(bfme_heights_engine_new ${BFME_REPO_ROOT})

add_executable(bfme_heights_tests
    harness/TestMain.cpp
    src/TestUnit.cpp
    src/TestFormulas.cpp
    src/TestGamedata.cpp
)
target_link_libraries(bfme_heights_tests PRIVATE bfme_heights_engine_new)
target_compile_definitions(bfme_heights_tests PRIVATE BFME_ZH_MAPS_DIR="${BFME_ZH_MAPS_DIR}")

add_executable(heights_zh_dump_new src/ZhDump.cpp)
target_link_libraries(heights_zh_dump_new PRIVATE bfme_heights_engine_new)

# ---------------------------------------------------------------------------------------------------
set(BFME_HEIGHTS_TESTS
    constants_u16_scale zh_v1_v4_samples_widened_bit_identical_world zh_all_256_values
    bfme_v5_u16_samples_exact corrupt_height_chunk_rejected cliff_flags_threshold zh_repo_maps_widened
    formula_get_height_map_height_and_normal formula_raw_times_scale formula_uv_cliff_limits
    formula_flatten_and_crater formula_flatten_rounding_boundaries formula_crater_integer_displacements
    formula_seismic_and_save_layout formula_cast_ray_boxes
    gamedata_bfme1_maps_minmax_vs_oracle gamedata_bfme1_maps_all_samples_vs_oracle_dump
)
foreach(t IN LISTS BFME_HEIGHTS_TESTS)
    add_test(NAME heights.${t} COMMAND bfme_heights_tests ${t})
    set_tests_properties(heights.${t} PROPERTIES SKIP_RETURN_CODE 77)
    if(t MATCHES "^gamedata_")
        set_tests_properties(heights.${t} PROPERTIES LABELS "gamedata")
    else()
        set_tests_properties(heights.${t} PROPERTIES LABELS "unit")
    endif()
endforeach()
add_test(NAME heights.registry_complete COMMAND ${CMAKE_COMMAND}
    -DBIN=$<TARGET_FILE:bfme_heights_tests> "-DEXPECTED=${BFME_HEIGHTS_TESTS}"
    -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/check_registry.cmake)
set_tests_properties(heights.registry_complete PROPERTIES LABELS "unit")

# ---------------------------------------------------------------------------------------------------
# Before/after: upstream (u8) vs this tree (u16), bit-identical ZH world heights and cliff flags.
bfme_e125_baseline_root(baseline_root)
if(baseline_root)
    bfme_heights_engine(bfme_heights_engine_old ${baseline_root})
    add_executable(heights_zh_dump_old src/ZhDump.cpp)
    target_link_libraries(heights_zh_dump_old PRIVATE bfme_heights_engine_old)
    add_test(NAME heights.zh_baseline_bit_identical COMMAND ${CMAKE_COMMAND}
        -DOLD=$<TARGET_FILE:heights_zh_dump_old> -DNEW=$<TARGET_FILE:heights_zh_dump_new>
        "-DMAPS_DIR=${BFME_ZH_MAPS_DIR}" -DWORK=${CMAKE_CURRENT_BINARY_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/compare_dumps.cmake)
else()
    add_test(NAME heights.zh_baseline_bit_identical COMMAND ${CMAKE_COMMAND} -E echo "SKIP: no baseline tree")
    set_tests_properties(heights.zh_baseline_bit_identical PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:")
endif()
set_tests_properties(heights.zh_baseline_bit_identical PROPERTIES LABELS "unit")
