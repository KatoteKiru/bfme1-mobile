// BFME1 Mobile — engine
// SPDX-License-Identifier: GPL-3.0-or-later
//
// BFME: E2-03. Weapon effect nuggets of BFME1 weapons: parse tables and pure rules.
// See GameLogic/BfmeWeaponNuggets.h. Engine glue is in Weapon.cpp ("// BFME: E2-03").
#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#define DEFINE_DEATH_NAMES
#include "Common/INI.h"
#include "GameLogic/BfmeWeaponNuggets.h"

#include <cstring>

namespace
{

// "0.5" or "50%" (BFME data uses both spellings for fractions such as HeroResist).
Real scanRealOrPercent( const char* token )
{
	const size_t n = strlen( token );
	if( n > 0 && token[n - 1] == '%' )
		return INI::scanPercentToReal( token );
	return INI::scanReal( token );
}

void parseRealOrPercent( INI* ini, void* /*instance*/, void* store, const void* /*userData*/ )
{
	*(Real*)store = scanRealOrPercent( ini->getNextToken() );
}

// DamageFXType: BFME1 names its DamageFX entries mostly after damage types (SWORD_SLASH, MAGIC,
// FLAME ...). A name that is not a damage type (SMALL_ROCK, FIRE1, UNDEFINED) means "no override".
void parseDamageFXType( INI* ini, void* /*instance*/, void* store, const void* /*userData*/ )
{
	const Int dt = DamageTypeFlags::getSingleBitFromName( ini->getNextToken() );
	*(DamageType*)store = dt >= 0 ? (DamageType)dt : DAMAGE_UNRESISTABLE;
}

// Fields of BfmeWeaponNuggets.h marked "parsed only" and the BFME2+ fields OpenSAGE lists: read
// and dropped, so neither mode reports them as unknown.
void parseIgnoredLine( INI* ini, void* /*instance*/, void* /*store*/, const void* /*userData*/ )
{
	while( ini->getNextTokenOrNull() != nullptr )
		;
}

const FieldParse s_damageNuggetFieldParse[] =
{
	{ "Damage",								INI::parseReal,									nullptr,	offsetof( BfmeDamageNugget, m_damage ) },
	{ "Radius",								INI::parseReal,									nullptr,	offsetof( BfmeDamageNugget, m_radius ) },
	{ "DelayTime",						INI::parseDurationUnsignedInt,	nullptr,	offsetof( BfmeDamageNugget, m_delayFrames ) },
	{ "DamageType",						DamageTypeFlags::parseSingleBitFromINI,	nullptr,	offsetof( BfmeDamageNugget, m_damageType ) },
	{ "DamageFXType",					parseDamageFXType,							nullptr,	offsetof( BfmeDamageNugget, m_damageFXType ) },
	{ "DeathType",						INI::parseIndexList,						TheDeathNames,	offsetof( BfmeDamageNugget, m_deathType ) },
	{ "DamageArc",						INI::parseAngleReal,						nullptr,	offsetof( BfmeDamageNugget, m_damageArc ) },
	{ "DamageMaxHeight",			INI::parseReal,									nullptr,	offsetof( BfmeDamageNugget, m_damageMaxHeight ) },
	{ "DamageSpeed",					INI::parseReal,									nullptr,	offsetof( BfmeDamageNugget, m_damageSpeed ) },
	{ "AcceptDamageAdd",			INI::parseBool,									nullptr,	offsetof( BfmeDamageNugget, m_acceptDamageAdd ) },
	{ "DamageScalar",					BfmeDamageScalar::parseFromINI,	nullptr,	offsetof( BfmeDamageNugget, m_damageScalar ) },
	{ "SpecialObjectFilter",	BfmeObjectFilter::parseFromINI,	nullptr,	offsetof( BfmeDamageNugget, m_filter ) },
	{ "LostLeadershipUselessAgainst",	parseIgnoredLine,				nullptr,	0 },
	{ "ForbiddenUpgradeNames",	parseIgnoredLine,							nullptr,	0 },
	{ "RequiredUpgradeNames",		parseIgnoredLine,							nullptr,	0 },
	{ nullptr, nullptr, nullptr, 0 }
};

const FieldParse s_metaImpactNuggetFieldParse[] =
{
	{ "HeroResist",						parseRealOrPercent,							nullptr,	offsetof( BfmeMetaImpactNugget, m_heroResist ) },
	{ "ShockWaveAmount",			INI::parseReal,									nullptr,	offsetof( BfmeMetaImpactNugget, m_shockWaveAmount ) },
	{ "ShockWaveRadius",			INI::parseReal,									nullptr,	offsetof( BfmeMetaImpactNugget, m_shockWaveRadius ) },
	{ "ShockWaveTaperOff",		parseRealOrPercent,							nullptr,	offsetof( BfmeMetaImpactNugget, m_shockWaveTaperOff ) },
	{ "ShockWaveArc",					INI::parseAngleReal,						nullptr,	offsetof( BfmeMetaImpactNugget, m_shockWaveArc ) },
	{ "ShockWaveZMult",				INI::parseReal,									nullptr,	offsetof( BfmeMetaImpactNugget, m_shockWaveZMult ) },
	{ "ShockWaveSpeed",				INI::parseReal,									nullptr,	offsetof( BfmeMetaImpactNugget, m_shockWaveSpeed ) },
	{ "InvertShockWave",			INI::parseBool,									nullptr,	offsetof( BfmeMetaImpactNugget, m_invertShockWave ) },
	{ "FlipDirection",				INI::parseBool,									nullptr,	offsetof( BfmeMetaImpactNugget, m_flipDirection ) },
	{ "DelayTime",						INI::parseDurationUnsignedInt,	nullptr,	offsetof( BfmeMetaImpactNugget, m_delayFrames ) },
	{ "SpecialObjectFilter",	BfmeObjectFilter::parseFromINI,	nullptr,	offsetof( BfmeMetaImpactNugget, m_filter ) },
	{ "OnlyWhenJustDied",			parseIgnoredLine,								nullptr,	0 },
	{ "KillObjectFilter",			parseIgnoredLine,								nullptr,	0 },
	{ "CyclonicFactor",				parseIgnoredLine,								nullptr,	0 },
	{ "ForbiddenUpgradeNames",	parseIgnoredLine,							nullptr,	0 },
	{ "RequiredUpgradeNames",		parseIgnoredLine,							nullptr,	0 },
	{ nullptr, nullptr, nullptr, 0 }
};

const FieldParse s_projectileNuggetFieldParse[] =
{
	{ "ProjectileTemplateName",	INI::parseAsciiString,				nullptr,	offsetof( BfmeProjectileNugget, m_projectileName ) },
	{ "WarheadTemplateName",		INI::parseAsciiString,				nullptr,	offsetof( BfmeProjectileNugget, m_warheadName ) },
	{ "SpecialObjectFilter",		BfmeObjectFilter::parseFromINI,	nullptr,	offsetof( BfmeProjectileNugget, m_filter ) },
	{ "ForbiddenUpgradeNames",	parseIgnoredLine,							nullptr,	0 },
	{ "RequiredUpgradeNames",		parseIgnoredLine,							nullptr,	0 },
	{ nullptr, nullptr, nullptr, 0 }
};

Bool arcAllowsCos( Real fullArc, Real cosToVictim )
{
	if( fullArc <= 0.0f || fullArc >= 2.0f * PI )
		return TRUE;
	return cosToVictim >= Cos( fullArc * 0.5f );
}

} // namespace

