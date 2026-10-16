// BFME1 Mobile — GlobalLighting v7 parser tests (ADR-0005 §4.1)
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Synthetic chunks only. v7 payload is 4 + 4*9*36 + 4 + 44 = 1348 bytes.
// Layout reference, not copied: OpenSAGE GlobalLightingConfiguration.cs, master, read 08.10.2026.
#include "PreRTS.h"

#include "../../heights/harness/TestHarness.h"

#include "Common/DataChunk.h"
#include "Common/Errors.h"
#include "Common/GameType.h"
#include "Common/GlobalData.h"
#include "Common/MapReaderWriterInfo.h"
#include "W3DDevice/GameClient/W3DShadow.h"
#include "W3DDevice/GameClient/WorldHeightMap.h"

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace bfmelighting {
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
};

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

// kind: 0 terrain, 1 object, 2 infantry. comp: 0..8 = ambient rgb, diffuse rgb, direction xyz.
float lightFloat(int tod, int kind, int slot, int comp)
{
	return float(tod * 1000 + kind * 100 + slot * 10 + comp) + 0.25f;
}

void writeLight(ByteWriter& p, int tod, int kind, int slot)
{
	for (int c = 0; c < 9; ++c) p.f32(lightFloat(tod, kind, slot, c));
}

// v1: T0 O0. v2: + O1 O2. v3: + T1 T2. Same order as the ZH writer.
void writeZhLights(ByteWriter& p, int version)
{
	for (int tod = 0; tod < 4; ++tod) {
		writeLight(p, tod, 0, 0);
		writeLight(p, tod, 1, 0);
		if (version >= 2) {
			writeLight(p, tod, 1, 1);
			writeLight(p, tod, 1, 2);
		}
		if (version >= 3) {
			writeLight(p, tod, 0, 1);
			writeLight(p, tod, 0, 2);
		}
	}
}

// v7: per time of day, T0 O0 I0, T1 O1 I1, T2 O2 I2.
void writeV7Lights(ByteWriter& p)
{
	for (int tod = 0; tod < 4; ++tod) {
		for (int slot = 0; slot < 3; ++slot) {
			writeLight(p, tod, 0, slot);
			writeLight(p, tod, 1, slot);
			writeLight(p, tod, 2, slot);
		}
	}
}

std::vector<uint8_t> writeMap(int version, int timeOfDay, const std::vector<uint8_t>& payload)
{
	ByteWriter f;
	f.raw("CkMp", 4);
	f.i32(1);
	const char* name = "GlobalLighting";
	f.u8(uint8_t(std::strlen(name)));
	f.raw(name, std::strlen(name));
	f.u32(1); // symbol id
	f.u32(1); // chunk id
	f.u16(uint16_t(version));
	f.i32(int32_t(payload.size()));
	f.raw(payload.data(), payload.size());
	(void)timeOfDay;
	return f.bytes;
}

std::vector<uint8_t> zhPayload(int version, int timeOfDay, uint32_t shadow)
{
	ByteWriter p;
	p.i32(timeOfDay);
	writeZhLights(p, version);
	p.u32(shadow);
	return p.bytes;
}

std::vector<uint8_t> v7Payload(int timeOfDay, uint32_t shadow, const uint8_t unknown[44])
{
	ByteWriter p;
	p.i32(timeOfDay);
	writeV7Lights(p);
	p.u32(shadow);
	p.raw(unknown, 44);
	return p.bytes;
}

struct ParseResult {
	bool ok = false;
	bool atEnd = false;
	bool threw = false;
	unsigned error = 0;
};

bool g_atEnd = false;

// ParseLightingDataChunk is protected. A derived class can call it without constructing the map.
class TestLightMap : public WorldHeightMap {
public:
	static Bool parse(DataChunkInput& file, DataChunkInfo* info, void*)
	{
		return ParseLightingDataChunk(file, info, nullptr);
	}
};

