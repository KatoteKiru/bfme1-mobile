// BFME1 Mobile — tests (E1-52)
// SPDX-License-Identifier: GPL-3.0-or-later
// Synthetic CkMp chunks for Teams, BuildLists, PlayerScriptsList and ScriptGroup v3.
// OpenSAGE is the layout oracle; this file does not contain its code.
#include "PreRTS.h"

#define INSTANTIATE_WELL_KNOWN_KEYS
#include "Common/WellKnownKeys.h"

#include "Common/DataChunk.h"
#include "Common/Dict.h"
#include "Common/Errors.h"
#include "Common/GlobalData.h"
#include "Common/MapReaderWriterInfo.h"
#include "Common/NameKeyGenerator.h"
#include "GameLogic/BfmeMapChunks.h"
#include "GameLogic/ScriptEngine.h"
#include "GameLogic/Scripts.h"
#include "GameLogic/SidesList.h"

#include "TestHarness.h"

#include "refcodex.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fstream>
#include <string>
#include <unistd.h>
#include <vector>

extern void bfmeTestPrimeScriptTemplates(NameKeyType action, NameKeyType condition);

namespace {

class MemoryChunkStream : public ChunkInputStream {
public:
	explicit MemoryChunkStream(const std::vector<uint8_t> &data) : m_data(data) {}
	Int read(void *pData, Int numBytes) override
	{
		if (numBytes < 0) numBytes = 0;
		if (m_pos + size_t(numBytes) > m_data.size()) numBytes = Int(m_data.size() - m_pos);
		if (numBytes > 0) std::memcpy(pData, m_data.data() + m_pos, size_t(numBytes));
		m_pos += size_t(numBytes);
		return numBytes;
	}
	UnsignedInt tell() override { return UnsignedInt(m_pos); }
	Bool absoluteSeek(UnsignedInt pos) override
	{
		if (pos > m_data.size()) return false;
		m_pos = pos;
		return true;
	}
	Bool eof() override { return m_pos >= m_data.size(); }

private:
	const std::vector<uint8_t> &m_data;
	size_t m_pos = 0;
};

class W {
public:
	std::vector<uint8_t> b;
	void u8(uint8_t v) { b.push_back(v); }
	void u16(uint16_t v) { u8(uint8_t(v)); u8(uint8_t(v >> 8)); }
	void u32(uint32_t v) { u16(uint16_t(v)); u16(uint16_t(v >> 16)); }
	void i32(int32_t v) { u32(uint32_t(v)); }
	void f32(float v)
	{
		uint32_t u = 0;
		std::memcpy(&u, &v, 4);
		u32(u);
	}
	void raw(const void *p, size_t n)
	{
		b.insert(b.end(), (const uint8_t *)p, (const uint8_t *)p + n);
	}
	void str(const char *s)
	{
		const auto n = std::strlen(s);
		u16(uint16_t(n));
		raw(s, n);
	}
	void append(const W &o) { raw(o.b.data(), o.b.size()); }
	void chunk(uint32_t id, uint16_t ver, const W &payload)
	{
		u32(id);
		u16(ver);
		i32(int32_t(payload.b.size()));
		append(payload);
	}
};

struct Sym { const char *name; uint32_t id; };

W toc(std::initializer_list<Sym> syms)
{
	W f;
	f.raw("CkMp", 4);
	f.i32(int32_t(syms.size()));
	for (const Sym &s : syms) {
		const auto n = std::strlen(s.name);
		f.u8(uint8_t(n));
		f.raw(s.name, n);
		f.u32(s.id);
	}
	return f;
}

void nameKey(W &w, uint32_t tocId) { w.i32(int32_t((tocId << 8) | 3)); } // DICT_ASCIISTRING

void dictAscii(W &w, uint32_t tocId, const char *value)
{
	nameKey(w, tocId);
	w.str(value);
}

W scriptV2(const char *name)
{
	W s;
	s.str(name);
	s.str("");
	s.str("");
	s.str("");
	s.u8(1); s.u8(1); s.u8(1); s.u8(1); s.u8(1); s.u8(0);
	s.i32(0);
	return s;
}

W intParm(int value)
{
	W p;
	p.i32(0); // Parameter::INT
	p.i32(value);
	p.f32(0);
	p.str("");
	return p;
}

void setup()
{
	if (!TheNameKeyGenerator) {
		TheNameKeyGenerator = new NameKeyGenerator();
		TheNameKeyGenerator->init();
	}
	if (!TheSidesList)
		TheSidesList = new SidesList();
	TheSidesList->clear();
	if (!TheScriptEngine)
		TheScriptEngine = new ScriptEngine();
	bfmeTestPrimeScriptTemplates(NAMEKEY("ZhDebugMessage"), NAMEKEY("ZhTrueCondition"));
	ScriptAction::resetBfmeSkippedCount();
	Condition::resetBfmeSkippedCount();
	ScriptGroup::resetBfmeMaxNesting();
}

W teamDict(const char *name, const char *owner)
{
	W team;
	team.u16(2);
	dictAscii(team, 2, name);
	dictAscii(team, 3, owner);
	return team;
}

W nestedGroup(int levels)
{
	W body;
	body.str("L");
	body.u8(1);
	body.u8(0);
	for (int i = 1; i < levels; ++i) {
		W outer;
		outer.str("L");
		outer.u8(1);
		outer.u8(0);
		outer.chunk(3, 3, body);
		body = outer;
	}
	return body;
}

W playerScripts(const W &lists)
{
	W file = toc({
		{"PlayerScriptsList", 1}, {"ScriptList", 2}, {"ScriptGroup", 3}, {"Script", 4},
		{"OrCondition", 5}, {"Condition", 6}, {"ScriptAction", 7}
	});
	file.chunk(1, 5, lists);
	return file;
}

Bool chunkReturnsFalse(DataChunkInput &, DataChunkInfo *, void *)
{
	return false;
}

class StderrCapture {
public:
	StderrCapture()
	{
		m_saved = ::dup(STDERR_FILENO);
		if (::pipe(m_pipe) != 0) {
			m_pipe[0] = m_pipe[1] = -1;
			return;
		}
		std::fflush(stderr);
		::dup2(m_pipe[1], STDERR_FILENO);
		::close(m_pipe[1]);
		m_pipe[1] = -1;
	}
	~StderrCapture() { restore(); }
	std::string stop()
	{
		std::fflush(stderr);
		restore();
		std::string out;
		char buf[1024];
		ssize_t n = 0;
		while ((n = ::read(m_pipe[0], buf, sizeof buf)) > 0)
			out.append(buf, buf + n);
		if (m_pipe[0] >= 0) {
			::close(m_pipe[0]);
			m_pipe[0] = -1;
		}
		return out;
	}
private:
	void restore()
	{
		if (m_saved < 0)
			return;
		std::fflush(stderr);
		::dup2(m_saved, STDERR_FILENO);
		::close(m_saved);
		m_saved = -1;
	}
	int m_saved = -1;
	int m_pipe[2] = {-1, -1};
};

W sideV6(const char *name)
{
	W side;
	side.u16(1);
	dictAscii(side, 2, name);
	side.i32(0);
	return side;
}

void addSide(const char *playerName, const char *faction)
{
	Dict d;
	d.setAsciiString(TheKey_playerName, playerName);
	if (faction)
		d.setAsciiString(TheKey_playerFaction, faction);
	TheSidesList->addSide(&d);
}

void parseMap(const std::vector<uint8_t> &bytes)
{
	MemoryChunkStream stream(bytes);
	DataChunkInput file(&stream);
	file.registerParser("SidesList", AsciiString::TheEmptyString, SidesList::ParseSidesDataChunk);
	file.registerParser("Teams", AsciiString::TheEmptyString, BfmeParseTeamsDataChunk);
	file.registerParser("BuildLists", AsciiString::TheEmptyString, BfmeParseBuildListsDataChunk);
	file.registerParser("PlayerScriptsList", AsciiString::TheEmptyString, BfmeParsePlayerScriptsListDataChunk);
	if (!file.parse(nullptr))
		throw bfmetest::Failure("DataChunkInput::parse returned false");
}

bool throwsCorrupt(const std::vector<uint8_t> &bytes)
{
	try {
		parseMap(bytes);
	} catch (ErrorCode e) {
		return e == ERROR_CORRUPT_FILE_FORMAT;
	}
	return false;
}

W buildListItem()
{
	W item;
	item.str("Citadel");
	item.str("Fortress");
	item.f32(10);
	item.f32(20);
	item.f32(30);
	item.f32(1.5f);
	item.u8(1);
	item.i32(4);
	item.str("OnBuilt");
	item.i32(80);
	item.u8(0);
	item.u8(1);
	item.u8(0);
	return item;
}

} // namespace

