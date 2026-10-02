// BFME1 Mobile — tests (E1-25: BlendTileData v14/v15)
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Link stubs for the globals that WorldHeightMap::ParseBlendTileData reaches (in addition to
// tests/heights/src/EngineStubs.cpp), compiled against both trees:
// - TheWritableGlobalData (TheGlobalData): only m_use3WayTerrainBlends (and m_adjustCliffTextures = 0 in the consumers) is
//   read; plain zeroed storage stands in for the object (the INI-driven constructor is not linked).
// - TheTerrainTypes->findTerrain(): returns nullptr, so readTexClass opens no texture file
//   (TheFileSystem is never dereferenced). Tiles stay null: textures are E1-32, not E1-25.
// - DebugLog / DebugLogRaw: the engine is built with RELEASE_DEBUG_LOGGING, DEBUG_LOG lands here.
#include "PreRTS.h"

#include "Common/FileSystem.h"
#include "Common/GlobalData.h"
#include "Common/TerrainTypes.h"

#include "BlendLoader.h"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <string>

alignas(GlobalData) static unsigned char s_globalDataStorage[sizeof(GlobalData)];
GlobalData* TheWritableGlobalData = reinterpret_cast<GlobalData*>(s_globalDataStorage);

alignas(TerrainTypeCollection) static unsigned char s_terrainTypesStorage[sizeof(TerrainTypeCollection)];
TerrainTypeCollection* TheTerrainTypes = reinterpret_cast<TerrainTypeCollection*>(s_terrainTypesStorage);
TerrainType* TerrainTypeCollection::findTerrain(AsciiString) { return nullptr; }

FileSystem* TheFileSystem = nullptr;
// Referenced by readTexClass on the findTerrain() != nullptr branch, which the stub above never takes.
File* FileSystem::openFile(const Char*, Int, size_t, FileInstance)
{
	std::abort();
}

namespace bfmeblend {
std::string& capturedLog()
{
	static std::string log;
	return log;
}
} // namespace bfmeblend

static void appendLog(const char* format, va_list args, bool newline)
{
	char buf[1024];
	std::vsnprintf(buf, sizeof(buf), format, args);
	std::string& log = bfmeblend::capturedLog();
	log += buf;
	if (newline) log += '\n';
	if (std::getenv("BFME_TEST_ECHO_LOG")) std::fprintf(stderr, "%s%s", buf, newline ? "\n" : "");
}

extern "C" void DebugLog(const char* format, ...)
{
	va_list args;
	va_start(args, format);
	appendLog(format, args, true);
	va_end(args);
}

extern "C" void DebugLogRaw(const char* format, ...)
{
	va_list args;
	va_start(args, format);
	appendLog(format, args, false);
	va_end(args);
}
