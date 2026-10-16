# BFME1 Mobile — DataChunk skip log and map-chunk version ceilings (audit A5)
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Synthetic chunks only. No game data.
#
#   cmake -S tests/chunk -B build/tests-chunk -G "Unix Makefiles" -DCMAKE_CXX_COMPILER=g++
#   cmake --build build/tests-chunk -j2
#   ctest --test-dir build/tests-chunk -L unit --output-on-failure
#
# Clang 18 rejects the strlcpy redeclaration in Generals socket_compat.h
# (missing exception specification). g++ 13 accepts it.
cmake_minimum_required(VERSION 3.25)
project(bfme_chunk_tests C CXX)

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

set(R ${BFME_REPO_ROOT})
set(BFME_CHUNK_INCLUDES
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
set(BFME_CHUNK_DEFS
    LINUX _UNIX POSIX RTS_ZEROHOUR=1 RTS_RELEASE NDEBUG
    DISABLE_GAMEMEMORY=1 DISABLE_MEMORYPOOL_CHECKPOINTING=1 DISABLE_MEMORYPOOL_STACKTRACE=1
    HAVE_STRLCPY HAVE_STRLCAT HAVE_WCSLCPY HAVE_WCSLCAT SAGE_USE_SDL3
)
set(BFME_CHUNK_OPTS
    -include Utility/CppMacros.h -Wno-deprecated-declarations
    -ffunction-sections -fdata-sections
)

# DataChunk.cpp is the module under test. The map-load call sites are one extra
# argument on registerParser; WorldHeightMap.cpp needs d3d8.h, which this tree's
# DXVK checkout does not ship, so they are not compiled here.
add_library(bfme_chunk_engine STATIC
    ${R}/GeneralsMD/Code/GameEngine/Source/Common/System/DataChunk.cpp
    ${R}/Core/GameEngine/Source/Common/System/AsciiString.cpp
    ${R}/Core/GameEngine/Source/Common/System/GameMemoryNull.cpp
)
target_compile_definitions(bfme_chunk_engine PUBLIC ${BFME_CHUNK_DEFS})
target_include_directories(bfme_chunk_engine PUBLIC ${BFME_CHUNK_INCLUDES})
target_compile_options(bfme_chunk_engine PUBLIC ${BFME_CHUNK_OPTS})
target_link_options(bfme_chunk_engine INTERFACE -Wl,--gc-sections)

add_executable(bfme_chunk_tests
    ${R}/tests/heights/harness/TestMain.cpp
    src/TestDataChunk.cpp
)
target_link_libraries(bfme_chunk_tests PRIVATE bfme_chunk_engine)

set(BFME_CHUNK_TESTS
    chunk_skip_logs_each_chunk
    chunk_skip_count_is_per_map
    chunk_skip_count_resets
    chunk_known_skip_is_one_summary
    chunk_version_at_ceiling_is_parsed
    chunk_older_version_is_parsed
    chunk_newer_version_errors
    chunk_ceiling_does_not_count_as_skip
    chunk_scan_maxima_reach_the_parser
    chunk_no_ceiling_parses_any_version
    chunk_nested_unknown_shares_map_count
    chunk_wrong_parent_is_skipped
    chunk_ceiling_is_only_newer_than_known
    chunk_ceiling_table
)
foreach(t IN LISTS BFME_CHUNK_TESTS)
    add_test(NAME chunk.${t} COMMAND bfme_chunk_tests ${t})
    set_tests_properties(chunk.${t} PROPERTIES LABELS "unit")
endforeach()
add_test(NAME chunk.registry_complete COMMAND ${CMAKE_COMMAND}
    -DBIN=$<TARGET_FILE:bfme_chunk_tests> "-DEXPECTED=${BFME_CHUNK_TESTS}"
    -P ${R}/tests/cmake/check_registry.cmake)
set_tests_properties(chunk.registry_complete PROPERTIES LABELS "unit")