TEST(teams_v1_and_empty)
{
	setup();
	W emptyPayload;
	emptyPayload.i32(0);
	W emptyFile = toc({{"Teams", 1}});
	emptyFile.chunk(1, 1, emptyPayload);
	parseMap(emptyFile.b);
	REQUIRE_EQ(TheSidesList->getNumTeams(), 0);

	W team;
	team.u16(2);
	dictAscii(team, 2, "Guards");
	dictAscii(team, 3, "Men");
	W payload;
	payload.i32(1);
	payload.append(team);
	W file = toc({{"Teams", 1}, {"teamName", 2}, {"teamOwner", 3}});
	file.chunk(1, 1, payload);
	parseMap(file.b);
	REQUIRE_EQ(TheSidesList->getNumTeams(), 1);
	const AsciiString name = TheSidesList->getTeamInfo(0)->getDict()->getAsciiString(TheKey_teamName);
	const AsciiString owner = TheSidesList->getTeamInfo(0)->getDict()->getAsciiString(TheKey_teamOwner);
	REQUIRE(name == "Guards");
	REQUIRE(owner == "Men");
}

TEST(teams_v2_rejected)
{
	setup();
	W payload;
	payload.i32(0);
	W file = toc({{"Teams", 1}});
	file.chunk(1, 2, payload);
	REQUIRE(throwsCorrupt(file.b));
}

