// BFME1 Mobile
// SPDX-License-Identifier: GPL-3.0-or-later
#include "PreRTS.h"

#include "Common/BfmeLaunch.h"
#include <string>
#include "Common/BfmeMapPath.h"

#include "Common/GlobalData.h"
#include "Common/MessageStream.h"
#include "Common/PlayerTemplate.h"
#include "Common/RandomValue.h"
#include "GameClient/MapUtil.h"
#include "GameLogic/GameLogic.h"
#include "GameNetwork/GameInfo.h"

AsciiString BfmeDefaultMapPath()
{
	return AsciiString(BfmeDefaultMapPathString());
}

static Int firstPlayableTemplate()
{
	if (ThePlayerTemplateStore == nullptr)
		return PLAYERTEMPLATE_RANDOM;
	for (Int i = 0; i < ThePlayerTemplateStore->getPlayerTemplateCount(); ++i)
	{
		const PlayerTemplate *pt = ThePlayerTemplateStore->getNthPlayerTemplate(i);
		if (pt && pt->isPlayableSide())
			return i;
	}
	return PLAYERTEMPLATE_RANDOM;
}

void BfmeStartSkirmish(const AsciiString &mapPath)
{
	BfmeStartSkirmish(mapPath, nullptr, FALSE);
}

// BFME: E1-40c. PlayerTemplate index by side ("Rohan") or name ("FactionRohan"); -1 if none.
static Int templateBySide(const char *side)
{
	if (ThePlayerTemplateStore == nullptr)
		return -1;
	for (Int i = 0; i < ThePlayerTemplateStore->getPlayerTemplateCount(); ++i)
	{
		const PlayerTemplate *pt = ThePlayerTemplateStore->getNthPlayerTemplate(i);
		if (pt && (pt->getSide().compareNoCase(side) == 0 || pt->getName().compareNoCase(side) == 0))
			return i;
	}
	return -1;
}

void BfmeStartSkirmish(const AsciiString &mapPath, const char *aiSides, Bool observer)
{
	if (TheSkirmishGameInfo == nullptr)
		TheSkirmishGameInfo = NEW SkirmishGameInfo;
	else if (TheSkirmishGameInfo->isInGame())
		TheSkirmishGameInfo->endGame();
	TheSkirmishGameInfo->init();
	TheSkirmishGameInfo->clearSlotList();
	TheSkirmishGameInfo->reset();
	TheSkirmishGameInfo->setLocalIP(TheSkirmishGameInfo->getSlot(0)->getIP());
	TheSkirmishGameInfo->enterGame();

	Int playerTemplate = firstPlayableTemplate();
	if (observer)	// BFME: E1-40c. Same as the ZH menu's observer choice: takes no start position.
		playerTemplate = PLAYERTEMPLATE_OBSERVER;
	GameSlot slot;
	slot.setName(UnicodeString(L"Player"));
	slot.setState(SLOT_PLAYER, UnicodeString(L"Player"));
	slot.setColor(-1);
	slot.setStartPos(-1);
	slot.setPlayerTemplate(playerTemplate);
	TheSkirmishGameInfo->setSlot(0, slot);
	// Slots 1.. stay SLOT_OPEN after clearSlotList() unless AI sides are given.
	// BFME: E1-40c. One medium AI per side in aiSides.
	Int aiSlot = 1;
	if (aiSides != nullptr)
	{
		std::string list(aiSides);
		size_t pos = 0;
		while (pos <= list.size() && aiSlot < MAX_SLOTS)
		{
			size_t comma = list.find(',', pos);
			std::string side = list.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos);
			pos = (comma == std::string::npos) ? list.size() + 1 : comma + 1;
			if (side.empty())
				continue;
			const Int t = templateBySide(side.c_str());
			if (t < 0)
			{
				fprintf(stderr, "[launch] BFME: E1-40c unknown AI side '%s'\n", side.c_str());
				continue;
			}
			GameSlot ai;
			ai.setState(SLOT_MED_AI);
			ai.setColor(-1);
			ai.setStartPos(-1);
			ai.setPlayerTemplate(t);
			TheSkirmishGameInfo->setSlot(aiSlot, ai);
			fprintf(stderr, "[launch] BFME: E1-40c AI slot %d side %s (template %d)\n", aiSlot, side.c_str(), t);
			++aiSlot;
		}
	}

	TheSkirmishGameInfo->setSeed(0);
	TheSkirmishGameInfo->setMap(mapPath);
	const MapMetaData *md = TheMapCache ? TheMapCache->findMap(mapPath) : nullptr;
	TheSkirmishGameInfo->setMapCRC(md ? md->m_CRC : 0);
	TheSkirmishGameInfo->setMapSize(md ? md->m_filesize : 0);

	fprintf(stderr, "[launch] BFME: E1-39 skirmish '%s' (in MapCache: %s, player template %d of %d)\n",
		mapPath.str(), md ? "yes" : "no", playerTemplate,
		ThePlayerTemplateStore ? ThePlayerTemplateStore->getPlayerTemplateCount() : 0);

	TheWritableGlobalData->m_pendingFile = mapPath;
	TheWritableGlobalData->m_mapName = mapPath;
	TheSkirmishGameInfo->startGame(0);

	InitRandom(TheSkirmishGameInfo->getSeed());
	GameMessage *msg = TheMessageStream->appendMessage(GameMessage::MSG_NEW_GAME);
	msg->appendIntegerArgument(GAME_SKIRMISH);
	msg->appendIntegerArgument(DIFFICULTY_NORMAL);
	msg->appendIntegerArgument(0);
	msg->appendIntegerArgument(TheGlobalData->m_framesPerSecondLimit);
}
