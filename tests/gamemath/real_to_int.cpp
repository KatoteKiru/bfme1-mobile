// Locks REAL_TO_INT_FLOOR / REAL_TO_INT_CEIL to the ZH behaviour in
// ADR-0003 §2.7. "Fixing" the macros to ordinary floor/ceil fails this test.
// The macros themselves are not modified here.

#include <type_traits>

#include "Lib/BaseType.h"
#include "Lib/GameMathInt.h"

#include <cstdint>
#include <cstdio>
#include <cmath>

namespace {

int fail(const char* msg)
{
	std::fprintf(stderr, "FAIL: %s\n", msg);
	return 1;
}

long floorOf(float x)
{
	return REAL_TO_INT_FLOOR(x);
}

long ceilOf(float x)
{
	return REAL_TO_INT_CEIL(x);
}

} // namespace

int main()
{
	// n = 1 .. 2^24-1: negative integer floor is -n-1, positive integer ceil is n+1.
	// n = 2^24: the sum rounds to even and both are exact (§2.7).
	const int limit = 16777216;
	for (int n = 1; n <= limit; ++n) {
		const float fn = static_cast<float>(n);
		const long fl = floorOf(-fn);
		const long ce = ceilOf(fn);
		if (n < limit) {
			if (fl != static_cast<long>(-n - 1) || ce != static_cast<long>(n + 1)) {
				std::fprintf(stderr, "FAIL: n=%d floor(-n)=%ld ceil(n)=%ld\n", n, fl, ce);
				return 1;
			}
			if (floorOf(fn) != n || ceilOf(-fn) != static_cast<long>(-n)) {
				std::fprintf(stderr, "FAIL: exact side n=%d\n", n);
				return 1;
			}
		} else if (fl != static_cast<long>(-n) || ce != static_cast<long>(n)) {
			std::fprintf(stderr, "FAIL: 2^24 floor=%ld ceil=%ld\n", fl, ce);
			return 1;
		}
	}

	const float samples[] = {0.f, -0.f, 0.5f, -0.5f, 1.25f, -1.25f, 0.1f, -0.1f, 2.9f, -3.2f};
	for (float x : samples) {
		const long expectFloor = static_cast<long>(std::floor(x));
		const long expectCeil = static_cast<long>(std::ceil(x));
		if (floorOf(x) != expectFloor || ceilOf(x) != expectCeil) {
			std::fprintf(stderr, "FAIL: non-integer %g floor=%ld ceil=%ld\n", x, floorOf(x), ceilOf(x));
			return 1;
		}
	}

	// The BFME helper is ordinary floor/ceil and must not grow the ZH quirk.
	if (GameMath::floorToInt32(-1.0f) != -1 || GameMath::ceilToInt32(1.0f) != 1) {
		return fail("GameMath helper must not follow REAL_TO_INT_*");
	}
	if (floorOf(-1.0f) != -2 || ceilOf(1.0f) != 2) {
		return fail("ZH macros lost the §2.7 integer quirk");
	}

	std::printf("real_to_int zh ok\n");
	return 0;
}
