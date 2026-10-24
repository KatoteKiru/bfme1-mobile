// BFME1 Mobile — tests (E2-03): BFME1 weapon nuggets, damage/death types and armor.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The real INI.cpp, Damage.cpp, KindOf.cpp, Armor.cpp, BfmeWeaponNuggets.cpp and (E2-04)
// BfmeAttributeModifiers.cpp load small synthetic INI files (own text in the shape of BFME1
// Weapon.ini / Armor.ini / attributemodifier.ini / experiencelevels.ini, no EA data).
// "Weapon" blocks are parsed with the nugget field parsers into a test struct: the full
// WeaponTemplate needs the whole game (FX, OCL, audio), its glue is covered by the in-game check.
#include "PreRTS.h"

#include "Common/FileSystem.h"
#include "Common/INI.h"
#include "Common/INILenient.h"
#include "Common/KindOf.h"
#include "Common/LocalFileSystem.h"
#include "Common/NameKeyGenerator.h"
#include "GameLogic/Armor.h"
#include "GameLogic/BfmeAttributeModifiers.h"
#include "GameLogic/BfmeWeaponNuggets.h"
#include "GameLogic/Damage.h"
#include "StdDevice/Common/StdLocalFileSystem.h"

#include "../vfs/TestHarness.h"

#include <cmath>
#include <cstdlib>
#include <fstream>
#include <map>
#include <string>
#include <unistd.h>

namespace {

struct TestWeapon {
	Real attackRange = 0.0f;
	BfmeWeaponNuggets nuggets;
};

const FieldParse s_testWeaponTable[] = {
	{ "AttackRange", INI::parseReal, nullptr, offsetof(TestWeapon, attackRange) },
	{ "DamageNugget", BfmeWeaponNuggets::parseDamageNugget, nullptr, offsetof(TestWeapon, nuggets) },
	{ "MetaImpactNugget", BfmeWeaponNuggets::parseMetaImpactNugget, nullptr, offsetof(TestWeapon, nuggets) },
	{ "ProjectileNugget", BfmeWeaponNuggets::parseProjectileNugget, nullptr, offsetof(TestWeapon, nuggets) },
	{ nullptr, nullptr, nullptr, 0 },
};

std::map<std::string, TestWeapon> s_weapons;

std::string writeIni(const char *name, const char *text)
{
	static std::string dir;
	if (dir.empty()) {
		char tmpl[] = "/tmp/bfme-combat-XXXXXX";
		REQUIRE(mkdtemp(tmpl) != nullptr);
		dir = tmpl;
	}
	const std::string path = dir + "/" + name;
	std::ofstream(path, std::ios::binary) << text;
	return path;
}

void ensureEngine()
{
	if (TheLocalFileSystem == nullptr) {
		TheLocalFileSystem = new StdLocalFileSystem;
		TheLocalFileSystem->init();
	}
	if (TheFileSystem == nullptr)
		TheFileSystem = new FileSystem;
	if (TheNameKeyGenerator == nullptr) {
		TheNameKeyGenerator = new NameKeyGenerator;
		TheNameKeyGenerator->init();
	}
	if (TheArmorStore == nullptr)
		TheArmorStore = new ArmorStore;
	if (TheBfmeAttributeStore == nullptr)
		TheBfmeAttributeStore = new BfmeAttributeStore;
}

// TRUE if INI::load threw.
bool load(const char *name, const char *text, bool lenient)
{
	ensureEngine();
	INILenient::reset();
	INILenient::setEnabled(lenient ? TRUE : FALSE);
	s_weapons.clear();
	bool threw = false;
	try {
		INI ini;
		ini.load(AsciiString(writeIni(name, text).c_str()), INI_LOAD_OVERWRITE, nullptr);
	} catch (...) {
		threw = true;
	}
	INILenient::setEnabled(FALSE);
	return threw;
}

bool near(Real a, Real b) { return std::fabs(a - b) < 1e-4f; }

BfmeObjectFilter::Subject subject(std::initializer_list<KindOfType> kinds, const char *name = "Thing",
	Relationship rel = ENEMIES, Bool hasSource = TRUE)
{
	BfmeObjectFilter::Subject s;
	for (KindOfType k : kinds)
		s.kinds.set(k);
	s.templateName = name;
	s.relationshipToSource = rel;
	s.hasSource = hasSource;
	return s;
}

BfmeObjectFilter filter(std::initializer_list<const char *> tokens)
{
	std::vector<AsciiString> v;
	for (const char *t : tokens)
		v.push_back(AsciiString(t));
	BfmeObjectFilter f;
	REQUIRE(f.parseTokens(v));
	return f;
}

// Own text in the shape of BFME1 Weapon.ini: a sword with a second, structure-only nugget and a
// knockback, a bow whose damage sits on its warhead, and the warhead.
const char *const kWeapons =
	"Weapon TestSword\n"
	"  AttackRange = 20\n"
	"  DamageNugget\n"
	"    Damage = 40\n"
	"    Radius = 10.0\n"
	"    DelayTime = 100\n"
	"    DamageType = SLASH\n"
	"    DamageFXType = SWORD_SLASH\n"
	"    DeathType = KNOCKBACK\n"
	"    DamageArc = 90\n"
	"    DamageScalar = 150% ANY +STRUCTURE\n"
	"  End\n"
	"  DamageNugget\n"
	"    Damage = 100\n"
	"    DamageType = STRUCTURAL\n"
	"    DamageFXType = SMALL_ROCK\n"
	"    SpecialObjectFilter = ANY +STRUCTURE\n"
	"  End\n"
	"  MetaImpactNugget\n"
	"    HeroResist = 0.5\n"
	"    ShockWaveAmount = 30\n"
	"    ShockWaveRadius = 20\n"
	"    ShockWaveTaperOff = 0.25\n"
	"    ShockWaveArc = 180\n"
	"    ShockWaveZMult = 2.0\n"
	"    InvertShockWave = Yes\n"
	"    SpecialObjectFilter = ALL ENEMIES -STRUCTURE\n"
	"  End\n"
	"End\n"
	"Weapon TestBow\n"
	"  AttackRange = 200\n"
	"  ProjectileNugget\n"
	"    ProjectileTemplateName = TestArrow\n"
	"    WarheadTemplateName = TestBowWarhead\n"
	"  End\n"
	"End\n"
	"Weapon TestBowWarhead\n"
	"  DamageNugget\n"
	"    Damage = 25\n"
	"    DamageType = PIERCE\n"
	"  End\n"
	"End\n";

// A BFME2 field inside a nugget (DamageTaperOff) and an unknown damage type.
const char *const kBfme2Fields =
	"Weapon TestAxe\n"
	"  DamageNugget\n"
	"    Damage = 30\n"
	"    DamageTaperOff = 3\n"
	"    DamageType = NOT_A_DAMAGE_TYPE\n"
	"    DeathType = SLAUGHTERED\n"
	"  End\n"
	"  AttackRange = 15\n"
	"End\n";

const char *const kArmor =
	"Armor TestInfantryArmor\n"
	"  Armor = DEFAULT 50%\n"
	"  Armor = SLASH 25%\n"
	"  Armor = SIEGE 200%\n"
	"  DamageScalar = 50%\n"
	"  FlankedPenalty = 25%\n"
	"End\n"
	"Armor TestZhArmor\n"
	"  Armor = DEFAULT 100%\n"
	"  Armor = SMALL_ARMS 40%\n"
	"End\n";

} // namespace

