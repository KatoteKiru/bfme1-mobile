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

// BFME: E1-52 Teams, PlayerScriptsList and BuildLists as top-level CkMp chunks.
#include "PreRTS.h"

#include "GameLogic/BfmeMapChunks.h"

#include "Common/Errors.h"
#include "Common/GlobalData.h"
#include "Common/NameKeyGenerator.h"
#include "Common/WellKnownKeys.h"
#include "GameLogic/Scripts.h"
#include "GameLogic/SidesList.h"

#include <cstdio>

namespace {

// BFME: E1-52 versions above these are not BFME1. Zero Hour never writes the chunks.
const UnsignedShort K_BFME_TEAMS_VERSION_1 = 1;
const UnsignedShort K_BFME_BUILD_LISTS_VERSION_1 = 1;

void bfmeRejectNewerChunk(const char *chunk, unsigned version, unsigned maxKnown)
{
	if (version > maxKnown) {
		std::fprintf(stderr, "[bfme] E1-52 %s version %u is newer than %u\n", chunk, version, maxKnown);
		throw ERROR_CORRUPT_FILE_FORMAT;
	}
}

int s_teamRecords = 0;
int s_droppedTeamRecords = 0;
int s_scriptRecords = 0;
int s_buildRecords = 0;
UnsignedInt s_lastRemainder = 0;
const char *s_lastRemainderName = "";

int bfmeCountGroupScripts(ScriptGroup *group)
{
	int n = 0;
	for (; group != nullptr; group = group->getNext()) {
		for (Script *script = group->getScript(); script != nullptr; script = script->getNext())
			++n;
		n += bfmeCountGroupScripts(group->getFirstChildGroup());
	}
	return n;
}

int bfmeCountListScripts(ScriptList *list)
{
	if (list == nullptr)
		return 0;
	int n = bfmeCountGroupScripts(list->getScriptGroup());
	for (Script *script = list->getScript(); script != nullptr; script = script->getNext())
		++n;
	return n;
}

void bfmeNoteRemainder(const char *chunk, UnsignedInt left)
{
	s_lastRemainder = left;
	s_lastRemainderName = chunk;
}

// One line per faction name. A gamedata scan has no SidesList chunk, so every map would repeat this.
void bfmeWarnMissingFaction(const AsciiString &faction, Int items)
{
	static AsciiString seen[64];
	static Int seenCount = 0;
	for (Int i = 0; i < seenCount; ++i) {
		if (seen[i] == faction)
			return;
	}
	if (seenCount < 64)
		seen[seenCount++] = faction;
	std::fprintf(stderr, "[bfme] E1-52 BuildLists faction '%s' has no side; %d items dropped\n",
		faction.str(), items);
}

void bfmeRequireBytes(DataChunkInput &file, UnsignedInt n, const char *what)
{
	if (file.getChunkDataSizeLeft() < n) {
		std::fprintf(stderr, "[bfme] E1-52 %s needs %u bytes, %u left\n",
			what, n, file.getChunkDataSizeLeft());
		throw ERROR_CORRUPT_FILE_FORMAT;
	}
}

void bfmeRequireExactEnd(DataChunkInput &file, const char *chunk)
{
	const UnsignedInt left = file.getChunkDataSizeLeft();
	if (left != 0) {
		bfmeNoteRemainder(chunk, left);
		std::fprintf(stderr, "[bfme] E1-52 %s left %u bytes\n", chunk, left);
		throw ERROR_CORRUPT_FILE_FORMAT;
	}
}

SidesInfo *bfmeFindSideForFaction(const AsciiString &faction)
{
	if (TheSidesList == nullptr)
		return nullptr;
	SidesInfo *byName = TheSidesList->findSideInfo(faction);
	if (byName)
		return byName;
	const Int sides = TheSidesList->getNumSides();
	for (Int i = 0; i < sides; ++i) {
		Dict *dict = TheSidesList->getSideInfo(i)->getDict();
		if (dict->getAsciiString(TheKey_playerFaction) == faction)
			return TheSidesList->getSideInfo(i);
	}
	return nullptr;
}

// Same field order as SidesList::ParseSidesDataChunk for version >= 3. The chunk
// version is the container version; every BFME1 item carries the v3 tail.
BuildListInfo *bfmeReadBuildListItem(DataChunkInput &file)
{
	bfmeRequireBytes(file, 2, "BuildLists building name");
	BuildListInfo *info = newInstance(BuildListInfo);
	info->setBuildingName(file.readAsciiString());
	info->setTemplateName(file.readAsciiString());
	Coord3D loc;
	loc.x = file.readReal();
	loc.y = file.readReal();
	loc.z = file.readReal();
	loc.z = 0; // same ground snap as the Zero Hour sides reader
	info->setLocation(loc);
	info->setAngle(file.readReal());
	info->setInitiallyBuilt(file.readByte());
	info->setNumRebuilds(file.readInt());
	info->setScript(file.readAsciiString());
	info->setHealth(file.readInt());
	info->setWhiner(file.readByte());
	info->setUnsellable(file.readByte());
	info->setRepairable(file.readByte());
	return info;
}

} // namespace

