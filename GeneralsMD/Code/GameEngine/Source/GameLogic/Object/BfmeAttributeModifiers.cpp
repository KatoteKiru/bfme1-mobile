// BFME1 Mobile — engine
// SPDX-License-Identifier: GPL-3.0-or-later
//
// BFME: E2-04. BFME1 experience levels and attribute modifiers: parse tables, stores and the
// pure rules. See GameLogic/BfmeAttributeModifiers.h.
#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "Common/INI.h"
#include "Common/Xfer.h"
#include "GameLogic/BfmeAttributeModifiers.h"

#include <algorithm>
#include <cstring>

BfmeAttributeStore* TheBfmeAttributeStore = nullptr;

const char* const TheBfmeModifierTypeNames[] =
{
	"ARMOR",
	"DAMAGE_ADD",
	"DAMAGE_MULT",
	"SPELL_DAMAGE",
	"RESIST_FEAR",
	"EXPERIENCE",
	"RANGE",
	"SPEED",
	"CRUSH_DECELERATE",
	"RESIST_KNOCKBACK",
	"RECHARGE_TIME",
	"PRODUCTION",
	"HEALTH",
	"VISION",
	"AUTO_HEAL",
	"BOUNTY_PERCENTAGE",
	"MINIMUM_CRUSH_VELOCITY",
	nullptr
};
static_assert( ARRAY_SIZE( TheBfmeModifierTypeNames ) == BFME_MOD_COUNT + 1, "modifier names" );

const char* const TheBfmeModifierCategoryNames[] =
{
	"LEADERSHIP",
	"SPELL",
	"FORMATION",
	"WEAPON",
	"STRUCTURE",
	"LEVEL",
	"BUFF",
	nullptr
};
static_assert( ARRAY_SIZE( TheBfmeModifierCategoryNames ) == BFME_MODCAT_COUNT + 1, "category names" );

//-------------------------------------------------------------------------------------------------
Real bfmeScanAmount( const char* token )
{
	const size_t n = strlen( token );
	if( n > 0 && token[n - 1] == '%' )
		return INI::scanPercentToReal( token );
	return INI::scanReal( token );
}

void bfmeParseAmount( INI* ini, void* /*instance*/, void* store, const void* /*userData*/ )
{
	*(Real*)store = bfmeScanAmount( ini->getNextToken() );
	while( ini->getNextTokenOrNull() != nullptr )
		;
}

