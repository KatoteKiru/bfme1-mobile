// E1-49: GameMath on one architecture, and the input table the cross run
// compares. NaN payloads are not part of IEEE 754, so a NaN result is hashed
// as one canonical NaN. Finite values, infinities and signed zeros are bits.

#include "GameMath.h"
#include "Lib/GameMathInt.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr int kSpanCount = 1000000;
constexpr int kUnitCount = 1000000;
constexpr int kAtan2Side = 256;
constexpr double kPi = 3.14159265358979323846;
constexpr double kFourPi = 4.0 * kPi;

uint64_t xorshift(uint64_t& state)
{
	state ^= state >> 12;
	state ^= state << 25;
	state ^= state >> 27;
	return state * 0x2545F4914F6CDD1Dull;
}

uint64_t fnvMix(uint64_t hash, uint64_t word)
{
	hash ^= word;
	hash *= 0x100000001b3ull;
	return hash;
}

uint64_t canonDouble(double y)
{
	uint64_t bits = 0;
	std::memcpy(&bits, &y, sizeof(bits));
	const uint64_t exp = bits & 0x7ff0000000000000ull;
	const uint64_t frac = bits & 0x000fffffffffffffull;
	if (exp == 0x7ff0000000000000ull && frac != 0) {
		return 0x7ff8000000000000ull;
	}
	return bits;
}

uint64_t canonFloat(float y)
{
	uint32_t bits = 0;
	std::memcpy(&bits, &y, sizeof(bits));
	const uint32_t exp = bits & 0x7f800000u;
	const uint32_t frac = bits & 0x007fffffu;
	if (exp == 0x7f800000u && frac != 0) {
		return 0x7fc00000u;
	}
	return bits;
}

double bitsToDouble(uint64_t bits)
{
	double y = 0;
	std::memcpy(&y, &bits, sizeof(y));
	return y;
}

float bitsToFloat(uint32_t bits)
{
	float y = 0;
	std::memcpy(&y, &bits, sizeof(y));
	return y;
}

double unitInterval(uint64_t& state)
{
	// Top 53 bits. The scale is a power of two, so this is an exact double.
	const uint64_t bits = xorshift(state) >> 11;
	return static_cast<double>(bits) * (1.0 / 9007199254740992.0);
}

double uniform(uint64_t& state, double lo, double hi)
{
	return lo + (hi - lo) * unitInterval(state);
}

struct Table {
	std::vector<double> d;
	std::vector<double> unit;
	std::vector<double> grid;
	std::vector<float> f;
	std::vector<float> unitf;
	std::vector<float> gridf;
};

Table makeTable()
{
	Table t;
	t.d.reserve(static_cast<size_t>(kSpanCount) + 32);
	t.f.reserve(static_cast<size_t>(kSpanCount) + 32);
	t.unit.reserve(kUnitCount);
	t.unitf.reserve(kUnitCount);
	uint64_t state = 0x9E3779B97F4A7C15ull;
	for (int i = 0; i < kSpanCount; ++i) {
		const double x = uniform(state, -kFourPi, kFourPi);
		t.d.push_back(x);
		t.f.push_back(static_cast<float>(x));
	}
	for (int i = 0; i < kUnitCount; ++i) {
		const double x = uniform(state, -1.0, 1.0);
		t.unit.push_back(x);
		t.unitf.push_back(static_cast<float>(x));
	}
	t.grid.reserve(static_cast<size_t>(kAtan2Side) * kAtan2Side);
	t.gridf.reserve(t.grid.capacity());
	for (int j = 0; j < kAtan2Side; ++j) {
		const double y = -kFourPi + (2.0 * kFourPi) * (static_cast<double>(j) / (kAtan2Side - 1));
		for (int i = 0; i < kAtan2Side; ++i) {
			const double x = -kFourPi + (2.0 * kFourPi) * (static_cast<double>(i) / (kAtan2Side - 1));
			t.grid.push_back(y);
			t.grid.push_back(x);
			t.gridf.push_back(static_cast<float>(y));
			t.gridf.push_back(static_cast<float>(x));
		}
	}
	const double edges[] = {
		0.0, -0.0, 1.0, -1.0, 0.5, -0.5,
		kPi, kPi / 2.0, kPi / 4.0, 2.0 * kPi,
		1e-320, -1e-320, 1e-10, -1e-10, 1e20, -1e20,
		std::numeric_limits<double>::infinity(),
		-std::numeric_limits<double>::infinity(),
		std::numeric_limits<double>::quiet_NaN(),
		std::numeric_limits<double>::min(),
		std::numeric_limits<double>::max(),
		std::numeric_limits<double>::denorm_min(),
	};
	for (double e : edges) {
		t.d.push_back(e);
		t.f.push_back(static_cast<float>(e));
	}
	return t;
}

