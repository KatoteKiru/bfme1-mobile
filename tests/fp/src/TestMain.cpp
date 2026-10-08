// BFME: ADR-0003 §2.3 thread-gate tests.
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
		for (const TestCase& t : registry())
			std::printf("%s\n", t.name.c_str());
		return 0;
	}
	int failed = 0;
	int passed = 0;
	for (const TestCase& t : registry()) {
		if (argc > 1 && t.name != argv[1])
			continue;
		const int before = checkCount();
		try {
			t.body();
			++passed;
			std::printf("[ PASS ] %s (%d checks)\n", t.name.c_str(), checkCount() - before);
		} catch (const std::exception& e) {
			++failed;
			std::printf("[ FAIL ] %s: %s\n", t.name.c_str(), e.what());
		}
		std::fflush(stdout);
	}
	std::printf("passed %d, failed %d\n", passed, failed);
	if (passed + failed == 0) {
		std::printf("no test named '%s'\n", argc > 1 ? argv[1] : "");
		return 2;
	}
	return failed > 0 ? 1 : 0;
}
