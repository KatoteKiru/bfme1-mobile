// BFME1 Mobile — engine
// SPDX-License-Identifier: GPL-3.0-or-later
//
// BFME: E2-04. BFME1 modules that hand out ModifierLists and experience levels:
//   AttributeModifierAuraUpdate  hero leadership and other auras (ModifierList on objects in range)
//   AttributeModifierUpgrade     a ModifierList for as long as an upgrade is owned (forge, banners)
//   ExperienceLevelCreate        object created at a given level (summons, LevelToGrant = 10)
//   LevelUpUpgrade               an upgrade grants levels
// Field names follow OpenSAGE (Logic/Object/Update/AttributeModifierAuraUpdate.cs,
// Upgrade/AttributeModifierUpgrade.cs, Create/ExperienceLevelCreate.cs, Upgrade/LevelUpUpgrade.cs).
#pragma once

#include "GameLogic/BfmeWeaponNuggets.h"
#include "GameLogic/Module/CreateModule.h"
#include "GameLogic/Module/UpdateModule.h"
#include "GameLogic/Module/UpgradeModule.h"

//-------------------------------------------------------------------------------------------------
class AttributeModifierAuraUpdateModuleData : public UpdateModuleData
{
public:
	UpgradeMuxData m_upgradeMuxData;
	Bool m_startsActive;
	AsciiString m_bonusName;				///< ModifierList
	UnsignedInt m_refreshDelay;			///< frames between scans
	Real m_range;
	Bool m_targetEnemy;							///< no relationship in the filter: enemies instead of allies
	Bool m_allowSelf;
	Bool m_runWhileDead;
	BfmeObjectFilter m_objectFilter;

	AttributeModifierAuraUpdateModuleData();
	static void buildFieldParse( MultiIniFieldParse& p );
};

class AttributeModifierAuraUpdate : public UpdateModule, public UpgradeMux
{
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE( AttributeModifierAuraUpdate, "AttributeModifierAuraUpdate" )
	MAKE_STANDARD_MODULE_MACRO_WITH_MODULE_DATA( AttributeModifierAuraUpdate, AttributeModifierAuraUpdateModuleData )

public:
	AttributeModifierAuraUpdate( Thing *thing, const ModuleData* moduleData );
	// virtual destructor prototype provided by memory pool declaration

	static Int getInterfaceMask() { return UpdateModule::getInterfaceMask() | MODULEINTERFACE_UPGRADE; }
	virtual UpgradeModuleInterface* getUpgrade() override { return this; }

	virtual UpdateSleepTime update() override;

protected:
	virtual void upgradeImplementation() override { setWakeFrame( getObject(), UPDATE_SLEEP_NONE ); }
	virtual void getUpgradeActivationMasks( UpgradeMaskType& activation, UpgradeMaskType& conflicting ) const override
	{
		getAttributeModifierAuraUpdateModuleData()->m_upgradeMuxData.getUpgradeActivationMasks( activation, conflicting );
	}
	virtual void performUpgradeFX() override { getAttributeModifierAuraUpdateModuleData()->m_upgradeMuxData.performUpgradeFX( getObject() ); }
	virtual void processUpgradeRemoval() override { getAttributeModifierAuraUpdateModuleData()->m_upgradeMuxData.muxDataProcessUpgradeRemoval( getObject() ); }
	virtual Bool requiresAllActivationUpgrades() const override { return getAttributeModifierAuraUpdateModuleData()->m_upgradeMuxData.m_requiresAllTriggers; }
	virtual Bool isSubObjectsUpgrade() override { return false; }
};

//-------------------------------------------------------------------------------------------------
class AttributeModifierUpgradeModuleData : public UpgradeModuleData
{
public:
	AsciiString m_attributeModifier;	///< ModifierList
	AttributeModifierUpgradeModuleData() { }
	static void buildFieldParse( MultiIniFieldParse& p );
};

class AttributeModifierUpgrade : public UpgradeModule
{
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE( AttributeModifierUpgrade, "AttributeModifierUpgrade" )
	MAKE_STANDARD_MODULE_MACRO_WITH_MODULE_DATA( AttributeModifierUpgrade, AttributeModifierUpgradeModuleData );

public:
	AttributeModifierUpgrade( Thing *thing, const ModuleData* moduleData );
	// virtual destructor prototype defined by MemoryPoolObject

protected:
	virtual void upgradeImplementation() override;
	virtual Bool isSubObjectsUpgrade() override { return false; }
};

//-------------------------------------------------------------------------------------------------
class ExperienceLevelCreateModuleData : public CreateModuleData
{
public:
	Int m_levelToGrant;
	Bool m_mpOnly;
	ExperienceLevelCreateModuleData() : m_levelToGrant( 1 ), m_mpOnly( FALSE ) { }
	static void buildFieldParse( MultiIniFieldParse& p );
};

class ExperienceLevelCreate : public CreateModule
{
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE( ExperienceLevelCreate, "ExperienceLevelCreate" );
	MAKE_STANDARD_MODULE_MACRO_WITH_MODULE_DATA( ExperienceLevelCreate, ExperienceLevelCreateModuleData );

public:
	ExperienceLevelCreate( Thing *thing, const ModuleData* moduleData );
	// virtual destructor prototype provided by memory pool declaration

	virtual void onCreate() override;
};

//-------------------------------------------------------------------------------------------------
class LevelUpUpgradeModuleData : public UpgradeModuleData
{
public:
	Int m_levelsToGain;
	Int m_levelCap;
	LevelUpUpgradeModuleData() : m_levelsToGain( 1 ), m_levelCap( 0 ) { }
	static void buildFieldParse( MultiIniFieldParse& p );
};

class LevelUpUpgrade : public UpgradeModule
{
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE( LevelUpUpgrade, "LevelUpUpgrade" )
	MAKE_STANDARD_MODULE_MACRO_WITH_MODULE_DATA( LevelUpUpgrade, LevelUpUpgradeModuleData );

public:
	LevelUpUpgrade( Thing *thing, const ModuleData* moduleData );
	// virtual destructor prototype defined by MemoryPoolObject

protected:
	virtual void upgradeImplementation() override;
	virtual Bool isSubObjectsUpgrade() override { return false; }
};
