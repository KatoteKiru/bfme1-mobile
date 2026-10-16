# BFME: E1-49. Own GameMath on libm from musl (MIT), not FetchContent of TSH.
# TSH Core/GameMath does not exist at the tag this file used to pin.
# The option name and the USE_DETERMINISTIC_MATH define stay, so a later
# merge with TSH #2670 still has a single switch.
#
# SAGE_USE_DETERMINISTIC_MATH stays OFF until tests/gamemath has a passing
# bit-exact x86_64 vs arm64 run. A skipped qemu run is not that pass.
# Do not turn the option on in the presets before then.

if(NOT IS_VS6_BUILD)
    option(SAGE_USE_DETERMINISTIC_MATH "Use musl libm GameMath for cross-platform replay validation" OFF)
else()
    set(SAGE_USE_DETERMINISTIC_MATH OFF)
endif()

if(SAGE_USE_DETERMINISTIC_MATH)
    message(STATUS "GameMath: musl libm, USE_DETERMINISTIC_MATH")
    # Not add_subdirectory: that directory's CMakeLists.txt is the fp gate.
    include(${CMAKE_CURRENT_LIST_DIR}/../Core/Libraries/Source/GameMath/gamemath_lib.cmake)
    add_compile_definitions(USE_DETERMINISTIC_MATH)
else()
    message(STATUS "Deterministic math disabled (SAGE_USE_DETERMINISTIC_MATH=OFF)")
    message(STATUS "  Logic keeps the platform libm until the bit-exact test passes")
endif()
