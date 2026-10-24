// BFME1 Mobile — tests (E1-25: BlendTileData v14/v15)
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Unit tests of the real WorldHeightMap::ParseBlendTileData on synthetic maps (SyntheticBlend.h) and on
// the ZH maps already in the repository. No game data needed.
#include "../../heights/harness/TestHarness.h"
#include "../../heights/src/MapLoader.h"
#include "BlendLoader.h"
#include "SyntheticBlend.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>

using namespace bfmeblend;

namespace {

int countOccurrences(const std::string& s, const std::string& what)
{
	int n = 0;
	for (size_t p = s.find(what); p != std::string::npos; p = s.find(what, p + what.size())) ++n;
	return n;
}

// DataChunkInput logs every chunk without a registered parser ("[chunk] skipped ...", #61).
// These loaders register only the terrain parsers, so drop those lines before comparing.
std::string withoutChunkLines(const std::string& s)
{
	std::string out;
	size_t p = 0;
	while (p < s.size()) {
		size_t e = s.find('\n', p);
		e = e == std::string::npos ? s.size() : e + 1;
		if (s.compare(p, 8, "[chunk] ") != 0) out.append(s, p, e - p);
		p = e;
	}
	return out;
}

LoadedBlend load(const BlendSpec& s, int use3Way = 1)
{
	LoadOptions opt;
	opt.use3WayTerrainBlends = use3Way;
	capturedLog().clear();
	return loadBlend(writeBlendMap(s), opt);
}

// Everything the parser stored equals the spec (no anomalies in the spec).
void requireMatchesSpec(const LoadedBlend& b, const BlendSpec& s)
{
	REQUIRE(b.ok);
	REQUIRE(b.newTree);
	REQUIRE(b.atEndOfChunk);
	REQUIRE_EQ(b.blendVersion, s.version);
	REQUIRE_EQ(b.width, s.width);
	REQUIRE_EQ(b.height, s.height);
	const size_t n = size_t(s.width) * size_t(s.height);
	REQUIRE_EQ(b.tiles.size(), n);
	for (size_t i = 0; i < n; ++i) {
		REQUIRE_EQ(uint16_t(b.tiles[i]), s.tiles[i]);
		REQUIRE_EQ(uint32_t(b.blends[i]), s.blends[i]);
		if (s.version >= 6) REQUIRE_EQ(uint32_t(b.threeWay[i]), s.threeWay[i]);
		if (s.version >= 5) REQUIRE_EQ(uint32_t(b.cliff[i]), s.cliff[i]);
	}
	if (s.version >= 7) {
		const int sw = s.cliffStateBytesPerRow();
		for (int y = 0; y < s.height; ++y)
			for (int x = 0; x < sw; ++x)
				REQUIRE_EQ(int(b.cliffState[size_t(y) * size_t(b.bytesPerRow) + size_t(x)]), int(s.cliffState[size_t(y) * size_t(sw) + size_t(x)]));
	}
	for (int m = 0; m < 4; ++m) {
		REQUIRE_EQ(b.hasMask[m], BlendSpec::hasMask(s.version, m));
		if (b.hasMask[m]) REQUIRE(b.masks[m] == s.masks[m]);
	}
	REQUIRE_EQ(b.numBitmapTiles, s.numBitmapTiles);
	REQUIRE_EQ(b.numBlendedTiles, s.numBlendedTiles);
	REQUIRE_EQ(b.numCliffInfo, s.numCliffInfo);
	REQUIRE_EQ(b.textureClasses.size(), s.textureClasses.size());
	for (size_t i = 0; i < s.textureClasses.size(); ++i) {
		REQUIRE_EQ(b.textureClasses[i].firstTile, s.textureClasses[i].firstTile);
		REQUIRE_EQ(b.textureClasses[i].numTiles, s.textureClasses[i].numTiles);
		REQUIRE_EQ(b.textureClasses[i].width, s.textureClasses[i].width);
		REQUIRE_EQ(b.textureClasses[i].name, s.textureClasses[i].name);
	}
	REQUIRE_EQ(b.edgeClasses.size(), s.edgeClasses.size());
	REQUIRE_EQ(b.descs.size(), s.descs.size());
	for (size_t i = 0; i < s.descs.size(); ++i) {
		const BlendDesc& d = b.descs[i];
		const BlendDescSpec& e = s.descs[i];
		REQUIRE_EQ(d.blendNdx, e.blendNdx);
		REQUIRE_EQ(d.horiz, int(e.horiz));
		REQUIRE_EQ(d.vert, int(e.vert));
		REQUIRE_EQ(d.rightDiagonal, int(e.rightDiagonal));
		REQUIRE_EQ(d.leftDiagonal, int(e.leftDiagonal));
		REQUIRE_EQ(d.inverted, int(e.inverted));
		REQUIRE_EQ(d.longDiagonal, int(e.longDiagonal));
		REQUIRE_EQ(d.customBlendEdgeClass, e.customBlendEdgeClass);
	}
	if (s.version >= 5) {
		REQUIRE_EQ(b.cliffs.size(), s.cliffs.size());
		for (size_t i = 0; i < s.cliffs.size(); ++i) {
			REQUIRE_EQ(b.cliffs[i].tileIndex, s.cliffs[i].tileIndex);
			for (int k = 0; k < 8; ++k) REQUIRE_EQ(b.cliffs[i].uv[k], s.cliffs[i].uv[k]);
			REQUIRE_EQ(b.cliffs[i].flip, int(s.cliffs[i].flip));
			REQUIRE_EQ(b.cliffs[i].mutant, int(s.cliffs[i].mutant));
		}
	}
	const BlendStatsCopy& st = b.stats;
	REQUIRE_EQ(st.version, s.version);
	REQUIRE_EQ(st.danglingTiles + st.flagsOutside0x3 + st.edgeClassWithoutClasses + st.edgeClassOutOfRange + st.dirBytesNot01 +
		st.danglingBlend + st.dangling3Way + st.danglingCliff, 0);
	REQUIRE(b.tileCapacity >= b.numBitmapTiles);
}

} // namespace

