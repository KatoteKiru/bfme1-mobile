// BFME1 Mobile — tests
// SPDX-License-Identifier: GPL-3.0-or-later
//
// CI tests on synthetic archives only (no EA data): BIG4/BIGF parsing in the engine, mount order.
#include "PreRTS.h"

#include "EngineFixture.h"
#include "SyntheticBig.h"
#include "TestHarness.h"

#include "Common/ArchiveFile.h"
#include "Common/GlobalData.h"
#include "Common/file.h"
#include "Compression.h"
#include "StdDevice/Common/StdBIGFileSystem.h"
#include "bfme/vfs/BfmeBIGFileSystem.h"
#include "bfme/vfs/BigArchiveReader.h"
#include "bfme/vfs/MountOrder.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <sys/wait.h>
#include <unistd.h>

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

std::string readAll(ArchiveFileSystem& fs, const char* path)
{
	File* f = fs.openFile(path, File::READ | File::BINARY);
	REQUIRE(f != nullptr);
	const Int n = f->size();
	std::string out(size_t(n > 0 ? n : 0), '\0');
	const Int got = n > 0 ? f->read(&out[0], n) : 0;
	f->close();
	REQUIRE_EQ(got, n);
	return out;
}

// stderr is fully buffered once it is a pipe, so the code under test must fflush.
class StderrCapture {
public:
	StderrCapture()
	{
		REQUIRE(pipe(fds_) == 0);
		saved_ = dup(STDERR_FILENO);
		REQUIRE(saved_ >= 0);
		REQUIRE(dup2(fds_[1], STDERR_FILENO) >= 0);
	}
	~StderrCapture()
	{
		if (saved_ >= 0) {
			fflush(stderr);
			dup2(saved_, STDERR_FILENO);
			close(saved_);
			close(fds_[0]);
			close(fds_[1]);
		}
	}
	std::string stop()
	{
		fflush(stderr);
		dup2(saved_, STDERR_FILENO);
		close(saved_);
		saved_ = -1;
		close(fds_[1]);
		std::string text;
		char buf[1024];
		ssize_t n = 0;
		while ((n = read(fds_[0], buf, sizeof(buf))) > 0) {
			text.append(buf, size_t(n));
		}
		close(fds_[0]);
		return text;
	}

private:
	int fds_[2] = {-1, -1};
	int saved_ = -1;
};

struct ChildExit {
	int status = 0;
	std::string err;
};

template <class F>
ChildExit exitInChild(F fn)
{
	int fds[2];
	REQUIRE(pipe(fds) == 0);
	const pid_t pid = fork();
	REQUIRE(pid >= 0);
	if (pid == 0) {
		dup2(fds[1], STDERR_FILENO);
		close(fds[0]);
		close(fds[1]);
		fn();
		std::_Exit(0);
	}
	close(fds[1]);
	ChildExit result;
	char buf[1024];
	ssize_t n = 0;
	while ((n = read(fds[0], buf, sizeof(buf))) > 0) {
		result.err.append(buf, size_t(n));
	}
	close(fds[0]);
	REQUIRE(waitpid(pid, &result.status, 0) == pid);
	return result;
}

