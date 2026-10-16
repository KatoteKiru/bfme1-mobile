# BFME1 Mobile — SidesList v6 parser tests
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Real SidesList::ParseSidesDataChunk. Synthetic chunks need no game data.
# BFME_GAMEDATA (maps.big and _patch222maps.big) is optional; those tests exit 77.
#
#   cmake -S tests/sides -B build/tests-sides -G Ninja && cmake --build build/tests-sides -j2
#   ctest --test-dir build/tests-sides -L unit
#   BFME_GAMEDATA=/path/to/bfme1 ctest --test-dir build/tests-sides -L gamedata
cmake_minimum_required(VERSION 3.25)
project(bfme_sides_tests C CXX)

include(CTest)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

include(${CMAKE_CURRENT_SOURCE_DIR}/../heights/cmake/engine.cmake)

set(R ${BFME_REPO_ROOT})
add_library(bfme_sides_engine STATIC
    ${R}/GeneralsMD/Code/GameEngine/Source/GameLogic/Map/SidesList.cpp
    ${R}/GeneralsMD/Code/GameEngine/Source/Common/System/DataChunk.cpp
    ${R}/GeneralsMD/Code/GameEngine/Source/Common/Dict.cpp
    ${R}/GeneralsMD/Code/GameEngine/Source/Common/NameKeyGenerator.cpp
    ${R}/Core/GameEngine/Source/Common/System/AsciiString.cpp
    ${R}/Core/GameEngine/Source/Common/System/UnicodeString.cpp
    ${R}/Core/GameEngine/Source/Common/System/GameMemoryNull.cpp
    ${R}/Core/Libraries/Source/Compression/EAC/refdecode.cpp
    ${BFME_E125_TESTS_DIR}/src/MapLoader.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/SidesStubs.cpp
)
target_compile_definitions(bfme_sides_engine PUBLIC
    LINUX _UNIX POSIX RTS_ZEROHOUR=1 RTS_RELEASE NDEBUG
    DISABLE_GAMEMEMORY=1 DISABLE_MEMORYPOOL_CHECKPOINTING=1 DISABLE_MEMORYPOOL_STACKTRACE=1
    HAVE_STRLCPY HAVE_STRLCAT HAVE_WCSLCPY HAVE_WCSLCAT SAGE_USE_SDL3
    RELEASE_DEBUG_LOGGING=1
)
target_include_directories(bfme_sides_engine PUBLIC
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
    ${BFME_E125_TESTS_DIR}/src
)
target_compile_options(bfme_sides_engine PUBLIC
    -include Utility/CppMacros.h -Wno-deprecated-declarations
    -ffp-contract=off -fno-strict-aliasing -fwrapv -fno-delete-null-pointer-checks
    -ffunction-sections -fdata-sections)
target_link_options(bfme_sides_engine INTERFACE -Wl,--gc-sections)

add_executable(bfme_sides_tests
    ${BFME_E125_TESTS_DIR}/harness/TestMain.cpp
    src/TestSides.cpp
)
target_include_directories(bfme_sides_tests PRIVATE ${BFME_E125_TESTS_DIR}/harness)
target_link_libraries(bfme_sides_tests PRIVATE bfme_sides_engine)

set(BFME_SIDES_UNIT_TESTS
    sides_v6_leading_byte_ten_sides
    sides_v6_build_list_v3
    sides_v6_display_name_stays_off_player_name
    sides_v6_counts_must_fit_chunk
    sides_v6_unicode_player_name
    sides_v6_player_name_ignores_case
    sides_v6_sides_past_cap_consumed
    sides_v3_zh_teams_and_scripts
    sides_v1_zh_no_nested_teams
    sides_v5_zh_layout
    sides_version_above_6_rejected
)
set(BFME_SIDES_GAMEDATA_TESTS
    gamedata_fords_of_isen_ten_sides
    gamedata_sideslist_all_maps
)
foreach(t IN LISTS BFME_SIDES_UNIT_TESTS)
    add_test(NAME sides.${t} COMMAND bfme_sides_tests ${t})
    set_tests_properties(sides.${t} PROPERTIES SKIP_RETURN_CODE 77 LABELS "unit")
endforeach()
foreach(t IN LISTS BFME_SIDES_GAMEDATA_TESTS)
    add_test(NAME sides.${t} COMMAND bfme_sides_tests ${t})
    set_tests_properties(sides.${t} PROPERTIES SKIP_RETURN_CODE 77 LABELS "gamedata" TIMEOUT 120)
endforeach()
add_test(NAME sides.registry_complete COMMAND ${CMAKE_COMMAND}
    -DBIN=$<TARGET_FILE:bfme_sides_tests>
    "-DEXPECTED=${BFME_SIDES_UNIT_TESTS};${BFME_SIDES_GAMEDATA_TESTS}"
    -P ${BFME_E125_TESTS_DIR}/cmake/check_registry.cmake)
set_tests_properties(sides.registry_complete PROPERTIES LABELS "unit")
