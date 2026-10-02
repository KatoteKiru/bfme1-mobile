// BFME1 Mobile — tests (E1-25: 16-bit terrain heights)
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Integration with the original BFME1 data (read only, nothing is copied or committed):
//   BFME_GAMEDATA         directory with maps.big
//   BFME_ORACLE_DIR       tools/oracle/out (maps.big.maps.json: OpenSAGE min/max per map)
//   BFME_ORACLE_DUMP_DIR  optional, `oracle.sh map ... --dump DIR` output (<map>.heights.u16le.bin)
// Without them the tests are skipped (exit code 77).
#include "PreRTS.h"

#include "Common/MapObject.h"

#include "../harness/MiniJson.h"
#include "../harness/TestHarness.h"
#include "MapLoader.h"

#include <cstdlib>
#include <filesystem>
#include <map>

using namespace bfmeheights;
using bfmetest::Json;

namespace {

std::string env(const char* name)
{
	const char* v = std::getenv(name);
	return v ? std::string(v) : std::string();
}

struct OracleMaps {
	std::string bigPath;
	Json json;
	std::map<std::string, BigEntry> entries;   // normalized name -> entry
};

OracleMaps openOracle()
{
	const std::string gamedata = env("BFME_GAMEDATA"), oracle = env("BFME_ORACLE_DIR");
	if (gamedata.empty()) SKIP("BFME_GAMEDATA not set");
	if (oracle.empty()) SKIP("BFME_ORACLE_DIR not set");
	OracleMaps o;
	o.bigPath = gamedata + "/maps.big";
	if (!std::filesystem::exists(o.bigPath)) SKIP("no maps.big in BFME_GAMEDATA");
	const std::string jsonPath = oracle + "/maps.big.maps.json";
	if (!std::filesystem::exists(jsonPath)) SKIP("no maps.big.maps.json in BFME_ORACLE_DIR");
	o.json = Json::parseFile(jsonPath);
	std::vector<BigEntry> entries;
	std::string error;
	if (!readBigDirectory(o.bigPath, entries, error)) throw bfmetest::Failure(error);
	for (const BigEntry& e : entries) o.entries[normalizeBigName(e.name)] = e;
	return o;
}

LoadedHeights loadEntry(const OracleMaps& o, const std::string& entry, ParseMode mode)
{
	auto it = o.entries.find(normalizeBigName(entry));
	if (it == o.entries.end()) throw bfmetest::Failure("entry not in maps.big: " + entry);
	std::vector<uint8_t> bytes;
	if (!readBigEntry(o.bigPath, it->second, bytes)) throw bfmetest::Failure("cannot read " + entry);
	LoadedHeights h = loadHeights(bytes, mode, false);
	if (!h.ok) throw bfmetest::Failure(entry + ": " + h.error);
	return h;
}

std::string dumpName(const std::string& entry)   // oracle --dump naming: basename, ' ' -> '_'
{
	std::string base = entry.substr(entry.find_last_of("\\/") + 1);
	base = base.substr(0, base.size() - 4);
	for (char& c : base) if (c == ' ') c = '_';
	return base;
}

} // namespace

// All 115 maps: version 5, size, border, min/max raw and min/max world height equal to OpenSAGE.
TEST(gamedata_bfme1_maps_minmax_vs_oracle)
{
	OracleMaps o = openOracle();
	const Json& maps = o.json["maps"];
	REQUIRE_EQ(maps.arr.size(), size_t(115));
	REQUIRE_EQ(o.json["mapCount"].asInt(), 115LL);
	long long samples = 0;
	int minAll = 0xFFFF, maxAll = 0;
	for (const Json& m : maps.arr) {
		const std::string entry = m["source"]["entry"].str;
		const Json& hm = m["heightMap"];
		for (ParseMode mode : {ParseMode::Full, ParseMode::SizeOnly}) {
			LoadedHeights h = loadEntry(o, entry, mode);
			REQUIRE_EQ(h.version, int(hm["version"].asInt()));
			REQUIRE_EQ(h.version, 5);
			REQUIRE_EQ(h.width, int(hm["width"].asInt()));
			REQUIRE_EQ(h.height, int(hm["height"].asInt()));
			REQUIRE_EQ(h.border, int(hm["borderWidth"].asInt()));
			REQUIRE_EQ(h.raw.size(), size_t(hm["area"].asInt()));
			int mn = 0xFFFF, mx = 0;
			float wmn = 1e30f, wmx = -1e30f;
			for (size_t i = 0; i < h.raw.size(); ++i) {
				mn = std::min(mn, h.raw[i]);
				mx = std::max(mx, h.raw[i]);
				wmn = std::min(wmn, h.world[i]);
				wmx = std::max(wmx, h.world[i]);
			}
			REQUIRE_EQ(mn, int(hm["minRaw"].asInt()));
			REQUIRE_EQ(mx, int(hm["maxRaw"].asInt()));
			REQUIRE_EQ(double(wmn), hm["minZ"].num);
			REQUIRE_EQ(double(wmx), hm["maxZ"].num);
			REQUIRE_EQ(double(MAP_HEIGHT_SCALE), hm["verticalScale"].num);
			if (mode == ParseMode::Full) {
				samples += (long long)h.raw.size();
				minAll = std::min(minAll, mn);
				maxAll = std::max(maxAll, mx);
			}
		}
	}
	std::printf("  115 maps, %lld samples, raw %d..%d, world %.4f..%.4f\n", samples, minAll, maxAll,
		minAll * (double)MAP_HEIGHT_SCALE, maxAll * (double)MAP_HEIGHT_SCALE);
}

// Every sample of every map against the OpenSAGE Elevations dump (u16 LE, row-major y,x).
TEST(gamedata_bfme1_maps_all_samples_vs_oracle_dump)
{
	OracleMaps o = openOracle();
	const std::string dumpDir = env("BFME_ORACLE_DUMP_DIR");
	if (dumpDir.empty()) SKIP("BFME_ORACLE_DUMP_DIR not set (oracle.sh map ... --dump DIR)");
	long long compared = 0;
	int maps = 0;
	for (const Json& m : o.json["maps"].arr) {
		const std::string entry = m["source"]["entry"].str;
		const std::string bin = dumpDir + "/" + dumpName(entry) + ".heights.u16le.bin";
		std::vector<uint8_t> dump;
		if (!readFile(bin, dump)) throw bfmetest::Failure("missing oracle dump " + bin);
		LoadedHeights h = loadEntry(o, entry, ParseMode::Full);
		REQUIRE_EQ(dump.size(), h.raw.size() * 2);
		for (size_t i = 0; i < h.raw.size(); ++i) {
			const int oracle = int(dump[2 * i]) | int(dump[2 * i + 1]) << 8;
			if (h.raw[i] != oracle) {
				throw bfmetest::Failure(entry + ": sample " + std::to_string(i) + " engine " + std::to_string(h.raw[i]) +
					" != OpenSAGE " + std::to_string(oracle));
			}
			REQUIRE(double(h.world[i]) == oracle * 0.0390625);
		}
		compared += (long long)h.raw.size();
		++maps;
	}
	REQUIRE_EQ(maps, 115);
	std::printf("  %d maps, %lld samples identical to OpenSAGE\n", maps, compared);
}
