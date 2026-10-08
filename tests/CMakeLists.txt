# BFME1 Mobile — unit and integration tests (E1-21: VFS BIG4/BIGF + mount order)
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Standalone project: no vcpkg, no DXVK build, no game link. The test binary compiles the REAL
# upstream VFS translation units (StdBIGFileSystem, ArchiveFileSystem, StdLocalFileSystem, RAMFile,
# AsciiString, CompressionManager, ...) with the Linux desktop defines plus formats/vfs, and stubs
# the few engine globals they reference (tests/vfs/EngineStubs.cpp).
#
#   cmake -S tests -B build/tests -G Ninja && cmake --build build/tests && ctest --test-dir build/tests
#   BFME_GAMEDATA=/path/to/bfme1 BFME_ORACLE_DIR=/path/to/tools/oracle/out ctest --test-dir build/tests
#
# Needs only the DXVK native Windows-compat headers (submodule references/fbraz3-dxvk, used by every
# Linux/Android build anyway): git submodule update --init references/fbraz3-dxvk
cmake_minimum_required(VERSION 3.25)
if(POLICY CMP0169)
    cmake_policy(SET CMP0169 OLD)  # cmake/lzhl.cmake and cmake/zlib.cmake use FetchContent_Populate(<args>)
endif()
project(bfme_tests C CXX)

include(FetchContent)
include(CTest)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

