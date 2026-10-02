// BFME1 Mobile — tests (E1-25: 16-bit terrain heights)
// SPDX-License-Identifier: GPL-3.0-or-later
#include "PreRTS.h"

#include "Common/DataChunk.h"
#include "Common/MapObject.h"
#include "Common/MapReaderWriterInfo.h"
#include "W3DDevice/GameClient/WorldHeightMap.h"

#include "codex.h"
#include "refcodex.h"

#include "MapLoader.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>

namespace bfmeheights {
namespace {

// In-memory equivalent of CachedFileInputStream (which needs TheFileSystem).
class MemoryChunkStream : public ChunkInputStream {
public:
	explicit MemoryChunkStream(const std::vector<uint8_t>& data) : m_data(data) {}
	Int read(void* pData, Int numBytes) override
	{
		Int n = numBytes;
		if (n < 0) n = 0;
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
	Bool eof() override { return m_pos >= m_data.size(); }  // as CachedFileInputStream::eof

private:
	const std::vector<uint8_t>& m_data;
	size_t m_pos = 0;
};

// Version seen by the chunk parser (wraps the engine's parse callback).
struct ParseContext {
	WorldHeightMap* map = nullptr;
	int version = -1;
};

class TestHeightMap : public WorldHeightMap {
public:
	TestHeightMap() : WorldHeightMap() {}

	static Bool parseFull(DataChunkInput& file, DataChunkInfo* info, void* userData)
	{
		ParseContext* ctx = (ParseContext*)userData;
		ctx->version = info->version;
		return ParseHeightMapDataChunk(file, info, ctx->map);
	}
	static Bool parseSizeOnly(DataChunkInput& file, DataChunkInfo* info, void* userData)
	{
		ParseContext* ctx = (ParseContext*)userData;
		ctx->version = info->version;
		return ParseSizeOnlyInChunk(file, info, ctx->map);
	}

	bool load(ChunkInputStream* stream, ParseMode mode, int& version)
	{
		ParseContext ctx;
		ctx.map = this;
		DataChunkInput file(stream);
		if (!file.isValidFileType()) return false;
		file.registerParser("HeightMapData", AsciiString::TheEmptyString,
			mode == ParseMode::Full ? &TestHeightMap::parseFull : &TestHeightMap::parseSizeOnly);
		const bool ok = file.parse(&ctx);
		version = ctx.version;
		return ok && m_data != nullptr;
	}

	// Upstream allocates the cliff bits in ParseBlendTileData (not parsed here) and then calls
	// initCliffFlagsFromHeights() when the map has no cliff info; do just that.
	void computeCliffFlags()
	{
		m_flipStateWidth = (m_width + 7) / 8;
		delete[] m_cellCliffState;
		m_cellCliffState = new UnsignedByte[size_t(m_flipStateWidth) * size_t(m_height)];
		std::memset(m_cellCliffState, 0, size_t(m_flipStateWidth) * size_t(m_height));
		initCliffFlagsFromHeights();
	}

