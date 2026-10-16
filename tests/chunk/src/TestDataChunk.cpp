// BFME1 Mobile — skipped map chunks and version ceilings (audit A5).
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Synthetic chunk files only. OpenSAGE was not used. Ceilings are the maxima
// from a scan of every map in maps.big and _patch222maps.big. PolygonTriggers
// uses K_TRIGGERS_VERSION_5 and K_SIDES_DATA_VERSION_6.
#include "PreRTS.h"

#include "../../heights/harness/TestHarness.h"

#include "Common/CriticalSection.h"
#include "Common/DataChunk.h"
#include "Common/Errors.h"
#include "Common/GameMemory.h"
#include "Common/MapReaderWriterInfo.h"

#include <cstdint>
#include <cstring>
#include <string>
#include <unistd.h>
#include <vector>

CriticalSection* TheAsciiStringCriticalSection = nullptr;

namespace {

struct InitMemory {
	InitMemory() { initMemoryManager(); }
};
InitMemory g_initMemory;

class ByteWriter {
public:
	std::vector<uint8_t> bytes;
	void u8(uint8_t v) { bytes.push_back(v); }
	void u16(uint16_t v) { u8(uint8_t(v)); u8(uint8_t(v >> 8)); }
	void u32(uint32_t v) { u16(uint16_t(v)); u16(uint16_t(v >> 16)); }
	void i32(int32_t v) { u32(uint32_t(v)); }
	void raw(const void* p, size_t n)
	{
		bytes.insert(bytes.end(), (const uint8_t*)p, (const uint8_t*)p + n);
	}
};

struct Symbol {
	const char* name;
	uint32_t id;
};

void writeChunk(ByteWriter& b, uint32_t id, uint16_t version, const std::vector<uint8_t>& payload)
{
	b.u32(id);
	b.u16(version);
	b.i32(int32_t(payload.size()));
	if (!payload.empty()) b.raw(payload.data(), payload.size());
}

std::vector<uint8_t> writeFile(const std::vector<Symbol>& symbols, const std::vector<uint8_t>& body)
{
	ByteWriter f;
	f.raw("CkMp", 4);
	f.i32(int32_t(symbols.size()));
	for (const Symbol& s : symbols) {
		const size_t n = std::strlen(s.name);
		f.u8(uint8_t(n));
		f.raw(s.name, n);
		f.u32(s.id);
	}
	f.raw(body.data(), body.size());
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

class StderrCapture {
public:
	StderrCapture()
	{
		std::fflush(stderr);
		m_old = ::dup(fileno(stderr));
		m_file = std::tmpfile();
		::dup2(fileno(m_file), fileno(stderr));
	}
	~StderrCapture()
	{
		std::fflush(stderr);
		if (m_old >= 0) {
			::dup2(m_old, fileno(stderr));
			::close(m_old);
		}
		if (m_file) std::fclose(m_file);
	}
	std::string text()
	{
		std::fflush(stderr);
		std::rewind(m_file);
		std::string s;
		char buf[256];
		while (std::fgets(buf, sizeof(buf), m_file)) s += buf;
		return s;
	}
	StderrCapture(const StderrCapture&) = delete;
	StderrCapture& operator=(const StderrCapture&) = delete;

private:
	int m_old = -1;
	FILE* m_file = nullptr;
};

struct Hit {
	int calls = 0;
	int version = -1;
	int size = -1;
};

Bool noteHit(DataChunkInput&, DataChunkInfo* info, void* userData)
{
	Hit* hit = (Hit*)userData;
	hit->calls++;
	hit->version = info->version;
	hit->size = int(info->dataSize);
	return true;
}

Bool parseChildren(DataChunkInput& file, DataChunkInfo*, void* userData)
{
	Hit* hit = (Hit*)userData;
	hit->calls++;
	return file.parse(userData);
}

struct Outcome {
	bool ok = false;
	bool threw = false;
	unsigned error = 0;
	Hit hit;
	std::string log;
};

Outcome parseWith(const std::vector<uint8_t>& bytes, DataChunkParserPtr parser, const char* label,
	const char* parent, DataChunkVersionType maxVersion, bool withCeiling)
{
	Outcome o;
	StderrCapture cap;
	try {
		MemoryChunkStream stream(bytes);
		DataChunkInput file(&stream);
		REQUIRE(file.isValidFileType());
		if (parser) {
			if (withCeiling)
				file.registerParser(label, parent ? AsciiString(parent) : AsciiString::TheEmptyString, parser, &o.hit, maxVersion);
			else
				file.registerParser(label, parent ? AsciiString(parent) : AsciiString::TheEmptyString, parser, &o.hit);
		}
		o.ok = file.parse(&o.hit) == true;
	} catch (ErrorCode e) {
		o.threw = true;
		o.error = unsigned(e);
	}
	o.log = cap.text();
	return o;
}

std::vector<uint8_t> oneChunk(const char* name, uint32_t id, uint16_t version, const std::vector<uint8_t>& payload)
{
	ByteWriter body;
	writeChunk(body, id, version, payload);
	return writeFile({{name, id}}, body.bytes);
}

} // namespace

TEST(chunk_skip_logs_each_chunk)
{
	const std::vector<uint8_t> payload = {0x11, 0x22, 0x33, 0x44};
	ByteWriter body;
	writeChunk(body, 1, 1, payload);
	writeChunk(body, 2, 3, {0xAB});
	const Outcome o = parseWith(writeFile({{"Nope", 1}, {"AlsoNope", 2}}, body.bytes), nullptr, nullptr, nullptr, 0, false);
	REQUIRE(o.ok);
	REQUIRE(!o.threw);
	REQUIRE(o.log.find("[chunk] skipped Nope v1 size 4 count 1\n") != std::string::npos);
	REQUIRE(o.log.find("[chunk] skipped AlsoNope v3 size 1 count 2\n") != std::string::npos);
	REQUIRE(o.log.find("known") == std::string::npos);
}

TEST(chunk_skip_count_is_per_map)
{
	const Outcome a = parseWith(oneChunk("Nope", 1, 1, {1, 2, 3, 4}), nullptr, nullptr, nullptr, 0, false);
	const Outcome b = parseWith(oneChunk("AlsoNope", 7, 1, {9}), nullptr, nullptr, nullptr, 0, false);
	REQUIRE(a.log.find("Nope v1 size 4 count 1\n") != std::string::npos);
	REQUIRE(a.log.find("count 2") == std::string::npos);
	REQUIRE(b.log.find("AlsoNope v1 size 1 count 1\n") != std::string::npos);
	REQUIRE(b.log.find("count 2") == std::string::npos);
}

TEST(chunk_skip_count_resets)
{
	const std::vector<uint8_t> bytes = oneChunk("Nope", 1, 1, {1, 2});
	StderrCapture cap;
	MemoryChunkStream stream(bytes);
	DataChunkInput file(&stream);
	REQUIRE(file.parse(nullptr));
	file.reset();
	REQUIRE(file.parse(nullptr));
	const std::string log = cap.text();
	const size_t first = log.find("[chunk] skipped Nope v1 size 2 count 1\n");
	REQUIRE(first != std::string::npos);
	REQUIRE(log.find("[chunk] skipped Nope v1 size 2 count 1\n", first + 1) != std::string::npos);
	REQUIRE(log.find("count 2") == std::string::npos);
}

TEST(chunk_known_skip_is_one_summary)
{
	// Teams, BuildLists and SkyboxSettings are in mapChunkVersionCeiling. A partial
	// pass does not register them; a line each was 39 lines on rohan. Nope is not
	// in the table, so it stays a line, and its count is not the known skips.
	ByteWriter body;
	writeChunk(body, 1, 1, {0x11, 0x22, 0x33, 0x44});
	writeChunk(body, 2, 3, {0xAB});
	writeChunk(body, 3, 1, {0x10, 0x20});
	writeChunk(body, 4, 1, {9});
	const Outcome o = parseWith(writeFile({
		{"Teams", 1}, {"BuildLists", 2}, {"Nope", 3}, {"SkyboxSettings", 4}
	}, body.bytes), nullptr, nullptr, nullptr, 0, false);
	REQUIRE(o.ok);
	REQUIRE(!o.threw);
	REQUIRE(o.log.find("[chunk] skipped Teams") == std::string::npos);
	REQUIRE(o.log.find("[chunk] skipped BuildLists") == std::string::npos);
	REQUIRE(o.log.find("[chunk] skipped SkyboxSettings") == std::string::npos);
	REQUIRE(o.log.find("[chunk] skipped Nope v1 size 2 count 1\n") != std::string::npos);
	REQUIRE(o.log.find("count 2") == std::string::npos);
	REQUIRE(o.log.find("[chunk] skipped 3 known\n") != std::string::npos);
	REQUIRE(o.log.find("[chunk] skipped 4 known") == std::string::npos);

	const std::vector<uint8_t> again = oneChunk("Teams", 1, 1, {1});
	StderrCapture cap;
	MemoryChunkStream stream(again);
	DataChunkInput file(&stream);
	REQUIRE(file.parse(nullptr));
	file.reset();
	REQUIRE(file.parse(nullptr));
	const std::string log = cap.text();
	REQUIRE(log.find("[chunk] skipped Teams") == std::string::npos);
	const size_t first = log.find("[chunk] skipped 1 known\n");
	REQUIRE(first != std::string::npos);
	REQUIRE(log.find("[chunk] skipped 1 known\n", first + 1) != std::string::npos);
	REQUIRE(log.find("[chunk] skipped 2 known") == std::string::npos);
}

TEST(chunk_version_at_ceiling_is_parsed)
{
	const DataChunkVersionType maxVersion = DataChunkInput::mapChunkVersionCeiling("SidesList");
	const Outcome o = parseWith(oneChunk("SidesList", 1, maxVersion, {1, 2, 3, 4}), noteHit, "SidesList", nullptr, maxVersion, true);
	REQUIRE(o.ok);
	REQUIRE(!o.threw);
	REQUIRE_EQ(o.hit.calls, 1);
	REQUIRE_EQ(o.hit.version, int(maxVersion));
	REQUIRE_EQ(o.hit.size, 4);
	REQUIRE(o.log.find("newer than known") == std::string::npos);
	REQUIRE(o.log.find("skipped") == std::string::npos);
}

TEST(chunk_older_version_is_parsed)
{
	const DataChunkVersionType maxVersion = DataChunkInput::mapChunkVersionCeiling("PolygonTriggers");
	const Outcome o = parseWith(oneChunk("PolygonTriggers", 1, 1, {0x7E}), noteHit, "PolygonTriggers", nullptr, maxVersion, true);
	REQUIRE(o.ok);
	REQUIRE_EQ(o.hit.calls, 1);
	REQUIRE_EQ(o.hit.version, 1);
	REQUIRE_EQ(o.hit.size, 1);
}

TEST(chunk_newer_version_errors)
{
	const DataChunkVersionType maxVersion = DataChunkInput::mapChunkVersionCeiling("SidesList");
	const Outcome o = parseWith(oneChunk("SidesList", 1, uint16_t(maxVersion + 3), {9, 8, 7}), noteHit, "SidesList", nullptr, maxVersion, true);
	REQUIRE(!o.ok);
	REQUIRE(o.threw);
	REQUIRE_EQ(o.error, unsigned(ERROR_CORRUPT_FILE_FORMAT));
	REQUIRE_EQ(o.hit.calls, 0);
	const std::string expect = std::string("[chunk] SidesList v") + std::to_string(int(maxVersion + 3))
		+ " is newer than known v" + std::to_string(int(maxVersion)) + " (size 3)\n";
	REQUIRE(o.log.find(expect) != std::string::npos);
	REQUIRE_EQ(int(maxVersion), 6);
}

TEST(chunk_ceiling_does_not_count_as_skip)
{
	const DataChunkVersionType maxVersion = DataChunkInput::mapChunkVersionCeiling("PolygonTriggers");
	const unsigned above = unsigned(maxVersion) + 1;
	const Outcome o = parseWith(oneChunk("PolygonTriggers", 1, uint16_t(above), {1}), noteHit, "PolygonTriggers", nullptr,
		maxVersion, true);
	REQUIRE(o.threw);
	REQUIRE(o.log.find("skipped") == std::string::npos);
	REQUIRE(o.log.find("count") == std::string::npos);
	const std::string expect = std::string("[chunk] PolygonTriggers v") + std::to_string(int(above))
		+ " is newer than known v" + std::to_string(int(maxVersion)) + " (size 1)\n";
	REQUIRE(o.log.find(expect) != std::string::npos);
	REQUIRE_EQ(int(maxVersion), 5);
}

TEST(chunk_scan_maxima_reach_the_parser)
{
	// v6 and v5 are the maxima in the map scan. They must reach the parser.
	// #59 still owns the SidesList v6 layout. A ceiling throw here would reject every BFME1 map.
	const Outcome sides = parseWith(oneChunk("SidesList", 1, 6, {1, 2, 3}), noteHit, "SidesList", nullptr,
		DataChunkInput::mapChunkVersionCeiling("SidesList"), true);
	REQUIRE(sides.ok);
	REQUIRE(!sides.threw);
	REQUIRE_EQ(sides.hit.calls, 1);
	REQUIRE_EQ(sides.hit.version, 6);
	REQUIRE(sides.log.find("newer than known") == std::string::npos);

	const Outcome triggers = parseWith(oneChunk("PolygonTriggers", 1, 5, {1, 2, 3}), noteHit, "PolygonTriggers", nullptr,
		DataChunkInput::mapChunkVersionCeiling("PolygonTriggers"), true);
	REQUIRE(triggers.ok);
	REQUIRE(!triggers.threw);
	REQUIRE_EQ(triggers.hit.calls, 1);
	REQUIRE_EQ(triggers.hit.version, 5);
	REQUIRE(triggers.log.find("newer than known") == std::string::npos);
}

TEST(chunk_no_ceiling_parses_any_version)
{
	const Outcome o = parseWith(oneChunk("SidesList", 1, 99, {1}), noteHit, "SidesList", nullptr, 0, false);
	REQUIRE(o.ok);
	REQUIRE_EQ(o.hit.calls, 1);
	REQUIRE_EQ(o.hit.version, 99);
	REQUIRE(o.log.empty());
}

TEST(chunk_nested_unknown_shares_map_count)
{
	ByteWriter child;
	writeChunk(child, 2, 1, {0x10, 0x20});
	ByteWriter body;
	writeChunk(body, 1, 3, child.bytes);
	const std::vector<uint8_t> bytes = writeFile({{"ObjectsList", 1}, {"Nope", 2}}, body.bytes);

	Outcome o;
	StderrCapture cap;
	try {
		MemoryChunkStream stream(bytes);
		DataChunkInput file(&stream);
		file.registerParser("ObjectsList", AsciiString::TheEmptyString, parseChildren, &o.hit,
			DataChunkInput::mapChunkVersionCeiling("ObjectsList"));
		o.ok = file.parse(&o.hit) == true;
	} catch (ErrorCode e) {
		o.threw = true;
		o.error = unsigned(e);
	}
	o.log = cap.text();
	REQUIRE(o.ok);
	REQUIRE(!o.threw);
	REQUIRE_EQ(o.hit.calls, 1);
	REQUIRE(o.log.find("[chunk] skipped Nope v1 size 2 count 1\n") != std::string::npos);
	REQUIRE(o.log.find("ObjectsList") == std::string::npos);
}

TEST(chunk_wrong_parent_is_skipped)
{
	const Outcome o = parseWith(oneChunk("Object", 1, 3, {5, 6}), noteHit, "Object", "ObjectsList",
		DataChunkInput::mapChunkVersionCeiling("Object"), true);
	REQUIRE(o.ok);
	REQUIRE_EQ(o.hit.calls, 0);
	// Object is in the ceiling table. Wrong parent is a known skip, not a line.
	REQUIRE(o.log.find("[chunk] skipped Object") == std::string::npos);
	REQUIRE(o.log.find("[chunk] skipped 1 known\n") != std::string::npos);
	REQUIRE(o.log.find("newer than known") == std::string::npos);
}

TEST(chunk_ceiling_is_only_newer_than_known)
{
	// v4 is below GlobalLighting's ceiling of 7. The ceiling does not reject the hole;
	// ParseLightingDataChunk does that when it is the registered parser.
	const Outcome o = parseWith(oneChunk("GlobalLighting", 1, 4, {1, 2, 3, 4}), noteHit, "GlobalLighting", nullptr,
		DataChunkInput::mapChunkVersionCeiling("GlobalLighting"), true);
	REQUIRE(o.ok);
	REQUIRE_EQ(o.hit.calls, 1);
	REQUIRE_EQ(o.hit.version, 4);
	REQUIRE(o.log.find("newer than known") == std::string::npos);

	const Outcome above = parseWith(oneChunk("GlobalLighting", 1, 8, {1}), noteHit, "GlobalLighting", nullptr,
		DataChunkInput::mapChunkVersionCeiling("GlobalLighting"), true);
	REQUIRE(above.threw);
	REQUIRE_EQ(above.hit.calls, 0);
	REQUIRE(above.log.find("[chunk] GlobalLighting v8 is newer than known v7 (size 1)\n") != std::string::npos);
}

TEST(chunk_ceiling_table)
{
	REQUIRE_EQ(int(DataChunkInput::mapChunkVersionCeiling("HeightMapData")), int(K_HEIGHT_MAP_VERSION_5));
	REQUIRE_EQ(int(DataChunkInput::mapChunkVersionCeiling("BlendTileData")), int(K_BLEND_TILE_VERSION_MAX));
	REQUIRE_EQ(int(DataChunkInput::mapChunkVersionCeiling("GlobalLighting")), int(K_LIGHTING_VERSION_7));
	REQUIRE_EQ(int(DataChunkInput::mapChunkVersionCeiling("WorldInfo")), int(K_WORLDDICT_VERSION_1));
	REQUIRE_EQ(int(DataChunkInput::mapChunkVersionCeiling("ObjectsList")), int(K_OBJECTS_VERSION_3));
	REQUIRE_EQ(int(DataChunkInput::mapChunkVersionCeiling("Object")), int(K_OBJECTS_VERSION_3));
	REQUIRE_EQ(int(DataChunkInput::mapChunkVersionCeiling("PolygonTriggers")), int(K_TRIGGERS_VERSION_5));
	REQUIRE_EQ(int(K_TRIGGERS_VERSION_5), 5);
	REQUIRE_EQ(int(DataChunkInput::mapChunkVersionCeiling("SidesList")), int(K_SIDES_DATA_VERSION_6));
	REQUIRE_EQ(int(K_SIDES_DATA_VERSION_6), 6);
	REQUIRE_EQ(int(DataChunkInput::mapChunkVersionCeiling("WaypointsList")), int(K_WAYPOINTS_VERSION_1));
	REQUIRE_EQ(int(K_SIDES_DATA_VERSION_MAX), 3);
	REQUIRE_EQ(int(K_TRIGGERS_VERSION_4), 4);
	REQUIRE_EQ(int(K_HEIGHT_MAP_VERSION_5), 5);
	REQUIRE_EQ(int(K_BLEND_TILE_VERSION_MAX), 15);
	REQUIRE_EQ(int(DataChunkInput::mapChunkVersionCeiling("EnvironmentData")), 3);
	REQUIRE_EQ(int(DataChunkInput::mapChunkVersionCeiling("NamedCameras")), 2);
	REQUIRE_EQ(int(DataChunkInput::mapChunkVersionCeiling("SkyboxSettings")), 1);
	REQUIRE_EQ(int(DataChunkInput::mapChunkVersionCeiling("MPPositionList")), 0);
	REQUIRE_EQ(int(DataChunkInput::mapChunkVersionCeiling("MPPositionInfo")), 1);
	REQUIRE_EQ(int(DataChunkInput::mapChunkVersionCeiling("LibraryMapLists")), 1);
	REQUIRE_EQ(int(DataChunkInput::mapChunkVersionCeiling("LibraryMaps")), 1);
	REQUIRE_EQ(int(DataChunkInput::mapChunkVersionCeiling("BuildLists")), 1);
	REQUIRE_EQ(int(DataChunkInput::mapChunkVersionCeiling("Teams")), 1);
	REQUIRE_EQ(int(DataChunkInput::mapChunkVersionCeiling("PlayerScriptsList")), 5);
	REQUIRE_EQ(int(DataChunkInput::mapChunkVersionCeiling("ScriptList")), 1);
	REQUIRE_EQ(int(DataChunkInput::mapChunkVersionCeiling("ScriptGroup")), 3);
	REQUIRE_EQ(int(DataChunkInput::mapChunkVersionCeiling("Script")), 2);
	REQUIRE_EQ(int(DataChunkInput::mapChunkVersionCeiling("OrCondition")), 1);
	REQUIRE_EQ(int(DataChunkInput::mapChunkVersionCeiling("Condition")), 4);
	REQUIRE_EQ(int(DataChunkInput::mapChunkVersionCeiling("ScriptAction")), 2);
	REQUIRE_EQ(int(DataChunkInput::mapChunkVersionCeiling("ScriptActionFalse")), 2);

	const char* skipped[] = {
		"MapPreview", "ScriptsPlayers", "ScriptTeams", "FUNKY_TILING", nullptr
	};
	for (int i = 0; skipped[i]; ++i)
		REQUIRE_EQ(int(DataChunkInput::mapChunkVersionCeiling(skipped[i])), int(DATA_CHUNK_VERSION_ANY));
	REQUIRE_EQ(int(DataChunkInput::mapChunkVersionCeiling(nullptr)), int(DATA_CHUNK_VERSION_ANY));
}
