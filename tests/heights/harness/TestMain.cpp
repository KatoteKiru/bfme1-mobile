// BFME1 Mobile — tests (copy of tests/vfs/<file> from E1-21, kept standalone so E1-25 does not depend on E1-21)
// SPDX-License-Identifier: GPL-3.0-or-later
#include "TestHarness.h"

#include <cstring>

namespace bfmetest {
std::vector<TestCase>& registry()
{
	static std::vector<TestCase> tests;
	return tests;
}
} // namespace bfmetest

int main(int argc, char** argv)
{
	using namespace bfmetest;
	if (argc > 1 && std::strcmp(argv[1], "--list") == 0) {
		for (const TestCase& t : registry()) {
			std::printf("%s\n", t.name.c_str());
		}
		return 0;
	}
	int failed = 0, passed = 0, skipped = 0;
	for (const TestCase& t : registry()) {
		if (argc > 1 && t.name != argv[1]) {
			continue;
		}
		const int before = checkCount();
		try {
			t.body();
			++passed;
			std::printf("[ PASS ] %s (%d checks)\n", t.name.c_str(), checkCount() - before);
		} catch (const Skip& s) {
			++skipped;
			std::printf("[ SKIP ] %s: %s\n", t.name.c_str(), s.what());
		} catch (const std::exception& e) {
			++failed;
			std::printf("[ FAIL ] %s: %s\n", t.name.c_str(), e.what());
		}
		std::fflush(stdout);
	}
	std::printf("passed %d, failed %d, skipped %d\n", passed, failed, skipped);
	if (passed + failed + skipped == 0) {
		std::printf("no test named '%s'\n", argc > 1 ? argv[1] : "");
		return 2;
	}
	if (failed > 0) {
		return 1;
	}
	return (skipped > 0 && passed == 0) ? 77 : 0;
}
