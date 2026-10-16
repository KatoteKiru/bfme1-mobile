// BFME1 Mobile — GlobalLighting v7 parser tests
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Globals the real ParseLightingDataChunk reaches. GlobalData is not constructed:
// the test zeroes the storage and the parser writes the lighting fields.
#include "PreRTS.h"

#include "Common/Debug.h"
#include "Common/GlobalData.h"
#include "W3DDevice/GameClient/W3DShadow.h"

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <string>

alignas(GlobalData) static unsigned char s_globalDataStorage[sizeof(GlobalData)];
GlobalData* TheWritableGlobalData = reinterpret_cast<GlobalData*>(s_globalDataStorage);

alignas(W3DShadowManager) static unsigned char s_shadowStorage[sizeof(W3DShadowManager)];
W3DShadowManager* TheW3DShadowManager = reinterpret_cast<W3DShadowManager*>(s_shadowStorage);

namespace bfmelighting {
std::string& capturedLog()
{
	static std::string log;
	return log;
}

void resetGlobals()
{
	std::memset(s_globalDataStorage, 0, sizeof(s_globalDataStorage));
	std::memset(s_shadowStorage, 0, sizeof(s_shadowStorage));
	capturedLog().clear();
}
} // namespace bfmelighting

static void appendLog(const char* format, va_list args)
{
	char buf[1024];
	std::vsnprintf(buf, sizeof(buf), format, args);
	bfmelighting::capturedLog() += buf;
	bfmelighting::capturedLog() += '\n';
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
