// BFME1 Mobile — tests (E1-25: BlendTileData v14/v15)
// SPDX-License-Identifier: GPL-3.0-or-later
#include "PreRTS.h"

#include "Common/DataChunk.h"
#include "Common/GlobalData.h"
#include "Common/MapReaderWriterInfo.h"
#include "W3DDevice/GameClient/WorldHeightMap.h"

#include "../../heights/src/MapLoader.h"
#include "BlendLoader.h"

#include <cstring>

namespace bfmeblend {
namespace {

class MemoryChunkStream : public ChunkInputStream {   // as tests/heights MapLoader.cpp
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

struct Ctx {
	WorldHeightMap* map = nullptr;
	MemoryChunkStream* stream = nullptr;
	LoadedBlend* out = nullptr;
};

class TestBlendMap : public WorldHeightMap {
public:
	TestBlendMap() : WorldHeightMap() {}

	static Bool parseHeights(DataChunkInput& file, DataChunkInfo* info, void* userData)
	{
		Ctx* c = (Ctx*)userData;
		c->out->heightVersion = info->version;
		return ParseHeightMapDataChunk(file, info, c->map);
	}
	static Bool parseBlend(DataChunkInput& file, DataChunkInfo* info, void* userData)
	{
		Ctx* c = (Ctx*)userData;
		c->out->blendVersion = info->version;
		c->out->payloadOffset = c->stream->tell();
		c->out->payloadSize = size_t(file.getChunkDataSize());
		const Bool ok = ParseBlendTileDataChunk(file, info, c->map);
		c->out->atEndOfChunk = file.atEndOfChunk();
		return ok;
	}

	bool load(MemoryChunkStream* stream, LoadedBlend& out)
	{
		Ctx c;
		c.map = this;
		c.stream = stream;
		c.out = &out;
		DataChunkInput file(stream);
		if (!file.isValidFileType()) return false;
		file.registerParser("HeightMapData", AsciiString::TheEmptyString, &TestBlendMap::parseHeights);
		file.registerParser("BlendTileData", AsciiString::TheEmptyString, &TestBlendMap::parseBlend);
		return file.parse(&c) && m_data != nullptr && m_tileNdxes != nullptr;
	}

	// WorldHeightMap(ChunkInputStream*, false) after file.parse(): "patch bad maps" (unchanged upstream
	// loop, repeated here because the test calls the chunk parsers directly).
	void patchBadMaps()
	{
		for (Int i=0; i<m_dataSize; i++) {
			if (m_cliffInfoNdxes[i]<0 || m_cliffInfoNdxes[i]>= m_numCliffInfo) m_cliffInfoNdxes[i] = 0;
			if (m_blendTileNdxes[i]<0 || m_blendTileNdxes[i]>= m_numBlendedTiles) m_blendTileNdxes[i] = 0;
			if (m_extraBlendTileNdxes[i]<0 || m_extraBlendTileNdxes[i]>= m_numBlendedTiles) m_extraBlendTileNdxes[i] = 0;
		}
	}

