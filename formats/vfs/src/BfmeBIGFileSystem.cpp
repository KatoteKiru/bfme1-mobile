// BFME1 Mobile — formats/vfs
// SPDX-License-Identifier: GPL-3.0-or-later
// No PreRTS.h: builds in the z_gameenginedevice context like StdBIGFileSystem.cpp.
#include "Common/ArchiveFile.h"
#include "Common/ArchiveFileSystem.h"
#include "Common/LocalFileSystem.h"

#include "bfme/vfs/BfmeBIGFileSystem.h"

#include <cstdio>
#include <cstdlib>

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
		return;
	}
	if (!m_configured) {
		std::string error;
		if (!BfmeVfsConfig::fromEnvironment(m_config, &error)) {
			fprintf(stderr, "[bfmevfs] not configured: %s\n", error.c_str());
			return;
		}
		m_configured = true;
	}

	const bfme::vfs::MountRules rules = bfme::vfs::resolveLayerDirs(m_config.rules, m_config.gameRoot);
	m_plan = bfme::vfs::planMount(bfme::vfs::scanArchives(m_config.gameRoot, rules), rules);
	const Bool mountedAny = mountPlan(m_plan);
	DEBUG_ASSERTCRASH(mountedAny, ("No BFME archives were mounted from %s", m_config.gameRoot.c_str()));

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

AsciiString BfmeBIGFileSystem::getWinningArchiveName(const AsciiString& path) const
{
	ArchiveFile* archive = getArchiveFile(path);
	return archive != nullptr ? archive->getName() : AsciiString::TheEmptyString;
}