// The "Weapon" block of theTypeTable; the real one is WeaponStore::parseWeaponTemplateDefinition.
void INI::parseWeaponTemplateDefinition(INI *ini)
{
	const char *name = ini->getNextToken();
	TestWeapon &w = s_weapons[name];
	ini->initFromINI(&w, s_testWeaponTable);
}

void INI::parseTerrainDefinition(INI *) {}

TEST(zh_damage_type_indices_unchanged)
{
	// Saves, replays and Zero Hour INI address damage types by index: BFME ones are appended.
	REQUIRE(DAMAGE_STATUS == 37);
	REQUIRE(DAMAGE_SLASH == 38);
	REQUIRE(DamageTypeFlags::getSingleBitFromName("STATUS") == DAMAGE_STATUS);
	REQUIRE(DamageTypeFlags::getSingleBitFromName("SMALL_ARMS") == DAMAGE_SMALL_ARMS);
	REQUIRE(DEATH_POISONED_GAMMA == 20);
	REQUIRE(DEATH_NUM_TYPES <= 32);	// DeathTypeFlags is a 32-bit mask
}

TEST(bfme_damage_type_names_resolve)
{
	REQUIRE(DamageTypeFlags::getSingleBitFromName("SLASH") == DAMAGE_SLASH);
	REQUIRE(DamageTypeFlags::getSingleBitFromName("PIERCE") == DAMAGE_PIERCE);
	REQUIRE(DamageTypeFlags::getSingleBitFromName("HERO_RANGED") == DAMAGE_HERO_RANGED);
	REQUIRE(DamageTypeFlags::getSingleBitFromName("WITCH_KING_MORGUL_BLADE") == DAMAGE_WITCH_KING_MORGUL_BLADE);
	REQUIRE(DamageTypeFlags::getSingleBitFromName("CLUBBING") == DAMAGE_CLUBBING);
	REQUIRE(DAMAGE_CLUBBING == DAMAGE_NUM_TYPES - 1);
	REQUIRE(DamageTypeFlags::getSingleBitFromName("NOT_A_DAMAGE_TYPE") == -1);
}

