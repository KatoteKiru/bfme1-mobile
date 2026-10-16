// BFME: E1-49. Wrappers around the vendored musl libm. The .c files are
// compiled with musl_rename.h so these symbols do not replace libc.

#include "GameMath.h"

extern "C" {
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
}

namespace GameMath {

double Sin(double x) { return gm_musl_sin(x); }
double Cos(double x) { return gm_musl_cos(x); }
double Tan(double x) { return gm_musl_tan(x); }
double Asin(double x) { return gm_musl_asin(x); }
double Acos(double x) { return gm_musl_acos(x); }
double Atan(double x) { return gm_musl_atan(x); }
double Atan2(double y, double x) { return gm_musl_atan2(y, x); }
double Pow(double x, double y) { return gm_musl_pow(x, y); }

float Sin(float x) { return gm_musl_sinf(x); }
float Cos(float x) { return gm_musl_cosf(x); }
float Tan(float x) { return gm_musl_tanf(x); }
float Asin(float x) { return gm_musl_asinf(x); }
float Acos(float x) { return gm_musl_acosf(x); }
float Atan(float x) { return gm_musl_atanf(x); }
float Atan2(float y, float x) { return gm_musl_atan2f(y, x); }

}
