// BFME1 Mobile — engine
// SPDX-License-Identifier: GPL-3.0-or-later
//
// BFME: E2-04. BFME1 experience levels and attribute modifiers (leadership, level-up bonuses,
// forge upgrades): data, stores and the pure rules.
//
// attributemodifier.ini:
//   ModifierList BoromirPassiveLeadership
//     Category = LEADERSHIP
//     Modifier = DAMAGE_MULT 160%
//     Modifier = ARMOR 50%
//     Duration = 3000
//   End
// experiencelevels.ini:
//   ExperienceLevel SamLevel2
//     TargetNames = RohanSam
//     RequiredExperience = 200
//     ExperienceAward = 100
//     Rank = 2
//     AttributeModifiers = HobbitLevelUpDamage1
//     Upgrades = Upgrade_NewAbility1
//   End
// Field names and modifier semantics follow OpenSAGE (Logic/ModifierList.cs, whose comments are
// EA's own from the BFME2 attributemodifier.ini header, and Logic/ExperienceLevel.cs).
//
// The engine glue (Object, ExperienceTracker, ActiveBody, Weapon.cpp and the BFME modules
// AttributeModifierAuraUpdate, AttributeModifierUpgrade, ExperienceLevelCreate, LevelUpUpgrade)
// is behind "// BFME: E2-04" hooks. What is here runs without the game (tests/combat).
#pragma once

#include "Common/AsciiString.h"
#include "Common/GameCommon.h"
#include "Common/NameKeyGenerator.h"
#include "Common/SubsystemInterface.h"
#include "GameLogic/Damage.h"

#include <map>
#include <vector>

class INI;
class Xfer;
struct FieldParse;

//-------------------------------------------------------------------------------------------------
/** "Modifier = TYPE AMOUNT". Stacking rule per type (EA comments, OpenSAGE ModifierList.cs). */
//-------------------------------------------------------------------------------------------------
enum BfmeModifierType
{
	BFME_MOD_ARMOR = 0,						///< additive; damage after Armor.ini *= 1 - min(sum, GameData AttributeModifierArmorMaxBonus)
	BFME_MOD_DAMAGE_ADD,					///< additive; added to the base damage of nuggets with AcceptDamageAdd, before the mults
	BFME_MOD_DAMAGE_MULT,					///< multiplicative
	BFME_MOD_SPELL_DAMAGE,				///< multiplicative; replaces DAMAGE_MULT for MAGIC damage
	BFME_MOD_RESIST_FEAR,					///< additive (parsed only)
	BFME_MOD_EXPERIENCE,					///< multiplicative; experience gained
	BFME_MOD_RANGE,								///< additive (parsed only)
	BFME_MOD_SPEED,								///< multiplicative (parsed only)
	BFME_MOD_CRUSH_DECELERATE,		///< multiplicative (parsed only)
	BFME_MOD_RESIST_KNOCKBACK,		///< additive (parsed only)
	BFME_MOD_RECHARGE_TIME,				///< multiplicative (parsed only)
	BFME_MOD_PRODUCTION,					///< multiplicative (parsed only)
	BFME_MOD_HEALTH,							///< additive; added to max and current hit points when the list is applied
	BFME_MOD_VISION,							///< additive (parsed only)
	BFME_MOD_AUTO_HEAL,						///< additive (parsed only)
	BFME_MOD_BOUNTY_PERCENTAGE,		///< (parsed only)
	BFME_MOD_MINIMUM_CRUSH_VELOCITY,	///< (parsed only)

	BFME_MOD_COUNT
};

enum BfmeModifierCategory
{
	BFME_MODCAT_LEADERSHIP = 0,
	BFME_MODCAT_SPELL,
	BFME_MODCAT_FORMATION,
	BFME_MODCAT_WEAPON,
	BFME_MODCAT_STRUCTURE,
	BFME_MODCAT_LEVEL,
	BFME_MODCAT_BUFF,

	BFME_MODCAT_COUNT
};

extern const char* const TheBfmeModifierTypeNames[];			///< BFME_MOD_COUNT names + nullptr
extern const char* const TheBfmeModifierCategoryNames[];	///< BFME_MODCAT_COUNT names + nullptr

struct BfmeModifier
{
	BfmeModifierType m_type;
	Real m_amount;
};

/** The value of a BFME amount token. "160%" (percent sign attached) is 1.6; a plain number is
	taken as written, so "1.2" and "1.2 %" (a #define'd value followed by a separate "%") are 1.2.
	This is the reading of tools/datacheck (report.md §9, GAMEPLAY-GAP §5 п.10): the data's own
	comments call PRODUCTION 1.2 % a x1.2 factor, which OpenSAGE's "always /100" would make 0.012. */
Real bfmeScanAmount( const char* token );
/// FieldParse for a single amount (GameData AttributeModifierArmorMaxBonus); store = Real*.
void bfmeParseAmount( INI* ini, void* instance, void* store, const void* userData );

//-------------------------------------------------------------------------------------------------
struct BfmeModifierList
{
	AsciiString m_name;
	BfmeModifierCategory m_category;
	std::vector<BfmeModifier> m_modifiers;
	UnsignedInt m_durationFrames;					///< Duration (msec in INI); 0 = until removed
	std::vector<AsciiString> m_upgrades;	///< "Upgrade = A B Delay:500": granted with the list (delay not modelled)

	BfmeModifierList();
	static const FieldParse* getFieldParse();

	Real sum( BfmeModifierType t ) const;			///< sum of this type's amounts (0 if none)
	Real product( BfmeModifierType t ) const;	///< product of this type's amounts (1 if none)
	Bool has( BfmeModifierType t ) const;
};

