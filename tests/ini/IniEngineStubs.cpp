// BFME1 Mobile — tests (E1-23a)
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Link stubs for bfme_ini_mode_tests. The binary compiles the REAL INI.cpp; its block table and
// field parsers reference most of the game (stores, modules, scripts). None of those run in the
// tests: they only load synthetic files through the "Terrain" block, whose parser
// (INI::parseTerrainDefinition) is supplied by TestIniMode.cpp. Everything here is a symbol only.
#include "PreRTS.h"

#include "Common/AudioEventRTS.h"
#include "Common/CriticalSection.h"
#include "Common/GameAudio.h"
#include "Common/GameCommon.h"
#include "Common/GlobalData.h"
#include "Common/Module.h"
#include "Common/INI.h"
#include "Common/Science.h"
#include "Common/SpecialPower.h"
#include "Common/SubsystemInterface.h"
#include "Common/ThingFactory.h"
#include "Common/Upgrade.h"
#include "GameClient/Anim2D.h"
#include "GameClient/ClientRandomValue.h"
#include "GameClient/Drawable.h"
#include "GameClient/FXList.h"
#include "GameClient/GameText.h"
#include "GameClient/Image.h"
#include "GameClient/ParticleSys.h"
#include "GameLogic/Armor.h"
#include "GameLogic/Damage.h"
#include "GameLogic/FPUControl.h"
#include "GameLogic/Module/UpdateModule.h"
#include "GameLogic/ObjectCreationList.h"
#include "GameLogic/ScriptEngine.h"
#include "GameLogic/Weapon.h"
#include "Common/DamageFX.h"
#include "Common/DisabledTypes.h"

// Engine globals: never set, the tests never reach code that dereferences them.
GlobalData *TheWritableGlobalData = nullptr;
AudioManager *TheAudio = nullptr;
CriticalSection *TheAsciiStringCriticalSection = nullptr;
CriticalSection *TheUnicodeStringCriticalSection = nullptr;
SubsystemInterfaceList *TheSubsystemList = nullptr;
Anim2DCollection *TheAnim2DCollection = nullptr;
#ifndef BFME_COMBAT_TESTS	// tests/combat links the real Armor.cpp and Damage.cpp
ArmorStore *TheArmorStore = nullptr;
#endif
DamageFXStore *TheDamageFXStore = nullptr;
FXListStore *TheFXListStore = nullptr;
GameTextInterface *TheGameText = nullptr;
ImageCollection *TheMappedImageCollection = nullptr;
ObjectCreationListStore *TheObjectCreationListStore = nullptr;
ParticleSystemManager *TheParticleSystemManager = nullptr;
ScienceStore *TheScienceStore = nullptr;
SpecialPowerStore *TheSpecialPowerStore = nullptr;
ThingFactory *TheThingFactory = nullptr;
UpgradeCenter *TheUpgradeCenter = nullptr;
#ifndef BFME_COMBAT_WEAPON_TESTS	// tests/combat weapon binary links the real Weapon.cpp
WeaponStore *TheWeaponStore = nullptr;
#endif
const char *const TheVeterancyNames[] = { nullptr };
#ifndef BFME_COMBAT_TESTS
DamageTypeFlags DAMAGE_TYPE_FLAGS_NONE;
DamageTypeFlags DAMAGE_TYPE_FLAGS_ALL;
template<> const char *const DamageTypeFlags::s_bitNameList[] = { nullptr };
#endif
DisabledMaskType DISABLEDMASK_NONE;

void setFPMode() {}
void parseReallyLowMHz(INI *) {}
Bool GetStringFromGeneralsRegistry(AsciiString, AsciiString, AsciiString &) { return FALSE; }
Bool GetStringFromRegistry(AsciiString, AsciiString, AsciiString &) { return FALSE; }

AudioEventRTS::AudioEventRTS() {}
AudioEventRTS::~AudioEventRTS() {}
void AudioEventRTS::setEventName(AsciiString) {}
// E1-37: INIAudioEventInfo.cpp is linked for its real field table; its block parsers are never reached.
void AudioManager::addTrackName(const AsciiString &) {}

