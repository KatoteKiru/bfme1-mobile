# BFME1 Mobile — E1-32: ThreadClass::_Get_Current_Thread_ID on Linux/Android
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Standalone: compiles the real Core/.../WWLib/thread.cpp (the _UNIX branch) and
# checks two threads. No vcpkg, no game link.
#
#   cmake -S tests/thread -B build/tests-thread -G "Unix Makefiles"
#   cmake --build build/tests-thread
#   ctest --test-dir build/tests-thread --output-on-failure
cmake_minimum_required(VERSION 3.25)
project(bfme_thread_id_tests CXX)

include(CTest)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

if(NOT UNIX OR NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "tests/thread builds the Linux/Android branch of thread.cpp")
endif()

get_filename_component(BFME_REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(BFME_DXVK_INCLUDE_DIR "${BFME_REPO_ROOT}/references/fbraz3-dxvk/include" CACHE PATH
    "DXVK headers (submodule references/fbraz3-dxvk)")
if(NOT EXISTS "${BFME_DXVK_INCLUDE_DIR}/native/windows/windows_base.h")
    message(FATAL_ERROR "DXVK native headers not found in ${BFME_DXVK_INCLUDE_DIR}. "
        "Run: git submodule update --init references/fbraz3-dxvk")
endif()

set(WWLIB "${BFME_REPO_ROOT}/Core/Libraries/Source/WWVegas/WWLib")

add_executable(bfme_thread_id_tests
    src/TestThreadId.cpp
    ${WWLIB}/thread.cpp
)
target_compile_definitions(bfme_thread_id_tests PRIVATE
    LINUX _UNIX POSIX RTS_ZEROHOUR=1 RTS_RELEASE NDEBUG
    HAVE_STRLCPY HAVE_STRLCAT HAVE_WCSLCPY HAVE_WCSLCAT
)
target_include_directories(bfme_thread_id_tests PRIVATE
    ${BFME_REPO_ROOT}/GeneralsMD/Code/CompatLib/Include
    ${BFME_REPO_ROOT}/Generals/Code/CompatLib/Include
    ${WWLIB}
    ${BFME_REPO_ROOT}/Core/Libraries/Source/WWVegas/WW3D2
    ${BFME_REPO_ROOT}/Core/Libraries/Source/WWVegas/WWDebug
    ${BFME_REPO_ROOT}/Core/Libraries/Source/WWVegas
    ${BFME_REPO_ROOT}/Core/Libraries/Include
    ${BFME_REPO_ROOT}/Core/GameEngine/Include
    ${BFME_REPO_ROOT}/Dependencies/Utility
    ${BFME_DXVK_INCLUDE_DIR}/native
    ${BFME_DXVK_INCLUDE_DIR}/native/windows
    ${BFME_DXVK_INCLUDE_DIR}/native/directx
)
target_compile_options(bfme_thread_id_tests PRIVATE
    -include Utility/CppMacros.h
    -Wno-deprecated-declarations
    -ffp-contract=off -fno-strict-aliasing -fwrapv -fno-delete-null-pointer-checks
    -ffunction-sections -fdata-sections
)
target_link_options(bfme_thread_id_tests PRIVATE -Wl,--gc-sections)
target_link_libraries(bfme_thread_id_tests PRIVATE pthread)

add_test(NAME thread.two_threads_and_stable_main COMMAND bfme_thread_id_tests)
set_tests_properties(thread.two_threads_and_stable_main PROPERTIES LABELS "unit")
