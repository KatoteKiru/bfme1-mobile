// BFME1 Mobile — PolygonTriggers tests: read a .map out of a BIG archive.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Same EAR\0 + RefPack and BIG4/BIGF walk as tests/heights/src/MapLoader.cpp.
// This copy does not include WorldHeightMap, so the triggers module does not link it.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace bfmetrigger {

struct BigEntry {
	std::string name;
	uint32_t offset = 0;
	uint32_t size = 0;
};

bool readBigDirectory(const std::string& path, std::vector<BigEntry>& entries, std::string& error);
bool readBigEntry(const std::string& path, const BigEntry& entry, std::vector<uint8_t>& out);
bool decompressMap(const std::vector<uint8_t>& in, std::vector<uint8_t>& out, std::string& error);
std::string normalizeBigName(std::string name);

} // namespace bfmetrigger