namespace
{

Int findName( const char* const* names, const char* token )
{
	for( Int i = 0; names[i] != nullptr; ++i )
		if( stricmp( names[i], token ) == 0 )
			return i;
	return -1;
}

// "Modifier = DAMAGE_MULT 160%". A type of a later game (HEALTH_MULT, RATE_OF_FIRE, ...) is dropped
// with its line: BFME1 data does not use them, and an exception would drop the whole list.
void parseModifier( INI* ini, void* /*instance*/, void* store, const void* /*userData*/ )
{
	std::vector<BfmeModifier>* mods = (std::vector<BfmeModifier>*)store;
	const Int type = findName( TheBfmeModifierTypeNames, ini->getNextToken() );
	const char* amount = ini->getNextToken();
	while( ini->getNextTokenOrNull() != nullptr )
		;	// a separate "%" after a #define'd value
	if( type < 0 )
		return;
	BfmeModifier m;
	m.m_type = (BfmeModifierType)type;
	m.m_amount = bfmeScanAmount( amount );
	mods->push_back( m );
}

void parseCategory( INI* ini, void* /*instance*/, void* store, const void* /*userData*/ )
{
	const Int c = findName( TheBfmeModifierCategoryNames, ini->getNextToken() );
	if( c >= 0 )
		*(BfmeModifierCategory*)store = (BfmeModifierCategory)c;
}

// "Upgrade = Upgrade_A Upgrade_B Delay:500"
void parseModifierUpgrade( INI* ini, void* /*instance*/, void* store, const void* /*userData*/ )
{
	std::vector<AsciiString>* names = (std::vector<AsciiString>*)store;
	for( const char* t = ini->getNextTokenOrNull(); t != nullptr; t = ini->getNextTokenOrNull() )
		if( strchr( t, ':' ) == nullptr )
			names->push_back( AsciiString( t ) );
}

// Appends every token of the line (TargetNames, AttributeModifiers and Upgrades may repeat).
void parseNameList( INI* ini, void* /*instance*/, void* store, const void* /*userData*/ )
{
	std::vector<AsciiString>* names = (std::vector<AsciiString>*)store;
	for( const char* t = ini->getNextTokenOrNull(); t != nullptr; t = ini->getNextTokenOrNull() )
		names->push_back( AsciiString( t ) );
}

// Presentation fields (FX, tints, model conditions) and fields of later games: read and dropped.
void parseIgnoredLine( INI* ini, void* /*instance*/, void* /*store*/, const void* /*userData*/ )
{
	while( ini->getNextTokenOrNull() != nullptr )
		;
}

const FieldParse s_modifierListFieldParse[] =
{
	{ "Category",							parseCategory,									nullptr,	offsetof( BfmeModifierList, m_category ) },
	{ "Modifier",							parseModifier,									nullptr,	offsetof( BfmeModifierList, m_modifiers ) },
	{ "Duration",							INI::parseDurationUnsignedInt,	nullptr,	offsetof( BfmeModifierList, m_durationFrames ) },
	{ "Upgrade",							parseModifierUpgrade,						nullptr,	offsetof( BfmeModifierList, m_upgrades ) },
	{ "FX",										parseIgnoredLine,								nullptr,	0 },
	{ "FX2",									parseIgnoredLine,								nullptr,	0 },
	{ "FX3",									parseIgnoredLine,								nullptr,	0 },
	{ "EndFX",								parseIgnoredLine,								nullptr,	0 },
	{ "EndFX2",								parseIgnoredLine,								nullptr,	0 },
	{ "EndFX3",								parseIgnoredLine,								nullptr,	0 },
	{ "MultiLevelFX",					parseIgnoredLine,								nullptr,	0 },
	{ "ModelCondition",				parseIgnoredLine,								nullptr,	0 },
	{ "ClearModelCondition",	parseIgnoredLine,								nullptr,	0 },
	{ "ReplaceInCategoryIfLongest",	parseIgnoredLine,					nullptr,	0 },
	{ "IgnoreIfAnticategoryActive",	parseIgnoredLine,					nullptr,	0 },
	{ nullptr, nullptr, nullptr, 0 }
};

const FieldParse s_experienceLevelFieldParse[] =
{
	{ "TargetNames",					parseNameList,									nullptr,	offsetof( BfmeExperienceLevel, m_targetNames ) },
	{ "RequiredExperience",		INI::parseInt,									nullptr,	offsetof( BfmeExperienceLevel, m_requiredExperience ) },
	{ "ExperienceAward",			INI::parseInt,									nullptr,	offsetof( BfmeExperienceLevel, m_experienceAward ) },
	{ "Rank",									INI::parseInt,									nullptr,	offsetof( BfmeExperienceLevel, m_rank ) },
	{ "AttributeModifiers",		parseNameList,									nullptr,	offsetof( BfmeExperienceLevel, m_attributeModifiers ) },
	{ "Upgrades",							parseNameList,									nullptr,	offsetof( BfmeExperienceLevel, m_upgrades ) },
	{ "ExperienceAwardOwnGuysDie",	parseIgnoredLine,					nullptr,	0 },
	{ "ShowLevelUpTint",			parseIgnoredLine,								nullptr,	0 },
	{ "LevelUpFx",						parseIgnoredLine,								nullptr,	0 },
	{ "LevelUpOCL",						parseIgnoredLine,								nullptr,	0 },
	{ "LevelUpTintColor",			parseIgnoredLine,								nullptr,	0 },
	{ "LevelUpTintPreColorTime",			parseIgnoredLine,				nullptr,	0 },
	{ "LevelUpTintPostColorTime",			parseIgnoredLine,				nullptr,	0 },
	{ "LevelUpTintSustainColorTime",	parseIgnoredLine,				nullptr,	0 },
	{ "InformUpdateModule",		parseIgnoredLine,								nullptr,	0 },
	{ "EmotionType",					parseIgnoredLine,								nullptr,	0 },
	{ nullptr, nullptr, nullptr, 0 }
};

} // namespace

