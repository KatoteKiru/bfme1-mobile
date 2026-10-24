// BFME: E1-43 map-load trace and stall watchdog (Pixel 8 Pro "second black screen").
//
// The E1-42 Pixel run ends with "[terrainload] end HeightMapRenderObjClass::initHeightData
// arg=2690269" and then 80+ s of silence: process alive, audio playing, threads sleeping, no
// frame on screen, no stack (no root, simpleperf gives 0 samples). This header adds:
//
//  - "[mapload] begin/end <step> arg=N  X ms" lines for the steps between terrain init and the
//    first frame (GameLogic::startNewGame, W3DTerrainVisual::load, the first W3DDisplay::draw
//    calls), "[frame] first present" lines, and a rate-limited "[frame] render skipped" line
//    when W3DDisplay::draw runs but does not render.
//  - on Linux/Android only, a watchdog thread (plain std::thread, NOT ThreadClass, which is a
//    no-op on _UNIX). Every trace call and every present bumps a progress counter. If the
//    counter does not move for BFME_WATCHDOG_SECS (default 10) seconds, the watchdog logs the
//    last step and the draw/present/logic counters, then sends a signal to the game thread
//    (tgkill). The handler only records return addresses (frame-pointer walk, then
//    _Unwind_Backtrace); the watchdog thread formats them with dladdr and writes with write(2)
//    to fd 2 (generals-stderr.log on Android). Repeats every period while the stall lasts.
//
// Environment: BFME_WATCHDOG=0 disables the watchdog (trace lines stay), BFME_WATCHDOG_SECS=N
// sets the period, BFME_WATCHDOG_UNWIND=0 skips _Unwind_Backtrace in the handler (frame-pointer
// walk only). Header-only so no CMake change is needed; all state is C++17 inline variables.
#pragma once

#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#if defined(__linux__) || defined(__ANDROID__)
#define BFME_MAPLOAD_WATCHDOG 1
#include <csignal>
#include <cxxabi.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <mutex>
#include <pthread.h>
#include <sys/syscall.h>
#include <thread>
#include <ucontext.h>
#include <unistd.h>
#include <unwind.h>
#else
#define BFME_MAPLOAD_WATCHDOG 0
#endif

