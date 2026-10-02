# BFME1 Mobile — E1-25 tests: shared CMake for tests/heights and tests/blend
# SPDX-License-Identifier: GPL-3.0-or-later
#
# include()d by tests/heights/CMakeLists.txt and tests/blend/CMakeLists.txt. Provides:
#   BFME_REPO_ROOT, BFME_ZH_MAPS_DIR, BFME_E125_TESTS_DIR (= tests/heights)
#   bfme_heights_engine(<target> <source root> [extra sources...])  real engine sources under test
#   bfme_e125_baseline_root(<out var>)  upstream tree for before/after checks ("" if unavailable)
#   option BFME_TESTS_ASAN  build everything with AddressSanitizer
set(BFME_E125_TESTS_DIR "${CMAKE_CURRENT_LIST_DIR}/..")
get_filename_component(BFME_E125_TESTS_DIR "${BFME_E125_TESTS_DIR}" ABSOLUTE)

option(BFME_TESTS_ASAN "Build the tests and the engine sources with -fsanitize=address" OFF)
if(BFME_TESTS_ASAN)
    add_compile_options(-fsanitize=address -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address)
endif()

get_filename_component(BFME_REPO_ROOT "${BFME_E125_TESTS_DIR}/../.." ABSOLUTE)
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
    # ARGN: extra sources (e.g. stubs) compiled against the same tree
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
        ${BFME_E125_TESTS_DIR}/src/MapLoader.cpp
        ${BFME_E125_TESTS_DIR}/src/EngineStubs.cpp
        ${ARGN}
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
    if(BFME_TESTS_ASAN)
        # compiled into every executable that links the engine (see the file for why)
        target_sources(${target} INTERFACE ${BFME_E125_TESTS_DIR}/src/AsanSizedDelete.cpp)
    endif()
endfunction()

# ---------------------------------------------------------------------------------------------------
# Upstream tree for the before/after comparisons: BFME_HEIGHTS_BASELINE_SOURCE_DIR, or extracted with
# `git archive` into the build dir at configure time.
function(bfme_e125_baseline_root outvar)
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
    if(baseline_root AND NOT EXISTS "${baseline_root}/Core/GameEngineDevice/Source/W3DDevice/GameClient/WorldHeightMap.cpp")
        set(baseline_root "")
    endif()
    set(${outvar} "${baseline_root}" PARENT_SCOPE)
endfunction()
