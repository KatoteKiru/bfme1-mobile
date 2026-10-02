// BFME1 Mobile — tests (E1-25: BlendTileData v14/v15)
// SPDX-License-Identifier: GPL-3.0-or-later
//
// blend_zh_dump: built twice, against the upstream tree (Short indices, fixed arrays) and against this
// tree (Int indices, heap tables, v9..v15). Loads the ZH maps (synthetic BlendTileData v2..v8 from
// SyntheticBlend.h plus every *.map under --maps-dir) through the real parser, with
// m_use3WayTerrainBlends 1 and 0, and writes everything the parser stored plus what the render-side
// consumers (getAlphaUVData, getExtraAlphaUVData, getUVData, getTextureClass, ...) return per cell.
// cmake/compare_dumps.cmake requires byte-identical output.
#include "../../heights/src/MapLoader.h"
#include "BlendLoader.h"
#include "SyntheticBlend.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

using namespace bfmeblend;

namespace {

struct Out {
	std::vector<uint8_t> bytes;
	void u32(uint32_t v) { for (int i = 0; i < 4; ++i) bytes.push_back(uint8_t(v >> (8 * i))); }
	void i32(int32_t v) { u32(uint32_t(v)); }
	void str(const std::string& s) { u32(uint32_t(s.size())); bytes.insert(bytes.end(), s.begin(), s.end()); }
	void f32(float f) { uint32_t b; std::memcpy(&b, &f, 4); u32(b); }
	void raw(const std::vector<uint8_t>& v) { u32(uint32_t(v.size())); bytes.insert(bytes.end(), v.begin(), v.end()); }
	template <typename T> void ints(const std::vector<T>& v) { u32(uint32_t(v.size())); for (T x : v) i32(int32_t(x)); }
	void classes(const std::vector<TexClass>& v)
	{
		u32(uint32_t(v.size()));
		for (const TexClass& t : v) { i32(t.firstTile); i32(t.numTiles); i32(t.width); str(t.name); }
	}
};

bool dumpOne(Out& out, const std::string& name, const std::vector<uint8_t>& file, size_t& cells, size_t& blends)
{
	out.str(name);
	for (int use3Way : {1, 0}) {
		LoadOptions opt;
		opt.use3WayTerrainBlends = use3Way;
		opt.consumers = true;
		LoadedBlend b = loadBlend(file, opt);
		out.u32(b.ok ? 1u : 0u);
		if (!b.ok) {
			std::fprintf(stderr, "blend_zh_dump: %s: %s\n", name.c_str(), b.error.c_str());
			return false;
		}
		if (b.newTree && (b.stats.danglingTiles || b.stats.flagsOutside0x3 || b.stats.edgeClassOutOfRange)) {
			// a legitimate difference (E1-25 decisions); the ZH maps are expected to have none
			std::fprintf(stderr, "blend_zh_dump: %s: anomalies on a ZH map (tiles %d, flags %d, edge %d)\n", name.c_str(),
				b.stats.danglingTiles, b.stats.flagsOutside0x3, b.stats.edgeClassOutOfRange);
			return false;
		}
		out.i32(b.heightVersion); out.i32(b.blendVersion);
		out.i32(b.width); out.i32(b.height); out.i32(b.dataSize); out.i32(b.bytesPerRow);
		out.u32(b.atEndOfChunk ? 1u : 0u);
		out.i32(b.numBitmapTiles); out.i32(b.numBlendedTiles); out.i32(b.numCliffInfo); out.i32(b.numEdgeTiles);
		out.ints(b.tiles); out.ints(b.blends); out.ints(b.threeWay); out.ints(b.cliff);
		out.raw(b.cliffState);
		out.classes(b.textureClasses);
		out.classes(b.edgeClasses);
		out.u32(uint32_t(b.descs.size()));
		for (const BlendDesc& d : b.descs) {
			out.i32(d.blendNdx); out.i32(d.horiz); out.i32(d.vert); out.i32(d.rightDiagonal); out.i32(d.leftDiagonal);
			out.i32(d.inverted); out.i32(d.longDiagonal); out.i32(d.customBlendEdgeClass);
		}
		out.u32(uint32_t(b.cliffs.size()));
		for (const CliffInfo& c : b.cliffs) {
			out.i32(c.tileIndex);
			for (float f : c.uv) out.f32(f);
			out.i32(c.flip); out.i32(c.mutant);
		}
		out.u32(uint32_t(b.consumers.size()));
		for (const CellConsumers& c : b.consumers) {
			for (int k = 0; k < 4; ++k) { out.f32(c.alphaU[k]); out.f32(c.alphaV[k]); out.u32(c.alpha[k]); }
			out.i32(c.alphaFlip);
			out.i32(c.extraOk);
			for (int k = 0; k < 4; ++k) { out.f32(c.extraU[k]); out.f32(c.extraV[k]); out.u32(c.extraAlpha[k]); }
			out.i32(c.extraFlip); out.i32(c.extraCliff);
			out.i32(c.uvOk);
			for (int k = 0; k < 4; ++k) { out.f32(c.u[k]); out.f32(c.v[k]); }
			out.i32(c.textureClass); out.i32(c.baseTextureClass); out.i32(c.cliffMapped); out.i32(c.cliffState); out.i32(c.flipState);
		}
		if (use3Way == 1) {
			cells += size_t(b.dataSize);
			blends += b.descs.size();
		}
	}
	return true;
}

} // namespace