// v15 layout of §2.4.1 / §2.4.3, including blend and cliff indices above 32767 (u32 since v14).
TEST(blend_v15_layout_roundtrip)
{
	struct Case { int w, h, blends, cliffs; };
	const Case cases[] = {{2, 2, 0, 0}, {9, 7, 0, 0}, {100, 37, 0, 0}, {257, 129, 40000, 35000}};
	uint32_t seed = 0x15u;
	for (const Case& c : cases) {
		const BlendSpec s = makeBlendSpec("v15", 15, c.w, c.h, seed++, c.blends, c.cliffs);
		const LoadedBlend b = load(s);
		requireMatchesSpec(b, s);
		if (c.blends) {
			REQUIRE(*std::max_element(b.blends.begin(), b.blends.end()) > 32767);
			REQUIRE(*std::max_element(b.cliff.begin(), b.cliff.end()) > 32767);
		}
		REQUIRE_EQ(b.tileCapacity, s.numBitmapTiles);   // classes are consecutive, numTiles == width^2
	}
}

// v9..v15: mask order and presence strictly by version, 16-bit indices up to v13, 32-bit from v14.
TEST(blend_versions_9_to_15_masks)
{
	for (int v = 9; v <= 15; ++v) {
		const BlendSpec s = makeBlendSpec("v" + std::to_string(v), v, 33, 17, 0x900u + uint32_t(v));
		const LoadedBlend b = load(s);
		requireMatchesSpec(b, s);
		REQUIRE_EQ(b.payloadSize, blendPayload(s).size());
		// the u16 -> u32 switch: the same spec written as v13 is 3*N*2 bytes shorter than as v14
	}
	BlendSpec a = makeBlendSpec("v13", 13, 20, 20, 7), c = a;
	c.version = 14;
	c.masks[2] = c.masks[1];   // taint mask present from v14
	REQUIRE_EQ(blendPayload(c).size() - blendPayload(a).size(), size_t(3 * 400 * 2 + 3 * 20));
	requireMatchesSpec(load(c), c);
}

TEST(blend_v16_and_above_rejected)
{
	for (int v : {16, 17, 23, 24, 25, 30}) {
		BlendSpec s = makeBlendSpec("v15", 15, 9, 9, 3);
		s.version = v;   // payload written as v15: the parser must stop before reading anything
		const LoadedBlend b = load(s);
		REQUIRE(!b.ok);
		REQUIRE(capturedLog().find("unsupported version v" + std::to_string(v)) != std::string::npos);
	}
	requireMatchesSpec(load(makeBlendSpec("v15", 15, 9, 9, 3)), makeBlendSpec("v15", 15, 9, 9, 3));
}