//-------------------------------------------------------------------------------------------------
// BfmeObjectFilter
//-------------------------------------------------------------------------------------------------
BfmeObjectFilter::BfmeObjectFilter() :
	m_set( FALSE ),
	m_mode( MODE_ANY ),
	m_relationships( 0 )
{
}

//-------------------------------------------------------------------------------------------------
Bool BfmeObjectFilter::parseTokens( const std::vector<AsciiString>& tokens )
{
	*this = BfmeObjectFilter();
	m_set = TRUE;
	for( size_t i = 0; i < tokens.size(); ++i )
	{
		const char* t = tokens[i].str();
		if( stricmp( t, "ALL" ) == 0 )					{ m_mode = MODE_ALL; continue; }
		if( stricmp( t, "ANY" ) == 0 )					{ m_mode = MODE_ANY; continue; }
		if( stricmp( t, "NONE" ) == 0 )					{ m_mode = MODE_NONE; continue; }
		if( stricmp( t, "ALLIES" ) == 0 )				{ m_relationships |= REL_ALLIES; continue; }
		if( stricmp( t, "ENEMIES" ) == 0 )			{ m_relationships |= REL_ENEMIES; continue; }
		if( stricmp( t, "NEUTRAL" ) == 0 || stricmp( t, "NEUTRALS" ) == 0 )	{ m_relationships |= REL_NEUTRAL; continue; }
		// Rules OpenSAGE lists that the Zero Hour damage loop already decides (self, similar,
		// airborne): accepted, not applied here.
		if( stricmp( t, "SELF" ) == 0 || stricmp( t, "SUICIDE" ) == 0 || stricmp( t, "NOT_SIMILAR" ) == 0 ||
				stricmp( t, "NOT_AIRBORNE" ) == 0 || stricmp( t, "SAME_HEIGHT_ONLY" ) == 0 || stricmp( t, "MINES" ) == 0 ||
				stricmp( t, "SAME_PLAYER" ) == 0 )
			continue;

		if( t[0] != '+' && t[0] != '-' )
			return FALSE;
		const Bool include = t[0] == '+';
		const char* name = t + 1;
		const Int kind = KindOfMaskType::getSingleBitFromName( name );
		if( kind >= 0 )
			( include ? m_includeKinds : m_excludeKinds ).set( kind );
		else
			( include ? m_includeThings : m_excludeThings ).push_back( AsciiString( name ) );
	}
	return TRUE;
}

