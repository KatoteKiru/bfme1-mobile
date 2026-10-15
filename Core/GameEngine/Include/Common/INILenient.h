// BFME1 Mobile — engine
// SPDX-License-Identifier: GPL-3.0-or-later
//
// BFME: E1-23a. Lenient INI mode for BFME1 2.22 data on the Zero Hour parser.
//
// Off by default: the Zero Hour path in INI.cpp (exceptions, DEBUG_CRASH) is unchanged. It is
// switched on only when the BFME archive filesystem is selected (SDL3GameEngine::
// createArchiveFileSystem, BFME_GAMEDATA). Then INI.cpp:
//   - executes `#define NAME value` and substitutes NAME in later lines (SAGE macros, global);
//   - skips an unknown field, together with its sub-block if the line opens one;
//   - skips an unknown top-level block up to its matching End;
//   - catches a field whose value does not parse, keeps the Zero Hour default and skips the
//     field's sub-block; a top-level block whose parser throws is skipped to its End.
// An unknown field is logged once per field name, other skips once per (file, block, token), as
//   [ini-lenient] skip field|block|value <file>:<line> <block> <token> [detail]
// and appended to the TSV named by BFME_INI_LENIENT_REPORT (input for the full E1-23).
// Rules and their check against the 2.22 data: docs/bfme/INI_LENIENT.md.
// Kept out of INI.h on purpose: INI.h is included by ~1000 engine files.
#pragma once

#include "Lib/BaseType.h"
#include "Common/INILenientRules.h"	// LineKind, classifyLine, checkStructure

class INI;

namespace INILenient
{
	void setEnabled( Bool on );		///< TRUE only with BfmeBIGFileSystem (SDL3GameEngine::createArchiveFileSystem)
	Bool isEnabled();

	/// Record one skip. kind: "field" (logged once per field name), "block" or "value" (once per (file, block, token)).
	void note( const char *kind, const char *file, UnsignedInt line, const char *block, const char *token, const char *detail );

	struct Counters
	{
		UnsignedInt fields, blocks, values;							///< all occurrences
		UnsignedInt uniqueFields, uniqueBlocks, uniqueValues;	///< distinct (file, block, token)
		UnsignedInt subBlocksSkipped, scriptsSkipped, linesSkipped;
		UnsignedInt macrosDefined, macroSubstitutions;
	};
	/// Lenient mode only: remember the bad token and throw INI_INVALID_DATA, so the field is skipped
	/// and logged as a "value" skip. No-op in strict (Zero Hour) mode: the caller keeps its old path.
	/// For ZH parsers that would otherwise use a failed lookup (-1) as an array index.
	void rejectToken( const char *token );
	/// Lenient mode only: log a "block" skip (e.g. an unknown module class) at the current field of `ini`
	/// and throw INI_INVALID_DATA; INI then skips that field's sub-block without logging it again.
	/// No-op in strict mode.
	void rejectBlock( INI *ini, const char *token, const char *detail );

	Counters counters();
	void printSummary( const char *where, Bool withTopFiles );
	void reset();		///< tests only: forget macros, counters and the dedup set
}
