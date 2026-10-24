// BFME1 Mobile — engine
// SPDX-License-Identifier: GPL-3.0-or-later
//
// BFME: E2-04. AttributeModifierAuraUpdate, AttributeModifierUpgrade, ExperienceLevelCreate and
// LevelUpUpgrade. See GameLogic/Module/BfmeAttributeModules.h.
#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "Common/INI.h"
#include "Common/ThingTemplate.h"
#include "Common/Xfer.h"
#include "GameLogic/BfmeAttributeModifiers.h"
#include "GameLogic/ExperienceTracker.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/BfmeAttributeModules.h"
#include "GameLogic/Object.h"
#include "GameLogic/PartitionManager.h"

namespace
{
// Fields of later games (AntiCategory, MaxActiveRank, AffectContainedOnly, ...): read and dropped.
void parseIgnoredLine( INI* ini, void* /*instance*/, void* /*store*/, const void* /*userData*/ )
{
	while( ini->getNextTokenOrNull() != nullptr )
		;
}
}

//=================================================================================================
// AttributeModifierAuraUpdate
//=================================================================================================
AttributeModifierAuraUpdateModuleData::AttributeModifierAuraUpdateModuleData() :
	m_startsActive( FALSE ),
	m_refreshDelay( LOGICFRAMES_PER_SECOND ),
	m_range( 0.0f ),
	m_targetEnemy( FALSE ),
	m_allowSelf( FALSE ),
	m_runWhileDead( FALSE )
{
}

void AttributeModifierAuraUpdateModuleData::buildFieldParse( MultiIniFieldParse& p )
{
	static const FieldParse dataFieldParse[] =
	{
		{ "StartsActive",			INI::parseBool,									nullptr,	offsetof( AttributeModifierAuraUpdateModuleData, m_startsActive ) },
		{ "BonusName",				INI::parseAsciiString,					nullptr,	offsetof( AttributeModifierAuraUpdateModuleData, m_bonusName ) },
		{ "RefreshDelay",			INI::parseDurationUnsignedInt,	nullptr,	offsetof( AttributeModifierAuraUpdateModuleData, m_refreshDelay ) },
		{ "Range",						INI::parseReal,									nullptr,	offsetof( AttributeModifierAuraUpdateModuleData, m_range ) },
		{ "TargetEnemy",			INI::parseBool,									nullptr,	offsetof( AttributeModifierAuraUpdateModuleData, m_targetEnemy ) },
		{ "AllowSelf",				INI::parseBool,									nullptr,	offsetof( AttributeModifierAuraUpdateModuleData, m_allowSelf ) },
		{ "RunWhileDead",			INI::parseBool,									nullptr,	offsetof( AttributeModifierAuraUpdateModuleData, m_runWhileDead ) },
		{ "ObjectFilter",			BfmeObjectFilter::parseFromINI,	nullptr,	offsetof( AttributeModifierAuraUpdateModuleData, m_objectFilter ) },
		{ "RequiredConditions",	parseIgnoredLine,							nullptr,	0 },
		{ "AntiCategory",			parseIgnoredLine,								nullptr,	0 },
		{ "AntiFX",						parseIgnoredLine,								nullptr,	0 },
		{ "AllowPowerWhenAttacking",	parseIgnoredLine,				nullptr,	0 },
		{ "MaxActiveRank",		parseIgnoredLine,								nullptr,	0 },
		{ "AffectContainedOnly",	parseIgnoredLine,						nullptr,	0 },
		{ nullptr, nullptr, nullptr, 0 }
	};
	UpdateModuleData::buildFieldParse( p );
	p.add( dataFieldParse );
	p.add( UpgradeMuxData::getFieldParse(), offsetof( AttributeModifierAuraUpdateModuleData, m_upgradeMuxData ) );
}

AttributeModifierAuraUpdate::AttributeModifierAuraUpdate( Thing *thing, const ModuleData* moduleData ) : UpdateModule( thing, moduleData )
{
	const AttributeModifierAuraUpdateModuleData *d = getAttributeModifierAuraUpdateModuleData();
	if( d->m_startsActive )
	{
		giveSelfUpgrade();
		// random phase, so that the auras of a whole army do not all scan on one frame
		setWakeFrame( getObject(), UPDATE_SLEEP( GameLogicRandomValue( 1, max( 1u, d->m_refreshDelay ) ) ) );
	}
	else
	{
		setWakeFrame( getObject(), UPDATE_SLEEP_FOREVER );
	}
}