//-------------------------------------------------------------------------------------------------
struct BfmeExperienceLevel
{
	AsciiString m_name;
	std::vector<AsciiString> m_targetNames;		///< object templates this level belongs to
	Int m_requiredExperience;
	Int m_experienceAward;										///< experience a killer gets for this object at this level
	Int m_rank;																///< 1-based level number shown in game
	std::vector<AsciiString> m_attributeModifiers;	///< ModifierLists applied when the level is reached (they stay)
	std::vector<AsciiString> m_upgrades;					///< upgrades granted when the level is reached

	BfmeExperienceLevel();
	static const FieldParse* getFieldParse();
};

//-------------------------------------------------------------------------------------------------
/** All levels of one object template, lowest first. Levels are 1-based: level N is m_levels[N-1]. */
//-------------------------------------------------------------------------------------------------
struct BfmeLevelTable
{
	std::vector<const BfmeExperienceLevel*> m_levels;

	Int getMaxLevel() const { return (Int)m_levels.size(); }
	/// Highest level whose RequiredExperience <= xp; level 1 when xp is below every requirement.
	Int levelForExperience( Int xp ) const;
	/// Experience that puts an object exactly at `level` (clamped to 1..max).
	Int experienceForLevel( Int level ) const;
	const BfmeExperienceLevel* getLevel( Int level ) const;	///< nullptr outside 1..max
	/// VeterancyLevel for the Zero Hour code that still reads it (veterancy icons, VETERAN/ELITE/HERO
	/// weapon and armor sets): one-way, level 1 REGULAR, 2 VETERAN, 3 ELITE, 4+ HEROIC (GAMEPLAY-GAP §2.2 п.2).
	static VeterancyLevel veterancyForLevel( Int level );
};

//-------------------------------------------------------------------------------------------------
/** ModifierLists active on one object (member of Object). A list is never active twice: applying
	it again only extends its expiry. Different lists with the same effect do stack. */
//-------------------------------------------------------------------------------------------------
class BfmeAttributeSet
{
public:
	struct Active
	{
		const BfmeModifierList* m_list;
		UnsignedInt m_expireFrame;		///< 0 = until removed
	};

	/// Applies `list` until frame now+duration (duration 0 = until removed). Returns TRUE when the list
	/// was not active (the caller then does the one-shot part: HEALTH, upgrades), FALSE on a refresh.
	Bool add( const BfmeModifierList* list, UnsignedInt now, UnsignedInt durationFrames );
	Bool remove( const BfmeModifierList* list );
	/// Drops the lists that ended before `now`; appends them to `expired` when given.
	void expire( UnsignedInt now, std::vector<const BfmeModifierList*>* expired );
	Bool isActive( const BfmeModifierList* list, UnsignedInt now ) const;
	Bool isEmpty() const { return m_active.empty(); }
	const std::vector<Active>& getActive() const { return m_active; }
	void clear() { m_active.clear(); }

	Real sum( BfmeModifierType t, UnsignedInt now ) const;
	Real product( BfmeModifierType t, UnsignedInt now ) const;

	/// Multiplier on an attacker's damage of type `dt`: SPELL_DAMAGE for MAGIC (if any), else DAMAGE_MULT.
	Real damageMult( DamageType dt, UnsignedInt now ) const;
	Real damageAdd( UnsignedInt now ) const { return sum( BFME_MOD_DAMAGE_ADD, now ); }
	/// Multiplier on damage this object takes after Armor.ini, from the ARMOR sum and the GameData cap.
	Real armorFactor( DamageType dt, UnsignedInt now, Real maxBonus ) const;
	Real experienceMult( UnsignedInt now ) const { return product( BFME_MOD_EXPERIENCE, now ); }

	void crc( Xfer* xfer );
	void xfer( Xfer* xfer );	///< saves list names; loading resolves them in TheBfmeAttributeStore

	/// The ARMOR rule alone: 1 - min(armorSum, maxBonus), never below 0.
	static Real armorFactorFor( Real armorSum, Real maxBonus );
	/// FALSE for damage types that are not hit point damage from a weapon (healing, unresistable,
	/// surrender, status, ...): ARMOR does not reduce them.
	static Bool armorApplies( DamageType dt );

private:
	static Bool live( const Active& a, UnsignedInt now ) { return a.m_expireFrame == 0 || now < a.m_expireFrame; }
	std::vector<Active> m_active;
};

//-------------------------------------------------------------------------------------------------
/** TheBfmeAttributeStore: ModifierList (attributemodifier.ini) and ExperienceLevel
	(experiencelevels.ini) blocks. */
//-------------------------------------------------------------------------------------------------
class BfmeAttributeStore : public SubsystemInterface
{
public:
	BfmeAttributeStore();
	virtual ~BfmeAttributeStore() override;

	virtual void init() override { }
	virtual void reset() override { }
	virtual void update() override { }

	const BfmeModifierList* findModifierList( const AsciiString& name ) const;
	/// Levels of an object template, or nullptr if no ExperienceLevel names it.
	const BfmeLevelTable* findLevelTable( const AsciiString& templateName ) const;

	Int getModifierListCount() const { return (Int)m_lists.size(); }
	Int getExperienceLevelCount() const { return (Int)m_levels.size(); }

	static void parseModifierListDefinition( INI* ini );
	static void parseExperienceLevelDefinition( INI* ini );

private:
	void buildLevelTables() const;

	std::map<NameKeyType, BfmeModifierList> m_lists;
	std::map<NameKeyType, BfmeExperienceLevel> m_levels;
	mutable std::map<NameKeyType, BfmeLevelTable> m_tables;	///< by object template name
	mutable Bool m_tablesDirty;
};

extern BfmeAttributeStore* TheBfmeAttributeStore;
