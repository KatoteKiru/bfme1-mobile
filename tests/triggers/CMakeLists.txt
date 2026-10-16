# BFME1 Mobile — PolygonTriggers v5 parser tests
# SPDX-License-Identifier: GPL-3.0-or-later
#
# The real PolygonTrigger.cpp parser, on synthetic chunks and (when BFME_GAMEDATA is set)
# the maps inside maps.big and _patch222maps.big. Nothing is written into the game directory.
#
#   cmake -S tests/triggers -B build/tests-triggers -G Ninja && cmake --build build/tests-triggers -j2
#   ctest --test-dir build/tests-triggers -L unit
#   BFME_GAMEDATA=/path/to/bfme1 ctest --test-dir build/tests-triggers -L gamedata
cmake_minimum_required(VERSION 3.25)
project(bfme_triggers_tests C CXX)

include(CTest)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

include(${CMAKE_CURRENT_SOURCE_DIR}/../heights/cmake/engine.cmake)

set(R ${BFME_REPO_ROOT})
add_library(bfme_triggers_engine STATIC
    ${R}/GeneralsMD/Code/GameEngine/Source/GameLogic/Map/PolygonTrigger.cpp
    ${R}/GeneralsMD/Code/GameEngine/Source/Common/System/DataChunk.cpp
    ${R}/Core/GameEngine/Source/Common/System/AsciiString.cpp
    ${R}/Core/GameEngine/Source/Common/System/UnicodeString.cpp
    ${R}/Core/GameEngine/Source/Common/System/GameMemoryNull.cpp
    ${R}/Core/Libraries/Source/Compression/EAC/refdecode.cpp
    src/MapIo.cpp
    src/TriggerStubs.cpp
)
target_compile_definitions(bfme_triggers_engine PUBLIC
    LINUX _UNIX POSIX RTS_ZEROHOUR=1 RTS_RELEASE NDEBUG
    DISABLE_GAMEMEMORY=1 DISABLE_MEMORYPOOL_CHECKPOINTING=1 DISABLE_MEMORYPOOL_STACKTRACE=1
    HAVE_STRLCPY HAVE_STRLCAT HAVE_WCSLCPY HAVE_WCSLCAT SAGE_USE_SDL3
    RELEASE_DEBUG_LOGGING=1
)
target_include_directories(bfme_triggers_engine PUBLIC
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
target_compile_options(bfme_triggers_engine PUBLIC
    -include Utility/CppMacros.h -Wno-deprecated-declarations
    -ffp-contract=off -fno-strict-aliasing -fwrapv -fno-delete-null-pointer-checks
    -ffunction-sections -fdata-sections)
target_link_options(bfme_triggers_engine INTERFACE -Wl,--gc-sections)
if(BFME_TESTS_ASAN)
    target_sources(bfme_triggers_engine INTERFACE ${BFME_E125_TESTS_DIR}/src/AsanSizedDelete.cpp)
endif()

add_executable(bfme_triggers_tests
    ${BFME_E125_TESTS_DIR}/harness/TestMain.cpp
    src/TestPolygonTriggers.cpp
)
target_link_libraries(bfme_triggers_tests PRIVATE bfme_triggers_engine)

set(BFME_TRIGGERS_TESTS
    triggers_v5_reads_water_fields
    triggers_old_versions_match_zh
    triggers_numpoints_outside_chunk_rejected
    triggers_version_above_5_rejected
    triggers_v5_truncated_chunk_rejected
    gamedata_fords_of_isen
    gamedata_all_maps_remainder_zero
)
foreach(t IN LISTS BFME_TRIGGERS_TESTS)
    add_test(NAME triggers.${t} COMMAND bfme_triggers_tests ${t})
    set_tests_properties(triggers.${t} PROPERTIES SKIP_RETURN_CODE 77)
    if(t MATCHES "^gamedata_")
        set_tests_properties(triggers.${t} PROPERTIES LABELS "gamedata")
    else()
        set_tests_properties(triggers.${t} PROPERTIES LABELS "unit")
    endif()
endforeach()
add_test(NAME triggers.registry_complete COMMAND ${CMAKE_COMMAND}
    -DBIN=$<TARGET_FILE:bfme_triggers_tests> "-DEXPECTED=${BFME_TRIGGERS_TESTS}"
    -P ${BFME_E125_TESTS_DIR}/cmake/check_registry.cmake)
set_tests_properties(triggers.registry_complete PROPERTIES LABELS "unit")