//-------------------------------------------------------------------------------------------------
/*static*/ void BfmeObjectFilter::parseFromINI( INI* ini, void* /*instance*/, void* store, const void* /*userData*/ )
{
	std::vector<AsciiString> tokens;
	for( const char* t = ini->getNextTokenOrNull(); t != nullptr; t = ini->getNextTokenOrNull() )
		tokens.push_back( AsciiString( t ) );
	if( !( (BfmeObjectFilter*)store )->parseTokens( tokens ) )
		throw INI_INVALID_DATA;
}

//-------------------------------------------------------------------------------------------------
Bool BfmeObjectFilter::matches( const Subject& s ) const
{
	if( !m_set )
		return TRUE;

	if( m_relationships != 0 && s.hasSource )
	{
		Int rel = 0;
		switch( s.relationshipToSource )
		{
			case ALLIES:	rel = REL_ALLIES; break;
			case ENEMIES:	rel = REL_ENEMIES; break;
			default:			rel = REL_NEUTRAL; break;
		}
		if( ( m_relationships & rel ) == 0 )
			return FALSE;
	}

	if( s.kinds.testForAny( m_excludeKinds ) )
		return FALSE;
	for( size_t i = 0; i < m_excludeThings.size(); ++i )
		if( s.templateName.compareNoCase( m_excludeThings[i] ) == 0 )
			return FALSE;

	Bool listed = s.kinds.testForAny( m_includeKinds );
	for( size_t i = 0; !listed && i < m_includeThings.size(); ++i )
		listed = s.templateName.compareNoCase( m_includeThings[i] ) == 0;
	const Bool listEmpty = !m_includeKinds.any() && m_includeThings.empty();

	switch( m_mode )
	{
		case MODE_ALL:	return TRUE;
		case MODE_NONE:	return !listed;
		default:				return listEmpty || listed;
	}
}

