// BFME1 Mobile — formats/vfs
// SPDX-License-Identifier: GPL-3.0-or-later
#include "bfme/vfs/MountOrder.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <system_error>

namespace bfme::vfs {

namespace {

namespace fs = std::filesystem;

// Must stay identical (as rules) to formats/vfs/bfme1-mount-order.cfg; tests/vfs checks it.
const char* const kDefaultRules =
	"layer base      0    .            recursive  first-wins  *.big\n"
	"layer patch222  100  .            flat       first-wins  _patch222*.big\n"
	"layer mobile    200  MobileLayer  recursive  first-wins  *.big\n";

char upperAscii(char c)
{
	return (c >= 'a' && c <= 'z') ? char(c - 'a' + 'A') : c;
}

char lowerAscii(char c)
{
	return (c >= 'A' && c <= 'Z') ? char(c - 'A' + 'a') : c;
}

std::string normalizeSlashes(std::string path)
{
	std::replace(path.begin(), path.end(), '\\', '/');
	while (path.size() > 1 && path.back() == '/') {
		path.pop_back();
	}
	if (path == ".") {
		path.clear();
	}
	if (path.rfind("./", 0) == 0) {
		path.erase(0, 2);
	}
	return path;
}

bool isAbsolutePath(const std::string& path)
{
	return !path.empty() && (path[0] == '/' || (path.size() > 1 && path[1] == ':'));
}

bool equalsNoCase(const std::string& a, const std::string& b)
{
	return a.size() == b.size() && compareArchiveNames(a, b) == 0;
}

bool startsWithNoCase(const std::string& text, const std::string& prefix)
{
	return text.size() >= prefix.size() && equalsNoCase(text.substr(0, prefix.size()), prefix);
}

std::string parentOf(const std::string& relPath)
{
	const size_t slash = relPath.rfind('/');
	return slash == std::string::npos ? std::string() : relPath.substr(0, slash);
}

std::string fileNameOf(const std::string& relPath)
{
	const size_t slash = relPath.rfind('/');
	return slash == std::string::npos ? relPath : relPath.substr(slash + 1);
}

bool dirMatches(const LayerRule& rule, const std::string& relPath)
{
	const std::string dir = normalizeSlashes(rule.dir);
	const std::string parent = parentOf(normalizeSlashes(relPath));
	if (equalsNoCase(parent, dir)) {
		return true;
	}
	if (!rule.recursive) {
		return false;
	}
	return dir.empty() ? !isAbsolutePath(relPath) : startsWithNoCase(parent, dir + "/");
}

bool globMatchAt(const char* p, const char* t)
{
	for (; *p != '\0'; ++p, ++t) {
		if (*p == '*') {
			while (*p == '*') {
				++p;
			}
			if (*p == '\0') {
				return true;
			}
			for (; *t != '\0'; ++t) {
				if (globMatchAt(p, t)) {
					return true;
				}
			}
			return false;
		}
		if (*t == '\0') {
			return false;
		}
		if (*p != '?' && upperAscii(*p) != upperAscii(*t)) {
			return false;
		}
	}
	return *t == '\0';
}

void setError(std::string* error, const std::string& text)
{
	if (error != nullptr) {
		*error = text;
	}
}

} // namespace

const LayerRule* MountRules::findLayer(const std::string& name) const
{
	for (const LayerRule& layer : layers) {
		if (layer.name == name) {
			return &layer;
		}
	}
	return nullptr;
}

LayerRule* MountRules::findLayer(const std::string& name)
{
	return const_cast<LayerRule*>(static_cast<const MountRules*>(this)->findLayer(name));
}

const char* defaultBfme1MountRulesText()
{
	return kDefaultRules;
}

MountRules defaultBfme1MountRules()
{
	MountRules rules;
	std::istringstream in(kDefaultRules);
	std::string error;
	parseMountRules(in, rules, &error);
	return rules;
}

bool parseMountRules(std::istream& in, MountRules& out, std::string* error)
{
	out = MountRules();
	std::string line;
	int lineNo = 0;
	while (std::getline(in, line)) {
		++lineNo;
		const size_t hash = line.find('#');
		if (hash != std::string::npos) {
			line.erase(hash);
		}
		std::istringstream words(line);
		std::string keyword;
		if (!(words >> keyword)) {
			continue;
		}
		if (keyword != "layer") {
			setError(error, "line " + std::to_string(lineNo) + ": unknown keyword '" + keyword + "'");
			return false;
		}
		LayerRule rule;
		std::string recursion;
		std::string order;
		if (!(words >> rule.name >> rule.priority >> rule.dir >> recursion >> order)) {
			setError(error, "line " + std::to_string(lineNo) + ": expected 'layer <name> <priority> <dir> <flat|recursive> <last-wins|first-wins> <glob>...'");
			return false;
		}
		if (recursion == "recursive") {
			rule.recursive = true;
		} else if (recursion != "flat") {
			setError(error, "line " + std::to_string(lineNo) + ": expected flat or recursive, got '" + recursion + "'");
			return false;
		}
		if (order == "last-wins") {
			rule.order = IntraLayerOrder::SortedLastWins;
		} else if (order == "first-wins") {
			rule.order = IntraLayerOrder::SortedFirstWins;
		} else {
			setError(error, "line " + std::to_string(lineNo) + ": expected last-wins or first-wins, got '" + order + "'");
			return false;
		}
		std::string glob;
		while (words >> glob) {
			rule.globs.push_back(glob);
		}
		if (rule.globs.empty()) {
			setError(error, "line " + std::to_string(lineNo) + ": layer '" + rule.name + "' has no glob");
			return false;
		}
		if (out.findLayer(rule.name) != nullptr) {
			setError(error, "line " + std::to_string(lineNo) + ": duplicate layer '" + rule.name + "'");
			return false;
		}
		for (const LayerRule& other : out.layers) {
			if (other.priority == rule.priority) {
				setError(error, "line " + std::to_string(lineNo) + ": layers '" + other.name + "' and '" + rule.name + "' share priority " + std::to_string(rule.priority));
				return false;
			}
		}
		out.layers.push_back(rule);
	}
	if (out.layers.empty()) {
		setError(error, "no layers defined");
		return false;
	}
	return true;
}

bool loadMountRulesFile(const std::string& path, MountRules& out, std::string* error)
{
	std::ifstream in(path);
	if (!in) {
		setError(error, "cannot open mount rules " + path);
		return false;
	}
	return parseMountRules(in, out, error);
}

int compareArchiveNames(const std::string& a, const std::string& b)
{
	const size_t n = std::min(a.size(), b.size());
	for (size_t i = 0; i < n; ++i) {
		const unsigned char ca = static_cast<unsigned char>(lowerAscii(a[i]));
		const unsigned char cb = static_cast<unsigned char>(lowerAscii(b[i]));
		if (ca != cb) {
			return ca < cb ? -1 : 1;
		}
	}
	return a.size() == b.size() ? 0 : (a.size() < b.size() ? -1 : 1);
}

bool globMatchNoCase(const std::string& pattern, const std::string& text)
{
	return globMatchAt(pattern.c_str(), text.c_str());
}

const LayerRule* classifyArchive(const ArchiveCandidate& archive, const MountRules& rules)
{
	const LayerRule* best = nullptr;
	const std::string name = fileNameOf(normalizeSlashes(archive.relPath));
	for (const LayerRule& rule : rules.layers) {
		if (best != nullptr && best->priority >= rule.priority) {
			continue;
		}
		if (!dirMatches(rule, archive.relPath)) {
			continue;
		}
		for (const std::string& glob : rule.globs) {
			if (globMatchNoCase(glob, name)) {
				best = &rule;
				break;
			}
		}
	}
	return best;
}

MountPlan planMount(const std::vector<ArchiveCandidate>& archives, const MountRules& rules)
{
	MountPlan plan;
	std::map<int, std::vector<ArchiveCandidate>, std::greater<int>> byPriority;
	std::map<int, const LayerRule*> layerOf;
	for (const ArchiveCandidate& archive : archives) {
		const LayerRule* layer = classifyArchive(archive, rules);
		if (layer == nullptr) {
			plan.unassigned.push_back(archive);
			continue;
		}
		byPriority[layer->priority].push_back(archive);
		layerOf[layer->priority] = layer;
	}
	for (auto& [priority, list] : byPriority) {
		const LayerRule* layer = layerOf[priority];
		std::sort(list.begin(), list.end(), [](const ArchiveCandidate& a, const ArchiveCandidate& b) {
			const int c = compareArchiveNames(normalizeSlashes(a.relPath), normalizeSlashes(b.relPath));
			return c != 0 ? c < 0 : a.relPath < b.relPath;  // deterministic for case-only differences
		});
		// ZH FilenameList is a set with less_than_nocase, so two names that differ only by case
		// are one archive. Keep the byte-smaller path when _stricmp ties (the first after this sort).
		list.erase(std::unique(list.begin(), list.end(), [](const ArchiveCandidate& a, const ArchiveCandidate& b) {
			return compareArchiveNames(normalizeSlashes(a.relPath), normalizeSlashes(b.relPath)) == 0;
		}), list.end());
		if (layer->order == IntraLayerOrder::SortedLastWins) {
			std::reverse(list.begin(), list.end());
		}
		for (const ArchiveCandidate& archive : list) {
			plan.resolutionOrder.push_back(PlannedArchive{archive, layer->name, layer->priority});
		}
	}
	return plan;
}

MountRules resolveLayerDirs(const MountRules& rules, const std::string& gameRoot)
{
	MountRules resolved = rules;
	std::error_code ec;
	const fs::path root = fs::weakly_canonical(fs::path(gameRoot), ec);
	for (LayerRule& rule : resolved.layers) {
		if (!isAbsolutePath(rule.dir)) {
			continue;
		}
		const fs::path dir = fs::weakly_canonical(fs::path(rule.dir), ec);
		const fs::path rel = dir.lexically_relative(root);
		const std::string relText = rel.generic_string();
		if (!rel.empty() && relText.rfind("..", 0) != 0) {
			rule.dir = relText == "." ? std::string(".") : relText;
		} else {
			rule.dir = normalizeSlashes(dir.generic_string());
		}
	}
	return resolved;
}

std::vector<ArchiveCandidate> scanArchives(const std::string& gameRoot, const MountRules& rules)
{
	std::vector<ArchiveCandidate> result;
	std::error_code ec;
	const fs::path root(gameRoot);

	auto addFrom = [&](const fs::path& dir, bool outsideRoot) {
		for (fs::recursive_directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end; it.increment(ec)) {
			if (!it->is_regular_file(ec)) {
				continue;
			}
			const fs::path& p = it->path();
			std::string ext = p.extension().string();
			std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return char(std::tolower(c)); });
			if (ext != ".big") {
				continue;
			}
			ArchiveCandidate candidate;
			candidate.path = p.string();
			candidate.relPath = outsideRoot ? normalizeSlashes(p.generic_string()) : normalizeSlashes(p.lexically_relative(root).generic_string());
			result.push_back(candidate);
		}
		ec.clear();
	};

	addFrom(root, false);
	for (const LayerRule& rule : rules.layers) {
		if (isAbsolutePath(rule.dir)) {
			addFrom(fs::path(rule.dir), true);
		}
	}
	std::sort(result.begin(), result.end(), [](const ArchiveCandidate& a, const ArchiveCandidate& b) { return a.path < b.path; });
	result.erase(std::unique(result.begin(), result.end(), [](const ArchiveCandidate& a, const ArchiveCandidate& b) { return a.path == b.path; }), result.end());
	return result;
}

std::string describePlan(const MountPlan& plan)
{
	std::ostringstream out;
	for (size_t i = 0; i < plan.resolutionOrder.size(); ++i) {
		const PlannedArchive& a = plan.resolutionOrder[i];
		out << i << ' ' << a.layer << ' ' << a.archive.relPath << '\n';
	}
	for (const ArchiveCandidate& a : plan.unassigned) {
		out << "- unassigned " << a.relPath << '\n';
	}
	return out.str();
}

} // namespace bfme::vfs
