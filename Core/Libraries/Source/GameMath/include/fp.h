#pragma once

// BFME: ADR-0003 §2.3. Logic-thread floating-point gate.
//
// The logic thread calls bindLogicThread() once, first thing in GameMain(),
// before TheGameEngine->init(). Later checks compare std::thread::id.
// gettid() / GetCurrentThreadId() are for the log line only: ThreadClass::
// _Get_Current_Thread_ID() is not used, because on _UNIX it returns 0.
//
// Platform replaces fp_platform_stub.cpp with fp_arm64.cpp, fp_x86_64.cpp,
// and fp_msvc.cpp. Until then reset() is the previous non-VC6 setFPMode()
// body, and read() / isExpected() stay trivial (zero state, always true).

#include <cstdint>

namespace fp {

struct State {
#if defined(__aarch64__)
	std::uint64_t fpcr;
#else
	std::uint32_t mxcsr;
	std::uint16_t x87cw;
#endif
};

// Record the calling thread as the logic thread.
// A second call from the same thread does nothing.
// A second call from another thread is fatal in a debug build.
// It is not fatal in release or in a CI build that is not a debug build:
// one log line, and the original thread stays bound.
void bindLogicThread();

// Thread gate used by GameLogic::init, setFPMode, checkAndReset, assertExpected,
// and getCRC (via assertExpected).
//
// Not yet bound: debug and CI abort. Release logs once and binds the caller
// (never silently).
// Wrong thread: debug and CI abort. Release logs once and returns false.
// The caller must not touch FP registers when this returns false.
// True means the caller is the bound logic thread.
bool ensureLogicThread(const char* where);

State read();
bool isExpected(const State& state);
void reset();

// Read, and only then reset. A deviation is counted and logged. Off skips the
// read. The thread gate still runs. Does not reset when the caller is the
// wrong thread (release): that would write the other thread's FP state.
void checkAndReset(const char* where);

// FP-state failure follows setMode: Off ignores it, Log records it, Fatal
// aborts. Debug and CI default to Fatal, release to Log. The thread-id
// mismatch above does not follow setMode; that policy is fixed.
void assertExpected(const char* where);

// Platform's Guard calls this after restoring a drifted state. The counters
// are not atomic: only the bound logic thread updates them.
void recordGuardRestore(const char* where, const State& value);

class Guard {
public:
	explicit Guard(const char* where);
	~Guard();

	Guard(const Guard&) = delete;
	Guard& operator=(const Guard&) = delete;

private:
	State saved_;
	const char* where_;
};

enum class Mode { Off, Log, Fatal };

void setMode(Mode mode);

struct Stats {
	std::uint32_t tickStartDeviations;
	std::uint32_t tickEndFailures;
	std::uint32_t guardRestores;
	const char* lastWhere;
	State lastValue;
	std::uint32_t lastFrame;
};

// Stats limitation: stats() and resetStats() do not check the thread.
// g_stats is not atomic.
const Stats& stats();
void resetStats();

#ifdef BFME_FP_THREAD_TEST
void debugResetForTests();
bool testThreadGateIsFatal();
bool testRebindIsFatal();
int testResetCallCount();
void testNoteThreadName(const char* name, bool android, bool debugBuild);
bool testThreadNameWarns(const char* name, bool android, bool debugBuild);
#endif

} // namespace fp
