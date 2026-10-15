/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2025 Electronic Arts Inc.
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
**
**	This program is distributed in the hope that it will be useful,
**	but WITHOUT ANY WARRANTY; without even the implied warranty of
**	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**	GNU General Public License for more details.
**
**	You should have received a copy of the GNU General Public License
**	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

// GeneralsX @bugfix Android port 08/10/2026 applicationId is not the Java package.
// BFME: E1-04 resolve the install path from argv[0], not a hardcoded package name.
// Private files live under /data/user/<userId>/<applicationId>/files. The crash
// handler and the logging/update markers run before JNI_OnLoad, so they cannot
// call Context.getPackageName(). Android sets argv[0] to that same string before
// any of our code runs; /proc/self/cmdline is the equivalent.
#pragma once

#if defined(__ANDROID__)
#include <cstdio>
#include <cstring>
#include <unistd.h>

inline bool gxAndroidPackageName(char *out, size_t outLen)
{
	if (out == nullptr || outLen == 0)
		return false;
	out[0] = '\0';
	FILE *f = fopen("/proc/self/cmdline", "r");
	if (f == nullptr)
		return false;
	const size_t n = fread(out, 1, outLen - 1, f);
	fclose(f);
	if (n == 0)
		return false;
	out[n] = '\0';
	for (char *p = out; *p != '\0'; ++p)
	{
		const char c = *p;
		const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
			|| (c >= '0' && c <= '9') || c == '_' || c == '.';
		if (!ok)
		{
			out[0] = '\0';
			return false;
		}
	}
	return true;
}

// leaf is a path under files/, for example "crash.log" or "update".
inline bool gxAndroidFilesPath(char *out, size_t outLen, const char *leaf)
{
	char pkg[128];
	if (out == nullptr || outLen == 0 || leaf == nullptr || !gxAndroidPackageName(pkg, sizeof(pkg)))
		return false;
	const int userId = (int)(getuid() / 100000);
	const int n = snprintf(out, outLen, "/data/user/%d/%s/files/%s", userId, pkg, leaf);
	return n > 0 && (size_t)n < outLen;
}
#endif