Bool parseWrap(DataChunkInput& file, DataChunkInfo* info, void* userData)
{
	(void)userData;
	const Bool ok = TestLightMap::parse(file, info, nullptr);
	g_atEnd = file.atEndOfChunk() == true;
	return ok;
}

ParseResult parseChunk(const std::vector<uint8_t>& bytes)
{
	bfmelighting::resetGlobals();
	g_atEnd = false;
	ParseResult r;
	MemoryChunkStream stream(bytes);
	DataChunkInput file(&stream);
	if (!file.isValidFileType()) return r;
	file.registerParser("GlobalLighting", AsciiString::TheEmptyString, parseWrap, nullptr);
	try {
		r.ok = file.parse(nullptr) == true;
		r.atEnd = g_atEnd;
	} catch (ErrorCode e) {
		r.threw = true;
		r.error = unsigned(e);
	}
	return r;
}

const GlobalData::TerrainLighting& stored(int which, int tod, int slot)
{
	const int idx = tod + TIME_OF_DAY_FIRST;
	if (which == 0) return TheGlobalData->m_terrainLighting[idx][slot];
	if (which == 1) return TheGlobalData->m_terrainObjectsLighting[idx][slot];
	return TheGlobalData->m_terrainInfantryLighting[idx][slot];
}

void requireLight(int which, int tod, int slot)
{
	const GlobalData::TerrainLighting& L = stored(which, tod, slot);
	const float* f = &L.ambient.red;
	for (int c = 0; c < 9; ++c) REQUIRE_EQ(f[c], lightFloat(tod, which, slot, c));
}

void requireInit(int which, int tod, int slot)
{
	const GlobalData::TerrainLighting& L = stored(which, tod, slot);
	REQUIRE_EQ(L.ambient.red, 0.0f);
	REQUIRE_EQ(L.ambient.green, 0.0f);
	REQUIRE_EQ(L.ambient.blue, 0.0f);
	REQUIRE_EQ(L.diffuse.red, 0.0f);
	REQUIRE_EQ(L.diffuse.green, 0.0f);
	REQUIRE_EQ(L.diffuse.blue, 0.0f);
	REQUIRE_EQ(L.lightPos.x, 0.0f);
	REQUIRE_EQ(L.lightPos.y, 0.0f);
	REQUIRE_EQ(L.lightPos.z, -1.0f);
}

} // namespace

TEST(lighting_v7_1348_all_lights)
{
	uint8_t unknown[44];
	for (int i = 0; i < 44; ++i) unknown[i] = uint8_t(0xA0 + i);
	const uint32_t shadow = 0xA1B2C3D4u;
	const std::vector<uint8_t> payload = v7Payload(TIME_OF_DAY_AFTERNOON, shadow, unknown);
	REQUIRE_EQ(payload.size(), size_t(1348));
	const ParseResult r = parseChunk(writeMap(K_LIGHTING_VERSION_7, TIME_OF_DAY_AFTERNOON, payload));
	REQUIRE(r.ok);
	REQUIRE(r.atEnd);
	REQUIRE(!r.threw);
	REQUIRE_EQ(int(TheGlobalData->m_timeOfDay), int(TIME_OF_DAY_AFTERNOON));
	REQUIRE(TheGlobalData->m_terrainInfantryLightingPresent);
	REQUIRE(TheGlobalData->m_bfmeLightingUnknownPresent);
	REQUIRE_EQ(TheW3DShadowManager->getShadowColor(), shadow);
	for (int tod = 0; tod < 4; ++tod) {
		for (int slot = 0; slot < 3; ++slot) {
			requireLight(0, tod, slot);
			requireLight(1, tod, slot);
			requireLight(2, tod, slot);
		}
	}
	REQUIRE(std::memcmp(TheGlobalData->m_bfmeLightingUnknown, unknown, 44) == 0);
	REQUIRE(bfmelighting::capturedLog().find("unsupported version") == std::string::npos);
}