get_filename_component(BFME_REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
set(BFME_DXVK_INCLUDE_DIR "${BFME_REPO_ROOT}/references/fbraz3-dxvk/include" CACHE PATH
    "DXVK include dir with native/windows (submodule references/fbraz3-dxvk)")
if(NOT EXISTS "${BFME_DXVK_INCLUDE_DIR}/native/windows/windows_base.h")
    message(FATAL_ERROR "DXVK native headers not found in ${BFME_DXVK_INCLUDE_DIR}. "
        "Run: git submodule update --init references/fbraz3-dxvk (or pass -DBFME_DXVK_INCLUDE_DIR=...)")
endif()

# Compression deps exactly as the engine gets them (core_compression).
include(${BFME_REPO_ROOT}/cmake/lzhl.cmake)
find_package(ZLIB)
if(ZLIB_FOUND)
    set(BFME_TEST_ZLIB ZLIB::ZLIB)
else()
    include(${BFME_REPO_ROOT}/cmake/zlib.cmake)
    set(BFME_TEST_ZLIB libzlib)
endif()

set(R ${BFME_REPO_ROOT})

# formats/vfs: engine-independent part (also usable by tools/datacheck).
add_library(bfme_formats_vfs_core STATIC
    ${R}/formats/vfs/src/BigArchiveReader.cpp
    ${R}/formats/vfs/src/MountOrder.cpp
)
target_include_directories(bfme_formats_vfs_core PUBLIC ${R}/formats/vfs/include)
target_compile_features(bfme_formats_vfs_core PUBLIC cxx_std_17)

# Upstream engine sources under test + the formats/vfs engine adapter.
set(BFME_ENGINE_VFS_SOURCES
    ${R}/Core/GameEngineDevice/Source/StdDevice/Common/StdBIGFileSystem.cpp
    ${R}/Core/GameEngineDevice/Source/StdDevice/Common/StdBIGFile.cpp
    ${R}/Core/GameEngineDevice/Source/StdDevice/Common/StdLocalFileSystem.cpp
    ${R}/Core/GameEngineDevice/Source/StdDevice/Common/StdLocalFile.cpp
    ${R}/Core/GameEngine/Source/Common/System/ArchiveFileSystem.cpp
    ${R}/Core/GameEngine/Source/Common/System/ArchiveFile.cpp
    ${R}/Core/GameEngine/Source/Common/System/LocalFileSystem.cpp
    ${R}/Core/GameEngine/Source/Common/System/LocalFile.cpp
    ${R}/Core/GameEngine/Source/Common/System/RAMFile.cpp
    ${R}/Core/GameEngine/Source/Common/System/StreamingArchiveFile.cpp
    ${R}/Core/GameEngine/Source/Common/System/File.cpp
    ${R}/Core/GameEngine/Source/Common/System/FileSystem.cpp
    ${R}/Core/GameEngine/Source/Common/System/AsciiString.cpp
    ${R}/Core/GameEngine/Source/Common/System/UnicodeString.cpp
    ${R}/Core/GameEngine/Source/Common/System/SubsystemInterface.cpp
    ${R}/Core/GameEngine/Source/Common/System/GameMemoryNull.cpp
    ${R}/Core/Libraries/Source/Compression/CompressionManager.cpp
    ${R}/Core/Libraries/Source/Compression/EAC/btreeabout.cpp
    ${R}/Core/Libraries/Source/Compression/EAC/btreedecode.cpp
    ${R}/Core/Libraries/Source/Compression/EAC/btreeencode.cpp
    ${R}/Core/Libraries/Source/Compression/EAC/huffabout.cpp
    ${R}/Core/Libraries/Source/Compression/EAC/huffdecode.cpp
    ${R}/Core/Libraries/Source/Compression/EAC/huffencode.cpp
    ${R}/Core/Libraries/Source/Compression/EAC/refabout.cpp
    ${R}/Core/Libraries/Source/Compression/EAC/refdecode.cpp
    ${R}/Core/Libraries/Source/Compression/EAC/refencode.cpp
    ${R}/Core/Libraries/Source/Compression/LZHCompress/NoxCompress.cpp
    ${R}/formats/vfs/src/BfmeBIGFileSystem.cpp
)

add_executable(bfme_vfs_tests
    ${BFME_ENGINE_VFS_SOURCES}
    vfs/TestMain.cpp
    vfs/EngineStubs.cpp
    vfs/EngineFixture.cpp
    vfs/TestSynthetic.cpp
    vfs/TestGamedata.cpp
)
target_link_libraries(bfme_vfs_tests PRIVATE bfme_formats_vfs_core liblzhl ${BFME_TEST_ZLIB})
# Same game/platform defines as the linux64-deploy and android-vulkan presets (ZH build, null
# GameMemory, release asserts); see build/*/compile_commands.json.
target_compile_definitions(bfme_vfs_tests PRIVATE
    LINUX _UNIX POSIX RTS_ZEROHOUR=1 RTS_RELEASE NDEBUG
    DISABLE_GAMEMEMORY=1 DISABLE_MEMORYPOOL_CHECKPOINTING=1 DISABLE_MEMORYPOOL_STACKTRACE=1
    HAVE_STRLCPY HAVE_STRLCAT HAVE_WCSLCPY HAVE_WCSLCAT SAGE_USE_SDL3  # as cmake/config-build.cmake (UNIX)
    BFME_SOURCE_DIR="${BFME_REPO_ROOT}"
)
target_include_directories(bfme_vfs_tests PRIVATE
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
    ${R}/Core/Libraries/Source/Compression
    ${R}/Core/Main
    ${R}/Dependencies/Utility
    ${BFME_DXVK_INCLUDE_DIR}/native
    ${BFME_DXVK_INCLUDE_DIR}/native/windows
    ${BFME_DXVK_INCLUDE_DIR}/native/directx
)
# The engine's precompiled header starts with Utility/CppMacros.h (CPP_11 macro etc.).
target_compile_options(bfme_vfs_tests PRIVATE -include Utility/CppMacros.h -Wno-deprecated-declarations)

# One ctest per test case; exit code 77 = skipped (integration tests without BFME_GAMEDATA).
set(BFME_VFS_TESTS
    big4_header_endianness big_reader_rejects_bad_input engine_opens_big4_and_bigf
    engine_rejects_unknown_signature engine_reads_big4_entries mount_rules_cfg_equals_builtin
    mount_rules_parse_errors mount_classify_bfme1_names mount_plan_bfme1_order
    engine_layer_priority_base_patch_mobile engine_layer_priority_beats_names
    engine_intra_layer_first_by_stricmp_wins engine_layer_patch222_over_base engine_mobile_layer_case_and_slashes engine_mobile_layer_outside_root engine_skips_broken_archive
    bfme_gamedata_requests_bfme_filesystem
    bfme_skips_zh_community_patch bfme_init_exits_when_rules_do_not_parse
    bfme_init_exits_when_no_archive_mounts
    refpack_roundtrip_compression_manager
    gamedata_ini_big_matches_oracle gamedata_maps_big_matches_oracle gamedata_m1_map_refpack
    gamedata_all_maps_refpack gamedata_full_mount_follows_plan gamedata_patch222_over_base
)
foreach(t IN LISTS BFME_VFS_TESTS)
    add_test(NAME vfs.${t} COMMAND bfme_vfs_tests ${t})
    set_tests_properties(vfs.${t} PROPERTIES SKIP_RETURN_CODE 77)
    if(t MATCHES "^gamedata_")
        set_tests_properties(vfs.${t} PROPERTIES LABELS "gamedata")
    else()
        set_tests_properties(vfs.${t} PROPERTIES LABELS "unit")
    endif()
endforeach()
# Guard: every TEST() in the binary is registered above.
add_test(NAME vfs.registry_complete COMMAND ${CMAKE_COMMAND}
    -DBIN=$<TARGET_FILE:bfme_vfs_tests> "-DEXPECTED=${BFME_VFS_TESTS}" -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/check_registry.cmake)