	void copyOut(LoadedBlend& r, const LoadOptions& opt)
	{
		const int v = r.blendVersion;
		r.width = m_width;
		r.height = m_height;
		r.dataSize = m_dataSize;
		r.bytesPerRow = m_flipStateWidth;
		r.numBitmapTiles = m_numBitmapTiles;
		r.numBlendedTiles = m_numBlendedTiles;
		r.numCliffInfo = m_numCliffInfo;
		r.numEdgeTiles = m_numEdgeTiles;
		const size_t n = size_t(m_dataSize);
		r.tiles.assign(m_tileNdxes, m_tileNdxes + n);
		r.blends.assign(m_blendTileNdxes, m_blendTileNdxes + n);
		// upstream leaves these uninitialised below v6 / v5
		if (v >= K_BLEND_TILE_VERSION_6) r.threeWay.assign(m_extraBlendTileNdxes, m_extraBlendTileNdxes + n);
		if (v >= K_BLEND_TILE_VERSION_5) r.cliff.assign(m_cliffInfoNdxes, m_cliffInfoNdxes + n);
		r.cliffState.assign(m_cellCliffState, m_cellCliffState + size_t(m_flipStateWidth) * size_t(m_height));
		for (int i = 0; i < m_numTextureClasses; ++i) {
			const TXTextureClass& t = m_textureClasses[i];
			r.textureClasses.push_back({t.firstTile, t.numTiles, t.width, t.name.str()});
		}
		for (int i = 0; i < m_numEdgeTextureClasses; ++i) {
			const TXTextureClass& t = m_edgeTextureClasses[i];
			r.edgeClasses.push_back({t.firstTile, t.numTiles, t.width, t.name.str()});
		}
		for (int i = 1; i < m_numBlendedTiles; ++i) {   // entry 0 is never written by the parser
			const TBlendTileInfo& b = m_blendedTiles[i];
			r.descs.push_back({b.blendNdx, b.horiz, b.vert, b.rightDiagonal, b.leftDiagonal, b.inverted, b.longDiagonal,
				b.customBlendEdgeClass});
		}
		if (v >= K_BLEND_TILE_VERSION_5) {
			for (int i = 1; i < m_numCliffInfo; ++i) {
				const TCliffInfo& c = m_cliffInfo[i];
				CliffInfo o;
				o.tileIndex = c.tileIndex;
				const Real uv[8] = {c.u0, c.v0, c.u1, c.v1, c.u2, c.v2, c.u3, c.v3};
				for (int k = 0; k < 8; ++k) o.uv[k] = uv[k];
				o.flip = c.flip;
				o.mutant = c.mutant;
				r.cliffs.push_back(o);
			}
		}
#ifdef K_BLEND_TILE_VERSION_MAX
		r.newTree = true;
		const BlendTileDataStats& s = getBlendTileDataStats();
		r.stats = {s.version, s.danglingTiles, s.flagsOutside0x3, s.edgeClassWithoutClasses, s.edgeClassOutOfRange,
			s.dirBytesNot01, s.danglingBlend, s.dangling3Way, s.danglingCliff};
		for (int m = 0; m < BLEND_MASK_COUNT; ++m) {
			const UnsignedByte* p = getPassabilityMask(BlendPassabilityMask(m));
			r.hasMask[m] = p != nullptr;
			if (p) r.masks[m].assign(p, p + size_t(getMaskBytesPerRow()) * size_t(m_height));
		}
		r.tileCapacity = m_tileCapacity;
#endif
		// below v6 upstream leaves the 3-way (and below v5 the cliff) index arrays uninitialised and the
		// consumers read them, so the before/after comparison would compare garbage
		if (opt.consumers && v >= K_BLEND_TILE_VERSION_6) {
			patchBadMaps();
			r.consumers.resize(n);
			for (Int y = 0; y < m_height; ++y) {
				for (Int x = 0; x < m_width; ++x) {
					CellConsumers& c = r.consumers[size_t(y) * size_t(m_width) + size_t(x)];
					std::memset(&c, 0, sizeof(c));
					Bool flip = false, cliff = false;
					getAlphaUVData(x, y, c.alphaU, c.alphaV, c.alpha, &flip);
					c.alphaFlip = flip;
					flip = false;
					c.extraOk = getExtraAlphaUVData(x, y, c.extraU, c.extraV, c.extraAlpha, &flip, &cliff);
					c.extraFlip = flip;
					c.extraCliff = cliff;
					c.uvOk = getUVData(x, y, c.u, c.v);
					c.textureClass = getTextureClass(x, y, false);
					c.baseTextureClass = getTextureClass(x, y, true);
					c.cliffMapped = isCliffMappedTexture(x, y);
					c.cliffState = getCliffState(x, y);
					c.flipState = getFlipState(x, y);
				}
			}
		}
	}
};

} // namespace

LoadedBlend loadBlend(const std::vector<uint8_t>& bytes, const LoadOptions& opt, std::vector<uint8_t>* plainOut)
{
	LoadedBlend r;
	std::vector<uint8_t> plain;
	if (!bfmeheights::decompressMap(bytes, plain, r.error)) return r;
	TheWritableGlobalData->m_use3WayTerrainBlends = opt.use3WayTerrainBlends;
	TestBlendMap* map = new TestBlendMap();
	try {
		MemoryChunkStream stream(plain);
		if (!map->load(&stream, r)) {
			r.error = "DataChunkInput / BlendTileData parse failed";
		} else {
			r.ok = true;
		}
	} catch (...) {
		r.error = "exception from the engine parser";
	}
	if (r.ok) map->copyOut(r, opt);
	map->Release_Ref();
	if (plainOut) *plainOut = std::move(plain);
	return r;
}

} // namespace bfmeblend