TEST(parse_damage_nugget_fields)
{
	REQUIRE(!load("weapons.ini", kWeapons, true));
	const TestWeapon &w = s_weapons["TestSword"];
	REQUIRE(near(w.attackRange, 20.0f));
	REQUIRE(w.nuggets.m_damage.size() == 2);
	const BfmeDamageNugget &n = w.nuggets.m_damage[0];
	REQUIRE(near(n.m_damage, 40.0f));
	REQUIRE(near(n.m_radius, 10.0f));
	REQUIRE(n.m_delayFrames == 3);	// 100 ms at 30 logic frames/s
	REQUIRE(n.m_damageType == DAMAGE_SLASH);
	REQUIRE(n.m_damageFXType == DAMAGE_SWORD_SLASH);
	REQUIRE(n.m_deathType == DEATH_KNOCKBACK);
	REQUIRE(near(n.m_damageArc, PI * 0.5f));
	REQUIRE(near(n.m_damageScalar.m_scalar, 1.5f));
	REQUIRE(n.m_damageScalar.m_filter.m_set);
	REQUIRE(!n.m_filter.m_set);

	const BfmeDamageNugget &s = w.nuggets.m_damage[1];
	REQUIRE(s.m_damageType == DAMAGE_STRUCTURAL);
	REQUIRE(s.m_damageFXType == DAMAGE_UNRESISTABLE);	// SMALL_ROCK is no damage type: no FX override
	REQUIRE(s.m_filter.m_set);
	REQUIRE(INILenient::counters().fields + INILenient::counters().values + INILenient::counters().blocks == 0);
}

TEST(parse_meta_impact_nugget)
{
	REQUIRE(!load("weapons.ini", kWeapons, true));
	const TestWeapon &w = s_weapons["TestSword"];
	REQUIRE(w.nuggets.m_metaImpact.size() == 1);
	const BfmeMetaImpactNugget &m = w.nuggets.m_metaImpact[0];
	REQUIRE(near(m.m_heroResist, 0.5f));
	REQUIRE(near(m.m_shockWaveAmount, 30.0f));
	REQUIRE(near(m.m_shockWaveRadius, 20.0f));
	REQUIRE(near(m.m_shockWaveTaperOff, 0.25f));
	REQUIRE(near(m.m_shockWaveArc, PI));
	REQUIRE(near(m.m_shockWaveZMult, 2.0f));
	REQUIRE(m.m_invertShockWave);
	REQUIRE(m.m_filter.m_set);
}

TEST(parse_projectile_nugget_and_warhead)
{
	REQUIRE(!load("weapons.ini", kWeapons, true));
	const TestWeapon &bow = s_weapons["TestBow"];
	REQUIRE(bow.nuggets.m_projectile.size() == 1);
	REQUIRE(bow.nuggets.m_projectile[0].m_projectileName == "TestArrow");
	REQUIRE(bow.nuggets.m_projectile[0].m_warheadName == "TestBowWarhead");
	REQUIRE(!bow.nuggets.hasEffects());
	REQUIRE(bow.nuggets.hasAny());
	REQUIRE(s_weapons["TestBowWarhead"].nuggets.m_damage.size() == 1);
	REQUIRE(s_weapons["TestBowWarhead"].nuggets.m_damage[0].m_damageType == DAMAGE_PIERCE);
}

TEST(zh_mode_parses_nuggets_too)
{
	// Nuggets are ordinary fields: the strict Zero Hour parser reads them as well.
	REQUIRE(!load("weapons.ini", kWeapons, false));
	REQUIRE(s_weapons["TestSword"].nuggets.m_damage.size() == 2);
}

TEST(bfme_mode_skips_bfme2_nugget_fields)
{
	REQUIRE(!load("axe.ini", kBfme2Fields, true));
	const TestWeapon &w = s_weapons["TestAxe"];
	REQUIRE(w.nuggets.m_damage.size() == 1);
	REQUIRE(near(w.nuggets.m_damage[0].m_damage, 30.0f));
	REQUIRE(w.nuggets.m_damage[0].m_damageType == DAMAGE_EXPLOSION);	// bad value: default kept
	REQUIRE(w.nuggets.m_damage[0].m_deathType == DEATH_SLAUGHTERED);
	REQUIRE(near(w.attackRange, 15.0f));	// parsing went on after the nugget's End
	REQUIRE(INILenient::counters().fields == 1);
	REQUIRE(INILenient::counters().values == 1);
}

