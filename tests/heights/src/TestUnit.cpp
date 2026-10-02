// BFME1 Mobile — tests (E1-25: 16-bit terrain heights)
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Unit tests of the real WorldHeightMap parser on synthetic maps (no game data).
#include "PreRTS.h"

#include "Common/MapObject.h"
#include "Common/MapReaderWriterInfo.h"
#include "W3DDevice/GameClient/WorldHeightMap.h"

#include "../harness/TestHarness.h"
#include "MapLoader.h"
#include "SyntheticMaps.h"

#include <cstring>
#include <filesystem>

using namespace bfmeheights;

namespace {

uint32_t bits(float f)
{
	uint32_t b;
	std::memcpy(&b, &f, 4);
	return b;
}

// What the upstream parser leaves in m_data / m_width for a map (v1 maps are halved in place;
// the full parser keeps m_width, the size-only parser updates it: upstream behaviour, unchanged).
struct Expected { int width, height; std::vector<int> samples; };
Expected expectedSamples(const SyntheticMap& m, ParseMode mode)
{
	Expected e{m.width, m.height, {}};
	e.samples.assign(m.samples.begin(), m.samples.end());
	if (m.version == 1) {
		const int nw = (m.width + 1) / 2, nh = (m.height + 1) / 2;
		for (int i = 0; i < nh; ++i)
			for (int j = 0; j < nw; ++j)
				e.samples[size_t(i * nw + j)] = e.samples[size_t(2 * i * m.width + 2 * j)];
		if (mode == ParseMode::SizeOnly) { e.width = nw; e.height = nh; }
	}
	e.samples.resize(size_t(e.width * e.height));
	return e;
}

SyntheticMap flatMap(int version, int w, int h, uint16_t value)
{
	SyntheticMap m;
	m.version = version;
	m.width = w;
	m.height = h;
	m.samples.assign(size_t(w * h), value);
	return m;
}

} // namespace

TEST(constants_u16_scale)
{
	REQUIRE_EQ(sizeof(HeightSampleType), size_t(2));
	REQUIRE_EQ(K_HEIGHT_MAP_VERSION_5, 5);
	REQUIRE_EQ(WorldHeightMap::getMaxHeightValue(), 0xFFFF);
	REQUIRE_EQ(WorldHeightMap::getMinHeightValue(), 0);
	REQUIRE_EQ(MAP_HEIGHT_ZH_STEP, 1 << MAP_HEIGHT_ZH_SHIFT);
	REQUIRE_EQ(bits(MAP_HEIGHT_SCALE), bits(10.0f / 256.0f));
	REQUIRE_EQ(bits(MAP_HEIGHT_SCALE_ZH), bits(10.0f / 16.0f));
	REQUIRE_EQ(bits(MAP_HEIGHT_SCALE * MAP_HEIGHT_ZH_STEP), bits(MAP_HEIGHT_SCALE_ZH));
	REQUIRE(MAP_HEIGHT_SCALE == 0.0390625f);   // OpenSAGE HeightMapData.VerticalScale for v5
}

// ZH v1..v4: every sample becomes u8 << 4, and the world height getHeight()*MAP_HEIGHT_SCALE is
// bit-identical to the upstream u8 * (MAP_XY_FACTOR/16).
TEST(zh_v1_v4_samples_widened_bit_identical_world)
{
	for (const SyntheticMap& m : zhSyntheticMaps()) {
		const std::vector<uint8_t> file = writeMapFile(m);
		for (ParseMode mode : {ParseMode::Full, ParseMode::SizeOnly}) {
			LoadedHeights h = loadHeights(file, mode, false);
			REQUIRE(h.ok);
			REQUIRE_EQ(h.version, m.version);
			const Expected e = expectedSamples(m, mode);
			REQUIRE_EQ(h.width, e.width);
			REQUIRE_EQ(h.height, e.height);
			REQUIRE_EQ(h.border, m.version >= 3 ? m.border : 0);
			REQUIRE_EQ(h.raw.size(), e.samples.size());
			for (size_t i = 0; i < e.samples.size(); ++i) {
				REQUIRE_EQ(h.raw[i], e.samples[i] << 4);
				const UnsignedByte zh = UnsignedByte(e.samples[i]);
				REQUIRE_EQ(bits(h.world[i]), bits(zh * (MAP_XY_FACTOR / 16.0f)));
			}
			// getHeight() outside the map returns 0 (v1 size-only parse keeps the full m_dataSize: upstream)
			if (!(m.version == 1 && mode == ParseMode::SizeOnly))
				REQUIRE_EQ(h.outOfRange[0] + h.outOfRange[1] + h.outOfRange[2] + h.outOfRange[3], 0);
		}
	}
}