namespace MapLoadTrace {

inline double nowMs()
{
	using namespace std::chrono;
	return duration<double, std::milli>(steady_clock::now().time_since_epoch()).count();
}

// Shared state. Written by the game thread, read by the watchdog thread.
inline std::atomic<unsigned long> g_progress{0};
inline std::atomic<unsigned long> g_draws{0};
inline std::atomic<unsigned long> g_presents{0};
inline std::atomic<unsigned long> g_logicFrames{0};
inline std::atomic<const char *> g_lastStep{"(none)"};
inline std::atomic<int> g_lastArg{-1};
inline std::atomic<bool> g_lastStepOpen{false};
inline std::atomic<double> g_lastStepAt{0.0};
inline std::atomic<const char *> g_lastSkip{nullptr};
inline double g_firstTraceMs = 0.0;	// game thread only

#if BFME_MAPLOAD_WATCHDOG

namespace Watchdog {

enum { MAX_FRAMES = 48 };

inline std::once_flag g_startOnce;
inline pid_t g_gameTid = 0;
inline uintptr_t g_stackLo = 0;
inline uintptr_t g_stackHi = 0;
inline int g_signal = 0;
inline bool g_useUnwind = true;

// Filled by the signal handler, read by the watchdog thread after g_captureDone changes.
inline uintptr_t g_fpFrames[MAX_FRAMES];
inline int g_fpCount = 0;
inline uintptr_t g_uwFrames[MAX_FRAMES];
inline int g_uwCount = 0;
inline uintptr_t g_regPc = 0, g_regLr = 0, g_regSp = 0, g_regFp = 0;
inline std::atomic<int> g_captureStage{0};	// 0 idle, 1 handler entered, 2 fp walk done, 3 all done

inline void writeAll(const char *buf, size_t len)
{
	while (len > 0) {
		ssize_t n = ::write(2, buf, len);
		if (n <= 0)
			return;
		buf += n;
		len -= (size_t)n;
	}
}

// Formats into a stack buffer and writes straight to fd 2: does not take the stderr FILE
// lock, which the stalled game thread might be holding.
inline void say(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
inline void say(const char *fmt, ...)
{
	char buf[768];
	va_list ap;
	va_start(ap, fmt);
	int n = vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	if (n <= 0)
		return;
	if ((size_t)n >= sizeof(buf))
		n = (int)sizeof(buf) - 1;
	writeAll(buf, (size_t)n);
}

struct UnwindState {
	uintptr_t *frames;
	int count;
	int max;
};

inline _Unwind_Reason_Code unwindCallback(struct _Unwind_Context *ctx, void *arg)
{
	UnwindState *st = static_cast<UnwindState *>(arg);
	uintptr_t pc = (uintptr_t)_Unwind_GetIP(ctx);
	if (pc != 0) {
		if (st->count >= st->max)
			return _URC_END_OF_STACK;
		st->frames[st->count++] = pc;
	}
	return _URC_NO_REASON;
}

// Runs on the game thread. Only stores numbers: no allocation, no locks, no stdio. The
// frame-pointer walk is bounded by the thread's stack range recorded at start.
inline void signalHandler(int, siginfo_t *, void *ucv)
{
	if ((pid_t)syscall(SYS_gettid) != g_gameTid)
		return;
	g_captureStage.store(1, std::memory_order_release);

	g_regPc = g_regLr = g_regSp = g_regFp = 0;
	g_fpCount = 0;
	g_uwCount = 0;

	const ucontext_t *uc = static_cast<const ucontext_t *>(ucv);
#if defined(__aarch64__)
	if (uc) {
		g_regPc = (uintptr_t)uc->uc_mcontext.pc;
		g_regSp = (uintptr_t)uc->uc_mcontext.sp;
		g_regFp = (uintptr_t)uc->uc_mcontext.regs[29];
		g_regLr = (uintptr_t)uc->uc_mcontext.regs[30];
	}
#elif defined(__x86_64__) && defined(REG_RIP)
	if (uc) {
		g_regPc = (uintptr_t)uc->uc_mcontext.gregs[REG_RIP];
		g_regSp = (uintptr_t)uc->uc_mcontext.gregs[REG_RSP];
		g_regFp = (uintptr_t)uc->uc_mcontext.gregs[REG_RBP];
	}
#else
	(void)uc;
#endif

	// Frame-pointer chain of the interrupted code: [fp] = caller fp, [fp + 8] = return address
	// (AArch64 and x86-64 alike). Lock-free, so it is done first.
	if (g_regPc)
		g_fpFrames[g_fpCount++] = g_regPc;
	if (g_regLr && g_fpCount < MAX_FRAMES)
		g_fpFrames[g_fpCount++] = g_regLr;
	uintptr_t fp = g_regFp;
	const uintptr_t lo = g_stackLo ? g_stackLo : g_regSp;
	const uintptr_t hi = g_stackHi;
	while (g_fpCount < MAX_FRAMES && fp != 0 && hi != 0 && (fp & 0x7) == 0 && fp >= lo
		&& fp + 2 * sizeof(uintptr_t) <= hi) {
		const uintptr_t *frame = reinterpret_cast<const uintptr_t *>(fp);
		uintptr_t nextFp = frame[0];
		uintptr_t ret = frame[1];
		if (ret == 0)
			break;
		g_fpFrames[g_fpCount++] = ret;
		if (nextFp <= fp)
			break;
		fp = nextFp;
	}
	g_captureStage.store(2, std::memory_order_release);

	// _Unwind_Backtrace starts in this handler and has to step through the signal frame; it
	// is not formally async-signal-safe (it may look up unwind tables under the loader lock).
	// The watchdog thread only waits a bounded time for it, and BFME_WATCHDOG_UNWIND=0
	// turns it off.
	if (g_useUnwind) {
		UnwindState st = { g_uwFrames, 0, MAX_FRAMES };
		_Unwind_Backtrace(unwindCallback, &st);
		g_uwCount = st.count;
	}
	g_captureStage.store(3, std::memory_order_release);
}

inline bool handlerFree(int sig)
{
	struct sigaction old;
	memset(&old, 0, sizeof(old));
	if (sigaction(sig, nullptr, &old) != 0)
		return false;
	if (old.sa_flags & SA_SIGINFO)
		return false;
	return old.sa_handler == SIG_DFL || old.sa_handler == SIG_IGN;
}

// SIGUSR2 first: ART uses SIGQUIT/SIGUSR1 (SignalCatcher) and bionic reserves the low
// real-time signals (timers, debuggerd, backtraces, heapprofd); SDL3 only hooks SIGINT/SIGTERM.
// If something already owns SIGUSR2, fall back to a high real-time signal.
inline int pickSignal()
{
	if (handlerFree(SIGUSR2))
		return SIGUSR2;
#ifdef SIGRTMAX
	for (int sig = SIGRTMAX - 2; sig > SIGRTMIN + 8; --sig) {
		if (handlerFree(sig))
			return sig;
	}
#endif
	return 0;
}

inline void printFrame(const char *tag, int idx, uintptr_t pc)
{
	Dl_info info;
	memset(&info, 0, sizeof(info));
	const char *module = "?";
	uintptr_t off = pc;
	const char *sym = nullptr;
	uintptr_t symOff = 0;
	if (dladdr(reinterpret_cast<void *>(pc), &info)) {
		if (info.dli_fname) {
			module = info.dli_fname;
			const char *slash = strrchr(module, '/');
			if (slash)
				module = slash + 1;
		}
		if (info.dli_fbase)
			off = pc - (uintptr_t)info.dli_fbase;
		if (info.dli_sname && info.dli_saddr) {
			sym = info.dli_sname;
			symOff = pc - (uintptr_t)info.dli_saddr;
		}
	}
	char *demangled = nullptr;
	if (sym) {
		int status = 0;
		demangled = abi::__cxa_demangle(sym, nullptr, nullptr, &status);
		if (status != 0) {
			free(demangled);
			demangled = nullptr;
		}
	}
	if (sym)
		say("[watchdog] %s #%02d pc 0x%016lx %s+0x%lx %s+0x%lx\n", tag, idx, (unsigned long)pc, module,
			(unsigned long)off, demangled ? demangled : sym, (unsigned long)symOff);
	else
		say("[watchdog] %s #%02d pc 0x%016lx %s+0x%lx\n", tag, idx, (unsigned long)pc, module, (unsigned long)off);
	free(demangled);
}

// The kernel's view of the game thread: state letter (S/R/D), and the syscall it is blocked in
// (first field of /proc/self/task/<tid>/syscall: 98 = futex, 101 = nanosleep, 115 =
// clock_nanosleep, 73 = ppoll, 29 = ioctl on arm64). Either file may be unreadable.
inline void printThreadState()
{
	char path[96];
	char buf[512];
	snprintf(path, sizeof(path), "/proc/self/task/%d/stat", (int)g_gameTid);
	int fd = ::open(path, O_RDONLY);
	if (fd >= 0) {
		ssize_t n = ::read(fd, buf, sizeof(buf) - 1);
		::close(fd);
		if (n > 0) {
			buf[n] = 0;
			const char *rp = strrchr(buf, ')');
			say("[watchdog] game thread tid=%d state=%c\n", (int)g_gameTid, (rp && rp[1] && rp[2]) ? rp[2] : '?');
		}
	}
	snprintf(path, sizeof(path), "/proc/self/task/%d/syscall", (int)g_gameTid);
	fd = ::open(path, O_RDONLY);
	if (fd >= 0) {
		ssize_t n = ::read(fd, buf, sizeof(buf) - 1);
		::close(fd);
		if (n > 0) {
			buf[n] = 0;
			char *nl = strchr(buf, '\n');
			if (nl)
				*nl = 0;
			say("[watchdog] game thread syscall: %s\n", buf);
		}
	}
}

inline void captureGameThread()
{
	if (g_signal == 0 || g_gameTid == 0) {
		say("[watchdog] no signal available, no backtrace\n");
		return;
	}
	g_captureStage.store(0, std::memory_order_release);
	if (syscall(SYS_tgkill, (int)getpid(), (int)g_gameTid, g_signal) != 0) {
		say("[watchdog] tgkill(%d, sig %d) failed: %s\n", (int)g_gameTid, g_signal, strerror(errno));
		return;
	}
	int stage = 0;
	for (int i = 0; i < 200; ++i) {	// up to 2 s
		stage = g_captureStage.load(std::memory_order_acquire);
		if (stage == 3)
			break;
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}
	if (stage < 2) {
		say("[watchdog] handler did not run on tid %d within 2 s (stage %d); signal blocked?\n", (int)g_gameTid, stage);
		return;
	}
	say("[watchdog] regs pc=0x%lx lr=0x%lx sp=0x%lx fp=0x%lx\n", (unsigned long)g_regPc, (unsigned long)g_regLr,
		(unsigned long)g_regSp, (unsigned long)g_regFp);
	for (int i = 0; i < g_fpCount; ++i)
		printFrame("fp", i, g_fpFrames[i]);
	if (stage < 3)
		say("[watchdog] _Unwind_Backtrace did not finish within 2 s (deadlock in unwinder?); fp walk only\n");
	else
		for (int i = 0; i < g_uwCount; ++i)
			printFrame("uw", i, g_uwFrames[i]);
}

inline void run(int periodSec)
{
	unsigned long lastProgress = g_progress.load(std::memory_order_relaxed);
	double lastChange = nowMs();
	double lastReport = 0.0;
	int reportsThisStall = 0;
	for (;;) {
		std::this_thread::sleep_for(std::chrono::seconds(1));
		unsigned long p = g_progress.load(std::memory_order_relaxed);
		double now = nowMs();
		if (p != lastProgress) {
			if (reportsThisStall > 0)
				say("[watchdog] progress resumed after %.1f s\n", (now - lastChange) / 1000.0);
			lastProgress = p;
			lastChange = now;
			reportsThisStall = 0;
			continue;
		}
		double stalled = now - lastChange;
		if (stalled < periodSec * 1000.0)
			continue;
		// Full report for the first 6 periods of a stall, then one line per minute.
		double interval = reportsThisStall < 6 ? periodSec * 1000.0 : 60000.0;
		if (reportsThisStall > 0 && now - lastReport < interval)
			continue;
		lastReport = now;
		++reportsThisStall;
		const char *skip = g_lastSkip.load(std::memory_order_relaxed);
		say("[watchdog] STALL %.1f s: no progress; last step '%s' arg=%d %s %.1f s ago; draws=%lu presents=%lu logicFrames=%lu%s%s\n",
			stalled / 1000.0, g_lastStep.load(), g_lastArg.load(), g_lastStepOpen.load() ? "(still open, began)" : "(ended)",
			(now - g_lastStepAt.load()) / 1000.0, g_draws.load(), g_presents.load(), g_logicFrames.load(),
			skip ? " lastRenderSkip=" : "", skip ? skip : "");
		if (reportsThisStall <= 6) {
			printThreadState();
			captureGameThread();
			say("[watchdog] end of report #%d\n", reportsThisStall);
		}
	}
}

// Called on the game thread by the first trace call.
inline void startOnGameThread()
{
	const char *env = getenv("BFME_WATCHDOG");
	if (env && env[0] == '0') {
		fprintf(stderr, "[watchdog] disabled (BFME_WATCHDOG=0)\n");
		return;
	}
	int period = 10;
	if (const char *s = getenv("BFME_WATCHDOG_SECS")) {
		int v = atoi(s);
		if (v >= 2 && v <= 3600)
			period = v;
	}
	const char *uw = getenv("BFME_WATCHDOG_UNWIND");
	g_useUnwind = !(uw && uw[0] == '0');

	g_gameTid = (pid_t)syscall(SYS_gettid);
	pthread_attr_t attr;
	if (pthread_getattr_np(pthread_self(), &attr) == 0) {
		void *addr = nullptr;
		size_t size = 0;
		if (pthread_attr_getstack(&attr, &addr, &size) == 0 && addr) {
			g_stackLo = (uintptr_t)addr;
			g_stackHi = (uintptr_t)addr + size;
		}
		pthread_attr_destroy(&attr);
	}

	g_signal = pickSignal();
	if (g_signal) {
		struct sigaction sa;
		memset(&sa, 0, sizeof(sa));
		sa.sa_sigaction = signalHandler;
		sa.sa_flags = SA_SIGINFO | SA_RESTART;
		sigemptyset(&sa.sa_mask);
		if (sigaction(g_signal, &sa, nullptr) != 0)
			g_signal = 0;
	}

	try {
		std::thread(run, period).detach();
	} catch (...) {
		fprintf(stderr, "[watchdog] could not start thread\n");
		return;
	}
	fprintf(stderr, "[watchdog] started: period %d s, game tid %d, stack 0x%lx-0x%lx, signal %d, unwind %d\n", period,
		(int)g_gameTid, (unsigned long)g_stackLo, (unsigned long)g_stackHi, g_signal, (int)g_useUnwind);
	fflush(stderr);
}

} // namespace Watchdog

#endif // BFME_MAPLOAD_WATCHDOG

inline void ensureStarted()
{
	if (g_firstTraceMs == 0.0)
		g_firstTraceMs = nowMs();
#if BFME_MAPLOAD_WATCHDOG
	std::call_once(Watchdog::g_startOnce, Watchdog::startOnGameThread);
#endif
}

inline void bump(const char *step, int arg, bool open)
{
	g_lastStep.store(step, std::memory_order_relaxed);
	g_lastArg.store(arg, std::memory_order_relaxed);
	g_lastStepOpen.store(open, std::memory_order_relaxed);
	g_lastStepAt.store(nowMs(), std::memory_order_relaxed);
	g_progress.fetch_add(1, std::memory_order_relaxed);
}

// Scoped step: "begin" on entry, "end ... X ms" on exit. Names must be string literals.
// enabled=false makes it a no-op (for per-frame code that is traced only for a few frames).
class Step {
public:
	explicit Step(const char *name, int arg = -1, bool enabled = true) : m_name(name), m_arg(arg), m_on(enabled)
	{
		if (!m_on)
			return;
		ensureStarted();
		m_t0 = nowMs();
		bump(m_name, m_arg, true);
		fprintf(stderr, "[mapload] begin %s arg=%d\n", m_name, m_arg);
		fflush(stderr);
	}
	~Step()
	{
		if (!m_on)
			return;
		bump(m_name, m_arg, false);
		fprintf(stderr, "[mapload] end   %s arg=%d %.1f ms\n", m_name, m_arg, nowMs() - m_t0);
		fflush(stderr);
	}
	Step(const Step &) = delete;
	Step &operator=(const Step &) = delete;
private:
	const char *m_name;
	int m_arg;
	bool m_on;
	double m_t0 = 0.0;
};

// Sequential phases inside one long function: phase("b") ends the open phase "a" (logging its
// time) and begins "b". endPhase() closes the open one. Game thread only.
inline const char *g_phaseName = nullptr;
inline int g_phaseArg = -1;
inline double g_phaseT0 = 0.0;

inline void endPhase()
{
	if (!g_phaseName)
		return;
	bump(g_phaseName, g_phaseArg, false);
	fprintf(stderr, "[mapload] end   %s arg=%d %.1f ms\n", g_phaseName, g_phaseArg, nowMs() - g_phaseT0);
	fflush(stderr);
	g_phaseName = nullptr;
}

inline void phase(const char *name, int arg = -1)
{
	ensureStarted();
	endPhase();
	g_phaseName = name;
	g_phaseArg = arg;
	g_phaseT0 = nowMs();
	bump(name, arg, true);
	fprintf(stderr, "[mapload] begin %s arg=%d\n", name, arg);
	fflush(stderr);
}

// Progress mark without begin/end pairing (e.g. load screen percentage).
inline void mark(const char *name, int arg)
{
	ensureStarted();
	bump(name, arg, false);
	fprintf(stderr, "[mapload] mark  %s arg=%d t=%.1f ms\n", name, arg, nowMs() - g_firstTraceMs);
	fflush(stderr);
}

// Counters only (no log line, no progress): cheap enough for every frame.
inline void drawCalled() { g_draws.fetch_add(1, std::memory_order_relaxed); }
// countsAsProgress: true in headless mode, which never presents (otherwise every headless run
// would report a stall once the map is loaded).
inline void logicFrame(bool countsAsProgress)
{
	g_logicFrames.fetch_add(1, std::memory_order_relaxed);
	if (countsAsProgress)
		g_progress.fetch_add(1, std::memory_order_relaxed);
}

// A frame reached WW3D::End_Render (the swap). Counts as progress.
inline void presented(bool loadScreen)
{
	unsigned long n = g_presents.fetch_add(1, std::memory_order_relaxed) + 1;
	g_progress.fetch_add(1, std::memory_order_relaxed);
	static bool s_firstAny = false;
	static bool s_firstGame = false;
	if (!s_firstAny) {
		s_firstAny = true;
		fprintf(stderr, "[frame] first present (%s) #%lu t=%.1f ms after first [mapload] step\n",
			loadScreen ? "load screen" : "game", n, g_firstTraceMs != 0.0 ? nowMs() - g_firstTraceMs : -1.0);
		fflush(stderr);
	}
	if (!loadScreen && !s_firstGame) {
		s_firstGame = true;
		fprintf(stderr, "[frame] first game present #%lu t=%.1f ms after first [mapload] step\n", n,
			g_firstTraceMs != 0.0 ? nowMs() - g_firstTraceMs : -1.0);
		fflush(stderr);
	}
}

// W3DDisplay::draw ran but did not render. Logged on the first skip, whenever the reason
// changes, and then every 300th skip. Reason must be a string literal. Not progress.
inline void renderSkipped(const char *reason)
{
	static const char *s_lastReason = nullptr;
	static unsigned long s_count = 0;
	++s_count;
	g_lastSkip.store(reason, std::memory_order_relaxed);
	if (reason != s_lastReason || (s_count % 300) == 0) {
		fprintf(stderr, "[frame] render skipped: %s (skips=%lu draws=%lu presents=%lu)\n", reason, s_count,
			g_draws.load(std::memory_order_relaxed), g_presents.load(std::memory_order_relaxed));
		fflush(stderr);
		s_lastReason = reason;
	}
}

} // namespace MapLoadTrace
