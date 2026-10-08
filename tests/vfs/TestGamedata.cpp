// BFME1 Mobile — tests
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Integration tests on the user's BFME1 copy. EA data is never in git: the game root comes from
// BFME_GAMEDATA and the OpenSAGE oracle dumps (tools/oracle, E1-20) from BFME_ORACLE_DIR; without
// them the tests are skipped (exit 77). Nothing is written to BFME_GAMEDATA: single archives are
// mounted through symlinks in a temporary directory.
#include "PreRTS.h"

#include "EngineFixture.h"
#include "MiniJson.h"
#include "Sha256.h"
#include "SyntheticBig.h"
#include "TestHarness.h"

#include "Compression.h"
#include "bfme/vfs/BfmeBIGFileSystem.h"
#include "bfme/vfs/BigArchiveReader.h"
#include "bfme/vfs/MountOrder.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <map>
#include <set>

using namespace bfmetest;
using namespace bfme::vfs;
namespace fs = std::filesystem;

namespace {

const char* const kM1Map = "maps\\map mp fords of isen\\map mp fords of isen.map";

std::string requireGamedata()
{
	const std::string g = gamedataDir();
	if (g.empty()) SKIP("BFME_GAMEDATA is not set");
	return g;
}

std::string requireArchive(const std::string& name)
{
	const fs::path p = fs::path(requireGamedata()) / name;
	if (!fs::exists(p)) SKIP(name + " is not in BFME_GAMEDATA");
	return p.string();
}

Json requireOracle(const std::string& file)
{
	const std::string dir = oracleDir();
	if (dir.empty()) SKIP("BFME_ORACLE_DIR is not set and <gamedata>/../tools/oracle/out does not exist");
	const fs::path p = fs::path(dir) / file;
	if (!fs::exists(p)) SKIP(p.string() + " is missing (run tools/oracle/oracle.sh)");
	return Json::parseFile(p.string());
}

std::string canonical(std::string path)
{
	std::replace(path.begin(), path.end(), '/', '\\');
	std::transform(path.begin(), path.end(), path.begin(), [](unsigned char c) { return char(std::tolower(c)); });
	return path;
}

// Mounts exactly one archive (via a symlink in a temp root) and checks it entry by entry
// against the oracle: count, names, sizes and SHA-256 of the raw stored bytes.
void checkArchiveAgainstOracle(const std::string& archiveName, size_t expectedCount)
{
	const std::string archive = requireArchive(archiveName);
	const Json oracle = requireOracle(archiveName + ".json");
	REQUIRE_EQ(oracle["archive"]["format"].str, std::string("BIG4"));
	REQUIRE_EQ((size_t)oracle["archive"]["entryCount"].asInt(), expectedCount);
	REQUIRE_EQ(oracle["entries"].arr.size(), expectedCount);

	// Header fields as the reader sees them (pins the endianness on real data).
	BigDirectory dir;
	std::string error;
	REQUIRE(readBigDirectory(archive, dir, &error));
	REQUIRE(dir.header.format == BigFormat::BIG4);
	REQUIRE_EQ((uint64_t)dir.header.archiveSize, (uint64_t)fs::file_size(archive));
	REQUIRE_EQ((long long)dir.header.entryCount, oracle["archive"]["header"]["entryCountBE"].asInt());
	REQUIRE_EQ((long long)dir.header.firstEntryOffset, oracle["archive"]["header"]["firstEntryOffsetBE"].asInt());

	TempDir root;
	fs::create_symlink(fs::absolute(archive), root.path() / archiveName);
	BfmeVfsConfig config;
	config.gameRoot = root.str();
	MountedVfs vfs(config);
	REQUIRE_EQ(vfs.fs().getMountedArchiveCount(), 1);

	std::set<std::string> listed;
	for (const std::string& p : vfs.listAll()) listed.insert(canonical(p));
	REQUIRE_EQ(listed.size(), expectedCount);

	size_t matched = 0;
	for (const Json& e : oracle["entries"].arr) {
		const std::string path = e["path"].str;
		REQUIRE(listed.count(canonical(path)) == 1);
		REQUIRE_EQ(vfs.sizeOf(path), e["size"].asInt());
		std::string data;
		REQUIRE(vfs.read(path, data));
		REQUIRE_EQ(Sha256::of(data.data(), data.size()), e["sha256"].str);
		++matched;
	}
	REQUIRE_EQ(matched, expectedCount);
	std::printf("  %s: %zu/%zu entries match the oracle (names, sizes, SHA-256)\n", archiveName.c_str(), matched, expectedCount);
}

std::string decompress(const std::string& stored)
{
	std::string buf = stored;
	const Int outLen = CompressionManager::getUncompressedSize(&buf[0], Int(buf.size()));
	std::string out(size_t(outLen), '\0');
	const Int got = CompressionManager::decompressData(&buf[0], Int(buf.size()), &out[0], outLen);
	if (got != outLen) throw Failure("decompressData returned " + std::to_string(got) + ", expected " + std::to_string(outLen));
	return out;
}

} // namespace