TEST(lighting_v7_truncated_rejected)
{
	// Payload is the chunk body. 1300 is short of 4 + 4*9*36 + 4 + 44.
	std::vector<uint8_t> payload(1300, 0x11);
	payload[0] = uint8_t(TIME_OF_DAY_AFTERNOON);
	REQUIRE_EQ(payload.size(), size_t(1300));
	const ParseResult r = parseChunk(writeMap(K_LIGHTING_VERSION_7, TIME_OF_DAY_AFTERNOON, payload));
	REQUIRE(!r.ok);
	REQUIRE(r.threw);
	REQUIRE_EQ(r.error, unsigned(ERROR_CORRUPT_FILE_FORMAT));
	REQUIRE(!TheGlobalData->m_terrainInfantryLightingPresent);
}

TEST(lighting_v3_zh_no_regression)
{
	const uint32_t shadow = 0x01020304u;
	for (int version = 1; version <= 3; ++version) {
		const std::vector<uint8_t> payload = zhPayload(version, TIME_OF_DAY_MORNING, shadow);
		if (version == 1) REQUIRE_EQ(payload.size(), size_t(4 + 4 * 2 * 36 + 4));
		if (version == 2) REQUIRE_EQ(payload.size(), size_t(4 + 4 * 4 * 36 + 4));
		if (version == 3) REQUIRE_EQ(payload.size(), size_t(4 + 4 * 6 * 36 + 4));
		const ParseResult r = parseChunk(writeMap(version, TIME_OF_DAY_MORNING, payload));
		REQUIRE(r.ok);
		REQUIRE(r.atEnd);
		REQUIRE_EQ(int(TheGlobalData->m_timeOfDay), int(TIME_OF_DAY_MORNING));
		REQUIRE(!TheGlobalData->m_terrainInfantryLightingPresent);
		REQUIRE(!TheGlobalData->m_bfmeLightingUnknownPresent);
		REQUIRE_EQ(TheW3DShadowManager->getShadowColor(), shadow);
		for (int tod = 0; tod < 4; ++tod) {
			requireLight(0, tod, 0);
			requireLight(1, tod, 0);
			if (version >= 2) {
				requireLight(1, tod, 1);
				requireLight(1, tod, 2);
			} else {
				requireInit(1, tod, 1);
				requireInit(1, tod, 2);
			}
			if (version >= 3) {
				requireLight(0, tod, 1);
				requireLight(0, tod, 2);
			} else {
				requireInit(0, tod, 1);
				requireInit(0, tod, 2);
			}
			for (int slot = 0; slot < 3; ++slot) requireInit(2, tod, slot);
		}
		for (int i = 0; i < GlobalData::BFME_LIGHTING_UNKNOWN_BYTES; ++i)
			REQUIRE_EQ(int(TheGlobalData->m_bfmeLightingUnknown[i]), 0);
	}
}

TEST(lighting_version_0_and_8_rejected)
{
	uint8_t unknown[44];
	std::memset(unknown, 0x5A, sizeof(unknown));
	const std::vector<uint8_t> payload = v7Payload(TIME_OF_DAY_AFTERNOON, 1u, unknown);
	for (int version : {0, 4, 5, 6, 8, 9}) {
		bfmelighting::capturedLog().clear();
		const ParseResult r = parseChunk(writeMap(version, TIME_OF_DAY_AFTERNOON, payload));
		REQUIRE(!r.ok);
		REQUIRE(r.threw);
		REQUIRE_EQ(r.error, unsigned(ERROR_CORRUPT_FILE_FORMAT));
		REQUIRE(bfmelighting::capturedLog().find("unsupported version v" + std::to_string(version)) != std::string::npos);
		// The throw is before any light is stored, so a bad version cannot be read as v3.
		REQUIRE(!TheGlobalData->m_terrainInfantryLightingPresent);
		REQUIRE_EQ(int(TheGlobalData->m_timeOfDay), 0);
	}
}
