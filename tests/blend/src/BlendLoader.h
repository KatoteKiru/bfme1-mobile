// BFME1 Mobile — tests (E1-25: BlendTileData v14/v15)
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Loads HeightMapData + BlendTileData of a .map through the REAL engine code (DataChunkInput,
// WorldHeightMap::ParseHeightMapDataChunk / ParseBlendTileDataChunk) and copies out everything the
// parser stored. Compiled against both the upstream and the patched tree (blend_zh_dump); the BFME-only
// parts (counters, masks) exist only in the patched tree (`newTree`).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace bfmeblend {

struct TexClass { int firstTile = 0, numTiles = 0, width = 0; std::string name; };
struct BlendDesc {
	int blendNdx = 0;
	int horiz = 0, vert = 0, rightDiagonal = 0, leftDiagonal = 0, inverted = 0, longDiagonal = 0;
	int customBlendEdgeClass = 0;
};
struct CliffInfo { int tileIndex = 0; float uv[8] = {}; int flip = 0, mutant = 0; };

struct BlendStatsCopy {
	int version = -1, danglingTiles = 0, flagsOutside0x3 = 0, edgeClassWithoutClasses = 0, edgeClassOutOfRange = 0,
		dirBytesNot01 = 0, danglingBlend = 0, dangling3Way = 0, danglingCliff = 0;
};

// What the consumers of the blend data return per cell (render-side code in WorldHeightMap.cpp).
struct CellConsumers {
	float alphaU[4], alphaV[4]; uint8_t alpha[4]; int alphaFlip;
	int extraOk; float extraU[4], extraV[4]; uint8_t extraAlpha[4]; int extraFlip, extraCliff;
	int uvOk; float u[4], v[4];
	int textureClass, baseTextureClass, cliffMapped, cliffState, flipState;
};

struct LoadedBlend {
	bool ok = false;
	std::string error;
	bool newTree = false;
	int heightVersion = -1, blendVersion = -1;
	int width = 0, height = 0, dataSize = 0;
	size_t payloadOffset = 0, payloadSize = 0;   // BlendTileData payload in the decompressed map
	bool atEndOfChunk = false;
	std::vector<int16_t> tiles;
	std::vector<int32_t> blends, threeWay, cliff;
	std::vector<uint8_t> cliffState;   // m_cellCliffState, bytesPerRow * height
	int bytesPerRow = 0;
	int numBitmapTiles = 0, numBlendedTiles = 0, numCliffInfo = 0, numEdgeTiles = 0;
	std::vector<TexClass> textureClasses, edgeClasses;
	std::vector<BlendDesc> descs;      // [0, numBlendedTiles)
	std::vector<CliffInfo> cliffs;     // [0, numCliffInfo)
	// patched tree only
	BlendStatsCopy stats;
	bool hasMask[4] = {false, false, false, false};
	std::vector<uint8_t> masks[4];
	int tileCapacity = 0;
	std::vector<CellConsumers> consumers;   // filled when requested
};

struct LoadOptions {
	int use3WayTerrainBlends = 1;   // TheGlobalData->m_use3WayTerrainBlends (upstream default 1)
	bool consumers = false;         // also run the render-side consumers on every cell
};

// `bytes`: plain or EAR\0 RefPack map. `plain` (optional) receives the decompressed bytes.
LoadedBlend loadBlend(const std::vector<uint8_t>& bytes, const LoadOptions& opt, std::vector<uint8_t>* plain = nullptr);

// Engine debug log (DEBUG_LOG, built with RELEASE_DEBUG_LOGGING) captured by the stub DebugLog.
std::string& capturedLog();

} // namespace bfmeblend
