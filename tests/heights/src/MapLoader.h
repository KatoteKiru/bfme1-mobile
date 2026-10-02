// BFME1 Mobile — tests (E1-25: 16-bit terrain heights)
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Loads the HeightMapData chunk of a .map through the REAL engine code: DataChunkInput (DataChunk.cpp)
// and WorldHeightMap::ParseHeightMapData / ParseSizeOnly (WorldHeightMap.cpp), then the pathfinding
// cliff flags through WorldHeightMap::initCliffFlagsFromHeights. Only engine API that exists both
// upstream (u8 heights) and after E1-25 (u16) is used, because heights_zh_dump is compiled against both.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace bfmeheights {

enum class ParseMode { Full, SizeOnly };

struct LoadedHeights {
	bool ok = false;
	std::string error;
	int version = -1;        // HeightMapData chunk version seen by the parser
	int width = 0, height = 0, border = 0, dataSize = 0;
	std::vector<int> raw;    // WorldHeightMap::getHeight(x, y) for idx = y*width+x < dataSize
	std::vector<float> world;  // getHeight(x, y) * MAP_HEIGHT_SCALE, the engine's world-height expression
	std::vector<uint8_t> cliff;  // getCliffState(x, y) after initCliffFlagsFromHeights, (width-1)*(height-1)
	int outOfRange[4] = {0, 0, 0, 0};  // getHeight(-1,0), getHeight(0,-1), getHeight(w,h-1), getHeight(0,h)
};

// `bytes` is an uncompressed chunky map ("CkMp"...) or an EAR\0 RefPack-compressed one.
LoadedHeights loadHeights(const std::vector<uint8_t>& bytes, ParseMode mode, bool withCliffs);

// EAR\0 + u32 size + RefPack (EAC refdecode.cpp, as CompressionManager); other data returned as is.
bool decompressMap(const std::vector<uint8_t>& in, std::vector<uint8_t>& out, std::string& error);

float mapHeightScale();   // MAP_HEIGHT_SCALE of the tree the test is built against
int headerVersion5();     // 1 if K_HEIGHT_MAP_VERSION_5 exists in the tree (patched), 0 upstream

// Minimal BIG4/BIGF reader (big-endian header, case-insensitive lookup with '/' == '\\').
struct BigEntry { std::string name; uint32_t offset = 0, size = 0; };
bool readBigDirectory(const std::string& path, std::vector<BigEntry>& entries, std::string& error);
bool readBigEntry(const std::string& path, const BigEntry& e, std::vector<uint8_t>& out);
std::string normalizeBigName(std::string s);

bool readFile(const std::string& path, std::vector<uint8_t>& out);

} // namespace bfmeheights
