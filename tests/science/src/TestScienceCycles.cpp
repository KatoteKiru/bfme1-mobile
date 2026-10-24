// BFME1 Mobile — ScienceInfo::addRootSciences cycle guard.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Synthetic sciences only. Calls the real walk. A diamond must keep every root,
// in the same order the unguarded depth-first walk inserted them, and must not
// log a cycle. A cycle logs each directed loop once per load.
#include "PreRTS.h"

#include "../../heights/harness/TestHarness.h"

#include "Common/CriticalSection.h"
#include "Common/GameMemory.h"
#include "Common/Science.h"
#include "Common/SubsystemInterface.h"

#include <initializer_list>
#include <map>
#include <sstream>
#include <string>
#include <unistd.h>
#include <utility>
#include <vector>

CriticalSection* TheAsciiStringCriticalSection = nullptr;
CriticalSection* TheUnicodeStringCriticalSection = nullptr;
CriticalSection* TheDmaCriticalSection = nullptr;
CriticalSection* TheMemoryPoolCriticalSection = nullptr;

SubsystemInterface::SubsystemInterface() {}
SubsystemInterface::~SubsystemInterface() {}

namespace {

ScienceStore* g_store = nullptr;
NameKeyGenerator* g_keys = nullptr;
std::map<std::string, ScienceInfo*> g_nodes;

class StderrCapture {
public:
	StderrCapture()
	{
		m_pipe[0] = -1;
		m_pipe[1] = -1;
		if (pipe(m_pipe) != 0) {
			throw bfmetest::Failure("pipe failed");
		}
		m_saved = dup(STDERR_FILENO);
		if (m_saved < 0) {
			throw bfmetest::Failure("dup stderr failed");
		}
		std::fflush(stderr);
		if (dup2(m_pipe[1], STDERR_FILENO) < 0) {
			throw bfmetest::Failure("dup2 stderr failed");
		}
	}

	~StderrCapture()
	{
		restore();
		closePipe();
	}

	std::string stop()
	{
		restore();
		if (m_pipe[1] >= 0) {
			close(m_pipe[1]);
			m_pipe[1] = -1;
		}
		std::string out;
		char buf[1024];
		ssize_t n = 0;
		while (m_pipe[0] >= 0 && (n = ::read(m_pipe[0], buf, sizeof buf)) > 0) {
			out.append(buf, buf + static_cast<size_t>(n));
		}
		closePipe();
		return out;
	}

private:
	void restore()
	{
		if (m_saved >= 0) {
			std::fflush(stderr);
			dup2(m_saved, STDERR_FILENO);
			close(m_saved);
			m_saved = -1;
		}
	}

	void closePipe()
	{
		if (m_pipe[0] >= 0) {
			close(m_pipe[0]);
			m_pipe[0] = -1;
		}
		if (m_pipe[1] >= 0) {
			close(m_pipe[1]);
			m_pipe[1] = -1;
		}
	}

	int m_saved = -1;
	int m_pipe[2] = {-1, -1};
};

void requireNames(const std::vector<std::string>& got, std::initializer_list<const char*> expect)
{
	const std::vector<std::string> wanted(expect.begin(), expect.end());
	if (got == wanted) {
		++bfmetest::checkCount();
		return;
	}
	std::ostringstream os;
	os << "roots";
	for (const std::string& name : got) {
		os << " " << name;
	}
	os << " expected";
	for (const char* name : expect) {
		os << " " << name;
	}
	throw bfmetest::Failure(os.str());
}

} // namespace

class BfmeScienceWalkTest {
public:
	static void prepare()
	{
		static bool memory = false;
		if (!memory) {
			initMemoryManager();
			memory = true;
		}
		g_nodes.clear();
		delete g_store;
		g_store = new ScienceStore();
		TheScienceStore = g_store;
		g_store->init();
		if (g_keys == nullptr) {
			g_keys = new NameKeyGenerator();
			TheNameKeyGenerator = g_keys;
		}
		g_keys->init();
	}

