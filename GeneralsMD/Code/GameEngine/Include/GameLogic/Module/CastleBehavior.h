// BFME1 Mobile
// SPDX-License-Identifier: GPL-3.0-or-later
///////////////////////////////////////////////////////////////////////////////////////////////////
#pragma once

// BFME: E1-40b. BFME1 CastleBehavior (castle flags, civilianbuildings.ini CastleFlag): which base
// (bases\<name>\<name>.bse) each faction unpacks on the flag. Data only; the start-time unpack is
// in GameLogic.cpp (bfmeUnpackStartCastles). Reference: OpenSAGE CastleBehavior.Unpack.

#include "GameLogic/Module/UpdateModule.h"
#include <vector>

class Thing;

class CastleBehaviorModuleData : public UpdateModuleData
{
public:
	struct Castle
	{
		AsciiString m_faction;		///< PlayerTemplate side, e.g. "Rohan"
		AsciiString m_base;				///< base name, e.g. "Castle_Rohan" -> bases\Castle_Rohan\Castle_Rohan.bse
		Real m_cost;
	};
	std::vector<Castle> m_castles;
	Real m_maxCastleRadius;
	Real m_scanDistance;

	CastleBehaviorModuleData() : m_maxCastleRadius(0.0f), m_scanDistance(0.0f) {}

	const Castle* findCastle(const AsciiString& side) const;

	static void parseCastleToUnpack(INI* ini, void* instance, void* store, const void* userData);
	static void buildFieldParse(MultiIniFieldParse& p);
};

class CastleBehavior : public UpdateModule
{
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE( CastleBehavior, "CastleBehavior" )
	MAKE_STANDARD_MODULE_MACRO_WITH_MODULE_DATA( CastleBehavior, CastleBehaviorModuleData )

public:
	CastleBehavior( Thing *thing, const ModuleData* moduleData );
	// virtual destructor prototype provided by memory pool declaration

	virtual UpdateSleepTime update() override { return UPDATE_SLEEP_FOREVER; }	// data only for now

	const CastleBehaviorModuleData* getCastleData() const { return getCastleBehaviorModuleData(); }
	Bool isUnpacked() const { return m_unpacked; }
	void setUnpacked() { m_unpacked = TRUE; }

protected:
	Bool m_unpacked;
};