//-------------------------------------------------------------------------------------------------
// BfmeDamageScalar
//-------------------------------------------------------------------------------------------------
/*static*/ void BfmeDamageScalar::parseFromINI( INI* ini, void* /*instance*/, void* store, const void* /*userData*/ )
{
	BfmeDamageScalar* self = (BfmeDamageScalar*)store;
	self->m_scalar = scanRealOrPercent( ini->getNextToken() );
	std::vector<AsciiString> tokens;
	for( const char* t = ini->getNextTokenOrNull(); t != nullptr; t = ini->getNextTokenOrNull() )
		tokens.push_back( AsciiString( t ) );
	self->m_filter = BfmeObjectFilter();
	if( !tokens.empty() && !self->m_filter.parseTokens( tokens ) )
		throw INI_INVALID_DATA;
}

//-------------------------------------------------------------------------------------------------
// BfmeDamageNugget
//-------------------------------------------------------------------------------------------------
BfmeDamageNugget::BfmeDamageNugget() :
	m_damage( 0.0f ),
	m_radius( 0.0f ),
	m_delayFrames( 0 ),
	m_damageType( DAMAGE_EXPLOSION ),
	m_damageFXType( DAMAGE_UNRESISTABLE ),
	m_deathType( DEATH_NORMAL ),
	m_damageArc( 0.0f ),
	m_damageMaxHeight( 0.0f ),
	m_damageSpeed( 0.0f ),
	m_acceptDamageAdd( FALSE )
{
}

/*static*/ const FieldParse* BfmeDamageNugget::getFieldParse() { return s_damageNuggetFieldParse; }

//-------------------------------------------------------------------------------------------------
Real BfmeDamageNugget::damageFor( const BfmeObjectFilter::Subject& victim, Bool isPrimaryVictim, Real distSqr,
	Real heightAboveImpact, Real bonusDamage, Real bonusRadius, Real damageAdd ) const
{
	if( !m_filter.matches( victim ) )
		return -1.0f;

	// Radius 0 hits the target only; with a radius the target is always hit, the rest by distance.
	if( !isPrimaryVictim )
	{
		const Real radius = m_radius * bonusRadius;
		if( radius <= 0.0f || distSqr > radius * radius )
			return -1.0f;
	}

	if( m_damageMaxHeight > 0.0f && heightAboveImpact > m_damageMaxHeight )
		return -1.0f;

	// BFME: E2-04. (base + DAMAGE_ADD) * mults, EA's order (OpenSAGE ModifierList.cs).
	Real damage = ( m_damage + ( m_acceptDamageAdd ? damageAdd : 0.0f ) ) * bonusDamage;
	if( m_damageScalar.m_filter.matches( victim ) )
		damage *= m_damageScalar.m_scalar;
	return damage;
}

//-------------------------------------------------------------------------------------------------
Bool BfmeDamageNugget::arcAllows( Real cosToVictim ) const
{
	return arcAllowsCos( m_damageArc, cosToVictim );
}

//-------------------------------------------------------------------------------------------------
// BfmeMetaImpactNugget
//-------------------------------------------------------------------------------------------------
BfmeMetaImpactNugget::BfmeMetaImpactNugget() :
	m_heroResist( 0.0f ),
	m_shockWaveAmount( 0.0f ),
	m_shockWaveRadius( 0.0f ),
	m_shockWaveTaperOff( 0.0f ),
	m_shockWaveArc( 0.0f ),
	m_shockWaveZMult( 1.0f ),
	m_shockWaveSpeed( 0.0f ),
	m_invertShockWave( FALSE ),
	m_flipDirection( FALSE ),
	m_delayFrames( 0 )
{
}

/*static*/ const FieldParse* BfmeMetaImpactNugget::getFieldParse() { return s_metaImpactNuggetFieldParse; }