TEST(buildlists_v1_items)
{
	setup();
	addSide("Men", nullptr);
	addSide("P2", "Elves");

	W men;
	nameKey(men, 2);
	men.i32(1);
	men.append(buildListItem());
	W elves;
	nameKey(elves, 3);
	elves.i32(0);
	W orcs;
	nameKey(orcs, 4);
	orcs.i32(1);
	orcs.append(buildListItem());

	W payload;
	payload.i32(3);
	payload.append(men);
	payload.append(elves);
	payload.append(orcs);
	W file = toc({{"BuildLists", 1}, {"Men", 2}, {"Elves", 3}, {"Orcs", 4}});
	file.chunk(1, 1, payload);
	parseMap(file.b);

	BuildListInfo *menList = TheSidesList->getSideInfo(0)->getBuildList();
	REQUIRE(menList != nullptr);
	REQUIRE(menList->getBuildingName() == "Citadel");
	REQUIRE(menList->getTemplateName() == "Fortress");
	REQUIRE(menList->getLocation()->x == 10.f);
	REQUIRE(menList->getLocation()->y == 20.f);
	REQUIRE(menList->getLocation()->z == 0.f);
	REQUIRE(menList->getAngle() == 1.5f);
	REQUIRE(menList->isInitiallyBuilt());
	REQUIRE_EQ(menList->getNumRebuilds(), 4);
	REQUIRE(menList->getScript() == "OnBuilt");
	REQUIRE_EQ(menList->getHealth(), 80);
	REQUIRE(!menList->getWhiner());
	REQUIRE(menList->getUnsellable());
	REQUIRE(!menList->getRepairable());
	REQUIRE(menList->getNext() == nullptr);
	REQUIRE(TheSidesList->getSideInfo(1)->getBuildList() == nullptr);
}

TEST(buildlists_v2_rejected)
{
	setup();
	W payload;
	payload.i32(0);
	W file = toc({{"BuildLists", 1}});
	file.chunk(1, 2, payload);
	REQUIRE(throwsCorrupt(file.b));
}

TEST(script_groups_v1_v2_v3)
{
	setup();
	addSide("A", nullptr);
	addSide("B", nullptr);
	addSide("C", nullptr);

	W g1 = scriptV2("S1");
	W g1body;
	g1body.str("G1");
	g1body.u8(1);
	g1body.chunk(4, 2, g1);

	W g2 = scriptV2("S2");
	W g2body;
	g2body.str("G2");
	g2body.u8(1);
	g2body.u8(1);
	g2body.chunk(4, 2, g2);

	W innerScript = scriptV2("S3c");
	W inner;
	inner.str("G3c");
	inner.u8(1);
	inner.u8(0);
	inner.chunk(4, 2, innerScript);

	W outerScript = scriptV2("S3");
	W g3body;
	g3body.str("G3");
	g3body.u8(1);
	g3body.u8(1);
	g3body.chunk(4, 2, outerScript);
	g3body.chunk(3, 3, inner);

	W list1; list1.chunk(3, 1, g1body);
	W list2; list2.chunk(3, 2, g2body);
	W list3; list3.chunk(3, 3, g3body);
	W scripts;
	scripts.chunk(2, 1, list1);
	scripts.chunk(2, 1, list2);
	scripts.chunk(2, 1, list3);

	W file = toc({
		{"PlayerScriptsList", 1}, {"ScriptList", 2}, {"ScriptGroup", 3}, {"Script", 4}
	});
	file.chunk(1, 5, scripts);
	parseMap(file.b);

	ScriptGroup *a = TheSidesList->getSideInfo(0)->getScriptList()->getScriptGroup();
	REQUIRE(a != nullptr);
	REQUIRE(a->getName() == "G1");
	REQUIRE(a->isActive());
	REQUIRE(!a->isSubroutine());
	REQUIRE(a->getFirstChildGroup() == nullptr);
	REQUIRE(a->getScript()->getName() == "S1");

	ScriptGroup *b = TheSidesList->getSideInfo(1)->getScriptList()->getScriptGroup();
	REQUIRE(b->getName() == "G2");
	REQUIRE(b->isSubroutine());
	REQUIRE(b->getFirstChildGroup() == nullptr);
	REQUIRE(b->getScript()->getName() == "S2");

	ScriptGroup *c = TheSidesList->getSideInfo(2)->getScriptList()->getScriptGroup();
	REQUIRE(c->getName() == "G3");
	REQUIRE(c->isSubroutine());
	REQUIRE(c->getScript()->getName() == "S3");
	ScriptGroup *child = c->getFirstChildGroup();
	REQUIRE(child != nullptr);
	REQUIRE(child->getName() == "G3c");
	REQUIRE(child->isActive());
	REQUIRE(!child->isSubroutine());
	REQUIRE(child->getNext() == nullptr);
	REQUIRE(child->getScript()->getName() == "S3c");
	REQUIRE(child->getFirstChildGroup() == nullptr);
}

TEST(script_group_v4_rejected)
{
	setup();
	addSide("A", nullptr);
	W body;
	body.str("TooNew");
	body.u8(1);
	body.u8(1);
	W list;
	list.chunk(3, 4, body);
	W scripts;
	scripts.chunk(2, 1, list);
	W file = toc({{"PlayerScriptsList", 1}, {"ScriptList", 2}, {"ScriptGroup", 3}});
	file.chunk(1, 5, scripts);
	REQUIRE(throwsCorrupt(file.b));
}

TEST(script_group_depth_6_parses)
{
	setup();
	addSide("A", nullptr);
	W list;
	list.chunk(3, 3, nestedGroup(6));
	W scripts;
	scripts.chunk(2, 1, list);
	parseMap(playerScripts(scripts).b);
	REQUIRE_EQ(ScriptGroup::getBfmeMaxNesting(), 6);
	ScriptGroup *g = TheSidesList->getSideInfo(0)->getScriptList()->getScriptGroup();
	int depth = 0;
	for (; g != nullptr; g = g->getFirstChildGroup()) {
		REQUIRE(g->getName() == "L");
		++depth;
	}
	REQUIRE_EQ(depth, 6);
}

TEST(script_group_stack_limit)
{
	setup();
	addSide("A", nullptr);
	W list;
	list.chunk(3, 3, nestedGroup(65));
	W scripts;
	scripts.chunk(2, 1, list);
	REQUIRE(throwsCorrupt(playerScripts(scripts).b));
	REQUIRE(std::string(BfmeLastChunkErrorName()) == "ScriptGroup");
	REQUIRE(BfmeLastChunkErrorOffset() > 0);
}

