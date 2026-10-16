/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2025 Electronic Arts Inc.
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
**
**	This program is distributed in the hope that it will be useful,
**	but WITHOUT ANY WARRANTY; without even the implied warranty of
**	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**	GNU General Public License for more details.
**
**	You should have received a copy of the GNU General Public License
**	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

// BFME: E1-52 top-level map chunks that Zero Hour stores inside SidesList.
#pragma once

#include "Common/DataChunk.h"

Bool BfmeParseTeamsDataChunk(DataChunkInput &file, DataChunkInfo *info, void *userData);
Bool BfmeParsePlayerScriptsListDataChunk(DataChunkInput &file, DataChunkInfo *info, void *userData);
Bool BfmeParseBuildListsDataChunk(DataChunkInput &file, DataChunkInfo *info, void *userData);

// BFME: E1-52 running totals for the gamedata remainder report. Records, not chunk counts.
void BfmeResetParseCounts();
Int BfmeParsedTeamRecords();
Int BfmeDroppedTeamRecords();
Int BfmeParsedScriptRecords();
Int BfmeParsedBuildRecords();

// Set when a top-level chunk throws because bytes remain. Cleared by a reset or a later chunk start.
void BfmeResetChunkRemainder();
UnsignedInt BfmeLastChunkRemainder();
const char *BfmeLastChunkRemainderName();
