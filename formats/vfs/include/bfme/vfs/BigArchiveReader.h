// BFME1 Mobile — formats/vfs
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Engine-independent reader for the BIG archive directory (BIGF from Generals/ZH and BIG4 from
// BFME1). It is the reference implementation the tests check the engine's StdBIGFileSystem
// against, and the base for tools (datacheck). It never decompresses entries: RefPack (EAR\0)
// stays the business of the map loader (CompressionManager), see formats/vfs/README.md.
//
// Layout (both signatures, verified against OpenSAGE BigArchive on BFME1 ini.big / maps.big):
//   0x00  char[4]  "BIGF" | "BIG4"
//   0x04  u32 LE   archive size in bytes (the engine ignores it)
//   0x08  u32 BE   number of entries
//   0x0C  u32 BE   offset of the first entry's data (= end of directory; the engine ignores it)
//   0x10  entries: u32 BE offset, u32 BE size, NUL-terminated path ('\' separators)
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace bfme::vfs {

enum class BigFormat { Unknown, BIGF, BIG4 };

const char* bigFormatName(BigFormat format);
BigFormat classifyBigSignature(const unsigned char signature[4]);

struct BigHeader {
	BigFormat format = BigFormat::Unknown;
	uint32_t archiveSize = 0;      // as stored (little-endian field)
	uint32_t entryCount = 0;       // big-endian field
	uint32_t firstEntryOffset = 0; // big-endian field
};

struct BigEntry {
	std::string path;  // as stored in the archive
	uint32_t offset = 0;
	uint32_t size = 0;
};

struct BigDirectory {
	BigHeader header;
	std::vector<BigEntry> entries;  // in directory order
};

// Parses header + directory from a memory block that holds at least the directory.
// fileSize is the archive length. Every entry must lie inside it, including when
// fileSize is 0: a zero length used to skip the check and accept any offset.
// reserve() is capped at (length - 16) / 9 so a huge entry count cannot bad_alloc.
bool parseBigDirectory(const unsigned char* data, size_t length, uint64_t fileSize, BigDirectory& out, std::string* error);

// Reads header + directory from a file on disk (does not read entry payloads).
bool readBigDirectory(const std::string& archivePath, BigDirectory& out, std::string* error);

// Reads the raw (stored, never decompressed) bytes of one entry.
bool readBigEntry(const std::string& archivePath, const BigEntry& entry, std::vector<unsigned char>& out, std::string* error);

// False for an empty path, an absolute path ('/', '\\', or a drive letter), or any '..' segment.
// '.' segments are allowed. The parser still accepts such names; writers must refuse them.
bool entryPathSafeForDisk(const std::string& path);

// Writes one entry under destDir. Rejects absolute paths and '..' (does not create the file).
bool extractBigEntry(const std::string& archivePath, const BigEntry& entry, const std::string& destDir, std::string* error);

} // namespace bfme::vfs