TEST(script_lists_past_16)
{
	setup();
	for (int i = 0; i < 16; ++i)
		addSide("P", nullptr);
	W scripts;
	for (int i = 0; i < 17; ++i) {
		W list;
		if (i == 0 || i == 16) {
			W group;
			group.str(i == 0 ? "Keep" : "Drop");
			group.u8(1);
			group.u8(0);
			list.chunk(3, 2, group);
		}
		scripts.chunk(2, 1, list);
	}
	TheWritableGlobalData->m_mapName.set("map sps evil belfalas.map");
	std::string err;
	{
		StderrCapture pipe;
		parseMap(playerScripts(scripts).b);
		err = pipe.stop();
	}
	REQUIRE(err.find("PlayerScriptsList: map 'map sps evil belfalas.map' has 17 script lists, stored 16, dropped 1") != std::string::npos);
	REQUIRE(TheSidesList->getSideInfo(0)->getScriptList()->getScriptGroup()->getName() == "Keep");
	for (int i = 1; i < 16; ++i)
		REQUIRE(TheSidesList->getSideInfo(i)->getScriptList()->getScriptGroup() == nullptr);
	REQUIRE_EQ(TheSidesList->getNumSides(), 16);
}

TEST(unknown_kindof_keeps_int)
{
	setup();
	addSide("A", nullptr);
	W parm;
	parm.i32(Parameter::KIND_OF_PARAM);
	parm.i32(11);
	parm.f32(0);
	parm.str("BFME_RING_HERO");
	W cond;
	cond.i32(1);
	nameKey(cond, 10);
	cond.i32(1);
	cond.append(parm);
	W orCond;
	orCond.chunk(6, 4, cond);
	W script = scriptV2("Kind");
	script.chunk(5, 1, orCond);
	W group;
	group.str("G");
	group.u8(1);
	group.u8(0);
	group.chunk(4, 2, script);
	W list;
	list.chunk(3, 2, group);
	W scripts;
	scripts.chunk(2, 1, list);
	W file = toc({
		{"PlayerScriptsList", 1}, {"ScriptList", 2}, {"ScriptGroup", 3}, {"Script", 4},
		{"OrCondition", 5}, {"Condition", 6}, {"BfmeKindOf", 10}
	});
	file.chunk(1, 5, scripts);
	parseMap(file.b);
	Condition *parsed = TheSidesList->getSideInfo(0)->getScriptList()->getScriptGroup()->getScript()->getOrCondition()->getFirstAndCondition();
	REQUIRE(parsed != nullptr);
	REQUIRE_EQ(parsed->getNumParameters(), 1);
	REQUIRE_EQ(parsed->getParameter(0)->getInt(), 11);
	REQUIRE(parsed->getParameter(0)->getString() == "BFME_RING_HERO");
}

TEST(teams_owner_not_a_stored_side)
{
	setup();
	addSide("Men", nullptr);
	W payload;
	payload.i32(2);
	payload.append(teamDict("Guards", "Men"));
	payload.append(teamDict("Late", "Seventeenth"));
	W file = toc({{"Teams", 1}, {"teamName", 2}, {"teamOwner", 3}});
	file.chunk(1, 1, payload);
	parseMap(file.b);
	REQUIRE_EQ(TheSidesList->getNumTeams(), 1);
	REQUIRE(TheSidesList->getTeamInfo(0)->getDict()->getAsciiString(TheKey_teamName) == "Guards");

	setup();
	parseMap(file.b);
	REQUIRE_EQ(TheSidesList->getNumTeams(), 2);
}

TEST(sides_v6_then_teams_drop)
{
	setup();
	TheWritableGlobalData->m_mapName.set("map sps evil belfalas.map");
	W sides;
	sides.u8(1);
	sides.i32(17);
	for (int i = 0; i < 17; ++i) {
		char name[8];
		std::snprintf(name, sizeof name, "S%02d", i);
		sides.append(sideV6(name));
	}
	W guards;
	guards.u16(2);
	dictAscii(guards, 4, "Guards");
	dictAscii(guards, 5, "S00");
	W late;
	late.u16(2);
	dictAscii(late, 4, "Late");
	dictAscii(late, 5, "S16");
	W teams;
	teams.i32(2);
	teams.append(guards);
	teams.append(late);
	W file = toc({{"SidesList", 1}, {"playerName", 2}, {"Teams", 3}, {"teamName", 4}, {"teamOwner", 5}});
	file.chunk(1, 6, sides);
	file.chunk(3, 1, teams);
	std::string err;
	{
		StderrCapture pipe;
		parseMap(file.b);
		err = pipe.stop();
	}
	REQUIRE(err.find("SidesList: map 'map sps evil belfalas.map' has 17 sides, stored 16, dropped 1") != std::string::npos);
	REQUIRE(err.find("Teams: map 'map sps evil belfalas.map' has 2 teams, stored 1, dropped 1") != std::string::npos);
	REQUIRE_EQ(TheSidesList->getNumSides(), 16);
	REQUIRE_EQ(TheSidesList->getNumTeams(), 1);
	REQUIRE(TheSidesList->getTeamInfo(0)->getDict()->getAsciiString(TheKey_teamOwner) == "S00");
	REQUIRE_EQ(BfmeDroppedTeamRecords(), 1);
}

