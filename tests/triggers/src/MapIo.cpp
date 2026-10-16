// BFME1 Mobile — PolygonTriggers tests
// SPDX-License-Identifier: GPL-3.0-or-later
#include "MapIo.h"

#include "refcodex.h"

#include <cctype>
#include <cstring>
#include <fstream>

namespace bfmetrigger {
namespace {

uint32_t be32(const uint8_t* p)
{
	return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | uint32_t(p[3]);
}

} // namespace

std::string normalizeBigName(std::string s)
{
	for (char& c : s) {
		if (c == '/') c = '\\';
		c = char(std::tolower(static_cast<unsigned char>(c)));
	}
	return s;
}

bool decompressMap(const std::vector<uint8_t>& in, std::vector<uint8_t>& out, std::string& error)
{
	if (in.size() >= 8 && std::memcmp(in.data(), "EAR\0", 4) == 0) {
		const uint32_t size = uint32_t(in[4]) | uint32_t(in[5]) << 8 | uint32_t(in[6]) << 16 | uint32_t(in[7]) << 24;
		if (!REF_is(in.data() + 8)) {
			error = "EAR\\0 without RefPack";
			return false;
		}
		out.assign(size_t(size) + 16, 0);
		int compressed = 0;
		const int got = REF_decode(out.data(), in.data() + 8, &compressed);
		if (got != int(size)) {
			error = "RefPack size mismatch";
			return false;
		}
		out.resize(size);
		return true;
	}
	out = in;
	return true;
}

bool readBigDirectory(const std::string& path, std::vector<BigEntry>& entries, std::string& error)
{
	std::ifstream f(path, std::ios::binary);
	if (!f) {
		error = "cannot open " + path;
		return false;
	}
	uint8_t head[16];
	if (!f.read(reinterpret_cast<char*>(head), 16)) {
		error = "short BIG header";
		return false;
	}
	if (std::memcmp(head, "BIG4", 4) != 0 && std::memcmp(head, "BIGF", 4) != 0) {
		error = "not a BIG4/BIGF archive";
		return false;
	}
	const uint32_t count = be32(head + 8);
	const uint32_t headerLen = be32(head + 12);
	if (headerLen < 16 || headerLen > (64u << 20)) {
		error = "bad BIG header length";
		return false;
	}
	std::vector<uint8_t> h(headerLen);
	std::memcpy(h.data(), head, 16);
	if (!f.read(reinterpret_cast<char*>(h.data() + 16), headerLen - 16)) {
		error = "short BIG directory";
		return false;
	}
	size_t p = 16;
	for (uint32_t i = 0; i < count; ++i) {
		if (p + 9 > h.size()) {
			error = "truncated BIG directory";
			return false;
		}
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

bool readBigEntry(const std::string& path, const BigEntry& entry, std::vector<uint8_t>& out)
{
	std::ifstream f(path, std::ios::binary);
	if (!f) return false;
	f.seekg(std::streamoff(entry.offset));
	out.resize(entry.size);
	return bool(f.read(reinterpret_cast<char*>(out.data()), std::streamsize(entry.size)));
}

} // namespace bfmetrigger
