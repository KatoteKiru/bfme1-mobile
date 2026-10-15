// BFME1 Mobile — engine
// SPDX-License-Identifier: GPL-3.0-or-later
//
// BFME: E1-23a. Line classification rules of the lenient INI mode (see Common/INILenientRules.h).
// Engine-independent on purpose so tests/ini can compile it without the game.
#include "Common/INILenientRules.h"

#include <algorithm>
#include <cctype>
#include <cstring>

namespace INILenient
{
namespace
{
const size_t kMaxLine = 1028;	// INI_MAX_CHARS_PER_LINE (Common/INI.h)

int rulesStricmp( const char *a, const char *b )
{
	for (;; ++a, ++b)
	{
		const int ca = tolower((unsigned char)*a), cb = tolower((unsigned char)*b);
		if (ca != cb || ca == 0)
			return ca - cb;
	}
}

bool lenientIsSep( char ch )
{
	return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n' || ch == '=';
}

// Block-opening fields of BFME1 2.22 that are written with '=' ("Draw = W3DScriptedModelDraw Tag").
// Derived from the 2.22 INI corpus and checked there (docs/bfme/INI_LENIENT.md, tools/ini-lenient).
const char *const s_openersWithEq[] =
{
	"Alpha", "Animation", "AnimationState", "Behavior", "Body", "ClientBehavior", "ClientUpdate", "Draw",
	"EmissionVelocity", "EmissionVolume", "Emitter", "Event", "LodOptions", "ModelConditionState", "Physics",
	"SoundState", "SoundUpgrade", "TransitionState", "Update", "Wind",
};
// Block openers written without '=' but with a name or condition after the token.
const char *const s_openersNoEqMulti[] =
{
	"Act", "AddModule", "AnimationState", "AttackPriority", "Behavior", "Mission", "ModelConditionState", "Object",
	"PlayerPosition", "Region", "ReplaceModule", "SideInfo", "SkirmishBuildList", "Structure", "TransitionState",
};
// A lone token without '=' opens a block ("WeaponSet", "DefaultModelConditionState"), except these.
const char *const s_singleTokenFields[] =
{
	"Blank", "ClearNuggets",
};

template <size_t N>
bool lenientInList( const char *token, const char *const (&list)[N] )
{
	for (size_t i = 0; i < N; ++i)
		if (rulesStricmp(token, list[i]) == 0)
			return true;
	return false;
}

} // namespace

LineKind classifyLine( const char *line )
{
	if (line == nullptr)
		return LINE_EMPTY;
	enum { MAX_TOK = 64 };
	char tok[2][MAX_TOK];
	int count = 0;
	for (const char *p = line; *p; )
	{
		while (*p && lenientIsSep(*p))
			++p;
		if (!*p)
			break;
		const char *e = p;
		while (*e && !lenientIsSep(*e))
			++e;
		if (count < 2)
		{
			const size_t len = std::min((size_t)(e - p), (size_t)MAX_TOK - 1);
			memcpy(tok[count], p, len);
			tok[count][len] = 0;
		}
		++count;
		p = e;
	}
	if (count == 0 || tok[0][0] == '#')
		return LINE_EMPTY;
	const char *t = tok[0];
	if (rulesStricmp(t, "End") == 0)
		return LINE_END;
	if (rulesStricmp(t, "BeginScript") == 0)
		return LINE_SCRIPT_BEGIN;
	if (rulesStricmp(t, "EndScript") == 0)
		return LINE_SCRIPT_END;
	const char *v = count > 1 ? tok[1] : nullptr;
	if (strchr(line, '=') != nullptr)
	{
		if (v && rulesStricmp(v, "FCurve") == 0)
			return LINE_OPENER;	// "Opacity = FCurve" ... End
		if (lenientInList(t, s_openersWithEq))
			return LINE_OPENER;
		if (rulesStricmp(t, "Color") == 0 && strchr(line, ':') == nullptr)
			return LINE_OPENER;	// "Color = DefaultColor" opens; "Color = R:255 G:0 B:0" is a value
		if (rulesStricmp(t, "Transition") == 0 && v && rulesStricmp(v, "DOWN") != 0 && rulesStricmp(v, "UP") != 0)
			return LINE_OPENER;	// window transition; "Transition = DOWN" is a CommandMap field
		if (rulesStricmp(t, "AddEmotion") == 0 && v && rulesStricmp(v, "OVERRIDE") == 0)
			return LINE_OPENER;
		return LINE_FIELD;
	}
	if (count == 1)
		return lenientInList(t, s_singleTokenFields) ? LINE_FIELD : LINE_OPENER;
	return lenientInList(t, s_openersNoEqMulti) ? LINE_OPENER : LINE_FIELD;
}

StructureStats checkStructure( const char *text, size_t length )
{
	StructureStats st;
	memset(&st, 0, sizeof(st));
	int depth = 0;
	bool inScript = false;
	char line[kMaxLine + 1];
	size_t i = 0;
	while (i < length)
	{
		// same normalisation as INI::readLine(): ';' starts a comment, control characters are spaces
		size_t n = 0;
		bool comment = false;
		for (; i < length && text[i] != '\n'; ++i)
		{
			char ch = text[i];
			if (ch == ';')
				comment = true;
			if (comment || n >= kMaxLine)
				continue;
			line[n++] = (ch > 0 && ch < 32) ? ' ' : ch;
		}
		++i;
		line[n] = 0;
		const LineKind k = classifyLine(line);
		if (inScript)
		{
			if (k == LINE_SCRIPT_END)
				inScript = false;
			continue;
		}
		switch (k)
		{
			case LINE_EMPTY: case LINE_SCRIPT_END: break;
			case LINE_SCRIPT_BEGIN: inScript = true; ++st.scripts; break;
			case LINE_END:
				if (depth == 0) ++st.strayEnds;
				else { --depth; ++st.ends; }
				break;
			default:
				if (depth == 0) { depth = 1; ++st.topLevelBlocks; }
				else if (k == LINE_OPENER) { ++depth; ++st.nestedOpeners; }
				break;
		}
	}
	st.unclosedAtEof = (unsigned int)depth;
	return st;
}

} // namespace INILenient