int main(int argc, char** argv)
{
	std::string outPath, mapsDir;
	for (int i = 1; i < argc; ++i) {
		if (!std::strcmp(argv[i], "--out") && i + 1 < argc) outPath = argv[++i];
		else if (!std::strcmp(argv[i], "--maps-dir") && i + 1 < argc) mapsDir = argv[++i];
		else { std::fprintf(stderr, "usage: blend_zh_dump --out FILE [--maps-dir DIR]\n"); return 2; }
	}
	if (outPath.empty()) { std::fprintf(stderr, "blend_zh_dump: --out required\n"); return 2; }

	Out out;
	size_t maps = 0, fileMaps = 0, cells = 0, blends = 0;
	bool ok = true;
	for (const BlendSpec& s : zhSyntheticBlendMaps()) {
		ok = dumpOne(out, s.name, writeBlendMap(s), cells, blends) && ok;
		++maps;
	}
	if (!mapsDir.empty() && std::filesystem::is_directory(mapsDir)) {
		std::vector<std::filesystem::path> files;
		for (const auto& e : std::filesystem::recursive_directory_iterator(mapsDir)) {
			std::string ext = e.path().extension().string();
			std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return char(std::tolower(c)); });
			if (e.is_regular_file() && ext == ".map") files.push_back(e.path());
		}
		std::sort(files.begin(), files.end());
		for (const auto& p : files) {
			std::vector<uint8_t> bytes;
			if (!bfmeheights::readFile(p.string(), bytes)) { std::fprintf(stderr, "cannot read %s\n", p.string().c_str()); ok = false; continue; }
			ok = dumpOne(out, std::filesystem::relative(p, mapsDir).generic_string(), bytes, cells, blends) && ok;
			++maps;
			++fileMaps;
		}
	}
	FILE* f = std::fopen(outPath.c_str(), "wb");
	if (!f) { std::fprintf(stderr, "cannot write %s\n", outPath.c_str()); return 1; }
	std::fwrite(out.bytes.data(), 1, out.bytes.size(), f);
	std::fclose(f);
	std::printf("blend_zh_dump: maps=%zu (files %zu) cells=%zu blend-descriptions=%zu bytes=%zu\n",
		maps, fileMaps, cells, blends, out.bytes.size());
	return ok ? 0 : 1;
}
