// BFME1 Mobile — formats/vfs
// SPDX-License-Identifier: GPL-3.0-or-later
#include "bfme/vfs/BigArchiveReader.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace bfme::vfs {

namespace {

uint32_t readLE32(const unsigned char* p)
{
	return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}

uint32_t readBE32(const unsigned char* p)
{
	return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | uint32_t(p[3]);
}

void setError(std::string* error, const std::string& text)
{
	if (error != nullptr) {
		*error = text;
	}
}

constexpr size_t kHeaderSize = 0x10;
constexpr size_t kMaxPath = 1024;

} // namespace

const char* bigFormatName(BigFormat format)
{
	switch (format) {
	case BigFormat::BIGF: return "BIGF";
	case BigFormat::BIG4: return "BIG4";
	default: return "unknown";
	}
}

BigFormat classifyBigSignature(const unsigned char signature[4])
{
	if (std::memcmp(signature, "BIGF", 4) == 0) {
		return BigFormat::BIGF;
	}
	if (std::memcmp(signature, "BIG4", 4) == 0) {
		return BigFormat::BIG4;
	}
	return BigFormat::Unknown;
}

bool parseBigDirectory(const unsigned char* data, size_t length, uint64_t fileSize, BigDirectory& out, std::string* error)
{
	out = BigDirectory();
	if (data == nullptr || length < kHeaderSize) {
		setError(error, "file is shorter than the 16-byte BIG header");
		return false;
	}

	out.header.format = classifyBigSignature(data);
	if (out.header.format == BigFormat::Unknown) {
		setError(error, "unknown BIG signature");
		return false;
	}
	out.header.archiveSize = readLE32(data + 4);
	out.header.entryCount = readBE32(data + 8);
	out.header.firstEntryOffset = readBE32(data + 12);

	size_t pos = kHeaderSize;
	// Smallest entry is 8 bytes of offsets plus a NUL (9). A count of 0xFFFFFFFF must not reserve that many.
	const size_t maxFit = (length - kHeaderSize) / 9;
	size_t reserveCount = out.header.entryCount;
	if (reserveCount > maxFit) {
		reserveCount = maxFit;
	}
	out.entries.reserve(reserveCount);
	for (uint32_t i = 0; i < out.header.entryCount; ++i) {
		if (pos + 8 > length) {
			setError(error, "directory truncated at entry " + std::to_string(i));
			return false;
		}
		BigEntry entry;
		entry.offset = readBE32(data + pos);
		entry.size = readBE32(data + pos + 4);
		pos += 8;

		const size_t nameStart = pos;
		while (pos < length && data[pos] != 0 && pos - nameStart < kMaxPath) {
			++pos;
		}
		if (pos >= length || data[pos] != 0) {
			setError(error, "unterminated or too long path at entry " + std::to_string(i));
			return false;
		}
		entry.path.assign(reinterpret_cast<const char*>(data + nameStart), pos - nameStart);
		++pos; // NUL

		// fileSize 0 is an empty archive, not "skip the check": offset + size must still fit.
		if (uint64_t(entry.offset) + uint64_t(entry.size) > fileSize) {
			setError(error, "entry '" + entry.path + "' lies outside the archive");
			return false;
		}
		out.entries.push_back(std::move(entry));
	}
	return true;
}

