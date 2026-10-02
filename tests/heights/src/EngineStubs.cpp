// BFME1 Mobile — tests (E1-25: 16-bit terrain heights)
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Link stubs for the engine globals that the real WorldHeightMap / DataChunk / AsciiString translation
// units reference (the linker keeps only what is reachable, -Wl,--gc-sections). Pulling SidesList and
// the rest of GameLogic along would mean linking the whole game.
#include "PreRTS.h"

#include "Common/CriticalSection.h"
#include "GameLogic/SidesList.h"

CriticalSection* TheAsciiStringCriticalSection = nullptr;    // single-threaded test: no locking
CriticalSection* TheUnicodeStringCriticalSection = nullptr;

// WorldHeightMap::WorldHeightMap() calls TheSidesList->validateSides() (it fixes up player sides for
// WorldBuilder). The stub below never touches the object, so plain storage stands in for it.
alignas(SidesList) static unsigned char s_sidesListStorage[sizeof(SidesList)];
SidesList* TheSidesList = reinterpret_cast<SidesList*>(s_sidesListStorage);
Bool SidesList::validateSides() { return false; }