// M1 criterion 1: ini.big (BIG4, 242 entries) through the engine VFS == OpenSAGE BigArchive.
TEST(gamedata_ini_big_matches_oracle)
{
	checkArchiveAgainstOracle("ini.big", 242);
}

// M1 criterion 1: maps.big (BIG4, 372 entries).
TEST(gamedata_maps_big_matches_oracle)
{
	checkArchiveAgainstOracle("maps.big", 372);
}

// The M1 map opens through the VFS; its RefPack (EAR\0) is recognised and unpacked by the ZH
// CompressionManager (the VFS hands out stored bytes, unpacking stays in the map loader).
TEST(gamedata_m1_map_refpack)
{
	const std::string archive = requireArchive("maps.big");
	const Json oracle = requireOracle("map mp fords of isen.json");
	TempDir root;
	fs::create_symlink(fs::absolute(archive), root.path() / "maps.big");
	BfmeVfsConfig config;
	config.gameRoot = root.str();
	MountedVfs vfs(config);

	std::string stored;
	REQUIRE(vfs.read(kM1Map, stored));
	REQUIRE_EQ((long long)stored.size(), oracle["storedSize"].asInt());
	REQUIRE_EQ((long long)stored.size(), 518519LL);
	REQUIRE_EQ(Sha256::of(stored.data(), stored.size()), oracle["storedSha256"].str);
	REQUIRE_EQ(oracle["compression"].str, std::string("refpack"));
	REQUIRE(stored.compare(0, 4, std::string("EAR\0", 4)) == 0);
	REQUIRE(CompressionManager::isDataCompressed(stored.data(), Int(stored.size())));
	REQUIRE(CompressionManager::getCompressionType(stored.data(), Int(stored.size())) == COMPRESSION_REFPACK);
	REQUIRE_EQ((long long)CompressionManager::getUncompressedSize(stored.data(), Int(stored.size())), oracle["decompressedSize"].asInt());

	const std::string map = decompress(stored);
	REQUIRE_EQ(Sha256::of(map.data(), map.size()), oracle["decompressedSha256"].str);
	REQUIRE(map.compare(0, 4, "CkMp") == 0);
	std::printf("  M1 map: stored %zu B (EAR\\0) -> %zu B, SHA-256 of both match the oracle\n", stored.size(), map.size());
}

// Every map in maps.big: EAR\0, unpacked by CompressionManager, SHA-256 == OpenSAGE MapFile.Decompress.
TEST(gamedata_all_maps_refpack)
{
	const std::string archive = requireArchive("maps.big");
	const Json oracle = requireOracle("maps.big.maps.json");
	TempDir root;
	fs::create_symlink(fs::absolute(archive), root.path() / "maps.big");
	BfmeVfsConfig config;
	config.gameRoot = root.str();
	MountedVfs vfs(config);

	size_t checked = 0;
	for (const Json& m : oracle["maps"].arr) {
		std::string stored;
		const std::string path = m["source"]["entry"].str;
		REQUIRE(vfs.read(path, stored));
		REQUIRE(CompressionManager::getCompressionType(stored.data(), Int(stored.size())) == COMPRESSION_REFPACK);
		const std::string map = decompress(stored);
		REQUIRE_EQ(Sha256::of(map.data(), map.size()), m["decompressedSha256"].str);
		++checked;
	}
	REQUIRE_EQ(checked, size_t(115));
	std::printf("  %zu/115 maps: CompressionManager unpacks EAR\\0 byte-identical to OpenSAGE\n", checked);
}