//-------------------------------------------------------------------------------------------------
// BfmeModifierList
//-------------------------------------------------------------------------------------------------
BfmeModifierList::BfmeModifierList() :
	m_category( BFME_MODCAT_BUFF ),
	m_durationFrames( 0 )
{
}

const FieldParse* BfmeModifierList::getFieldParse() { return s_modifierListFieldParse; }

Real BfmeModifierList::sum( BfmeModifierType t ) const
{
	Real r = 0.0f;
	for( size_t i = 0; i < m_modifiers.size(); ++i )
		if( m_modifiers[i].m_type == t )
			r += m_modifiers[i].m_amount;
	return r;
}

Real BfmeModifierList::product( BfmeModifierType t ) const
{
	Real r = 1.0f;
	for( size_t i = 0; i < m_modifiers.size(); ++i )
		if( m_modifiers[i].m_type == t )
			r *= m_modifiers[i].m_amount;
	return r;
}

Bool BfmeModifierList::has( BfmeModifierType t ) const
{
	for( size_t i = 0; i < m_modifiers.size(); ++i )
		if( m_modifiers[i].m_type == t )
			return TRUE;
	return FALSE;
}

//-------------------------------------------------------------------------------------------------
// BfmeExperienceLevel / BfmeLevelTable
//-------------------------------------------------------------------------------------------------
BfmeExperienceLevel::BfmeExperienceLevel() :
	m_requiredExperience( 0 ),
	m_experienceAward( 0 ),
	m_rank( 0 )
{
}

const FieldParse* BfmeExperienceLevel::getFieldParse() { return s_experienceLevelFieldParse; }

Int BfmeLevelTable::levelForExperience( Int xp ) const
{
	Int level = 1;
	for( Int i = 0; i < (Int)m_levels.size(); ++i )
		if( xp >= m_levels[i]->m_requiredExperience )
			level = i + 1;
	return m_levels.empty() ? 0 : level;
}

Int BfmeLevelTable::experienceForLevel( Int level ) const
{
	if( m_levels.empty() )
		return 0;
	level = std::max( 1, std::min( level, getMaxLevel() ) );
	return m_levels[level - 1]->m_requiredExperience;
}

const BfmeExperienceLevel* BfmeLevelTable::getLevel( Int level ) const
{
	if( level < 1 || level > getMaxLevel() )
		return nullptr;
	return m_levels[level - 1];
}

VeterancyLevel BfmeLevelTable::veterancyForLevel( Int level )
{
	if( level >= 4 )
		return LEVEL_HEROIC;
	if( level == 3 )
		return LEVEL_ELITE;
	if( level == 2 )
		return LEVEL_VETERAN;
	return LEVEL_REGULAR;
}

//-------------------------------------------------------------------------------------------------
// BfmeAttributeSet
//-------------------------------------------------------------------------------------------------
Bool BfmeAttributeSet::add( const BfmeModifierList* list, UnsignedInt now, UnsignedInt durationFrames )
{
	if( list == nullptr )
		return FALSE;
	const UnsignedInt expire = durationFrames > 0 ? now + durationFrames : 0;
	for( size_t i = 0; i < m_active.size(); ++i )
	{
		Active& a = m_active[i];
		if( a.m_list != list )
			continue;
		const Bool wasLive = live( a, now );
		if( a.m_expireFrame != 0 && ( expire == 0 || expire > a.m_expireFrame ) )
			a.m_expireFrame = expire;
		return !wasLive;
	}
	Active a;
	a.m_list = list;
	a.m_expireFrame = expire;
	m_active.push_back( a );
	return TRUE;
}

Bool BfmeAttributeSet::remove( const BfmeModifierList* list )
{
	for( size_t i = 0; i < m_active.size(); ++i )
	{
		if( m_active[i].m_list == list )
		{
			m_active.erase( m_active.begin() + i );
			return TRUE;
		}
	}
	return FALSE;
}

