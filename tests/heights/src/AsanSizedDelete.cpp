// BFME1 Mobile — tests (E1-25), only in -DBFME_TESTS_ASAN=ON builds
// SPDX-License-Identifier: GPL-3.0-or-later
//
// GameMemoryNull.cpp replaces operator new/delete (malloc/free) but not the C++14 sized
// operator delete(void*, size_t). Without ASan libstdc++'s sized delete forwards to the replaced
// unsized one; under ASan its interceptor takes the sized call itself and reports a false
// alloc-dealloc-mismatch (malloc vs operator delete) on every std::vector reallocation. Forward the
// sized forms here so ASan sees malloc/free pairs and keeps checking overflows, use-after-free and leaks.
#include <cstddef>
#include <new>

void operator delete(void* p, std::size_t) noexcept { ::operator delete(p); }
void operator delete[](void* p, std::size_t) noexcept { ::operator delete[](p); }
