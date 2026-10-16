#pragma once

// BFME: E1-49. Ordinary int32 conversions for new BFME code.
// This header lives under Libraries/Include, which is on the include path
// with SAGE_USE_DETERMINISTIC_MATH OFF. It is not REAL_TO_INT_FLOOR / CEIL
// (ADR-0003 §2.7). NaN becomes 0. A finite value outside int32 saturates.
// roundToInt32 is half away from zero (std::round), not lroundf.

#include <cstdint>
#include <cmath>

namespace GameMath {

inline int32_t saturateToInt32(float y)
{
	if (y != y) {
		return 0;
	}
	if (y >= 2147483648.0f) {
		return INT32_MAX;
	}
	if (y < -2147483648.0f) {
		return INT32_MIN;
	}
	return static_cast<int32_t>(y);
}

inline int32_t floorToInt32(float x)
{
	return saturateToInt32(std::floor(x));
}

inline int32_t ceilToInt32(float x)
{
	return saturateToInt32(std::ceil(x));
}

inline int32_t truncToInt32(float x)
{
	return saturateToInt32(std::trunc(x));
}

inline int32_t roundToInt32(float x)
{
	return saturateToInt32(std::round(x));
}

}