bool readBigDirectory(const std::string& archivePath, BigDirectory& out, std::string* error)
{
	std::ifstream in(archivePath, std::ios::binary);
	if (!in) {
		setError(error, "cannot open " + archivePath);
		return false;
	}
	in.seekg(0, std::ios::end);
	const uint64_t fileSize = static_cast<uint64_t>(in.tellg());
	in.seekg(0, std::ios::beg);

	unsigned char header[kHeaderSize];
	if (!in.read(reinterpret_cast<char*>(header), kHeaderSize)) {
		setError(error, "cannot read BIG header of " + archivePath);
		return false;
	}
	// The directory ends where the first entry's data starts; fall back to a bounded read if
	// that field is implausible (it is not used by the engine either).
	uint64_t directoryEnd = readBE32(header + 12);
	if (directoryEnd < kHeaderSize || directoryEnd > fileSize) {
		directoryEnd = fileSize;
	}
	std::vector<unsigned char> buffer(static_cast<size_t>(directoryEnd));
	std::memcpy(buffer.data(), header, kHeaderSize);
	if (directoryEnd > kHeaderSize && !in.read(reinterpret_cast<char*>(buffer.data() + kHeaderSize), static_cast<std::streamsize>(directoryEnd - kHeaderSize))) {
		setError(error, "cannot read BIG directory of " + archivePath);
		return false;
	}
	return parseBigDirectory(buffer.data(), buffer.size(), fileSize, out, error);
}

bool readBigEntry(const std::string& archivePath, const BigEntry& entry, std::vector<unsigned char>& out, std::string* error)
{
	std::ifstream in(archivePath, std::ios::binary);
	if (!in) {
		setError(error, "cannot open " + archivePath);
		return false;
	}
	out.resize(entry.size);
	in.seekg(entry.offset, std::ios::beg);
	if (entry.size != 0 && !in.read(reinterpret_cast<char*>(out.data()), entry.size)) {
		setError(error, "cannot read entry " + entry.path);
		return false;
	}
	return true;
}

bool entryPathSafeForDisk(const std::string& path)
{
	if (path.empty() || path[0] == '/' || path[0] == '\\') {
		return false;
	}
	if (path.size() >= 2 && path[1] == ':') {
		return false;
	}
	bool any = false;
	std::string segment;
	auto flush = [&]() -> bool {
		if (segment == "..") {
			return false;
		}
		if (!segment.empty() && segment != ".") {
			any = true;
		}
		segment.clear();
		return true;
	};
	for (char c : path) {
		if (c == '/' || c == '\\') {
			if (!flush()) {
				return false;
			}
		} else {
			segment.push_back(c);
		}
	}
	return flush() && any;
}

bool extractBigEntry(const std::string& archivePath, const BigEntry& entry, const std::string& destDir, std::string* error)
{
	if (!entryPathSafeForDisk(entry.path)) {
		setError(error, "entry path is absolute or contains '..': " + entry.path);
		return false;
	}
	std::vector<unsigned char> bytes;
	if (!readBigEntry(archivePath, entry, bytes, error)) {
		return false;
	}

	namespace fs = std::filesystem;
	std::error_code ec;
	fs::create_directories(destDir, ec);
	if (ec) {
		setError(error, "cannot create " + destDir);
		return false;
	}
	const fs::path destNorm = fs::weakly_canonical(fs::path(destDir), ec);
	if (ec) {
		setError(error, "cannot resolve " + destDir);
		return false;
	}

	fs::path relPath;
	std::string segment;
	auto push = [&]() {
		if (!segment.empty() && segment != ".") {
			relPath /= segment;
		}
		segment.clear();
	};
	for (char c : entry.path) {
		if (c == '/' || c == '\\') {
			push();
		} else {
			segment.push_back(c);
		}
	}
	push();

	const fs::path outNorm = (destNorm / relPath).lexically_normal();
	const std::string destStr = destNorm.generic_string();
	const std::string outStr = outNorm.generic_string();
	const bool inside = outStr == destStr
		|| (outStr.size() > destStr.size() && outStr.compare(0, destStr.size(), destStr) == 0 && outStr[destStr.size()] == '/');
	if (!inside) {
		setError(error, "entry path escapes the output directory: " + entry.path);
		return false;
	}

	fs::create_directories(outNorm.parent_path(), ec);
	if (ec) {
		setError(error, "cannot create directory for " + entry.path);
		return false;
	}
	std::ofstream out(outNorm, std::ios::binary);
	if (!out || (!bytes.empty() && !out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size())))) {
		setError(error, "cannot write " + entry.path);
		return false;
	}
	return true;
}

} // namespace bfme::vfs