// Lead decisions: clamp dangling tiles, &0x3 flags, customBlendEdgeClass without such a class -> -1;
// each case counted and logged, plus a per-map summary.
TEST(blend_anomalies_clamped_masked_counted)
{
	BlendSpec s = makeBlendSpec("anomalies", 15, 20, 10, 0x2000u, 60);   // even seed: no edge classes
	REQUIRE(s.edgeClasses.empty());
	const int nb = s.numBitmapTiles;
	const uint16_t badTiles[] = {uint16_t(nb * 4), uint16_t(nb * 4 + 3), uint16_t(nb * 4 + 6), 24839, 0x7FFF, 0x8000, 0xFFFE, 0xFFFF};
	for (int i = 0; i < 8; ++i) s.tiles[size_t(i * 13)] = badTiles[i];
	const uint8_t badFlags[] = {8, 0x2D, 61, 0x83, 0xFF};
	for (int i = 0; i < 5; ++i) s.descs[size_t(i)].inverted = badFlags[i];
	const int32_t badEdges[] = {0, 5, 62, 1000};
	for (int i = 0; i < 4; ++i) s.descs[size_t(10 + i)].customBlendEdgeClass = badEdges[i];
	for (int i = 0; i < 3; ++i) s.descs[size_t(20 + i)].horiz = 12;
	s.blends[3] = uint32_t(s.numBlendedTiles + 5);
	s.blends[4] = uint32_t(s.numBlendedTiles);
	s.threeWay[5] = 0xFFFFFFFFu;
	s.cliff[6] = uint32_t(s.numCliffInfo);

	const LoadedBlend b = load(s);
	REQUIRE(b.ok);
	REQUIRE(b.atEndOfChunk);
	REQUIRE_EQ(b.stats.danglingTiles, 8);
	REQUIRE_EQ(b.stats.flagsOutside0x3, 5);
	REQUIRE_EQ(b.stats.edgeClassWithoutClasses, 4);
	REQUIRE_EQ(b.stats.edgeClassOutOfRange, 4);
	REQUIRE_EQ(b.stats.dirBytesNot01, 3);
	REQUIRE_EQ(b.stats.danglingBlend, 2);
	REQUIRE_EQ(b.stats.dangling3Way, 1);
	REQUIRE_EQ(b.stats.danglingCliff, 1);
	for (int i = 0; i < 8; ++i) REQUIRE_EQ(int(uint16_t(b.tiles[size_t(i * 13)])), ((nb - 1) << 2) | (badTiles[i] & 3));
	for (size_t i = 0; i < b.tiles.size(); ++i) REQUIRE((uint16_t(b.tiles[i]) >> 2) < nb);
	for (int i = 0; i < 5; ++i) REQUIRE_EQ(b.descs[size_t(i)].inverted, badFlags[i] & 3);
	for (int i = 0; i < 4; ++i) REQUIRE_EQ(b.descs[size_t(10 + i)].customBlendEdgeClass, -1);
	for (int i = 0; i < 3; ++i) REQUIRE_EQ(b.descs[size_t(20 + i)].horiz, 12);   // counted only
	REQUIRE_EQ(b.blends[3], s.numBlendedTiles + 5);   // the constructor resets these, the parser counts
	REQUIRE_EQ(b.threeWay[5], -1);
	// one log line per case (fewer than 16 of each kind here), then the summary
	const std::string& log = capturedLog();
	REQUIRE_EQ(countOccurrences(log, "BlendTileData: dangling tile #"), 8);
	REQUIRE_EQ(countOccurrences(log, "outside &0x3 (#"), 5);
	REQUIRE_EQ(countOccurrences(log, "edge classes (#"), 4);
	REQUIRE_EQ(countOccurrences(log, "not 0/1 (#"), 3);
	REQUIRE_EQ(countOccurrences(log, "BlendTileData: dangling blend index "), 2);
	REQUIRE_EQ(countOccurrences(log, "BlendTileData: dangling 3-way blend index "), 1);
	REQUIRE_EQ(countOccurrences(log, "BlendTileData: dangling cliff index "), 1);
	REQUIRE(log.find("dangling tile #8 at cell (11,4): tile 65535") != std::string::npos);
	REQUIRE(log.find("BlendTileData v15 20x10: bitmap tiles " + std::to_string(nb)) != std::string::npos);
	REQUIRE(log.find("dangling tiles 8 (clamped), flags outside &0x3 5 (masked), customBlendEdgeClass without edge classes 4 / "
		"out of range 4 (-> -1), direction bytes not 0/1 3, dangling blend/3-way/cliff 2/1/1") != std::string::npos);
	REQUIRE_EQ(countOccurrences(log, "BlendTileData v15 "), 1);

	// with edge classes: only indices >= numEdgeTextureClasses are reset, "without classes" stays 0
	BlendSpec e = makeBlendSpec("edges", 15, 12, 12, 0x2001u, 30);
	REQUIRE_EQ(e.edgeClasses.size(), size_t(2));
	e.descs[0].customBlendEdgeClass = 1;
	e.descs[1].customBlendEdgeClass = 2;
	e.descs[2].customBlendEdgeClass = 7;
	const LoadedBlend be = load(e);
	REQUIRE(be.ok);
	REQUIRE_EQ(be.stats.edgeClassWithoutClasses, 0);
	REQUIRE_EQ(be.stats.edgeClassOutOfRange, 2);
	REQUIRE_EQ(be.descs[0].customBlendEdgeClass, 1);
	REQUIRE_EQ(be.descs[1].customBlendEdgeClass, -1);
	REQUIRE_EQ(be.descs[2].customBlendEdgeClass, -1);
}

