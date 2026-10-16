# BFME1 Mobile — E1-51 terrain atlas tests (synthetic; no game data)
# SPDX-License-Identifier: GPL-3.0-or-later
#
#   cmake -S tests/atlas -B build/tests-atlas -G Ninja -DBFME_TESTS_ASAN=ON && cmake --build build/tests-atlas -j2
#   ctest --test-dir build/tests-atlas -L unit --output-on-failure
cmake_minimum_required(VERSION 3.25)
project(bfme_atlas_tests C CXX)

include(CTest)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

include(${CMAKE_CURRENT_SOURCE_DIR}/../heights/cmake/engine.cmake)

bfme_heights_engine(bfme_atlas_engine ${BFME_REPO_ROOT}
    ${CMAKE_CURRENT_SOURCE_DIR}/../blend/src/BlendStubs.cpp)
target_compile_definitions(bfme_atlas_engine PUBLIC RELEASE_DEBUG_LOGGING=1)

add_executable(bfme_atlas_tests
    ${BFME_E125_TESTS_DIR}/harness/TestMain.cpp
    src/TestAtlas.cpp)
target_link_libraries(bfme_atlas_tests PRIVATE bfme_atlas_engine)

set(BFME_ATLAS_TESTS
    atlas_fords_one_page_4096x2048
    atlas_fords_single_2048_drops_17
    atlas_eastfold_four_pages
    atlas_trilogy_five_pages
    atlas_overflow_log_first_16
    atlas_map_blit_no_oob
    atlas_tile_index_bounds
    atlas_cliff_uv_uses_texture_width
)
foreach(t IN LISTS BFME_ATLAS_TESTS)
    add_test(NAME atlas.${t} COMMAND bfme_atlas_tests ${t})
    set_tests_properties(atlas.${t} PROPERTIES SKIP_RETURN_CODE 77 LABELS "unit")
endforeach()
add_test(NAME atlas.registry_complete COMMAND ${CMAKE_COMMAND}
    -DBIN=$<TARGET_FILE:bfme_atlas_tests> "-DEXPECTED=${BFME_ATLAS_TESTS}"
    -P ${BFME_E125_TESTS_DIR}/cmake/check_registry.cmake)
set_tests_properties(atlas.registry_complete PROPERTIES LABELS "unit")
