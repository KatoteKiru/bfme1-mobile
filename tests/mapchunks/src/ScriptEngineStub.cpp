// BFME1 Mobile — tests (E1-52)
// SPDX-License-Identifier: GPL-3.0-or-later
//
// ParseAction / ParseCondition call TheScriptEngine->getActionTemplate. The real
// ScriptEngine.cpp is compiled by bfme_e152_compile and is not linked here.
#include "PreRTS.h"

#include "Common/BitFlags.h"
#include "Common/CriticalSection.h"
#include "Common/Errors.h"
#include "Common/GlobalData.h"
#include "Common/NameKeyGenerator.h"
#include "Common/Snapshot.h"
#include "Common/SubsystemInterface.h"
#include "GameLogic/ScriptEngine.h"

CriticalSection *TheAsciiStringCriticalSection = nullptr;
CriticalSection *TheUnicodeStringCriticalSection = nullptr;

// Zeroed storage: only m_mapName is written. The INI constructor is not linked.
alignas(GlobalData) static unsigned char s_globalDataStorage[sizeof(GlobalData)] = {};
GlobalData *TheWritableGlobalData = reinterpret_cast<GlobalData *>(s_globalDataStorage);

// Referenced from Parameter::ReadParameter. The name tables live in TUs this test does not link.
template<> const char* const BitFlags<116>::s_bitNameList[] = { nullptr };
template<> const char* const BitFlags<45>::s_bitNameList[] = { nullptr };

Snapshot::Snapshot() {}
Snapshot::~Snapshot() {}

SubsystemInterface::SubsystemInterface() {}
SubsystemInterface::~SubsystemInterface() {}

AttackPriorityInfo::AttackPriorityInfo() : m_defaultPriority(0), m_priorityMap(nullptr) {}
AttackPriorityInfo::~AttackPriorityInfo() {}
void AttackPriorityInfo::crc(Xfer *) {}
void AttackPriorityInfo::xfer(Xfer *) {}
void AttackPriorityInfo::loadPostProcess() {}

ScriptEngine *TheScriptEngine = nullptr;

static ActionTemplate s_knownAction;
static ConditionTemplate s_knownCondition;
static ActionTemplate s_otherAction;
static ConditionTemplate s_otherCondition;

void bfmeTestPrimeScriptTemplates(NameKeyType action, NameKeyType condition)
{
	s_knownAction.m_internalNameKey = action;
	s_knownAction.m_numParameters = 0;
	s_knownAction.m_uiName = "ZhDebugMessage";
	s_knownCondition.m_internalNameKey = condition;
	s_knownCondition.m_numParameters = 0;
	s_knownCondition.m_uiName = "ZhTrueCondition";
	s_otherAction.m_internalNameKey = NAMEKEY("UnusedZhActionSlot");
	s_otherAction.m_numParameters = 0;
	s_otherAction.m_uiName = "unused";
	s_otherCondition.m_internalNameKey = NAMEKEY("UnusedZhConditionSlot");
	s_otherCondition.m_numParameters = 0;
	s_otherCondition.m_uiName = "unused";
}

ScriptEngine::ScriptEngine() {}
ScriptEngine::~ScriptEngine() {}
void ScriptEngine::init() {}
void ScriptEngine::reset() {}
void ScriptEngine::update() {}
void ScriptEngine::crc(Xfer *) {}
void ScriptEngine::xfer(Xfer *) {}
void ScriptEngine::loadPostProcess() {}
void ScriptEngine::newMap() {}

const ActionTemplate *ScriptEngine::getActionTemplate(Int ndx)
{
	return ndx == 0 ? &s_knownAction : &s_otherAction;
}

const ConditionTemplate *ScriptEngine::getConditionTemplate(Int ndx)
{
	return ndx == 0 ? &s_knownCondition : &s_otherCondition;
}

void ScriptEngine::startEndGameTimer() {}
void ScriptEngine::startQuickEndGameTimer() {}
void ScriptEngine::startCloseWindowTimer() {}
void ScriptEngine::runScript(const AsciiString &, Team *) {}
void ScriptEngine::runObjectScript(const AsciiString &, Object *) {}
Team *ScriptEngine::getTeamNamed(const AsciiString &) { return nullptr; }
Player *ScriptEngine::getSkirmishEnemyPlayer() { return nullptr; }
Player *ScriptEngine::getCurrentPlayer() { return nullptr; }
Player *ScriptEngine::getPlayerFromAsciiString(const AsciiString &) { return nullptr; }
ObjectTypes *ScriptEngine::getObjectTypes(const AsciiString &) { return nullptr; }
void ScriptEngine::doObjectTypeListMaintenance(const AsciiString &, const AsciiString &, Bool) {}
PolygonTrigger *ScriptEngine::getQualifiedTriggerAreaByName(AsciiString) { return nullptr; }
Bool ScriptEngine::evaluateConditions(Script *, Team *, Player *) { return false; }
void ScriptEngine::friend_executeAction(ScriptAction *, Team *) {}
Object *ScriptEngine::getUnitNamed(const AsciiString &) { return nullptr; }
Bool ScriptEngine::didUnitExist(const AsciiString &) { return false; }
void ScriptEngine::addObjectToCache(Object *) {}
void ScriptEngine::removeObjectFromCache(Object *) {}
void ScriptEngine::transferObjectName(const AsciiString &, Object *) {}
void ScriptEngine::notifyOfObjectDestruction(Object *) {}
void ScriptEngine::notifyOfCompletedVideo(const AsciiString &) {}
void ScriptEngine::notifyOfTriggeredSpecialPower(Int, const AsciiString &, ObjectID) {}
void ScriptEngine::notifyOfMidwaySpecialPower(Int, const AsciiString &, ObjectID) {}
void ScriptEngine::notifyOfCompletedSpecialPower(Int, const AsciiString &, ObjectID) {}
void ScriptEngine::notifyOfCompletedUpgrade(Int, const AsciiString &, ObjectID) {}
void ScriptEngine::notifyOfAcquiredScience(Int, ScienceType) {}
void ScriptEngine::signalUIInteract(const AsciiString &) {}
Bool ScriptEngine::isVideoComplete(const AsciiString &, Bool) { return false; }
Bool ScriptEngine::isSpeechComplete(const AsciiString &, Bool) { return false; }
Bool ScriptEngine::isAudioComplete(const AsciiString &, Bool) { return false; }
Bool ScriptEngine::isSpecialPowerTriggered(Int, const AsciiString &, Bool, ObjectID) { return false; }
Bool ScriptEngine::isSpecialPowerMidway(Int, const AsciiString &, Bool, ObjectID) { return false; }
Bool ScriptEngine::isSpecialPowerComplete(Int, const AsciiString &, Bool, ObjectID) { return false; }
Bool ScriptEngine::isUpgradeComplete(Int, const AsciiString &, Bool, ObjectID) { return false; }
Bool ScriptEngine::isScienceAcquired(Int, ScienceType, Bool) { return false; }
void ScriptEngine::adjustToppleDirection(Object *, Coord2D *) {}
void ScriptEngine::adjustToppleDirection(Object *, Coord3D *) {}
Script *ScriptEngine::findScript(const AsciiString &) { return nullptr; }

// GCC 14 emits SequentialScript's vtable in every TU that includes the inline
// destructor (EMPTY_DTOR). The test does not link ScriptEngine.cpp, where crc,
// xfer and loadPostProcess are defined, so the vtable's slots and the
// non-virtual thunks for the Snapshot base are provided here.
void SequentialScript::crc(Xfer *) {}
void SequentialScript::xfer(Xfer *) {}
void SequentialScript::loadPostProcess() {}
