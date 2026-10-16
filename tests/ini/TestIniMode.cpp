// BFME1 Mobile — tests (E1-23a/E1-26b): INI strictness by mode. Synthetic INI text only (no EA data).
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The real INI.cpp loads small files from a temp directory. Zero Hour mode (the default, no
// BFME_GAMEDATA) must behave as upstream: throw on unknown blocks, bad index-list values and a
// missing End; an unknown field is only a DEBUG_CRASH there (no-op in release) and is skipped
// silently. Lenient (BFME) mode must log and skip all of these and keep parsing the file.
#include "PreRTS.h"

#include "Common/FileSystem.h"
#include "Common/INI.h"
#include "Common/INIException.h"
#include "Common/INILenient.h"
#include "bfme/vfs/BfmeBIGFileSystem.h"
#include "../vfs/EngineFixture.h"
#include "../vfs/SyntheticBig.h"
#include "Common/AudioEventInfo.h"
#include "Common/LocalFileSystem.h"
#include "StdDevice/Common/StdLocalFileSystem.h"

#include "../vfs/TestHarness.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <unistd.h>

namespace {

// A Terrain block parsed with a test table: one plain field and the Zero Hour index list used by
// TerrainType "Class" (TerrainTypes.cpp before E1-26b).
struct TestTerrain {
	Int known = 0;
	Int cls = -1;
};
const char *const s_classNames[] = { "DESERT_1", "GRASS", nullptr };
const FieldParse s_testTerrainTable[] = {
	{ "Known", INI::parseInt, nullptr, offsetof(TestTerrain, known) },
	{ "Class", INI::parseIndexList, s_classNames, offsetof(TestTerrain, cls) },
	{ nullptr, nullptr, nullptr, 0 },
};
TestTerrain s_parsed;

// E1-37: "Terrain Audio" blocks are parsed with the real AudioEventInfo table into raw zeroed storage
// (only plain fields are used, no Sounds lists), so the test needs no AudioManager or memory pool.
alignas(16) unsigned char s_audio[sizeof(AudioEventInfo)];
AudioEventInfo &audio() { return *reinterpret_cast<AudioEventInfo *>(s_audio); }

std::string writeIni(const char *name, const char *text)
{
	static std::string dir;
	if (dir.empty()) {
		char tmpl[] = "/tmp/bfme-ini-mode-XXXXXX";
		REQUIRE(mkdtemp(tmpl) != nullptr);
		dir = tmpl;
	}
	const std::string path = dir + "/" + name;
	std::ofstream(path, std::ios::binary) << text;
	return path;
}

void ensureFileSystem()
{
	if (TheLocalFileSystem == nullptr) {
		TheLocalFileSystem = new StdLocalFileSystem;
		TheLocalFileSystem->init();
	}
	if (TheFileSystem == nullptr)
		TheFileSystem = new FileSystem;
}

// TRUE if INI::load threw.
bool loadThrows(const std::string &path, bool lenient)
{
	ensureFileSystem();
	INILenient::reset();
	INILenient::setEnabled(lenient ? TRUE : FALSE);
	s_parsed = TestTerrain();
	bool threw = false;
	try {
		INI ini;
		ini.load(AsciiString(path.c_str()), INI_LOAD_OVERWRITE, nullptr);
	} catch (...) {
		threw = true;
	}
	INILenient::setEnabled(FALSE);
	return threw;
}

const char *const kUnknownBlock = "BfmeOnlyBlock Foo\n  Something = 1\nEnd\nTerrain T\n  Known = 7\nEnd\n";
const char *const kUnknownField = "Terrain T\n  ShowProps = No\n  Known = 7\nEnd\n";
const char *const kBfmeClass = "Terrain T\n  Class = Type Grass NEXT Region Edoras\n  Known = 7\nEnd\n";
const char *const kMissingEnd = "Terrain T\n  Known = 7\n";
// BFME Data\INI\Default\Music.ini shape (own text): a Type value ZH does not know, then three
// fields that are not in the ZH AudioEventInfo table, then a known one.
const char *const kBfmeAudio = "Terrain Audio\n  Volume = 50.0\n  Type = DEFAULT\n  ReverbEffectLevel = 0\n"
	"  DryLevel = 100\n  SubmixSlider = music\n  Limit = 3\nEnd\n";
const char *const kZhClass = "Terrain T\n  Class = GRASS\n  Known = 7\nEnd\n";

} // namespace

// The "Terrain" block of theTypeTable; the real one lives in TerrainTypes.cpp.
void INI::parseTerrainDefinition(INI *ini)
{
	const char *name = ini->getNextToken();	// block name
	if (strcmp(name, "Audio") == 0)
		ini->initFromINI(s_audio, AudioEventInfo::m_audioEventInfo);
	else
		ini->initFromINI(&s_parsed, s_testTerrainTable);
}

TEST(zh_mode_is_default_off)
{
	REQUIRE(!INILenient::isEnabled());
}

TEST(zh_mode_throws_on_unknown_block)
{
	REQUIRE(loadThrows(writeIni("block.ini", kUnknownBlock), false));
}

TEST(zh_mode_ignores_unknown_field_unlogged)
{
	REQUIRE(!loadThrows(writeIni("field.ini", kUnknownField), false));
	REQUIRE(s_parsed.known == 7);
	REQUIRE(INILenient::counters().fields == 0);
}

TEST(zh_mode_throws_on_bad_index_list_value)
{
	REQUIRE(loadThrows(writeIni("class.ini", kBfmeClass), false));
}

TEST(zh_mode_throws_on_missing_end)
{
	REQUIRE(loadThrows(writeIni("noend.ini", kMissingEnd), false));
}

TEST(zh_mode_parses_valid_file)
{
	REQUIRE(!loadThrows(writeIni("zh.ini", kZhClass), false));
	REQUIRE(s_parsed.cls == 1);
	REQUIRE(s_parsed.known == 7);
	REQUIRE(INILenient::counters().fields + INILenient::counters().blocks + INILenient::counters().values == 0);
}

TEST(bfme_mode_skips_unknown_block)
{
	REQUIRE(!loadThrows(writeIni("block.ini", kUnknownBlock), true));
	REQUIRE(s_parsed.known == 7);
	REQUIRE(INILenient::counters().blocks == 1);
}

TEST(bfme_mode_skips_unknown_field)
{
	REQUIRE(!loadThrows(writeIni("field.ini", kUnknownField), true));
	REQUIRE(s_parsed.known == 7);
	REQUIRE(INILenient::counters().fields == 1);
}

TEST(bfme_mode_skips_bad_index_list_value)
{
	REQUIRE(!loadThrows(writeIni("class.ini", kBfmeClass), true));
	REQUIRE(s_parsed.known == 7);
	REQUIRE(INILenient::counters().values == 1);
}

TEST(bfme_mode_closes_block_at_eof)
{
	REQUIRE(!loadThrows(writeIni("noend.ini", kMissingEnd), true));
	REQUIRE(s_parsed.known == 7);
}

// E1-37: findFieldParse() stops only at token == nullptr. The AudioEventInfo table lost that end
// marker, and the first unknown audio field read past the array (SIGBUS on the Pixel).
TEST(audio_field_table_has_end_marker)
{
	int n = 0;
	while (n < 256 && AudioEventInfo::m_audioEventInfo[n].token != nullptr)
		++n;
	REQUIRE(n < 256);
	REQUIRE(n > 0);
	REQUIRE(AudioEventInfo::m_audioEventInfo[n].parse == nullptr);
}

TEST(bfme_mode_skips_unknown_audio_fields)
{
	std::memset(s_audio, 0, sizeof(s_audio));
	REQUIRE(!loadThrows(writeIni("music.ini", kBfmeAudio), true));
	REQUIRE(audio().m_limit == 3);
	REQUIRE(INILenient::counters().values == 1);	// Type = DEFAULT
	REQUIRE(INILenient::counters().fields == 3);	// ReverbEffectLevel, DryLevel, SubmixSlider
}

// E1-38: a Zero Hour-only INI directory (Data\INI\ChallengeMode) is absent from BFME data. Zero Hour
// mode still throws INI_CANT_OPEN_FILE; lenient mode logs it and returns 0 files.
static bool loadDirThrows(bool lenient, UnsignedInt &files)
{
	ensureFileSystem();
	INILenient::reset();
	INILenient::setEnabled(lenient ? TRUE : FALSE);
	bool threw = false;
	files = 99;
	const std::string present = writeIni("present.ini", "");
	const std::string dir = present + "-no-such-dir";
	// loadFileDirectory also asks TheArchiveFileSystem: mount one synthetic BIG without that directory.
	BfmeVfsConfig config;
	config.gameRoot = present.substr(0, present.rfind('/'));
	bfmetest::writeBig(config.gameRoot + "/ini.big", {{"data\\ini\\other.ini", ""}});
	bfmetest::MountedVfs vfs(config);
	try {
		INI ini;
		files = ini.loadFileDirectory(AsciiString(dir.c_str()), INI_LOAD_OVERWRITE, nullptr);
	} catch (...) {
		threw = true;
	}
	INILenient::setEnabled(FALSE);
	return threw;
}

TEST(zh_mode_throws_on_missing_directory)
{
	UnsignedInt files = 0;
	REQUIRE(loadDirThrows(false, files));
}

TEST(bfme_mode_skips_missing_directory)
{
	UnsignedInt files = 0;
	REQUIRE(!loadDirThrows(true, files));
	REQUIRE(files == 0);
}
