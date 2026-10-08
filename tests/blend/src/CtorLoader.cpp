// BFME1 Mobile — tests (E1-25 review #10)
// SPDX-License-Identifier: GPL-3.0-or-later
#include "PreRTS.h"

#include "Common/DataChunk.h"
#include "Common/Errors.h"
#include "Common/GlobalData.h"
#include "W3DDevice/GameClient/WorldHeightMap.h"

#include "CtorLoader.h"

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

struct AlphaTilesAccess : public WorldHeightMap {
	static void release()
	{
		for (Int i = 0; i < NUM_ALPHA_TILES; ++i) REF_PTR_RELEASE(m_alphaTiles[i]);
	}
};

} // namespace

void releaseStaticAlphaTiles() { AlphaTilesAccess::release(); }

unsigned errorCorruptFileFormat() { return unsigned(ERROR_CORRUPT_FILE_FORMAT); }
unsigned errorOutOfMemory() { return unsigned(ERROR_OUT_OF_MEMORY); }

CtorResult constructMap(const std::vector<uint8_t>& plain, bool logicalDataOnly)
{
	CtorResult r;
	TheWritableGlobalData->m_use3WayTerrainBlends = 1;   // upstream default (GlobalData.cpp)
	MemoryChunkStream stream(plain);
	WorldHeightMap* map = nullptr;
	try {
		map = new WorldHeightMap(&stream, logicalDataOnly);
		r.constructed = true;
	} catch (ErrorCode e) {
		r.threw = true;
		r.errorCode = true;
		r.error = unsigned(e);
	} catch (...) {
		r.threw = true;
	}
	if (map) {
		const BlendTileDataStats& s = map->getBlendTileDataStats();
		r.stats = {s.version, s.danglingTiles, s.flagsOutside0x3, s.edgeClassWithoutClasses, s.edgeClassOutOfRange,
			s.dirBytesNot01, s.danglingBlend, s.dangling3Way, s.danglingCliff};
		r.width = map->getXExtent();
		r.height = map->getYExtent();
		map->Release_Ref();
	}
	return r;
}

} // namespace bfmeblend