// Retail v5 (camps, others, ai_debug) ends on the last side. The next bytes are
// another chunk, not a Zero Hour team count. A v5 chunk that still contains the
// team count keeps that tail (sides_v5_zh_layout).
TEST(sides_v5_ends_on_last_side)
{
	setup();
	W side0;
	side0.u16(1);
	dictAscii(side0, 2, "Gondor");
	side0.i32(2);
	side0.append(buildListItem());
	side0.append(buildListItem());
	W side1;
	side1.u16(1);
	dictAscii(side1, 2, "Mordor");
	side1.i32(0);
	W sides;
	sides.i32(2);
	sides.append(side0);
	sides.append(side1);
	W guards;
	guards.u16(2);
	dictAscii(guards, 4, "Guards");
	dictAscii(guards, 5, "Gondor");
	W teams;
	teams.i32(1);
	teams.append(guards);
	W file = toc({{"SidesList", 1}, {"playerName", 2}, {"Teams", 3}, {"teamName", 4}, {"teamOwner", 5}});
	file.chunk(1, 5, sides);
	file.chunk(3, 1, teams);
	parseMap(file.b);
	REQUIRE_EQ(TheSidesList->getNumSides(), 2);
	REQUIRE(TheSidesList->getSideInfo(0)->getDict()->getAsciiString(TheKey_playerName) == "Gondor");
	REQUIRE(TheSidesList->getSideInfo(1)->getDict()->getAsciiString(TheKey_playerName) == "Mordor");
	BuildListInfo *list = TheSidesList->getSideInfo(0)->getBuildList();
	REQUIRE(list != nullptr);
	REQUIRE(list->getBuildingName() == "Citadel");
	REQUIRE(list->getNext() != nullptr);
	REQUIRE(list->getNext()->getNext() == nullptr);
	REQUIRE(TheSidesList->getSideInfo(1)->getBuildList() == nullptr);
	REQUIRE_EQ(SidesList::lastChunkBuildCount(), 2);
	REQUIRE_EQ(TheSidesList->getNumTeams(), 1);
	REQUIRE(TheSidesList->getTeamInfo(0)->getDict()->getAsciiString(TheKey_teamOwner) == "Gondor");
}

TEST(sides_trace_on_short_build_count)
{
	setup();
	W sides;
	sides.u8(0);
	sides.i32(1);
	sides.u16(0);
	sides.i32(4);
	W file = toc({{"SidesList", 1}});
	file.chunk(1, 6, sides);
	for (int i = 0; i < 32; ++i)
		file.u8(0xab);
	std::string err;
	bool threw = false;
	{
		StderrCapture pipe;
		threw = throwsCorrupt(file.b);
		err = pipe.stop();
	}
	REQUIRE(threw);
	REQUIRE(err.find("SidesList trace: version 6 sides 1 phase build-count sideIndex 0") != std::string::npos);
	REQUIRE(err.find("SidesList side 0 offset ") != std::string::npos);
	REQUIRE(err.find("builds 4") != std::string::npos);
	REQUIRE(err.find("SidesList hex ab ab ab ab") != std::string::npos);
}

TEST(chunk_error_reports_name_and_offset)
{
	setup();
	BfmeResetChunkErrorSite();
	W payload;
	payload.u8(1);
	W file = toc({{"Boom", 1}});
	file.chunk(1, 1, payload);
	MemoryChunkStream stream(file.b);
	DataChunkInput in(&stream);
	in.registerParser("Boom", AsciiString::TheEmptyString, chunkReturnsFalse);
	REQUIRE(!in.parse(nullptr));
	REQUIRE(std::string(BfmeLastChunkErrorName()) == "Boom");
	REQUIRE_EQ(int(BfmeLastChunkErrorOffset()), 27);

	BfmeResetChunkErrorSite();
	addSide("A", nullptr);
	W body;
	body.str("TooNew");
	body.u8(1);
	body.u8(1);
	W list;
	list.chunk(3, 4, body);
	W scripts;
	scripts.chunk(2, 1, list);
	W bad = playerScripts(scripts);
	REQUIRE(throwsCorrupt(bad.b));
	REQUIRE(std::string(BfmeLastChunkErrorName()) == "ScriptGroup");
	REQUIRE(BfmeLastChunkErrorOffset() > 0);
	REQUIRE(BfmeLastChunkErrorOffset() < bad.b.size());
}

