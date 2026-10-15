// BFME1 Mobile — formats/vfs
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Mount order for BFME1 archives: which .big wins when several archives hold the same path.
//
// The rule is data, not code: a list of layers (base BFME1 -> patch 2.22 -> MobileLayer) with a
// priority each, and an order of archives inside a layer. The defaults live in
// formats/vfs/bfme1-mount-order.cfg (the same text is compiled in, see defaultBfme1MountRulesText()),
// so E1-22 (exact archive names of 2.22) can refine it by editing the file / env BFME_VFS_MOUNT_ORDER,
// without touching the engine. The rule itself is one paragraph in docs/formats/bfme1-big-order.md,
// shared with tools/datacheck.
//
// This header is engine-independent (C++17, std only); BfmeBIGFileSystem applies a MountPlan to
// the engine's ArchiveFileSystem.
#pragma once

#include <istream>
#include <map>
#include <string>
#include <vector>

namespace bfme::vfs {

// Order of archives that fall into the same layer. Archives are sorted by relative path with _stricmp
// (case-insensitive, lower-case folding, so '_' sorts before letters), as the ZH engine lists them.
// The rule (docs/formats/bfme1-big-order.md): first-wins, the ZH behaviour.
enum class IntraLayerOrder {
	// The LAST one in _stricmp order wins (kept for experiments only; not the BFME1 rule).
	SortedLastWins,
	// The FIRST one in _stricmp order wins: ZH loadBigFilesFromDirectory(..., overwrite=FALSE). Default.
	SortedFirstWins,
};

struct LayerRule {
	std::string name;
	int priority = 0;               // higher priority wins over lower, regardless of names
	std::string dir;                // "." = game root; relative to the root, or absolute
	bool recursive = false;         // also match archives in subdirectories of dir
	IntraLayerOrder order = IntraLayerOrder::SortedFirstWins;
	std::vector<std::string> globs; // matched against the archive file name, case-insensitive, '*' and '?'
};

struct MountRules {
	std::vector<LayerRule> layers;
	const LayerRule* findLayer(const std::string& name) const;
	LayerRule* findLayer(const std::string& name);
};

struct ArchiveCandidate {
	std::string path;     // path used to open the archive (absolute or relative to cwd)
	std::string relPath;  // '/'-separated path relative to the game root, or absolute for out-of-root layers
};

struct PlannedArchive {
	ArchiveCandidate archive;
	std::string layer;
	int priority = 0;
};

struct MountPlan {
	// Resolution order: element 0 has the highest precedence. Mounting in this order with
	// "append" semantics (engine overwrite=FALSE) makes the first mounted archive win.
	std::vector<PlannedArchive> resolutionOrder;
	// Archives that matched no layer (they are not mounted).
	std::vector<ArchiveCandidate> unassigned;
};

// Text of formats/vfs/bfme1-mount-order.cfg, compiled in.
const char* defaultBfme1MountRulesText();
MountRules defaultBfme1MountRules();

// Format: one rule per line, '#' comments.
//   layer <name> <priority> <dir> <flat|recursive> <last-wins|first-wins> <glob> [<glob>...]
bool parseMountRules(std::istream& in, MountRules& out, std::string* error);
bool loadMountRulesFile(const std::string& path, MountRules& out, std::string* error);

// _stricmp: case-insensitive ordinal comparison with lower-case folding ('_' sorts before letters).
// Callers pass a '/' -separated key (normalizeSlashes). ZH on Windows compares '\\'. After a shared
// prefix those separators sit on opposite sides of a digit or a dot, so the order can diverge.
// On the current BFME1 archive set the two keys sort the same.
int compareArchiveNames(const std::string& a, const std::string& b);
bool globMatchNoCase(const std::string& pattern, const std::string& text);

// Which layer an archive belongs to: the highest-priority layer that matches; nullptr if none.
const LayerRule* classifyArchive(const ArchiveCandidate& archive, const MountRules& rules);

MountPlan planMount(const std::vector<ArchiveCandidate>& archives, const MountRules& rules);

// Absolute layer dirs that lie inside the game root become root-relative (so they classify like
// the rest); absolute dirs outside the root stay absolute.
MountRules resolveLayerDirs(const MountRules& rules, const std::string& gameRoot);

// Lists *.big under the game root (recursive) and under every absolute layer dir outside the root.
// Does not descend into "Command and Conquer Generals Zero Hour Data" (ZH user data) or
// "GeneralsOnlineGameData", at any depth, case-insensitively. Those trees hold the Zero Hour
// community patch; gx_no_community_patch.txt is not a BFME switch. Pass rules through
// resolveLayerDirs() first.
std::vector<ArchiveCandidate> scanArchives(const std::string& gameRoot, const MountRules& rules);

// One line per archive, for logs and tests: "<rank> <layer> <relPath>".
std::string describePlan(const MountPlan& plan);

} // namespace bfme::vfs
