// BFME1 Mobile — engine
// SPDX-License-Identifier: GPL-3.0-or-later
//
// BFME: E1-23a. Engine-independent part of the lenient INI mode: how a line is classified when
// skipping unknown blocks/fields. No game headers, so tests/ini compiles it standalone.
#pragma once

#include <cstddef>

namespace INILenient
{
	/// What a (comment-stripped) INI line is to the lenient skipper.
	enum LineKind
	{
		LINE_EMPTY,					///< blank, or a preprocessor line
		LINE_END,						///< "End"
		LINE_OPENER,				///< opens a nested block that is closed by its own End
		LINE_FIELD,					///< single-line field
		LINE_SCRIPT_BEGIN,	///< "BeginScript": Lua up to "EndScript", its "end"s are not INI Ends
		LINE_SCRIPT_END			///< "EndScript"
	};
	LineKind classifyLine( const char *line );

	/// Structural check of a whole INI text with the same rules (tests, docs/bfme/INI_LENIENT.md).
	struct StructureStats
	{
		unsigned int topLevelBlocks;
		unsigned int nestedOpeners;
		unsigned int ends;
		unsigned int scripts;
		unsigned int strayEnds;			///< End at depth 0
		unsigned int unclosedAtEof;	///< depth left open at the end of the text
	};
	StructureStats checkStructure( const char *text, size_t length );

}