AttributeModifierAuraUpdate::~AttributeModifierAuraUpdate()
{
}

/** Every RefreshDelay: the ModifierList goes to each object in Range that the ObjectFilter accepts,
	for the list's Duration (at least until the next scan), so it stays while the object stays near.
	A filter that names no relationship means allies (enemies with TargetEnemy = Yes). */
UpdateSleepTime AttributeModifierAuraUpdate::update()
{
	const AttributeModifierAuraUpdateModuleData *d = getAttributeModifierAuraUpdateModuleData();
	Object *self = getObject();
	if( !isAlreadyUpgraded() )
		return UPDATE_SLEEP_FOREVER;
	const UnsignedInt delay = max( 1u, d->m_refreshDelay );
	if( self->isEffectivelyDead() && !d->m_runWhileDead )
		return UPDATE_SLEEP_FOREVER;

	const BfmeModifierList *list = TheBfmeAttributeStore ? TheBfmeAttributeStore->findModifierList( d->m_bonusName ) : nullptr;
	if( list == nullptr || d->m_range <= 0.0f )
		return UPDATE_SLEEP_FOREVER;
	const UnsignedInt duration = max( list->m_durationFrames, delay + 1 );

	PartitionFilterAlive filterAlive;
	PartitionFilterSameMapStatus filterMapStatus( self );
	PartitionFilter *filters[] = { &filterAlive, &filterMapStatus, nullptr };
	ObjectIterator *iter = ThePartitionManager->iterateObjectsInRange( self->getPosition(), d->m_range, FROM_CENTER_2D, filters );
	MemoryPoolObjectHolder hold( iter );
	for( Object *obj = iter->first(); obj != nullptr; obj = iter->next() )
	{
		if( obj == self && !d->m_allowSelf )
			continue;

		BfmeObjectFilter::Subject s;
		s.kinds = obj->getTemplate()->getKindOfMask();
		s.templateName = obj->getTemplate()->getName();
		s.relationshipToSource = obj->getRelationship( self );
		s.hasSource = TRUE;
		if( obj == self )
			s.relationshipToSource = ALLIES;
		if( d->m_objectFilter.m_relationships == 0 )
		{
			const Relationship wanted = d->m_targetEnemy ? ENEMIES : ALLIES;
			if( s.relationshipToSource != wanted )
				continue;
		}
		if( !d->m_objectFilter.matches( s ) )
			continue;

		obj->bfmeApplyModifierList( list, duration );
	}
	return UPDATE_SLEEP( delay );
}

void AttributeModifierAuraUpdate::crc( Xfer *xfer )
{
	UpdateModule::crc( xfer );
	UpgradeMux::upgradeMuxCRC( xfer );
}

/** Xfer method
	* Version Info:
	* 1: Initial version */
void AttributeModifierAuraUpdate::xfer( Xfer *xfer )
{
	XferVersion currentVersion = 1;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );
	UpdateModule::xfer( xfer );
	UpgradeMux::upgradeMuxXfer( xfer );
}

void AttributeModifierAuraUpdate::loadPostProcess()
{
	UpdateModule::loadPostProcess();
	UpgradeMux::upgradeMuxLoadPostProcess();
}

//=================================================================================================
// AttributeModifierUpgrade
//=================================================================================================
void AttributeModifierUpgradeModuleData::buildFieldParse( MultiIniFieldParse& p )
{
	UpgradeModuleData::buildFieldParse( p );
	static const FieldParse dataFieldParse[] =
	{
		{ "AttributeModifier",	INI::parseAsciiString,	nullptr,	offsetof( AttributeModifierUpgradeModuleData, m_attributeModifier ) },
		{ nullptr, nullptr, nullptr, 0 }
	};
	p.add( dataFieldParse );
}

AttributeModifierUpgrade::AttributeModifierUpgrade( Thing *thing, const ModuleData* moduleData ) : UpgradeModule( thing, moduleData )
{
}

AttributeModifierUpgrade::~AttributeModifierUpgrade()
{
}

void AttributeModifierUpgrade::upgradeImplementation()
{
	const BfmeModifierList *list = TheBfmeAttributeStore
		? TheBfmeAttributeStore->findModifierList( getAttributeModifierUpgradeModuleData()->m_attributeModifier ) : nullptr;
	getObject()->bfmeApplyModifierList( list, 0 );
}

