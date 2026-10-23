# BFME1 Mobile — prerequisite cycles in ScienceInfo::addRootSciences
# SPDX-License-Identifier: GPL-3.0-or-later
#
# The real Science.cpp walk. Synthetic sciences only. No game data.
#
#   cmake -S tests/science -B build/tests-science -G "Unix Makefiles" -DCMAKE_CXX_COMPILER=g++
#   cmake --build build/tests-science -j2
#   ctest --test-dir build/tests-science -L unit --output-on-failure
#
# Clang 18 rejects the strlcpy redeclaration in Generals socket_compat.h
# (missing exception specification). g++ 13 accepts it.
cmake_minimum_required(VERSION 3.25)
project(bfme_science_tests CXX)

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
add_library(bfme_science_engine STATIC
    ${R}/GeneralsMD/Code/GameEngine/Source/Common/RTS/Science.cpp
    ${R}/GeneralsMD/Code/GameEngine/Source/Common/NameKeyGenerator.cpp
    ${R}/Core/GameEngine/Source/Common/System/AsciiString.cpp
    ${R}/Core/GameEngine/Source/Common/System/UnicodeString.cpp
    ${R}/Core/GameEngine/Source/Common/System/GameMemoryNull.cpp
)
target_compile_definitions(bfme_science_engine PUBLIC
    LINUX _UNIX POSIX RTS_ZEROHOUR=1 RTS_RELEASE NDEBUG
    DISABLE_GAMEMEMORY=1 DISABLE_MEMORYPOOL_CHECKPOINTING=1 DISABLE_MEMORYPOOL_STACKTRACE=1
    HAVE_STRLCPY HAVE_STRLCAT HAVE_WCSLCPY HAVE_WCSLCAT SAGE_USE_SDL3
)
target_include_directories(bfme_science_engine PUBLIC
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
target_compile_options(bfme_science_engine PUBLIC
    -include Utility/CppMacros.h -Wno-deprecated-declarations
    -ffunction-sections -fdata-sections
)
target_link_options(bfme_science_engine INTERFACE -Wl,--gc-sections)

add_executable(bfme_science_tests
    ${R}/tests/heights/harness/TestMain.cpp
    src/TestScienceCycles.cpp
)
target_link_libraries(bfme_science_tests PRIVATE bfme_science_engine)

set(BFME_SCIENCE_TESTS
    science_cycle_a_b_a
    science_cycle_self
    science_diamond_no_cycle
)
foreach(t IN LISTS BFME_SCIENCE_TESTS)
    add_test(NAME science.${t} COMMAND bfme_science_tests ${t})
    set_tests_properties(science.${t} PROPERTIES LABELS "unit")
endforeach()
add_test(NAME science.registry_complete COMMAND ${CMAKE_COMMAND}
    -DBIN=$<TARGET_FILE:bfme_science_tests> "-DEXPECTED=${BFME_SCIENCE_TESTS}"
    -P ${R}/tests/cmake/check_registry.cmake)
set_tests_properties(science.registry_complete PROPERTIES LABELS "unit")
