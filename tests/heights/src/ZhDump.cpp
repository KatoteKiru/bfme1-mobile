// BFME1 Mobile — tests (E1-25: 16-bit terrain heights)
// SPDX-License-Identifier: GPL-3.0-or-later
//
// heights_zh_dump: the same source is built twice, against the upstream tree (u8 heights,
// baseline) and against this tree (u16 heights). It loads the ZH maps (synthetic v1..v4 set from
// SyntheticMaps.h plus, optionally, every *.map under --maps-dir) through the real engine parser and
// writes what the engine derives from heights: the world height getHeight()*MAP_HEIGHT_SCALE of every
// sample (float bits, full parse and size-only parse), the pathfinding cliff flags and the
// out-of-range getHeight values. Raw samples are NOT written: they differ by design (u8 vs u8<<4).
// cmake/compare_dumps.cmake requires the two outputs to be byte-identical.
#include "MapLoader.h"
#include "SyntheticMaps.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

using namespace bfmeheights;

namespace {

struct Out {
	std::vector<uint8_t> bytes;
	void u32(uint32_t v) { for (int i = 0; i < 4; ++i) bytes.push_back(uint8_t(v >> (8 * i))); }
	void str(const std::string& s) { u32(uint32_t(s.size())); bytes.insert(bytes.end(), s.begin(), s.end()); }
	void f32(float f) { uint32_t b; std::memcpy(&b, &f, 4); u32(b); }
};

bool dumpOne(Out& out, const std::string& name, const std::vector<uint8_t>& file, size_t& cells, size_t& cliffs)
{
	out.str(name);
	const ParseMode modes[2] = {ParseMode::Full, ParseMode::SizeOnly};
	for (ParseMode mode : modes) {
		LoadedHeights h = loadHeights(file, mode, mode == ParseMode::Full);
		out.u32(h.ok ? 1u : 0u);
		if (!h.ok) {
			std::fprintf(stderr, "heights_zh_dump: %s: %s\n", name.c_str(), h.error.c_str());
			return false;
		}
		out.u32(uint32_t(h.version));
		out.u32(uint32_t(h.width));
		out.u32(uint32_t(h.height));
		out.u32(uint32_t(h.border));
		out.u32(uint32_t(h.dataSize));
		out.u32(uint32_t(h.world.size()));
		for (float w : h.world) out.f32(w);
		for (int v : h.outOfRange) out.f32(v * mapHeightScale());
		out.u32(uint32_t(h.cliff.size()));
		for (uint8_t c : h.cliff) out.bytes.push_back(c);
		if (mode == ParseMode::Full) {
			cells += h.world.size();
			cliffs += size_t(std::count(h.cliff.begin(), h.cliff.end(), uint8_t(1)));
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
		else { std::fprintf(stderr, "usage: heights_zh_dump --out FILE [--maps-dir DIR]\n"); return 2; }
	}
	if (outPath.empty()) { std::fprintf(stderr, "heights_zh_dump: --out required\n"); return 2; }

	Out out;
	size_t maps = 0, cells = 0, cliffs = 0;
	bool ok = true;
	for (const SyntheticMap& m : zhSyntheticMaps()) {
		ok = dumpOne(out, m.name, writeMapFile(m), cells, cliffs) && ok;
		++maps;
	}
	size_t fileMaps = 0;
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
			if (!readFile(p.string(), bytes)) { std::fprintf(stderr, "cannot read %s\n", p.string().c_str()); ok = false; continue; }
			ok = dumpOne(out, std::filesystem::relative(p, mapsDir).generic_string(), bytes, cells, cliffs) && ok;
			++maps;
			++fileMaps;
		}
	}
	FILE* f = std::fopen(outPath.c_str(), "wb");
	if (!f) { std::fprintf(stderr, "cannot write %s\n", outPath.c_str()); return 1; }
	std::fwrite(out.bytes.data(), 1, out.bytes.size(), f);
	std::fclose(f);
	std::printf("heights_zh_dump: MAP_HEIGHT_SCALE=%.9g u16-tree=%d maps=%zu (files %zu) samples=%zu cliff-cells=%zu bytes=%zu\n",
		(double)mapHeightScale(), headerVersion5(), maps, fileMaps, cells, cliffs, out.bytes.size());
	return ok ? 0 : 1;
}
