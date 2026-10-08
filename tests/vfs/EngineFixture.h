// BFME1 Mobile — tests
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>
#include <vector>

class BfmeBIGFileSystem;
struct BfmeVfsConfig;

namespace bfmetest {

// Creates TheLocalFileSystem (StdLocalFileSystem) once for the process.
void ensureLocalFileSystem();

// Mounts with BfmeBIGFileSystem and installs it as TheArchiveFileSystem for the scope.
class MountedVfs {
public:
	explicit MountedVfs(const BfmeVfsConfig& config);
	~MountedVfs();
	BfmeBIGFileSystem& fs() { return *fs_; }

	bool exists(const std::string& path) const;
	bool read(const std::string& path, std::string& out) const;  // via TheArchiveFileSystem->openFile
	long long sizeOf(const std::string& path) const;              // via getFileInfo, -1 if missing
	std::string winner(const std::string& path) const;            // archive name serving the path
	std::vector<std::string> listAll() const;                     // getFileListInDirectory("", "*", recursive)

private:
	BfmeBIGFileSystem* fs_;
};

// Opens one archive with the engine's StdBIGFileSystem::openArchiveFile (null on rejection).
bool engineAcceptsArchive(const std::string& path, std::vector<std::string>* files = nullptr);

// Environment helpers for the integration tests.
std::string gamedataDir();   // BFME_GAMEDATA or "" (test skips)
std::string oracleDir();     // BFME_ORACLE_DIR or <BFME_GAMEDATA>/../tools/oracle/out, "" if absent

} // namespace bfmetest
