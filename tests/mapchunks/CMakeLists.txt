# BFME1 Mobile — E1-52 Teams, PlayerScriptsList, BuildLists, ScriptGroup v3
# SPDX-License-Identifier: GPL-3.0-or-later
#
#   cmake -S tests/mapchunks -B build/tests-mapchunks -G Ninja && cmake --build build/tests-mapchunks -j2
#   ctest --test-dir build/tests-mapchunks --output-on-failure
cmake_minimum_required(VERSION 3.25)
project(bfme_mapchunks_tests C CXX)

include(CTest)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

get_filename_component(BFME_REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(BFME_DXVK_INCLUDE_DIR "${BFME_REPO_ROOT}/references/fbraz3-dxvk/include" CACHE PATH
    "DXVK include dir with native/windows")
if(NOT EXISTS "${BFME_DXVK_INCLUDE_DIR}/native/windows/windows_base.h")
    message(FATAL_ERROR "DXVK native headers not found in ${BFME_DXVK_INCLUDE_DIR}")
endif()

set(BFME_E152_INCLUDES
    ${BFME_REPO_ROOT}/Generals/Code/CompatLib/Include
    ${BFME_REPO_ROOT}/GeneralsMD/Code/CompatLib/Include
    ${BFME_REPO_ROOT}/GeneralsMD/Code/GameEngine/Include
    ${BFME_REPO_ROOT}/GeneralsMD/Code/GameEngine/Include/Precompiled
    ${BFME_REPO_ROOT}/GeneralsMD/Code/GameEngineDevice/Include
    ${BFME_REPO_ROOT}/Core/GameEngine/Include
    ${BFME_REPO_ROOT}/Core/GameEngineDevice/Include
    ${BFME_REPO_ROOT}/Core/Libraries/Include
    ${BFME_REPO_ROOT}/Core/Libraries/Source/WWVegas
    ${BFME_REPO_ROOT}/Core/Libraries/Source/WWVegas/WWLib
    ${BFME_REPO_ROOT}/Core/Libraries/Source/WWVegas/WWDebug
    ${BFME_REPO_ROOT}/Core/Libraries/Source/WWVegas/WWMath
    ${BFME_REPO_ROOT}/Core/Libraries/Source/WWVegas/WWSaveLoad
    ${BFME_REPO_ROOT}/Core/Libraries/Source/WWVegas/WW3D2
    ${BFME_REPO_ROOT}/GeneralsMD/Code/Libraries/Source/WWVegas
    ${BFME_REPO_ROOT}/GeneralsMD/Code/Libraries/Source/WWVegas/WW3D2
    ${BFME_REPO_ROOT}/Core/Libraries/Source/Compression
    ${BFME_REPO_ROOT}/Core/Libraries/Source/Compression/EAC
    ${BFME_REPO_ROOT}/Dependencies/Utility
    ${BFME_DXVK_INCLUDE_DIR}/native
    ${BFME_DXVK_INCLUDE_DIR}/native/windows
    ${BFME_DXVK_INCLUDE_DIR}/native/directx
    ${BFME_REPO_ROOT}/tests/heights/harness
)

set(BFME_E152_DEFS
    LINUX _UNIX POSIX RTS_ZEROHOUR=1 RTS_RELEASE NDEBUG
    DISABLE_GAMEMEMORY=1 DISABLE_MEMORYPOOL_CHECKPOINTING=1 DISABLE_MEMORYPOOL_STACKTRACE=1
    HAVE_STRLCPY HAVE_STRLCAT HAVE_WCSLCPY HAVE_WCSLCAT SAGE_USE_SDL3
)

set(BFME_E152_OPTS
    -include Utility/CppMacros.h -Wno-deprecated-declarations -Wno-unused-parameter
    -ffp-contract=off -fno-strict-aliasing -fwrapv -fno-delete-null-pointer-checks
    -ffunction-sections -fdata-sections
)

# Compile the script TUs this change edits. They are not linked: the unit test stubs
# ScriptEngine. WorldHeightMap.cpp is not in this target; its include of d3d8.h comes
# from the DXVK/Windows SDK path used by the game and tests/blend, not from this tree.
add_library(bfme_e152_compile OBJECT
    ${BFME_REPO_ROOT}/GeneralsMD/Code/GameEngine/Source/GameLogic/ScriptEngine/ScriptEngine.cpp
    ${BFME_REPO_ROOT}/GeneralsMD/Code/GameEngine/Source/GameLogic/ScriptEngine/ScriptActions.cpp
    ${BFME_REPO_ROOT}/GeneralsMD/Code/GameEngine/Source/GameLogic/ScriptEngine/ScriptConditions.cpp
)
target_compile_definitions(bfme_e152_compile PRIVATE ${BFME_E152_DEFS})
target_include_directories(bfme_e152_compile PRIVATE ${BFME_E152_INCLUDES})
target_compile_options(bfme_e152_compile PRIVATE ${BFME_E152_OPTS})

add_library(bfme_mapchunks_engine STATIC
    ${BFME_REPO_ROOT}/GeneralsMD/Code/GameEngine/Source/Common/System/DataChunk.cpp
    ${BFME_REPO_ROOT}/GeneralsMD/Code/GameEngine/Source/Common/Dict.cpp
    ${BFME_REPO_ROOT}/GeneralsMD/Code/GameEngine/Source/Common/NameKeyGenerator.cpp
    ${BFME_REPO_ROOT}/Core/GameEngine/Source/Common/System/AsciiString.cpp
    ${BFME_REPO_ROOT}/Core/GameEngine/Source/Common/System/UnicodeString.cpp
    ${BFME_REPO_ROOT}/Core/GameEngine/Source/Common/System/GameMemoryNull.cpp
    ${BFME_REPO_ROOT}/GeneralsMD/Code/GameEngine/Source/GameLogic/ScriptEngine/Scripts.cpp
    ${BFME_REPO_ROOT}/GeneralsMD/Code/GameEngine/Source/GameLogic/Map/SidesList.cpp
    ${BFME_REPO_ROOT}/GeneralsMD/Code/GameEngine/Source/GameLogic/Map/BfmeMapChunks.cpp
    ${BFME_REPO_ROOT}/Core/Libraries/Source/Compression/EAC/refdecode.cpp
    src/ScriptEngineStub.cpp
)
target_compile_definitions(bfme_mapchunks_engine PUBLIC ${BFME_E152_DEFS})
target_include_directories(bfme_mapchunks_engine PUBLIC ${BFME_E152_INCLUDES})
target_compile_options(bfme_mapchunks_engine PUBLIC ${BFME_E152_OPTS})
target_link_options(bfme_mapchunks_engine INTERFACE -Wl,--gc-sections)

add_executable(bfme_mapchunks_tests
    ${BFME_REPO_ROOT}/tests/heights/harness/TestMain.cpp
    src/TestMapChunks.cpp
)
target_link_libraries(bfme_mapchunks_tests PRIVATE bfme_mapchunks_engine)
# Compile ScriptEngine/ScriptActions/ScriptConditions/WorldHeightMap. Do not link them:
# the unit test stubs ScriptEngine, and those TUs pull the rest of the game.
add_dependencies(bfme_mapchunks_tests bfme_e152_compile)

set(BFME_MAPCHUNK_TESTS
    teams_v1_and_empty
    teams_v2_rejected
    buildlists_v1_items
    buildlists_v2_rejected
    script_groups_v1_v2_v3
    script_group_v4_rejected
    script_group_depth_6_parses
    script_group_stack_limit
    script_lists_past_16
    unknown_kindof_keeps_int
    teams_owner_not_a_stored_side
    sides_v6_then_teams_drop
    sides_v5_ends_on_last_side
    sides_trace_on_short_build_count
    chunk_error_reports_name_and_offset
    bfme_only_action_and_condition
    newer_script_versions_rejected
    gamedata_chunk_remainder
)
foreach(t IN LISTS BFME_MAPCHUNK_TESTS)
    add_test(NAME mapchunks.${t} COMMAND bfme_mapchunks_tests ${t})
    set_tests_properties(mapchunks.${t} PROPERTIES SKIP_RETURN_CODE 77 LABELS "unit")
endforeach()
add_test(NAME mapchunks.registry_complete COMMAND ${CMAKE_COMMAND}
    -DBIN=$<TARGET_FILE:bfme_mapchunks_tests> "-DEXPECTED=${BFME_MAPCHUNK_TESTS}"
    -P ${BFME_REPO_ROOT}/tests/heights/cmake/check_registry.cmake)
set_tests_properties(mapchunks.registry_complete PROPERTIES LABELS "unit")
