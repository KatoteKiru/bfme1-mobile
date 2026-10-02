// BFME1 Mobile — tests (copy of tests/vfs/<file> from E1-21, kept standalone so E1-25 does not depend on E1-21)
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Minimal dependency-free test harness: the repo has no unit-test framework, and the tests must
// build without vcpkg. Each TEST is registered by name; `<test binary> <name>` runs one test
// (ctest registers one test per name), the binary without arguments runs all. Exit code 77 = skipped.
#pragma once

#include <cstdio>
#include <functional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace bfmetest {

struct TestCase {
	std::string name;
	std::function<void()> body;
};

std::vector<TestCase>& registry();

struct Registrar {
	Registrar(const char* name, std::function<void()> body) { registry().push_back({name, std::move(body)}); }
};

struct Failure : std::runtime_error {
	using std::runtime_error::runtime_error;
};

struct Skip : std::runtime_error {
	using std::runtime_error::runtime_error;
};

inline int& checkCount()
{
	static int count = 0;
	return count;
}

} // namespace bfmetest

#define BFME_CAT2(a, b) a##b
#define BFME_CAT(a, b) BFME_CAT2(a, b)

#define TEST(name)                                                                   \
	static void BFME_CAT(test_, name)();                                             \
	static bfmetest::Registrar BFME_CAT(registrar_, name)(#name, &BFME_CAT(test_, name)); \
	static void BFME_CAT(test_, name)()

#define REQUIRE(cond)                                                                                     \
	do {                                                                                                  \
		++bfmetest::checkCount();                                                                         \
		if (!(cond)) {                                                                                    \
			std::ostringstream bfme_os_;                                                                  \
			bfme_os_ << __FILE__ << ":" << __LINE__ << ": REQUIRE(" #cond ") failed";                     \
			throw bfmetest::Failure(bfme_os_.str());                                                      \
		}                                                                                                 \
	} while (0)

#define REQUIRE_EQ(a, b)                                                                                  \
	do {                                                                                                  \
		++bfmetest::checkCount();                                                                         \
		const auto bfme_a_ = (a);  /* by value: (a) may refer into a temporary */                                                                        \
		const auto bfme_b_ = (b);                                                                        \
		if (!(bfme_a_ == bfme_b_)) {                                                                      \
			std::ostringstream bfme_os_;                                                                  \
			bfme_os_ << __FILE__ << ":" << __LINE__ << ": REQUIRE_EQ(" #a ", " #b ") failed: '" << bfme_a_ \
			         << "' != '" << bfme_b_ << "'";                                                       \
			throw bfmetest::Failure(bfme_os_.str());                                                      \
		}                                                                                                 \
	} while (0)

#define SKIP(msg) throw bfmetest::Skip(msg)