TEST(bfme_only_action_and_condition)
{
	setup();
	addSide("A", nullptr);

	W unknownCond;
	unknownCond.i32(7);
	nameKey(unknownCond, 10); // BfmeHasRing
	unknownCond.i32(1);
	unknownCond.append(intParm(42));
	W knownCond;
	knownCond.i32(0);
	nameKey(knownCond, 8); // ZhTrueCondition
	knownCond.i32(0);
	W orCond;
	orCond.chunk(6, 4, unknownCond);
	orCond.chunk(6, 4, knownCond);

	W unknownAct;
	unknownAct.i32(9);
	nameKey(unknownAct, 11); // BfmeSummonBalrog
	unknownAct.i32(1);
	unknownAct.append(intParm(7));
	W knownAct;
	knownAct.i32(0);
	nameKey(knownAct, 9); // ZhDebugMessage, template index 0
	knownAct.i32(0);

	W script = scriptV2("HasRing");
	script.chunk(5, 1, orCond);
	script.chunk(7, 2, unknownAct);
	script.chunk(7, 2, knownAct);

	W group;
	group.str("G");
	group.u8(1);
	group.u8(0);
	group.chunk(4, 2, script);
	W list;
	list.chunk(3, 2, group);
	W scripts;
	scripts.chunk(2, 1, list);
	W file = toc({
		{"PlayerScriptsList", 1}, {"ScriptList", 2}, {"ScriptGroup", 3}, {"Script", 4},
		{"OrCondition", 5}, {"Condition", 6}, {"ScriptAction", 7},
		{"ZhTrueCondition", 8}, {"ZhDebugMessage", 9}, {"BfmeHasRing", 10}, {"BfmeSummonBalrog", 11}
	});
	file.chunk(1, 5, scripts);
	parseMap(file.b);

	REQUIRE_EQ(Condition::getBfmeSkippedCount(), 1);
	REQUIRE_EQ(ScriptAction::getBfmeSkippedCount(), 1);
	Script *parsed = TheSidesList->getSideInfo(0)->getScriptList()->getScriptGroup()->getScript();
	Condition *cond = parsed->getOrCondition()->getFirstAndCondition();
	REQUIRE(cond->isBfmeUnsupported());
	REQUIRE(cond->getConditionType() == Condition::CONDITION_FALSE);
	REQUIRE_EQ(cond->getNumParameters(), 1);
	REQUIRE_EQ(cond->getParameter(0)->getInt(), 42);
	Condition *known = cond->getNext();
	REQUIRE(known != nullptr);
	REQUIRE(!known->isBfmeUnsupported());
	REQUIRE(known->getConditionType() == Condition::CONDITION_FALSE || known->getConditionType() == Condition::ConditionType(0));
	REQUIRE_EQ(known->getNumParameters(), 0);

	ScriptAction *act = parsed->getAction();
	REQUIRE(act->isBfmeUnsupported());
	REQUIRE(act->getActionType() == ScriptAction::NO_OP);
	REQUIRE_EQ(act->getNumParameters(), 1);
	REQUIRE_EQ(act->getParameter(0)->getInt(), 7);
	ScriptAction *knownAction = act->getNext();
	REQUIRE(knownAction != nullptr);
	REQUIRE(!knownAction->isBfmeUnsupported());
	// Type 0 matches the primed template and stays DEBUG_MESSAGE_BOX. NO_OP is a later enumerator.
	REQUIRE(knownAction->getActionType() == ScriptAction::DEBUG_MESSAGE_BOX);
	REQUIRE_EQ(knownAction->getNumParameters(), 0);
}

TEST(newer_script_versions_rejected)
{
	setup();
	addSide("A", nullptr);

	{
		// A zero-length chunk at end of file is treated as EOF before its parser runs.
		W body;
		body.u8(0);
		W file = toc({{"PlayerScriptsList", 1}});
		file.chunk(1, 6, body);
		REQUIRE(throwsCorrupt(file.b));
	}
	{
		W list;
		list.u8(0);
		W scripts;
		scripts.chunk(2, 2, list);
		W file = toc({{"PlayerScriptsList", 1}, {"ScriptList", 2}});
		file.chunk(1, 5, scripts);
		REQUIRE(throwsCorrupt(file.b));
	}
	{
		W script;
		script.str("X");
		W group;
		group.str("G");
		group.u8(1);
		group.chunk(4, 3, script);
		W list;
		list.chunk(3, 1, group);
		W scripts;
		scripts.chunk(2, 1, list);
		W file = toc({{"PlayerScriptsList", 1}, {"ScriptList", 2}, {"ScriptGroup", 3}, {"Script", 4}});
		file.chunk(1, 5, scripts);
		REQUIRE(throwsCorrupt(file.b));
	}
	{
		W cond;
		cond.i32(0);
		W orc;
		orc.chunk(6, 5, cond);
		W script = scriptV2("X");
		script.chunk(5, 1, orc); // OrCondition v1, so the reject is Condition v5
		W group;
		group.str("G");
		group.u8(1);
		group.chunk(4, 2, script);
		W list;
		list.chunk(3, 1, group);
		W scripts;
		scripts.chunk(2, 1, list);
		W file = toc({
			{"PlayerScriptsList", 1}, {"ScriptList", 2}, {"ScriptGroup", 3}, {"Script", 4},
			{"OrCondition", 5}, {"Condition", 6}
		});
		file.chunk(1, 5, scripts);
		REQUIRE(throwsCorrupt(file.b));
	}
	{
		W act;
		act.i32(0);
		W script = scriptV2("X");
		script.chunk(5, 3, act);
		W group;
		group.str("G");
		group.u8(1);
		group.chunk(4, 2, script);
		W list;
		list.chunk(3, 1, group);
		W scripts;
		scripts.chunk(2, 1, list);
		W file = toc({
			{"PlayerScriptsList", 1}, {"ScriptList", 2}, {"ScriptGroup", 3}, {"Script", 4},
			{"ScriptAction", 5}
		});
		file.chunk(1, 5, scripts);
		REQUIRE(throwsCorrupt(file.b));
	}
}

