// BFME1 Mobile — PolygonTriggers v5 parser tests
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Synthetic chunks have no EA bytes. The gamedata tests read maps.big and
// _patch222maps.big from BFME_GAMEDATA in place (exit 77 when it is unset) and
// do not write into that directory.
// Layout checked against the M1 audit, not copied from OpenSAGE: after riverStart,
// six texture names, additive, RGB + pad, uv scroll, alpha, then numPoints.
#include "PreRTS.h"

#include "../../heights/harness/TestHarness.h"
#include "MapIo.h"

#include "Common/DataChunk.h"
#include "Common/Errors.h"
#include "Common/MapObject.h"
#include "Common/MapReaderWriterInfo.h"
#include "GameLogic/PolygonTrigger.h"

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <string>
#include <unistd.h>
#include <vector>

namespace bfmetrigger {
std::string& capturedLog();
void resetGlobals();
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

class MemoryChunkStream : public ChunkInputStream {
public:
	explicit MemoryChunkStream(const std::vector<uint8_t>& data) : m_data(data) {}
	Int read(void* pData, Int numBytes) override
	{
		Int n = numBytes < 0 ? 0 : numBytes;
		if (m_pos + size_t(n) > m_data.size()) n = Int(m_data.size() - m_pos);
		if (n > 0) std::memcpy(pData, m_data.data() + m_pos, size_t(n));
		if (numBytes > n) std::memset((uint8_t*)pData + n, 0, size_t(numBytes - n));
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

const char* kTextures[PolygonTrigger::WATER_TEX_COUNT] = {
	"TWWater01.tga",
	"Noise0000.tga",
	"TWAlphaEdge.tga",
	"WaterSurfaceBubbles.tga",
	"WaterRippleBump.tga",
	"SkyEnv.tga",
};

struct PolySpec {
	std::string name;
	std::string layer;
	int id = 1;
	bool isWater = false;
	bool isRiver = false;
	int riverStart = 0;
	std::string tex[PolygonTrigger::WATER_TEX_COUNT];
	uint8_t additive = 0;
	uint8_t r = 0, g = 0, b = 0, pad = 0;
	float uvU = 0, uvV = 0, alpha = 0;
	std::vector<int> xyz; // x,y,z triples
};

void writePoly(ByteWriter& p, int version, const PolySpec& s)
{
	p.ascii(s.name);
	if (version >= 4) p.ascii(s.layer);
	p.i32(s.id);
	if (version >= 2) p.u8(s.isWater ? 1 : 0);
	if (version >= 3) {
		p.u8(s.isRiver ? 1 : 0);
		p.i32(s.riverStart);
	}
	if (version >= 5) {
		for (int i = 0; i < PolygonTrigger::WATER_TEX_COUNT; ++i) p.ascii(s.tex[i]);
		p.u8(s.additive);
		p.u8(s.r);
		p.u8(s.g);
		p.u8(s.b);
		p.u8(s.pad);
		p.f32(s.uvU);
		p.f32(s.uvV);
		p.f32(s.alpha);
	}
	const int n = int(s.xyz.size() / 3);
	p.i32(n);
	for (int v : s.xyz) p.i32(v);
}

std::vector<uint8_t> writeChunk(int version, const std::vector<uint8_t>& payload)
{
	ByteWriter f;
	f.raw("CkMp", 4);
	f.i32(1);
	const char* name = "PolygonTriggers";
	f.u8(uint8_t(std::strlen(name)));
	f.raw(name, std::strlen(name));
	f.u32(1);
	f.u32(1);
	f.u16(uint16_t(version));
	f.i32(int32_t(payload.size()));
	f.raw(payload.data(), payload.size());
	return f.bytes;
}

struct ParseResult {
	bool ok = false;
	bool atEnd = false;
	bool threw = false;
	bool saw = false;
	unsigned error = 0;
	unsigned left = 0xFFFFFFFFu;
	int version = -1;
};

bool g_atEnd = false;
bool g_saw = false;
unsigned g_left = 0;
int g_version = -1;

Bool parseWrap(DataChunkInput& file, DataChunkInfo* info, void*)
{
	g_saw = true;
	g_version = info->version;
	const Bool ok = PolygonTrigger::ParsePolygonTriggersDataChunk(file, info, nullptr);
	g_atEnd = file.atEndOfChunk() == true;
	g_left = file.getChunkDataSizeLeft();
	return ok;
}

ParseResult parseBytes(const std::vector<uint8_t>& bytes)
{
	bfmetrigger::resetGlobals();
	g_atEnd = false;
	g_saw = false;
	g_left = 0xFFFFFFFFu;
	g_version = -1;
	ParseResult r;
	MemoryChunkStream stream(bytes);
	DataChunkInput file(&stream);
	if (!file.isValidFileType()) return r;
	file.registerParser("PolygonTriggers", AsciiString::TheEmptyString, parseWrap, nullptr);
	try {
		r.ok = file.parse(nullptr) == true;
		r.atEnd = g_atEnd;
		r.saw = g_saw;
		r.left = g_left;
		r.version = g_version;
	} catch (ErrorCode e) {
		r.threw = true;
		r.error = unsigned(e);
		r.saw = g_saw;
		r.version = g_version;
	}
	return r;
}

std::vector<uint8_t> g_capturedBytes;
ParseResult g_captured;

void captureParse()
{
	g_captured = parseBytes(g_capturedBytes);
}

std::string captureStderr(void (*fn)())
{
	int fds[2] = {-1, -1};
	if (pipe(fds) != 0) throw bfmetest::Failure("pipe");
	const int saved = dup(fileno(stderr));
	if (saved < 0 || dup2(fds[1], fileno(stderr)) < 0) throw bfmetest::Failure("dup stderr");
	close(fds[1]);
	try {
		fn();
		fflush(stderr);
	} catch (...) {
		fflush(stderr);
		dup2(saved, fileno(stderr));
		close(saved);
		close(fds[0]);
		throw;
	}
	dup2(saved, fileno(stderr));
	close(saved);
	std::string out;
	char buf[1024];
	ssize_t n = 0;
	while ((n = read(fds[0], buf, sizeof(buf))) > 0) out.append(buf, size_t(n));
	close(fds[0]);
	return out;
}

int countTriggers()
{
	int n = 0;
	for (PolygonTrigger* p = PolygonTrigger::getFirstPolygonTrigger(); p; p = p->getNext()) ++n;
	return n;
}

int countPoints()
{
	int n = 0;
	for (PolygonTrigger* p = PolygonTrigger::getFirstPolygonTrigger(); p; p = p->getNext()) n += p->getNumPoints();
	return n;
}

void requirePoints(PolygonTrigger* p, const int* xyz, int n)
{
	REQUIRE_EQ(p->getNumPoints(), n);
	for (int i = 0; i < n; ++i) {
		const ICoord3D* pt = p->getPoint(i);
		REQUIRE_EQ(pt->x, xyz[i * 3]);
		REQUIRE_EQ(pt->y, xyz[i * 3 + 1]);
		REQUIRE_EQ(pt->z, xyz[i * 3 + 2]);
	}
}

void requireWaterDefaults(const PolygonTrigger* p)
{
	for (int i = 0; i < PolygonTrigger::WATER_TEX_COUNT; ++i)
		REQUIRE_EQ(std::string(p->getWaterTexture(i).str()), std::string());
	REQUIRE(!p->isWaterAdditive());
	REQUIRE_EQ(int(p->getWaterColorR()), 0);
	REQUIRE_EQ(int(p->getWaterColorG()), 0);
	REQUIRE_EQ(int(p->getWaterColorB()), 0);
	REQUIRE_EQ(int(p->getWaterColorPad()), 0);
	REQUIRE_EQ(p->getWaterUvScrollU(), 0.0f);
	REQUIRE_EQ(p->getWaterUvScrollV(), 0.0f);
	REQUIRE_EQ(p->getWaterAlpha(), 0.0f);
}

PolySpec sampleV5()
{
	PolySpec s;
	s.name = "river7";
	s.layer = "Ground";
	s.id = 46;
	s.isWater = true;
	s.isRiver = true;
	s.riverStart = 2;
	for (int i = 0; i < PolygonTrigger::WATER_TEX_COUNT; ++i) s.tex[i] = kTextures[i];
	s.additive = 1;
	s.r = 10;
	s.g = 20;
	s.b = 30;
	s.pad = 0;
	s.uvU = 0.25f;
	s.uvV = -0.5f;
	s.alpha = 0.75f;
	s.xyz = {10, 20, 30, 40, 50, 60, 70, 80, 90};
	return s;
}

std::string env(const char* name)
{
	const char* v = std::getenv(name);
	return v ? std::string(v) : std::string();
}

struct MapStats {
	int maps = 0;
	int polygons = 0;
	int points = 0;
	int empty = 0;
};

MapStats parseArchive(const std::string& bigPath)
{
	std::vector<bfmetrigger::BigEntry> entries;
	std::string error;
	if (!bfmetrigger::readBigDirectory(bigPath, entries, error)) throw bfmetest::Failure(error);
	MapStats stats;
	for (const bfmetrigger::BigEntry& e : entries) {
		const std::string name = bfmetrigger::normalizeBigName(e.name);
		if (name.size() < 4 || name.compare(name.size() - 4, 4, ".map") != 0) continue;
		std::vector<uint8_t> stored, plain;
		if (!bfmetrigger::readBigEntry(bigPath, e, stored)) throw bfmetest::Failure("cannot read " + name);
		if (!bfmetrigger::decompressMap(stored, plain, error)) throw bfmetest::Failure(name + ": " + error);
		const ParseResult r = parseBytes(plain);
		if (r.threw || !r.ok || !r.saw || !r.atEnd || r.left != 0 || r.version != K_TRIGGERS_VERSION_5) {
			PolygonTrigger::deleteTriggers();
			throw bfmetest::Failure(name + " version=" + std::to_string(r.version) + " left=" + std::to_string(r.left)
				+ " threw=" + std::to_string(int(r.threw)) + " error=" + std::to_string(r.error));
		}
		const int polys = countTriggers();
		const int points = countPoints();
		stats.maps += 1;
		stats.polygons += polys;
		stats.points += points;
		stats.empty += polys == 0;
		PolygonTrigger::deleteTriggers();
	}
	return stats;
}

} // namespace

// The first four bytes of the length-prefixed name "TWWater01.tga" are the int 1465122829.
// v5 must read that name as a texture, then the three points, and finish the chunk.
TEST(triggers_v5_reads_water_fields)
{
	ByteWriter tex;
	tex.ascii("TWWater01.tga");
	int32_t asCount = 0;
	std::memcpy(&asCount, tex.bytes.data(), 4);
	REQUIRE_EQ(asCount, 1465122829);

	const PolySpec a = sampleV5();
	PolySpec b;
	b.name = "ford2";
	b.layer = "";
	b.id = 43;
	b.isWater = true;
	b.isRiver = false;
	b.riverStart = 0;
	for (int i = 0; i < PolygonTrigger::WATER_TEX_COUNT; ++i) b.tex[i] = std::string("t") + char('0' + i) + ".tga";
	b.additive = 0;
	b.r = 1;
	b.g = 2;
	b.b = 3;
	b.pad = 0;
	b.uvU = 1.0f;
	b.uvV = 2.0f;
	b.alpha = 0.5f;
	b.xyz = {1, 2, 3, 4, 5, 6};

	ByteWriter payload;
	payload.i32(2);
	writePoly(payload, 5, a);
	writePoly(payload, 5, b);
	const ParseResult r = parseBytes(writeChunk(5, payload.bytes));
	REQUIRE(r.ok);
	REQUIRE(r.atEnd);
	REQUIRE(!r.threw);
	REQUIRE_EQ(r.left, 0u);
	REQUIRE_EQ(r.version, 5);
	REQUIRE_EQ(countTriggers(), 2);
	REQUIRE_EQ(countPoints(), 5);

	PolygonTrigger* p = PolygonTrigger::getFirstPolygonTrigger();
	REQUIRE_EQ(std::string(p->getTriggerName().str()), std::string("river7"));
	REQUIRE_EQ(std::string(p->getLayerName().str()), std::string("Ground"));
	REQUIRE_EQ(p->getID(), 46);
	REQUIRE(p->isWaterArea());
	REQUIRE(p->isRiver());
	REQUIRE_EQ(p->getRiverStart(), 2);
	for (int i = 0; i < PolygonTrigger::WATER_TEX_COUNT; ++i)
		REQUIRE_EQ(std::string(p->getWaterTexture(i).str()), std::string(kTextures[i]));
	REQUIRE(p->isWaterAdditive());
	REQUIRE_EQ(int(p->getWaterColorR()), 10);
	REQUIRE_EQ(int(p->getWaterColorG()), 20);
	REQUIRE_EQ(int(p->getWaterColorB()), 30);
	REQUIRE_EQ(int(p->getWaterColorPad()), 0);
	REQUIRE_EQ(p->getWaterUvScrollU(), 0.25f);
	REQUIRE_EQ(p->getWaterUvScrollV(), -0.5f);
	REQUIRE_EQ(p->getWaterAlpha(), 0.75f);
	const int aPts[] = {10, 20, 30, 40, 50, 60, 70, 80, 90};
	requirePoints(p, aPts, 3);

	p = p->getNext();
	REQUIRE_EQ(std::string(p->getTriggerName().str()), std::string("ford2"));
	REQUIRE(!p->isRiver());
	REQUIRE(!p->isWaterAdditive());
	REQUIRE_EQ(int(p->getWaterColorPad()), 0);
	REQUIRE_EQ(p->getWaterAlpha(), 0.5f);
	REQUIRE_EQ(std::string(p->getWaterTexture(0).str()), std::string("t0.tga"));
	REQUIRE_EQ(std::string(p->getWaterTexture(5).str()), std::string("t5.tga"));
	const int bPts[] = {1, 2, 3, 4, 5, 6};
	requirePoints(p, bPts, 2);
	REQUIRE(p->getNext() == nullptr);
	REQUIRE_EQ(std::string(p->getWaterTexture(99).str()), std::string());
	PolygonTrigger::deleteTriggers();
}

// v0–v4 stay on the Zero Hour layout: no water-texture fields, v1 still adds the default water area.
TEST(triggers_old_versions_match_zh)
{
	const int pts[] = {100, 200, 7, 300, 400, 8, 500, 600, 9};
	PolySpec s;
	s.name = "area";
	s.layer = "LayerA";
	s.id = 9;
	s.isWater = true;
	s.isRiver = true;
	s.riverStart = 1;
	s.xyz.assign(pts, pts + 9);

	for (int version = 0; version <= 4; ++version) {
		ByteWriter payload;
		payload.i32(1);
		writePoly(payload, version, s);
		const ParseResult r = parseBytes(writeChunk(version, payload.bytes));
		REQUIRE(r.ok);
		REQUIRE(r.atEnd);
		REQUIRE_EQ(r.left, 0u);
		REQUIRE_EQ(r.version, version);
		const int expect = version == 1 ? 2 : 1;
		REQUIRE_EQ(countTriggers(), expect);
		PolygonTrigger* p = PolygonTrigger::getFirstPolygonTrigger();
		REQUIRE_EQ(std::string(p->getTriggerName().str()), std::string("area"));
		REQUIRE_EQ(p->getID(), 9);
		REQUIRE_EQ(p->isWaterArea(), version >= 2);
		REQUIRE_EQ(p->isRiver(), version >= 3);
		REQUIRE_EQ(p->getRiverStart(), version >= 3 ? 1 : 0);
		if (version >= 4) REQUIRE_EQ(std::string(p->getLayerName().str()), std::string("LayerA"));
		else REQUIRE_EQ(std::string(p->getLayerName().str()), std::string());
		requireWaterDefaults(p);
		requirePoints(p, pts, 3);
		if (version == 1) {
			PolygonTrigger* water = p->getNext();
			REQUIRE(water != nullptr);
			REQUIRE(water->isWaterArea());
			REQUIRE(!water->isRiver());
			REQUIRE_EQ(water->getNumPoints(), 4);
			requireWaterDefaults(water);
			REQUIRE_EQ(water->getPoint(0)->x, int(-30 * MAP_XY_FACTOR));
			REQUIRE_EQ(water->getPoint(0)->y, int(-30 * MAP_XY_FACTOR));
			REQUIRE_EQ(water->getPoint(0)->z, 7);
			REQUIRE_EQ(water->getPoint(2)->x, int(30 * MAP_XY_FACTOR));
			REQUIRE_EQ(water->getPoint(2)->y, int(30 * MAP_XY_FACTOR));
		}
		PolygonTrigger::deleteTriggers();
	}

	// One point is not a polygon. Zero Hour drops it and still finishes the chunk.
	PolySpec one = s;
	one.xyz = {1, 2, 3};
	ByteWriter payload;
	payload.i32(1);
	writePoly(payload, 4, one);
	const ParseResult dropped = parseBytes(writeChunk(4, payload.bytes));
	REQUIRE(dropped.ok);
	REQUIRE(dropped.atEnd);
	REQUIRE_EQ(dropped.left, 0u);
	REQUIRE_EQ(countTriggers(), 0);
	REQUIRE(bfmetrigger::capturedLog().find("Deleting polygon trigger") != std::string::npos);
	PolygonTrigger::deleteTriggers();
}

TEST(triggers_numpoints_outside_chunk_rejected)
{
	auto requireReject = [](const std::vector<uint8_t>& payload, const char* needle) {
		g_capturedBytes = writeChunk(4, payload);
		const std::string err = captureStderr(&captureParse);
		REQUIRE(g_captured.threw);
		REQUIRE(!g_captured.ok);
		REQUIRE_EQ(g_captured.error, unsigned(ERROR_CORRUPT_FILE_FORMAT));
		REQUIRE(bfmetrigger::capturedLog().find(needle) != std::string::npos);
		REQUIRE(err.find(needle) != std::string::npos);
		REQUIRE_EQ(countTriggers(), 0);
		PolygonTrigger::deleteTriggers();
	};

	ByteWriter huge;
	huge.i32(1);
	huge.ascii("river7");
	huge.ascii("Ground");
	huge.i32(46);
	huge.u8(1);
	huge.u8(1);
	huge.i32(2);
	huge.i32(1465122829);
	requireReject(huge.bytes, "1465122829");

	ByteWriter shortTail;
	shortTail.i32(1);
	shortTail.ascii("area");
	shortTail.ascii("L");
	shortTail.i32(1);
	shortTail.u8(0);
	shortTail.u8(0);
	shortTail.i32(0);
	shortTail.i32(2);
	shortTail.i32(1);
	shortTail.i32(2);
	shortTail.i32(3);
	requireReject(shortTail.bytes, "numPoints 2");

	ByteWriter negative;
	negative.i32(1);
	negative.ascii("area");
	negative.ascii("L");
	negative.i32(1);
	negative.u8(0);
	negative.u8(0);
	negative.i32(0);
	negative.i32(-1);
	requireReject(negative.bytes, "numPoints -1");

	// Exactly 12 bytes holds one point. Two points do not fit; one point is read and then dropped.
	ByteWriter exact;
	exact.i32(1);
	exact.ascii("area");
	exact.ascii("L");
	exact.i32(1);
	exact.u8(0);
	exact.u8(0);
	exact.i32(0);
	exact.i32(1);
	exact.i32(9);
	exact.i32(8);
	exact.i32(7);
	const ParseResult one = parseBytes(writeChunk(4, exact.bytes));
	REQUIRE(one.ok);
	REQUIRE(one.atEnd);
	REQUIRE_EQ(one.left, 0u);
	REQUIRE_EQ(countTriggers(), 0);
	PolygonTrigger::deleteTriggers();
}

TEST(triggers_version_above_5_rejected)
{
	PolygonTrigger* keep = newInstance(PolygonTrigger)(2);
	keep->setTriggerName(AsciiString("keep"));
	PolygonTrigger::addPolygonTrigger(keep);

	const PolySpec s = sampleV5();
	ByteWriter payload;
	payload.i32(1);
	writePoly(payload, 5, s);
	g_capturedBytes = writeChunk(6, payload.bytes);
	const std::string err = captureStderr(&captureParse);
	REQUIRE(g_captured.threw);
	REQUIRE_EQ(g_captured.error, unsigned(ERROR_CORRUPT_FILE_FORMAT));
	REQUIRE(bfmetrigger::capturedLog().find("unsupported version v6") != std::string::npos);
	REQUIRE(err.find("unsupported version v6") != std::string::npos);
	// The previous map's polygons must not survive a version this parser does not know.
	REQUIRE_EQ(countTriggers(), 0);

	keep = newInstance(PolygonTrigger)(2);
	keep->setTriggerName(AsciiString("keep"));
	PolygonTrigger::addPolygonTrigger(keep);
	g_capturedBytes = writeChunk(7, payload.bytes);
	const ParseResult v7 = parseBytes(g_capturedBytes);
	REQUIRE(v7.threw);
	REQUIRE_EQ(v7.error, unsigned(ERROR_CORRUPT_FILE_FORMAT));
	REQUIRE(bfmetrigger::capturedLog().find("unsupported version v7") != std::string::npos);
	REQUIRE_EQ(countTriggers(), 0);
}

// Release readAsciiString does not stop at the chunk. A short v5 payload must
// fail on the remainder check, and must not keep polygons from the previous map.
TEST(triggers_v5_truncated_chunk_rejected)
{
	auto requireTruncated = [](const std::vector<uint8_t>& payload) {
		PolygonTrigger* keep = newInstance(PolygonTrigger)(2);
		keep->setTriggerName(AsciiString("keep"));
		PolygonTrigger::addPolygonTrigger(keep);
		REQUIRE_EQ(countTriggers(), 1);
		g_capturedBytes = writeChunk(5, payload);
		const std::string err = captureStderr(&captureParse);
		REQUIRE(g_captured.threw);
		REQUIRE(!g_captured.ok);
		REQUIRE_EQ(g_captured.error, unsigned(ERROR_CORRUPT_FILE_FORMAT));
		REQUIRE(bfmetrigger::capturedLog().find("v5 water fields do not fit the chunk") != std::string::npos);
		REQUIRE(err.find("v5 water fields do not fit the chunk") != std::string::npos);
		REQUIRE_EQ(countTriggers(), 0);
	};

	// Length says 13 ("TWWater01.tga") but only "TW" is in the chunk. Release
	// readAsciiString still consumes the claimed length.
	ByteWriter cut;
	cut.i32(1);
	cut.ascii("river7");
	cut.ascii("Ground");
	cut.i32(1);
	cut.u8(1);
	cut.u8(1);
	cut.i32(0);
	cut.u16(13);
	cut.raw("TW", 2);
	requireTruncated(cut.bytes);

	// Water fields are complete. The point count is missing, so nothing is left for numPoints.
	ByteWriter full;
	full.i32(1);
	writePoly(full, 5, sampleV5());
	const size_t pointTail = 4u + (sampleV5().xyz.size() / 3) * 12u;
	REQUIRE(full.bytes.size() > pointTail);
	full.bytes.resize(full.bytes.size() - pointTail);
	requireTruncated(full.bytes);
}

TEST(gamedata_fords_of_isen)
{
	const std::string root = env("BFME_GAMEDATA");
	if (root.empty()) SKIP("BFME_GAMEDATA not set");
	const std::string big = root + "/_patch222maps.big";
	if (!std::filesystem::exists(big)) SKIP("no _patch222maps.big in BFME_GAMEDATA");

	std::vector<bfmetrigger::BigEntry> entries;
	std::string error;
	if (!bfmetrigger::readBigDirectory(big, entries, error)) throw bfmetest::Failure(error);
	const bfmetrigger::BigEntry* fords = nullptr;
	for (const bfmetrigger::BigEntry& e : entries) {
		if (bfmetrigger::normalizeBigName(e.name) == "maps\\map mp fords of isen\\map mp fords of isen.map") {
			fords = &e;
			break;
		}
	}
	if (!fords) throw bfmetest::Failure("map mp fords of isen is not in _patch222maps.big");
	std::vector<uint8_t> stored, plain;
	if (!bfmetrigger::readBigEntry(big, *fords, stored)) throw bfmetest::Failure("cannot read Fords of Isen");
	if (!bfmetrigger::decompressMap(stored, plain, error)) throw bfmetest::Failure(error);
	const ParseResult r = parseBytes(plain);
	REQUIRE(r.ok);
	REQUIRE(!r.threw);
	REQUIRE(r.atEnd);
	REQUIRE_EQ(r.left, 0u);
	REQUIRE_EQ(r.version, int(K_TRIGGERS_VERSION_5));
	REQUIRE_EQ(countTriggers(), 10);
	REQUIRE_EQ(countPoints(), 118);
	for (PolygonTrigger* p = PolygonTrigger::getFirstPolygonTrigger(); p; p = p->getNext()) {
		REQUIRE(p->isWaterArea());
		REQUIRE(p->isRiver());
		REQUIRE(p->getNumPoints() >= 2);
		for (int i = 0; i < PolygonTrigger::WATER_TEX_COUNT; ++i) {
			REQUIRE_EQ(std::string(p->getWaterTexture(i).str()), std::string(kTextures[i]));
		}
	}
	PolygonTrigger::deleteTriggers();
}

TEST(gamedata_all_maps_remainder_zero)
{
	const std::string root = env("BFME_GAMEDATA");
	if (root.empty()) SKIP("BFME_GAMEDATA not set");
	const std::string maps = root + "/maps.big";
	const std::string patch = root + "/_patch222maps.big";
	if (!std::filesystem::exists(maps)) SKIP("no maps.big in BFME_GAMEDATA");
	if (!std::filesystem::exists(patch)) SKIP("no _patch222maps.big in BFME_GAMEDATA");

	const MapStats base = parseArchive(maps);
	const MapStats patched = parseArchive(patch);
	REQUIRE_EQ(base.maps, 115);
	REQUIRE_EQ(patched.maps, 291);
	REQUIRE(base.polygons > 0);
	REQUIRE(patched.polygons > 0);
	REQUIRE(base.points > 0);
	REQUIRE(patched.points > 0);
	std::printf("gamedata_all_maps_remainder_zero: maps.big %d maps %d polys %d pts (%d empty); "
	            "_patch222maps.big %d maps %d polys %d pts (%d empty)\n",
		base.maps, base.polygons, base.points, base.empty,
		patched.maps, patched.polygons, patched.points, patched.empty);
	PolygonTrigger::deleteTriggers();
}
