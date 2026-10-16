// BFME1 Mobile — tests (E1-51: terrain tile atlas)
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Synthetic stand-ins for the 2.22 measurements. No game files. Fords of Isen class widths are the
// ones recorded in docs/bfme/E1-26-M1.md (984 tiles, 63 classes). Eastfold and Trilogy use the
// measured totals: 4 classes of side 16, and 3396 tiles / 175 classes of which 5 have side >= 15.
#include "PreRTS.h"

#include "Common/DataChunk.h"
#include "Common/GlobalData.h"
#include "W3DDevice/GameClient/TerrainAtlas.h"
#include "W3DDevice/GameClient/TileData.h"
#include "W3DDevice/GameClient/WorldHeightMap.h"

#include "../../blend/src/BlendLoader.h"
#include "../../blend/src/SyntheticBlend.h"
#include "../../heights/harness/TestHarness.h"

#include <cmath>
#include <cstring>
#include <set>
#include <string>
#include <vector>

using bfmeblend::BlendSpec;
using bfmeblend::TexClassSpec;
using bfmeblend::capturedLog;
using bfmeblend::writeBlendMap;

namespace {

const int kFordsWidths[] = {
	4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 2, 4, 4, 4, 4, 4, 4, 8,
	4, 4, 4, 4, 4, 4, 4, 2, 2, 2, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
	4, 2, 4, 4, 4, 4, 2,
};

struct MaxEdgeGuard {
	explicit MaxEdgeGuard(int edge) { bfmeTerrainAtlasSetMaxEdge(edge); }
	~MaxEdgeGuard() { bfmeTerrainAtlasSetMaxEdge(0); }
};

struct ShapeGuard {
	ShapeGuard(int w, int h, bool multi) { bfmeTerrainAtlasForceShape(w, h, multi ? 1 : 0); }
	~ShapeGuard() { bfmeTerrainAtlasForceShape(0, 0, false); }
};

int tileCount(const int *widths, int n)
{
	int t = 0;
	for (int i = 0; i < n; ++i) t += widths[i] * widths[i];
	return t;
}

int countOf(const std::string &s, const char *what)
{
	int n = 0;
	for (size_t p = s.find(what); p != std::string::npos; p = s.find(what, p + std::strlen(what))) ++n;
	return n;
}

TerrainAtlasStats packWidths(const int *widths, int n, const char *kind)
{
	std::vector<TerrainAtlasClassIn> in(static_cast<size_t>(n));
	std::vector<TerrainAtlasClassOut> out(static_cast<size_t>(n));
	int cursor = 0;
	for (int i = 0; i < n; ++i) {
		in[size_t(i)].index = i;
		in[size_t(i)].firstTile = cursor;
		in[size_t(i)].width = widths[i];
		in[size_t(i)].numTiles = widths[i] * widths[i];
		in[size_t(i)].name = "synth";
		cursor += in[size_t(i)].numTiles;
	}
	TerrainAtlasStats stats;
	Int detailed = 0;
	capturedLog().clear();
	bfmePackTerrainAtlas(in.data(), n, out.data(), &stats, &detailed, kind, true);
	bfmeLogTerrainAtlasSummary(kind, &stats);
	int wide = 0;
	std::set<long long> slots;
	for (int i = 0; i < n; ++i) {
		if (widths[i] >= 15) ++wide;
		if (out[size_t(i)].page < 0) continue;
		const long long key = (long long)out[size_t(i)].page * 1000000ll
			+ (long long)out[size_t(i)].row * 1000ll + out[size_t(i)].column;
		REQUIRE(slots.insert(key).second);
		REQUIRE(out[size_t(i)].x == 4 + out[size_t(i)].column * (64 + 8));
		REQUIRE(out[size_t(i)].y == 4 + out[size_t(i)].row * (64 + 8));
	}
	if (stats.overflowClasses == 0) REQUIRE(stats.pageCount >= wide);
	return stats;
}

std::vector<int> trilogyWidths()
{
	// 5 classes of side 16 (each needs its own 2048 page) plus smaller classes that fill the
	// gaps: 86 of side 4, 82 of side 3, 2 of side 1. 5+86+82+2 = 175 classes,
	// 5*256 + 86*16 + 82*9 + 2 = 3396 tiles.
	std::vector<int> w(5, 16);
	w.insert(w.end(), 86, 4);
	w.insert(w.end(), 82, 3);
	w.insert(w.end(), 2, 1);
	return w;
}

class MemoryChunkStream : public ChunkInputStream {
public:
	explicit MemoryChunkStream(const std::vector<uint8_t> &data) : m_data(data) {}
	Int read(void *pData, Int numBytes) override
	{
		Int n = numBytes < 0 ? 0 : numBytes;
		if (m_pos + size_t(n) > m_data.size()) n = Int(m_data.size() - m_pos);
		if (n > 0) std::memcpy(pData, m_data.data() + m_pos, size_t(n));
		m_pos += size_t(n);
		return n;
	}
	UnsignedInt tell() override { return UnsignedInt(m_pos); }
	Bool absoluteSeek(UnsignedInt pos) override
	{
		if (pos > m_data.size()) return false;
		m_pos = pos;
		return true;
	}
	Bool eof() override { return m_pos >= m_data.size(); }

private:
	const std::vector<uint8_t> &m_data;
	size_t m_pos = 0;
};

class AtlasMap : public WorldHeightMap {
public:
	AtlasMap() : WorldHeightMap() {}