// Review #10, decision 1: per map, a log line for each of the first 16 cases of every kind, then only
// the summary with the totals.
TEST(blend_anomaly_log_first_16_of_each_kind)
{
	BlendSpec s = makeBlendSpec("many", 15, 20, 10, 0x2010u, 60);   // even seed: no edge classes
	const int nb = s.numBitmapTiles;
	for (int i = 0; i < 40; ++i) s.tiles[size_t(i * 5)] = uint16_t(nb * 4 + i);
	for (int i = 0; i < 20; ++i) s.descs[size_t(i)].inverted = uint8_t(8 + i);
	for (int i = 0; i < 18; ++i) s.descs[size_t(20 + i)].customBlendEdgeClass = i;
	for (int i = 0; i < 17; ++i) s.descs[size_t(40 + i)].vert = 2;
	for (int i = 0; i < 19; ++i) s.blends[size_t(i)] = uint32_t(s.numBlendedTiles + i);
	for (int i = 0; i < 21; ++i) s.threeWay[size_t(50 + i)] = 0xFFFFFFFFu;
	for (int i = 0; i < 22; ++i) s.cliff[size_t(100 + i)] = uint32_t(s.numCliffInfo + i);
	const LoadedBlend b = load(s);
	REQUIRE(b.ok);
	REQUIRE_EQ(b.stats.danglingTiles, 40);
	REQUIRE_EQ(b.stats.flagsOutside0x3, 20);
	REQUIRE_EQ(b.stats.edgeClassOutOfRange, 18);
	REQUIRE_EQ(b.stats.dirBytesNot01, 17);
	REQUIRE_EQ(b.stats.danglingBlend, 19);
	REQUIRE_EQ(b.stats.dangling3Way, 21);
	REQUIRE_EQ(b.stats.danglingCliff, 22);
	const std::string& log = capturedLog();
	REQUIRE_EQ(countOccurrences(log, "BlendTileData: dangling tile #"), 16);
	REQUIRE_EQ(countOccurrences(log, "outside &0x3 (#"), 16);
	REQUIRE_EQ(countOccurrences(log, "edge classes (#"), 16);
	REQUIRE_EQ(countOccurrences(log, "not 0/1 (#"), 16);
	REQUIRE_EQ(countOccurrences(log, "BlendTileData: dangling blend index "), 16);
	REQUIRE_EQ(countOccurrences(log, "BlendTileData: dangling 3-way blend index "), 16);
	REQUIRE_EQ(countOccurrences(log, "BlendTileData: dangling cliff index "), 16);
	REQUIRE(log.find("dangling tile #16 at cell") != std::string::npos);
	REQUIRE(log.find("dangling tile #17 ") == std::string::npos);
	REQUIRE(log.find("(#16)") != std::string::npos);
	REQUIRE(log.find("(#17)") == std::string::npos);
	REQUIRE_EQ(countOccurrences(log, "BlendTileData v15 20x10: "), 1);
	REQUIRE(log.find("dangling tiles 40 (clamped), flags outside &0x3 20 (masked), customBlendEdgeClass without edge classes 18 / "
		"out of range 18 (-> -1), direction bytes not 0/1 17, dangling blend/3-way/cliff 19/21/22; per-case lines: first 16 of each kind") !=
		std::string::npos);
	// lines = 7 kinds * 16 + the summary
	REQUIRE_EQ(countOccurrences(withoutChunkLines(log), "\n"), 7 * 16 + 1);
}

