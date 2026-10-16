// BFME: ADR-0003 §2.3. Which thread may touch the logic FP state.
//
// On macOS ThreadClass returns 0. On Linux and Android it returns gettid.
// The OS tid (gettid / GetCurrentThreadId) is written into the log so it
// matches logcat and a tombstone.

#ifndef _WIN32
#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#endif
#endif

#include "fp.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif

#if defined(__ANDROID__)
#include <pthread.h>
#endif

namespace fp {
namespace {

std::mutex g_mu;
std::thread::id g_bound;
bool g_boundValid = false;
bool g_unboundLogged = false;
bool g_mismatchLogged = false;
bool g_rebindLogged = false;
bool g_nameWarned = false;

Stats g_stats = {};

// Debug (RTS_DEBUG or a non-NDEBUG build) and CI: fatal.
// Release: one log line.
bool threadGateIsFatal()
{
#if defined(BFME_FP_CI) || defined(RTS_DEBUG) || !defined(NDEBUG)
	return true;
#else
	return false;
#endif
}

// Repeated bindLogicThread() from another thread is fatal in debug only.
bool rebindIsFatal()
{
#if defined(RTS_DEBUG) || !defined(NDEBUG)
	return true;
#else
	return false;
#endif
}

Mode defaultMode()
{
#if defined(BFME_FP_CI) || defined(RTS_DEBUG) || !defined(NDEBUG)
	return Mode::Fatal;
#else
	return Mode::Log;
#endif
}

std::atomic<Mode> g_mode{defaultMode()};

Mode currentMode()
{
	return g_mode.load(std::memory_order_relaxed);
}

void storeMode(Mode mode)
{
	g_mode.store(mode, std::memory_order_relaxed);
}

unsigned long currentOsTid()
{
#if defined(_WIN32)
	return static_cast<unsigned long>(GetCurrentThreadId());
#elif defined(__linux__)
	return static_cast<unsigned long>(gettid());
#else
	return 0;
#endif
}

std::string idText(std::thread::id id)
{
	std::ostringstream os;
	os << id;
	return os.str();
}

const char* whereText(const char* where)
{
	return (where != nullptr && where[0] != '\0') ? where : "?";
}

[[noreturn]] void fatalLine(const std::string& line)
{
	std::fprintf(stderr, "%s\n", line.c_str());
	std::fflush(stderr);
	std::abort();
}

void logOnce(bool& logged, const std::string& line)
{
	if (logged)
		return;
	logged = true;
	std::fprintf(stderr, "%s\n", line.c_str());
	std::fflush(stderr);
}

bool threadNameWarns(const char* name, bool android, bool debugBuild)
{
	if (!android || !debugBuild)
		return false;
	return name == nullptr || std::strcmp(name, "SDLThread") != 0;
}

// Caller holds g_mu. Warning only: never fatal. Android debug builds only.
void noteThreadNameLocked(const char* name, bool android, bool debugBuild)
{
	if (!threadNameWarns(name, android, debugBuild))
		return;
	if (g_nameWarned)
		return;
	g_nameWarned = true;
	std::fprintf(stderr,
		"fp: warning: thread name \"%s\" is not SDLThread (tid=%lu); not fatal\n",
		name != nullptr ? name : "",
		currentOsTid());
	std::fflush(stderr);
}

void noteBoundThreadNameLocked()
{
#if defined(__ANDROID__) && !defined(NDEBUG)
	char name[16] = {};
	if (pthread_getname_np(pthread_self(), name, sizeof name) != 0)
		name[0] = '\0';
	noteThreadNameLocked(name, true, true);
#else
	// Not Android debug: the name check is a no-op. Still call it so the
	// policy function is part of every build, not only the Android one.
	noteThreadNameLocked(nullptr, false, false);
#endif
}

std::string unboundLine(const char* where, std::thread::id self)
{
	std::ostringstream os;
	os << "fp: logic thread is not bound at " << whereText(where)
	   << " (tid=" << currentOsTid() << " id=" << idText(self) << ")";
	return os.str();
}

std::string mismatchLine(const char* where, std::thread::id self)
{
	std::ostringstream os;
	os << "fp: wrong thread at " << whereText(where)
	   << " (tid=" << currentOsTid() << " id=" << idText(self)
	   << " bound=" << idText(g_bound) << ")";
	return os.str();
}

} // namespace

void bindLogicThread()
{
	const std::thread::id self = std::this_thread::get_id();
	std::lock_guard<std::mutex> lock(g_mu);
	if (g_boundValid) {
		if (self == g_bound)
			return;
		std::ostringstream os;
		os << "fp: bindLogicThread() from another thread (tid=" << currentOsTid()
		   << " id=" << idText(self) << " bound=" << idText(g_bound) << ")";
		if (rebindIsFatal())
			fatalLine(os.str());
		logOnce(g_rebindLogged, os.str());
		return;
	}
	g_bound = self;
	g_boundValid = true;
	noteBoundThreadNameLocked();
}

bool ensureLogicThread(const char* where)
{
	const std::thread::id self = std::this_thread::get_id();
	std::lock_guard<std::mutex> lock(g_mu);
	if (!g_boundValid) {
		const std::string line = unboundLine(where, self);
		if (threadGateIsFatal())
			fatalLine(line);
		logOnce(g_unboundLogged, line + "; binding this thread");
		g_bound = self;
		g_boundValid = true;
		noteBoundThreadNameLocked();
		return true;
	}
	if (self != g_bound) {
		const std::string line = mismatchLine(where, self);
		if (threadGateIsFatal())
			fatalLine(line);
		logOnce(g_mismatchLogged, line);
		return false;
	}
	return true;
}

void checkAndReset(const char* where)
{
	if (!ensureLogicThread(where))
		return;
	// Counters are plain integers. ensureLogicThread has already rejected
	// every thread except the bound one, so only that thread reaches here.
	if (currentMode() != Mode::Off) {
		const State value = read();
		if (!isExpected(value)) {
			g_stats.tickStartDeviations += 1u;
			g_stats.lastWhere = whereText(where);
			g_stats.lastValue = value;
			std::fprintf(stderr, "fp: FP state deviation at %s\n", whereText(where));
			std::fflush(stderr);
		}
	}
	reset();
}

void assertExpected(const char* where)
{
	if (!ensureLogicThread(where))
		return;
	if (currentMode() == Mode::Off)
		return;
	const State value = read();
	if (isExpected(value))
		return;
	g_stats.tickEndFailures += 1u;
	g_stats.lastWhere = whereText(where);
	g_stats.lastValue = value;
	std::fprintf(stderr, "fp: FP state not expected at %s\n", whereText(where));
	std::fflush(stderr);
	if (currentMode() == Mode::Fatal)
		std::abort();
}

void recordGuardRestore(const char* where, const State& value)
{
	if (!ensureLogicThread(where))
		return;
	g_stats.guardRestores += 1u;
	g_stats.lastWhere = whereText(where);
	g_stats.lastValue = value;
	std::fprintf(stderr, "fp: Guard restored FP state at %s\n", whereText(where));
	std::fflush(stderr);
}

void setMode(Mode mode)
{
	storeMode(mode);
}

const Stats& stats()
{
	return g_stats;
}

void resetStats()
{
	g_stats = {};
}

#ifdef BFME_FP_THREAD_TEST

void debugResetForTests()
{
	std::lock_guard<std::mutex> lock(g_mu);
	g_bound = std::thread::id();
	g_boundValid = false;
	g_unboundLogged = false;
	g_mismatchLogged = false;
	g_rebindLogged = false;
	g_nameWarned = false;
	g_stats = {};
	storeMode(defaultMode());
}

bool testThreadGateIsFatal()
{
	return threadGateIsFatal();
}

bool testRebindIsFatal()
{
	return rebindIsFatal();
}

void testNoteThreadName(const char* name, bool android, bool debugBuild)
{
	std::lock_guard<std::mutex> lock(g_mu);
	noteThreadNameLocked(name, android, debugBuild);
}

bool testThreadNameWarns(const char* name, bool android, bool debugBuild)
{
	return threadNameWarns(name, android, debugBuild);
}

#endif

} // namespace fp