Bool BfmeParseTeamsDataChunk(DataChunkInput &file, DataChunkInfo *info, void * /*userData*/)
{
	// BFME: E1-52 int count, then Dict per team. Not the .scb ScriptTeams chunk.
	s_lastRemainder = 0;
	s_lastRemainderName = "";
	bfmeRejectNewerChunk("Teams", info->version, K_BFME_TEAMS_VERSION_1);
	if (TheSidesList == nullptr) {
		std::fprintf(stderr, "[bfme] E1-52 Teams: TheSidesList is null\n");
		throw ERROR_CORRUPT_FILE_FORMAT;
	}
	bfmeRequireBytes(file, sizeof(Int), "Teams count");
	const Int count = file.readInt();
	if (count < 0) {
		std::fprintf(stderr, "[bfme] E1-52 Teams count %d\n", count);
		throw ERROR_CORRUPT_FILE_FORMAT;
	}
	TheSidesList->emptyTeams();
	// BFME: E1-52 with no sides loaded every owner would look missing, so teams are kept.
	// Once SidesList has stored its 16, an owner that is not one of those player names is
	// a side past the cap: read it, drop it, and warn in the same shape as SidesList v6.
	const Bool filterOwners = TheSidesList->getNumSides() > 0;
	Int dropped = 0;
	for (Int i = 0; i < count; ++i) {
		bfmeRequireBytes(file, sizeof(UnsignedShort), "Teams dict");
		Dict team = file.readDict();
		++s_teamRecords;
		if (filterOwners) {
			const AsciiString owner = team.getAsciiString(TheKey_teamOwner);
			if (!owner.isEmpty() && TheSidesList->findSideInfo(owner) == nullptr) {
				++dropped;
				++s_droppedTeamRecords;
				continue;
			}
		}
		TheSidesList->addTeam(&team);
	}
	if (dropped > 0) {
		const char *mapName = "?";
		if (TheGlobalData != nullptr && TheGlobalData->m_mapName.isEmpty() == false)
			mapName = TheGlobalData->m_mapName.str();
		const Int stored = count - dropped;
		DEBUG_LOG(("Teams: map '%s' has %d teams, stored %d, dropped %d", mapName, count, stored, dropped));
		std::fprintf(stderr, "Teams: map '%s' has %d teams, stored %d, dropped %d\n",
			mapName, count, stored, dropped);
	}
	bfmeRequireExactEnd(file, "Teams");
	return true;
}

