// BFME1 Mobile — tests (E1-25: BlendTileData v14/v15)
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Writer for synthetic .map files with HeightMapData + BlendTileData in the layout of
// docs/formats/BFME1_vs_ZH.md §2.4.1-2.4.3 (ZH v1..v8 as WorldHeightMap::ParseBlendTileData reads it,
// BFME1 v9..v15 as OpenSAGE BlendTileData.cs). Engine-independent: used by the unit tests and by
// blend_zh_dump (built against both the upstream and the patched tree). No game data involved.
#pragma once

#include "../../heights/src/SyntheticMaps.h"

#include <cstdint>
#include <string>
#include <vector>

namespace bfmeblend {

using bfmeheights::ByteWriter;
using bfmeheights::XorShift;

const uint32_t BLEND_FLAG_VAL = 0x7ADA0000u;

struct TexClassSpec { int32_t firstTile = 0, numTiles = 0, width = 0; std::string name; };
struct BlendDescSpec {
	int32_t blendNdx = 0;
	uint8_t horiz = 0, vert = 0, rightDiagonal = 0, leftDiagonal = 0, inverted = 0, longDiagonal = 0;
	int32_t customBlendEdgeClass = -1;
	uint32_t flag = BLEND_FLAG_VAL;
};
struct CliffSpec { int32_t tileIndex = 0; float uv[8] = {}; uint8_t flip = 0, mutant = 0; };

struct BlendSpec {
	std::string name;
	int heightVersion = 4;   // HeightMapData version (4: ZH u8, 5: BFME1 u16)
	int version = 8;         // BlendTileData version
	int width = 0, height = 0;
	std::vector<uint16_t> heights;   // width*height
	std::vector<uint16_t> tiles;     // width*height
	std::vector<uint32_t> blends, threeWay, cliff;   // width*height (written 16- or 32-bit by version)
	std::vector<uint8_t> cliffState;                 // rows * bytes per row of the version
	std::vector<uint8_t> masks[4];                   // impassToPlayers, passageWidths, taint, extraPassability
	int32_t numBitmapTiles = 0, numBlendedTiles = 1, numCliffInfo = 1, numEdgeTiles = 0;
	std::vector<TexClassSpec> textureClasses, edgeClasses;
	std::vector<BlendDescSpec> descs;   // numBlendedTiles - 1
	std::vector<CliffSpec> cliffs;      // numCliffInfo - 1 (v >= 5)
	std::vector<uint8_t> trailing;      // bytes appended to the chunk payload

