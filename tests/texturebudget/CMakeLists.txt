# BFME: K6 texture-edge budget and the headless device gate.
# Standalone: header only, no game, no GLES, no vcpkg.
cmake_minimum_required(VERSION 3.25)
project(bfme_texture_budget_tests CXX)

include(CTest)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(BFME_REPO_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)

add_executable(bfme_texture_budget_tests src/TestHostLimits.cpp)
target_include_directories(bfme_texture_budget_tests PRIVATE
    ${BFME_REPO_ROOT}/Core/Libraries/Source/WWVegas/WW3D2
)
target_compile_options(bfme_texture_budget_tests PRIVATE -Wall -Wextra -Werror)
add_test(NAME texturebudget.host_limits COMMAND bfme_texture_budget_tests)
set_tests_properties(texturebudget.host_limits PROPERTIES LABELS "unit")
