# BFME1 Mobile — E1-25 tests: BlendTileData v14/v15 (WorldHeightMap::ParseBlendTileData, heap tables)
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Standalone project like tests/heights (shares its harness, map loader and engine source list via
# ../heights/cmake/engine.cmake); see README.md.
#
#   cmake -S tests/blend -B build/tests-blend -G Ninja [-DBFME_TESTS_ASAN=ON] && cmake --build build/tests-blend -j2
#   ctest --test-dir build/tests-blend -L unit
#   BFME_GAMEDATA=... BFME_ORACLE_DIR=... BFME_ORACLE_DUMP_DIR=... BFME_DATACHECK_MAPS_JSON=... \
#     ctest --test-dir build/tests-blend -L gamedata
#
# The engine is built with RELEASE_DEBUG_LOGGING so that DEBUG_LOG (the engine log) reaches the stub
# DebugLog in src/BlendStubs.cpp and the tests can check the per-case lines and the per-map summary.
cmake_minimum_required(VERSION 3.25)
project(bfme_blend_tests C CXX)

include(CTest)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

include(${CMAKE_CURRENT_SOURCE_DIR}/../heights/cmake/engine.cmake)

function(bfme_blend_engine target root)
    bfme_heights_engine(${target} ${root}
        ${CMAKE_CURRENT_SOURCE_DIR}/src/BlendLoader.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/BlendStubs.cpp)
    target_compile_definitions(${target} PUBLIC RELEASE_DEBUG_LOGGING=1)
endfunction()

bfme_blend_engine(bfme_blend_engine_new ${BFME_REPO_ROOT})

add_executable(bfme_blend_tests
    ${BFME_E125_TESTS_DIR}/harness/TestMain.cpp
    src/TestBlendUnit.cpp
    src/TestBlendGamedata.cpp
)
target_link_libraries(bfme_blend_tests PRIVATE bfme_blend_engine_new)
target_compile_definitions(bfme_blend_tests PRIVATE BFME_ZH_MAPS_DIR="${BFME_ZH_MAPS_DIR}")

add_executable(blend_zh_dump_new src/BlendZhDump.cpp)
target_link_libraries(blend_zh_dump_new PRIVATE bfme_blend_engine_new)

# ---------------------------------------------------------------------------------------------------
set(BFME_BLEND_TESTS
    blend_v15_layout_roundtrip blend_versions_9_to_15_masks blend_v16_and_above_rejected
    blend_anomalies_clamped_masked_counted blend_anomaly_log_first_16_of_each_kind blend_limits_heap_tables blend_index_widths_and_sign
    blend_use3way_reset blend_zh_repo_maps
    gamedata_refpack_all_maps_vs_oracle gamedata_blend_all_maps_vs_oracle_and_datacheck
)
foreach(t IN LISTS BFME_BLEND_TESTS)
    add_test(NAME blend.${t} COMMAND bfme_blend_tests ${t})
    set_tests_properties(blend.${t} PROPERTIES SKIP_RETURN_CODE 77)
    if(t MATCHES "^gamedata_")
        set_tests_properties(blend.${t} PROPERTIES LABELS "gamedata")
    else()
        set_tests_properties(blend.${t} PROPERTIES LABELS "unit")
    endif()
endforeach()
add_test(NAME blend.registry_complete COMMAND ${CMAKE_COMMAND}
    -DBIN=$<TARGET_FILE:bfme_blend_tests> "-DEXPECTED=${BFME_BLEND_TESTS}"
    -P ${BFME_E125_TESTS_DIR}/cmake/check_registry.cmake)
set_tests_properties(blend.registry_complete PROPERTIES LABELS "unit")

# ---------------------------------------------------------------------------------------------------
# Review #10: the loading constructor WorldHeightMap(ChunkInputStream*, Bool) in the release configuration
# (no RELEASE_DEBUG_LOGGING: DEBUG_LOG is compiled out, the per-map summary goes to stderr) with counting
# operator new/delete (DISABLE_GAMEMEMORY_NEW_OPERATORS + src/CountingNew.cpp) for the leak and
# out-of-memory checks of its failure paths. Under -DBFME_TESTS_ASAN=ON LSan checks the same at exit.
bfme_heights_engine(bfme_blend_engine_release ${BFME_REPO_ROOT}
    ${CMAKE_CURRENT_SOURCE_DIR}/src/BlendStubs.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/CtorLoader.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/CtorStubs.cpp)
target_compile_definitions(bfme_blend_engine_release PUBLIC DISABLE_GAMEMEMORY_NEW_OPERATORS=1)

add_executable(bfme_blend_ctor_tests
    ${BFME_E125_TESTS_DIR}/harness/TestMain.cpp
    src/TestBlendCtor.cpp
    src/CountingNew.cpp
)
target_link_libraries(bfme_blend_ctor_tests PRIVATE bfme_blend_engine_release)
target_compile_definitions(bfme_blend_ctor_tests PRIVATE BFME_ZH_MAPS_DIR="${BFME_ZH_MAPS_DIR}")

set(BFME_BLEND_CTOR_TESTS
    ctor_corrupt_map_frees_arrays ctor_out_of_memory_at_every_allocation ctor_release_log_one_summary_line
    ctor_zh_repo_maps
)
foreach(t IN LISTS BFME_BLEND_CTOR_TESTS)
    add_test(NAME blend.${t} COMMAND bfme_blend_ctor_tests ${t})
    set_tests_properties(blend.${t} PROPERTIES SKIP_RETURN_CODE 77 LABELS "unit")
endforeach()
add_test(NAME blend.ctor_registry_complete COMMAND ${CMAKE_COMMAND}
    -DBIN=$<TARGET_FILE:bfme_blend_ctor_tests> "-DEXPECTED=${BFME_BLEND_CTOR_TESTS}"
    -P ${BFME_E125_TESTS_DIR}/cmake/check_registry.cmake)
set_tests_properties(blend.ctor_registry_complete PROPERTIES LABELS "unit")

# ---------------------------------------------------------------------------------------------------
# Before/after: upstream (Short indices, fixed arrays, v<=8) vs this tree on ZH BlendTileData.
bfme_e125_baseline_root(baseline_root)
if(baseline_root)
    bfme_blend_engine(bfme_blend_engine_old ${baseline_root})
    add_executable(blend_zh_dump_old src/BlendZhDump.cpp)
    target_link_libraries(blend_zh_dump_old PRIVATE bfme_blend_engine_old)
    add_test(NAME blend.zh_baseline_bit_identical COMMAND ${CMAKE_COMMAND}
        -DOLD=$<TARGET_FILE:blend_zh_dump_old> -DNEW=$<TARGET_FILE:blend_zh_dump_new>
        "-DMAPS_DIR=${BFME_ZH_MAPS_DIR}" -DWORK=${CMAKE_CURRENT_BINARY_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/compare_dumps.cmake)
else()
    add_test(NAME blend.zh_baseline_bit_identical COMMAND ${CMAKE_COMMAND} -E echo "SKIP: no baseline tree")
    set_tests_properties(blend.zh_baseline_bit_identical PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:")
endif()
set_tests_properties(blend.zh_baseline_bit_identical PROPERTIES LABELS "unit")
