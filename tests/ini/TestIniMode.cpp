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
#include "Common/LocalFileSystem.h"
#include "StdDevice/Common/StdLocalFileSystem.h"

#include "../vfs/TestHarness.h"

#include <cstdio>
#include <cstdlib>
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
const char *const kZhClass = "Terrain T\n  Class = GRASS\n  Known = 7\nEnd\n";

} // namespace

// The "Terrain" block of theTypeTable; the real one lives in TerrainTypes.cpp.
void INI::parseTerrainDefinition(INI *ini)
{
	ini->getNextToken();	// block name
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
