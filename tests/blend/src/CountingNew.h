// BFME1 Mobile — tests (E1-25 review #10)
// SPDX-License-Identifier: GPL-3.0-or-later
//
// operator new / delete of bfme_blend_ctor_tests (src/CountingNew.cpp). The engine of that binary is
// built with DISABLE_GAMEMEMORY_NEW_OPERATORS, so these replace the GameMemoryNull ones and behave the
// same (malloc, zero-filled, ERROR_OUT_OF_MEMORY on failure). In addition they count live blocks (a leak
// check that also works without ASan) and can fail one chosen allocation (out-of-memory injection).
#pragma once

namespace bfmealloc {

long liveBlocks();             // operator new / new[] blocks not yet deleted
long allocationCount();        // operator new / new[] calls since the last armFailure()
void armFailure(long nth);     // the nth allocation from now (1-based) throws ERROR_OUT_OF_MEMORY; 0 = never
bool failureFired();           // the armed allocation has failed

} // namespace bfmealloc