void BfmeAttributeSet::expire( UnsignedInt now, std::vector<const BfmeModifierList*>* expired )
{
	for( size_t i = 0; i < m_active.size(); )
	{
		if( live( m_active[i], now ) )
		{
			++i;
			continue;
		}
		if( expired )
			expired->push_back( m_active[i].m_list );
		m_active.erase( m_active.begin() + i );
	}
}

Bool BfmeAttributeSet::isActive( const BfmeModifierList* list, UnsignedInt now ) const
{
	for( size_t i = 0; i < m_active.size(); ++i )
		if( m_active[i].m_list == list )
			return live( m_active[i], now );
	return FALSE;
}

Real BfmeAttributeSet::sum( BfmeModifierType t, UnsignedInt now ) const
{
	Real r = 0.0f;
	for( size_t i = 0; i < m_active.size(); ++i )
		if( live( m_active[i], now ) )
			r += m_active[i].m_list->sum( t );
	return r;
}

Real BfmeAttributeSet::product( BfmeModifierType t, UnsignedInt now ) const
{
	Real r = 1.0f;
	for( size_t i = 0; i < m_active.size(); ++i )
		if( live( m_active[i], now ) )
			r *= m_active[i].m_list->product( t );
	return r;
}

Real BfmeAttributeSet::damageMult( DamageType dt, UnsignedInt now ) const
{
	if( dt == DAMAGE_MAGIC )
	{
		for( size_t i = 0; i < m_active.size(); ++i )
			if( live( m_active[i], now ) && m_active[i].m_list->has( BFME_MOD_SPELL_DAMAGE ) )
				return product( BFME_MOD_SPELL_DAMAGE, now );
	}
	return product( BFME_MOD_DAMAGE_MULT, now );
}

Real BfmeAttributeSet::armorFactor( DamageType dt, UnsignedInt now, Real maxBonus ) const
{
	if( m_active.empty() || !armorApplies( dt ) )
		return 1.0f;
	return armorFactorFor( sum( BFME_MOD_ARMOR, now ), maxBonus );
}

Real BfmeAttributeSet::armorFactorFor( Real armorSum, Real maxBonus )
{
	// A negative sum (a curse) makes the victim take more; only the protection is capped.
	const Real bonus = std::min( armorSum, maxBonus );
	return std::max( 0.0f, 1.0f - bonus );
}

Bool BfmeAttributeSet::armorApplies( DamageType dt )
{
	switch( dt )
	{
		case DAMAGE_HEALING:
		case DAMAGE_UNRESISTABLE:
		case DAMAGE_DEPLOY:
		case DAMAGE_SURRENDER:
		case DAMAGE_HACK:
		case DAMAGE_KILLPILOT:
		case DAMAGE_PENALTY:
		case DAMAGE_DISARM:
		case DAMAGE_HAZARD_CLEANUP:
		case DAMAGE_SUBDUAL_UNRESISTABLE:
		case DAMAGE_KILL_GARRISONED:
		case DAMAGE_STATUS:
			return FALSE;
		default:
			return TRUE;
	}
}

void BfmeAttributeSet::crc( Xfer* xfer )
{
	UnsignedShort count = (UnsignedShort)m_active.size();
	xfer->xferUnsignedShort( &count );
	for( size_t i = 0; i < m_active.size(); ++i )
		xfer->xferUnsignedInt( &m_active[i].m_expireFrame );
}

/** Xfer: count, then (list name, expire frame) per active list. A list missing on load (data
	changed since the save) is dropped. */
void BfmeAttributeSet::xfer( Xfer* xfer )
{
	UnsignedShort count = (UnsignedShort)m_active.size();
	xfer->xferUnsignedShort( &count );
	if( xfer->getXferMode() == XFER_LOAD )
	{
		m_active.clear();
		for( UnsignedShort i = 0; i < count; ++i )
		{
			AsciiString name;
			UnsignedInt expire = 0;
			xfer->xferAsciiString( &name );
			xfer->xferUnsignedInt( &expire );
			const BfmeModifierList* list = TheBfmeAttributeStore ? TheBfmeAttributeStore->findModifierList( name ) : nullptr;
			if( list )
			{
				Active a;
				a.m_list = list;
				a.m_expireFrame = expire;
				m_active.push_back( a );
			}
		}
		return;
	}
	for( size_t i = 0; i < m_active.size(); ++i )
	{
		AsciiString name = m_active[i].m_list->m_name;
		xfer->xferAsciiString( &name );
		xfer->xferUnsignedInt( &m_active[i].m_expireFrame );
	}
}

