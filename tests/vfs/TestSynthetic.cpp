// BFME1 Mobile — tests
// SPDX-License-Identifier: GPL-3.0-or-later
//
// CI tests on synthetic archives only (no EA data): BIG4/BIGF parsing in the engine, mount order.
#include "PreRTS.h"

#include "EngineFixture.h"
#include "SyntheticBig.h"
#include "TestHarness.h"

#include "Compression.h"
#include "bfme/vfs/BfmeBIGFileSystem.h"
#include "bfme/vfs/BigArchiveReader.h"
#include "bfme/vfs/MountOrder.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <sstream>

using namespace bfmetest;
using namespace bfme::vfs;

namespace {

std::vector<SyntheticEntry> sampleEntries(const std::string& tag)
{
	return {
		{"data\\ini\\gamedata.ini", "GameData ; " + tag + "\n"},
		{"data\\ini\\object\\gondor.ini", std::string(1000, 'g') + tag},
		{"maps\\map mp test\\map mp test.map", std::string("EAR\0", 4) + tag},
	};
}

BfmeVfsConfig configFor(const TempDir& root)
{
	BfmeVfsConfig config;
	config.gameRoot = root.str();
	return config;
}

std::string lower(std::string s)
{
	std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return char(std::tolower(c)); });
	return s;
}

} // namespace

// --- BIG layout -----------------------------------------------------------------------------

TEST(big4_header_endianness)
{
	// Oracle fact (tools/oracle, BFME1 ini.big/maps.big): archive size LE, count + first offset BE.
	const std::string bytes = buildBig(sampleEntries("x"), {"BIG4", false});
	const unsigned char* p = reinterpret_cast<const unsigned char*>(bytes.data());
	REQUIRE_EQ(bytes.substr(0, 4), std::string("BIG4"));
	REQUIRE_EQ(uint32_t(p[4] | (p[5] << 8) | (p[6] << 16) | (uint32_t(p[7]) << 24)), uint32_t(bytes.size()));
	REQUIRE_EQ(uint32_t((p[8] << 24) | (p[9] << 16) | (p[10] << 8) | p[11]), 3u);

	BigDirectory dir;
	std::string error;
	REQUIRE(parseBigDirectory(p, bytes.size(), bytes.size(), dir, &error));
	REQUIRE(dir.header.format == BigFormat::BIG4);
	REQUIRE_EQ(dir.header.archiveSize, uint32_t(bytes.size()));
	REQUIRE_EQ(dir.header.entryCount, 3u);
	REQUIRE_EQ(dir.entries.size(), size_t(3));
	REQUIRE_EQ(dir.header.firstEntryOffset, dir.entries[0].offset);
	REQUIRE_EQ(dir.entries[1].path, std::string("data\\ini\\object\\gondor.ini"));
	REQUIRE_EQ(dir.entries[1].size, 1001u);
	REQUIRE_EQ(bytes.substr(dir.entries[1].offset, dir.entries[1].size), sampleEntries("x")[1].data);

	// A size field written big-endian is detectable (it no longer equals the file size).
	const std::string wrong = buildBig(sampleEntries("x"), {"BIG4", true});
	BigDirectory wrongDir;
	REQUIRE(parseBigDirectory(reinterpret_cast<const unsigned char*>(wrong.data()), wrong.size(), wrong.size(), wrongDir, &error));
	REQUIRE(wrongDir.header.archiveSize != uint32_t(wrong.size()));
}

TEST(big_reader_rejects_bad_input)
{
	std::string error;
	BigDirectory dir;
	const std::string bad = buildBig(sampleEntries("x"), {"BIG5", false});
	REQUIRE(!parseBigDirectory(reinterpret_cast<const unsigned char*>(bad.data()), bad.size(), bad.size(), dir, &error));
	std::string truncated = buildBig(sampleEntries("x"));
	truncated.resize(0x14);
	REQUIRE(!parseBigDirectory(reinterpret_cast<const unsigned char*>(truncated.data()), truncated.size(), truncated.size(), dir, &error));
}

TEST(engine_opens_big4_and_bigf)
{
	TempDir tmp;
	for (const char* sig : {"BIG4", "BIGF"}) {
		const auto path = tmp.path() / (std::string(sig) + ".big");
		writeBig(path, sampleEntries(sig), {sig, false});
		std::vector<std::string> files;
		REQUIRE(engineAcceptsArchive(path.string(), &files));
		REQUIRE_EQ(files.size(), size_t(3));
		std::sort(files.begin(), files.end());
		REQUIRE_EQ(lower(files[0]), std::string("data\\ini\\gamedata.ini"));
	}
}

