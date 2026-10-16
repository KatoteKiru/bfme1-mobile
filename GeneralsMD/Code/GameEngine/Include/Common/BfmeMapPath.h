// BFME1 Mobile
// SPDX-License-Identifier: GPL-3.0-or-later
//
// BFME: E1-39. Map path helpers with no engine dependencies (unit-tested in tests/launch).
#pragma once

#include <cctype>
#include <cstddef>
#include <string>

// Archive path of the map a BFME run starts in when no -file is given: a small official
// 2-player map from maps.big. BfmeDefaultMapPath() (Common/BfmeLaunch.h) returns it.
inline const char *BfmeDefaultMapPathString()
{
	return "maps\\map mp rohan\\map mp rohan.map";
}

// true for "<dir>\<name>\<name>.map" ('/' or '\', any case). The upstream -file parser expands
// only the short form "<dir>\<name>.map"; expanding a long path would double the folder.
inline bool BfmeIsLongMapPath(const char *path)
{
	if (path == nullptr) return false;
	std::string p = path;
	for (char &c : p) {
		if (c == '/') c = '\\';
		c = (char)std::tolower((unsigned char)c);
	}
	if (p.size() < 4 || p.compare(p.size() - 4, 4, ".map") != 0) return false;
	const size_t fileStart = p.rfind('\\');
	if (fileStart == std::string::npos || fileStart == 0) return false;
	const size_t dirStart = p.rfind('\\', fileStart - 1);
	if (dirStart == std::string::npos) return false;
	const std::string dir = p.substr(dirStart + 1, fileStart - dirStart - 1);
	const std::string file = p.substr(fileStart + 1, p.size() - fileStart - 5);
	return !dir.empty() && dir == file;
}