// Block parsers of INI.cpp's theTypeTable, except Terrain (TestIniMode.cpp, tests/combat) and
// WindowTransition (TestWindowTransitions.cpp).
#define BFME_INI_BLOCK_STUB(name) void INI::name(INI *) {}
BFME_INI_BLOCK_STUB(parseAIDataDefinition)
BFME_INI_BLOCK_STUB(parseAnim2DDefinition)
#ifndef BFME_COMBAT_TESTS
BFME_INI_BLOCK_STUB(parseArmorDefinition)
#endif
BFME_INI_BLOCK_STUB(parseAudioSettingsDefinition)
BFME_INI_BLOCK_STUB(parseBenchProfile)
BFME_INI_BLOCK_STUB(parseCampaignDefinition)
BFME_INI_BLOCK_STUB(parseChallengeModeDefinition)
BFME_INI_BLOCK_STUB(parseCommandButtonDefinition)
BFME_INI_BLOCK_STUB(parseCommandSetDefinition)
BFME_INI_BLOCK_STUB(parseControlBarResizerDefinition)
BFME_INI_BLOCK_STUB(parseControlBarSchemeDefinition)
BFME_INI_BLOCK_STUB(parseCrateTemplateDefinition)
BFME_INI_BLOCK_STUB(parseCredits)
BFME_INI_BLOCK_STUB(parseDamageFXDefinition)
BFME_INI_BLOCK_STUB(parseDrawGroupNumberDefinition)
BFME_INI_BLOCK_STUB(parseDynamicGameLODDefinition)
BFME_INI_BLOCK_STUB(parseEvaEvent)
BFME_INI_BLOCK_STUB(parseFXListDefinition)
BFME_INI_BLOCK_STUB(parseGameDataDefinition)
BFME_INI_BLOCK_STUB(parseHeaderTemplateDefinition)
BFME_INI_BLOCK_STUB(parseInGameUIDefinition)
BFME_INI_BLOCK_STUB(parseLODPreset)
BFME_INI_BLOCK_STUB(parseLanguageDefinition)
BFME_INI_BLOCK_STUB(parseLocomotorTemplateDefinition)
BFME_INI_BLOCK_STUB(parseMapCacheDefinition)
BFME_INI_BLOCK_STUB(parseMapDataDefinition)
BFME_INI_BLOCK_STUB(parseMappedImageDefinition)
BFME_INI_BLOCK_STUB(parseMetaMapDefinition)
BFME_INI_BLOCK_STUB(parseMiscAudio)
BFME_INI_BLOCK_STUB(parseMouseCursorDefinition)
BFME_INI_BLOCK_STUB(parseMouseDefinition)
BFME_INI_BLOCK_STUB(parseMultiplayerColorDefinition)
BFME_INI_BLOCK_STUB(parseMultiplayerSettingsDefinition)
BFME_INI_BLOCK_STUB(parseMultiplayerStartingMoneyChoiceDefinition)
BFME_INI_BLOCK_STUB(parseObjectCreationListDefinition)
BFME_INI_BLOCK_STUB(parseObjectDefinition)
BFME_INI_BLOCK_STUB(parseObjectReskinDefinition)
BFME_INI_BLOCK_STUB(parseOnlineChatColorDefinition)
BFME_INI_BLOCK_STUB(parseParticleSystemDefinition)
BFME_INI_BLOCK_STUB(parsePlayerTemplateDefinition)
BFME_INI_BLOCK_STUB(parseRankDefinition)
BFME_INI_BLOCK_STUB(parseScienceDefinition)
BFME_INI_BLOCK_STUB(parseShellMenuSchemeDefinition)
BFME_INI_BLOCK_STUB(parseSpecialPowerDefinition)
BFME_INI_BLOCK_STUB(parseStaticGameLODDefinition)
BFME_INI_BLOCK_STUB(parseTerrainBridgeDefinition)
BFME_INI_BLOCK_STUB(parseTerrainRoadDefinition)
BFME_INI_BLOCK_STUB(parseUpgradeDefinition)
BFME_INI_BLOCK_STUB(parseVideoDefinition)
BFME_INI_BLOCK_STUB(parseWaterSettingDefinition)
BFME_INI_BLOCK_STUB(parseWaterTransparencyDefinition)
#ifndef BFME_COMBAT_TESTS	// tests/combat parses Weapon blocks with the nugget tables
BFME_INI_BLOCK_STUB(parseWeaponTemplateDefinition)
#endif
BFME_INI_BLOCK_STUB(parseWeatherDefinition)
BFME_INI_BLOCK_STUB(parseWebpageURLDefinition)
#undef BFME_INI_BLOCK_STUB

