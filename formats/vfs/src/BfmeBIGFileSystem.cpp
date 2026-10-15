// BFME1 Mobile — formats/vfs
// SPDX-License-Identifier: GPL-3.0-or-later
// No PreRTS.h: builds in the z_gameenginedevice context like StdBIGFileSystem.cpp.
#include "Common/ArchiveFile.h"
#include "Common/ArchiveFileSystem.h"
#include "Common/LocalFileSystem.h"

#include "bfme/vfs/BfmeBIGFileSystem.h"

#include <cstdio>
#include <cstdlib>
#include <string>

#ifdef __ANDROID__
// BFME: this translation unit is compiled into z_gameenginedevice, which links SDL3.
// The host vfs tests are not __ANDROID__ and do not include SDL.
#include <SDL3/SDL_log.h>
#endif

namespace {

// BFME: StdBIGFileSystem::init() reports a missing ZH asset root with
// DEBUG_ASSERTCRASH ("No BIG files were loaded for the primary game assets.").
// That macro is empty in a release build, so a failed rules file or an empty
// mount used to return with no message (and mountedAny tripped -Wunused-variable).
// Log the cause and leave the process in every build.
//
// BFME: on Android, native stderr is /dev/null (SDL3Main.cpp mirrors it into a
// file only when the launcher has not turned logging off). SDL_Log still
// reaches logcat. The host keeps the stderr line only.
[[noreturn]] void failBfmeVfsInit(const char* message)
{
	fprintf(stderr, "[bfmevfs] FATAL: %s\n", message);
	fflush(stderr);
#ifdef __ANDROID__
	SDL_Log("[bfmevfs] FATAL: %s", message);
#endif
	DEBUG_ASSERTCRASH(false, ("[bfmevfs] FATAL: %s", message));
	std::_Exit(EXIT_FAILURE);
}

bool equalsIniBig(const std::string& name)
{
	const char* expected = "ini.big";
	if (name.size() != 7) {
		return false;
	}
	for (size_t i = 0; i < name.size(); ++i) {
		char c = name[i];
		if (c >= 'A' && c <= 'Z') {
			c = char(c - 'A' + 'a');
		}
		if (c != expected[i]) {
			return false;
		}
	}
	return true;
}

// Root ini.big only. A nested archive with the same file name does not satisfy it.
bool isRootIniBigRelPath(const std::string& relPath)
{
	std::string path = relPath;
	for (char& c : path) {
		if (c == '\\') {
			c = '/';
		}
	}
	while (path.size() > 1 && path.back() == '/') {
		path.pop_back();
	}
	const size_t slash = path.rfind('/');
	if (slash != std::string::npos) {
		return false;
	}
	return equalsIniBig(path);
}

} // namespace

bool BfmeVfsConfig::fromEnvironment(BfmeVfsConfig& out, std::string* error)
{
	const char* root = getenv("BFME_GAMEDATA");
	if (root == nullptr || root[0] == '\0') {
		if (error != nullptr) {
			*error = "BFME_GAMEDATA is not set";
		}
		return false;
	}
	out.gameRoot = root;
	const char* rulesFile = getenv("BFME_VFS_MOUNT_ORDER");
	if (rulesFile != nullptr && rulesFile[0] != '\0') {
		if (!bfme::vfs::loadMountRulesFile(rulesFile, out.rules, error)) {
			return false;
		}
	}
	const char* mobile = getenv("BFME_MOBILE_LAYER");
	if (mobile != nullptr && mobile[0] != '\0') {
		if (bfme::vfs::LayerRule* layer = out.rules.findLayer("mobile")) {
			layer->dir = mobile;
		}
	}
	return true;
}

BfmeBIGFileSystem::BfmeBIGFileSystem()
	: m_configured(false)
	, m_mounted(0)
	, m_failed(0)
{
}

BfmeBIGFileSystem::BfmeBIGFileSystem(const BfmeVfsConfig& config)
	: m_config(config)
	, m_configured(true)
	, m_mounted(0)
	, m_failed(0)
{
}