TEST(engine_rejects_unknown_signature)
{
	TempDir tmp;
	for (const char* sig : {"BIG5", "BIGf", "XXXX"}) {
		const auto path = tmp.path() / (std::string(sig) + ".big");
		writeBig(path, sampleEntries(sig), {sig, false});
		REQUIRE(!engineAcceptsArchive(path.string()));
	}
}

TEST(engine_reads_big4_entries)
{
	TempDir root;
	writeBig(root.path() / "ini.big", sampleEntries("base"));
	MountedVfs vfs(configFor(root));
	REQUIRE_EQ(vfs.fs().getMountedArchiveCount(), 1);
	for (const SyntheticEntry& e : sampleEntries("base")) {
		std::string data;
		REQUIRE(vfs.read(e.path, data));
		REQUIRE_EQ(data, e.data);
		REQUIRE_EQ(vfs.sizeOf(e.path), (long long)e.data.size());
	}
	// Engine lookups are case-insensitive and accept both separators.
	REQUIRE(vfs.exists("DATA/INI/GameData.ini"));
	REQUIRE(!vfs.exists("data\\ini\\missing.ini"));
}

// --- mount rules ------------------------------------------------------------------------------

TEST(mount_rules_cfg_equals_builtin)
{
	MountRules fromFile;
	std::string error;
	REQUIRE(loadMountRulesFile(BFME_SOURCE_DIR "/formats/vfs/bfme1-mount-order.cfg", fromFile, &error));
	const MountRules builtin = defaultBfme1MountRules();
	REQUIRE_EQ(fromFile.layers.size(), builtin.layers.size());
	for (size_t i = 0; i < builtin.layers.size(); ++i) {
		const LayerRule& a = fromFile.layers[i];
		const LayerRule& b = builtin.layers[i];
		REQUIRE_EQ(a.name, b.name);
		REQUIRE_EQ(a.priority, b.priority);
		REQUIRE_EQ(a.dir, b.dir);
		REQUIRE_EQ(a.recursive, b.recursive);
		REQUIRE(a.order == b.order);
		REQUIRE(a.globs == b.globs);
	}
}

TEST(mount_rules_parse_errors)
{
	MountRules rules;
	std::string error;
	for (const char* text : {
		     "",
		     "lyer base 0 . flat last-wins *.big\n",
		     "layer base 0 . sideways last-wins *.big\n",
		     "layer base 0 . flat random *.big\n",
		     "layer base 0 . flat last-wins\n",
		     "layer a 0 . flat last-wins *.big\nlayer b 0 . flat last-wins *.big\n",
		     "layer a 0 . flat last-wins *.big\nlayer a 1 . flat last-wins *.big\n"}) {
		std::istringstream in(text);
		REQUIRE(!parseMountRules(in, rules, &error));
	}
}

TEST(mount_classify_bfme1_names)
{
	const MountRules rules = defaultBfme1MountRules();
	auto layerOf = [&](const std::string& rel) {
		const LayerRule* l = classifyArchive({rel, rel}, rules);
		return l != nullptr ? l->name : std::string("-");
	};
	REQUIRE_EQ(layerOf("ini.big"), std::string("base"));
	REQUIRE_EQ(layerOf("maps.big"), std::string("base"));
	REQUIRE_EQ(layerOf("lang/English.big"), std::string("base"));
	REQUIRE_EQ(layerOf("_patch222.big"), std::string("patch222"));
	REQUIRE_EQ(layerOf("_patch222textures.big"), std::string("patch222"));
	REQUIRE_EQ(layerOf("_Patch222Maps.BIG"), std::string("patch222"));
	REQUIRE_EQ(layerOf("lang/_patch222.big"), std::string("base"));  // patch layer is flat (root only)
	REQUIRE_EQ(layerOf("_patch201.big"), std::string("base"));       // other patches: not 2.22
	REQUIRE_EQ(layerOf("MobileLayer/ui.big"), std::string("mobile"));
	REQUIRE_EQ(layerOf("mobilelayer/sub/_patch222.big"), std::string("mobile"));
	REQUIRE_EQ(layerOf("/elsewhere/x.big"), std::string("-"));
}