// Tables on the heap sized by the counts. 4096 / 65536 / 131072 are no longer loader limits:
// a count one past them loads when its records are in the chunk. A count whose records do not
// fit, or a bitmap-tile count above BLEND_BITMAP_TILE_CEILING (262144), is corrupt. The log
// line carries the three counters and dataLeft.
TEST(blend_limits_heap_tables)
{
	{
		const BlendSpec s = makeBlendSpec("above-old-blend", 15, 4, 4, 0x3000u, 65537, 4);
		const LoadedBlend b = load(s);
		requireMatchesSpec(b, s);
		REQUIRE_EQ(b.numBlendedTiles, 65537);
	}
	{
		const BlendSpec s = makeBlendSpec("above-old-cliff", 15, 4, 4, 0x3001u, 4, 131073);
		const LoadedBlend b = load(s);
		requireMatchesSpec(b, s);
		REQUIRE_EQ(b.numCliffInfo, 131073);
	}
	for (int which = 0; which < 2; ++which) {
		BlendSpec s = makeBlendSpec("nofit", 15, 4, 4, 0x3002u, 4, 4);
		if (which == 0) s.numBlendedTiles = 1000000;	// three descriptions in the chunk, not 999999
		else s.numCliffInfo = 1000000;
		capturedLog().clear();
		REQUIRE(!load(s).ok);
		const std::string& log = capturedLog();
		REQUIRE(log.find("counts out of range: numBitmapTiles ") != std::string::npos);
		REQUIRE(log.find(which == 0 ? "numBlendedTiles 1000000" : "numCliffInfo 1000000") != std::string::npos);
		REQUIRE(log.find("dataLeft ") != std::string::npos);
	}
	auto withClasses = [](std::vector<TexClassSpec> classes, int numBitmapTiles) {
		BlendSpec s = makeBlendSpec("classes", 15, 8, 8, 0x3002u, 4, 4);
		s.textureClasses = std::move(classes);
		s.numBitmapTiles = numBitmapTiles;
		for (uint16_t& t : s.tiles) t = 0;
		for (BlendDescSpec& d : s.descs) d.blendNdx = 0;
		for (CliffSpec& c : s.cliffs) c.tileIndex = 0;
		return s;
	};
	{
		// one past the old NUM_SOURCE_TILES; width 64 covers 4096 cells, numTiles the 4097th
		const LoadedBlend b = load(withClasses({{0, 4097, 64, "big"}}, 4097));
		REQUIRE(b.ok);
		REQUIRE_EQ(b.tileCapacity, 4097);
	}
	{
		// width^2 beyond numTiles and numBitmapTiles: the table covers what updateTileTexturePositions touches
		const LoadedBlend b = load(withClasses({{0, 8, 2, "a"}, {8, 4, 3, "b"}}, 12));
		REQUIRE(b.ok);
		REQUIRE_EQ(b.tileCapacity, 17);
	}
	{
		// class end past the old 4096 still loads; the ceiling is 262144
		const LoadedBlend b = load(withClasses({{4000, 100, 10, "a"}}, 100));
		REQUIRE(b.ok);
		REQUIRE_EQ(b.tileCapacity, 4100);
	}
	REQUIRE(!load(withClasses({{0, 1, 1, "a"}}, 262145)).ok);
	REQUIRE(capturedLog().find("numBitmapTiles 262145") != std::string::npos);
	REQUIRE(!load(withClasses({{262100, 100, 10, "a"}}, 100)).ok);
	REQUIRE(!load(withClasses({{-1, 4, 2, "a"}}, 4)).ok);
	REQUIRE(!load(withClasses({{0, -4, 2, "a"}}, 4)).ok);
	REQUIRE(!load(withClasses({{0, 4, 65, "a"}}, 4)).ok);
	std::vector<TexClassSpec> many(257, TexClassSpec{0, 1, 1, "x"});
	REQUIRE(!load(withClasses(many, 1)).ok);
	std::vector<TexClassSpec> most(256, TexClassSpec{0, 1, 1, "x"});
	REQUIRE(load(withClasses(most, 1)).ok);
}