namespace {

bool endsWithMap(const std::string &name)
{
	if (name.size() < 4) return false;
	const char *e = name.c_str() + name.size() - 4;
	return e[0] == '.' && (e[1] == 'm' || e[1] == 'M') && (e[2] == 'a' || e[2] == 'A')
		&& (e[3] == 'p' || e[3] == 'P');
}

uint32_t be32(const uint8_t *p)
{
	return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | uint32_t(p[3]);
}

struct BigEntry { std::string name; uint32_t offset = 0, size = 0; };

bool readBigDirectory(const std::string &path, std::vector<BigEntry> &entries)
{
	std::ifstream f(path, std::ios::binary);
	if (!f) return false;
	uint8_t head[16];
	if (!f.read((char *)head, 16)) return false;
	if (std::memcmp(head, "BIG4", 4) != 0 && std::memcmp(head, "BIGF", 4) != 0) return false;
	const uint32_t count = be32(head + 8), headerLen = be32(head + 12);
	if (headerLen < 16 || headerLen > (64u << 20)) return false;
	std::vector<uint8_t> h(headerLen);
	std::memcpy(h.data(), head, 16);
	if (!f.read((char *)h.data() + 16, headerLen - 16)) return false;
	size_t p = 16;
	for (uint32_t i = 0; i < count; ++i) {
		if (p + 9 > h.size()) return false;
		BigEntry e;
		e.offset = be32(&h[p]);
		e.size = be32(&h[p + 4]);
		p += 8;
		while (p < h.size() && h[p] != 0) e.name += char(h[p++]);
		++p;
		entries.push_back(e);
	}
	return true;
}

bool readBigEntry(std::ifstream &f, const BigEntry &e, std::vector<uint8_t> &out)
{
	f.clear();
	f.seekg(std::streamoff(e.offset));
	out.resize(e.size);
	if (e.size == 0) return true;
	return bool(f.read((char *)out.data(), std::streamsize(e.size)));
}

bool decompressMap(const std::vector<uint8_t> &in, std::vector<uint8_t> &out)
{
	if (in.size() >= 8 && std::memcmp(in.data(), "EAR\0", 4) == 0) {
		const uint32_t size = uint32_t(in[4]) | uint32_t(in[5]) << 8 | uint32_t(in[6]) << 16 | uint32_t(in[7]) << 24;
		if (!REF_is(in.data() + 8)) return false;
		out.assign(size_t(size) + 16, 0);
		int compressed = 0;
		const int got = REF_decode(out.data(), in.data() + 8, &compressed);
		if (got != int(size)) return false;
		out.resize(size);
		return true;
	}
	out = in;
	return true;
}

void parseIsolated(const std::vector<uint8_t> &plain)
{
	MemoryChunkStream stream(plain);
	DataChunkInput file(&stream);
	file.registerParser("SidesList", AsciiString::TheEmptyString, SidesList::ParseSidesDataChunk);
	file.registerParser("Teams", AsciiString::TheEmptyString, BfmeParseTeamsDataChunk);
	file.registerParser("PlayerScriptsList", AsciiString::TheEmptyString, BfmeParsePlayerScriptsListDataChunk);
	file.registerParser("BuildLists", AsciiString::TheEmptyString, BfmeParseBuildListsDataChunk);
	if (!file.parse(nullptr))
		throw bfmetest::Failure("parse returned false");
}

std::string baseName(const std::string &path)
{
	const auto slash = path.find_last_of('/');
	return slash == std::string::npos ? path : path.substr(slash + 1);
}

int archiveRank(const std::string &base)
{
	if (base == "maps.big") return 0;
	if (base == "_patch222maps.big") return 1;
	return 2;
}

struct ArchiveReport {
	std::string name;
	int maps = 0;
	int teams = 0;
	int scripts = 0;
	int buildRecords = 0;
	int unknownActions = 0;
	int unknownConditions = 0;
	int groupDepth = 0;
	int droppedTeams = 0;
	double seconds = 0;
	std::vector<std::string> remainderMaps;
	std::vector<std::string> errors;
	std::vector<std::string> deepMaps;
};

void printArchive(const ArchiveReport &r)
{
	std::printf("[bfme] E1-52 %s: maps %d teams %d droppedTeams %d scripts %d buildRecords %d unknownActions %d unknownConditions %d groupDepth %d remainder %zu seconds %.2f\n",
		r.name.c_str(), r.maps, r.teams, r.droppedTeams, r.scripts, r.buildRecords,
		r.unknownActions, r.unknownConditions, r.groupDepth, r.remainderMaps.size(), r.seconds);
	for (const std::string &deep : r.deepMaps)
		std::printf("[bfme] E1-52 %s groupDepth: %s\n", r.name.c_str(), deep.c_str());
	if (r.remainderMaps.empty())
		std::printf("[bfme] E1-52 %s remainder: (none)\n", r.name.c_str());
	for (const std::string &map : r.remainderMaps)
		std::printf("[bfme] E1-52 %s remainder: %s\n", r.name.c_str(), map.c_str());
	for (const std::string &err : r.errors)
		std::printf("[bfme] E1-52 %s error: %s\n", r.name.c_str(), err.c_str());
	std::fflush(stdout);
}

std::string hexAt(const std::vector<uint8_t> &plain, UnsignedInt offset)
{
	std::string s = " hex";
	if (offset >= plain.size())
		return s + " (none)";
	const size_t n = std::min(size_t(32), plain.size() - size_t(offset));
	char buf[4];
	for (size_t i = 0; i < n; ++i) {
		std::snprintf(buf, sizeof buf, " %02x", plain[size_t(offset) + i]);
		s += buf;
	}
	return s;
}

std::string chunkSite()
{
	const char *name = BfmeLastChunkErrorName();
	if (name == nullptr || name[0] == '\0')
		name = "(none)";
	return std::string("chunk ") + name + " offset " + std::to_string(BfmeLastChunkErrorOffset());
}

void noteGroupDepth(ArchiveReport &report, const std::string &label)
{
	const int depth = ScriptGroup::getBfmeMaxNesting();
	if (depth > report.groupDepth)
		report.groupDepth = depth;
	if (depth > 5)
		report.deepMaps.push_back(label + " groupDepth " + std::to_string(depth));
}

void scanMap(ArchiveReport &report, const std::string &label, const std::vector<uint8_t> &raw)
{
	std::vector<uint8_t> plain;
	if (!decompressMap(raw, plain)) {
		report.errors.push_back(label + ": decompress failed chunk (none) offset 0");
		return;
	}
	if (plain.size() < 4 || std::memcmp(plain.data(), "CkMp", 4) != 0)
		return;
	TheSidesList->clear();
	if (TheWritableGlobalData != nullptr)
		TheWritableGlobalData->m_mapName.set(label.c_str());
	BfmeResetChunkRemainder();
	BfmeResetChunkErrorSite();
	ScriptGroup::resetBfmeMaxNesting();
	try {
		parseIsolated(plain);
	} catch (ErrorCode e) {
		std::string site = chunkSite();
		if (std::string(BfmeLastChunkErrorName()) == "SidesList")
			site += hexAt(plain, BfmeLastChunkErrorOffset());
		if (BfmeLastChunkRemainder() != 0) {
			report.remainderMaps.push_back(label + " " + BfmeLastChunkRemainderName()
				+ " left " + std::to_string(BfmeLastChunkRemainder()) + " " + site);
		} else {
			report.errors.push_back(label + " ErrorCode " + std::to_string(unsigned(e)) + " " + site);
		}
	} catch (const bfmetest::Failure &f) {
		report.errors.push_back(label + ": " + f.what() + " " + chunkSite());
	}
	noteGroupDepth(report, label);
	++report.maps;
}

void finishArchive(ArchiveReport &report, std::chrono::steady_clock::time_point started)
{
	report.teams = BfmeParsedTeamRecords();
	report.droppedTeams = BfmeDroppedTeamRecords();
	report.scripts = BfmeParsedScriptRecords();
	report.buildRecords = BfmeParsedBuildRecords();
	report.unknownActions = ScriptAction::getBfmeSkippedCount();
	report.unknownConditions = Condition::getBfmeSkippedCount();
	report.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
	printArchive(report);
}

} // namespace

