// BFME1 Mobile — PolygonTriggers v5 parser tests
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Globals the real PolygonTrigger translation unit reaches. GlobalData is not constructed:
// v1's default water area only reads m_waterExtentX / m_waterExtentY, which stay 0.
#include "PreRTS.h"

#include "Common/CriticalSection.h"
#include "Common/Debug.h"
#include "Common/GlobalData.h"
#include "Common/Snapshot.h"

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

CriticalSection* TheAsciiStringCriticalSection = nullptr;
CriticalSection* TheUnicodeStringCriticalSection = nullptr;

// Snapshot.cpp ctor/dtor are empty. The post-process unhook on TheGameState is commented out upstream.
Snapshot::Snapshot() {}
Snapshot::~Snapshot() {}

alignas(GlobalData) static unsigned char s_globalDataStorage[sizeof(GlobalData)];
GlobalData* TheWritableGlobalData = reinterpret_cast<GlobalData*>(s_globalDataStorage);

namespace bfmetrigger {
std::string& capturedLog()
{
	static std::string log;
	return log;
}

void resetGlobals()
{
	std::memset(s_globalDataStorage, 0, sizeof(s_globalDataStorage));
	capturedLog().clear();
}
} // namespace bfmetrigger

static void appendLog(const char* format, va_list args)
{
	char buf[1024];
	std::vsnprintf(buf, sizeof(buf), format, args);
	bfmetrigger::capturedLog() += buf;
	bfmetrigger::capturedLog() += '\n';
}

extern "C" void DebugLog(const char* format, ...)
{
	va_list args;
	va_start(args, format);
	appendLog(format, args);
	va_end(args);
}

extern "C" void DebugLogRaw(const char* format, ...)
{
	va_list args;
	va_start(args, format);
	appendLog(format, args);
	va_end(args);
}

unsigned int DebugLevelMask = ~0u;
const char* TheDebugLevels[DEBUG_LEVEL_MAX] = {"net"};
