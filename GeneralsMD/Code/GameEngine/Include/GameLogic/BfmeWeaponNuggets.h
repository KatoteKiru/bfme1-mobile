// BFME1 Mobile — engine
// SPDX-License-Identifier: GPL-3.0-or-later
//
// BFME: E2-03. Weapon effect nuggets of BFME1 weapons on the Zero Hour WeaponTemplate.
//
// A BFME1 weapon does not carry PrimaryDamage/ProjectileObject; it lists "nuggets", each a
// sub-block closed by End:
//   Weapon GondorSwordsmanSword
//     AttackRange = 20
//     DamageNugget
//       Damage = 40
//       Radius = 0.0
//       DamageType = SLASH
//       DeathType = NORMAL
//     End
//     MetaImpactNugget
//       ShockWaveAmount = 20
//       ShockWaveRadius = 15
//     End
//     ProjectileNugget
//       ProjectileTemplateName = GondorArrow
//       WarheadTemplateName = GondorArcherWarhead
//     End
//   End
// Field names follow the BFME1 data and OpenSAGE (Logic/Object/Weapon/WeaponEffects/*.cs).
//
// This header holds only the data and the pure rules (parse tables, the object filter, the
// fold into Zero Hour fields, per-victim damage). The engine glue (iterating the partition
// manager, attemptDamage, physics) lives in Weapon.cpp behind "// BFME: E2-03" hooks, so the
// rules can be unit tested on synthetic data without the game (tests/combat).
#pragma once

#include "Common/AsciiString.h"
#include "Common/GameCommon.h"
#include "Common/KindOf.h"
#include "GameLogic/Damage.h"

#include <vector>

class INI;
class WeaponTemplate;
struct FieldParse;

//-------------------------------------------------------------------------------------------------
/** SAGE ObjectFilter ("SpecialObjectFilter = ANY ENEMIES +INFANTRY -HERO +SomeThing").
	Keywords: ALL (everything not excluded), ANY (at least one +entry, or everything if none),
	NONE (none of the +entries), ALLIES / ENEMIES / NEUTRAL(S) (relationship to the attacker; none
	given = any relationship). +X / -X is a KindOf name, or else an object template name. */
//-------------------------------------------------------------------------------------------------
struct BfmeObjectFilter
{
	enum Mode { MODE_ANY = 0, MODE_ALL, MODE_NONE };
	enum
	{
		REL_ALLIES	= 1 << 0,
		REL_ENEMIES	= 1 << 1,
		REL_NEUTRAL	= 1 << 2,
	};

	Bool m_set;												///< a filter line was parsed (an unset filter matches everything)
	Mode m_mode;
	Int m_relationships;							///< REL_* mask; 0 = any relationship
	KindOfMaskType m_includeKinds;
	KindOfMaskType m_excludeKinds;
	std::vector<AsciiString> m_includeThings;	///< +Name that is not a KindOf
	std::vector<AsciiString> m_excludeThings;

	BfmeObjectFilter();

	/// What the filter needs to know about a candidate victim.
	struct Subject
	{
		KindOfMaskType kinds;
		AsciiString templateName;
		Relationship relationshipToSource;	///< victim->getRelationship(source)
		Bool hasSource;											///< FALSE: attacker is gone, relationships are not checked
	};
	Bool matches( const Subject& s ) const;

	/// Parses the rest of the current INI line (FieldParse signature, store = BfmeObjectFilter*).
	static void parseFromINI( INI* ini, void* instance, void* store, const void* userData );
	/// Same rules on an already tokenized line; returns FALSE on a token that is not a keyword or +/-.
	Bool parseTokens( const std::vector<AsciiString>& tokens );
};

//-------------------------------------------------------------------------------------------------
/** "DamageScalar = 150% ANY +STRUCTURE": damage multiplier for victims the filter matches. */
//-------------------------------------------------------------------------------------------------
struct BfmeDamageScalar
{
	Real m_scalar;
	BfmeObjectFilter m_filter;
	BfmeDamageScalar() : m_scalar( 1.0f ) {}
	static void parseFromINI( INI* ini, void* instance, void* store, const void* userData );
};

