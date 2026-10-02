// BFME1 Mobile — tests (E1-25: 16-bit terrain heights)
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Writer for synthetic .map files in the engine's chunky format (DataChunkOutput layout: "CkMp",
// symbol table, then chunks of {u32 id, u16 version, i32 size, payload}). Only HeightMapData is
// meaningful; a dummy chunk before it checks that the parser skips foreign chunks. The maps are
// generated from a fixed seed, so no game data is needed or committed. Engine-independent: used by
// the unit tests and by heights_zh_dump (built against both the upstream and the patched tree).
#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace bfmeheights {

struct SyntheticMap {
	std::string name;
	int version = 4;              // HeightMapData chunk version: 1..4 = Generals/ZH (u8), 5 = BFME1 (u16)
	int width = 0, height = 0, border = 0;
	std::vector<uint16_t> samples;  // width*height; u8 range for v1..v4
};

class ByteWriter {
public:
	std::vector<uint8_t> bytes;
	void u8(uint8_t v) { bytes.push_back(v); }
	void u16(uint16_t v) { u8(uint8_t(v)); u8(uint8_t(v >> 8)); }
	void u32(uint32_t v) { u16(uint16_t(v)); u16(uint16_t(v >> 16)); }
	void i32(int32_t v) { u32(uint32_t(v)); }
	void raw(const void* p, size_t n) { bytes.insert(bytes.end(), (const uint8_t*)p, (const uint8_t*)p + n); }
};

// Serialises one map: symbol table {WorldInfoDummy=1, HeightMapData=2}, a dummy chunk, HeightMapData.
inline std::vector<uint8_t> writeMapFile(const SyntheticMap& m)
{
	ByteWriter payload;
	payload.i32(m.width);
	payload.i32(m.height);
	if (m.version >= 3) payload.i32(m.border);
	if (m.version >= 4) {
		payload.i32(1);  // one boundary
		payload.i32(m.width - 2 * m.border);
		payload.i32(m.height - 2 * m.border);
	}
	payload.i32(m.width * m.height);
	for (uint16_t s : m.samples) {
		if (m.version >= 5) payload.u16(s); else payload.u8(uint8_t(s));
	}

	ByteWriter f;
	f.raw("CkMp", 4);
	f.i32(2);
	const char* names[2] = {"WorldInfoDummy", "HeightMapData"};
	for (int i = 0; i < 2; ++i) {
		f.u8(uint8_t(std::strlen(names[i])));
		f.raw(names[i], std::strlen(names[i]));
		f.u32(uint32_t(i + 1));
	}
	f.u32(1); f.u16(1); f.i32(6); f.raw("dummy!", 6);   // skipped by the parser
	f.u32(2); f.u16(uint16_t(m.version)); f.i32(int32_t(payload.bytes.size()));
	f.raw(payload.bytes.data(), payload.bytes.size());
	return f.bytes;
}

// Deterministic generator (xorshift32): no <random>, so the sequence is identical everywhere.
struct XorShift {
	uint32_t s;
	explicit XorShift(uint32_t seed) : s(seed ? seed : 1u) {}
	uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
};

// Heights in [0, maxValue]: smooth hills, plateaus and cliffs (steps far above the 9.8 world-unit
// pathfinding cliff limit), the extremes 0 and maxValue, and plain noise.
inline std::vector<uint16_t> makeTerrain(int w, int h, uint32_t maxValue, uint32_t seed)
{
	XorShift rng(seed);
	std::vector<uint16_t> out(size_t(w) * size_t(h));
	for (int y = 0; y < h; ++y) {
		for (int x = 0; x < w; ++x) {
			uint32_t v;
			const uint32_t r = rng.next();
			switch ((x / 7 + y / 5) % 6) {
			case 0: v = (uint32_t(x * 37 + y * 11) % (maxValue + 1)); break;          // ramps
			case 1: v = (x % 3 == 0) ? maxValue : 0; break;                             // cliffs, extremes
			case 2: v = maxValue / 2 + (r % 3) - 1; break;                              // plateau with +-1 noise
			case 3: v = r % (maxValue + 1); break;                                      // noise
			case 4: v = ((x + y) & 1) ? maxValue / 3 : maxValue / 3 + maxValue / 16; break; // checkerboard steps
			default: v = (uint32_t(y) * maxValue) / uint32_t(h > 1 ? h - 1 : 1); break;  // y gradient
			}
			out[size_t(y) * size_t(w) + size_t(x)] = uint16_t(v > maxValue ? maxValue : v);
		}
	}
	if (!out.empty()) {
		out.front() = 0;
		out.back() = uint16_t(maxValue);
	}
	return out;
}

// The ZH (u8) map set compared bit by bit between the upstream and the patched build.
inline std::vector<SyntheticMap> zhSyntheticMaps()
{
	struct Size { int w, h, border; };
	const Size sizes[] = {{2, 2, 0}, {3, 5, 1}, {17, 9, 2}, {64, 48, 8}, {129, 130, 20}, {257, 193, 30}};
	std::vector<SyntheticMap> maps;
	uint32_t seed = 0xE125u;
	for (int version = 1; version <= 4; ++version) {
		for (const Size& s : sizes) {
			SyntheticMap m;
			m.version = version;
			m.width = s.w;
			m.height = s.h;
			m.border = version >= 3 ? s.border : 0;
			m.samples = makeTerrain(s.w, s.h, 255, seed++);
			m.name = "synthetic_zh_v" + std::to_string(version) + "_" + std::to_string(s.w) + "x" + std::to_string(s.h);
			maps.push_back(std::move(m));
		}
	}
	// Every u8 value once, row by row (16 x 16).
	SyntheticMap all;
	all.version = 4;
	all.width = all.height = 16;
	for (int i = 0; i < 256; ++i) all.samples.push_back(uint16_t(i));
	all.name = "synthetic_zh_v4_all_values";
	maps.push_back(std::move(all));
	return maps;
}

} // namespace bfmeheights