struct EnvRestore {
	bool hadRoot;
	std::string root;
	bool hadRules;
	std::string rules;
	EnvRestore()
	{
		const char* g = getenv("BFME_GAMEDATA");
		hadRoot = g != nullptr;
		if (hadRoot) {
			root = g;
		}
		const char* r = getenv("BFME_VFS_MOUNT_ORDER");
		hadRules = r != nullptr;
		if (hadRules) {
			rules = r;
		}
	}
	~EnvRestore()
	{
		if (hadRoot) {
			setenv("BFME_GAMEDATA", root.c_str(), 1);
		} else {
			unsetenv("BFME_GAMEDATA");
		}
		if (hadRules) {
			setenv("BFME_VFS_MOUNT_ORDER", rules.c_str(), 1);
		} else {
			unsetenv("BFME_VFS_MOUNT_ORDER");
		}
	}
};

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

	// entryCount 0xFFFFFFFF used to reserve that many pointers and throw bad_alloc.
	std::string huge = "BIG4";
	putLE32(huge, 16);
	putBE32(huge, 0xFFFFFFFFu);
	putBE32(huge, 16);
	REQUIRE(!parseBigDirectory(reinterpret_cast<const unsigned char*>(huge.data()), huge.size(), huge.size(), dir, &error));

	// fileSize 0 is an empty file: the bounds check must not be skipped.
	const std::string payload = buildBig({{"a.ini", "hi"}});
	REQUIRE(!parseBigDirectory(reinterpret_cast<const unsigned char*>(payload.data()), payload.size(), 0, dir, &error));
	REQUIRE(parseBigDirectory(reinterpret_cast<const unsigned char*>(payload.data()), payload.size(), payload.size(), dir, &error));

	REQUIRE(!entryPathSafeForDisk(""));
	REQUIRE(!entryPathSafeForDisk("../x.ini"));
	REQUIRE(!entryPathSafeForDisk("a/../../etc/passwd"));
	REQUIRE(!entryPathSafeForDisk("/etc/passwd"));
	REQUIRE(!entryPathSafeForDisk("\\\\server\\share\\x"));
	REQUIRE(!entryPathSafeForDisk("C:\\Windows\\x.dll"));
	REQUIRE(entryPathSafeForDisk("data\\ini\\a.ini"));
	REQUIRE(entryPathSafeForDisk("data/./ini/a.ini"));

	TempDir extracted;
	const auto archivePath = extracted.path() / "ini.big";
	writeBig(archivePath, {{"data\\ini\\a.ini", "hi"}});
	BigDirectory written;
	REQUIRE(readBigDirectory(archivePath.string(), written, &error));
	const auto dest = extracted.path() / "out";
	REQUIRE(extractBigEntry(archivePath.string(), written.entries[0], dest.string(), &error));
	std::ifstream back(dest / "data" / "ini" / "a.ini");
	std::string got;
	REQUIRE(std::getline(back, got));
	REQUIRE_EQ(got, std::string("hi"));
	BigEntry escape = written.entries[0];
	escape.path = "../outside.ini";
	REQUIRE(!extractBigEntry(archivePath.string(), escape, dest.string(), &error));
	REQUIRE(!std::filesystem::exists(extracted.path() / "outside.ini"));
	escape.path = "/tmp/abs.ini";
	REQUIRE(!extractBigEntry(archivePath.string(), escape, dest.string(), &error));
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
	writeBig(root.path() / "ini.big", {{"required.ini", "ini"}});
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
	writeBig(root.path() / "ini.big", {{"required.ini", "ini"}});
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

