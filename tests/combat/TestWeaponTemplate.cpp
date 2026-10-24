// BFME1 Mobile — tests (E2-03): the real WeaponTemplate/WeaponStore (Weapon.cpp) on BFME1 nugget weapons.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Synthetic Weapon.ini text goes through WeaponStore::parseWeaponTemplateDefinition and
// WeaponStore::postProcessLoad exactly as in the game, to check what the unchanged Zero Hour code
// then sees: PrimaryDamage, radius and damage type folded from the nuggets (AI target choice and
// isDamageWeapon read these), the projectile name, the warhead link and its damage, and that a
// Zero Hour weapon stays as it was.
//
// Weapon.cpp references most of the game logic (objects, partition manager, FX); none of that
// runs on the load path tested here, so the binary links with unresolved symbols ignored
// (tests/CMakeLists.txt). A test that reached such code would crash, not pass.
#include "PreRTS.h"

#include "Common/FileSystem.h"
#include "Common/INI.h"
#include "Common/INILenient.h"
#include "Common/LocalFileSystem.h"
#include "Common/NameKeyGenerator.h"
#include "Common/ThingFactory.h"
#include "GameLogic/Weapon.h"
#include "StdDevice/Common/StdLocalFileSystem.h"

#include "../vfs/TestHarness.h"

#include <cmath>
#include <fstream>
#include <string>
#include <unistd.h>

namespace {

std::string writeIni(const char *name, const char *text)
{
	static std::string dir;
	if (dir.empty()) {
		char tmpl[] = "/tmp/bfme-combat-weapon-XXXXXX";
		REQUIRE(mkdtemp(tmpl) != nullptr);
		dir = tmpl;
	}
	const std::string path = dir + "/" + name;
	std::ofstream(path, std::ios::binary) << text;
	return path;
}

bool near(Real a, Real b) { return std::fabs(a - b) < 1e-4f; }

// postProcessLoad only checks that TheThingFactory exists; ThingFactory::findTemplateInternal is
// the IniEngineStubs one (no objects, so projectile templates stay unresolved, as asserted below).
alignas(16) unsigned char s_fakeThingFactory[64];

void loadWeapons(const char *text)
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
	if (TheWeaponStore == nullptr)
		TheWeaponStore = new WeaponStore;
	TheThingFactory = reinterpret_cast<ThingFactory *>(s_fakeThingFactory);

	INILenient::reset();
	INILenient::setEnabled(TRUE);
	bool threw = false;
	try {
		INI ini;
		ini.load(AsciiString(writeIni("weapon.ini", text).c_str()), INI_LOAD_OVERWRITE, nullptr);
		TheWeaponStore->postProcessLoad();
	} catch (...) {
		threw = true;
	}
	INILenient::setEnabled(FALSE);
	REQUIRE(!threw);
}

const WeaponTemplate *weapon(const char *name)
{
	const WeaponTemplate *w = TheWeaponStore->findWeaponTemplate(name);
	REQUIRE(w != nullptr);
	return w;
}

const char *const kWeapons =
	"Weapon CwSword\n"
	"  AttackRange = 20\n"
	"  DamageNugget\n"
	"    Damage = 40\n"
	"    Radius = 8.0\n"
	"    DamageType = SLASH\n"
	"    DeathType = NORMAL\n"
	"  End\n"
	"  DamageNugget\n"
	"    Damage = 100\n"
	"    DamageType = STRUCTURAL\n"
	"    SpecialObjectFilter = ANY +STRUCTURE\n"
	"  End\n"
	"  MetaImpactNugget\n"
	"    ShockWaveAmount = 20\n"
	"    ShockWaveRadius = 15\n"
	"  End\n"
	"End\n"
	"Weapon CwBow\n"
	"  AttackRange = 200\n"
	"  ProjectileNugget\n"
	"    ProjectileTemplateName = CwArrow\n"
	"    WarheadTemplateName = CwBowWarhead\n"
	"  End\n"
	"End\n"
	"Weapon CwBowWarhead\n"
	"  DamageNugget\n"
	"    Damage = 25\n"
	"    Radius = 2\n"
	"    DamageType = PIERCE\n"
	"    DeathType = KNOCKBACK\n"
	"  End\n"
	"End\n"
	"Weapon CwZhGun\n"
	"  PrimaryDamage = 12\n"
	"  PrimaryDamageRadius = 3\n"
	"  DamageType = SMALL_ARMS\n"
	"  AttackRange = 150\n"
	"End\n";

} // namespace

void INI::parseWeaponTemplateDefinition(INI *ini) { WeaponStore::parseWeaponTemplateDefinition(ini); }
void INI::parseTerrainDefinition(INI *) {}

TEST(weapon_template_folds_damage_nuggets)
{
	loadWeapons(kWeapons);
	const WeaponTemplate *w = weapon("CwSword");
	WeaponBonus none;
	REQUIRE(near(w->getPrimaryDamage(none), 40.0f));		// the +STRUCTURE nugget is not the ordinary estimate
	REQUIRE(near(w->getPrimaryDamageRadius(none), 8.0f));
	REQUIRE(near(w->getSecondaryDamage(none), 0.0f));
	REQUIRE(w->getDamageType() == DAMAGE_SLASH);
	REQUIRE(w->getBfmeNuggets().m_damage.size() == 2);
	REQUIRE(w->getBfmeNuggets().m_metaImpact.size() == 1);
	REQUIRE(w->getBfmeNuggets().m_warheadTmpl == nullptr);
	REQUIRE(w->getProjectileTemplate() == nullptr);
	REQUIRE(near(w->getUnmodifiedAttackRange(), 20.0f));
}

TEST(weapon_template_projectile_takes_warhead_damage)
{
	loadWeapons(kWeapons);
	const WeaponTemplate *bow = weapon("CwBow");
	const WeaponTemplate *warhead = weapon("CwBowWarhead");
	WeaponBonus none;
	REQUIRE(bow->getBfmeNuggets().m_warheadTmpl == warhead);
	REQUIRE(near(bow->getPrimaryDamage(none), 25.0f));		// estimate = the warhead's
	REQUIRE(near(bow->getPrimaryDamageRadius(none), 2.0f));
	REQUIRE(bow->getDamageType() == DAMAGE_PIERCE);
	REQUIRE(bow->getDeathType() == DEATH_KNOCKBACK);
	REQUIRE(near(warhead->getPrimaryDamage(none), 25.0f));
	REQUIRE(warhead->getBfmeNuggets().m_warheadTmpl == nullptr);
}

TEST(weapon_template_zero_hour_weapon_unchanged)
{
	loadWeapons(kWeapons);
	const WeaponTemplate *gun = weapon("CwZhGun");
	WeaponBonus none;
	REQUIRE(!gun->getBfmeNuggets().hasAny());
	REQUIRE(near(gun->getPrimaryDamage(none), 12.0f));
	REQUIRE(near(gun->getPrimaryDamageRadius(none), 3.0f));
	REQUIRE(gun->getDamageType() == DAMAGE_SMALL_ARMS);
}
