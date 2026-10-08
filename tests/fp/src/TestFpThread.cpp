// BFME: ADR-0003 §2.3. Binding, a second bind from another thread, a check
// before bindLogicThread(), and a call from the wrong thread.
#define BFME_FP_THREAD_TEST 1

#include "fp.h"

#include "TestHarness.h"

#include <atomic>
#include <cstdlib>
#include <cstring>
#include <fenv.h>
#include <string>
#include <thread>
#include <unistd.h>
#include <sys/wait.h>

#if defined(__i386__) || defined(__x86_64__)
#include <xmmintrin.h>
#endif

namespace {

int countOf(const std::string& haystack, const char* needle)
{
	int n = 0;
	const std::size_t len = std::strlen(needle);
	for (std::size_t p = 0; (p = haystack.find(needle, p)) != std::string::npos; p += len)
		++n;
	return n;
}

unsigned long tidIn(const std::string& log)
{
	const std::size_t p = log.find("tid=");
	if (p == std::string::npos)
		return 0;
	return std::strtoul(log.c_str() + p + 4, nullptr, 10);
}

struct Capture {
	int fds[2] = {-1, -1};
	int saved = -1;
	bool active = false;

	Capture()
	{
		REQUIRE(::pipe(fds) == 0);
		saved = ::dup(STDERR_FILENO);
		REQUIRE(saved >= 0);
		REQUIRE(::dup2(fds[1], STDERR_FILENO) >= 0);
		active = true;
	}

	std::string stop()
	{
		std::fflush(stderr);
		REQUIRE(::dup2(saved, STDERR_FILENO) >= 0);
		::close(fds[1]);
		fds[1] = -1;
		std::string out;
		char buf[1024];
		ssize_t n = 0;
		while ((n = ::read(fds[0], buf, sizeof buf)) > 0)
			out.append(buf, static_cast<std::size_t>(n));
		::close(fds[0]);
		fds[0] = -1;
		::close(saved);
		saved = -1;
		active = false;
		return out;
	}

	~Capture()
	{
		if (!active)
			return;
		std::fflush(stderr);
		::dup2(saved, STDERR_FILENO);
		if (fds[1] >= 0)
			::close(fds[1]);
		if (fds[0] >= 0)
			::close(fds[0]);
		if (saved >= 0)
			::close(saved);
	}
};

struct ChildResult {
	bool aborted = false;
	int exitCode = -1;
	std::string log;
};

template <typename Fn>
ChildResult inChild(Fn&& fn)
{
	int fds[2];
	REQUIRE(::pipe(fds) == 0);
	const pid_t pid = ::fork();
	REQUIRE(pid >= 0);
	if (pid == 0) {
		::close(fds[0]);
		::dup2(fds[1], STDERR_FILENO);
		fn();
		std::fflush(stderr);
		::_exit(0);
	}
	::close(fds[1]);
	ChildResult result;
	char buf[1024];
	ssize_t n = 0;
	while ((n = ::read(fds[0], buf, sizeof buf)) > 0)
		result.log.append(buf, static_cast<std::size_t>(n));
	::close(fds[0]);
	int status = 0;
	REQUIRE(::waitpid(pid, &status, 0) == pid);
	if (WIFSIGNALED(status)) {
		result.aborted = WTERMSIG(status) == SIGABRT;
	} else if (WIFEXITED(status)) {
		result.exitCode = WEXITSTATUS(status);
	}
	return result;
}

void requireOsTid(const std::string& log)
{
	REQUIRE(log.find("tid=") != std::string::npos);
	REQUIRE(tidIn(log) > 0);
}

} // namespace

TEST(bind_then_same_thread_checks_pass)
{
	fp::debugResetForTests();
	fp::bindLogicThread();
	fp::bindLogicThread();
	const int resets = fp::testResetCallCount();
	REQUIRE(fp::ensureLogicThread("GameLogic::init") == true);
	REQUIRE(fp::ensureLogicThread("setFPMode") == true);
	fp::checkAndReset("tick-start");
	fp::assertExpected("tick-end");
	fp::assertExpected("getCRC");
	REQUIRE(fp::testResetCallCount() == resets + 1);
}

TEST(assert_expected_does_not_reset)
{
	fp::debugResetForTests();
	fp::bindLogicThread();
	const int resets = fp::testResetCallCount();
	fp::assertExpected("tick-end");
	fp::assertExpected("getCRC");
	REQUIRE(fp::testResetCallCount() == resets);
}

TEST(check_without_prior_bind)
{
	fp::debugResetForTests();
	if (fp::testThreadGateIsFatal()) {
		const char* sites[] = {"checkAndReset", "assertExpected", "setFPMode", "GameLogic::init", "getCRC"};
		for (const char* site : sites) {
			fp::debugResetForTests();
			const ChildResult child = inChild([site] {
				if (std::strcmp(site, "checkAndReset") == 0)
					fp::checkAndReset(site);
				else if (std::strcmp(site, "assertExpected") == 0 || std::strcmp(site, "getCRC") == 0)
					fp::assertExpected(site);
				else
					fp::ensureLogicThread(site);
			});
			REQUIRE(child.aborted);
			REQUIRE(countOf(child.log, "fp: logic thread is not bound") == 1);
			REQUIRE(child.log.find(site) != std::string::npos);
			REQUIRE(child.log.find("binding this thread") == std::string::npos);
			requireOsTid(child.log);
		}
		return;
	}

	Capture cap;
	fp::checkAndReset("checkAndReset");
	fp::assertExpected("assertExpected");
	REQUIRE(fp::ensureLogicThread("setFPMode") == true);
	REQUIRE(fp::ensureLogicThread("GameLogic::init") == true);
	fp::assertExpected("getCRC");
	const std::string log = cap.stop();
	REQUIRE(countOf(log, "fp: logic thread is not bound") == 1);
	REQUIRE(log.find("binding this thread") != std::string::npos);
	requireOsTid(log);
	const int resets = fp::testResetCallCount();
	fp::checkAndReset("tick-start");
	REQUIRE(fp::testResetCallCount() == resets + 1);
}

