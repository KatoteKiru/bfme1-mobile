# BFME: ADR-0003 §2.3 thread-gate tests. Standalone: no game, no vcpkg.
#
#   cmake -S tests/fp -B build/tests-fp -G "Unix Makefiles" && cmake --build build/tests-fp -j2
#   ctest --test-dir build/tests-fp --output-on-failure
# Verification builds need an explicit -DBFME_FP_CI=ON: GitHub Actions are off, so ENV{CI} does not enable it.
cmake_minimum_required(VERSION 3.25)
project(bfme_fp_thread_tests CXX)

include(CTest)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

find_package(Threads REQUIRED)

set(BFME_REPO_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)
set(BFME_FP_SOURCES
    ${BFME_REPO_ROOT}/Core/Libraries/Source/GameMath/fp/fp_thread.cpp
    ${BFME_REPO_ROOT}/Core/Libraries/Source/GameMath/fp/fp_platform_stub.cpp
    src/TestMain.cpp
    src/TestFpThread.cpp
)

set(BFME_FP_TESTS
    bind_then_same_thread_checks_pass
    assert_expected_does_not_reset
    check_without_prior_bind
    call_from_wrong_thread
    rebind_from_other_thread
    sdl_thread_name_is_warning_only
    check_and_reset_resets_once_per_call
    reset_restores_tonearest
)

function(bfme_fp_test_binary name)
    add_executable(${name} ${BFME_FP_SOURCES})
    target_include_directories(${name} PRIVATE
        ${BFME_REPO_ROOT}/Core/Libraries/Source/GameMath/include
        ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
    target_compile_definitions(${name} PRIVATE BFME_FP_THREAD_TEST=1)
    target_link_libraries(${name} PRIVATE Threads::Threads)
    target_compile_options(${name} PRIVATE -Wall -Wextra -Werror)
endfunction()

# Debug policy: mismatch, unbound, and a second bind from another thread are fatal.
bfme_fp_test_binary(bfme_fp_tests_debug)
target_compile_options(bfme_fp_tests_debug PRIVATE -UNDEBUG)

# Release policy: one log line. Unbound binds the caller. Rebind keeps the original thread.
bfme_fp_test_binary(bfme_fp_tests_release)
target_compile_definitions(bfme_fp_tests_release PRIVATE NDEBUG)

# CI without debug: mismatch and unbound are fatal. Rebind from another thread is not.
bfme_fp_test_binary(bfme_fp_tests_ci)
target_compile_definitions(bfme_fp_tests_ci PRIVATE NDEBUG BFME_FP_CI=1)

foreach(bin IN ITEMS bfme_fp_tests_debug bfme_fp_tests_release bfme_fp_tests_ci)
    foreach(t IN LISTS BFME_FP_TESTS)
        add_test(NAME ${bin}.${t} COMMAND ${bin} ${t})
    endforeach()
endforeach()