	bool loadBytes(const std::vector<uint8_t> &bytes)
	{
		MemoryChunkStream stream(bytes);
		DataChunkInput file(&stream);
		if (!file.isValidFileType()) return false;
		file.registerParser("HeightMapData", AsciiString::TheEmptyString, &AtlasMap::parseHeights);
		file.registerParser("BlendTileData", AsciiString::TheEmptyString, &AtlasMap::parseBlend);
		return file.parse(this) && m_data != nullptr;
	}

	void fillSourceTiles()
	{
		for (Int i = 0; i < m_tileCapacity; ++i) {
			if (m_sourceTiles[i] == nullptr) m_sourceTiles[i] = new TileData();
		}
	}

	Int pack()
	{
		Int edge = 0;
		return updateTileTexturePositions(&edge);
	}

	Int blitPage(UnsignedByte *bits, Int pitch, Int w, Int h, Int page, Int *rejected)
	{
		return blitBaseAtlasPage(bits, pitch, w, h, page, rejected);
	}

	Bool cliffUV(Int ndx, Short tileNdx, float U[4], float V[4])
	{
		return getUVForTileIndex(ndx, tileNdx, U, V);
	}

	Int tilePage(Short tileNdx)
	{
		float a = 0, b = 0, c = 0, d = 0;
		Int page = -2;
		getUVForNdx(tileNdx, &a, &b, &c, &d, &page);
		return page;
	}

	const TerrainAtlasStats &stats() const { return m_atlasStats; }
	TXTextureClass &texClass(int i) { return m_textureClasses[i]; }

private:
	static Bool parseHeights(DataChunkInput &file, DataChunkInfo *info, void *userData)
	{
		return ParseHeightMapDataChunk(file, info, userData);
	}
	static Bool parseBlend(DataChunkInput &file, DataChunkInfo *info, void *userData)
	{
		return ParseBlendTileDataChunk(file, info, userData);
	}
};

BlendSpec specFromWidths(const int *widths, int n)
{
	BlendSpec s;
	s.name = "atlas";
	s.version = 4;
	s.heightVersion = 4;
	s.width = 2;
	s.height = 2;
	s.heights.assign(4, 0);
	s.tiles.assign(4, 0);
	s.blends.assign(4, 0);
	s.numBlendedTiles = 1;
	int cursor = 0;
	for (int i = 0; i < n; ++i) {
		TexClassSpec t;
		t.firstTile = cursor;
		t.width = widths[i];
		t.numTiles = widths[i] * widths[i];
		t.name = "Synth" + std::to_string(i);
		cursor += t.numTiles;
		s.textureClasses.push_back(t);
	}
	s.numBitmapTiles = cursor;
	return s;
}

void blitAllPages(AtlasMap &map, int expectWritten)
{
	const TerrainAtlasStats &st = map.stats();
	REQUIRE(st.pageWidth > 0);
	REQUIRE(st.pageHeight > 0);
	int written = 0;
	int rejected = 0;
	const size_t pixels = size_t(st.pageWidth) * size_t(st.pageHeight);
	for (int p = 0; p < st.pageCount; ++p) {
		std::vector<uint8_t> buf(pixels * 2 + 64, 0);
		std::memset(buf.data() + pixels * 2, 0xA5, 64);
		Int pageRejected = 0;
		written += map.blitPage(buf.data(), st.pageWidth * 2, st.pageWidth, st.pageHeight, p, &pageRejected);
		rejected += pageRejected;
		for (int i = 0; i < 64; ++i) REQUIRE_EQ(int(buf[pixels * 2 + size_t(i)]), 0xA5);
	}
	REQUIRE_EQ(written, expectWritten);
	REQUIRE_EQ(rejected, 0);
}

} // namespace