// ZH FilenameList keeps one name when two paths differ only by case. The byte-smaller path is kept.
TEST(engine_mounts_case_duplicate_archive_once)
{
	TempDir root;
	writeBig(root.path() / "ini.big", {{"data\\ini\\x.ini", "lower"}});
	writeBig(root.path() / "INI.BIG", {{"data\\ini\\x.ini", "UPPER"}});
	MountedVfs vfs(configFor(root));
	REQUIRE_EQ(vfs.fs().getMountedArchiveCount(), 1);
	std::string data;
	REQUIRE(vfs.read("data\\ini\\x.ini", data));
	REQUIRE_EQ(data, std::string("UPPER"));
	REQUIRE(vfs.winner("data\\ini\\x.ini").find("INI.BIG") != std::string::npos);
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

// 500_900_CommunityPatch_CoreINI.big sorts ahead of _patch222 ('5' < '_'), so its
// data\ini\* would win. BfmeBIGFileSystem must not mount it, marker file or not.
// loadMods() calls loadZhCommunityPatch() with the user-data directory.
TEST(bfme_skips_zh_community_patch)
{
	TempDir root;
	TempDir user;
	writeBig(root.path() / "ini.big", {{"data\\ini\\gamedata.ini", "BASE"}});
	writeBig(root.path() / "_patch222core.big", {{"data\\ini\\gamedata.ini", "PATCH222"}});
	const std::filesystem::path patch = user.path() / "OnlineGameData" / "500_900_CommunityPatch_CoreINI.big";
	writeBig(patch, {{"data\\ini\\gamedata.ini", "ZHCP"}});
	REQUIRE(std::filesystem::exists(patch));
	REQUIRE(!std::filesystem::exists(user.path() / "gx_no_community_patch.txt"));
	ensureLocalFileSystem();

	{
		StdBIGFileSystem zh;
		REQUIRE(zh.mountsZhCommunityPatch());
		REQUIRE(zh.loadBigFilesFromDirectory(AsciiString(root.str().c_str()), "*.big", FALSE));
		StderrCapture cap;
		zh.loadZhCommunityPatch(AsciiString(user.str().c_str()));
		const std::string log = cap.stop();
		REQUIRE(log.find("community patch skipped (BFME)") == std::string::npos);
		REQUIRE(log.find("community patch mounted") != std::string::npos);
		REQUIRE_EQ(readAll(zh, "data\\ini\\gamedata.ini"), std::string("ZHCP"));
		ArchiveFile* winner = zh.getArchiveFile("data\\ini\\gamedata.ini");
		REQUIRE(winner != nullptr);
		REQUIRE(std::string(winner->getName().str()).find("500_900") != std::string::npos);
	}

	{
		MountedVfs vfs(configFor(root));
		REQUIRE(!vfs.fs().mountsZhCommunityPatch());
		std::string before;
		REQUIRE(vfs.read("data\\ini\\gamedata.ini", before));
		REQUIRE_EQ(before, std::string("PATCH222"));
		StderrCapture cap;
		vfs.fs().loadZhCommunityPatch(AsciiString(user.str().c_str()));
		const std::string log = cap.stop();
		REQUIRE(log.find("community patch skipped (BFME)") != std::string::npos);
		REQUIRE(log.find("community patch mounted") == std::string::npos);
		std::string data;
		REQUIRE(vfs.read("data\\ini\\gamedata.ini", data));
		REQUIRE_EQ(data, std::string("PATCH222"));
		REQUIRE(vfs.winner("data\\ini\\gamedata.ini").find("500_900") == std::string::npos);
		REQUIRE(vfs.winner("data\\ini\\gamedata.ini").find("_patch222") != std::string::npos);
	}
}

bool candidateUnderExcludedDir(const std::string& relPath)
{
	std::string path = relPath;
	std::transform(path.begin(), path.end(), path.begin(), [](unsigned char c) { return char(std::tolower(c)); });
	return path.find("onlinegamedata") != std::string::npos
		|| path.find("command and conquer generals zero hour data") != std::string::npos
		|| path.find("500_900_communitypatch") != std::string::npos;
}

// The base layer walks the game root recursively. userdata ZH and
// OnlineGameData inside that root must not contribute archives.
// gx_no_community_patch.txt is not created: it is not the BFME switch.
TEST(bfme_scan_skips_zh_userdata_dirs)
{
	TempDir root;
	TempDir outside;
	writeBig(root.path() / "ini.big", {
		{"data\\ini\\gamedata.ini", "BASE"}, {"data\\ini\\neutralunits.ini", "BASE"}});
	writeBig(root.path() / "_patch222.big", {{"data\\ini\\gamedata.ini", "PATCH"}});
	writeBig(root.path() / "lang" / "english.big", {{"lang.ini", "LANG"}});
	writeBig(root.path() / "MobileLayer" / "ui.big", {{"hud.ini", "UI"}});
	const std::filesystem::path online = root.path() / "OnlineGameData";
	writeBig(online / "500_900_CommunityPatch_CoreINI.big", {
		{"data\\ini\\gamedata.ini", "ZH"}, {"data\\ini\\neutralunits.ini", "ZH"}, {"only_zh.ini", "ZH"}});
	writeBig(online / "side.big", {{"only_side.ini", "SIDE"}});
	writeBig(root.path() / "onlineGameData" / "also.big", {{"only_also.ini", "ALSO"}});
	const std::filesystem::path userdata = root.path() / "Command and Conquer Generals Zero Hour Data";
	writeBig(userdata / "loose.big", {{"only_loose.ini", "LOOSE"}});
	writeBig(userdata / "OnlineGameData" / "500_900_CommunityPatch_CoreINI.big", {
		{"data\\ini\\neutralunits.ini", "ZH2"}});
	writeBig(outside.path() / "ok.big", {{"outside.ini", "OUT"}});
	writeBig(outside.path() / "OnlineGameData" / "500_900_CommunityPatch_CoreINI.big", {
		{"data\\ini\\neutralunits.ini", "ZH3"}});
	REQUIRE(!std::filesystem::exists(root.path() / "gx_no_community_patch.txt"));
	REQUIRE(!std::filesystem::exists(userdata / "gx_no_community_patch.txt"));
	REQUIRE(!std::filesystem::exists(online / "gx_no_community_patch.txt"));

	MountRules rules = defaultBfme1MountRules();
	rules.findLayer("mobile")->dir = outside.str();
	const std::vector<ArchiveCandidate> found = scanArchives(root.str(), resolveLayerDirs(rules, root.str()));
	bool sawIni = false;
	bool sawLang = false;
	bool sawUi = false;
	bool sawOutside = false;
	for (const ArchiveCandidate& candidate : found) {
		REQUIRE(!candidateUnderExcludedDir(candidate.relPath));
		REQUIRE(!candidateUnderExcludedDir(candidate.path));
		const std::string rel = lower(candidate.relPath);
		if (rel == "ini.big") {
			sawIni = true;
		}
		if (rel == "lang/english.big") {
			sawLang = true;
		}
		if (rel == "mobilelayer/ui.big") {
			sawUi = true;
		}
		if (rel.find("ok.big") != std::string::npos) {
			sawOutside = true;
		}
	}
	REQUIRE(sawIni);
	REQUIRE(sawLang);
	REQUIRE(sawUi);
	REQUIRE(sawOutside);

	MountedVfs vfs(configFor(root));
	REQUIRE_EQ(vfs.fs().getMountedArchiveCount(), 4);
	std::string data;
	REQUIRE(vfs.read("data\\ini\\gamedata.ini", data));
	REQUIRE_EQ(data, std::string("PATCH"));
	REQUIRE(vfs.read("data\\ini\\neutralunits.ini", data));
	REQUIRE_EQ(data, std::string("BASE"));
	REQUIRE(vfs.winner("data\\ini\\neutralunits.ini").find("ini.big") != std::string::npos);
	REQUIRE(vfs.winner("data\\ini\\neutralunits.ini").find("500_900") == std::string::npos);
	REQUIRE(!vfs.exists("only_zh.ini"));
	REQUIRE(!vfs.exists("only_side.ini"));
	REQUIRE(!vfs.exists("only_also.ini"));
	REQUIRE(!vfs.exists("only_loose.ini"));
	REQUIRE(vfs.read("lang.ini", data));
	REQUIRE_EQ(data, std::string("LANG"));
	REQUIRE(vfs.read("hud.ini", data));
	REQUIRE_EQ(data, std::string("UI"));
}

// -mod <file> stores the path in m_modBIG; -mod <dir> stores it in m_modDir
// (CommandLine::parseMod). loadMods() still mounts both with overwrite, including
// a file that lives inside a directory scanArchives skipped. The community patch
// stays off for BfmeBIGFileSystem and on for StdBIGFileSystem.
// BFME: E1-04 installs made before the rename still have the old folder; the
// scan must keep skipping it, in any case.
TEST(bfme_scan_skips_legacy_online_dir)
{
	TempDir root;
	writeBig(root.path() / "ini.big", {{"data\\ini\\gamedata.ini", "BASE"}});
	writeBig(root.path() / "_patch222.big", {{"data\\ini\\gamedata.ini", "PATCH"}});
	writeBig(root.path() / "GeneralsOnlineGameData" / "500_900_CommunityPatch_CoreINI.big", {
		{"data\\ini\\gamedata.ini", "ZH"}, {"only_zh.ini", "ZH"}});
	writeBig(root.path() / "sub" / "generalsonlinegamedata" / "x.big", {{"only_x.ini", "X"}});
	const std::vector<ArchiveCandidate> found =
		scanArchives(root.str(), resolveLayerDirs(defaultBfme1MountRules(), root.str()));
	REQUIRE(!found.empty());
	for (const ArchiveCandidate& candidate : found) {
		REQUIRE(!candidateUnderExcludedDir(candidate.relPath));
	}
	ensureLocalFileSystem();
	MountedVfs vfs(configFor(root));
	std::string data;
	REQUIRE(vfs.read("data\\ini\\gamedata.ini", data));
	REQUIRE_EQ(data, std::string("PATCH"));
	REQUIRE(!vfs.exists("only_zh.ini"));
	REQUIRE(!vfs.exists("only_x.ini"));
}

TEST(bfme_loadmods_mounts_mod_not_community_patch)
{
	TempDir root;
	TempDir user;
	TempDir zhRoot;
	TempDir modDir;
	writeBig(root.path() / "ini.big", {
		{"data\\ini\\gamedata.ini", "BASE"},
		{"data\\ini\\neutralunits.ini", "BASE"},
		{"data\\ini\\object\\gondor.ini", "BASE"}});
	writeBig(root.path() / "_patch222.big", {{"data\\ini\\gamedata.ini", "PATCH"}});
	const std::filesystem::path patch = user.path() / "OnlineGameData" / "500_900_CommunityPatch_CoreINI.big";
	writeBig(patch, {
		{"data\\ini\\gamedata.ini", "ZH"}, {"data\\ini\\neutralunits.ini", "ZH"}, {"only_zh.ini", "ZH"}});
	writeBig(root.path() / "OnlineGameData" / "500_900_CommunityPatch_CoreINI.big", {
		{"data\\ini\\gamedata.ini", "ZH"}, {"data\\ini\\neutralunits.ini", "ZH"}, {"only_zh.ini", "ZH"}});
	const std::filesystem::path modBig = root.path() / "OnlineGameData" / "explicit-mod.big";
	writeBig(modBig, {{"data\\ini\\gamedata.ini", "MODBIG"}, {"only_mod.ini", "MODBIG"}});
	writeBig(modDir.path() / "pack.big", {{"data\\ini\\object\\gondor.ini", "MODDIR"}, {"only_dir.ini", "MODDIR"}});
	writeBig(zhRoot.path() / "ini.big", {
		{"data\\ini\\gamedata.ini", "BASE"}, {"data\\ini\\neutralunits.ini", "BASE"}});
	REQUIRE(!std::filesystem::exists(user.path() / "gx_no_community_patch.txt"));
	ensureLocalFileSystem();

	struct Install {
		GlobalData* data = nullptr;
		explicit Install(const std::string& userDir)
		{
			setTestUserDataDir(userDir);
			data = new GlobalData();
			TheWritableGlobalData = data;
		}
		~Install()
		{
			TheWritableGlobalData = nullptr;
			delete data;
			setTestUserDataDir("");
		}
	} install(user.str());
	TheWritableGlobalData->m_modBIG = modBig.string().c_str();
	TheWritableGlobalData->m_modDir = (modDir.str() + "/").c_str();

	{
		MountedVfs vfs(configFor(root));
		std::string before;
		REQUIRE(vfs.read("data\\ini\\gamedata.ini", before));
		REQUIRE_EQ(before, std::string("PATCH"));
		REQUIRE(vfs.read("data\\ini\\neutralunits.ini", before));
		REQUIRE_EQ(before, std::string("BASE"));
		REQUIRE(!vfs.exists("only_zh.ini"));
		StderrCapture cap;
		vfs.fs().loadMods();
		const std::string log = cap.stop();
		REQUIRE(log.find("community patch skipped (BFME)") != std::string::npos);
		REQUIRE(log.find("community patch mounted") == std::string::npos);
		std::string data;
		REQUIRE(vfs.read("data\\ini\\gamedata.ini", data));
		REQUIRE_EQ(data, std::string("MODBIG"));
		REQUIRE(vfs.winner("data\\ini\\gamedata.ini").find("explicit-mod.big") != std::string::npos);
		REQUIRE(vfs.read("only_mod.ini", data));
		REQUIRE_EQ(data, std::string("MODBIG"));
		REQUIRE(vfs.read("data\\ini\\object\\gondor.ini", data));
		REQUIRE_EQ(data, std::string("MODDIR"));
		REQUIRE(vfs.winner("data\\ini\\object\\gondor.ini").find("pack.big") != std::string::npos);
		REQUIRE(vfs.read("only_dir.ini", data));
		REQUIRE_EQ(data, std::string("MODDIR"));
		REQUIRE(vfs.read("data\\ini\\neutralunits.ini", data));
		REQUIRE_EQ(data, std::string("BASE"));
		REQUIRE(!vfs.exists("only_zh.ini"));
	}

	{
		StdBIGFileSystem zh;
		REQUIRE(zh.loadBigFilesFromDirectory(AsciiString(zhRoot.str().c_str()), "*.big", FALSE));
		StderrCapture cap;
		zh.loadMods();
		const std::string log = cap.stop();
		REQUIRE(log.find("community patch skipped (BFME)") == std::string::npos);
		REQUIRE(log.find("community patch mounted") != std::string::npos);
		REQUIRE_EQ(readAll(zh, "only_zh.ini"), std::string("ZH"));
		REQUIRE_EQ(readAll(zh, "data\\ini\\gamedata.ini"), std::string("MODBIG"));
		REQUIRE_EQ(readAll(zh, "data\\ini\\object\\gondor.ini"), std::string("MODDIR"));
		REQUIRE(std::string(zh.getArchiveFile("data\\ini\\gamedata.ini")->getName().str()).find("explicit-mod.big") != std::string::npos);
	}
}

// A rules file that does not parse must not return from init() in a release build.
TEST(bfme_init_exits_when_rules_do_not_parse)
{
	TempDir root;
	TempDir rulesDir;
	writeBig(root.path() / "ini.big", {{"data\\ini\\gamedata.ini", "BASE"}});
	const std::filesystem::path rules = rulesDir.path() / "bad.cfg";
	writeFile(rules, "not a layer\n");
	ensureLocalFileSystem();
	EnvRestore restore;
	setenv("BFME_GAMEDATA", root.str().c_str(), 1);
	setenv("BFME_VFS_MOUNT_ORDER", rules.string().c_str(), 1);

	const ChildExit child = exitInChild([] {
		BfmeBIGFileSystem fs;
		fs.init();
	});
	REQUIRE(WIFEXITED(child.status));
	REQUIRE(WEXITSTATUS(child.status) != 0);
	REQUIRE(child.err.find("[bfmevfs] FATAL:") != std::string::npos);
	REQUIRE(child.err.find("unknown keyword 'not'") != std::string::npos);
}

// An empty game root mounts nothing. Same loud exit as a missing ZH asset root.
TEST(bfme_init_exits_when_no_archive_mounts)
{
	TempDir root;
	ensureLocalFileSystem();
	BfmeVfsConfig config;
	config.gameRoot = root.str();

	const ChildExit child = exitInChild([config] {
		BfmeBIGFileSystem fs(config);
		fs.init();
	});
	REQUIRE(WIFEXITED(child.status));
	REQUIRE(WEXITSTATUS(child.status) != 0);
	REQUIRE(child.err.find("[bfmevfs] FATAL: No BFME archives were mounted from " + root.str()) != std::string::npos);
}

// maps.big opened. A nested ini.big does not stand in for the root archive.
TEST(bfme_init_exits_when_root_ini_missing)
{
	TempDir root;
	writeBig(root.path() / "maps.big", {{"maps\\x.map", "MAP"}});
	writeBig(root.path() / "lang" / "ini.big", {{"data\\ini\\x.ini", "NESTED"}});
	ensureLocalFileSystem();
	BfmeVfsConfig config;
	config.gameRoot = root.str();

	const ChildExit child = exitInChild([config] {
		BfmeBIGFileSystem fs(config);
		fs.init();
	});
	REQUIRE(WIFEXITED(child.status));
	REQUIRE(WEXITSTATUS(child.status) != 0);
	REQUIRE(child.err.find("[bfmevfs] FATAL: required ini.big was not opened from " + root.str()) != std::string::npos);
	REQUIRE(child.err.find("maps.big") != std::string::npos);
	REQUIRE(child.err.find("[bfmevfs] mounted") != std::string::npos);
	REQUIRE(child.err.find("No BFME archives were mounted") == std::string::npos);
}

// The root archive is present and unreadable. Other archives still mounted.
TEST(bfme_init_exits_when_root_ini_unreadable)
{
	TempDir root;
	writeBig(root.path() / "maps.big", {{"maps\\x.map", "MAP"}});
	writeFile(root.path() / "ini.big", "not a big file at all");
	ensureLocalFileSystem();
	BfmeVfsConfig config;
	config.gameRoot = root.str();

	const ChildExit child = exitInChild([config] {
		BfmeBIGFileSystem fs(config);
		fs.init();
	});
	REQUIRE(WIFEXITED(child.status));
	REQUIRE(WEXITSTATUS(child.status) != 0);
	REQUIRE(child.err.find("[bfmevfs] FATAL: required ini.big was not opened from " + root.str()) != std::string::npos);
	REQUIRE(child.err.find("FAILED TO OPEN") != std::string::npos);
	REQUIRE(child.err.find("[bfmevfs] mounted") != std::string::npos);
}

TEST(bfme_root_ini_name_is_case_insensitive)
{
	TempDir root;
	writeBig(root.path() / "INI.BIG", {{"data\\ini\\gamedata.ini", "BASE"}});
	writeBig(root.path() / "maps.big", {{"m.map", "M"}});
	MountedVfs vfs(configFor(root));
	std::string data;
	REQUIRE(vfs.read("data\\ini\\gamedata.ini", data));
	REQUIRE_EQ(data, std::string("BASE"));
}

// Dumps are not next to the game copy and are not in git. Only BFME_ORACLE_DIR counts.
TEST(oracle_dir_requires_bfme_oracle_dir)
{
	struct Restore {
		bool hadRoot;
		std::string root;
		bool hadOracle;
		std::string oracle;
		Restore()
		{
			const char* g = getenv("BFME_GAMEDATA");
			hadRoot = g != nullptr;
			if (hadRoot) {
				root = g;
			}
			const char* o = getenv("BFME_ORACLE_DIR");
			hadOracle = o != nullptr;
			if (hadOracle) {
				oracle = o;
			}
		}
		~Restore()
		{
			if (hadRoot) {
				setenv("BFME_GAMEDATA", root.c_str(), 1);
			} else {
				unsetenv("BFME_GAMEDATA");
			}
			if (hadOracle) {
				setenv("BFME_ORACLE_DIR", oracle.c_str(), 1);
			} else {
				unsetenv("BFME_ORACLE_DIR");
			}
		}
	} restore;

	TempDir root;
	unsetenv("BFME_ORACLE_DIR");
	setenv("BFME_GAMEDATA", root.str().c_str(), 1);
	REQUIRE(oracleDir().empty());
	setenv("BFME_ORACLE_DIR", "/tmp/bfme-oracle-not-created", 1);
	REQUIRE_EQ(oracleDir(), std::string("/tmp/bfme-oracle-not-created"));
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
