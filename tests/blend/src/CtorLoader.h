// BFME1 Mobile — tests (E1-25 review #10)
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Runs the REAL loading constructor WorldHeightMap(ChunkInputStream*, Bool logicalDataOnly) on an
// in-memory map: the path the game takes (W3DTerrainLogic / HeightMap), including the constructor's own
// throw and its cleanup on failure. Built into bfme_blend_engine_release only (see CMakeLists.txt).
#pragma once

#include "BlendLoader.h"

#include <cstdint>
#include <string>
#include <vector>

namespace bfmeblend {

struct CtorResult {
	bool constructed = false;   // the constructor returned
	bool threw = false;
	bool errorCode = false;     // the exception was an engine ErrorCode ...
	unsigned error = 0;         // ... with this value
	BlendStatsCopy stats;       // when constructed
	int width = 0, height = 0;
};

// `plain`: an uncompressed chunky map ("CkMp"...). The map object is released before returning.
CtorResult constructMap(const std::vector<uint8_t>& plain, bool logicalDataOnly);

// WorldHeightMap::m_alphaTiles are static, shared by all maps: created by the first constructor that gets
// to setupAlphaTiles(), released by any destructor (upstream). Releases them, for an exact block count
// after a constructor that failed inside setupAlphaTiles() itself.
void releaseStaticAlphaTiles();

unsigned errorCorruptFileFormat();   // ERROR_CORRUPT_FILE_FORMAT
unsigned errorOutOfMemory();         // ERROR_OUT_OF_MEMORY

} // namespace bfmeblend
