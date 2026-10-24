// BFME1 Mobile
// SPDX-License-Identifier: GPL-3.0-or-later
///////////////////////////////////////////////////////////////////////////////////////////////////
// BFME: E1-40b. See CastleBehavior.h.

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "Common/INI.h"
#include "Common/Xfer.h"
#include "GameLogic/Module/CastleBehavior.h"

//-------------------------------------------------------------------------------------------------
const CastleBehaviorModuleData::Castle* CastleBehaviorModuleData::findCastle(const AsciiString& side) const
{
	for (size_t i = 0; i < m_castles.size(); ++i)
		if (m_castles[i].m_faction.compareNoCase(side) == 0)
			return &m_castles[i];
	return nullptr;
}

//-------------------------------------------------------------------------------------------------
// CastleToUnpackForFaction = <Faction> <BaseName> <Cost>
void CastleBehaviorModuleData::parseCastleToUnpack(INI* ini, void* instance, void* /*store*/, const void* /*userData*/)
{
	CastleBehaviorModuleData* self = static_cast<CastleBehaviorModuleData*>(instance);
	Castle c;
	c.m_faction = ini->getNextToken();
	c.m_base = ini->getNextToken();
	c.m_cost = 0.0f;
	if (const char* tok = ini->getNextTokenOrNull())
		c.m_cost = INI::scanReal(tok);
	self->m_castles.push_back(c);
}

//-------------------------------------------------------------------------------------------------
void CastleBehaviorModuleData::buildFieldParse(MultiIniFieldParse& p)
{
	UpdateModuleData::buildFieldParse(p);
	static const FieldParse dataFieldParse[] =
	{
		{ "CastleToUnpackForFaction",	CastleBehaviorModuleData::parseCastleToUnpack,	nullptr, 0 },
		{ "MaxCastleRadius",					INI::parseReal,	nullptr, offsetof( CastleBehaviorModuleData, m_maxCastleRadius ) },
		{ "ScanDistance",							INI::parseReal,	nullptr, offsetof( CastleBehaviorModuleData, m_scanDistance ) },
		{ nullptr, nullptr, nullptr, 0 }
	};
	p.add(dataFieldParse);
}

//-------------------------------------------------------------------------------------------------
CastleBehavior::CastleBehavior( Thing *thing, const ModuleData* moduleData ) : UpdateModule( thing, moduleData ), m_unpacked(FALSE)
{
}

//-------------------------------------------------------------------------------------------------
CastleBehavior::~CastleBehavior()
{
}

//-------------------------------------------------------------------------------------------------
void CastleBehavior::crc( Xfer *xfer )
{
	UpdateModule::crc( xfer );
}

//-------------------------------------------------------------------------------------------------
// Version Info: 1: Initial version
void CastleBehavior::xfer( Xfer *xfer )
{
	XferVersion currentVersion = 1;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );
	UpdateModule::xfer( xfer );
	xfer->xferBool( &m_unpacked );
}

//-------------------------------------------------------------------------------------------------
void CastleBehavior::loadPostProcess()
{
	UpdateModule::loadPostProcess();
}
