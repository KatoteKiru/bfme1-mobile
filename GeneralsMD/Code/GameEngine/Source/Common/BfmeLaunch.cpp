// BFME1 Mobile
// SPDX-License-Identifier: GPL-3.0-or-later
#include "PreRTS.h"

#include "Common/BfmeLaunch.h"
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
	if (TheSkirmishGameInfo == nullptr)
		TheSkirmishGameInfo = NEW SkirmishGameInfo;
	else if (TheSkirmishGameInfo->isInGame())
		TheSkirmishGameInfo->endGame();
	TheSkirmishGameInfo->init();
	TheSkirmishGameInfo->clearSlotList();
	TheSkirmishGameInfo->reset();
	TheSkirmishGameInfo->setLocalIP(TheSkirmishGameInfo->getSlot(0)->getIP());
	TheSkirmishGameInfo->enterGame();

	const Int playerTemplate = firstPlayableTemplate();
	GameSlot slot;
	slot.setName(UnicodeString(L"Player"));
	slot.setState(SLOT_PLAYER, UnicodeString(L"Player"));
	slot.setColor(-1);
	slot.setStartPos(-1);
	slot.setPlayerTemplate(playerTemplate);
	TheSkirmishGameInfo->setSlot(0, slot);
	// Slots 1.. stay SLOT_OPEN after clearSlotList(): no AI player yet.

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