TEST(filter_any_with_kinds_and_relationship)
{
	const BfmeObjectFilter f = filter({ "ANY", "ENEMIES", "+INFANTRY", "+CAVALRY_THING", "-HERO" });
	REQUIRE(f.matches(subject({ KINDOF_INFANTRY })));
	REQUIRE(!f.matches(subject({ KINDOF_INFANTRY, KINDOF_HERO })));
	REQUIRE(!f.matches(subject({ KINDOF_INFANTRY }, "Thing", ALLIES)));
	REQUIRE(f.matches(subject({ KINDOF_INFANTRY }, "Thing", ALLIES, FALSE)));	// attacker gone
	REQUIRE(!f.matches(subject({ KINDOF_STRUCTURE })));
	REQUIRE(f.matches(subject({ KINDOF_STRUCTURE }, "cavalry_thing")));	// not a KindOf: template name
}

TEST(filter_all_and_none)
{
	const BfmeObjectFilter all = filter({ "ALL", "-STRUCTURE" });
	REQUIRE(all.matches(subject({ KINDOF_INFANTRY })));
	REQUIRE(all.matches(subject({})));
	REQUIRE(!all.matches(subject({ KINDOF_STRUCTURE })));

	const BfmeObjectFilter none = filter({ "NONE", "+STRUCTURE" });
	REQUIRE(none.matches(subject({ KINDOF_INFANTRY })));
	REQUIRE(!none.matches(subject({ KINDOF_STRUCTURE })));

	BfmeObjectFilter unset;
	REQUIRE(unset.matches(subject({ KINDOF_STRUCTURE })));

	BfmeObjectFilter bad;
	std::vector<AsciiString> tokens;
	tokens.push_back(AsciiString("INFANTRY"));	// no +/-
	REQUIRE(!bad.parseTokens(tokens));
}

TEST(damage_nugget_radius_filter_and_scalar)
{
	REQUIRE(!load("weapons.ini", kWeapons, true));
	const BfmeDamageNugget &n = s_weapons["TestSword"].nuggets.m_damage[0];
	const BfmeObjectFilter::Subject orc = subject({ KINDOF_INFANTRY });
	const BfmeObjectFilter::Subject tower = subject({ KINDOF_STRUCTURE });

	REQUIRE(near(n.damageFor(orc, TRUE, 0.0f, 0.0f, 1.0f, 1.0f), 40.0f));
	REQUIRE(near(n.damageFor(orc, TRUE, 900.0f, 0.0f, 1.0f, 1.0f), 40.0f));	// the target is always hit
	REQUIRE(near(n.damageFor(orc, FALSE, 81.0f, 0.0f, 1.0f, 1.0f), 40.0f));	// 9 < radius 10
	REQUIRE(n.damageFor(orc, FALSE, 121.0f, 0.0f, 1.0f, 1.0f) < 0.0f);				// 11 > radius 10
	REQUIRE(near(n.damageFor(orc, FALSE, 121.0f, 0.0f, 1.0f, 1.5f), 40.0f));	// RADIUS bonus 150%
	REQUIRE(near(n.damageFor(orc, TRUE, 0.0f, 0.0f, 2.0f, 1.0f), 80.0f));		// DAMAGE bonus 200%
	REQUIRE(near(n.damageFor(tower, TRUE, 0.0f, 0.0f, 1.0f, 1.0f), 60.0f));	// DamageScalar 150% +STRUCTURE

	const BfmeDamageNugget &s = s_weapons["TestSword"].nuggets.m_damage[1];
	REQUIRE(s.damageFor(orc, TRUE, 0.0f, 0.0f, 1.0f, 1.0f) < 0.0f);				// SpecialObjectFilter +STRUCTURE
	REQUIRE(near(s.damageFor(tower, TRUE, 0.0f, 0.0f, 1.0f, 1.0f), 100.0f));
	REQUIRE(s.damageFor(tower, FALSE, 0.0f, 0.0f, 1.0f, 1.0f) < 0.0f);		// Radius 0: target only
}

TEST(damage_nugget_arc_and_max_height)
{
	BfmeDamageNugget n;
	n.m_damage = 10.0f;
	n.m_radius = 50.0f;
	REQUIRE(n.arcAllows(-1.0f));	// no DamageArc: all around
	n.m_damageArc = PI * 0.5f;		// 90 degrees wide: +-45 around the facing
	REQUIRE(n.arcAllows(1.0f));
	REQUIRE(n.arcAllows(std::cos(PI * 0.2f)));
	REQUIRE(!n.arcAllows(std::cos(PI * 0.3f)));
	REQUIRE(!n.arcAllows(-1.0f));

	n.m_damageMaxHeight = 5.0f;
	REQUIRE(near(n.damageFor(subject({}), FALSE, 1.0f, 4.0f, 1.0f, 1.0f), 10.0f));
	REQUIRE(n.damageFor(subject({}), FALSE, 1.0f, 6.0f, 1.0f, 1.0f) < 0.0f);
}