//-------------------------------------------------------------------------------------------------
struct BfmeDamageNugget
{
	Real m_damage;
	Real m_radius;
	UnsignedInt m_delayFrames;				///< DelayTime (msec in INI)
	DamageType m_damageType;
	DamageType m_damageFXType;				///< DamageFXType when it names a damage type, else DAMAGE_UNRESISTABLE (= no override)
	DeathType m_deathType;
	Real m_damageArc;									///< DamageArc, full width in radians around the attacker's facing; >= 2*PI = all around
	Real m_damageMaxHeight;						///< DamageMaxHeight; 0 = unlimited
	Real m_damageSpeed;								///< parsed only (spread speed of the damage ring)
	Bool m_acceptDamageAdd;						///< parsed only
	BfmeDamageScalar m_damageScalar;
	BfmeObjectFilter m_filter;				///< SpecialObjectFilter

	BfmeDamageNugget();
	static const FieldParse* getFieldParse();

	/// Damage this nugget deals to a victim at `distSqr` from the impact point, before armor.
	/// `bonusDamage`/`bonusRadius` are the WeaponBonus DAMAGE/RADIUS fields (times the attacker's
	/// DAMAGE_MULT, BFME: E2-04); `damageAdd` is the attacker's DAMAGE_ADD sum, used only with
	/// AcceptDamageAdd. Returns <0 when the victim is not affected at all (outside radius, filtered
	/// out, above DamageMaxHeight).
	Real damageFor( const BfmeObjectFilter::Subject& victim, Bool isPrimaryVictim, Real distSqr,
		Real heightAboveImpact, Real bonusDamage, Real bonusRadius, Real damageAdd = 0.0f ) const;
	/// FALSE when DamageArc excludes a victim at angle `cosToVictim` (cos of the angle between the
	/// attacker's facing and the attacker→victim direction).
	Bool arcAllows( Real cosToVictim ) const;
};

//-------------------------------------------------------------------------------------------------
struct BfmeMetaImpactNugget
{
	Real m_heroResist;								///< fraction of the push a KINDOF_HERO ignores
	Real m_shockWaveAmount;
	Real m_shockWaveRadius;
	Real m_shockWaveTaperOff;					///< push left at the edge of the radius
	Real m_shockWaveArc;							///< full width in radians, as DamageArc
	Real m_shockWaveZMult;						///< vertical push = lateral push * this
	Real m_shockWaveSpeed;						///< parsed only
	Bool m_invertShockWave;						///< pull towards the centre
	Bool m_flipDirection;							///< parsed only
	UnsignedInt m_delayFrames;
	BfmeObjectFilter m_filter;

	BfmeMetaImpactNugget();
	static const FieldParse* getFieldParse();

	/// Lateral push for a victim at `dist` from the centre, or 0 outside the radius.
	Real pushFor( Real dist, Bool isHero ) const;
};

//-------------------------------------------------------------------------------------------------
struct BfmeProjectileNugget
{
	AsciiString m_projectileName;			///< ProjectileTemplateName
	AsciiString m_warheadName;				///< WarheadTemplateName
	BfmeObjectFilter m_filter;
	static const FieldParse* getFieldParse();
};

//-------------------------------------------------------------------------------------------------
/** All nuggets of one weapon, member WeaponTemplate::m_bfme. */
//-------------------------------------------------------------------------------------------------
struct BfmeWeaponNuggets
{
	std::vector<BfmeDamageNugget> m_damage;
	std::vector<BfmeMetaImpactNugget> m_metaImpact;
	std::vector<BfmeProjectileNugget> m_projectile;
	const WeaponTemplate* m_warheadTmpl;	///< resolved WarheadTemplateName of the first ProjectileNugget

	BfmeWeaponNuggets() : m_warheadTmpl( nullptr ) {}

	Bool hasEffects() const { return !m_damage.empty() || !m_metaImpact.empty(); }
	Bool hasAny() const { return hasEffects() || !m_projectile.empty(); }

	/// FieldParse entries; store = BfmeWeaponNuggets* (offsetof(WeaponTemplate, m_bfme)).
	static void parseDamageNugget( INI* ini, void* instance, void* store, const void* userData );
	static void parseMetaImpactNugget( INI* ini, void* instance, void* store, const void* userData );
	static void parseProjectileNugget( INI* ini, void* instance, void* store, const void* userData );

	/// Zero Hour fields a BFME weapon gets from its nuggets, so the unchanged ZH code (AI target
	/// choice via estimateWeaponTemplateDamage, isDamageWeapon, FX radius, projectile launch)
	/// sees a real weapon instead of a 0 damage one.
	struct Fold
	{
		Real primaryDamage;
		Real primaryDamageRadius;
		DamageType damageType;
		DeathType deathType;
		AsciiString projectileName;
	};
	/// Returns FALSE (and leaves `out` alone) when there is nothing to fold.
	Bool fold( Fold& out ) const;
};
