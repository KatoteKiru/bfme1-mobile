// BFME1 Mobile — E1-23a: lenient INI line rules. Synthetic INI text only (no EA data).
// SPDX-License-Identifier: GPL-3.0-or-later
#include "Common/INILenientRules.h"

#include <cstdio>
#include <cstring>

using namespace INILenient;

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); ++g_fail; } } while (0)

static StructureStats check(const char *t) { return checkStructure(t, std::strlen(t)); }

int main()
{
	// classifyLine
	CHECK(classifyLine("") == LINE_EMPTY);
	CHECK(classifyLine("   ") == LINE_EMPTY);
	CHECK(classifyLine(nullptr) == LINE_EMPTY);
	CHECK(classifyLine("#define FOO 1") == LINE_EMPTY);
	CHECK(classifyLine("End") == LINE_END);
	CHECK(classifyLine("  END  ") == LINE_END);
	CHECK(classifyLine("ShowProps = No") == LINE_FIELD);	// first unknown field in BFME GameData.ini
	CHECK(classifyLine("Class = Type Grass NEXT Region Edoras") == LINE_FIELD);	// terrain.ini
	CHECK(classifyLine("Draw = W3DScriptedModelDraw ModuleTag_01") == LINE_OPENER);
	CHECK(classifyLine("Behavior = AIUpdateInterface Tag") == LINE_OPENER);
	CHECK(classifyLine("Opacity = FCurve") == LINE_OPENER);
	CHECK(classifyLine("Color = DefaultColor") == LINE_OPENER);
	CHECK(classifyLine("Color = R:255 G:0 B:0") == LINE_FIELD);
	CHECK(classifyLine("Transition = DOWN") == LINE_FIELD);
	CHECK(classifyLine("Transition = FadeIn") == LINE_OPENER);
	CHECK(classifyLine("WeaponSet") == LINE_OPENER);
	CHECK(classifyLine("Blank") == LINE_FIELD);
	CHECK(classifyLine("ModelConditionState MOVING") == LINE_OPENER);
	CHECK(classifyLine("Foo Bar") == LINE_FIELD);
	CHECK(classifyLine("BeginScript") == LINE_SCRIPT_BEGIN);
	CHECK(classifyLine("EndScript") == LINE_SCRIPT_END);

	// checkStructure: nested blocks, comments, Lua scripts with their own "end"
	StructureStats s = check(
		"GameData\n  ShowProps = No ; comment End\n  Draw = W3DModelDraw M1\n    DefaultConditionState\n      Model = X\n    End\n  End\nEnd\n"
		"Object Foo\n  BeginScript\n    if x then y() end\n  EndScript\nEnd\n");
	CHECK(s.topLevelBlocks == 2);
	CHECK(s.nestedOpeners == 2);
	CHECK(s.ends == 4);
	CHECK(s.scripts == 1);
	CHECK(s.strayEnds == 0);
	CHECK(s.unclosedAtEof == 0);

	s = check("End\nTerrain Grass\n  Class = Type Grass NEXT Region Edoras\n");
	CHECK(s.strayEnds == 1);
	CHECK(s.unclosedAtEof == 1);

	if (g_fail == 0)
		std::printf("ini.lenient_rules: all checks passed\n");
	return g_fail ? 1 : 0;
}