TEST(meta_impact_push_taper_and_hero_resist)
{
	REQUIRE(!load("weapons.ini", kWeapons, true));
	const BfmeMetaImpactNugget &m = s_weapons["TestSword"].nuggets.m_metaImpact[0];
	REQUIRE(near(m.pushFor(0.0f, FALSE), 30.0f));
	REQUIRE(near(m.pushFor(20.0f, FALSE), 7.5f));		// TaperOff 25% at the edge
	REQUIRE(near(m.pushFor(10.0f, FALSE), 18.75f));
	REQUIRE(near(m.pushFor(0.0f, TRUE), 15.0f));		// HeroResist 0.5
	REQUIRE(near(m.pushFor(20.5f, FALSE), 0.0f));		// outside the radius
	REQUIRE(m.m_filter.matches(subject({ KINDOF_INFANTRY })));
	REQUIRE(!m.m_filter.matches(subject({ KINDOF_STRUCTURE })));
	REQUIRE(!m.m_filter.matches(subject({ KINDOF_INFANTRY }, "Thing", ALLIES)));
}

TEST(fold_gives_zero_hour_fields)
{
	REQUIRE(!load("weapons.ini", kWeapons, true));
	BfmeWeaponNuggets::Fold f;
	f.damageType = DAMAGE_EXPLOSION;
	f.deathType = DEATH_NORMAL;
	REQUIRE(s_weapons["TestSword"].nuggets.fold(f));
	REQUIRE(near(f.primaryDamage, 40.0f));		// the +STRUCTURE nugget is not what an ordinary target takes
	REQUIRE(near(f.primaryDamageRadius, 10.0f));
	REQUIRE(f.damageType == DAMAGE_SLASH);
	REQUIRE(f.deathType == DEATH_KNOCKBACK);
	REQUIRE(f.projectileName.isEmpty());

	BfmeWeaponNuggets::Fold b;
	b.damageType = DAMAGE_EXPLOSION;
	b.deathType = DEATH_NORMAL;
	REQUIRE(s_weapons["TestBow"].nuggets.fold(b));
	REQUIRE(b.projectileName == "TestArrow");
	REQUIRE(near(b.primaryDamage, 0.0f));		// WeaponTemplate::postProcessLoad takes the warhead's
	REQUIRE(b.damageType == DAMAGE_EXPLOSION);

	BfmeWeaponNuggets onlyFiltered;
	onlyFiltered.m_damage.push_back(s_weapons["TestSword"].nuggets.m_damage[1]);
	BfmeWeaponNuggets::Fold o;
	o.damageType = DAMAGE_EXPLOSION;
	o.deathType = DEATH_NORMAL;
	REQUIRE(onlyFiltered.fold(o));
	REQUIRE(near(o.primaryDamage, 100.0f));
	REQUIRE(o.damageType == DAMAGE_STRUCTURAL);

	BfmeWeaponNuggets zh;	// a Zero Hour weapon: nothing to fold
	REQUIRE(!zh.fold(o));
}

TEST(bfme_armor_types_and_damage_scalar)
{
	REQUIRE(!load("armor.ini", kArmor, true));
	const ArmorTemplate *a = TheArmorStore->findArmorTemplate("TestInfantryArmor");
	REQUIRE(a != nullptr);
	REQUIRE(near(a->getDamageScalar(), 0.5f));
	REQUIRE(near(a->adjustDamage(DAMAGE_SLASH, 100.0f), 12.5f));		// 25% * DamageScalar 50%
	REQUIRE(near(a->adjustDamage(DAMAGE_PIERCE, 100.0f), 25.0f));		// DEFAULT 50% * 50%
	REQUIRE(near(a->adjustDamage(DAMAGE_SIEGE, 100.0f), 100.0f));		// 200% * 50%
	REQUIRE(near(a->adjustDamage(DAMAGE_UNRESISTABLE, 100.0f), 100.0f));
	REQUIRE(INILenient::counters().fields + INILenient::counters().values == 0);

	const ArmorTemplate *zh = TheArmorStore->findArmorTemplate("TestZhArmor");
	REQUIRE(zh != nullptr);
	REQUIRE(near(zh->getDamageScalar(), 1.0f));
	REQUIRE(near(zh->adjustDamage(DAMAGE_SMALL_ARMS, 100.0f), 40.0f));
	REQUIRE(near(zh->adjustDamage(DAMAGE_SLASH, 100.0f), 100.0f));
}

// ---- E2-04: experience levels and attribute modifiers -------------------------------------------

