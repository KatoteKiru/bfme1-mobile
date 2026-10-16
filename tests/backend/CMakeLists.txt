# BFME: E1-17b Desktop GENERALSX_RENDER_BACKEND token table.
# Standalone: no game, no GLES, no vcpkg.
#
#   cmake -S tests/backend -B build/tests-backend -G "Unix Makefiles"
#   cmake --build build/tests-backend -j2
#   ctest --test-dir build/tests-backend --output-on-failure
cmake_minimum_required(VERSION 3.25)
project(bfme_desktop_backend_tests CXX)

include(CTest)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(BFME_REPO_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)

add_executable(bfme_desktop_backend_tests src/TestDesktopBackend.cpp)
target_include_directories(bfme_desktop_backend_tests PRIVATE
    ${BFME_REPO_ROOT}/Core/Libraries/Source/d3d8gles/include
)
target_compile_options(bfme_desktop_backend_tests PRIVATE -Wall -Wextra -Werror)
add_test(NAME desktop_backend_tokens COMMAND bfme_desktop_backend_tests)