// Index widths: 16-bit versions keep the upstream Short semantics (0xFFFF = -1, reset by the
// constructor), u32 values above INT_MAX become negative Int and are counted as dangling.
TEST(blend_index_widths_and_sign)
{
	BlendSpec s8 = makeBlendSpec("v8", 8, 6, 6, 0x4000u, 10, 10);
	s8.blends[0] = 0xFFFF;
	s8.cliff[1] = 0x8000;
	const LoadedBlend b8 = load(s8);
	REQUIRE(b8.ok);
	REQUIRE_EQ(b8.blends[0], -1);
	REQUIRE_EQ(b8.cliff[1], -32768);
	REQUIRE_EQ(b8.stats.danglingBlend, 1);
	REQUIRE_EQ(b8.stats.danglingCliff, 1);
	BlendSpec s15 = makeBlendSpec("v15", 15, 6, 6, 0x4001u, 50000, 10);
	s15.blends[0] = 0x80000000u;
	s15.blends[1] = 49999;
	const LoadedBlend b15 = load(s15);
	REQUIRE(b15.ok);
	REQUIRE(b15.blends[0] < 0);
	REQUIRE_EQ(b15.blends[1], 49999);
	REQUIRE_EQ(b15.stats.danglingBlend, 1);
}

// m_use3WayTerrainBlends == 0 (upstream INI switch): 3-way indices and FLIPPED_MASK cleared, as upstream.
TEST(blend_use3way_reset)
{
	BlendSpec s = makeBlendSpec("3way", 15, 16, 16, 0x5000u, 30);
	for (BlendDescSpec& d : s.descs) d.inverted = 3;
	const LoadedBlend b = load(s, 0);
	REQUIRE(b.ok);
	for (int32_t t : b.threeWay) REQUIRE_EQ(t, 0);
	for (const BlendDesc& d : b.descs) REQUIRE_EQ(d.inverted, 1);
}

// The 43 ZH maps in the repository (BlendTileData v8): load, nothing to fix.
TEST(blend_zh_repo_maps)
{
	const std::string dir = BFME_ZH_MAPS_DIR;
	if (dir.empty() || !std::filesystem::is_directory(dir)) SKIP("no ZH maps dir");
	int maps = 0;
	for (const auto& e : std::filesystem::recursive_directory_iterator(dir)) {
		std::string ext = e.path().extension().string();
		std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return char(std::tolower(c)); });
		if (!e.is_regular_file() || ext != ".map") continue;
		std::vector<uint8_t> bytes;
		REQUIRE(bfmeheights::readFile(e.path().string(), bytes));
		LoadOptions opt;
		opt.consumers = true;
		const LoadedBlend b = loadBlend(bytes, opt);
		REQUIRE(b.ok);
		REQUIRE(b.atEndOfChunk);
		REQUIRE_EQ(b.blendVersion, 8);
		REQUIRE_EQ(b.stats.danglingTiles + b.stats.flagsOutside0x3 + b.stats.edgeClassOutOfRange + b.stats.danglingBlend +
			b.stats.dangling3Way + b.stats.danglingCliff, 0);
		REQUIRE(!b.hasMask[0] && !b.hasMask[1] && !b.hasMask[2] && !b.hasMask[3]);
		REQUIRE(b.tileCapacity >= b.numBitmapTiles);
		++maps;
	}
	REQUIRE(maps > 0);
	std::printf("blend_zh_repo_maps: %d maps\n", maps);
}
