/*
** BFME: E1-17b Desktop GENERALSX_RENDER_BACKEND tokens.
**
** Android keeps its own reader (render_backend.cfg, then the env) in
** d3d8gles.cpp. This header is only the non-Android decision, so a unit
** test can run it without GLES, SDL, or d3d8.
**
** nullptr or an empty string is "unset": DXVK, no stderr. Comparison is
** ASCII case-insensitive and does not trim whitespace.
**   vulkan, dxvk          -> DXVK
**   gles                  -> GLES
**   gles_angle            -> GLES, and one stderr line that ANGLE is
**                            unavailable on the desktop (Mesa is used)
**   anything else         -> DXVK, and one stderr line naming the value
*/

#pragma once

#include <cstddef>
#include <cstdio>

enum BfmeDesktopBackend
{
	BFME_DESKTOP_DXVK = 0,
	BFME_DESKTOP_GLES = 1
};

enum BfmeDesktopBackendNotice
{
	BFME_DESKTOP_NOTICE_NONE = 0,
	BFME_DESKTOP_NOTICE_ANGLE = 1,
	BFME_DESKTOP_NOTICE_UNKNOWN = 2
};

struct BfmeDesktopBackendChoice
{
	BfmeDesktopBackend backend;
	BfmeDesktopBackendNotice notice;
};

// Exact stderr text, without the trailing newline the caller adds.
inline const char *BfmeDesktopAngleUnavailableLine()
{
	return "ANGLE недоступен на десктопе";
}

inline char BfmeDesktopAsciiLower(char c)
{
	if (c >= 'A' && c <= 'Z') {
		return static_cast<char>(c - 'A' + 'a');
	}
	return c;
}

inline bool BfmeDesktopEqualsIgnoreCase(const char *a, const char *b)
{
	if (a == nullptr || b == nullptr) {
		return false;
	}
	while (*a != '\0' && *b != '\0') {
		if (BfmeDesktopAsciiLower(*a) != BfmeDesktopAsciiLower(*b)) {
			return false;
		}
		++a;
		++b;
	}
	return *a == '\0' && *b == '\0';
}

inline BfmeDesktopBackendChoice BfmeChooseDesktopBackend(const char *raw)
{
	BfmeDesktopBackendChoice choice;
	choice.backend = BFME_DESKTOP_DXVK;
	choice.notice = BFME_DESKTOP_NOTICE_NONE;
	if (raw == nullptr || raw[0] == '\0') {
		return choice;
	}
	if (BfmeDesktopEqualsIgnoreCase(raw, "vulkan") || BfmeDesktopEqualsIgnoreCase(raw, "dxvk")) {
		return choice;
	}
	if (BfmeDesktopEqualsIgnoreCase(raw, "gles")) {
		choice.backend = BFME_DESKTOP_GLES;
		return choice;
	}
	if (BfmeDesktopEqualsIgnoreCase(raw, "gles_angle")) {
		choice.backend = BFME_DESKTOP_GLES;
		choice.notice = BFME_DESKTOP_NOTICE_ANGLE;
		return choice;
	}
	choice.notice = BFME_DESKTOP_NOTICE_UNKNOWN;
	return choice;
}

// Writes the stderr line (no newline) for a notice. Returns nullptr when
// there is nothing to print. unknownBuf is used only for the unknown token.
inline const char *BfmeDesktopBackendNoticeLine(const BfmeDesktopBackendChoice &choice, const char *raw, char *unknownBuf, std::size_t unknownBufSize)
{
	if (choice.notice == BFME_DESKTOP_NOTICE_ANGLE) {
		return BfmeDesktopAngleUnavailableLine();
	}
	if (choice.notice == BFME_DESKTOP_NOTICE_UNKNOWN) {
		if (unknownBuf == nullptr || unknownBufSize == 0) {
			return nullptr;
		}
		std::snprintf(unknownBuf, unknownBufSize,
			"[d3d8gles] unknown GENERALSX_RENDER_BACKEND=%s, using vulkan",
			raw != nullptr ? raw : "");
		return unknownBuf;
	}
	return nullptr;
}