TEST(atlas_fords_one_page_4096x2048)
{
	MaxEdgeGuard edge(8192);
	const int n = int(sizeof(kFordsWidths) / sizeof(kFordsWidths[0]));
	REQUIRE_EQ(n, 63);
	REQUIRE_EQ(tileCount(kFordsWidths, n), 984);
	const TerrainAtlasStats st = packWidths(kFordsWidths, n, "fords");
	REQUIRE_EQ(st.pageWidth, 4096);
	REQUIRE_EQ(st.pageHeight, 2048);
	REQUIRE_EQ(st.pageCount, 1);
	REQUIRE_EQ(st.tilesPerRow, 56);
	REQUIRE_EQ(st.rowsPerPage, 28);
	REQUIRE_EQ(st.classesPlaced, 63);
	REQUIRE_EQ(st.tilesPlaced, 984);
	REQUIRE_EQ(st.overflowClasses, 0);
	REQUIRE_EQ(st.overflowTiles, 0);
	REQUIRE_EQ(st.rejectedTiles, 0);
}

TEST(atlas_fords_single_2048_drops_17)
{
	// The old packer: one 2048 page, no second page. This is the measured loss.
	MaxEdgeGuard edge(2048);
	ShapeGuard shape(2048, 2048, false);
	const int n = int(sizeof(kFordsWidths) / sizeof(kFordsWidths[0]));
	const TerrainAtlasStats st = packWidths(kFordsWidths, n, "fords2048");
	REQUIRE_EQ(st.pageWidth, 2048);
	REQUIRE_EQ(st.pageHeight, 2048);
	REQUIRE_EQ(st.pageCount, 1);
	REQUIRE_EQ(st.overflowClasses, 17);
	REQUIRE_EQ(st.overflowTiles, 200);
	REQUIRE_EQ(st.classesPlaced, 46);
	REQUIRE_EQ(st.tilesPlaced, 784);
	REQUIRE_EQ(countOf(capturedLog(), "TerrainAtlas: overflow #"), 16);
	REQUIRE(capturedLog().find("overflow classes 17") != std::string::npos);
}

TEST(atlas_eastfold_four_pages)
{
	MaxEdgeGuard edge(8192);
	const int widths[4] = {16, 16, 16, 16};
	const TerrainAtlasStats st = packWidths(widths, 4, "eastfold");
	REQUIRE_EQ(st.pageWidth, 2048);
	REQUIRE_EQ(st.pageHeight, 2048);
	REQUIRE_EQ(st.pageCount, 4);
	REQUIRE_EQ(st.tilesPerRow, 28);
	REQUIRE_EQ(st.rowsPerPage, 28);
	REQUIRE_EQ(st.classesPlaced, 4);
	REQUIRE_EQ(st.tilesPlaced, 4 * 256);
	REQUIRE_EQ(st.overflowClasses, 0);
	REQUIRE(capturedLog().find("does not fit one 4096x2048") != std::string::npos);
}

TEST(atlas_trilogy_five_pages)
{
	MaxEdgeGuard edge(4096);
	const std::vector<int> widths = trilogyWidths();
	REQUIRE_EQ(int(widths.size()), 175);
	REQUIRE_EQ(tileCount(widths.data(), int(widths.size())), 3396);
	const TerrainAtlasStats st = packWidths(widths.data(), int(widths.size()), "trilogy");
	REQUIRE_EQ(st.pageWidth, 2048);
	REQUIRE_EQ(st.pageHeight, 2048);
	REQUIRE_EQ(st.pageCount, 5);
	REQUIRE_EQ(st.classesPlaced, 175);
	REQUIRE_EQ(st.tilesPlaced, 3396);
	REQUIRE_EQ(st.overflowClasses, 0);
	REQUIRE_EQ(st.overflowTiles, 0);
	REQUIRE_EQ(st.rejectedTiles, 0);
}

TEST(atlas_overflow_log_first_16)
{
	MaxEdgeGuard edge(2048);
	ShapeGuard shape(2048, 2048, false);
	std::vector<int> widths(801, 1);
	const TerrainAtlasStats st = packWidths(widths.data(), int(widths.size()), "ones");
	REQUIRE_EQ(st.classesPlaced, 784);
	REQUIRE_EQ(st.overflowClasses, 17);
	REQUIRE_EQ(countOf(capturedLog(), "TerrainAtlas: overflow #"), 16);
	REQUIRE(capturedLog().find("per-case lines: first 16") != std::string::npos);
	REQUIRE(capturedLog().find("overflow classes 17") != std::string::npos);
}

