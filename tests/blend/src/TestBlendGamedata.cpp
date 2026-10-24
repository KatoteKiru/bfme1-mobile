// BFME1 Mobile — tests (E1-25: BlendTileData v14/v15)
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Integration with the original BFME1 data (read only, nothing is copied or committed):
//   BFME_GAMEDATA             directory with maps.big
//   BFME_ORACLE_DIR           tools/oracle/out (maps.big.maps.json)
//   BFME_ORACLE_DUMP_DIR      `oracle.sh map ... --dump DIR -o DIR/json/<map>.json` output (per-map JSON with
//                             the sha256 of the chunk payloads and of the OpenSAGE-decoded arrays)
//   BFME_DATACHECK_MAPS_JSON  tools/datacheck/out/maps.json (reference anomaly counters)
// Without them the tests are skipped (exit code 77).
#include "../../heights/harness/MiniJson.h"
#include "../../heights/harness/TestHarness.h"
#include "../../heights/src/MapLoader.h"
#include "BlendLoader.h"
#include "Sha256.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <map>
#include <set>

using namespace bfmeblend;
using bfmetest::Json;

namespace {

std::string env(const char* name)
{
	const char* v = std::getenv(name);
	return v ? std::string(v) : std::string();
}

struct Data {
	std::string bigPath, dumpDir;
	Json oracle, datacheck;
	std::map<std::string, bfmeheights::BigEntry> entries;
};

Data openData(bool needDumps)
{
	Data d;
	const std::string gamedata = env("BFME_GAMEDATA"), oracle = env("BFME_ORACLE_DIR");
	if (gamedata.empty()) SKIP("BFME_GAMEDATA not set");
	if (oracle.empty()) SKIP("BFME_ORACLE_DIR not set");
	d.bigPath = gamedata + "/maps.big";
	if (!std::filesystem::exists(d.bigPath)) SKIP("no maps.big in BFME_GAMEDATA");
	d.oracle = Json::parseFile(oracle + "/maps.big.maps.json");
	if (needDumps) {
		d.dumpDir = env("BFME_ORACLE_DUMP_DIR");
		const std::string dc = env("BFME_DATACHECK_MAPS_JSON");
		if (d.dumpDir.empty() || !std::filesystem::is_directory(d.dumpDir + "/json")) SKIP("BFME_ORACLE_DUMP_DIR/json not available");
		if (dc.empty() || !std::filesystem::exists(dc)) SKIP("BFME_DATACHECK_MAPS_JSON not available");
		d.datacheck = Json::parseFile(dc);
	}
	std::vector<bfmeheights::BigEntry> entries;
	std::string error;
	if (!bfmeheights::readBigDirectory(d.bigPath, entries, error)) throw bfmetest::Failure(error);
	for (const auto& e : entries) d.entries[bfmeheights::normalizeBigName(e.name)] = e;
	return d;
}

std::vector<uint8_t> readEntry(const Data& d, const std::string& entry)
{
	auto it = d.entries.find(bfmeheights::normalizeBigName(entry));
	if (it == d.entries.end()) throw bfmetest::Failure("entry not in maps.big: " + entry);
	std::vector<uint8_t> bytes;
	if (!bfmeheights::readBigEntry(d.bigPath, it->second, bytes)) throw bfmetest::Failure("cannot read " + entry);
	return bytes;
}

std::string baseName(const std::string& entry)   // "maps\\map mp rhun\\map mp rhun.map" -> "map mp rhun"
{
	std::string s = entry.substr(entry.find_last_of("\\/") + 1);
	return s.substr(0, s.size() - 4);
}

std::string dumpSha(const Json& mapJson, const std::string& infix)
{
	for (const Json& f : mapJson["dump"]["files"].arr)
		if (f["file"].str.find(infix) != std::string::npos) return f["sha256"].str;
	throw bfmetest::Failure("no dump file *" + infix + "*");
}

template <typename T> std::vector<uint8_t> le(const std::vector<T>& v, int bytes)
{
	std::vector<uint8_t> out;
	out.reserve(v.size() * size_t(bytes));
	for (T x : v) for (int k = 0; k < bytes; ++k) out.push_back(uint8_t(uint32_t(x) >> (8 * k)));
	return out;
}

int nonZero(const std::vector<int32_t>& v)
{
	int n = 0;
	for (int32_t x : v) n += x != 0;
	return n;
}

} // namespace

