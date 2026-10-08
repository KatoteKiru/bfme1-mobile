// BFME: PLATFORM REPLACES THIS FILE.
//
// Temporary fp::reset() is the previous non-VC6 setFPMode() body from
// GameLogic.cpp at e9292c9c5, so Linux, Android, and non-VC6 Windows keep
// the same register writes. Platform deletes this file when these definitions
// move to fp_arm64.cpp, fp_x86_64.cpp, and fp_msvc.cpp (duplicate symbols
// otherwise). The lasting reset is fesetenv(FE_DFL_ENV) or `msr fpcr, xzr`,
// not fesetround alone.
//
// read() and isExpected() stay trivial until that replacement: read() returns
// a zero State and isExpected() returns true, so a deviation is not reported
// yet. Only reset() writes the control registers.

#include "fp.h"

#ifndef _WIN32
#include <fenv.h>
#if defined(__SSE__) || defined(__x86_64__)
#include <xmmintrin.h>
#endif
#else
#include <float.h>
#endif

namespace fp {

#ifdef BFME_FP_THREAD_TEST
namespace {
int g_resetCalls = 0;
} // namespace

int testResetCallCount()
{
	return g_resetCalls;
}
#endif

State read()
{
	// Trivial until Platform implements the register read.
	State state = {};
	return state;
}

bool isExpected(const State&)
{
	// Trivial until Platform implements the R3 compare. True avoids a false
	// fatal on every tick while this stub is still linked.
	return true;
}

void reset()
{
#ifdef BFME_FP_THREAD_TEST
	++g_resetCalls;
#endif
#ifdef _WIN32
	// Non-VC6 Windows: same PC_24 | RC_NEAR as setFPMode() at e9292c9c5.
	_fpreset();

	unsigned int curVal = _statusfp();
	unsigned int newVal = curVal;
	newVal = (newVal & ~_MCW_RC) | (_RC_NEAR & _MCW_RC);
	newVal = (newVal & ~_MCW_PC) | (_PC_24 & _MCW_PC);

	_controlfp(newVal, _MCW_PC | _MCW_RC);
#else
	fesetenv(FE_DFL_ENV);
	feclearexcept(FE_ALL_EXCEPT);
	fesetround(FE_TONEAREST);

#if defined(__i386__) || defined(__x86_64__)
	unsigned short controlWord = 0;
	__asm__ __volatile__("fnstcw %0" : "=m"(controlWord));
	controlWord = static_cast<unsigned short>(controlWord & ~0x0F00u);
	__asm__ __volatile__("fldcw %0" : : "m"(controlWord));
#endif

#if defined(__SSE__) || defined(__x86_64__)
	unsigned int mxcsr = static_cast<unsigned int>(_mm_getcsr());
	mxcsr = (mxcsr & ~static_cast<unsigned int>(_MM_ROUND_MASK)) | static_cast<unsigned int>(_MM_ROUND_NEAREST);
	_mm_setcsr(mxcsr);
#endif
#endif
}

} // namespace fp