template <typename Fn>
uint64_t hash1(const std::vector<double>& in, Fn fn)
{
	uint64_t h = 0xcbf29ce484222325ull;
	for (double x : in) {
		h = fnvMix(h, canonDouble(fn(x)));
	}
	return h;
}

template <typename Fn>
uint64_t hash1f(const std::vector<float>& in, Fn fn)
{
	uint64_t h = 0xcbf29ce484222325ull;
	for (float x : in) {
		h = fnvMix(h, canonFloat(fn(x)));
	}
	return h;
}

template <typename Fn>
uint64_t hashGrid(const std::vector<double>& pairs, Fn fn)
{
	uint64_t h = 0xcbf29ce484222325ull;
	for (size_t i = 0; i + 1 < pairs.size(); i += 2) {
		h = fnvMix(h, canonDouble(fn(pairs[i], pairs[i + 1])));
	}
	return h;
}

template <typename Fn>
uint64_t hashGridf(const std::vector<float>& pairs, Fn fn)
{
	uint64_t h = 0xcbf29ce484222325ull;
	for (size_t i = 0; i + 1 < pairs.size(); i += 2) {
		h = fnvMix(h, canonFloat(fn(pairs[i], pairs[i + 1])));
	}
	return h;
}

int fail(const char* msg)
{
	std::fprintf(stderr, "FAIL: %s\n", msg);
	return 1;
}

int checkHelpers()
{
	const float nan = std::numeric_limits<float>::quiet_NaN();
	const float pinf = std::numeric_limits<float>::infinity();
	const float ninf = -std::numeric_limits<float>::infinity();
	if (GameMath::floorToInt32(nan) != 0 || GameMath::ceilToInt32(nan) != 0
		|| GameMath::truncToInt32(nan) != 0 || GameMath::roundToInt32(nan) != 0) {
		return fail("NaN must become 0");
	}
	if (GameMath::floorToInt32(pinf) != INT32_MAX || GameMath::ceilToInt32(ninf) != INT32_MIN) {
		return fail("infinity must saturate");
	}
	if (GameMath::floorToInt32(1.9f) != 1 || GameMath::ceilToInt32(1.9f) != 2
		|| GameMath::truncToInt32(1.9f) != 1 || GameMath::roundToInt32(1.5f) != 2
		|| GameMath::roundToInt32(-1.5f) != -2) {
		return fail("ordinary round/floor/ceil/trunc");
	}
	if (GameMath::floorToInt32(-1.1f) != -2 || GameMath::ceilToInt32(-1.1f) != -1) {
		return fail("negative non-integer");
	}
	// Ordinary, unlike REAL_TO_INT_FLOOR(-1) == -2 and REAL_TO_INT_CEIL(1) == 2.
	if (GameMath::floorToInt32(-1.0f) != -1 || GameMath::ceilToInt32(1.0f) != 1) {
		return fail("exact integers must not use the ZH fast-float quirk");
	}
	if (GameMath::floorToInt32(1.0e20f) != INT32_MAX || GameMath::ceilToInt32(-1.0e20f) != INT32_MIN) {
		return fail("out of range must saturate");
	}
	if (GameMath::floorToInt32(-2147483648.0f) != INT32_MIN) {
		return fail("INT32_MIN must be preserved");
	}
	std::printf("helpers ok\n");
	return 0;
}

struct HashRow {
	const char* name;
	uint64_t value;
};