// RefPack: every .map of maps.big is EAR\0 + RefPack; the engine's decoder (refdecode.cpp, which
// CompressionManager uses for EAR\0) gives the size and SHA-256 OpenSAGE reports.
TEST(gamedata_refpack_all_maps_vs_oracle)
{
	const Data d = openData(false);
	int maps = 0, refpack = 0;
	size_t bytesIn = 0, bytesOut = 0;
	for (const Json& m : d.oracle["maps"].arr) {
		const std::string entry = m["source"]["entry"].str;
		const std::vector<uint8_t> bytes = readEntry(d, entry);
		std::vector<uint8_t> plain;
		std::string error;
		REQUIRE(bfmeheights::decompressMap(bytes, plain, error));
		if (m["compression"].str == "refpack") {
			REQUIRE(bytes.size() >= 8 && std::memcmp(bytes.data(), "EAR\0", 4) == 0);
			++refpack;
		}
		REQUIRE_EQ((long long)plain.size(), m["decompressedSize"].asInt());
		REQUIRE_EQ(sha256Hex(plain), m["decompressedSha256"].str);
		REQUIRE_EQ(sha256Hex(bytes), m["storedSha256"].str);
		bytesIn += bytes.size();
		bytesOut += plain.size();
		++maps;
	}
	REQUIRE_EQ(maps, 115);
	std::printf("gamedata_refpack_all_maps_vs_oracle: %d maps, %d RefPack, %zu -> %zu bytes\n", maps, refpack, bytesIn, bytesOut);
}