const char *kModifiers =
	"#define TEST_FARM_BONUS 1.2\n"
	"ModifierList TestLeadership\n"
	"  Category = LEADERSHIP\n"
	"  Modifier = DAMAGE_MULT 160%\n"
	"  Modifier = ARMOR 50%\n"
	"  Modifier = EXPERIENCE 200%\n"
	"  Duration = 3000\n"
	"  FX = FX_SomeLeadership\n"
	"  MultiLevelFX = Yes\n"
	"End\n"
	"ModifierList TestLevelUp\n"
	"  Category = LEVEL\n"
	"  Modifier = DAMAGE_MULT 125%\n"
	"  Modifier = ARMOR 40%\n"
	"  Modifier = HEALTH 100\n"
	"  Modifier = RATE_OF_FIRE 150%\n"	/* BFME2 type: line dropped */
	"End\n"
	"ModifierList TestFarmLevel2\n"
	"  Category = STRUCTURE\n"
	"  Modifier = PRODUCTION TEST_FARM_BONUS %\n"
	"  Modifier = DAMAGE_ADD 10\n"
	"End\n"
	"ModifierList TestSpell\n"
	"  Category = SPELL\n"
	"  Modifier = SPELL_DAMAGE 300%\n"
	"  Modifier = DAMAGE_MULT 50%\n"
	"  Upgrade = Upgrade_TestA Upgrade_TestB Delay:500\n"
	"End\n";

const char *kLevels =
	"#define TEST_HERO_LVL2_EXP 200\n"
	"ExperienceLevel TestHeroLevel3\n"
	"  TargetNames = TestHero TestHeroSummoned\n"
	"  RequiredExperience = 600\n"
	"  ExperienceAward = 300\n"
	"  Rank = 3\n"
	"  AttributeModifiers = TestLevelUp\n"
	"  LevelUpFx = FX:TestLevelUpFX\n"
	"  SelectionDecal\n"
	"    Texture = SomeDecal\n"
	"    Style = SHADOW_ALPHA_DECAL\n"
	"  End\n"
	"End\n"
	"ExperienceLevel TestHeroLevel1\n"
	"  TargetNames = TestHero TestHeroSummoned\n"
	"  RequiredExperience = 1\n"
	"  ExperienceAward = 100\n"
	"  Rank = 1\n"
	"End\n"
	"ExperienceLevel TestHeroLevel2\n"
	"  TargetNames = TestHero\n"
	"  TargetNames = TestHeroSummoned\n"
	"  RequiredExperience = TEST_HERO_LVL2_EXP\n"
	"  ExperienceAward = 200\n"
	"  Rank = 2\n"
	"  AttributeModifiers = TestLevelUp TestFarmLevel2\n"
	"  Upgrades = Upgrade_TestHeroAbility\n"
	"End\n"
	"ExperienceLevel TestSoldierLevel2\n"
	"  TargetNames = TestSoldier\n"
	"  RequiredExperience = 50\n"
	"  ExperienceAward = 20\n"
	"  Rank = 2\n"
	"End\n";

TEST(modifier_amounts_percent_and_plain)
{
	REQUIRE(near(bfmeScanAmount("160%"), 1.6f));
	REQUIRE(near(bfmeScanAmount("50%"), 0.5f));
	REQUIRE(near(bfmeScanAmount("1.2"), 1.2f));		// plain number: as written (PRODUCTION 1.2 % = x1.2)
	REQUIRE(near(bfmeScanAmount("100"), 100.0f));	// HEALTH 100 = +100 hit points
}

TEST(parse_modifier_lists)
{
	REQUIRE(!load("attributemodifier.ini", kModifiers, true));
	const BfmeModifierList *lead = TheBfmeAttributeStore->findModifierList("TestLeadership");
	REQUIRE(lead != nullptr);
	REQUIRE(lead->m_category == BFME_MODCAT_LEADERSHIP);
	REQUIRE(lead->m_modifiers.size() == 3);
	REQUIRE(near(lead->product(BFME_MOD_DAMAGE_MULT), 1.6f));
	REQUIRE(near(lead->sum(BFME_MOD_ARMOR), 0.5f));
	REQUIRE(near(lead->product(BFME_MOD_EXPERIENCE), 2.0f));
	REQUIRE(lead->m_durationFrames == 90);	// 3000 ms at 30 logic frames/s

	const BfmeModifierList *lvl = TheBfmeAttributeStore->findModifierList("TestLevelUp");
	REQUIRE(lvl != nullptr);
	REQUIRE(lvl->m_category == BFME_MODCAT_LEVEL);
	REQUIRE(lvl->m_modifiers.size() == 3);	// RATE_OF_FIRE dropped
	REQUIRE(near(lvl->sum(BFME_MOD_HEALTH), 100.0f));
	REQUIRE(lvl->m_durationFrames == 0);

	const BfmeModifierList *farm = TheBfmeAttributeStore->findModifierList("TestFarmLevel2");
	REQUIRE(farm != nullptr);
	REQUIRE(near(farm->product(BFME_MOD_PRODUCTION), 1.2f));	// #define + separate "%": x1.2, not x0.012
	REQUIRE(near(farm->sum(BFME_MOD_DAMAGE_ADD), 10.0f));

	const BfmeModifierList *spell = TheBfmeAttributeStore->findModifierList("TestSpell");
	REQUIRE(spell != nullptr);
	REQUIRE(spell->m_upgrades.size() == 2);
	REQUIRE(spell->m_upgrades[1] == AsciiString("Upgrade_TestB"));
	REQUIRE(TheBfmeAttributeStore->findModifierList("NoSuchList") == nullptr);
	REQUIRE(INILenient::counters().fields + INILenient::counters().values + INILenient::counters().blocks == 0);
}

