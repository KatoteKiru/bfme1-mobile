// BFME1 Mobile — tests
// SPDX-License-Identifier: GPL-3.0-or-later
// BFME: E1-39. -file map path handling (GeneralsMD/Code/GameEngine/Include/Common/BfmeMapPath.h).
#include "../vfs/TestHarness.h"

#include "Common/BfmeMapPath.h"

TEST(default_map_is_long_path)
{
	REQUIRE(BfmeIsLongMapPath(BfmeDefaultMapPathString()));
	REQUIRE_EQ(std::string(BfmeDefaultMapPathString()), std::string("maps\\map mp rohan\\map mp rohan.map"));
}

TEST(long_paths_detected)
{
	REQUIRE(BfmeIsLongMapPath("maps\\map mp wold\\map mp wold.map"));
	REQUIRE(BfmeIsLongMapPath("Maps/Map MP Wold/map mp wold.MAP"));
	REQUIRE(BfmeIsLongMapPath("data\\maps\\x\\x.map"));
}

TEST(short_and_bad_paths_not_long)
{
	REQUIRE(!BfmeIsLongMapPath("maps\\map mp wold.map"));
	REQUIRE(!BfmeIsLongMapPath("map mp wold.map"));
	REQUIRE(!BfmeIsLongMapPath("maps\\a\\b.map"));
	REQUIRE(!BfmeIsLongMapPath("maps\\a\\a.ini"));
	REQUIRE(!BfmeIsLongMapPath("\\a.map"));
	REQUIRE(!BfmeIsLongMapPath(""));
	REQUIRE(!BfmeIsLongMapPath(nullptr));
}