void fillHashes(const Table& t, HashRow* rows, int& n)
{
	n = 0;
	const auto add = [&](const char* name, uint64_t value) {
		rows[n].name = name;
		rows[n].value = value;
		++n;
	};
	add("sin", hash1(t.d, [](double x) { return GameMath::Sin(x); }));
	add("cos", hash1(t.d, [](double x) { return GameMath::Cos(x); }));
	add("tan", hash1(t.d, [](double x) { return GameMath::Tan(x); }));
	add("asin", hash1(t.unit, [](double x) { return GameMath::Asin(x); }));
	add("acos", hash1(t.unit, [](double x) { return GameMath::Acos(x); }));
	add("atan", hash1(t.d, [](double x) { return GameMath::Atan(x); }));
	add("atan2", hashGrid(t.grid, [](double y, double x) { return GameMath::Atan2(y, x); }));
	add("pow", hashGrid(t.grid, [](double x, double y) { return GameMath::Pow(x, y); }));
	add("sinf", hash1f(t.f, [](float x) { return GameMath::Sin(x); }));
	add("cosf", hash1f(t.f, [](float x) { return GameMath::Cos(x); }));
	add("tanf", hash1f(t.f, [](float x) { return GameMath::Tan(x); }));
	add("asinf", hash1f(t.unitf, [](float x) { return GameMath::Asin(x); }));
	add("acosf", hash1f(t.unitf, [](float x) { return GameMath::Acos(x); }));
	add("atanf", hash1f(t.f, [](float x) { return GameMath::Atan(x); }));
	add("atan2f", hashGridf(t.gridf, [](float y, float x) { return GameMath::Atan2(y, x); }));
}

void printHashes(const Table& t)
{
	// Measured length of the [-4pi, 4pi] sample, not a compile-time constant.
	std::printf("count %zu\n", t.d.size());
	HashRow rows[16];
	int n = 0;
	fillHashes(t, rows, n);
	for (int i = 0; i < n; ++i) {
		std::printf("%s %016llx\n", rows[i].name, static_cast<unsigned long long>(rows[i].value));
	}
}

int checkAgainstReference(const Table& t)
{
	const char* path = GAMEMATH_HASH_FILE;
	std::ifstream in(path);
	if (!in) {
		return fail("reference hash file missing");
	}
	HashRow rows[16];
	int n = 0;
	fillHashes(t, rows, n);
	std::string line;
	int seen = 0;
	bool sawCount = false;
	while (std::getline(in, line)) {
		if (line.empty() || line[0] == '#') {
			continue;
		}
		const auto sp = line.find(' ');
		if (sp == std::string::npos) {
			return fail("bad reference line");
		}
		const std::string key = line.substr(0, sp);
		const std::string val = line.substr(sp + 1);
		if (key == "count") {
			sawCount = true;
			if (val != std::to_string(t.d.size())) {
				return fail("reference count is not t.d.size()");
			}
			continue;
		}
		bool found = false;
		for (int i = 0; i < n; ++i) {
			if (key != rows[i].name) {
				continue;
			}
			char buf[32];
			std::snprintf(buf, sizeof(buf), "%016llx", static_cast<unsigned long long>(rows[i].value));
			if (val != buf) {
				std::fprintf(stderr, "FAIL: %s hash %s != reference %s\n", rows[i].name, buf, val.c_str());
				return 1;
			}
			found = true;
			++seen;
			break;
		}
		if (!found) {
			return fail("unknown reference hash name");
		}
	}
	if (!sawCount || seen != n) {
		return fail("reference file is missing hashes");
	}
	std::printf("self-check ok count %zu\n", t.d.size());
	return 0;
}

} // namespace

int main(int argc, char** argv)
{
	const std::string mode = argc > 1 ? argv[1] : "--self-check";
	if (mode == "--helpers") {
		return checkHelpers();
	}
	if (checkHelpers() != 0) {
		return 1;
	}
	const Table t = makeTable();
	if (t.d.size() < static_cast<size_t>(kSpanCount)) {
		return fail("input table shorter than 10^6");
	}
	if (mode == "--hash") {
		printHashes(t);
		return 0;
	}
	if (mode == "--self-check") {
		const Table again = makeTable();
		if (again.d.size() != t.d.size() || again.unit.size() != t.unit.size() || again.grid.size() != t.grid.size()) {
			return fail("table size changed");
		}
		for (size_t i = 0; i < t.d.size(); ++i) {
			uint64_t a = 0;
			uint64_t b = 0;
			std::memcpy(&a, &t.d[i], sizeof(a));
			std::memcpy(&b, &again.d[i], sizeof(b));
			if (a != b) {
				return fail("input table is not deterministic");
			}
		}
		return checkAgainstReference(t);
	}
	std::fprintf(stderr, "usage: %s [--helpers|--self-check|--hash]\n", argv[0]);
	return 2;
}