// All 115 maps through the real HeightMapData + BlendTileData parsers and the render-side consumers;
// payloads and arrays against OpenSAGE, anomaly counters against tools/datacheck.
TEST(gamedata_blend_all_maps_vs_oracle_and_datacheck)
{
	const Data d = openData(true);
	int maps = 0;
	std::map<std::string, int> totals, mapsWith;
	std::map<int, int> versions;
	for (const Json& m : d.oracle["maps"].arr) {
		const std::string entry = m["source"]["entry"].str;
		const std::string base = baseName(entry);
		const Json mj = Json::parseFile(d.dumpDir + "/json/" + base + ".json");
		const Json& ref = d.datacheck["maps"][entry];
		if (ref.type != Json::Object) throw bfmetest::Failure("no datacheck entry for " + entry);
		const Json& ob = mj["blendTiles"];

		LoadOptions opt;
		opt.consumers = true;
		capturedLog().clear();
		std::vector<uint8_t> plain;
		const LoadedBlend b = loadBlend(readEntry(d, entry), opt, &plain);
		if (!b.ok) throw bfmetest::Failure(entry + ": " + b.error);
		REQUIRE(b.atEndOfChunk);
		REQUIRE_EQ(b.blendVersion, int(ob["version"].asInt()));
		REQUIRE_EQ(b.blendVersion, int(ref["version"].asInt()));
		++versions[b.blendVersion];
		// raw payload == the oracle's chunk dump (also checks the RefPack output and the chunk walk)
		REQUIRE(b.payloadOffset + b.payloadSize <= plain.size());
		const std::vector<uint8_t> payload(plain.begin() + long(b.payloadOffset), plain.begin() + long(b.payloadOffset + b.payloadSize));
		REQUIRE_EQ(sha256Hex(payload), dumpSha(mj, ".BlendTileData.chunk."));
		// counts
		REQUIRE_EQ(b.dataSize, int(ob["numTiles"].asInt()));
		REQUIRE_EQ(b.numBitmapTiles, int(ob["textureCellCount"].asInt()));
		REQUIRE_EQ(b.numBitmapTiles, int(ref["numBitmapTiles"].asInt()));
		REQUIRE_EQ(b.numBlendedTiles, int(ref["numBlendedTiles"].asInt()));
		REQUIRE_EQ(std::max(b.numBlendedTiles - 1, 0), int(ob["blendDescriptionCount"].asInt()));
		REQUIRE_EQ(b.numCliffInfo, int(ref["numCliffInfo"].asInt()));
		REQUIRE_EQ(std::max(b.numCliffInfo - 1, 0), int(ob["cliffTextureMappingCount"].asInt()));
		REQUIRE_EQ(b.numEdgeTiles, int(ob["magicValue1"].asInt()));
		REQUIRE_EQ(int(b.edgeClasses.size()), int(ob["magicValue2"].asInt()));
		REQUIRE_EQ(int(b.textureClasses.size()), int(ob["textureCount"].asInt()));
		for (size_t i = 0; i < b.textureClasses.size(); ++i) {
			const Json& t = ob["textures"].arr[i];
			REQUIRE_EQ(b.textureClasses[i].name, t["name"].str);
			REQUIRE_EQ(b.textureClasses[i].firstTile, int(t["cellStart"].asInt()));
			REQUIRE_EQ(b.textureClasses[i].numTiles, int(t["cellCount"].asInt()));
			REQUIRE_EQ(b.textureClasses[i].width, int(t["cellSize"].asInt()));
		}
		REQUIRE(b.tileCapacity >= b.numBitmapTiles && b.tileCapacity <= 4096);
		// arrays: tiles as stored in the file (engine values except the clamped cells)
		const int nb = b.numBitmapTiles;
		std::vector<uint16_t> rawTiles(size_t(b.dataSize));
		std::memcpy(rawTiles.data(), payload.data() + 4, rawTiles.size() * 2);
		REQUIRE_EQ(sha256Hex(le(rawTiles, 2)), dumpSha(mj, ".tiles."));
		int clamped = 0;
		std::set<uint16_t> distinct;
		for (size_t i = 0; i < rawTiles.size(); ++i) {
			const uint16_t engine = uint16_t(b.tiles[i]);
			distinct.insert(rawTiles[i]);
			REQUIRE((engine >> 2) < nb);
			if (engine != rawTiles[i]) {
				REQUIRE((rawTiles[i] >> 2) >= nb);
				REQUIRE_EQ(int(engine), ((nb - 1) << 2) | (rawTiles[i] & 3));
				++clamped;
			}
		}
		REQUIRE_EQ(int(distinct.size()), int(ob["distinctTileValues"].asInt()));
		REQUIRE_EQ(clamped, b.stats.danglingTiles);
		const int w = int(ob["blendElementBits"].asInt()) / 8;
		REQUIRE_EQ(w, 4);
		REQUIRE_EQ(sha256Hex(le(b.blends, w)), dumpSha(mj, ".blends."));
		REQUIRE_EQ(sha256Hex(le(b.threeWay, w)), dumpSha(mj, ".threeway."));
		REQUIRE_EQ(sha256Hex(le(b.cliff, w)), dumpSha(mj, ".cliff."));
		REQUIRE_EQ(nonZero(b.blends), int(ob["nonZeroBlends"].asInt()));
		REQUIRE_EQ(nonZero(b.threeWay), int(ob["nonZeroThreeWayBlends"].asInt()));
		REQUIRE_EQ(nonZero(b.cliff), int(ob["nonZeroCliffTextures"].asInt()));
		// the four masks (stored only) == the oracle's optional layers
		std::set<std::string> layers;
		for (const Json& l : ob["optionalLayers"].arr) layers.insert(l.str);
		const char* names[4] = {"ImpassabilityToPlayers", "PassageWidths", "Taintability", "ExtraPassability"};
		for (int k = 0; k < 4; ++k) {
			REQUIRE_EQ(b.hasMask[k], layers.count(names[k]) == 1);
			if (b.hasMask[k]) REQUIRE_EQ(b.masks[k].size(), size_t(b.bytesPerRow) * size_t(b.height));
		}
		REQUIRE(layers.count("Impassability") == 1);
		// anomaly counters == tools/datacheck (maps.json)
		const std::pair<const char*, int> counters[] = {
			{"dangling_tiles", b.stats.danglingTiles}, {"flags_outside_0x3", b.stats.flagsOutside0x3},
			{"edge_class_without_classes", b.stats.edgeClassWithoutClasses}, {"edge_class_out_of_range", b.stats.edgeClassOutOfRange},
			{"dir_bytes_not_01", b.stats.dirBytesNot01}, {"dangling_blend", b.stats.danglingBlend},
			{"dangling_3way", b.stats.dangling3Way}, {"dangling_cliff", b.stats.danglingCliff}};
		for (const auto& c : counters) {
			if (c.second != int(ref[c.first].asInt()))
				throw bfmetest::Failure(entry + ": " + c.first + " engine " + std::to_string(c.second) + " != datacheck " +
					std::to_string(ref[c.first].asInt()));
			totals[c.first] += c.second;
			if (c.second) ++mapsWith[c.first];
		}
		// after the fixes nothing is left out of range
		for (const BlendDesc& bd : b.descs) {
			REQUIRE((bd.inverted & ~3) == 0);
			REQUIRE(bd.customBlendEdgeClass < int(b.edgeClasses.size()));
		}
		// the engine log has the summary with the same numbers
		const std::string summary = "dangling tiles " + std::to_string(b.stats.danglingTiles) + " (clamped), flags outside &0x3 " +
			std::to_string(b.stats.flagsOutside0x3) + " (masked), customBlendEdgeClass without edge classes " +
			std::to_string(b.stats.edgeClassWithoutClasses) + " / out of range " + std::to_string(b.stats.edgeClassOutOfRange);
		REQUIRE(capturedLog().find(summary) != std::string::npos);
		// review #10: per-case lines only for the first 16 of each kind (Rhun: 711 dangling tiles -> 16 lines)
		auto lines = [](const std::string& what) {
			int n = 0;
			for (size_t p = capturedLog().find(what); p != std::string::npos; p = capturedLog().find(what, p + 1)) ++n;
			return n;
		};
		REQUIRE_EQ(lines("BlendTileData: dangling tile #"), std::min(b.stats.danglingTiles, 16));
		REQUIRE_EQ(lines("outside &0x3 (#"), std::min(b.stats.flagsOutside0x3, 16));
		REQUIRE_EQ(lines("edge classes (#"), std::min(b.stats.edgeClassOutOfRange, 16));
		REQUIRE_EQ(lines("not 0/1 (#"), std::min(b.stats.dirBytesNot01, 16));
		if (b.stats.danglingTiles || b.stats.flagsOutside0x3 || b.stats.edgeClassOutOfRange)
			std::printf("  %s: v%d, dangling tiles %d, flags outside &0x3 %d, edge class without classes %d\n", entry.c_str(),
				b.blendVersion, b.stats.danglingTiles, b.stats.flagsOutside0x3, b.stats.edgeClassWithoutClasses);
		REQUIRE_EQ(b.consumers.size(), size_t(b.dataSize));
		++maps;
	}
	REQUIRE_EQ(maps, 115);
	std::printf("gamedata_blend_all_maps_vs_oracle_and_datacheck: %d maps (", maps);
	for (const auto& v : versions) std::printf(" v%d: %d", v.first, v.second);
	std::printf(" )\n");
	for (const auto& t : totals) std::printf("  %-28s %6d on %d maps\n", t.first.c_str(), t.second, mapsWith[t.first]);
}