Bool BfmeParsePlayerScriptsListDataChunk(DataChunkInput &file, DataChunkInfo *info, void *userData)
{
	// BFME: E1-52 same container as the nested chunk (version written by Zero Hour is 5).
	// Distribution matches SidesList::ParseSidesDataChunk after its nested parse.
	s_lastRemainder = 0;
	s_lastRemainderName = "";
	if (!ScriptList::ParseScriptsDataChunk(file, info, userData))
		return false;
	ScriptList *scripts[MAX_PLAYER_COUNT];
	const Int count = ScriptList::getReadScripts(scripts);
	for (Int i = 0; i < count; ++i)
		s_scriptRecords += bfmeCountListScripts(scripts[i]);
	if (file.getChunkDataSizeLeft() != 0) {
		for (Int i = 0; i < count; ++i)
			deleteInstance(scripts[i]);
		const UnsignedInt left = file.getChunkDataSizeLeft();
		bfmeNoteRemainder("PlayerScriptsList", left);
		std::fprintf(stderr, "[bfme] E1-52 PlayerScriptsList left %u bytes\n", left);
		throw ERROR_CORRUPT_FILE_FORMAT;
	}
	if (TheSidesList == nullptr) {
		for (Int i = 0; i < count; ++i)
			deleteInstance(scripts[i]);
		std::fprintf(stderr, "[bfme] E1-52 PlayerScriptsList: TheSidesList is null\n");
		throw ERROR_CORRUPT_FILE_FORMAT;
	}
	for (Int i = 0; i < count; ++i) {
		if (i < TheSidesList->getNumSides()) {
			deleteInstance(TheSidesList->getSideInfo(i)->getScriptList());
			TheSidesList->getSideInfo(i)->setScriptList(scripts[i]);
			scripts[i] = nullptr;
		} else {
			deleteInstance(scripts[i]);
			scripts[i] = nullptr;
		}
	}
	return true;
}

Bool BfmeParseBuildListsDataChunk(DataChunkInput &file, DataChunkInfo *info, void * /*userData*/)
{
	// BFME: E1-52 u32 faction count; each faction is a name key, a u32 item count, then items.
	s_lastRemainder = 0;
	s_lastRemainderName = "";
	bfmeRejectNewerChunk("BuildLists", info->version, K_BFME_BUILD_LISTS_VERSION_1);
	if (TheSidesList == nullptr) {
		std::fprintf(stderr, "[bfme] E1-52 BuildLists: TheSidesList is null\n");
		throw ERROR_CORRUPT_FILE_FORMAT;
	}
	bfmeRequireBytes(file, sizeof(UnsignedInt), "BuildLists count");
	const Int count = file.readInt();
	if (count < 0) {
		std::fprintf(stderr, "[bfme] E1-52 BuildLists count %d\n", count);
		throw ERROR_CORRUPT_FILE_FORMAT;
	}
	for (Int i = 0; i < count; ++i) {
		bfmeRequireBytes(file, sizeof(Int) + sizeof(UnsignedInt), "BuildLists faction");
		const NameKeyType factionKey = file.readNameKey();
		const AsciiString faction = TheNameKeyGenerator->keyToName(factionKey);
		const Int items = file.readInt();
		if (items < 0) {
			std::fprintf(stderr, "[bfme] E1-52 BuildLists '%s' item count %d\n", faction.str(), items);
			throw ERROR_CORRUPT_FILE_FORMAT;
		}
		SidesInfo *side = bfmeFindSideForFaction(faction);
		if (side == nullptr && items > 0)
			bfmeWarnMissingFaction(faction, items);
		for (Int j = 0; j < items; ++j) {
			BuildListInfo *entry = bfmeReadBuildListItem(file);
			if (side)
				side->addToBuildList(entry, j);
			else
				deleteInstance(entry);
			++s_buildRecords;
		}
	}
	bfmeRequireExactEnd(file, "BuildLists");
	return true;
}

void BfmeResetParseCounts()
{
	s_teamRecords = 0;
	s_droppedTeamRecords = 0;
	s_scriptRecords = 0;
	s_buildRecords = 0;
}

Int BfmeParsedTeamRecords() { return s_teamRecords; }
Int BfmeDroppedTeamRecords() { return s_droppedTeamRecords; }
Int BfmeParsedScriptRecords() { return s_scriptRecords; }
Int BfmeParsedBuildRecords() { return s_buildRecords; }

void BfmeResetChunkRemainder()
{
	s_lastRemainder = 0;
	s_lastRemainderName = "";
}

UnsignedInt BfmeLastChunkRemainder() { return s_lastRemainder; }
const char *BfmeLastChunkRemainderName() { return s_lastRemainderName != nullptr ? s_lastRemainderName : ""; }
