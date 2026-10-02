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

get_filename_component(BFME_REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(BFME_DXVK_INCLUDE_DIR "${BFME_REPO_ROOT}/references/fbraz3-dxvk/include" CACHE PATH
    "DXVK include dir with native/windows (submodule references/fbraz3-dxvk)")
if(NOT EXISTS "${BFME_DXVK_INCLUDE_DIR}/native/windows/windows_base.h")
    message(FATAL_ERROR "DXVK native headers not found in ${BFME_DXVK_INCLUDE_DIR}. "
        "Run: git submodule update --init references/fbraz3-dxvk (or pass -DBFME_DXVK_INCLUDE_DIR=...)")
endif()

# Upstream engine revision for the before/after comparison: the last commit of the upstream engine
# (GeneralsX) the fork is based on. The engine sources are identical in katotekiru/bfme ebd6c90.
set(BFME_HEIGHTS_BASELINE_REV "8ef2349af61c5e67fd8de82ebd5c34e8df794173" CACHE STRING
    "git revision with the upstream u8 height code")
set(BFME_HEIGHTS_BASELINE_SOURCE_DIR "" CACHE PATH
    "checkout of BFME_HEIGHTS_BASELINE_REV (empty: extract it with git archive into the build dir)")
set(BFME_ZH_MAPS_DIR "${BFME_REPO_ROOT}/GeneralsOnlineRelease/Users/AntiKeks/Documents/Command and Conquer Generals Zero Hour Data/Maps"
    CACHE PATH "ZH .map files (already in the repository) for the unit and before/after tests")
if(NOT IS_DIRECTORY "${BFME_ZH_MAPS_DIR}")
    set(BFME_ZH_MAPS_DIR "")
endif()

# ---------------------------------------------------------------------------------------------------
# Engine sources under test, compiled against a given source tree (this one or the baseline).
function(bfme_heights_engine target root)
    set(R ${root})
    add_library(${target} STATIC
        ${R}/Core/GameEngineDevice/Source/W3DDevice/GameClient/WorldHeightMap.cpp
        ${R}/Core/GameEngineDevice/Source/W3DDevice/GameClient/TileData.cpp
        ${R}/GeneralsMD/Code/GameEngine/Source/Common/System/DataChunk.cpp
        ${R}/GeneralsMD/Code/GameEngine/Source/Common/Dict.cpp
        ${R}/Core/GameEngine/Source/Common/System/AsciiString.cpp
        ${R}/Core/GameEngine/Source/Common/System/UnicodeString.cpp
        ${R}/Core/GameEngine/Source/Common/System/GameMemoryNull.cpp
        ${R}/Core/Libraries/Source/Compression/EAC/refdecode.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/MapLoader.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/EngineStubs.cpp
    )
    # Same game/platform defines as the linux64-deploy and android-vulkan presets (ZH build, null
    # GameMemory, release asserts), and the float/aliasing semantics of cmake/compilers.cmake.
    target_compile_definitions(${target} PUBLIC
        LINUX _UNIX POSIX RTS_ZEROHOUR=1 RTS_RELEASE NDEBUG
        DISABLE_GAMEMEMORY=1 DISABLE_MEMORYPOOL_CHECKPOINTING=1 DISABLE_MEMORYPOOL_STACKTRACE=1
        HAVE_STRLCPY HAVE_STRLCAT HAVE_WCSLCPY HAVE_WCSLCAT SAGE_USE_SDL3
    )
    target_include_directories(${target} PUBLIC
        ${R}/Generals/Code/CompatLib/Include
        ${R}/GeneralsMD/Code/CompatLib/Include
        ${R}/GeneralsMD/Code/GameEngine/Include
        ${R}/GeneralsMD/Code/GameEngine/Include/Precompiled
        ${R}/GeneralsMD/Code/GameEngineDevice/Include
        ${R}/Core/GameEngine/Include
        ${R}/Core/GameEngineDevice/Include
        ${R}/Core/Libraries/Include
        ${R}/Core/Libraries/Source/WWVegas
        ${R}/Core/Libraries/Source/WWVegas/WWLib
        ${R}/Core/Libraries/Source/WWVegas/WWDebug
        ${R}/Core/Libraries/Source/WWVegas/WWMath
        ${R}/Core/Libraries/Source/WWVegas/WWSaveLoad
        ${R}/Core/Libraries/Source/WWVegas/WW3D2
        ${R}/GeneralsMD/Code/Libraries/Source/WWVegas
        ${R}/GeneralsMD/Code/Libraries/Source/WWVegas/WW3D2
        ${R}/Core/Libraries/Source/Compression
        ${R}/Core/Libraries/Source/Compression/EAC
        ${R}/Dependencies/Utility
        ${BFME_DXVK_INCLUDE_DIR}/native
        ${BFME_DXVK_INCLUDE_DIR}/native/windows
        ${BFME_DXVK_INCLUDE_DIR}/native/directx
    )
    # The engine's precompiled header starts with Utility/CppMacros.h (CPP_11 macro etc.).
    target_compile_options(${target} PUBLIC
        -include Utility/CppMacros.h -Wno-deprecated-declarations
        -ffp-contract=off -fno-strict-aliasing -fwrapv -fno-delete-null-pointer-checks
        -ffunction-sections -fdata-sections)
    target_link_options(${target} INTERFACE -Wl,--gc-sections)
endfunction()

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
    formula_flatten_and_crater formula_seismic_and_save_layout formula_cast_ray_boxes
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
set(baseline_root "${BFME_HEIGHTS_BASELINE_SOURCE_DIR}")
if(NOT baseline_root)
    set(baseline_root "${CMAKE_CURRENT_BINARY_DIR}/baseline-src")
    set(stamp "${baseline_root}/.bfme-baseline-rev")
    set(have_rev "")
    if(EXISTS "${stamp}")
        file(READ "${stamp}" have_rev)
    endif()
    if(NOT have_rev STREQUAL BFME_HEIGHTS_BASELINE_REV)
        find_package(Git QUIET)
        set(rc 1)
        if(GIT_FOUND)
            file(REMOVE_RECURSE "${baseline_root}")
            file(MAKE_DIRECTORY "${baseline_root}")
            execute_process(
                COMMAND ${GIT_EXECUTABLE} -C ${BFME_REPO_ROOT} archive --format=tar
                    -o ${CMAKE_CURRENT_BINARY_DIR}/baseline-src.tar ${BFME_HEIGHTS_BASELINE_REV}
                    Core/GameEngine Core/GameEngineDevice Core/Libraries
                    GeneralsMD/Code/GameEngine GeneralsMD/Code/GameEngineDevice/Include GeneralsMD/Code/CompatLib
                    GeneralsMD/Code/Libraries Generals/Code/CompatLib Dependencies/Utility
                RESULT_VARIABLE rc ERROR_VARIABLE err)
            if(rc EQUAL 0)
                file(ARCHIVE_EXTRACT INPUT ${CMAKE_CURRENT_BINARY_DIR}/baseline-src.tar DESTINATION ${baseline_root})
                file(REMOVE ${CMAKE_CURRENT_BINARY_DIR}/baseline-src.tar)
                file(WRITE "${stamp}" "${BFME_HEIGHTS_BASELINE_REV}")
            endif()
        endif()
        if(NOT rc EQUAL 0)
            message(WARNING "E1-25 baseline ${BFME_HEIGHTS_BASELINE_REV} not available (git archive: ${err}); "
                "heights.zh_baseline_bit_identical will be skipped. Set BFME_HEIGHTS_BASELINE_SOURCE_DIR to a checkout.")
            set(baseline_root "")
        endif()
    endif()
endif()

if(baseline_root AND EXISTS "${baseline_root}/Core/GameEngineDevice/Source/W3DDevice/GameClient/WorldHeightMap.cpp")
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