TEST(zh_all_256_values)
{
	const std::vector<SyntheticMap> maps = zhSyntheticMaps();
	const SyntheticMap& all = maps.back();
	REQUIRE_EQ(all.name, std::string("synthetic_zh_v4_all_values"));
	LoadedHeights h = loadHeights(writeMapFile(all), ParseMode::Full, false);
	REQUIRE(h.ok);
	for (int v = 0; v < 256; ++v) {
		REQUIRE_EQ(h.raw[size_t(v)], v * 16);
		REQUIRE_EQ(bits(h.world[size_t(v)]), bits(UnsignedByte(v) * 0.625f));
	}
	REQUIRE_EQ(h.raw[255], 4080);
}

// BFME1 v5: u16 little-endian samples are stored as is, world = raw * 10/256 (exact in float).
TEST(bfme_v5_u16_samples_exact)
{
	SyntheticMap m;
	m.version = 5;
	m.width = 300;
	m.height = 221;
	m.border = 30;
	m.samples = makeTerrain(m.width, m.height, 0xFFFF, 0xBF3E1u);
	const uint16_t special[] = {0, 1, 2, 15, 16, 17, 255, 256, 4080, 4081, 6144, 0x7FFF, 0x8000, 0xFFFE, 0xFFFF};
	for (size_t i = 0; i < sizeof(special) / sizeof(special[0]); ++i) m.samples[i + 1] = special[i];
	for (ParseMode mode : {ParseMode::Full, ParseMode::SizeOnly}) {
		LoadedHeights h = loadHeights(writeMapFile(m), mode, false);
		REQUIRE(h.ok);
		REQUIRE_EQ(h.version, 5);
		REQUIRE_EQ(h.width, m.width);
		REQUIRE_EQ(h.height, m.height);
		REQUIRE_EQ(h.border, m.border);
		REQUIRE_EQ(h.raw.size(), m.samples.size());
		for (size_t i = 0; i < m.samples.size(); ++i) {
			REQUIRE_EQ(h.raw[i], int(m.samples[i]));
			REQUIRE_EQ(double(h.world[i]), double(m.samples[i]) * 0.0390625);
		}
	}
}

TEST(corrupt_height_chunk_rejected)
{
	SyntheticMap m = flatMap(5, 4, 4, 100);
	std::vector<uint8_t> file = writeMapFile(m);
	// dataSize field (last i32 before the samples) != width*height -> ERROR_CORRUPT_FILE_FORMAT
	const size_t dataSizePos = file.size() - m.samples.size() * 2 - 4;
	file[dataSizePos] = 15;
	REQUIRE(!loadHeights(file, ParseMode::Full, false).ok);
	REQUIRE(!loadHeights(file, ParseMode::SizeOnly, false).ok);
	std::vector<uint8_t> notChunky(file.begin() + 4, file.end());
	REQUIRE(!loadHeights(notChunky, ParseMode::Full, false).ok);
}

// Pathfinding cliff flag (WorldHeightMap::setCellCliffFlagFromHeights): cliff if the cell's height
// range > 9.8 world units. ZH: a step of 15 u8 (9.375) is not a cliff, 16 (10.0) is, as upstream.
// BFME: the threshold now has u16 resolution: 250 raw (9.765625) no, 251 (9.8046875) yes.
TEST(cliff_flags_threshold)
{
	struct Case { int version; uint16_t low, high; bool cliff; };
	const Case cases[] = {{4, 100, 115, false}, {4, 100, 116, true}, {2, 0, 255, true}, {4, 7, 7, false},
	                      {5, 1000, 1250, false}, {5, 1000, 1251, true}, {5, 0, 0xFFFF, true}};
	for (const Case& c : cases) {
		SyntheticMap m = flatMap(c.version, 3, 3, c.low);
		m.samples[4] = c.high;   // centre sample: all four cells touch it
		LoadedHeights h = loadHeights(writeMapFile(m), ParseMode::Full, true);
		REQUIRE(h.ok);
		REQUIRE_EQ(h.cliff.size(), size_t(4));
		for (uint8_t f : h.cliff) REQUIRE_EQ(f != 0, c.cliff);
	}
}

// The ZH community maps that are already committed (GeneralsOnlineRelease/.../Maps): all v<=4,
// every sample a multiple of 16 and <= 4080 after widening.
TEST(zh_repo_maps_widened)
{
	const std::string dir = BFME_ZH_MAPS_DIR;
	if (dir.empty() || !std::filesystem::is_directory(dir)) SKIP("no ZH maps dir (BFME_ZH_MAPS_DIR)");
	int maps = 0;
	for (const auto& e : std::filesystem::recursive_directory_iterator(dir)) {
		if (!e.is_regular_file() || e.path().extension() != ".map") continue;
		std::vector<uint8_t> bytes;
		REQUIRE(readFile(e.path().string(), bytes));
		LoadedHeights h = loadHeights(bytes, ParseMode::Full, false);
		REQUIRE(h.ok);
		REQUIRE(h.version >= 1 && h.version <= 4);
		for (int r : h.raw) {
			REQUIRE_EQ(r % 16, 0);
			REQUIRE(r <= 4080);
		}
		++maps;
	}
	REQUIRE(maps > 0);
	std::printf("  %d ZH maps\n", maps);
}