TEST(parse_experience_levels_into_tables)
{
	REQUIRE(!load("experiencelevels.ini", kLevels, true));
	const BfmeLevelTable *hero = TheBfmeAttributeStore->findLevelTable("TestHero");
	REQUIRE(hero != nullptr);
	REQUIRE(hero->getMaxLevel() == 3);
	REQUIRE(hero->getLevel(1)->m_name == AsciiString("TestHeroLevel1"));	// sorted by RequiredExperience
	REQUIRE(hero->getLevel(2)->m_requiredExperience == 200);				// #define resolved
	REQUIRE(hero->getLevel(2)->m_attributeModifiers.size() == 2);
	REQUIRE(hero->getLevel(2)->m_upgrades.size() == 1);
	REQUIRE(hero->getLevel(3)->m_experienceAward == 300);
	REQUIRE(hero->getLevel(4) == nullptr);

	const BfmeLevelTable *summoned = TheBfmeAttributeStore->findLevelTable("TestHeroSummoned");	// two TargetNames lines
	REQUIRE(summoned != nullptr);
	REQUIRE(summoned->getMaxLevel() == 3);
	REQUIRE(TheBfmeAttributeStore->findLevelTable("TestSoldier")->getMaxLevel() == 1);
	REQUIRE(TheBfmeAttributeStore->findLevelTable("TestTower") == nullptr);
}

TEST(level_from_experience_and_veterancy_mapping)
{
	REQUIRE(!load("experiencelevels.ini", kLevels, true));
	const BfmeLevelTable *hero = TheBfmeAttributeStore->findLevelTable("TestHero");
	REQUIRE(hero->levelForExperience(0) == 1);	// below every requirement: the first level
	REQUIRE(hero->levelForExperience(199) == 1);
	REQUIRE(hero->levelForExperience(200) == 2);
	REQUIRE(hero->levelForExperience(599) == 2);
	REQUIRE(hero->levelForExperience(100000) == 3);
	REQUIRE(hero->experienceForLevel(3) == 600);
	REQUIRE(hero->experienceForLevel(9) == 600);	// clamped to the last level
	REQUIRE(BfmeLevelTable::veterancyForLevel(1) == LEVEL_REGULAR);
	REQUIRE(BfmeLevelTable::veterancyForLevel(2) == LEVEL_VETERAN);
	REQUIRE(BfmeLevelTable::veterancyForLevel(3) == LEVEL_ELITE);
	REQUIRE(BfmeLevelTable::veterancyForLevel(10) == LEVEL_HEROIC);
}

TEST(modifier_lists_refresh_stack_and_expire)
{
	REQUIRE(!load("attributemodifier.ini", kModifiers, true));
	const BfmeModifierList *lead = TheBfmeAttributeStore->findModifierList("TestLeadership");
	const BfmeModifierList *lvl = TheBfmeAttributeStore->findModifierList("TestLevelUp");
	BfmeAttributeSet set;
	REQUIRE(near(set.damageMult(DAMAGE_SLASH, 0), 1.0f));

	REQUIRE(set.add(lead, 100, lead->m_durationFrames));		// first time: one-shot effects apply
	REQUIRE(!set.add(lead, 150, lead->m_durationFrames));		// two heroes' auras: refreshed, not doubled
	REQUIRE(set.getActive().size() == 1);
	REQUIRE(near(set.damageMult(DAMAGE_SLASH, 200), 1.6f));
	REQUIRE(set.add(lvl, 200, 0));													// a different list stacks
	REQUIRE(near(set.damageMult(DAMAGE_SLASH, 200), 2.0f));	// 160% * 125%
	REQUIRE(near(set.experienceMult(200), 2.0f));

	REQUIRE(set.isActive(lead, 239));												// refreshed at 150: until 240
	REQUIRE(!set.isActive(lead, 240));
	REQUIRE(near(set.damageMult(DAMAGE_SLASH, 240), 1.25f));	// leadership left, the level bonus stays
	std::vector<const BfmeModifierList*> ended;
	set.expire(240, &ended);
	REQUIRE(ended.size() == 1 && ended[0] == lead);
	REQUIRE(set.getActive().size() == 1);
	REQUIRE(set.add(lead, 300, lead->m_durationFrames));		// back in range: new again
	REQUIRE(set.remove(lvl));
	REQUIRE(!set.remove(lvl));
}

