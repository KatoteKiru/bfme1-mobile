// BFME1 Mobile — SidesList v6 parser tests
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Synthetic chunks only in the unit tests. The gamedata tests read BFME_GAMEDATA and
// skip (exit 77) when that copy is absent. Layout checked against OpenSAGE
// SidesList.Parse at 588ac477 (leading byte at version >= 6, no nested teams or
// scripts at version >= 5). That source was not copied.
#include "PreRTS.h"

#include "../../heights/harness/TestHarness.h"
#include "MapLoader.h"

#include "Common/DataChunk.h"
#include "Common/Errors.h"
#include "Common/GameType.h"
#include "Common/WellKnownKeys.h"
#include "GameLogic/SidesList.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <map>
#include <string>
#include <unistd.h>
#include <vector>

namespace bfmesides {
std::string& capturedLog();
void reset();
int& scriptParserCalls();
}

namespace {

class ByteWriter {
public:
	std::vector<uint8_t> bytes;
	void u8(uint8_t v) { bytes.push_back(v); }
	void u16(uint16_t v) { u8(uint8_t(v)); u8(uint8_t(v >> 8)); }
	void u32(uint32_t v) { u16(uint16_t(v)); u16(uint16_t(v >> 16)); }
	void i32(int32_t v) { u32(uint32_t(v)); }
	void f32(float v)
	{
		uint32_t u = 0;
		std::memcpy(&u, &v, 4);
		u32(u);
	}
	void raw(const void* p, size_t n) { bytes.insert(bytes.end(), (const uint8_t*)p, (const uint8_t*)p + n); }
	void ascii(const std::string& s)
	{
		u16(uint16_t(s.size()));
		raw(s.data(), s.size());
	}
};

// Chunk names and dict keys share one table of contents. Ids are handed out in
// first-seen order, starting at 1, which is what DataChunkInput::readDict looks up.
class Symbols {
public:
	uint32_t id(const std::string& name)
	{
		const auto it = m_ids.find(name);
		if (it != m_ids.end()) return it->second;
		const uint32_t n = uint32_t(m_order.size() + 1);
		m_order.push_back(name);
		m_ids[name] = n;
		return n;
	}
	void write(ByteWriter& f) const
	{
		f.raw("CkMp", 4);
		f.i32(int32_t(m_order.size()));
		for (size_t i = 0; i < m_order.size(); ++i) {
			f.u8(uint8_t(m_order[i].size()));
			f.raw(m_order[i].data(), m_order[i].size());
			f.u32(uint32_t(i + 1));
		}
	}

private:
	std::vector<std::string> m_order;
	std::map<std::string, uint32_t> m_ids;
};

enum { DICT_BOOL = 0, DICT_ASCII = 3 };

void dictAscii(ByteWriter& d, Symbols& sym, const std::string& key, const std::string& value)
{
	d.i32(int32_t((sym.id(key) << 8) | DICT_ASCII));
	d.ascii(value);
}

void dictBool(ByteWriter& d, Symbols& sym, const std::string& key, bool value)
{
	d.i32(int32_t((sym.id(key) << 8) | DICT_BOOL));
	d.u8(value ? 1 : 0);
}

void dictUnicode(ByteWriter& d, Symbols& sym, const std::string& key, const std::string& value)
{
	d.i32(int32_t((sym.id(key) << 8) | 4));
	d.u16(uint16_t(value.size()));
	for (unsigned char c : value) {
		d.u8(c);
		d.u8(0);
	}
}

void writeSide(ByteWriter& p, Symbols& sym, const std::string& playerName, const ByteWriter* buildOrNull)
{
	ByteWriter dict;
	dict.u16(1);
	dictAscii(dict, sym, "playerName", playerName);
	p.raw(dict.bytes.data(), dict.bytes.size());
	if (buildOrNull == nullptr) {
		p.i32(0);
		return;
	}
	p.i32(1);
	p.raw(buildOrNull->bytes.data(), buildOrNull->bytes.size());
}

// ZH v3 build-list entry. v6 uses the same fields. The parser forces z to 0.
// v6Extra inserts the byte OpenSAGE reads only for a C&C3 AssetList map.
ByteWriter buildEntry(bool v6Extra)
{
	ByteWriter b;
	b.ascii("Citadel");
	b.ascii("GondorCitadel");
	b.f32(10.5f);
	b.f32(20.25f);
	b.f32(30.0f);
	b.f32(1.5f);
	b.u8(1);
	if (v6Extra)
		b.u8(0);
	b.i32(3);
	b.ascii("OnCreate");
	b.i32(80);
	b.u8(0);
	b.u8(1);
	b.u8(0);
	return b;
}

void writeTeam(ByteWriter& p, Symbols& sym, const std::string& name, const std::string& owner)
{
	ByteWriter dict;
	dict.u16(3);
	dictAscii(dict, sym, "teamName", name);
	dictAscii(dict, sym, "teamOwner", owner);
	dictBool(dict, sym, "teamIsSingleton", true);
	p.raw(dict.bytes.data(), dict.bytes.size());
}

// Four payload bytes so the header is not the last thing in the file. openDataChunk
// returns an empty label when the read lands on EOF, and then the parser is not called.
void writeEmptyScripts(ByteWriter& p, Symbols& sym)
{
	p.u32(sym.id("PlayerScriptsList"));
	p.u16(1);
	p.i32(4);
	p.u32(0x11111111u);
}

std::vector<uint8_t> finish(Symbols& sym, int version, const ByteWriter& payload)
{
	const uint32_t chunkId = sym.id("SidesList");
	ByteWriter f;
	sym.write(f);
	f.u32(chunkId);
	f.u16(uint16_t(version));
	f.i32(int32_t(payload.bytes.size()));
	f.raw(payload.bytes.data(), payload.bytes.size());
	return f.bytes;
}

class MemoryChunkStream : public ChunkInputStream {
public:
	explicit MemoryChunkStream(const std::vector<uint8_t>& data) : m_data(data) {}
	Int read(void* pData, Int numBytes) override
	{
		Int n = numBytes < 0 ? 0 : numBytes;
		if (m_pos + size_t(n) > m_data.size()) n = Int(m_data.size() - m_pos);
		if (n > 0) std::memcpy(pData, m_data.data() + m_pos, size_t(n));
		m_pos += size_t(n);
		return n;
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
	const std::vector<uint8_t>& m_data;
	size_t m_pos = 0;
};

unsigned g_left = 0;
int g_version = -1;
int g_calls = 0;

Bool parseWrap(DataChunkInput& file, DataChunkInfo* info, void*)
{
	++g_calls;
	g_version = info->version;
	try {
		const Bool ok = SidesList::ParseSidesDataChunk(file, info, nullptr);
		g_left = file.getChunkDataSizeLeft();
		return ok;
	} catch (...) {
		g_left = file.getChunkDataSizeLeft();
		throw;
	}
}

struct ParseResult {
	bool ok = false;
	bool threw = false;
	unsigned error = 0;
	unsigned left = 0;
	int version = -1;
	int calls = 0;
};

ParseResult parseChunk(const std::vector<uint8_t>& bytes, bool resetFirst = true)
{
	if (resetFirst) bfmesides::reset();
	g_left = 0xffffffffu;
	g_version = -1;
	g_calls = 0;
	ParseResult r;
	MemoryChunkStream stream(bytes);
	DataChunkInput file(&stream);
	if (!file.isValidFileType()) return r;
	file.registerParser("SidesList", AsciiString::TheEmptyString, parseWrap, nullptr);
	try {
		r.ok = file.parse(nullptr) == true;
		r.left = g_left;
		r.version = g_version;
		r.calls = g_calls;
	} catch (ErrorCode e) {
		r.threw = true;
		r.error = unsigned(e);
		r.left = g_left;
		r.version = g_version;
		r.calls = g_calls;
	}
	return r;
}

std::string sideName(int i)
{
	return TheSidesList->getSideInfo(i)->getDict()->getAsciiString(TheKey_playerName).str();
}

std::string teamField(int i, NameKeyType key)
{
	return TheSidesList->getTeamInfo(i)->getDict()->getAsciiString(key).str();
}

void requireBuild(BuildListInfo* b)
{
	REQUIRE(b != nullptr);
	REQUIRE(b->getNext() == nullptr);
	REQUIRE_EQ(std::string(b->getBuildingName().str()), std::string("Citadel"));
	REQUIRE_EQ(std::string(b->getTemplateName().str()), std::string("GondorCitadel"));
	REQUIRE_EQ(b->getLocation()->x, 10.5f);
	REQUIRE_EQ(b->getLocation()->y, 20.25f);
	REQUIRE_EQ(b->getLocation()->z, 0.0f);
	REQUIRE_EQ(b->getAngle(), 1.5f);
	REQUIRE(b->isInitiallyBuilt());
	REQUIRE_EQ(b->getNumRebuilds(), 3);
	REQUIRE_EQ(std::string(b->getScript().str()), std::string("OnCreate"));
	REQUIRE_EQ(b->getHealth(), 80);
	REQUIRE(!b->getWhiner());
	REQUIRE(b->getUnsellable());
	REQUIRE(!b->getRepairable());
}

const char* kFordsNames[10] = {
	"Neutral", "PlyrCivilian", "SkirmishGondor", "SkirmishRohan", "SkirmishMordor",
	"SkirmishIsengard", "PlyrCreeps", "Player_1", "Player_2", "PlyrNeutral"
};

std::string env(const char* name)
{
	const char* v = std::getenv(name);
	return v ? std::string(v) : std::string();
}

std::string gamedataOrSkip()
{
	const std::string g = env("BFME_GAMEDATA");
	if (g.empty()) SKIP("BFME_GAMEDATA is not set");
	if (!std::filesystem::exists(g + "/maps.big")) SKIP("no maps.big in BFME_GAMEDATA");
	if (!std::filesystem::exists(g + "/_patch222maps.big")) SKIP("no _patch222maps.big in BFME_GAMEDATA");
	return g;
}

struct Archive {
	std::string path;
	std::vector<bfmeheights::BigEntry> maps;
};

Archive openMaps(const std::string& path)
{
	Archive a;
	a.path = path;
	std::vector<bfmeheights::BigEntry> all;
	std::string error;
	if (!bfmeheights::readBigDirectory(path, all, error)) throw bfmetest::Failure(error);
	for (const auto& e : all) {
		const std::string norm = bfmeheights::normalizeBigName(e.name);
		if (norm.size() >= 4 && norm.compare(norm.size() - 4, 4, ".map") == 0)
			a.maps.push_back(e);
	}
	return a;
}

bool isFordsMp(const std::string& norm)
{
	return norm.find("map mp fords of isen.map") != std::string::npos;
}

ParseResult parseArchiveMap(const Archive& a, const bfmeheights::BigEntry& e, std::vector<uint8_t>& plain)
{
	std::vector<uint8_t> stored;
	if (!bfmeheights::readBigEntry(a.path, e, stored))
		throw bfmetest::Failure("cannot read " + e.name);
	plain.clear();
	std::string error;
	if (!bfmeheights::decompressMap(stored, plain, error))
		throw bfmetest::Failure(e.name + ": " + error);
	SidesList::setParseMapName(e.name.c_str());
	return parseChunk(plain);
}

bool isBelfalas(const std::string& norm)
{
	return norm.find("map sps evil belfalas.map") != std::string::npos;
}

struct Cursor {
	const std::vector<uint8_t>* bytes = nullptr;
	size_t p = 0;
	bool ok = true;
	std::string err;
	bool need(size_t n)
	{
		if (!ok) return false;
		if (bytes == nullptr || p + n > bytes->size()) {
			ok = false;
			err = "past end at " + std::to_string(p);
			return false;
		}
		return true;
	}
	uint8_t u8() { if (!need(1)) return 0; return (*bytes)[p++]; }
	uint16_t u16()
	{
		if (!need(2)) return 0;
		const uint16_t v = uint16_t((*bytes)[p] | ((*bytes)[p + 1] << 8));
		p += 2;
		return v;
	}
	uint32_t u32()
	{
		if (!need(4)) return 0;
		const uint32_t v = uint32_t((*bytes)[p] | ((*bytes)[p + 1] << 8) |
			((*bytes)[p + 2] << 16) | ((*bytes)[p + 3] << 24));
		p += 4;
		return v;
	}
	int32_t i32() { return int32_t(u32()); }
	void skip(size_t n) { if (need(n)) p += n; }
	std::string ascii()
	{
		const uint16_t n = u16();
		if (!need(n)) return {};
		std::string s(reinterpret_cast<const char*>(bytes->data() + p), n);
		p += n;
		return s;
	}
};

std::string escapeText(const std::string& s)
{
	std::string out;
	for (unsigned char c : s) {
		if (c >= 32 && c < 127 && c != '\\' && c != '\'')
			out.push_back(char(c));
		else {
			char buf[8];
			std::snprintf(buf, sizeof(buf), "\\x%02x", c);
			out += buf;
		}
	}
	return out;
}

struct Toc {
	std::map<uint32_t, std::string> name;
	size_t body = 0;
	bool ok = false;
	std::string err;
};

Toc readToc(const std::vector<uint8_t>& d)
{
	Toc toc;
	if (d.size() < 8 || std::memcmp(d.data(), "CkMp", 4) != 0) {
		toc.err = "no CkMp";
		return toc;
	}
	Cursor c;
	c.bytes = &d;
	c.p = 4;
	const int32_t n = c.i32();
	if (!c.ok || n < 0 || n > 100000) {
		toc.err = "toc count";
		return toc;
	}
	for (int32_t i = 0; i < n; ++i) {
		const uint8_t len = c.u8();
		if (!c.need(size_t(len) + 4)) break;
		std::string name(reinterpret_cast<const char*>(d.data() + c.p), len);
		c.p += len;
		const uint32_t id = c.u32();
		toc.name[id] = std::move(name);
	}
	if (!c.ok) {
		toc.err = c.err;
		return toc;
	}
	toc.body = c.p;
	toc.ok = true;
	return toc;
}

bool findSides(const std::vector<uint8_t>& d, const Toc& toc, size_t& body, size_t& end, uint16_t& version)
{
	Cursor c;
	c.bytes = &d;
	c.p = toc.body;
	while (c.ok && c.p + 10 <= d.size()) {
		const uint32_t id = c.u32();
		const uint16_t ver = c.u16();
		const int32_t size = c.i32();
		if (!c.ok || size < 0) return false;
		const auto it = toc.name.find(id);
		if (it != toc.name.end() && it->second == "SidesList") {
			body = c.p;
			end = c.p + size_t(size);
			version = ver;
			return end <= d.size();
		}
		if (!c.need(size_t(size))) return false;
		c.p += size_t(size);
	}
	return false;
}

bool skipDict(Cursor& c, const Toc* toc, size_t chunkBody, bool record)
{
	const size_t dictAt = c.p;
	const uint16_t pairs = c.u16();
	if (!c.ok) return false;
	if (record)
		std::fprintf(stderr, "raw side0 dict file+%zu chunk+%zu pairs=%u\n",
			dictAt, dictAt - chunkBody, unsigned(pairs));
	for (unsigned i = 0; i < pairs; ++i) {
		const size_t at = c.p;
		const uint32_t kt = c.u32();
		const unsigned type = kt & 0xffu;
		const uint32_t id = kt >> 8;
		std::string key = "?";
		if (toc != nullptr) {
			const auto it = toc->name.find(id);
			if (it != toc->name.end()) key = it->second;
		}
		std::string value;
		if (type == 0)
			value = std::to_string(c.u8());
		else if (type == 1)
			value = std::to_string(c.i32());
		else if (type == 2) {
			const uint32_t bits = c.u32();
			float real = 0;
			std::memcpy(&real, &bits, 4);
			value = std::to_string(real);
		} else if (type == 3)
			value = c.ascii();
		else if (type == 4) {
			const uint16_t chars = c.u16();
			if (!c.need(size_t(chars) * 2)) return false;
			for (uint16_t k = 0; k < chars; ++k) {
				const unsigned unit = (*c.bytes)[c.p] | ((*c.bytes)[c.p + 1] << 8);
				c.p += 2;
				if (unit >= 32 && unit < 127)
					value.push_back(char(unit));
				else {
					char buf[8];
					std::snprintf(buf, sizeof(buf), "\\u%04x", unit);
					value += buf;
				}
			}
		} else {
			c.ok = false;
			c.err = "dict type " + std::to_string(type) + " at " + std::to_string(at);
			return false;
		}
		if (!c.ok) return false;
		if (record)
			std::fprintf(stderr, "  +%zu chunk+%zu id=%u name(%zu)='%s' type=%u value='%s'\n",
				at, at - chunkBody, id, key.size(), escapeText(key).c_str(), type, escapeText(value).c_str());
	}
	return c.ok;
}

bool skipBuild(Cursor& c, bool extraByte)
{
	c.ascii();
	c.ascii();
	c.skip(16);
	c.u8();
	if (extraByte) c.u8();
	c.i32();
	c.ascii();
	c.skip(7);
	return c.ok;
}

// How many bytes a v6 body still has after sides and build lists.
// extraByte is the byte after initiallyBuilt.
std::string layoutLeft(const std::vector<uint8_t>& d, bool extraByte)
{
	const Toc toc = readToc(d);
	if (!toc.ok) return "toc " + toc.err;
	size_t body = 0, end = 0;
	uint16_t version = 0;
	if (!findSides(d, toc, body, end, version)) return "no SidesList";
	Cursor c;
	c.bytes = &d;
	c.p = body;
	c.u8();
	const int32_t sides = c.i32();
	int builds = 0;
	if (sides < 0 || sides > 100000) return "side count";
	for (int32_t i = 0; i < sides && c.ok; ++i) {
		if (!skipDict(c, nullptr, body, false)) break;
		const int32_t n = c.i32();
		if (n < 0 || n > 100000) {
			c.ok = false;
			c.err = "build count";
			break;
		}
		builds += n;
		for (int32_t j = 0; j < n && c.ok; ++j)
			skipBuild(c, extraByte);
	}
	if (!c.ok) return c.err;
	const long left = long(end) - long(c.p);
	return "v" + std::to_string(version) + " sides=" + std::to_string(sides) +
		" builds=" + std::to_string(builds) + " left=" + std::to_string(left);
}

std::string chunkCatalog(const std::vector<uint8_t>& d)
{
	const Toc toc = readToc(d);
	if (!toc.ok) return "toc " + toc.err;
	Cursor c;
	c.bytes = &d;
	c.p = toc.body;
	std::string out;
	while (c.ok && c.p + 10 <= d.size()) {
		const uint32_t id = c.u32();
		const uint16_t ver = c.u16();
		const int32_t size = c.i32();
		const auto it = toc.name.find(id);
		const std::string name = it == toc.name.end() ? ("?" + std::to_string(id)) : it->second;
		if (name == "SidesList" || name == "Teams" || name == "PlayerScriptsList" || name == "BuildLists" ||
			name.find("Script") != std::string::npos) {
			if (!out.empty()) out += ", ";
			out += name + " v" + std::to_string(ver);
		}
		if (size < 0 || !c.need(size_t(size))) {
			out += " truncated";
			break;
		}
		c.p += size_t(size);
	}
	return out.empty() ? "no sides chunks" : out;
}

void dumpSideDiagnostics(const std::string& where, const std::vector<uint8_t>& plain)
{
	std::fprintf(stderr, "---- %s ----\n", where.c_str());
	std::fprintf(stderr, "layout v3 %s\n", layoutLeft(plain, false).c_str());
	std::fprintf(stderr, "layout extra-byte %s\n", layoutLeft(plain, true).c_str());
	std::fprintf(stderr, "chunks %s\n", chunkCatalog(plain).c_str());
	const Toc toc = readToc(plain);
	size_t body = 0, end = 0;
	uint16_t version = 0;
	if (!toc.ok || !findSides(plain, toc, body, end, version)) {
		std::fprintf(stderr, "raw side0: SidesList not found (%s)\n", toc.err.c_str());
	} else {
		Cursor c;
		c.bytes = &plain;
		c.p = body;
		const unsigned flag = c.u8();
		const int32_t sides = c.i32();
		std::fprintf(stderr, "raw header flag=%u sides=%d version=%u chunkBytes=%zu\n",
			flag, sides, unsigned(version), end - body);
		if (c.ok && sides > 0)
			skipDict(c, &toc, body, true);
		if (!c.ok)
			std::fprintf(stderr, "raw side0 walk: %s\n", c.err.c_str());
	}
	if (TheSidesList->getNumSides() < 1) {
		std::fprintf(stderr, "engine side0: no sides stored\n");
		return;
	}
	const Dict* dict = TheSidesList->getSideInfo(0)->getDict();
	std::fprintf(stderr, "engine TheKey_playerName=%u side0 pairs=%d\n",
		unsigned(NameKeyType(TheKey_playerName)), dict->getPairCount());
	for (Int n = 0; n < dict->getPairCount(); ++n) {
		const NameKeyType key = dict->getNthKey(n);
		const AsciiString name = TheNameKeyGenerator->keyToName(key);
		const int type = dict->getNthType(n);
		std::string value;
		if (type == Dict::DICT_BOOL)
			value = dict->getNthBool(n) ? "1" : "0";
		else if (type == Dict::DICT_INT)
			value = std::to_string(dict->getNthInt(n));
		else if (type == Dict::DICT_REAL)
			value = std::to_string(dict->getNthReal(n));
		else if (type == Dict::DICT_ASCIISTRING)
			value = dict->getNthAsciiString(n).str();
		else if (type == Dict::DICT_UNICODESTRING) {
			const UnicodeString text = dict->getNthUnicodeString(n);
			for (int i = 0; i < text.getLength(); ++i) {
				const unsigned unit = unsigned(text.getCharAt(i));
				if (unit >= 32 && unit < 127)
					value.push_back(char(unit));
				else {
					char buf[8];
					std::snprintf(buf, sizeof(buf), "\\u%04x", unit);
					value += buf;
				}
			}
		}
		std::fprintf(stderr, "  engine [%d] id=%u name='%s' type=%d value='%s'\n",
			n, unsigned(key), escapeText(name.str()).c_str(), type, escapeText(value).c_str());
	}
}

} // namespace

// The bytes 01 0a 00 00 00 are a leading 1 and a count of 10. Without the
// leading read, count becomes 2561 and the dict is shifted.
TEST(sides_v6_leading_byte_ten_sides)
{
	Symbols sym;
	ByteWriter payload;
	payload.u8(1);
	payload.i32(10);
	for (const char* name : kFordsNames)
		writeSide(payload, sym, name, nullptr);
	const ParseResult r = parseChunk(finish(sym, 6, payload));
	REQUIRE(r.ok);
	REQUIRE(!r.threw);
	REQUIRE_EQ(r.calls, 1);
	REQUIRE_EQ(r.version, 6);
	REQUIRE_EQ(r.left, 0u);
	REQUIRE_EQ(TheSidesList->getNumSides(), 10);
	REQUIRE_EQ(TheSidesList->getNumTeams(), 0);
	REQUIRE_EQ(bfmesides::scriptParserCalls(), 0);
	for (int i = 0; i < 10; ++i) {
		REQUIRE_EQ(sideName(i), std::string(kFordsNames[i]));
		REQUIRE(TheSidesList->getSideInfo(i)->getBuildList() == nullptr);
		REQUIRE(TheSidesList->getSideInfo(i)->getScriptList() == nullptr);
	}
}

TEST(sides_v6_build_list_v3)
{
	Symbols sym;
	const ByteWriter build = buildEntry(false);
	ByteWriter payload;
	payload.u8(1);
	payload.i32(1);
	writeSide(payload, sym, "Alpha", &build);
	const ParseResult r = parseChunk(finish(sym, 6, payload));
	REQUIRE(r.ok);
	REQUIRE_EQ(r.left, 0u);
	REQUIRE_EQ(TheSidesList->getNumSides(), 1);
	REQUIRE_EQ(TheSidesList->getNumTeams(), 0);
	REQUIRE_EQ(bfmesides::scriptParserCalls(), 0);
	REQUIRE_EQ(sideName(0), std::string("Alpha"));
	requireBuild(TheSidesList->getSideInfo(0)->getBuildList());
	REQUIRE_EQ(SidesList::lastChunkSideCount(), 1);
	REQUIRE_EQ(SidesList::lastChunkBuildCount(), 1);
}

// Fords stores the visible word on playerDisplayName. playerName stays empty.
TEST(sides_v6_display_name_stays_off_player_name)
{
	Symbols sym;
	ByteWriter payload;
	payload.u8(1);
	payload.i32(1);
	ByteWriter dict;
	dict.u16(2);
	dictAscii(dict, sym, "playerName", "");
	dictUnicode(dict, sym, "playerDisplayName", "Neutral");
	payload.raw(dict.bytes.data(), dict.bytes.size());
	payload.i32(0);
	const ParseResult r = parseChunk(finish(sym, 6, payload));
	REQUIRE(r.ok);
	REQUIRE_EQ(r.left, 0u);
	REQUIRE_EQ(sideName(0), std::string());
	Bool exists = false;
	const UnicodeString shown = TheSidesList->getSideInfo(0)->getDict()->getUnicodeString(TheKey_playerDisplayName, &exists);
	REQUIRE(exists);
	AsciiString converted;
	converted.translate(shown);
	REQUIRE_EQ(std::string(converted.str()), std::string("Neutral"));
}

// A count that cannot fit in the bytes left must throw, not loop.
TEST(sides_v6_counts_must_fit_chunk)
{
	Symbols sym;
	ByteWriter sides;
	sides.u8(1);
	sides.i32(100000000);
	const ParseResult tooManySides = parseChunk(finish(sym, 6, sides));
	REQUIRE(tooManySides.threw);
	REQUIRE_EQ(tooManySides.error, unsigned(ERROR_CORRUPT_FILE_FORMAT));

	ByteWriter builds;
	builds.u8(1);
	builds.i32(1);
	ByteWriter dict;
	dict.u16(1);
	dictAscii(dict, sym, "playerName", "Alpha");
	builds.raw(dict.bytes.data(), dict.bytes.size());
	builds.i32(100000000);
	const ParseResult tooManyBuilds = parseChunk(finish(sym, 6, builds));
	REQUIRE(tooManyBuilds.threw);
	REQUIRE_EQ(tooManyBuilds.error, unsigned(ERROR_CORRUPT_FILE_FORMAT));
}

// playerName stored as UTF-16. getAsciiString would otherwise return empty.
TEST(sides_v6_unicode_player_name)
{
	Symbols sym;
	ByteWriter payload;
	payload.u8(1);
	payload.i32(1);
	ByteWriter dict;
	dict.u16(1);
	dictUnicode(dict, sym, "playerName", "Neutral");
	payload.raw(dict.bytes.data(), dict.bytes.size());
	payload.i32(0);
	const ParseResult r = parseChunk(finish(sym, 6, payload));
	REQUIRE(r.ok);
	REQUIRE_EQ(r.left, 0u);
	REQUIRE_EQ(TheSidesList->getNumSides(), 1);
	REQUIRE_EQ(sideName(0), std::string("Neutral"));
}

// The key in the table of contents differs only by case.
TEST(sides_v6_player_name_ignores_case)
{
	Symbols sym;
	ByteWriter payload;
	payload.u8(1);
	payload.i32(1);
	ByteWriter dict;
	dict.u16(1);
	dictAscii(dict, sym, "PlayerName", "Neutral");
	payload.raw(dict.bytes.data(), dict.bytes.size());
	payload.i32(0);
	const ParseResult r = parseChunk(finish(sym, 6, payload));
	REQUIRE(r.ok);
	REQUIRE_EQ(r.left, 0u);
	REQUIRE_EQ(sideName(0), std::string("Neutral"));
}

// v3: teams and a nested PlayerScriptsList, no leading byte. The empty player
// name is the neutral side, so validateSides does not append another.
TEST(sides_v3_zh_teams_and_scripts)
{
	Symbols sym;
	const ByteWriter build = buildEntry(false);
	ByteWriter payload;
	payload.i32(1);
	writeSide(payload, sym, "", &build);
	payload.i32(1);
	writeTeam(payload, sym, "team", "");
	writeEmptyScripts(payload, sym);
	const ParseResult r = parseChunk(finish(sym, 3, payload));
	REQUIRE(r.ok);
	REQUIRE_EQ(r.left, 0u);
	REQUIRE_EQ(r.version, 3);
	REQUIRE_EQ(TheSidesList->getNumSides(), 1);
	REQUIRE_EQ(sideName(0), std::string(""));
	requireBuild(TheSidesList->getSideInfo(0)->getBuildList());
	REQUIRE_EQ(TheSidesList->getNumTeams(), 1);
	REQUIRE_EQ(teamField(0, TheKey_teamName), std::string("team"));
	REQUIRE_EQ(teamField(0, TheKey_teamOwner), std::string(""));
	REQUIRE(TheSidesList->getTeamInfo(0)->getDict()->getBool(TheKey_teamIsSingleton));
	REQUIRE_EQ(bfmesides::scriptParserCalls(), 1);
}

// v1 has no team list. validateSides still adds the default team. No leading byte.
TEST(sides_v1_zh_no_nested_teams)
{
	Symbols sym;
	ByteWriter payload;
	payload.i32(1);
	writeSide(payload, sym, "", nullptr);
	writeEmptyScripts(payload, sym);
	const ParseResult r = parseChunk(finish(sym, 1, payload));
	REQUIRE(r.ok);
	REQUIRE_EQ(r.left, 0u);
	REQUIRE_EQ(r.version, 1);
	REQUIRE_EQ(TheSidesList->getNumSides(), 1);
	REQUIRE_EQ(sideName(0), std::string(""));
	REQUIRE_EQ(TheSidesList->getNumTeams(), 1);
	REQUIRE_EQ(teamField(0, TheKey_teamName), std::string("team"));
	REQUIRE_EQ(bfmesides::scriptParserCalls(), 1);
}

// v5 is still the Zero Hour tail: no leading byte, teams and scripts inside.
TEST(sides_v5_zh_layout)
{
	Symbols sym;
	ByteWriter payload;
	payload.i32(1);
	writeSide(payload, sym, "", nullptr);
	payload.i32(1);
	writeTeam(payload, sym, "team", "");
	writeEmptyScripts(payload, sym);
	const ParseResult r = parseChunk(finish(sym, 5, payload));
	REQUIRE(r.ok);
	REQUIRE_EQ(r.left, 0u);
	REQUIRE_EQ(r.version, 5);
	REQUIRE_EQ(TheSidesList->getNumSides(), 1);
	REQUIRE_EQ(TheSidesList->getNumTeams(), 1);
	REQUIRE_EQ(teamField(0, TheKey_teamName), std::string("team"));
	REQUIRE_EQ(bfmesides::scriptParserCalls(), 1);
}

// Restores stderr if a REQUIRE fires while the pipe is installed.
struct StderrPipe {
	int saved = -1;
	int rd = -1;
	StderrPipe()
	{
		saved = dup(fileno(stderr));
		int fds[2] = {-1, -1};
		if (pipe(fds) != 0 || dup2(fds[1], fileno(stderr)) != fileno(stderr))
			throw bfmetest::Failure("stderr pipe");
		close(fds[1]);
		rd = fds[0];
	}
	std::string stop()
	{
		fflush(stderr);
		dup2(saved, fileno(stderr));
		close(saved);
		saved = -1;
		std::string err;
		char buf[512];
		ssize_t n = 0;
		while ((n = read(rd, buf, sizeof(buf))) > 0) err.append(buf, buf + n);
		close(rd);
		rd = -1;
		return err;
	}
	~StderrPipe()
	{
		if (saved >= 0) {
			fflush(stderr);
			dup2(saved, fileno(stderr));
			close(saved);
		}
		if (rd >= 0) close(rd);
	}
};

TEST(sides_version_above_6_rejected)
{
	for (int version : {7, 8, 9}) {
		Symbols sym;
		ByteWriter body;
		body.u8(1);
		body.i32(0);
		const std::vector<uint8_t> chunk = finish(sym, version, body);

		bfmesides::reset();
		Dict keep;
		keep.setAsciiString(TheKey_playerName, AsciiString("keep"));
		TheSidesList->addSide(&keep);
		bfmesides::capturedLog().clear();

		std::string err;
		ParseResult r;
		{
			StderrPipe pipe;
			r = parseChunk(chunk, false);
			err = pipe.stop();
		}

		REQUIRE(!r.ok);
		REQUIRE(r.threw);
		REQUIRE_EQ(r.error, unsigned(ERROR_CORRUPT_FILE_FORMAT));
		REQUIRE_EQ(TheSidesList->getNumSides(), 1);
		REQUIRE_EQ(sideName(0), std::string("keep"));
		const std::string needle = "unsupported version v" + std::to_string(version);
		REQUIRE(bfmesides::capturedLog().find(needle) != std::string::npos);
		REQUIRE(err.find(needle) != std::string::npos);
	}
}

// Side 17 carries a build entry. Stopping at MAX_PLAYER_COUNT would leave those bytes.
TEST(sides_v6_sides_past_cap_consumed)
{
	Symbols sym;
	const ByteWriter build = buildEntry(false);
	ByteWriter payload;
	payload.u8(1);
	payload.i32(17);
	for (int i = 0; i < 17; ++i) {
		const std::string name = (i < 10 ? "S0" : "S") + std::to_string(i);
		writeSide(payload, sym, name, i == 16 ? &build : nullptr);
	}
	const std::vector<uint8_t> chunk = finish(sym, 6, payload);
	bfmesides::reset();
	std::string err;
	ParseResult r;
	{
		StderrPipe pipe;
		r = parseChunk(chunk, false);
		err = pipe.stop();
	}
	REQUIRE(r.ok);
	REQUIRE_EQ(r.left, 0u);
	REQUIRE_EQ(TheSidesList->getNumSides(), 16);
	REQUIRE_EQ(sideName(0), std::string("S00"));
	REQUIRE_EQ(sideName(15), std::string("S15"));
	REQUIRE(err.find("SidesList: map '?' has 17 sides, stored 16, dropped 1") != std::string::npos);
}

TEST(gamedata_fords_of_isen_ten_sides)
{
	const std::string root = gamedataOrSkip();
	const char* archives[] = {"maps.big", "_patch222maps.big"};
	std::string failures;
	for (const char* archive : archives) {
		const Archive a = openMaps(root + "/" + archive);
		int found = 0;
		for (const auto& e : a.maps) {
			if (!isFordsMp(bfmeheights::normalizeBigName(e.name))) continue;
			++found;
			std::vector<uint8_t> plain;
			const ParseResult r = parseArchiveMap(a, e, plain);
			const std::string where = std::string(archive) + ":" + e.name;
			dumpSideDiagnostics(where, plain);
			if (r.threw || !r.ok || r.calls != 1 || r.version != 6 || r.left != 0 ||
				TheSidesList->getNumTeams() != 0 || bfmesides::scriptParserCalls() != 0 ||
				TheSidesList->getNumSides() != 10) {
				failures += where + " parse v" + std::to_string(r.version) + " left=" +
					std::to_string(r.left) + " sides=" + std::to_string(TheSidesList->getNumSides()) +
					" threw=" + std::to_string(r.threw) + "\n";
				continue;
			}
			const Dict* side0 = TheSidesList->getSideInfo(0)->getDict();
			Bool nameExists = false;
			const AsciiString playerName = side0->getAsciiString(TheKey_playerName, &nameExists);
			Bool displayExists = false;
			const UnicodeString display = side0->getUnicodeString(TheKey_playerDisplayName, &displayExists);
			AsciiString displayAscii;
			displayAscii.translate(display);
			if (!nameExists || !playerName.isEmpty() || !displayExists ||
				std::string(displayAscii.str()) != "Neutral" ||
				TheSidesList->getSideInfo(0)->getBuildList() != nullptr) {
				failures += where + " side 0 playerName '" + playerName.str() +
					"' exists=" + std::to_string(int(nameExists)) +
					" playerDisplayName '" + displayAscii.str() +
					"' exists=" + std::to_string(int(displayExists)) +
					" expected playerName '' and playerDisplayName 'Neutral'\n";
			}
			for (int i = 1; i < 10; ++i) {
				if (sideName(i) != kFordsNames[i] || TheSidesList->getSideInfo(i)->getBuildList() != nullptr)
					failures += where + " side " + std::to_string(i) + " name '" + sideName(i) +
						"' expected '" + kFordsNames[i] + "'\n";
			}
		}
		if (found != 1)
			failures += std::string(archive) + " Fords copies " + std::to_string(found) + "\n";
	}
	if (!failures.empty())
		throw bfmetest::Failure(failures);
}

TEST(gamedata_sideslist_all_maps)
{
	const std::string root = gamedataOrSkip();
	const struct { const char* file; int maps; } expect[] = {{"maps.big", 115}, {"_patch222maps.big", 291}};
	std::string failures;
	for (const auto& ex : expect) {
		const Archive a = openMaps(root + "/" + ex.file);
		if (int(a.maps.size()) != ex.maps)
			failures += std::string(ex.file) + " map count " + std::to_string(a.maps.size()) +
				" expected " + std::to_string(ex.maps) + "\n";
		int checked = 0;
		int nonzero = 0;
		int belfalas = 0;
		long long sideSum = 0;
		long long buildSum = 0;
		bool dumped = false;
		for (const auto& e : a.maps) {
			++checked;
			std::vector<uint8_t> plain;
			const ParseResult r = parseArchiveMap(a, e, plain);
			const int stored = TheSidesList->getNumSides();
			const int fileSides = (r.calls == 1 && !r.threw) ? SidesList::lastChunkSideCount() : 0;
			const int fileBuilds = (r.calls == 1 && !r.threw) ? SidesList::lastChunkBuildCount() : 0;
			if (r.calls == 1 && !r.threw) {
				sideSum += fileSides;
				buildSum += fileBuilds;
			}
			if (r.left != 0) ++nonzero;
			const std::string norm = bfmeheights::normalizeBigName(e.name);
			const bool bad = r.threw || !r.ok || r.calls != 1 || r.version != 6 || r.left != 0 ||
				TheSidesList->getNumTeams() != 0 || bfmesides::scriptParserCalls() != 0 ||
				stored < 1 || stored > 16 ||
				(isFordsMp(norm) && stored != 10) ||
				(fileSides > 16 && stored != 16);
			if (isBelfalas(norm)) {
				++belfalas;
				std::printf("%s: sps evil belfalas v%d left=%u sides=%d builds=%d stored=%d threw=%d\n",
					ex.file, r.version, r.left, fileSides, fileBuilds, stored, int(r.threw));
				std::fprintf(stderr, "%s: sps evil belfalas v%d left=%u sides=%d builds=%d stored=%d threw=%d\n",
					ex.file, r.version, r.left, fileSides, fileBuilds, stored, int(r.threw));
				if (r.threw || r.left != 0 || r.version != 6)
					failures += std::string(ex.file) + ":" + e.name + " belfalas left=" +
						std::to_string(r.left) + "\n";
			}
			if (!bad) continue;
			if (!dumped) {
				dumpSideDiagnostics(std::string(ex.file) + ":" + e.name, plain);
				dumped = true;
			}
			failures += std::string(ex.file) + ":" + e.name +
				" v" + std::to_string(r.version) +
				" left=" + std::to_string(r.left) +
				" sides=" + std::to_string(fileSides) +
				" builds=" + std::to_string(fileBuilds) +
				" stored=" + std::to_string(stored) +
				" threw=" + std::to_string(int(r.threw)) +
				" | v3 " + layoutLeft(plain, false) +
				" | extra-byte " + layoutLeft(plain, true) +
				" | " + chunkCatalog(plain) + "\n";
		}
		if (belfalas < 1)
			failures += std::string(ex.file) + " sps evil belfalas missing\n";
		std::printf("%s: maps %d, sides %lld, builds %lld, nonzero remainder %d\n",
			ex.file, checked, sideSum, buildSum, nonzero);
		std::fprintf(stderr, "%s: maps %d, sides %lld, builds %lld, nonzero remainder %d\n",
			ex.file, checked, sideSum, buildSum, nonzero);
		std::fflush(stdout);
		std::fflush(stderr);
	}
	if (!failures.empty())
		throw bfmetest::Failure(failures);
}
