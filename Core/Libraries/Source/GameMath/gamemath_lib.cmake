# BFME: E1-49. musl libm (MIT) compiled into the GameMath target.
# Included, never add_subdirectory of this directory: CMakeLists.txt here
# belongs to the fp gate (core_gamemath, PR that adds fp/). The game includes
# this fragment from cmake/gamemath.cmake only when SAGE_USE_DETERMINISTIC_MATH
# is ON. tests/gamemath includes it so the bit-exact binary can link.

if(TARGET GameMath)
    return()
endif()

# include() does not change CMAKE_CURRENT_SOURCE_DIR, so paths are anchored
# on this file.
set(GAMEMATH_DIR ${CMAKE_CURRENT_LIST_DIR})

set(GAMEMATH_MUSL_SOURCES
    ${GAMEMATH_DIR}/musl/src/math/sin.c
    ${GAMEMATH_DIR}/musl/src/math/cos.c
    ${GAMEMATH_DIR}/musl/src/math/tan.c
    ${GAMEMATH_DIR}/musl/src/math/asin.c
    ${GAMEMATH_DIR}/musl/src/math/acos.c
    ${GAMEMATH_DIR}/musl/src/math/atan.c
    ${GAMEMATH_DIR}/musl/src/math/atan2.c
    ${GAMEMATH_DIR}/musl/src/math/pow.c
    ${GAMEMATH_DIR}/musl/src/math/sinf.c
    ${GAMEMATH_DIR}/musl/src/math/cosf.c
    ${GAMEMATH_DIR}/musl/src/math/tanf.c
    ${GAMEMATH_DIR}/musl/src/math/asinf.c
    ${GAMEMATH_DIR}/musl/src/math/acosf.c
    ${GAMEMATH_DIR}/musl/src/math/atanf.c
    ${GAMEMATH_DIR}/musl/src/math/atan2f.c
    ${GAMEMATH_DIR}/musl/src/math/__sin.c
    ${GAMEMATH_DIR}/musl/src/math/__cos.c
    ${GAMEMATH_DIR}/musl/src/math/__tan.c
    ${GAMEMATH_DIR}/musl/src/math/__sindf.c
    ${GAMEMATH_DIR}/musl/src/math/__cosdf.c
    ${GAMEMATH_DIR}/musl/src/math/__tandf.c
    ${GAMEMATH_DIR}/musl/src/math/__rem_pio2.c
    ${GAMEMATH_DIR}/musl/src/math/__rem_pio2f.c
    ${GAMEMATH_DIR}/musl/src/math/__rem_pio2_large.c
    ${GAMEMATH_DIR}/musl/src/math/__math_invalid.c
    ${GAMEMATH_DIR}/musl/src/math/__math_oflow.c
    ${GAMEMATH_DIR}/musl/src/math/__math_uflow.c
    ${GAMEMATH_DIR}/musl/src/math/__math_xflow.c
    ${GAMEMATH_DIR}/musl/src/math/exp_data.c
    ${GAMEMATH_DIR}/musl/src/math/pow_data.c
)

add_library(GameMath STATIC ${GAMEMATH_MUSL_SOURCES} ${GAMEMATH_DIR}/src/GameMath.cpp)

target_include_directories(GameMath
    PUBLIC ${GAMEMATH_DIR}/include
    PRIVATE
        ${GAMEMATH_DIR}/musl/src/internal
        ${GAMEMATH_DIR}/musl/src/math
        ${GAMEMATH_DIR}/musl/arch/generic
)

target_compile_features(GameMath PUBLIC cxx_std_17)

# This fragment is included only when SAGE_USE_DETERMINISTIC_MATH is ON
# (and by the bit-exact test, which is not an MSVC build).
if(MSVC)
    message(FATAL_ERROR
        "SAGE_USE_DETERMINISTIC_MATH: musl GameMath is not supported with MSVC. Leave the flag OFF.")
endif()

# Same flags on x86_64 and arm64. No -ffast-math, no -mfma, no x87.
# __FP_FAST_FMA is cleared in musl_rename.h. Visibility hidden is C-only so
# the C++ wrappers stay linkable.
target_compile_options(GameMath PRIVATE
    $<$<COMPILE_LANGUAGE:C>:-std=c11>
    $<$<COMPILE_LANGUAGE:C>:-D_XOPEN_SOURCE=700>
    $<$<COMPILE_LANGUAGE:C>:-ffp-contract=off>
    $<$<COMPILE_LANGUAGE:C>:-fno-strict-aliasing>
    $<$<COMPILE_LANGUAGE:C>:-fno-math-errno>
    $<$<COMPILE_LANGUAGE:C>:-fvisibility=hidden>
    $<$<COMPILE_LANGUAGE:C>:-O2>
    $<$<COMPILE_LANGUAGE:C>:-Wall>
    $<$<COMPILE_LANGUAGE:C>:-Wno-unused-function>
    $<$<COMPILE_LANGUAGE:C>:-Wno-unused-but-set-variable>
    $<$<COMPILE_LANGUAGE:C>:-Wno-parentheses>
    $<$<COMPILE_LANGUAGE:C>:-include>
    "$<$<COMPILE_LANGUAGE:C>:${GAMEMATH_DIR}/musl_rename.h>"
    $<$<COMPILE_LANGUAGE:CXX>:-ffp-contract=off>
)