TEST(atlas_map_blit_no_oob)
{
	TheWritableGlobalData->m_use3WayTerrainBlends = 1;
	MaxEdgeGuard edge(8192);

	const int nFords = int(sizeof(kFordsWidths) / sizeof(kFordsWidths[0]));
	{
		AtlasMap *map = new AtlasMap();
		REQUIRE(map->loadBytes(writeBlendMap(specFromWidths(kFordsWidths, nFords))));
		map->fillSourceTiles();
		map->pack();
		REQUIRE_EQ(map->stats().pageWidth, 4096);
		REQUIRE_EQ(map->stats().pageHeight, 2048);
		REQUIRE_EQ(map->stats().pageCount, 1);
		REQUIRE_EQ(map->stats().rejectedTiles, 0);
		blitAllPages(*map, 984);
		map->Release_Ref();
	}
	{
		const int widths[4] = {16, 16, 16, 16};
		AtlasMap *map = new AtlasMap();
		REQUIRE(map->loadBytes(writeBlendMap(specFromWidths(widths, 4))));
		map->fillSourceTiles();
		map->pack();
		REQUIRE_EQ(map->stats().pageCount, 4);
		REQUIRE_EQ(map->stats().pageWidth, 2048);
		blitAllPages(*map, 4 * 256);
		map->Release_Ref();
	}
	{
		const std::vector<int> widths = trilogyWidths();
		AtlasMap *map = new AtlasMap();
		REQUIRE(map->loadBytes(writeBlendMap(specFromWidths(widths.data(), int(widths.size())))));
		map->fillSourceTiles();
		map->pack();
		REQUIRE_EQ(map->stats().pageCount, 5);
		REQUIRE_EQ(map->stats().pageWidth, 2048);
		REQUIRE_EQ(map->stats().tilesPlaced, 3396);
		REQUIRE_EQ(map->stats().rejectedTiles, 0);
		blitAllPages(*map, 3396);
		map->Release_Ref();
	}
}

TEST(atlas_cliff_uv_uses_texture_width)
{
	// File u/v are fractions of TEXTURE_WIDTH. Local class units: 1 is one class edge
	// (v = 0 is the bottom). The shader wraps with origin + fract(local) * size.
	TheWritableGlobalData->m_use3WayTerrainBlends = 1;
	TheWritableGlobalData->m_adjustCliffTextures = true;
	MaxEdgeGuard edge(4096);
	const int widths[1] = {4};
	BlendSpec s = specFromWidths(widths, 1);
	s.version = 5;
	s.numCliffInfo = 2;
	s.cliff.assign(4, 0);
	s.cliff[0] = 1;
	bfmeblend::CliffSpec cliff;
	cliff.tileIndex = 0;
	const float cell = 32.0f / 2048.0f;
	cliff.uv[0] = cell;
	cliff.uv[1] = cell;
	cliff.flip = 1; // getUVForTileIndex returns this flag once the cliff UVs are written
	s.cliffs.push_back(cliff);

	AtlasMap *map = new AtlasMap();
	REQUIRE(map->loadBytes(writeBlendMap(s)));
	map->fillSourceTiles();
	map->pack();
	REQUIRE_EQ(map->stats().pageWidth, 4096);
	REQUIRE_EQ(map->stats().pageHeight, 2048);
	float U[4], V[4];
	REQUIRE(map->cliffUV(0, 0, U, V));
	const float classPx = 4.0f * 64.0f;
	const float local = cell * (2048.0f / classPx);
	REQUIRE(std::fabs(U[0] - local) < 1e-6f);
	REQUIRE(std::fabs(V[0] - (1.0f + local)) < 1e-6f);
	Int page = -1;
	Coord2D origin, size;
	REQUIRE(map->getCliffClassInfo(0, &page, &origin, &size));
	REQUIRE_EQ(page, 0);
	REQUIRE_EQ(map->tilePage(0), 0);
	REQUIRE(std::fabs(origin.x - 4.0f / 4096.0f) < 1e-6f);
	REQUIRE(std::fabs(origin.y - 4.0f / 2048.0f) < 1e-6f);
	REQUIRE(std::fabs(size.x - classPx / 4096.0f) < 1e-6f);
	REQUIRE(std::fabs(size.y - classPx / 2048.0f) < 1e-6f);
	map->Release_Ref();
	TheWritableGlobalData->m_adjustCliffTextures = false;
}

TEST(atlas_tile_index_bounds)
{
	TheWritableGlobalData->m_use3WayTerrainBlends = 1;
	MaxEdgeGuard edge(4096);
	const int widths[1] = {1};
	AtlasMap *map = new AtlasMap();
	REQUIRE(map->loadBytes(writeBlendMap(specFromWidths(widths, 1))));
	map->fillSourceTiles();
	// The parser would have rejected this width. A later mutation must not walk off the table.
	map->texClass(0).width = 8;
	map->pack();
	REQUIRE(map->stats().rejectedTiles > 0);
	REQUIRE_EQ(map->stats().overflowClasses, 0);
	map->Release_Ref();
}
