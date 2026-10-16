// BFME1 Mobile — E1-32. Two threads get different kernel TIDs from
// ThreadClass::_Get_Current_Thread_ID, and the main thread id does not change.
// BfmeIsRecordedDx8Thread is what TextureLoader::Is_DX8_Thread calls:
// before DX8Wrapper::Init the stored id is 0 and the answer is true;
// after Init stores the caller, a different thread is false.
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "dx8_thread.h"
#include "thread.h"

#include <sys/syscall.h>
#include <unistd.h>

#include <cstdio>
#include <thread>

namespace {

unsigned kernelTid()
{
	return static_cast<unsigned>(syscall(SYS_gettid));
}

struct Sample
{
	unsigned api;
	unsigned kernel;
	unsigned libc;
	int beforeInit;
	int afterInit;
};

void worker(Sample* out, unsigned recordedByInit)
{
	out->api = ThreadClass::_Get_Current_Thread_ID();
	out->kernel = kernelTid();
	out->libc = static_cast<unsigned>(gettid());
	// Stored id is still 0: DX8Wrapper::Init has not run.
	out->beforeInit = BfmeIsRecordedDx8Thread(out->api, 0) ? 1 : 0;
	// Init has stored the other thread's tid (dx8wrapper.cpp:718).
	out->afterInit = BfmeIsRecordedDx8Thread(out->api, recordedByInit) ? 1 : 0;
}

int fail(const char* what)
{
	std::fprintf(stderr, "FAIL: %s\n", what);
	return 1;
}

} // namespace

int main()
{
	const unsigned mainKernel = kernelTid();
	const unsigned mainA = ThreadClass::_Get_Current_Thread_ID();
	if (mainA == 0) {
		return fail("main thread id is 0");
	}
	if (mainA != mainKernel || mainA != static_cast<unsigned>(gettid())) {
		std::fprintf(stderr, "FAIL: api %u syscall %u gettid %u\n", mainA, mainKernel,
			static_cast<unsigned>(gettid()));
		return 1;
	}
	for (int i = 0; i < 32; ++i) {
		if (ThreadClass::_Get_Current_Thread_ID() != mainA) {
			return fail("main thread id changed across calls");
		}
	}

	Sample other{};
	std::thread workerThread([&other, mainA]() { worker(&other, mainA); });
	workerThread.join();

	const unsigned mainB = ThreadClass::_Get_Current_Thread_ID();
	if (mainB != mainA) {
		return fail("main thread id changed after another thread ran");
	}
	if (other.api == 0) {
		return fail("worker thread id is 0");
	}
	if (other.api == mainA) {
		return fail("two threads returned the same id");
	}
	if (other.api != other.kernel || other.api != other.libc) {
		std::fprintf(stderr, "FAIL: worker api %u syscall %u gettid %u\n", other.api, other.kernel, other.libc);
		return 1;
	}

	// DX8Wrapper::_MainThreadID starts at 0 (dx8wrapper.cpp:569). Init writes
	// the caller's tid (dx8wrapper.cpp:718). Headless never reaches that write.
	if (!BfmeIsRecordedDx8Thread(mainA, 0) || !other.beforeInit) {
		return fail("before Init, Is_DX8_Thread is false");
	}
	if (!BfmeIsRecordedDx8Thread(mainA, mainA)) {
		return fail("after Init, the recording thread is not the DX8 thread");
	}
	if (other.afterInit) {
		return fail("after Init, a different thread is the DX8 thread");
	}

	std::printf("main %u worker %u\n", mainA, other.api);
	return 0;
}