TEST(gamedata_chunk_remainder)
{
	const char *root = std::getenv("BFME_GAMEDATA");
	if (root == nullptr || root[0] == '\0')
		SKIP("BFME_GAMEDATA is not set");
	DIR *dir = opendir(root);
	if (!dir)
		SKIP("BFME_GAMEDATA is not a directory");

	setup();
	std::vector<std::string> bigs;
	std::vector<std::string> loose;
	while (dirent *ent = readdir(dir)) {
		const std::string name = ent->d_name;
		if (name.size() >= 4 && name.compare(name.size() - 4, 4, ".big") == 0)
			bigs.push_back(std::string(root) + "/" + name);
		else if (endsWithMap(name))
			loose.push_back(std::string(root) + "/" + name);
	}
	closedir(dir);
	std::sort(bigs.begin(), bigs.end(), [](const std::string &a, const std::string &b) {
		const std::string ba = baseName(a), bb = baseName(b);
		if (archiveRank(ba) != archiveRank(bb)) return archiveRank(ba) < archiveRank(bb);
		return ba < bb;
	});

	int maps = 0, teams = 0, scripts = 0, builds = 0, remainder = 0, errors = 0;
	for (const std::string &big : bigs) {
		BfmeResetParseCounts();
		ScriptAction::resetBfmeSkippedCount();
		Condition::resetBfmeSkippedCount();
		ScriptGroup::resetBfmeMaxNesting();
		const auto started = std::chrono::steady_clock::now();
		ArchiveReport report;
		report.name = baseName(big);
		std::ifstream in(big, std::ios::binary);
		std::vector<BigEntry> entries;
		if (!in || !readBigDirectory(big, entries)) {
			report.errors.push_back("BIG directory " + big + " chunk (none) offset 0");
		} else {
			for (const BigEntry &e : entries) {
				if (!endsWithMap(e.name)) continue;
				std::vector<uint8_t> raw;
				if (!readBigEntry(in, e, raw))
					report.errors.push_back(e.name + ": short BIG entry chunk (none) offset 0");
				else
					scanMap(report, e.name, raw);
			}
		}
		finishArchive(report, started);
		maps += report.maps;
		teams += report.teams;
		scripts += report.scripts;
		builds += report.buildRecords;
		remainder += int(report.remainderMaps.size());
		errors += int(report.errors.size());
	}
	if (!loose.empty()) {
		BfmeResetParseCounts();
		ScriptAction::resetBfmeSkippedCount();
		Condition::resetBfmeSkippedCount();
		ScriptGroup::resetBfmeMaxNesting();
		const auto started = std::chrono::steady_clock::now();
		ArchiveReport report;
		report.name = "loose";
		for (const std::string &path : loose) {
			std::ifstream f(path, std::ios::binary);
			std::vector<uint8_t> raw((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
			scanMap(report, path, raw);
		}
		finishArchive(report, started);
		maps += report.maps;
		teams += report.teams;
		scripts += report.scripts;
		builds += report.buildRecords;
		remainder += int(report.remainderMaps.size());
		errors += int(report.errors.size());
	}
	if (maps == 0)
		SKIP("BFME_GAMEDATA has no .map files");
	REQUIRE(teams > 0);
	REQUIRE(scripts > 0);
	REQUIRE(builds > 0);
	REQUIRE(remainder == 0);
	REQUIRE(errors == 0);
}