TEST(mount_plan_bfme1_order)
{
	const MountRules rules = defaultBfme1MountRules();
	std::vector<ArchiveCandidate> archives;
	for (const char* rel : {"ini.big", "maps.big", "w3d.big", "_patch222.big", "_patch222bases.big", "_patch222libraries.big",
		     "_patch222maps.big", "_patch222textures.big", "MobileLayer/mobile.big", "lang/english.big"}) {
		archives.push_back({rel, rel});
	}
	const MountPlan plan = planMount(archives, rules);
	std::vector<std::string> order;
	for (const PlannedArchive& a : plan.resolutionOrder) order.push_back(a.archive.relPath);
	const std::vector<std::string> expected = {
		"MobileLayer/mobile.big",
		// patch222, first-wins in _stricmp order ('.' < letters): earlier names first.
		"_patch222.big", "_patch222bases.big", "_patch222libraries.big", "_patch222maps.big", "_patch222textures.big",
		// base, first-wins: ini < lang/english < maps < w3d.
		"ini.big", "lang/english.big", "maps.big", "w3d.big"};
	REQUIRE(order == expected);
	REQUIRE(plan.unassigned.empty());

	// _stricmp: lower-case folding, so '_' (0x5F) sorts before letters (0x61..), also before upper case ones.
	REQUIRE(compareArchiveNames("_x.big", "ini.big") < 0);
	REQUIRE(compareArchiveNames("_x.big", "INI.big") < 0);
	REQUIRE(compareArchiveNames("A_X.big", "aa.big") < 0);
	REQUIRE(compareArchiveNames("INI.BIG", "ini.big") == 0);
	REQUIRE(globMatchNoCase("_patch222*.big", "_PATCH222Bases.Big"));
	REQUIRE(!globMatchNoCase("_patch222*.big", "_patch22.big"));
}

// --- layered mount in the engine (M1 criterion 2) -------------------------------------------

TEST(engine_layer_priority_base_patch_mobile)
{
	TempDir root;
	writeBig(root.path() / "ini.big", {
		{"data\\ini\\gamedata.ini", "base"}, {"data\\ini\\only_base.ini", "base"}, {"data\\ini\\base_and_patch.ini", "base"}});
	writeBig(root.path() / "_patch222.big", {
		{"data\\ini\\gamedata.ini", "patch"}, {"data\\ini\\base_and_patch.ini", "patch"}, {"data\\ini\\only_patch.ini", "patch"}});
	writeBig(root.path() / "MobileLayer" / "mobile.big", {
		{"Data\\INI\\GameData.ini", "mobile"}, {"data\\mobile\\hud.ini", "mobile"}}, {"BIGF", false});

	MountedVfs vfs(configFor(root));
	REQUIRE_EQ(vfs.fs().getMountedArchiveCount(), 3);
	auto contentOf = [&](const char* path) {
		std::string data;
		REQUIRE(vfs.read(path, data));
		return data;
	};
	REQUIRE_EQ(contentOf("data\\ini\\gamedata.ini"), std::string("mobile"));
	REQUIRE_EQ(contentOf("data\\ini\\base_and_patch.ini"), std::string("patch"));
	REQUIRE_EQ(contentOf("data\\ini\\only_base.ini"), std::string("base"));
	REQUIRE_EQ(contentOf("data\\ini\\only_patch.ini"), std::string("patch"));
	REQUIRE_EQ(contentOf("data\\mobile\\hud.ini"), std::string("mobile"));
	REQUIRE(vfs.winner("data\\ini\\gamedata.ini").find("mobile.big") != std::string::npos);
	REQUIRE(vfs.winner("data\\ini\\base_and_patch.ini").find("_patch222.big") != std::string::npos);
	// The union lists every path once.
	REQUIRE_EQ(vfs.listAll().size(), size_t(5));
}

TEST(engine_layer_priority_beats_names)
{
	// "aaa.big" in the mobile layer and "zzz.big" in base: the layer decides, not the name.
	TempDir root;
	writeBig(root.path() / "zzz.big", {{"x.ini", "base"}});
	writeBig(root.path() / "MobileLayer" / "aaa.big", {{"x.ini", "mobile"}});
	MountedVfs vfs(configFor(root));
	std::string data;
	REQUIRE(vfs.read("x.ini", data));
	REQUIRE_EQ(data, std::string("mobile"));
}

