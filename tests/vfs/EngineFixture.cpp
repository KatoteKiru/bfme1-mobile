// BFME1 Mobile — tests
// SPDX-License-Identifier: GPL-3.0-or-later
#include "PreRTS.h"

#include "EngineFixture.h"

#include "Common/ArchiveFile.h"
#include "Common/ArchiveFileSystem.h"
#include "Common/file.h"
#include "Common/FileSystem.h"
#include "Common/LocalFileSystem.h"
#include "StdDevice/Common/StdLocalFileSystem.h"
#include "bfme/vfs/BfmeBIGFileSystem.h"

#include <cstdlib>

namespace bfmetest {

void ensureLocalFileSystem()
{
	if (TheLocalFileSystem == nullptr) {
		TheLocalFileSystem = new StdLocalFileSystem;
		TheLocalFileSystem->init();
	}
}

MountedVfs::MountedVfs(const BfmeVfsConfig& config)
{
	ensureLocalFileSystem();
	fs_ = new BfmeBIGFileSystem(config);
	TheArchiveFileSystem = fs_;
	fs_->init();
}

MountedVfs::~MountedVfs()
{
	TheArchiveFileSystem = nullptr;
	delete fs_;
}

bool MountedVfs::exists(const std::string& path) const
{
	return fs_->doesFileExist(path.c_str());
}

bool MountedVfs::read(const std::string& path, std::string& out) const
{
	File* f = fs_->openFile(path.c_str(), File::READ | File::BINARY);
	if (f == nullptr) {
		return false;
	}
	const Int n = f->size();
	out.assign(size_t(n), '\0');
	const Int got = n > 0 ? f->read(&out[0], n) : 0;
	f->close();
	return got == n;
}

long long MountedVfs::sizeOf(const std::string& path) const
{
	FileInfo info;
	if (!fs_->getFileInfo(AsciiString(path.c_str()), &info)) {
		return -1;
	}
	return info.size();
}

std::string MountedVfs::winner(const std::string& path) const
{
	return fs_->getWinningArchiveName(AsciiString(path.c_str())).str();
}

std::vector<std::string> MountedVfs::listAll() const
{
	FilenameList list;
	fs_->getFileListInDirectory("", "", "*", list, TRUE);
	std::vector<std::string> out;
	for (const AsciiString& s : list) {
		out.push_back(s.str());
	}
	return out;
}

bool engineAcceptsArchive(const std::string& path, std::vector<std::string>* files)
{
	ensureLocalFileSystem();
	BfmeBIGFileSystem probe;  // StdBIGFileSystem::openArchiveFile, nothing mounted
	ArchiveFile* archive = probe.openArchiveFile(path.c_str());
	if (archive == nullptr) {
		return false;
	}
	if (files != nullptr) {
		FilenameList list;
		archive->getFileListInDirectory("", "", "*", list, TRUE);
		for (const AsciiString& s : list) {
			files->push_back(s.str());
		}
	}
	delete archive;
	return true;
}

std::string gamedataDir()
{
	const char* g = getenv("BFME_GAMEDATA");
	return (g != nullptr && g[0] != '\0') ? std::string(g) : std::string();
}

std::string oracleDir()
{
	// No sibling guess. <gamedata>/../tools/oracle/out is not where the dumps live,
	// and the dumps are not in git. Callers set BFME_ORACLE_DIR.
	const char* o = getenv("BFME_ORACLE_DIR");
	return (o != nullptr && o[0] != '\0') ? std::string(o) : std::string();
}

} // namespace bfmetest