	int bytesPerRow() const { return (width + 7) / 8; }
	int cliffStateBytesPerRow() const { return version == 7 ? (width + 1) / 8 : bytesPerRow(); }
	static bool wideIndices(int v) { return v >= 14 && v < 24; }
	static bool hasMask(int v, int m)
	{
		switch (m) {
		case 0: return v >= 10;
		case 1: return v >= 11;
		case 2: return v >= 14 && v < 25;
		default: return v >= 15;
		}
	}
};

inline std::vector<uint8_t> blendPayload(const BlendSpec& s)
{
	ByteWriter p;
	const int n = s.width * s.height;
	const int v = s.version;
	const bool wide = BlendSpec::wideIndices(v);
	auto idx = [&](const std::vector<uint32_t>& a) {
		for (int i = 0; i < n; ++i) {
			if (wide) p.u32(a[size_t(i)]); else p.u16(uint16_t(a[size_t(i)]));
		}
	};
	p.i32(n);
	for (int i = 0; i < n; ++i) p.u16(s.tiles[size_t(i)]);
	idx(s.blends);
	if (v >= 6) idx(s.threeWay);
	if (v >= 5) idx(s.cliff);
	if (v >= 7) p.raw(s.cliffState.data(), s.cliffState.size());
	for (int m = 0; m < 4; ++m) {
		if (BlendSpec::hasMask(v, m)) p.raw(s.masks[m].data(), s.masks[m].size());
	}
	p.i32(s.numBitmapTiles);
	p.i32(s.numBlendedTiles);
	if (v >= 5) p.i32(s.numCliffInfo);
	p.i32(int32_t(s.textureClasses.size()));
	for (const TexClassSpec& t : s.textureClasses) {
		p.i32(t.firstTile); p.i32(t.numTiles); p.i32(t.width); p.i32(0);
		p.u16(uint16_t(t.name.size())); p.raw(t.name.data(), t.name.size());
	}
	if (v >= 4) {
		p.i32(s.numEdgeTiles);
		p.i32(int32_t(s.edgeClasses.size()));
		for (const TexClassSpec& t : s.edgeClasses) {
			p.i32(t.firstTile); p.i32(t.numTiles); p.i32(t.width);
			p.u16(uint16_t(t.name.size())); p.raw(t.name.data(), t.name.size());
		}
	}
	for (const BlendDescSpec& d : s.descs) {
		p.i32(d.blendNdx);
		p.u8(d.horiz); p.u8(d.vert); p.u8(d.rightDiagonal); p.u8(d.leftDiagonal); p.u8(d.inverted);
		if (v >= 3) p.u8(d.longDiagonal);
		if (v >= 4) p.i32(d.customBlendEdgeClass);
		p.u32(d.flag);
	}
	if (v >= 5) {
		for (const CliffSpec& c : s.cliffs) {
			p.i32(c.tileIndex);
			for (float f : c.uv) { uint32_t b; std::memcpy(&b, &f, 4); p.u32(b); }
			p.u8(c.flip); p.u8(c.mutant);
		}
	}
	p.raw(s.trailing.data(), s.trailing.size());
	return p.bytes;
}

inline std::vector<uint8_t> writeBlendMap(const BlendSpec& s)
{
	bfmeheights::SyntheticMap hm;
	hm.version = s.heightVersion;
	hm.width = s.width;
	hm.height = s.height;
	hm.border = 0;
	hm.samples = s.heights;
	std::vector<uint8_t> f = bfmeheights::writeMapFile(hm);   // CkMp, TOC {WorldInfoDummy=1, HeightMapData=2}
	// Rewrite the TOC with BlendTileData=3 and append the chunk.
	ByteWriter out;
	out.raw("CkMp", 4);
	out.i32(3);
	const char* names[3] = {"WorldInfoDummy", "HeightMapData", "BlendTileData"};
	for (int i = 0; i < 3; ++i) {
		out.u8(uint8_t(std::strlen(names[i])));
		out.raw(names[i], std::strlen(names[i]));
		out.u32(uint32_t(i + 1));
	}
	size_t tocEnd = 8;
	for (int i = 0; i < 2; ++i) tocEnd += 1 + std::strlen(names[i]) + 4;
	out.raw(f.data() + tocEnd, f.size() - tocEnd);
	const std::vector<uint8_t> payload = blendPayload(s);
	out.u32(3); out.u16(uint16_t(s.version)); out.i32(int32_t(payload.size()));
	out.raw(payload.data(), payload.size());
	return out.bytes;
}

// A consistent random spec (no anomalies): tiles inside the bitmap tiles, flags in 0..3, edge classes
// in range, indices inside the tables. `bigIndices` puts blend / cliff indices above 32767 (needs
// enough descriptions, i.e. a wide version).
inline BlendSpec makeBlendSpec(const std::string& name, int version, int w, int h, uint32_t seed,
	int numBlended = 0, int numCliff = 0)
{
	XorShift rng(seed);
	BlendSpec s;
	s.name = name;
	s.version = version;
	s.heightVersion = version >= 9 ? 5 : 4;
	s.width = w;
	s.height = h;
	const int n = w * h;
	s.heights = bfmeheights::makeTerrain(w, h, s.heightVersion >= 5 ? 0xFFFF : 255, seed ^ 0x5A5Au);
	// texture classes: widths 1, 2, 4 (numTiles = width^2), consecutive
	const int widths[] = {2, 1, 4, 2, 1};
	for (int i = 0; i < 5; ++i) {
		TexClassSpec t;
		t.firstTile = s.numBitmapTiles;
		t.width = widths[i];
		t.numTiles = widths[i] * widths[i];
		t.name = "SynthTexture" + std::to_string(i);
		s.numBitmapTiles += t.numTiles;
		s.textureClasses.push_back(t);
	}
	if (version >= 4 && (seed & 1)) {
		for (int i = 0; i < 2; ++i) {
			TexClassSpec t;
			t.firstTile = s.numEdgeTiles;
			t.width = 2;
			t.numTiles = 4;
			t.name = "SynthEdge" + std::to_string(i);
			s.numEdgeTiles += 4;
			s.edgeClasses.push_back(t);
		}
	}
	s.numBlendedTiles = numBlended > 0 ? numBlended : 1 + int(rng.next() % 40);
	s.numCliffInfo = version >= 5 ? (numCliff > 0 ? numCliff : 1 + int(rng.next() % 20)) : 1;
	for (int i = 1; i < s.numBlendedTiles; ++i) {
		BlendDescSpec d;
		d.blendNdx = int32_t(rng.next() % uint32_t(s.numBitmapTiles * 4));
		const uint32_t r = rng.next();
		switch (r % 4) {
		case 0: d.horiz = 1; break;
		case 1: d.vert = 1; break;
		case 2: d.rightDiagonal = 1; break;
		default: d.leftDiagonal = 1; break;
		}
		d.inverted = uint8_t((r >> 4) & 3);
		d.longDiagonal = version >= 3 ? uint8_t((r >> 6) & 1) : 0;
		d.customBlendEdgeClass = (version >= 4 && !s.edgeClasses.empty() && ((r >> 8) % 4 == 0))
			? int32_t((r >> 10) % s.edgeClasses.size()) : -1;
		s.descs.push_back(d);
	}
	for (int i = 1; i < s.numCliffInfo; ++i) {
		CliffSpec c;
		c.tileIndex = int32_t(rng.next() % uint32_t(s.numBitmapTiles * 4));
		for (float& f : c.uv) f = float(rng.next() % 1000) / 999.0f;
		c.flip = uint8_t(rng.next() & 1);
		c.mutant = uint8_t(rng.next() & 1);
		s.cliffs.push_back(c);
	}
	s.tiles.resize(size_t(n));
	s.blends.assign(size_t(n), 0);
	s.threeWay.assign(size_t(n), 0);
	s.cliff.assign(size_t(n), 0);
	for (int i = 0; i < n; ++i) {
		s.tiles[size_t(i)] = uint16_t(rng.next() % uint32_t(s.numBitmapTiles * 4));
		const uint32_t r = rng.next();
		if (r % 3 == 0) s.blends[size_t(i)] = 1 + (r >> 4) % uint32_t(s.numBlendedTiles - 1 > 0 ? s.numBlendedTiles - 1 : 1);
		if (s.numBlendedTiles <= 1) s.blends[size_t(i)] = 0;
		if (version >= 6 && r % 7 == 0 && s.numBlendedTiles > 1) s.threeWay[size_t(i)] = 1 + (r >> 8) % uint32_t(s.numBlendedTiles - 1);
		if (version >= 5 && r % 5 == 0 && s.numCliffInfo > 1) s.cliff[size_t(i)] = 1 + (r >> 12) % uint32_t(s.numCliffInfo - 1);
	}
	s.cliffState.resize(size_t(s.cliffStateBytesPerRow()) * size_t(h));
	for (uint8_t& b : s.cliffState) b = uint8_t(rng.next());
	for (int m = 0; m < 4; ++m) {
		if (!BlendSpec::hasMask(version, m)) continue;
		s.masks[m].resize(size_t(s.bytesPerRow()) * size_t(h));
		for (size_t i = 0; i < s.masks[m].size(); ++i) s.masks[m][i] = uint8_t((rng.next() & 0xF0) | uint32_t(m + 1));
	}
	return s;
}

// ZH layouts v1..v8 compared bit by bit between the upstream and the patched build.
inline std::vector<BlendSpec> zhSyntheticBlendMaps()
{
	struct Size { int w, h; };
	const Size sizes[] = {{2, 2}, {9, 7}, {33, 17}, {64, 64}};
	std::vector<BlendSpec> maps;
	uint32_t seed = 0xB1E0u;
	for (int v = 2; v <= 8; ++v) {
		for (const Size& sz : sizes) {
			maps.push_back(makeBlendSpec("synthetic_zh_blend_v" + std::to_string(v) + "_" + std::to_string(sz.w) + "x" +
				std::to_string(sz.h), v, sz.w, sz.h, seed++));
		}
	}
	return maps;
}

} // namespace bfmeblend
