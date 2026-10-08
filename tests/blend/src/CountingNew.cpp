// BFME1 Mobile — tests (E1-25 review #10)
// SPDX-License-Identifier: GPL-3.0-or-later
//
// See CountingNew.h. Single-threaded test binary: plain counters.
#include "PreRTS.h"

#include "Common/Errors.h"

#include "CountingNew.h"

#include <cstdlib>
#include <cstring>
#include <new>

#ifndef DISABLE_GAMEMEMORY_NEW_OPERATORS
#error "the engine of bfme_blend_ctor_tests must be built with DISABLE_GAMEMEMORY_NEW_OPERATORS"
#endif

namespace {
long s_live = 0;
long s_count = 0;
long s_failAt = 0;
bool s_fired = false;

void* allocate(size_t size)
{
	++s_count;
	if (s_failAt != 0 && s_count == s_failAt) {
		s_failAt = 0;
		s_fired = true;
		throw ERROR_OUT_OF_MEMORY;   // as GameMemoryNull when malloc fails
	}
	void* p = std::malloc(size ? size : 1);
	if (p == nullptr)
		throw ERROR_OUT_OF_MEMORY;
	std::memset(p, 0, size);   // GameMemoryNull zero-fills; the engine relies on it
	++s_live;
	return p;
}

void release(void* p)
{
	if (p == nullptr) return;
	--s_live;
	std::free(p);
}
} // namespace

namespace bfmealloc {
long liveBlocks() { return s_live; }
long allocationCount() { return s_count; }
void armFailure(long nth)
{
	s_count = 0;
	s_failAt = nth;
	s_fired = false;
}
bool failureFired() { return s_fired; }
} // namespace bfmealloc

void* operator new(size_t size) { return allocate(size); }
void* operator new[](size_t size) { return allocate(size); }
void operator delete(void* p) noexcept { release(p); }
void operator delete[](void* p) noexcept { release(p); }
// Sized delete: libstdc++ forwards it to the unsized one above; under ASan its interceptor would take it,
// so tests/heights/src/AsanSizedDelete.cpp (linked in -DBFME_TESTS_ASAN=ON builds) forwards it here.
