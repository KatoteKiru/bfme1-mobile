// BFME1 Mobile — tests
// SPDX-License-Identifier: GPL-3.0-or-later
//
// loadMods() reads TheGlobalData. The real GlobalData.cpp pulls the rest of the game, so this
// translation unit supplies the constructor and the virtuals the vtable needs, plus the Money
// and Snapshot symbols that constructor calls. Only m_userDataDir, m_modBIG and m_modDir are
// meaningful here.
#include "PreRTS.h"

#include "EngineFixture.h"

#include "Common/GlobalData.h"
#include "Common/Money.h"

#include <string>

namespace bfmetest {

std::string g_testUserDataDir;

void setTestUserDataDir(const std::string& dir)
{
	g_testUserDataDir = dir;
}

} // namespace bfmetest

Snapshot::Snapshot()
{
}

Snapshot::~Snapshot()
{
}

void Money::setStartingCash(UnsignedInt amount)
{
	m_money = amount;
	m_currentBucket = 0;
	m_cashPerMinute = 0;
	for (UnsignedInt& bucket : m_incomeBuckets) {
		bucket = 0;
	}
}

void Money::crc(Xfer*)
{
}

void Money::xfer(Xfer*)
{
}

void Money::loadPostProcess()
{
}

GlobalData::GlobalData()
{
	if (!bfmetest::g_testUserDataDir.empty()) {
		std::string dir = bfmetest::g_testUserDataDir;
		if (dir.back() != '/' && dir.back() != '\\') {
			dir.push_back('/');
		}
		m_userDataDir = dir.c_str();
	}
}

GlobalData::~GlobalData()
{
}

void GlobalData::init()
{
}

void GlobalData::reset()
{
}

GlobalData* GlobalData::newOverride()
{
	return nullptr;
}
