#pragma once

// BFME: E1-49. Deterministic libm for logic, from musl (MIT).
// sqrt, floor, ceil and trunc stay on the platform libm: IEEE 754 makes
// those exact. SAGE_USE_DETERMINISTIC_MATH stays off until the x86_64 vs
// arm64 bit-exact test passes (ADR-0003 §2.2).

namespace GameMath {

double Sin(double x);
double Cos(double x);
double Tan(double x);
double Asin(double x);
double Acos(double x);
double Atan(double x);
double Atan2(double y, double x);
double Pow(double x, double y);

float Sin(float x);
float Cos(float x);
float Tan(float x);
float Asin(float x);
float Acos(float x);
float Atan(float x);
float Atan2(float y, float x);

}
