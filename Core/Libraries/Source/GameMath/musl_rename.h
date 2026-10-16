/* BFME: E1-49. Not a musl file.
 * Included ahead of each vendored musl .c so the public libm names do not
 * replace the platform libm. musl's own `hidden` macro lives in
 * src/include/features.h (see README); that header pulls musl's public
 * features.h, which we do not put on the include path.
 */
#ifndef GAMEMATH_MUSL_RENAME_H
#define GAMEMATH_MUSL_RENAME_H

#include <math.h>
#include <stdint.h>
#include <float.h>
#include <endian.h>

#if defined(FLT_EVAL_METHOD) && FLT_EVAL_METHOD != 0
#error GameMath requires FLT_EVAL_METHOD 0 (SSE2 / AArch64, no x87 excess precision)
#endif

/* Same attribute musl gives `hidden` in src/include/features.h. */
#ifndef hidden
#define hidden __attribute__((__visibility__("hidden")))
#endif

/* pow.c takes the explicit __builtin_fma path when this is set. AArch64
 * defines it by default and x86_64 without -mfma does not, so the two
 * architectures would disagree. Contracted fma is already off; this drops
 * the source-level fma branch on both. */
#undef __FP_FAST_FMA

#undef sin
#undef cos
#undef tan
#undef asin
#undef acos
#undef atan
#undef atan2
#undef pow
#undef sinf
#undef cosf
#undef tanf
#undef asinf
#undef acosf
#undef atanf
#undef atan2f

#define sin gm_musl_sin
#define cos gm_musl_cos
#define tan gm_musl_tan
#define asin gm_musl_asin
#define acos gm_musl_acos
#define atan gm_musl_atan
#define atan2 gm_musl_atan2
#define pow gm_musl_pow
#define sinf gm_musl_sinf
#define cosf gm_musl_cosf
#define tanf gm_musl_tanf
#define asinf gm_musl_asinf
#define acosf gm_musl_acosf
#define atanf gm_musl_atanf
#define atan2f gm_musl_atan2f

/* glibc declares __sin/__cos/__tan as the public functions. musl uses those
 * names for the pi/4 kernels, with a different signature. Rename the kernels
 * after math.h has been parsed so the two do not collide. */
#define __sin gm_musl_ksin
#define __cos gm_musl_kcos
#define __tan gm_musl_ktan
#define __sinl gm_musl_ksinl
#define __cosl gm_musl_kcosl
#define __tanl gm_musl_ktanl

/* Internal musl names. Not part of the libc math.h we included above, so
 * renaming them does not touch the platform libm. Each identifier is its own
 * token: __rem_pio2 does not rename __rem_pio2f. */
#define __rem_pio2 gm_musl_rem_pio2
#define __rem_pio2f gm_musl_rem_pio2f
#define __rem_pio2_large gm_musl_rem_pio2_large
#define __rem_pio2l gm_musl_rem_pio2l
#define __math_xflow gm_musl_math_xflow
#define __math_uflow gm_musl_math_uflow
#define __math_oflow gm_musl_math_oflow
#define __math_invalid gm_musl_math_invalid
#define __math_xflowf gm_musl_math_xflowf
#define __math_uflowf gm_musl_math_uflowf
#define __math_oflowf gm_musl_math_oflowf
#define __math_invalidf gm_musl_math_invalidf
#define __math_invalidl gm_musl_math_invalidl
#define __exp_data gm_musl_exp_data
#define __pow_log_data gm_musl_pow_log_data

/* math.h declared the libc names before the macros above. Calls in musl
 * now spell gm_musl_*, which need prototypes or C treats them as returning int. */
double gm_musl_sin(double);
double gm_musl_cos(double);
double gm_musl_tan(double);
double gm_musl_asin(double);
double gm_musl_acos(double);
double gm_musl_atan(double);
double gm_musl_atan2(double, double);
double gm_musl_pow(double, double);
float gm_musl_sinf(float);
float gm_musl_cosf(float);
float gm_musl_tanf(float);
float gm_musl_asinf(float);
float gm_musl_acosf(float);
float gm_musl_atanf(float);
float gm_musl_atan2f(float, float);

#endif