TEST(attacker_damage_mult_add_and_spell_damage)
{
	REQUIRE(!load("attributemodifier.ini", kModifiers, true));
	BfmeAttributeSet set;
	set.add(TheBfmeAttributeStore->findModifierList("TestLeadership"), 0, 0);
	set.add(TheBfmeAttributeStore->findModifierList("TestSpell"), 0, 0);
	set.add(TheBfmeAttributeStore->findModifierList("TestFarmLevel2"), 0, 0);
	REQUIRE(near(set.damageMult(DAMAGE_SLASH, 1), 0.8f));		// DAMAGE_MULT 160% * 50%
	REQUIRE(near(set.damageMult(DAMAGE_MAGIC, 1), 3.0f));		// SPELL_DAMAGE replaces DAMAGE_MULT for MAGIC
	REQUIRE(near(set.damageAdd(1), 10.0f));

	// (base + DAMAGE_ADD) * mult, DAMAGE_ADD only on nuggets with AcceptDamageAdd.
	BfmeDamageNugget n;
	n.m_damage = 40.0f;
	const BfmeObjectFilter::Subject orc = subject({ KINDOF_INFANTRY });
	REQUIRE(near(n.damageFor(orc, TRUE, 0.0f, 0.0f, 1.0f * 1.6f, 1.0f, 10.0f), 64.0f));
	n.m_acceptDamageAdd = TRUE;
	REQUIRE(near(n.damageFor(orc, TRUE, 0.0f, 0.0f, 1.0f * 1.6f, 1.0f, 10.0f), 80.0f));
}

TEST(victim_armor_modifiers_capped)
{
	REQUIRE(!load("attributemodifier.ini", kModifiers, true));
	BfmeAttributeSet set;
	REQUIRE(near(set.armorFactor(DAMAGE_SLASH, 0, 0.8f), 1.0f));
	set.add(TheBfmeAttributeStore->findModifierList("TestLeadership"), 0, 0);	// ARMOR 50%
	REQUIRE(near(set.armorFactor(DAMAGE_SLASH, 0, 1.0f), 0.5f));
	set.add(TheBfmeAttributeStore->findModifierList("TestLevelUp"), 0, 0);		// + ARMOR 40%
	REQUIRE(near(set.armorFactor(DAMAGE_SLASH, 0, 1.0f), 0.1f));
	REQUIRE(near(set.armorFactor(DAMAGE_SLASH, 0, 0.8f), 0.2f));		// capped at AttributeModifierArmorMaxBonus
	REQUIRE(near(set.armorFactor(DAMAGE_HEALING, 0, 0.8f), 1.0f));	// not weapon damage
	REQUIRE(near(set.armorFactor(DAMAGE_UNRESISTABLE, 0, 0.8f), 1.0f));
	REQUIRE(near(BfmeAttributeSet::armorFactorFor(-0.25f, 0.8f), 1.25f));	// a curse: more damage
	REQUIRE(near(BfmeAttributeSet::armorFactorFor(2.0f, 1.5f), 0.0f));		// never below zero

	// GameData spelling of the cap.
	REQUIRE(near(bfmeScanAmount("80%"), 0.8f));
}

TEST(zh_mode_parses_modifier_and_level_blocks)
{
	// Without the lenient mode (no #define, no unknown fields) the blocks parse with the same tables.
	const char *strict =
		"ModifierList TestStrict\n"
		"  Category = WEAPON\n"
		"  Modifier = DAMAGE_MULT 125%\n"
		"End\n"
		"ExperienceLevel TestStrictLevel2\n"
		"  TargetNames = TestStrictUnit\n"
		"  RequiredExperience = 50\n"
		"  Rank = 2\n"
		"  AttributeModifiers = TestStrict\n"
		"End\n";
	REQUIRE(!load("strict.ini", strict, false));
	const BfmeModifierList *list = TheBfmeAttributeStore->findModifierList("TestStrict");
	REQUIRE(list != nullptr && list->m_category == BFME_MODCAT_WEAPON);
	REQUIRE(near(list->product(BFME_MOD_DAMAGE_MULT), 1.25f));
	REQUIRE(TheBfmeAttributeStore->findLevelTable("TestStrictUnit")->getLevel(1)->m_rank == 2);
}