//-------------------------------------------------------------------------------------------------
Real BfmeMetaImpactNugget::pushFor( Real dist, Bool isHero ) const
{
	if( m_shockWaveAmount <= 0.0f || m_shockWaveRadius <= 0.0f || dist > m_shockWaveRadius )
		return 0.0f;
	// Same taper as Object::attemptDamage: full push at the centre, TaperOff of it at the edge.
	const Real fromCenter = dist / m_shockWaveRadius;
	Real push = m_shockWaveAmount * ( 1.0f - fromCenter * ( 1.0f - m_shockWaveTaperOff ) );
	if( isHero )
		push *= ( 1.0f - m_heroResist );
	return push > 0.0f ? push : 0.0f;
}

//-------------------------------------------------------------------------------------------------
/*static*/ const FieldParse* BfmeProjectileNugget::getFieldParse() { return s_projectileNuggetFieldParse; }

//-------------------------------------------------------------------------------------------------
// BfmeWeaponNuggets
//-------------------------------------------------------------------------------------------------
/*static*/ void BfmeWeaponNuggets::parseDamageNugget( INI* ini, void* /*instance*/, void* store, const void* /*userData*/ )
{
	BfmeDamageNugget n;
	ini->initFromINI( &n, BfmeDamageNugget::getFieldParse() );
	( (BfmeWeaponNuggets*)store )->m_damage.push_back( n );
}

/*static*/ void BfmeWeaponNuggets::parseMetaImpactNugget( INI* ini, void* /*instance*/, void* store, const void* /*userData*/ )
{
	BfmeMetaImpactNugget n;
	ini->initFromINI( &n, BfmeMetaImpactNugget::getFieldParse() );
	( (BfmeWeaponNuggets*)store )->m_metaImpact.push_back( n );
}

/*static*/ void BfmeWeaponNuggets::parseProjectileNugget( INI* ini, void* /*instance*/, void* store, const void* /*userData*/ )
{
	BfmeProjectileNugget n;
	ini->initFromINI( &n, BfmeProjectileNugget::getFieldParse() );
	( (BfmeWeaponNuggets*)store )->m_projectile.push_back( n );
}

//-------------------------------------------------------------------------------------------------
Bool BfmeWeaponNuggets::fold( Fold& out ) const
{
	if( m_damage.empty() && m_projectile.empty() )
		return FALSE;

	// Estimate = what an ordinary target takes: the sum of the unfiltered nuggets. A weapon whose
	// nuggets are all filtered (e.g. only "+STRUCTURE") estimates with its largest one.
	Real unfiltered = 0.0f, unfilteredRadius = 0.0f;
	const BfmeDamageNugget* biggest = nullptr;
	const BfmeDamageNugget* biggestUnfiltered = nullptr;
	for( size_t i = 0; i < m_damage.size(); ++i )
	{
		const BfmeDamageNugget& n = m_damage[i];
		if( biggest == nullptr || n.m_damage > biggest->m_damage )
			biggest = &n;
		if( !n.m_filter.m_set )
		{
			unfiltered += n.m_damage;
			if( n.m_radius > unfilteredRadius )
				unfilteredRadius = n.m_radius;
			if( biggestUnfiltered == nullptr || n.m_damage > biggestUnfiltered->m_damage )
				biggestUnfiltered = &n;
		}
	}

	out.primaryDamage = 0.0f;
	out.primaryDamageRadius = 0.0f;
	if( biggestUnfiltered != nullptr )
	{
		out.primaryDamage = unfiltered;
		out.primaryDamageRadius = unfilteredRadius;
		out.damageType = biggestUnfiltered->m_damageType;
		out.deathType = biggestUnfiltered->m_deathType;
	}
	else if( biggest != nullptr )
	{
		out.primaryDamage = biggest->m_damage;
		out.primaryDamageRadius = biggest->m_radius;
		out.damageType = biggest->m_damageType;
		out.deathType = biggest->m_deathType;
	}

	out.projectileName.clear();
	if( !m_projectile.empty() )
		out.projectileName = m_projectile[0].m_projectileName;
	return TRUE;
}
