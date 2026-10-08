// BFME1 Mobile — formats/vfs
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Engine adapter: an ArchiveFileSystem that mounts BFME1 archives in the layered order of a
// MountPlan (base BFME1 -> patch 2.22 -> MobileLayer) instead of the ZH asset discovery done by
// StdBIGFileSystem::init(). Archive parsing (BIGF and BIG4) stays in StdBIGFileSystem.
//
// How precedence maps onto the engine: ArchiveFileSystem keeps, per path, a multimap of archives
// and returns the first one (FileInstance 0). Archives are mounted in MountPlan::resolutionOrder
// with loadIntoDirectoryTree(overwrite=FALSE), i.e. appended, so the first mounted archive (highest
// layer, then intra-layer order) wins. -mod / -modDir from loadMods() still mount later with
// overwrite=TRUE and therefore stay on top, as in retail.
//
// Configuration (all optional except the root):
//   BFME_GAMEDATA         game root with the .big files (required for init() from the environment)
//   BFME_VFS_MOUNT_ORDER  path to a rules file (default: compiled-in formats/vfs/bfme1-mount-order.cfg)
//   BFME_MOBILE_LAYER     directory of the "mobile" layer (default: <root>/MobileLayer)
#pragma once

#include "StdDevice/Common/StdBIGFileSystem.h"
#include "bfme/vfs/MountOrder.h"

#include <string>

struct BfmeVfsConfig {
	std::string gameRoot;
	bfme::vfs::MountRules rules = bfme::vfs::defaultBfme1MountRules();

	// Reads BFME_GAMEDATA, BFME_VFS_MOUNT_ORDER, BFME_MOBILE_LAYER. Returns false if BFME_GAMEDATA is
	// unset or the rules file cannot be parsed (error says why).
	static bool fromEnvironment(BfmeVfsConfig& out, std::string* error);
};

class BfmeBIGFileSystem : public StdBIGFileSystem
{
public:
	BfmeBIGFileSystem();                                  // configured from the environment in init()
	explicit BfmeBIGFileSystem(const BfmeVfsConfig& config);
	virtual ~BfmeBIGFileSystem() override;

	virtual void init() override;

	// BFME: the ZH community patch is a GeneralsOnline archive, not a BFME layer.
	virtual Bool mountsGeneralsOnlineCommunityPatch() const override { return FALSE; }

	// True when BFME_GAMEDATA is set: the hook a game factory uses to pick this file system.
	static bool isRequestedByEnvironment();

	const bfme::vfs::MountPlan& getMountPlan() const { return m_plan; }
	int getMountedArchiveCount() const { return m_mounted; }
	int getFailedArchiveCount() const { return m_failed; }

	// Name of the archive that serves a path (instance 0), empty if none. For tests and logs.
	AsciiString getWinningArchiveName(const AsciiString& path) const;

private:
	Bool mountPlan(const bfme::vfs::MountPlan& plan);

	BfmeVfsConfig m_config;
	bool m_configured;
	bfme::vfs::MountPlan m_plan;
	int m_mounted;
	int m_failed;
};
