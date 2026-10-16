# BFME1 Mobile — GlobalLighting v7 parser tests (ADR-0005 §4.1, §7 п.5)
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Standalone project like tests/blend (engine sources via ../heights/cmake/engine.cmake).
# Synthetic chunks only: no game data.
#
#   cmake -S tests/lighting -B build/tests-lighting -G Ninja && cmake --build build/tests-lighting -j2
#   ctest --test-dir build/tests-lighting -L unit
cmake_minimum_required(VERSION 3.25)
project(bfme_lighting_tests C CXX)

include(CTest)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

include(${CMAKE_CURRENT_SOURCE_DIR}/../heights/cmake/engine.cmake)

bfme_heights_engine(bfme_lighting_engine ${BFME_REPO_ROOT}
    ${CMAKE_CURRENT_SOURCE_DIR}/src/LightingStubs.cpp)
target_compile_definitions(bfme_lighting_engine PUBLIC RELEASE_DEBUG_LOGGING=1)

add_executable(bfme_lighting_tests
    ${BFME_E125_TESTS_DIR}/harness/TestMain.cpp
    src/TestLighting.cpp
)
target_link_libraries(bfme_lighting_tests PRIVATE bfme_lighting_engine)

set(BFME_LIGHTING_TESTS
    lighting_v7_1348_all_lights
    lighting_v7_truncated_rejected
    lighting_v3_zh_no_regression
    lighting_version_0_and_8_rejected
)
foreach(t IN LISTS BFME_LIGHTING_TESTS)
    add_test(NAME lighting.${t} COMMAND bfme_lighting_tests ${t})
    set_tests_properties(lighting.${t} PROPERTIES SKIP_RETURN_CODE 77 LABELS "unit")
endforeach()
add_test(NAME lighting.registry_complete COMMAND ${CMAKE_COMMAND}
    -DBIN=$<TARGET_FILE:bfme_lighting_tests> "-DEXPECTED=${BFME_LIGHTING_TESTS}"
    -P ${BFME_E125_TESTS_DIR}/cmake/check_registry.cmake)
set_tests_properties(lighting.registry_complete PROPERTIES LABELS "unit")