void AttributeModifierUpgrade::crc( Xfer *xfer )
{
	UpgradeModule::crc( xfer );
}

/** Xfer method
	* Version Info:
	* 1: Initial version */
void AttributeModifierUpgrade::xfer( Xfer *xfer )
{
	XferVersion currentVersion = 1;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );
	UpgradeModule::xfer( xfer );
}

void AttributeModifierUpgrade::loadPostProcess()
{
	UpgradeModule::loadPostProcess();
}

//=================================================================================================
// ExperienceLevelCreate
//=================================================================================================
void ExperienceLevelCreateModuleData::buildFieldParse( MultiIniFieldParse& p )
{
	CreateModuleData::buildFieldParse( p );
	static const FieldParse dataFieldParse[] =
	{
		{ "LevelToGrant",	INI::parseInt,	nullptr,	offsetof( ExperienceLevelCreateModuleData, m_levelToGrant ) },
		{ "MPOnly",				INI::parseBool,	nullptr,	offsetof( ExperienceLevelCreateModuleData, m_mpOnly ) },
		{ nullptr, nullptr, nullptr, 0 }
	};
	p.add( dataFieldParse );
}

ExperienceLevelCreate::ExperienceLevelCreate( Thing *thing, const ModuleData* moduleData ) : CreateModule( thing, moduleData )
{
}

ExperienceLevelCreate::~ExperienceLevelCreate()
{
}

/** Puts the object at LevelToGrant (never lowers it). Untrainable summons are created at their
	level too: like VeterancyGainCreate's explicit set, this does not depend on IsTrainable. */
void ExperienceLevelCreate::onCreate()
{
	const ExperienceLevelCreateModuleData *d = getExperienceLevelCreateModuleData();
	if( d->m_mpOnly && !( TheGameLogic->isInMultiplayerGame() || TheGameLogic->isInSkirmishGame() ) )
		return;
	if( ExperienceTracker *xp = getObject()->getExperienceTracker() )
		xp->setBfmeLevel( d->m_levelToGrant, FALSE );
}

void ExperienceLevelCreate::crc( Xfer *xfer )
{
	CreateModule::crc( xfer );
}

/** Xfer method
	* Version Info:
	* 1: Initial version */
void ExperienceLevelCreate::xfer( Xfer *xfer )
{
	XferVersion currentVersion = 1;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );
	CreateModule::xfer( xfer );
}

void ExperienceLevelCreate::loadPostProcess()
{
	CreateModule::loadPostProcess();
}

//=================================================================================================
// LevelUpUpgrade
//=================================================================================================
void LevelUpUpgradeModuleData::buildFieldParse( MultiIniFieldParse& p )
{
	UpgradeModuleData::buildFieldParse( p );
	static const FieldParse dataFieldParse[] =
	{
		{ "LevelsToGain",	INI::parseInt,	nullptr,	offsetof( LevelUpUpgradeModuleData, m_levelsToGain ) },
		{ "LevelCap",			INI::parseInt,	nullptr,	offsetof( LevelUpUpgradeModuleData, m_levelCap ) },
		{ nullptr, nullptr, nullptr, 0 }
	};
	p.add( dataFieldParse );
}

LevelUpUpgrade::LevelUpUpgrade( Thing *thing, const ModuleData* moduleData ) : UpgradeModule( thing, moduleData )
{
}

LevelUpUpgrade::~LevelUpUpgrade()
{
}

void LevelUpUpgrade::upgradeImplementation()
{
	const LevelUpUpgradeModuleData *d = getLevelUpUpgradeModuleData();
	if( ExperienceTracker *xp = getObject()->getExperienceTracker() )
		xp->gainBfmeLevels( d->m_levelsToGain, d->m_levelCap );
}

void LevelUpUpgrade::crc( Xfer *xfer )
{
	UpgradeModule::crc( xfer );
}

/** Xfer method
	* Version Info:
	* 1: Initial version */
void LevelUpUpgrade::xfer( Xfer *xfer )
{
	XferVersion currentVersion = 1;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );
	UpgradeModule::xfer( xfer );
}

void LevelUpUpgrade::loadPostProcess()
{
	UpgradeModule::loadPostProcess();
}