BfmeBIGFileSystem::~BfmeBIGFileSystem()
{
}

bool BfmeBIGFileSystem::isRequestedByEnvironment()
{
	const char* root = getenv("BFME_GAMEDATA");
	return root != nullptr && root[0] != '\0';
}

void BfmeBIGFileSystem::init()
{
	DEBUG_ASSERTCRASH(TheLocalFileSystem != nullptr, ("TheLocalFileSystem must be initialized before TheArchiveFileSystem."));
	if (TheLocalFileSystem == nullptr) {
		failBfmeVfsInit("TheLocalFileSystem must be initialized before TheArchiveFileSystem.");
	}
	if (!m_configured) {
		std::string error;
		if (!BfmeVfsConfig::fromEnvironment(m_config, &error)) {
			failBfmeVfsInit(error.c_str());
		}
		m_configured = true;
	}

	const bfme::vfs::MountRules rules = bfme::vfs::resolveLayerDirs(m_config.rules, m_config.gameRoot);
	m_plan = bfme::vfs::planMount(bfme::vfs::scanArchives(m_config.gameRoot, rules), rules);
	if (!mountPlan(m_plan)) {
		failBfmeVfsInit(("No BFME archives were mounted from " + m_config.gameRoot).c_str());
	}
	// BFME: other archives may have opened. The game still cannot start without
	// the root ini.big (data\ini\* lives there; _patch222 does not replace every file).
	if (!rootIniBigOpened()) {
		failBfmeVfsInit(("required ini.big was not opened from " + m_config.gameRoot).c_str());
	}

	// Loose files (Data\..., maps) resolve against the game root, as StdBIGFileSystem does for ZH.
	TheLocalFileSystem->setAssetRootPath(AsciiString(m_config.gameRoot.c_str()));
}

Bool BfmeBIGFileSystem::mountPlan(const bfme::vfs::MountPlan& plan)
{
	m_mounted = 0;
	m_failed = 0;
	for (size_t i = 0; i < plan.resolutionOrder.size(); ++i) {
		const bfme::vfs::PlannedArchive& planned = plan.resolutionOrder[i];
		ArchiveFile* archiveFile = openArchiveFile(planned.archive.path.c_str());
		if (archiveFile == nullptr) {
			++m_failed;
			fprintf(stderr, "[bfmevfs] FAILED TO OPEN (%s): %s\n", planned.layer.c_str(), planned.archive.path.c_str());
			continue;
		}
		// Append: archives mounted earlier keep precedence (resolutionOrder[0] wins).
		loadIntoDirectoryTree(archiveFile, FALSE);
		m_archiveFileMap[AsciiString(planned.archive.path.c_str())] = archiveFile;
		++m_mounted;
		fprintf(stderr, "[bfmevfs] mounted #%u (%s, priority %d): %s\n", unsigned(i), planned.layer.c_str(), planned.priority, planned.archive.relPath.c_str());
	}
	for (const bfme::vfs::ArchiveCandidate& skipped : plan.unassigned) {
		fprintf(stderr, "[bfmevfs] no layer matches, not mounted: %s\n", skipped.relPath.c_str());
	}
	fflush(stderr);
	return m_mounted > 0;
}

bool BfmeBIGFileSystem::rootIniBigOpened() const
{
	for (const bfme::vfs::PlannedArchive& planned : m_plan.resolutionOrder) {
		if (!isRootIniBigRelPath(planned.archive.relPath)) {
			continue;
		}
		const ArchiveFileMap::const_iterator it = m_archiveFileMap.find(AsciiString(planned.archive.path.c_str()));
		if (it != m_archiveFileMap.end() && it->second != nullptr) {
			return true;
		}
	}
	return false;
}

AsciiString BfmeBIGFileSystem::getWinningArchiveName(const AsciiString& path) const
{
	ArchiveFile* archive = getArchiveFile(path);
	return archive != nullptr ? archive->getName() : AsciiString::TheEmptyString;
}
