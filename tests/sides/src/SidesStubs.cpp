// BFME1 Mobile — SidesList v6 parser tests
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Globals the real ParseSidesDataChunk reaches. ScriptList is not linked: v6 does not
// call it, and the v1–v5 path only needs the two entry points so the chunk can close.
#include "PreRTS.h"

#define INSTANTIATE_WELL_KNOWN_KEYS
#include "Common/WellKnownKeys.h"

#include "Common/CriticalSection.h"
#include "Common/GlobalData.h"
#include "Common/Debug.h"
#include "Common/GameMemory.h"
#include "Common/Snapshot.h"
#include "Common/SubsystemInterface.h"
#include "GameLogic/Scripts.h"
#include "GameLogic/SidesList.h"

#include <cstdarg>
#include <string>

CriticalSection* TheAsciiStringCriticalSection = nullptr;
CriticalSection* TheUnicodeStringCriticalSection = nullptr;
CriticalSection* TheDmaCriticalSection = nullptr;
CriticalSection* TheMemoryPoolCriticalSection = nullptr;

SubsystemInterfaceList* TheSubsystemList = nullptr;
GlobalData* TheWritableGlobalData = nullptr;

static SidesList s_sidesList;
static NameKeyGenerator s_nameKeys;

Snapshot::Snapshot() {}
Snapshot::~Snapshot() {}
SubsystemInterface::SubsystemInterface() {}
SubsystemInterface::~SubsystemInterface() {}

namespace bfmesides {
std::string& capturedLog()
{
	static std::string log;
	return log;
}

int& scriptParserCalls()
{
	static int calls = 0;
	return calls;
}

void reset()
{
	static bool ready = false;
	if (!ready) {
		initMemoryManager();
		TheSidesList = &s_sidesList;
		TheNameKeyGenerator = &s_nameKeys;
		s_nameKeys.init();
		ready = true;
	}
	TheSidesList->clear();
	capturedLog().clear();
	scriptParserCalls() = 0;
}
} // namespace bfmesides

static void appendLog(const char* format, va_list args)
{
	char buf[1024];
	std::vsnprintf(buf, sizeof(buf), format, args);
	bfmesides::capturedLog() += buf;
	bfmesides::capturedLog() += '\n';
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

extern "C" void DebugInit(int) {}
extern "C" void DebugShutdown() {}

unsigned int DebugLevelMask = ~0u;
const char* TheDebugLevels[DEBUG_LEVEL_MAX] = {"net"};

Bool ScriptList::ParseScriptsDataChunk(DataChunkInput&, DataChunkInfo*, void*)
{
	++bfmesides::scriptParserCalls();
	return true;
}

Int ScriptList::getReadScripts(ScriptList* scriptLists[MAX_PLAYER_COUNT])
{
	for (Int i = 0; i < MAX_PLAYER_COUNT; ++i)
		scriptLists[i] = nullptr;
	return 0;
}