//-------------------------------------------------------------------------------------------------
// BfmeAttributeStore
//-------------------------------------------------------------------------------------------------
BfmeAttributeStore::BfmeAttributeStore() :
	m_tablesDirty( TRUE )
{
}

BfmeAttributeStore::~BfmeAttributeStore()
{
}

const BfmeModifierList* BfmeAttributeStore::findModifierList( const AsciiString& name ) const
{
	if( name.isEmpty() )
		return nullptr;
	std::map<NameKeyType, BfmeModifierList>::const_iterator it = m_lists.find( TheNameKeyGenerator->nameToKey( name ) );
	return it == m_lists.end() ? nullptr : &it->second;
}

const BfmeLevelTable* BfmeAttributeStore::findLevelTable( const AsciiString& templateName ) const
{
	if( m_tablesDirty )
		buildLevelTables();
	std::map<NameKeyType, BfmeLevelTable>::const_iterator it = m_tables.find( TheNameKeyGenerator->nameToKey( templateName ) );
	return it == m_tables.end() ? nullptr : &it->second;
}

namespace
{
// Level order: by RequiredExperience, then Rank (level 1 often requires 0 or 1 experience).
bool levelBefore( const BfmeExperienceLevel* a, const BfmeExperienceLevel* b )
{
	if( a->m_requiredExperience != b->m_requiredExperience )
		return a->m_requiredExperience < b->m_requiredExperience;
	if( a->m_rank != b->m_rank )
		return a->m_rank < b->m_rank;
	return strcmp( a->m_name.str(), b->m_name.str() ) < 0;
}
}

void BfmeAttributeStore::buildLevelTables() const
{
	m_tables.clear();
	for( std::map<NameKeyType, BfmeExperienceLevel>::const_iterator it = m_levels.begin(); it != m_levels.end(); ++it )
	{
		const BfmeExperienceLevel& lvl = it->second;
		for( size_t i = 0; i < lvl.m_targetNames.size(); ++i )
			m_tables[TheNameKeyGenerator->nameToKey( lvl.m_targetNames[i] )].m_levels.push_back( &lvl );
	}
	for( std::map<NameKeyType, BfmeLevelTable>::iterator it = m_tables.begin(); it != m_tables.end(); ++it )
		std::sort( it->second.m_levels.begin(), it->second.m_levels.end(), levelBefore );
	m_tablesDirty = FALSE;
}

/*static*/ void BfmeAttributeStore::parseModifierListDefinition( INI* ini )
{
	const char* name = ini->getNextToken();
	BfmeModifierList skipped;	// a block read before the store exists is parsed and dropped
	BfmeModifierList& list = TheBfmeAttributeStore ? TheBfmeAttributeStore->m_lists[TheNameKeyGenerator->nameToKey( name )] : skipped;
	list = BfmeModifierList();
	list.m_name = name;
	ini->initFromINI( &list, BfmeModifierList::getFieldParse() );
}

/*static*/ void BfmeAttributeStore::parseExperienceLevelDefinition( INI* ini )
{
	const char* name = ini->getNextToken();
	BfmeExperienceLevel skipped;
	BfmeExperienceLevel& lvl = TheBfmeAttributeStore ? TheBfmeAttributeStore->m_levels[TheNameKeyGenerator->nameToKey( name )] : skipped;
	lvl = BfmeExperienceLevel();
	lvl.m_name = name;
	ini->initFromINI( &lvl, BfmeExperienceLevel::getFieldParse() );
	if( TheBfmeAttributeStore )
		TheBfmeAttributeStore->m_tablesDirty = TRUE;
}

//-------------------------------------------------------------------------------------------------
/*static*/ void INI::parseModifierListDefinition( INI* ini )
{
	BfmeAttributeStore::parseModifierListDefinition( ini );
}

/*static*/ void INI::parseExperienceLevelDefinition( INI* ini )
{
	BfmeAttributeStore::parseExperienceLevelDefinition( ini );
}
