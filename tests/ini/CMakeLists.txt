# BFME1 Mobile — E1-23a tests: lenient INI line rules (Common/INILenientRules.h). Standalone, no game.
# SPDX-License-Identifier: GPL-3.0-or-later
#
#   cmake -S tests/ini -B build/tests-ini && cmake --build build/tests-ini && ctest --test-dir build/tests-ini
cmake_minimum_required(VERSION 3.20)
project(bfme_ini_lenient_tests CXX)
include(CTest)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(R ${CMAKE_CURRENT_SOURCE_DIR}/../..)
add_executable(bfme_ini_lenient_tests TestIniLenientRules.cpp ${R}/Core/GameEngine/Source/Common/INI/INILenientRules.cpp)
target_include_directories(bfme_ini_lenient_tests PRIVATE ${R}/Core/GameEngine/Include)
target_compile_options(bfme_ini_lenient_tests PRIVATE -Wall -Wextra)
add_test(NAME ini.lenient_rules COMMAND bfme_ini_lenient_tests)
set_tests_properties(ini.lenient_rules PROPERTIES LABELS unit)
