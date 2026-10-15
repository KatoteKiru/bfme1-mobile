// BFME1 Mobile — tests
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Link stubs for the engine globals that the VFS translation units reference but never use on the
// paths under test. The test binary compiles the REAL upstream VFS sources (StdBIGFileSystem,
// ArchiveFileSystem, StdLocalFileSystem, RAMFile, AsciiString, ...); pulling GlobalData, the
// audio manager, INI and the registry along would mean linking the whole game.
#include "PreRTS.h"

#include "Common/AudioEventRTS.h"
#include "Common/CriticalSection.h"
#include "Common/GameAudio.h"
#include "Common/GlobalData.h"
#include "Common/INI.h"
#include "Common/Registry.h"
#include "Common/SubsystemInterface.h"

GlobalData* TheWritableGlobalData = nullptr;      // loadMods() tests install the stub from GlobalDataStub.cpp
AudioManager* TheAudio = nullptr;                  // only closeArchiveFile(Music.big) uses it
CriticalSection* TheAsciiStringCriticalSection = nullptr;   // single-threaded test: no locking
CriticalSection* TheUnicodeStringCriticalSection = nullptr;
SubsystemInterfaceList* TheSubsystemList = nullptr;

// Referenced from SubsystemInterface::initSubsystem() (INI-driven subsystems); never called here.
INI::INI() {}
UnsignedInt INI::loadFileDirectory(AsciiString, INILoadType, Xfer*, Bool) { return 0; }

// Emitted through GameAudio.h inline code; never constructed here.
AudioEventRTS::~AudioEventRTS() {}

// StdBIGFileSystem::init() asks the registry for ZH/Generals paths; BfmeBIGFileSystem does not.
Bool GetStringFromGeneralsRegistry(AsciiString, AsciiString, AsciiString&) { return FALSE; }
Bool GetStringFromRegistry(AsciiString, AsciiString, AsciiString&) { return FALSE; }
