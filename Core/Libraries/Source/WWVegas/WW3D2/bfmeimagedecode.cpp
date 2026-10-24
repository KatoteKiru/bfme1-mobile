// BFME1 Mobile — engine
// SPDX-License-Identifier: GPL-3.0-or-later
//
// BFME: E1-44 JPEG/PNG texture decode (see bfmeimagedecode.h). The only translation unit that
// compiles the stb_image implementation (Dependencies/stb, MIT, LICENSES/stb-MIT.txt).

#include "bfmeimagedecode.h"

#include <climits>
#include <cstring>

// BFME: E1-44 stb_image v2.30: JPEG + PNG only, no stdio (callers read through the engine file
// factory so BIG archives work), static functions so no stbi_* symbol leaves this file.
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#if defined(__aarch64__) && !defined(STBI_NEON)
#define STBI_NEON	// every AArch64 core has NEON; stb only uses it when asked
#endif
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#endif
#include "stb_image.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace BfmeImage
{

Kind Sniff(const void *data, size_t size)
{
	const unsigned char *p = static_cast<const unsigned char *>(data);
	if (p == nullptr)
		return KIND_UNKNOWN;
	if (size >= 3 && p[0] == 0xFF && p[1] == 0xD8 && p[2] == 0xFF)
		return KIND_JPEG;
	if (size >= 4 && p[0] == 0x89 && p[1] == 0x50 && p[2] == 0x4E && p[3] == 0x47)
		return KIND_PNG;
	return KIND_UNKNOWN;
}

bool Get_Info(const void *data, size_t size, unsigned &width, unsigned &height)
{
	if (Sniff(data, size) == KIND_UNKNOWN || size > (size_t)INT_MAX)
		return false;
	int w = 0, h = 0, comp = 0;
	if (!stbi_info_from_memory(static_cast<const stbi_uc *>(data), (int)size, &w, &h, &comp) || w <= 0 || h <= 0)
		return false;
	width = (unsigned)w;
	height = (unsigned)h;
	return true;
}

namespace
{
	struct StbiPixels
	{
		stbi_uc *pixels = nullptr;
		~StbiPixels() { if (pixels) stbi_image_free(pixels); }
	};

	// RGBA (stb order) -> BGRA, alpha 255 when keepAlpha is false.
	void To_BGRA(const stbi_uc *rgba, size_t count, bool keepAlpha, unsigned char *out)
	{
		for (size_t i = 0; i < count; ++i, rgba += 4, out += 4)
		{
			out[0] = rgba[2];
			out[1] = rgba[1];
			out[2] = rgba[0];
			out[3] = keepAlpha ? rgba[3] : 255;
		}
	}
}

bool Decode_BGRA(const void *data, size_t size, const void *alpha_png, size_t alpha_size, Image &out, const char **error)
{
	out = Image();
	const char *dummy = nullptr;
	const char **err = error ? error : &dummy;
	*err = nullptr;

	const Kind kind = Sniff(data, size);
	if (kind == KIND_UNKNOWN)
	{
		*err = "not a JPEG or PNG";
		return false;
	}
	if (size > (size_t)INT_MAX)
	{
		*err = "file too large";
		return false;
	}

	int w = 0, h = 0, comp = 0;
	StbiPixels img;
	// stb returns rows top-down; that is already the D3D surface order, so no flip.
	img.pixels = stbi_load_from_memory(static_cast<const stbi_uc *>(data), (int)size, &w, &h, &comp, 4);
	if (img.pixels == nullptr || w <= 0 || h <= 0)
	{
		*err = stbi_failure_reason() ? stbi_failure_reason() : "decode failed";
		return false;
	}

	out.kind = kind;
	out.width = (unsigned)w;
	out.height = (unsigned)h;
	const size_t count = (size_t)w * (size_t)h;
	out.bgra.resize(count * 4);

	if (kind == KIND_PNG)
	{
		To_BGRA(img.pixels, count, true, out.bgra.data());	// stb fills 255 if the PNG has no alpha
		out.alpha = ALPHA_OWN;
		return true;
	}

	To_BGRA(img.pixels, count, false, out.bgra.data());
	out.alpha = ALPHA_OPAQUE;

	if (alpha_png == nullptr || alpha_size == 0)
		return true;

	// JPEG + sibling PNG: take the PNG's alpha (or, for a PNG without alpha, its luminance).
	if (Sniff(alpha_png, alpha_size) != KIND_PNG || alpha_size > (size_t)INT_MAX)
	{
		out.alpha = ALPHA_SIBLING_UNREADABLE;
		return true;
	}
	int aw = 0, ah = 0, acomp = 0;
	StbiPixels mask;
	mask.pixels = stbi_load_from_memory(static_cast<const stbi_uc *>(alpha_png), (int)alpha_size, &aw, &ah, &acomp, 4);
	if (mask.pixels == nullptr || aw <= 0 || ah <= 0)
	{
		out.alpha = ALPHA_SIBLING_UNREADABLE;
		return true;
	}
	out.siblingWidth = (unsigned)aw;
	out.siblingHeight = (unsigned)ah;
	if (aw != w || ah != h)
	{
		out.alpha = ALPHA_SIBLING_MISMATCH;
		return true;
	}

	const bool hasAlpha = (acomp == 2 || acomp == 4);
	const stbi_uc *m = mask.pixels;
	unsigned char *o = out.bgra.data();
	for (size_t i = 0; i < count; ++i, m += 4, o += 4)
	{
		if (hasAlpha)
			o[3] = m[3];
		else
			o[3] = (unsigned char)((m[0] * 77u + m[1] * 150u + m[2] * 29u) >> 8);	// Rec.601 luma
	}
	out.alpha = ALPHA_SIBLING_PNG;
	return true;
}

const char *Describe(const Image &img)
{
	if (img.kind == KIND_PNG)
		return "png";
	if (img.alpha == ALPHA_SIBLING_PNG)
		return "jpg+png alpha";
	return "jpg";
}

} // namespace BfmeImage