// All .map entries of _patch222maps.big through the real HeightMapData + BlendTileData parsers.
// Needs only BFME_GAMEDATA (no oracle): 0 refusals, and every map past the old hard limits
// (NUM_SOURCE_TILES / NUM_BLEND_TILES / NUM_CLIFF_INFO = 4096 / 65536 / 131072) is printed.
TEST(gamedata_blend_patch222_all_maps)
{
	const std::string gamedata = env("BFME_GAMEDATA");
	if (gamedata.empty()) SKIP("BFME_GAMEDATA not set");
	const std::string bigPath = gamedata + "/_patch222maps.big";
	if (!std::filesystem::exists(bigPath)) SKIP("no _patch222maps.big in BFME_GAMEDATA");
	std::vector<bfmeheights::BigEntry> entries;
	std::string error;
	if (!bfmeheights::readBigDirectory(bigPath, entries, error)) throw bfmetest::Failure(error);
	const int kOldTiles = 4096, kOldBlends = 65536, kOldCliffs = 131072;
	int maps = 0, refused = 0, above = 0;
	int maxTiles = 0, maxBlends = 0, maxCliffs = 0;
	std::string maxTilesMap, maxBlendsMap, maxCliffsMap;
	std::map<int, int> versions;
	for (const auto& e : entries) {
		const std::string name = e.name;
		if (name.size() < 4) continue;
		std::string ext = name.substr(name.size() - 4);
		std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return char(std::tolower(c)); });
		if (ext != ".map") continue;
		++maps;
		std::vector<uint8_t> bytes;
		if (!bfmeheights::readBigEntry(bigPath, e, bytes)) throw bfmetest::Failure("cannot read " + name);
		LoadOptions opt;
		capturedLog().clear();
		const LoadedBlend b = loadBlend(bytes, opt);
		if (!b.ok) {
			++refused;
			std::printf("  REFUSED %s: %s\n", name.c_str(), b.error.c_str());
			continue;
		}
		++versions[b.blendVersion];
		const bool past = b.numBitmapTiles > kOldTiles || b.numBlendedTiles > kOldBlends || b.numCliffInfo > kOldCliffs;
		if (past) ++above;
		if (past || name.find("The Trilogy (Updated) Singleplayer") != std::string::npos)
			std::printf("  %s %s: v%d numBitmapTiles %d numBlendedTiles %d numCliffInfo %d\n", past ? "ABOVE" : "below",
				name.c_str(), b.blendVersion, b.numBitmapTiles, b.numBlendedTiles, b.numCliffInfo);
		if (b.numBitmapTiles > maxTiles) { maxTiles = b.numBitmapTiles; maxTilesMap = name; }
		if (b.numBlendedTiles > maxBlends) { maxBlends = b.numBlendedTiles; maxBlendsMap = name; }
		if (b.numCliffInfo > maxCliffs) { maxCliffs = b.numCliffInfo; maxCliffsMap = name; }
	}
	std::printf("gamedata_blend_patch222_all_maps: %d maps, %d refused, %d above 4096/65536/131072 (", maps, refused, above);
	for (const auto& v : versions) std::printf(" v%d: %d", v.first, v.second);
	std::printf(" )\n");
	std::printf("  max numBitmapTiles %d (%s)\n  max numBlendedTiles %d (%s)\n  max numCliffInfo %d (%s)\n",
		maxTiles, maxTilesMap.c_str(), maxBlends, maxBlendsMap.c_str(), maxCliffs, maxCliffsMap.c_str());
	REQUIRE_EQ(maps, 291);
	REQUIRE_EQ(refused, 0);
}