// docs/formats/bfme1-big-order.md: inside a layer the FIRST archive in _stricmp order wins (ZH).
TEST(engine_intra_layer_first_by_stricmp_wins)
{
	// Same path in three archives of one layer (base). _stricmp order: "A_X.big" < "aa.big" < "b.big"
	// ('_' 0x5F < 'a' 0x61). An upper-case-folding sort (NTFS / OpenSAGE) would put "aa.big" first instead.
	TempDir root;
	writeBig(root.path() / "b.big", {{"data\\ini\\x.ini", "b"}});
	writeBig(root.path() / "aa.big", {{"data\\ini\\x.ini", "aa"}, {"only_aa.ini", "aa"}});
	writeBig(root.path() / "A_X.big", {{"Data/INI/X.ini", "A_X"}});
	{
		MountedVfs vfs(configFor(root));
		std::string data;
		REQUIRE(vfs.read("data\\ini\\x.ini", data));
		REQUIRE_EQ(data, std::string("A_X"));
		REQUIRE(vfs.winner("data\\ini\\x.ini").find("A_X.big") != std::string::npos);
		REQUIRE(vfs.read("only_aa.ini", data));
		REQUIRE_EQ(data, std::string("aa"));
	}
	{
		// last-wins stays available in the rules file for experiments: the last one, "b.big", wins.
		BfmeVfsConfig config = configFor(root);
		std::istringstream in("layer base 0 . recursive last-wins *.big\n");
		std::string error;
		REQUIRE(parseMountRules(in, config.rules, &error));
		MountedVfs vfs(config);
		std::string data;
		REQUIRE(vfs.read("data\\ini\\x.ini", data));
		REQUIRE_EQ(data, std::string("b"));
	}
}

// Layers: _patch222 over base even when a base archive comes first by name ("_a.big" < "_patch222.big").
TEST(engine_layer_patch222_over_base)
{
	TempDir root;
	writeBig(root.path() / "_a.big", {{"data\\ini\\gamedata.ini", "base _a"}});
	writeBig(root.path() / "ini.big", {{"data\\ini\\gamedata.ini", "base ini"}, {"data\\ini\\neutralunits.ini", "base ini"}});
	writeBig(root.path() / "_patch222.big", {{"data\\ini\\gamedata.ini", "patch"}});
	MountedVfs vfs(configFor(root));
	std::string data;
	REQUIRE(vfs.read("data\\ini\\gamedata.ini", data));
	REQUIRE_EQ(data, std::string("patch"));
	REQUIRE(vfs.winner("data\\ini\\gamedata.ini").find("_patch222.big") != std::string::npos);
	REQUIRE(vfs.read("data\\ini\\neutralunits.ini", data));
	REQUIRE_EQ(data, std::string("base ini"));
	// Same rule without layers (everything in one first-wins layer) would let "_a.big" win.
	BfmeVfsConfig config = configFor(root);
	std::istringstream in("layer base 0 . recursive first-wins *.big\n");
	std::string error;
	REQUIRE(parseMountRules(in, config.rules, &error));
	MountedVfs flat(config);
	REQUIRE(flat.read("data\\ini\\gamedata.ini", data));
	REQUIRE_EQ(data, std::string("base _a"));
}

TEST(engine_mobile_layer_case_and_slashes)
{
	// formats/vfs/README.md "MobileLayer: регистр и слэши". Paths are matched case-insensitively and
	// '\\' == '/', so a MobileLayer entry overrides the base entry whatever case/separators it uses.
	TempDir root;
	writeBig(root.path() / "ini.big", {
		{"data\\ini\\gamedata.ini", "base"}, {"data\\ini\\object\\gondor.ini", "base"}});
	// Directory "mobilelayer" (rule says "MobileLayer") and archive "UI.BIG": classified case-insensitively.
	writeBig(root.path() / "mobilelayer" / "UI.BIG", {
		{"DATA/INI/GameData.INI", "mobile"},                 // other case + forward slashes
		{"Data\\Ini\\Object/Gondor.ini", "mobile-mixed"},  // mixed separators
		{"data\\ini\\dup.ini", "first"},
		{"DATA/INI/DUP.INI", "second"}},                     // same path twice in one archive: later entry wins
		{"BIGF", false});
	MountedVfs vfs(configFor(root));
	REQUIRE_EQ(vfs.fs().getMountedArchiveCount(), 2);
	REQUIRE_EQ(vfs.fs().getMountPlan().resolutionOrder.front().layer, std::string("mobile"));
	auto contentOf = [&](const char* path) {
		std::string data;
		REQUIRE(vfs.read(path, data));
		return data;
	};
	for (const char* path : {"data\\ini\\gamedata.ini", "Data/Ini/GameData.ini", "DATA\\INI/GAMEDATA.INI"}) {
		REQUIRE_EQ(contentOf(path), std::string("mobile"));
	}
	REQUIRE_EQ(contentOf("data/ini/object/gondor.ini"), std::string("mobile-mixed"));
	REQUIRE_EQ(contentOf("data\\ini\\dup.ini"), std::string("second"));
	// One path per file in the union, listed lower-case with '\\'.
	const std::vector<std::string> listed = vfs.listAll();
	REQUIRE_EQ(listed.size(), size_t(3));
	for (const std::string& p : listed) {
		REQUIRE(p.find('/') == std::string::npos);
		REQUIRE_EQ(p, lower(p));
	}
}