	int width() const { return m_width; }
	int height() const { return m_height; }
	int border() const { return m_borderSize; }
	int dataSize() const { return m_dataSize; }
};

} // namespace

float mapHeightScale() { return MAP_HEIGHT_SCALE; }

int headerVersion5()
{
#ifdef K_HEIGHT_MAP_VERSION_5
	return 1;
#else
	return 0;
#endif
}

LoadedHeights loadHeights(const std::vector<uint8_t>& bytes, ParseMode mode, bool withCliffs)
{
	LoadedHeights r;
	std::vector<uint8_t> plain;
	if (!decompressMap(bytes, plain, r.error)) return r;
	TestHeightMap* map = new TestHeightMap();
	try {
		MemoryChunkStream stream(plain);
		if (!map->load(&stream, mode, r.version)) {
			r.error = "DataChunkInput/HeightMapData parse failed";
		} else {
			r.ok = true;
		}
	} catch (...) {
		r.error = "exception from the engine parser";
	}
	if (r.ok) {
		r.width = map->width();
		r.height = map->height();
		r.border = map->border();
		r.dataSize = map->dataSize();
		const int count = std::min(r.dataSize, r.width * r.height);
		r.raw.resize(size_t(count));
		r.world.resize(size_t(count));
		for (int i = 0; i < count; ++i) {
			const int x = i % r.width, y = i / r.width;
			r.raw[size_t(i)] = map->getHeight(x, y);
			r.world[size_t(i)] = map->getHeight(x, y) * MAP_HEIGHT_SCALE;   // same expression as the engine
		}
		r.outOfRange[0] = map->getHeight(-1, 0);
		r.outOfRange[1] = map->getHeight(0, -1);
		r.outOfRange[2] = map->getHeight(r.width, r.height - 1);
		r.outOfRange[3] = map->getHeight(0, r.height);
		if (withCliffs && r.width > 1 && r.height > 1) {
			map->computeCliffFlags();
			r.cliff.resize(size_t(r.width - 1) * size_t(r.height - 1));
			for (int y = 0; y < r.height - 1; ++y)
				for (int x = 0; x < r.width - 1; ++x)
					r.cliff[size_t(y) * size_t(r.width - 1) + size_t(x)] = map->getCliffState(x, y) ? 1 : 0;
		}
	}
	map->Release_Ref();
	return r;
}

bool decompressMap(const std::vector<uint8_t>& in, std::vector<uint8_t>& out, std::string& error)
{
	if (in.size() >= 8 && std::memcmp(in.data(), "EAR\0", 4) == 0) {
		const uint32_t size = uint32_t(in[4]) | uint32_t(in[5]) << 8 | uint32_t(in[6]) << 16 | uint32_t(in[7]) << 24;
		if (!REF_is(in.data() + 8)) { error = "EAR\\0 without RefPack"; return false; }
		out.assign(size_t(size) + 16, 0);
		int compressed = 0;
		const int got = REF_decode(out.data(), in.data() + 8, &compressed);
		if (got != int(size)) { error = "RefPack size mismatch"; return false; }
		out.resize(size);
		return true;
	}
	out = in;
	return true;
}

bool readFile(const std::string& path, std::vector<uint8_t>& out)
{
	std::ifstream f(path, std::ios::binary);
	if (!f) return false;
	out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
	return true;
}

std::string normalizeBigName(std::string s)
{
	for (char& c : s) {
		if (c == '/') c = '\\';
		c = char(std::tolower((unsigned char)c));
	}
	return s;
}

static uint32_t be32(const uint8_t* p) { return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | uint32_t(p[3]); }

bool readBigDirectory(const std::string& path, std::vector<BigEntry>& entries, std::string& error)
{
	std::ifstream f(path, std::ios::binary);
	if (!f) { error = "cannot open " + path; return false; }
	uint8_t head[16];
	if (!f.read((char*)head, 16)) { error = "short BIG header"; return false; }
	if (std::memcmp(head, "BIG4", 4) != 0 && std::memcmp(head, "BIGF", 4) != 0) { error = "not a BIG4/BIGF archive"; return false; }
	const uint32_t count = be32(head + 8), headerLen = be32(head + 12);
	if (headerLen < 16 || headerLen > (64u << 20)) { error = "bad BIG header length"; return false; }
	std::vector<uint8_t> h(headerLen);
	std::memcpy(h.data(), head, 16);
	if (!f.read((char*)h.data() + 16, headerLen - 16)) { error = "short BIG directory"; return false; }
	size_t p = 16;
	for (uint32_t i = 0; i < count; ++i) {
		if (p + 9 > h.size()) { error = "truncated BIG directory"; return false; }
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

bool readBigEntry(const std::string& path, const BigEntry& e, std::vector<uint8_t>& out)
{
	std::ifstream f(path, std::ios::binary);
	if (!f) return false;
	f.seekg(std::streamoff(e.offset));
	out.resize(e.size);
	return bool(f.read((char*)out.data(), e.size));
}

} // namespace bfmeheights
