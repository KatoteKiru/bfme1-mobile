// BFME1 Mobile — tests (E1-25 review #10)
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The loading constructor WorldHeightMap(ChunkInputStream*, Bool) on synthetic maps, in the release
// configuration (no DEBUG_LOGGING) and with counting operator new/delete (CountingNew.h):
// - every way the chunk parsers can throw leaves no allocated block behind (review #10: cleanup before
//   the exception leaves the constructor), checked by the block counter in every build and by LSan in
//   -DBFME_TESTS_ASAN=ON builds;
// - out of memory at every single allocation of a load, including a second BlendTileData chunk (the
//   arrays deleted before the new MSGNEW must not be deleted again: double free under ASan / glibc);
// - the release log: one stderr summary line per map with anomalies, nothing for a clean map.
#include "../../heights/harness/TestHarness.h"
#include "../../heights/src/MapLoader.h"
#include "CountingNew.h"
#include "CtorLoader.h"
#include "SyntheticBlend.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <functional>
#include <unistd.h>

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

uint32_t rd32(const std::vector<uint8_t>& b, size_t at)
{
	return uint32_t(b[at]) | uint32_t(b[at + 1]) << 8 | uint32_t(b[at + 2]) << 16 | uint32_t(b[at + 3]) << 24;
}

void wr32(std::vector<uint8_t>& b, size_t at, uint32_t v)
{
	for (int i = 0; i < 4; ++i) b[at + size_t(i)] = uint8_t(v >> (8 * i));
}

// Payload offset of the nth chunk with symbol id `id` (2 = HeightMapData, 3 = BlendTileData in the
// files of SyntheticBlend.h).
size_t chunkPayload(const std::vector<uint8_t>& f, uint32_t id, int nth = 0)
{
	size_t p = 8;
	const uint32_t symbols = rd32(f, 4);
	for (uint32_t i = 0; i < symbols; ++i) p += 1 + f[p] + 4;
	while (p + 10 <= f.size()) {
		const uint32_t cid = rd32(f, p);
		const uint32_t size = rd32(f, p + 6);
		if (cid == id && nth-- == 0) return p + 10;
		p += 10 + size;
	}
	throw bfmetest::Failure("chunk not found");
}

// The map of `a` with a second BlendTileData chunk (`b`, same size) after the first one.
std::vector<uint8_t> withSecondBlendChunk(const BlendSpec& a, const BlendSpec& b)
{
	std::vector<uint8_t> f = writeBlendMap(a);
	const std::vector<uint8_t> payload = blendPayload(b);
	ByteWriter w;
	w.u32(3); w.u16(uint16_t(b.version)); w.i32(int32_t(payload.size()));
	w.raw(payload.data(), payload.size());
	f.insert(f.end(), w.bytes.begin(), w.bytes.end());
	return f;
}

std::string captureStderr(const std::function<void()>& body)
{
	std::fflush(stderr);
	FILE* tmp = std::tmpfile();
	REQUIRE(tmp != nullptr);
	const int saved = dup(2);
	dup2(fileno(tmp), 2);
	try {
		body();
	} catch (...) {
		std::fflush(stderr);
		dup2(saved, 2);
		close(saved);
		std::fclose(tmp);
		throw;
	}
	std::fflush(stderr);
	dup2(saved, 2);
	close(saved);
	std::string out;
	std::rewind(tmp);
	char buf[4096];
	for (size_t n; (n = std::fread(buf, 1, sizeof(buf), tmp)) > 0;) out.append(buf, n);
	std::fclose(tmp);
	return out;
}

