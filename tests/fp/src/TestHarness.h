// BFME: ADR-0003 §2.3 thread-gate tests. No game link, no third-party framework.
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
	Registrar(const char* name, std::function<void()> body)
	{
		registry().push_back({name, std::move(body)});
	}
};

struct Failure : std::runtime_error {
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

#define TEST(name)                                                                                     \
	static void BFME_CAT(test_, name)();                                                               \
	static bfmetest::Registrar BFME_CAT(registrar_, name)(#name, &BFME_CAT(test_, name));              \
	static void BFME_CAT(test_, name)()

#define REQUIRE(cond)                                                                                  \
	do {                                                                                               \
		++bfmetest::checkCount();                                                                      \
		if (!(cond)) {                                                                                 \
			std::ostringstream bfme_os_;                                                               \
			bfme_os_ << __FILE__ << ":" << __LINE__ << ": REQUIRE(" #cond ") failed";                  \
			throw bfmetest::Failure(bfme_os_.str());                                                   \
		}                                                                                              \
	} while (0)