	static void node(const char* name, std::initializer_list<const char*> prereqs)
	{
		ScienceInfo* info = new ScienceInfo();
		info->m_science = static_cast<ScienceType>(TheNameKeyGenerator->nameToKey(name));
		for (const char* prereq : prereqs) {
			info->m_prereqSciences.push_back(
				static_cast<ScienceType>(TheNameKeyGenerator->nameToKey(prereq)));
		}
		g_store->m_sciences.push_back(info);
		g_nodes[name] = info;
	}

	static std::vector<std::string> roots(const char* name)
	{
		ScienceInfo* info = g_nodes.at(name);
		info->m_rootSciences.clear();
		info->addRootSciences(info->m_rootSciences);
		std::vector<std::string> out;
		out.reserve(info->m_rootSciences.size());
		for (ScienceType science : info->m_rootSciences) {
			out.push_back(TheNameKeyGenerator->keyToName(static_cast<NameKeyType>(science)).str());
		}
		return out;
	}

	static void resetStore()
	{
		g_store->reset();
	}
};

TEST(science_cycle_a_b_a)
{
	BfmeScienceWalkTest::prepare();
	BfmeScienceWalkTest::node("Root", {});
	BfmeScienceWalkTest::node("Alpha", {"Beta", "Root"});
	BfmeScienceWalkTest::node("Beta", {"Alpha"});

	StderrCapture cap;
	const std::vector<std::string> alpha = BfmeScienceWalkTest::roots("Alpha");
	const std::vector<std::string> beta = BfmeScienceWalkTest::roots("Beta");
	const std::string log = cap.stop();
	requireNames(alpha, {"Root"});
	requireNames(beta, {"Root"});
	REQUIRE_EQ(log, std::string("Science prerequisite cycle: Alpha -> Beta -> Alpha\n"));

	BfmeScienceWalkTest::resetStore();
	StderrCapture again;
	const std::vector<std::string> betaAgain = BfmeScienceWalkTest::roots("Beta");
	const std::vector<std::string> alphaAgain = BfmeScienceWalkTest::roots("Alpha");
	const std::string logAgain = again.stop();
	requireNames(betaAgain, {"Root"});
	requireNames(alphaAgain, {"Root"});
	REQUIRE_EQ(logAgain, std::string("Science prerequisite cycle: Beta -> Alpha -> Beta\n"));
}

TEST(science_cycle_self)
{
	BfmeScienceWalkTest::prepare();
	BfmeScienceWalkTest::node("Loop", {"Loop"});

	StderrCapture cap;
	const std::vector<std::string> first = BfmeScienceWalkTest::roots("Loop");
	const std::vector<std::string> second = BfmeScienceWalkTest::roots("Loop");
	const std::string log = cap.stop();
	REQUIRE(first.empty());
	REQUIRE(second.empty());
	REQUIRE_EQ(log, std::string("Science prerequisite cycle: Loop -> Loop\n"));
}

TEST(science_diamond_no_cycle)
{
	BfmeScienceWalkTest::prepare();
	BfmeScienceWalkTest::node("OnlyLeft", {});
	BfmeScienceWalkTest::node("Shared", {});
	BfmeScienceWalkTest::node("OnlyRight", {});
	BfmeScienceWalkTest::node("Mid", {"Shared"});
	BfmeScienceWalkTest::node("Left", {"OnlyLeft", "Mid"});
	BfmeScienceWalkTest::node("Right", {"Mid", "OnlyRight"});
	BfmeScienceWalkTest::node("Top", {"Left", "Right"});

	StderrCapture cap;
	const std::vector<std::string> onlyLeft = BfmeScienceWalkTest::roots("OnlyLeft");
	const std::vector<std::string> mid = BfmeScienceWalkTest::roots("Mid");
	const std::vector<std::string> left = BfmeScienceWalkTest::roots("Left");
	const std::vector<std::string> right = BfmeScienceWalkTest::roots("Right");
	const std::vector<std::string> top = BfmeScienceWalkTest::roots("Top");
	const std::string log = cap.stop();
	requireNames(onlyLeft, {"OnlyLeft"});
	requireNames(mid, {"Shared"});
	requireNames(left, {"OnlyLeft", "Shared"});
	requireNames(right, {"Shared", "OnlyRight"});
	requireNames(top, {"OnlyLeft", "Shared", "OnlyRight"});
	REQUIRE(log.empty());
}