TEST(engine_mobile_layer_outside_root)
{
	TempDir root;
	TempDir mobile;
	writeBig(root.path() / "ini.big", {{"x.ini", "base"}});
	writeBig(mobile.path() / "m.big", {{"x.ini", "mobile"}});
	BfmeVfsConfig config = configFor(root);
	config.rules.findLayer("mobile")->dir = mobile.str();
	MountedVfs vfs(config);
	REQUIRE_EQ(vfs.fs().getMountedArchiveCount(), 2);
	std::string data;
	REQUIRE(vfs.read("x.ini", data));
	REQUIRE_EQ(data, std::string("mobile"));
}

TEST(engine_skips_broken_archive)
{
	TempDir root;
	writeBig(root.path() / "ini.big", {{"x.ini", "base"}});
	writeFile(root.path() / "broken.big", "not a big file at all");
	MountedVfs vfs(configFor(root));
	REQUIRE_EQ(vfs.fs().getMountedArchiveCount(), 1);
	REQUIRE_EQ(vfs.fs().getFailedArchiveCount(), 1);
	REQUIRE(vfs.exists("x.ini"));
}

// SDL3GameEngine::createArchiveFileSystem() selects BfmeBIGFileSystem exactly when this is true.
TEST(bfme_gamedata_requests_bfme_filesystem)
{
	struct Restore {
		bool had;
		std::string value;
		Restore()
		{
			const char* v = getenv("BFME_GAMEDATA");
			had = v != nullptr;
			if (had) {
				value = v;
			}
		}
		~Restore()
		{
			if (had) {
				setenv("BFME_GAMEDATA", value.c_str(), 1);
			} else {
				unsetenv("BFME_GAMEDATA");
			}
		}
	} restore;

	unsetenv("BFME_GAMEDATA");
	REQUIRE(!BfmeBIGFileSystem::isRequestedByEnvironment());
	BfmeVfsConfig empty;
	std::string error;
	REQUIRE(!BfmeVfsConfig::fromEnvironment(empty, &error));

	setenv("BFME_GAMEDATA", "", 1);
	REQUIRE(!BfmeBIGFileSystem::isRequestedByEnvironment());

	setenv("BFME_GAMEDATA", "/tmp/bfme-not-a-real-gamedata", 1);
	REQUIRE(BfmeBIGFileSystem::isRequestedByEnvironment());
	BfmeVfsConfig cfg;
	REQUIRE(BfmeVfsConfig::fromEnvironment(cfg, &error));
	REQUIRE_EQ(cfg.gameRoot, std::string("/tmp/bfme-not-a-real-gamedata"));
}

// --- RefPack stays in CompressionManager -----------------------------------------------------

TEST(refpack_roundtrip_compression_manager)
{
	std::string src;
	for (int i = 0; i < 20000; ++i) src += char("CkMpHeightMapData"[i % 17] + (i / 997) % 3);
	std::vector<char> packed(size_t(CompressionManager::getMaxCompressedSize(Int(src.size()), COMPRESSION_REFPACK)) + 64);
	const Int packedLen = CompressionManager::compressData(COMPRESSION_REFPACK, &src[0], Int(src.size()), packed.data(), Int(packed.size()));
	REQUIRE(packedLen > 8);
	REQUIRE(std::string(packed.data(), 4) == std::string("EAR\0", 4));
	REQUIRE(CompressionManager::getCompressionType(packed.data(), packedLen) == COMPRESSION_REFPACK);
	REQUIRE_EQ(CompressionManager::getUncompressedSize(packed.data(), packedLen), Int(src.size()));
	std::string out(src.size(), '\0');
	REQUIRE_EQ(CompressionManager::decompressData(packed.data(), packedLen, &out[0], Int(out.size())), Int(src.size()));
	REQUIRE(out == src);
}