// A map that loads and has anomalies of every kind, more than 16 of each.
BlendSpec anomalySpec()
{
	BlendSpec s = makeBlendSpec("anomalies", 15, 20, 10, 0x2000u, 60);   // even seed: no edge classes
	const int nb = s.numBitmapTiles;
	for (int i = 0; i < 40; ++i) s.tiles[size_t(i * 5)] = uint16_t(nb * 4 + i);
	for (int i = 0; i < 20; ++i) s.descs[size_t(i)].inverted = uint8_t(8 + i);
	for (int i = 0; i < 18; ++i) s.descs[size_t(20 + i)].customBlendEdgeClass = i;
	for (int i = 0; i < 17; ++i) s.descs[size_t(40 + i)].vert = 2;
	for (int i = 0; i < 19; ++i) s.blends[size_t(i)] = uint32_t(s.numBlendedTiles + i);
	for (int i = 0; i < 21; ++i) s.threeWay[size_t(50 + i)] = 0xFFFFFFFFu;
	for (int i = 0; i < 22; ++i) s.cliff[size_t(100 + i)] = uint32_t(s.numCliffInfo + i);
	return s;
}

void requireThrowsCorruptWithoutLeak(const std::vector<uint8_t>& map, bool logicalDataOnly, const char* what)
{
	bfmetest::checkCount() += 2;
	const long before = bfmealloc::liveBlocks();
	const CtorResult r = constructMap(map, logicalDataOnly);
	if (!r.threw || !r.errorCode || r.error != errorCorruptFileFormat())
		throw bfmetest::Failure(std::string(what) + ": expected ERROR_CORRUPT_FILE_FORMAT from the constructor");
	if (bfmealloc::liveBlocks() != before)
		throw bfmetest::Failure(std::string(what) + ": " + std::to_string(bfmealloc::liveBlocks() - before) +
			" blocks leaked by the failed constructor");
}

} // namespace

// Review #10: every throw of the chunk parsers (and the constructor's own throw) frees what the
// constructor allocated so far. Without the fix each case leaks 1..13 blocks.
TEST(ctor_corrupt_map_frees_arrays)
{
	const BlendSpec good = makeBlendSpec("good", 15, 20, 10, 0x6000u, 60, 30);
	REQUIRE(constructMap(writeBlendMap(good), false).constructed);   // warm-up (static alpha tiles etc.)
	REQUIRE(constructMap(writeBlendMap(good), true).constructed);
	const long baseline = bfmealloc::liveBlocks();
	{
		const CtorResult r = constructMap(writeBlendMap(good), false);
		REQUIRE(r.constructed);
		REQUIRE_EQ(r.stats.version, 15);
		REQUIRE_EQ(bfmealloc::liveBlocks(), baseline);
		REQUIRE(constructMap(withSecondBlendChunk(good, makeBlendSpec("good2", 15, 20, 10, 0x6001u, 70, 40)), false).constructed);
		REQUIRE_EQ(bfmealloc::liveBlocks(), baseline);
	}
	const std::vector<uint8_t> plain = writeBlendMap(good);
	{
		std::vector<uint8_t> f = plain;   // HeightMapData: dataSize != w*h (after m_data is allocated)
		const size_t hp = chunkPayload(f, 2);
		wr32(f, hp + 24, rd32(f, hp + 24) + 1);
		requireThrowsCorruptWithoutLeak(f, false, "HeightMapData dataSize");
		requireThrowsCorruptWithoutLeak(f, true, "HeightMapData dataSize (logicalDataOnly, ParseSizeOnly)");
	}
	{
		BlendSpec s = good;   // v16: rejected before reading (heights and seismic arrays allocated)
		s.version = 16;
		requireThrowsCorruptWithoutLeak(writeBlendMap(s), false, "BlendTileData v16");
	}
	{
		std::vector<uint8_t> f = plain;   // BlendTileData cell count != HeightMapData
		wr32(f, chunkPayload(f, 3), uint32_t(good.width * good.height + 1));
		requireThrowsCorruptWithoutLeak(f, false, "BlendTileData cell count");
	}
	{
		BlendSpec s = good;   // counts out of range: cell arrays and the four masks allocated
		s.numBlendedTiles = 65537;
		s.descs.resize(65536);
		requireThrowsCorruptWithoutLeak(writeBlendMap(s), false, "numBlendedTiles 65537");
	}
	{
		BlendSpec s = good;   // bad texture class header
		s.textureClasses[2].firstTile = -1;
		requireThrowsCorruptWithoutLeak(writeBlendMap(s), false, "texture class firstTile -1");
	}
	{
		BlendSpec s = good;   // bad edge texture class header (v4+)
		TexClassSpec e;
		e.firstTile = 0; e.numTiles = 4; e.width = 65; e.name = "BadEdge";
		s.edgeClasses.push_back(e);
		s.numEdgeTiles = 4;
		requireThrowsCorruptWithoutLeak(writeBlendMap(s), false, "edge texture class width 65");
	}
	{
		BlendSpec s = good;   // FLAG_VAL of the last description: everything incl. the heap tables allocated
		s.descs.back().flag = 0x12345678u;
		requireThrowsCorruptWithoutLeak(writeBlendMap(s), false, "blend description flag");
		// the same in a second BlendTileData chunk: the first chunk's arrays replaced, then the throw
		requireThrowsCorruptWithoutLeak(withSecondBlendChunk(good, s), false, "second BlendTileData chunk, flag");
		BlendSpec v16 = good;
		v16.version = 16;
		requireThrowsCorruptWithoutLeak(withSecondBlendChunk(good, v16), false, "second BlendTileData chunk, v16");
	}
	{
		// not a chunk file: DataChunkInput::parse() returns false, the constructor's own throw
		std::vector<uint8_t> f = plain;
		std::memcpy(f.data(), "XXXX", 4);
		requireThrowsCorruptWithoutLeak(f, false, "no CkMp header");
	}
}