TEST(call_from_wrong_thread)
{
	fp::debugResetForTests();
	if (fp::testThreadGateIsFatal()) {
		const char* sites[] = {"checkAndReset", "assertExpected", "setFPMode", "GameLogic::init", "getCRC"};
		for (const char* site : sites) {
			fp::debugResetForTests();
			const ChildResult child = inChild([site] {
				fp::bindLogicThread();
				std::thread worker([site] {
					if (std::strcmp(site, "checkAndReset") == 0)
						fp::checkAndReset(site);
					else if (std::strcmp(site, "assertExpected") == 0 || std::strcmp(site, "getCRC") == 0)
						fp::assertExpected(site);
					else
						fp::ensureLogicThread(site);
				});
				worker.join();
			});
			REQUIRE(child.aborted);
			REQUIRE(countOf(child.log, "fp: wrong thread") == 1);
			REQUIRE(child.log.find(site) != std::string::npos);
			requireOsTid(child.log);
		}
		return;
	}

	fp::bindLogicThread();
	const int resets = fp::testResetCallCount();
	std::atomic<int> touched{0};
	Capture cap;
	std::thread worker([&touched] {
		if (fp::ensureLogicThread("setFPMode")) {
			fp::reset();
			touched = 1;
		}
		fp::checkAndReset("checkAndReset");
		fp::assertExpected("getCRC");
		fp::ensureLogicThread("GameLogic::init");
	});
	worker.join();
	const std::string log = cap.stop();
	REQUIRE(touched.load() == 0);
	REQUIRE(fp::testResetCallCount() == resets);
	REQUIRE(countOf(log, "fp: wrong thread") == 1);
	requireOsTid(log);
	REQUIRE(fp::ensureLogicThread("setFPMode") == true);
}

TEST(rebind_from_other_thread)
{
	fp::debugResetForTests();
	if (fp::testRebindIsFatal()) {
		const ChildResult child = inChild([] {
			fp::bindLogicThread();
			std::thread worker([] { fp::bindLogicThread(); });
			worker.join();
		});
		REQUIRE(child.aborted);
		REQUIRE(countOf(child.log, "fp: bindLogicThread() from another thread") == 1);
		requireOsTid(child.log);
		return;
	}

	fp::bindLogicThread();
	Capture cap;
	std::thread worker([] { fp::bindLogicThread(); });
	worker.join();
	std::thread workerAgain([] { fp::bindLogicThread(); });
	workerAgain.join();
	const std::string log = cap.stop();
	REQUIRE(countOf(log, "fp: bindLogicThread() from another thread") == 1);
	requireOsTid(log);
	REQUIRE(fp::ensureLogicThread("setFPMode") == true);
}

TEST(sdl_thread_name_is_warning_only)
{
	fp::debugResetForTests();
	REQUIRE(fp::testThreadNameWarns("SDLThread", true, true) == false);
	REQUIRE(fp::testThreadNameWarns("RenderThread", true, true) == true);
	REQUIRE(fp::testThreadNameWarns(nullptr, true, true) == true);
	REQUIRE(fp::testThreadNameWarns("RenderThread", false, true) == false);
	REQUIRE(fp::testThreadNameWarns("RenderThread", true, false) == false);

	Capture cap;
	fp::testNoteThreadName("SDLThread", true, true);
	fp::testNoteThreadName("RenderThread", true, true);
	fp::testNoteThreadName("RenderThread", true, true);
	const std::string log = cap.stop();
	REQUIRE(countOf(log, "fp: warning:") == 1);
	REQUIRE(log.find("not fatal") != std::string::npos);
	REQUIRE(log.find("RenderThread") != std::string::npos);
	requireOsTid(log);
}

TEST(check_and_reset_resets_once_per_call)
{
	fp::debugResetForTests();
	fp::bindLogicThread();
	const int resets = fp::testResetCallCount();
	fp::checkAndReset("tick-start");
	fp::checkAndReset("tick-start");
	REQUIRE(fp::testResetCallCount() == resets + 2);
}

TEST(reset_restores_tonearest)
{
	REQUIRE(fesetround(FE_UPWARD) == 0);
	REQUIRE(fegetround() == FE_UPWARD);
#if defined(__i386__) || defined(__x86_64__)
	unsigned short controlWord = 0;
	__asm__ __volatile__("fnstcw %0" : "=m"(controlWord));
	controlWord = static_cast<unsigned short>(controlWord | 0x0F00u);
	__asm__ __volatile__("fldcw %0" : : "m"(controlWord));

	unsigned int mxcsr = static_cast<unsigned int>(_mm_getcsr());
	mxcsr = (mxcsr & ~static_cast<unsigned int>(_MM_ROUND_MASK)) | static_cast<unsigned int>(_MM_ROUND_UP);
	_mm_setcsr(mxcsr);
#endif
	fp::reset();
	REQUIRE(fegetround() == FE_TONEAREST);
#if defined(__i386__) || defined(__x86_64__)
	controlWord = 0;
	__asm__ __volatile__("fnstcw %0" : "=m"(controlWord));
	REQUIRE((controlWord & 0x0F00u) == 0u);
	mxcsr = static_cast<unsigned int>(_mm_getcsr());
	REQUIRE((mxcsr & static_cast<unsigned int>(_MM_ROUND_MASK)) == static_cast<unsigned int>(_MM_ROUND_NEAREST));
#endif
}