// The whole BFME_GAMEDATA with the default rules: every path is served by the first archive of
// the resolution order that contains it (checked independently with BigArchiveReader). With only
// ini.big + maps.big this is a smoke test; once _patch222*.big are there it proves patch-over-base
// on real data and lists the overridden paths (input for E1-22).
TEST(gamedata_full_mount_follows_plan)
{
	const std::string gamedata = requireGamedata();
	BfmeVfsConfig config;
	config.gameRoot = gamedata;
	MountedVfs vfs(config);
	const MountPlan& plan = vfs.fs().getMountPlan();
	std::printf("%s", describePlan(plan).c_str());
	REQUIRE(vfs.fs().getMountedArchiveCount() > 0);
	REQUIRE_EQ(vfs.fs().getFailedArchiveCount(), 0);

	// A failed insert is not always a cross-archive overlap. data.big repeats three paths
	// (identical bytes). On the full 2.22 copy those 3 plus 1221 paths shared between
	// archives used to print as one number, 1224. Only the 1221 are overlaps; patch222 wins all of them.
	std::map<std::string, std::string> expectedWinner;  // canonical path -> archive path
	std::map<std::string, int> crossByLayer;
	size_t crossArchive = 0;
	size_t intraArchive = 0;
	for (const PlannedArchive& a : plan.resolutionOrder) {
		BigDirectory dir;
		std::string error;
		REQUIRE(readBigDirectory(a.archive.path, dir, &error));
		std::set<std::string> seenHere;
		for (const BigEntry& e : dir.entries) {
			const std::string key = canonical(e.path);
			if (!seenHere.insert(key).second) {
				++intraArchive;
				continue;
			}
			const auto inserted = expectedWinner.emplace(key, a.archive.path);
			if (!inserted.second) {
				++crossArchive;
				++crossByLayer[a.layer];
			}
		}
	}
	for (const auto& [path, archive] : expectedWinner) {
		REQUIRE_EQ(vfs.winner(path), archive);
	}
	std::printf("  %zu paths, winners match the plan\n", expectedWinner.size());
	std::printf("  %zu cross-archive overlaps; %zu duplicate paths inside one archive (not overlaps)\n", crossArchive, intraArchive);
	for (const auto& [layer, n] : crossByLayer) {
		std::printf("  %d entries of layer %s are shadowed by an earlier archive\n", n, layer.c_str());
	}
}

// 2.22 on real data: skipped until _patch222*.big are in BFME_GAMEDATA.
TEST(gamedata_patch222_over_base)
{
	const std::string gamedata = requireGamedata();
	const MountRules rules = defaultBfme1MountRules();
	const MountPlan plan = planMount(scanArchives(gamedata, rules), rules);
	size_t patches = 0;
	for (const PlannedArchive& a : plan.resolutionOrder) patches += (a.layer == "patch222");
	if (patches == 0) SKIP("no _patch222*.big in BFME_GAMEDATA yet");

	BfmeVfsConfig config;
	config.gameRoot = gamedata;
	MountedVfs vfs(config);
	size_t overlaps = 0;
	for (const PlannedArchive& a : plan.resolutionOrder) {
		if (a.layer != "patch222") continue;
		BigDirectory dir;
		std::string error;
		REQUIRE(readBigDirectory(a.archive.path, dir, &error));
		for (const BigEntry& e : dir.entries) {
			const std::string winner = vfs.winner(e.path);
			const LayerRule* layer = nullptr;
			for (const PlannedArchive& w : plan.resolutionOrder) {
				if (w.archive.path == winner) layer = rules.findLayer(w.layer);
			}
			REQUIRE(layer != nullptr);
			REQUIRE(layer->priority >= rules.findLayer("patch222")->priority);  // base never wins over 2.22
			++overlaps;
		}
	}
	std::printf("  %zu patch222 archives, %zu entries: none is served by base\n", patches, overlaps);
}