void ScriptEngine::parseScriptAction(INI *) {}
void ScriptEngine::parseScriptCondition(INI *) {}

// Lookups behind INI::parse*Ref field parsers.
Anim2DTemplate *Anim2DCollection::findTemplate(const AsciiString &) { return nullptr; }
#ifndef BFME_COMBAT_TESTS
const ArmorTemplate *ArmorStore::findArmorTemplate(const char *) const { return nullptr; }
#endif
const DamageFX *DamageFXStore::findDamageFX(const char *) const { return nullptr; }
const FXList *FXListStore::findFXList(const char *) const { return nullptr; }
const Image *ImageCollection::findImageByName(const char *) const { return nullptr; }
const ObjectCreationList *ObjectCreationListStore::findObjectCreationList(const char *) const { return nullptr; }
ParticleSystemTemplate *ParticleSystemManager::findTemplate(const AsciiString &) const { return nullptr; }
ScienceType ScienceStore::friend_lookupScience(const char *) const { return SCIENCE_INVALID; }
SpecialPowerTemplate *SpecialPowerStore::findSpecialPowerTemplatePrivate(AsciiString) { return nullptr; }
ThingTemplate *ThingFactory::findTemplateInternal(const AsciiString &, Bool) { return nullptr; }
const UpgradeTemplate *UpgradeCenter::findUpgrade(const char *) const { return nullptr; }
#ifndef BFME_COMBAT_WEAPON_TESTS
const WeaponTemplate *WeaponStore::findWeaponTemplate(const char *) const { return nullptr; }
#endif
const char *const GameClientRandomVariable::DistributionTypeNames[] = { nullptr };
void GameClientRandomVariable::setRange(Real, Real, DistributionType) {}

// Out-of-line virtuals of classes INI.cpp instantiates through inline code.
void FXNugget::doFXObj(const Object *, const Object *) const {}
Object *ObjectCreationNugget::create(const Object *, const Coord3D *, const Coord3D *, Bool, UnsignedInt) const { return nullptr; }
Object *ObjectCreationNugget::create(const Object *, const Object *, UnsignedInt) const { return nullptr; }
void TintEnvelope::crc(Xfer *) {}
void TintEnvelope::xfer(Xfer *) {}
void TintEnvelope::loadPostProcess() {}
void SequentialScript::crc(Xfer *) {}
void SequentialScript::xfer(Xfer *) {}
void SequentialScript::loadPostProcess() {}
Module::~Module() {}
void Module::crc(Xfer *) {}
void Module::xfer(Xfer *) {}
void Module::loadPostProcess() {}
ObjectModule::~ObjectModule() {}
void ObjectModule::crc(Xfer *) {}
void ObjectModule::xfer(Xfer *) {}
void ObjectModule::loadPostProcess() {}
void BehaviorModule::crc(Xfer *) {}
void BehaviorModule::xfer(Xfer *) {}
void BehaviorModule::loadPostProcess() {}
void UpdateModule::crc(Xfer *) {}
void UpdateModule::xfer(Xfer *) {}
void UpdateModule::loadPostProcess() {}
