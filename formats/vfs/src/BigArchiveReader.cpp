// BFME1 Mobile — formats/vfs
// SPDX-License-Identifier: GPL-3.0-or-later
#include "bfme/vfs/BigArchiveReader.h"

#include <cstring>
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
	out.entries.reserve(out.header.entryCount);
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

		if (fileSize != 0 && uint64_t(entry.offset) + entry.size > fileSize) {
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

} // namespace bfme::vfs