// Review #10: out of memory at each allocation of a load in turn. A failure after the delete[] of the
// old cell arrays (second BlendTileData chunk) must not lead to a second delete[] in the cleanup.
TEST(ctor_out_of_memory_at_every_allocation)
{
	struct Case { std::string name; std::vector<uint8_t> map; bool logicalDataOnly; };
	const BlendSpec v15 = makeBlendSpec("v15", 15, 20, 10, 0x7000u, 60, 30);
	const BlendSpec v15b = makeBlendSpec("v15b", 15, 20, 10, 0x7001u, 70, 40);
	const BlendSpec v8 = makeBlendSpec("v8", 8, 17, 9, 0x7002u, 30, 10);
	const BlendSpec v8b = makeBlendSpec("v8b", 8, 17, 9, 0x7003u, 20, 5);
	const BlendSpec v7 = makeBlendSpec("v7", 7, 17, 9, 0x7004u, 20, 5);
	const Case cases[] = {
		{"v15", writeBlendMap(v15), false},
		{"v15 + second v15 chunk", withSecondBlendChunk(v15, v15b), false},
		{"v8 + second v8 chunk (16-bit index buffers)", withSecondBlendChunk(v8, v8b), false},
		{"v7 (cliff state buffer)", writeBlendMap(v7), false},
		{"v15 logicalDataOnly", writeBlendMap(v15), true},
	};
	for (const Case& c : cases) {
		REQUIRE(constructMap(c.map, c.logicalDataOnly).constructed);
		const long baseline = bfmealloc::liveBlocks();
		long n = 1;
		for (;; ++n) {
			bfmealloc::armFailure(n);
			const CtorResult r = constructMap(c.map, c.logicalDataOnly);
			const bool fired = bfmealloc::failureFired();
			bfmealloc::armFailure(0);
			if (!r.constructed) releaseStaticAlphaTiles();   // in case it failed inside setupAlphaTiles()
			if (bfmealloc::liveBlocks() != baseline)
				throw bfmetest::Failure(c.name + ": allocation " + std::to_string(n) + " failed, " +
					std::to_string(bfmealloc::liveBlocks() - baseline) + " blocks leaked");
			if (!fired) {
				REQUIRE(r.constructed);
				break;
			}
			if (!r.threw || !r.errorCode || r.error != errorOutOfMemory())
				throw bfmetest::Failure(c.name + ": allocation " + std::to_string(n) + " failed, expected ERROR_OUT_OF_MEMORY");
			REQUIRE(n < 100000);
		}
		REQUIRE(n > 10);
		std::printf("ctor_out_of_memory_at_every_allocation: %s: %ld allocations, each failed once, no leak\n", c.name.c_str(), n - 1);
	}
}

