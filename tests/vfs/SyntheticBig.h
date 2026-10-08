// BFME1 Mobile — tests
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Generates BIG archives in the test (no EA data in git). Layout as documented in
// formats/vfs/include/bfme/vfs/BigArchiveReader.h; the size-field endianness can be flipped to
// prove the tests notice a wrong layout.
#pragma once

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

namespace bfmetest {

struct SyntheticEntry {
	std::string path;  // stored as is ('\' separators, like EA archives)
	std::string data;
};

struct SyntheticBigOptions {
	std::string signature = "BIG4";
	bool archiveSizeBigEndian = false;  // BFME1 and ZH store it little-endian
};

inline void putLE32(std::string& s, uint32_t v)
{
	for (int i = 0; i < 4; ++i) s += char((v >> (8 * i)) & 0xff);
}

inline void putBE32(std::string& s, uint32_t v)
{
	for (int i = 3; i >= 0; --i) s += char((v >> (8 * i)) & 0xff);
}

inline std::string buildBig(const std::vector<SyntheticEntry>& entries, const SyntheticBigOptions& opt = {})
{
	uint32_t directorySize = 0x10;
	for (const SyntheticEntry& e : entries) directorySize += 8 + uint32_t(e.path.size()) + 1;
	uint32_t total = directorySize;
	for (const SyntheticEntry& e : entries) total += uint32_t(e.data.size());

	std::string out = opt.signature.substr(0, 4);
	if (opt.archiveSizeBigEndian) putBE32(out, total); else putLE32(out, total);
	putBE32(out, uint32_t(entries.size()));
	putBE32(out, directorySize);
	uint32_t offset = directorySize;
	for (const SyntheticEntry& e : entries) {
		putBE32(out, offset);
		putBE32(out, uint32_t(e.data.size()));
		out += e.path;
		out += '\0';
		offset += uint32_t(e.data.size());
	}
	for (const SyntheticEntry& e : entries) out += e.data;
	return out;
}

inline void writeFile(const std::filesystem::path& path, const std::string& bytes)
{
	std::filesystem::create_directories(path.parent_path());
	std::ofstream f(path, std::ios::binary);
	f.write(bytes.data(), std::streamsize(bytes.size()));
	if (!f) throw std::runtime_error("cannot write " + path.string());
}

inline void writeBig(const std::filesystem::path& path, const std::vector<SyntheticEntry>& entries, const SyntheticBigOptions& opt = {})
{
	writeFile(path, buildBig(entries, opt));
}

// Unique scratch directory under the system temp dir, removed on destruction.
class TempDir {
public:
	TempDir()
	{
		std::random_device rd;
		path_ = std::filesystem::temp_directory_path() / ("bfme-vfs-test-" + std::to_string(rd()) + std::to_string(rd()));
		std::filesystem::create_directories(path_);
	}
	~TempDir()
	{
		std::error_code ec;
		std::filesystem::remove_all(path_, ec);
	}
	const std::filesystem::path& path() const { return path_; }
	std::string str() const { return path_.string(); }

private:
	std::filesystem::path path_;
};

} // namespace bfmetest