// Review #10, decision 2: without DEBUG_LOGGING (release) one summary line per map in the release log
// (stderr), only when a counter is not zero; DEBUG_LOG (per-case lines and the debug summary) is compiled out.
TEST(ctor_release_log_one_summary_line)
{
	const BlendSpec s = anomalySpec();
	CtorResult r;
	std::string err = withoutChunkLines(captureStderr([&] { r = constructMap(writeBlendMap(s), false); }));
	REQUIRE(r.constructed);
	REQUIRE_EQ(r.stats.danglingTiles, 40);
	REQUIRE_EQ(r.stats.flagsOutside0x3, 20);
	REQUIRE_EQ(r.stats.edgeClassWithoutClasses, 18);
	REQUIRE_EQ(r.stats.edgeClassOutOfRange, 18);
	REQUIRE_EQ(r.stats.dirBytesNot01, 17);
	REQUIRE_EQ(r.stats.danglingBlend, 19);
	REQUIRE_EQ(r.stats.dangling3Way, 21);
	REQUIRE_EQ(r.stats.danglingCliff, 22);
	REQUIRE_EQ(err, std::string("WARNING: BlendTileData v15 20x10: dangling tiles 40 (clamped), flags outside &0x3 20 (masked), "
		"customBlendEdgeClass without edge classes 18 / out of range 18 (-> -1), direction bytes not 0/1 17, "
		"dangling blend/3-way/cliff 19/21/22\n"));

	// a single anomaly is enough for the line
	BlendSpec one = makeBlendSpec("one", 15, 20, 10, 0x2002u, 60);
	one.descs[3].horiz = 7;
	err = withoutChunkLines(captureStderr([&] { r = constructMap(writeBlendMap(one), false); }));
	REQUIRE(r.constructed);
	REQUIRE_EQ(countOccurrences(err, "BlendTileData"), 1);
	REQUIRE(err.find("direction bytes not 0/1 1, dangling blend/3-way/cliff 0/0/0\n") != std::string::npos);

	// clean maps: nothing
	err = withoutChunkLines(captureStderr([&] { r = constructMap(writeBlendMap(makeBlendSpec("clean", 15, 20, 10, 0x2004u, 60)), false); }));
	REQUIRE(r.constructed);
	REQUIRE_EQ(err, std::string());
	for (const BlendSpec& zh : zhSyntheticBlendMaps()) {
		err = withoutChunkLines(captureStderr([&] { r = constructMap(writeBlendMap(zh), false); }));
		REQUIRE(r.constructed);
		REQUIRE_EQ(err, std::string());
	}
}

// The 43 ZH maps in the repository through the full loading constructor: load, no release log line, no leak.
TEST(ctor_zh_repo_maps)
{
	const std::string dir = BFME_ZH_MAPS_DIR;
	if (dir.empty() || !std::filesystem::is_directory(dir)) SKIP("no ZH maps dir");
	int maps = 0;
	bool warm = false;
	for (const auto& e : std::filesystem::recursive_directory_iterator(dir)) {
		std::string ext = e.path().extension().string();
		std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return char(std::tolower(c)); });
		if (!e.is_regular_file() || ext != ".map") continue;
		std::vector<uint8_t> bytes, plain;
		std::string error;
		REQUIRE(bfmeheights::readFile(e.path().string(), bytes));
		REQUIRE(bfmeheights::decompressMap(bytes, plain, error));
		if (!warm) {
			REQUIRE(constructMap(plain, false).constructed);   // warm-up
			warm = true;
		}
		CtorResult r;
		long before = 0, after = 0;
		const std::string err = withoutChunkLines(captureStderr([&] {
			before = bfmealloc::liveBlocks();
			r = constructMap(plain, false);
			after = bfmealloc::liveBlocks();
		}));
		if (!r.constructed) throw bfmetest::Failure(e.path().string() + ": constructor failed");
		REQUIRE_EQ(r.stats.version, 8);
		REQUIRE_EQ(err, std::string());
		REQUIRE_EQ(after, before);
		++maps;
	}
	REQUIRE(maps > 0);
	std::printf("ctor_zh_repo_maps: %d maps\n", maps);
}
