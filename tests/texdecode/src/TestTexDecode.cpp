// BFME1 Mobile — tests
// SPDX-License-Identifier: GPL-3.0-or-later
//
// BFME: E1-44 BfmeImage (bfmeimagedecode.cpp): magic sniffing, JPEG/PNG -> top-down BGRA, and the
// <name>.jpg + <name>.png alpha merge. All images are synthesised here with stb_image_write.
#include "TestHarness.h"
#include "bfmeimagedecode.h"

#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

// The vendored writer (v1.15) leans on its includer for these.
#include <cmath>
#include <cstdarg>
#include <cstdio>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_WRITE_STATIC
#define STBI_WRITE_NO_STDIO
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wsign-compare"
#endif
#include "stb_image_write.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

using Bytes = std::vector<unsigned char>;

namespace {

void appendBytes(void *ctx, void *data, int size)
{
	Bytes *out = static_cast<Bytes *>(ctx);
	const unsigned char *p = static_cast<const unsigned char *>(data);
	out->insert(out->end(), p, p + size);
}

// pixels: row 0 first (top-down), comp channels per pixel
Bytes makePng(int w, int h, int comp, const Bytes &pixels)
{
	Bytes out;
	REQUIRE(stbi_write_png_to_func(appendBytes, &out, w, h, comp, pixels.data(), w * comp) != 0);
	return out;
}

Bytes makeJpg(int w, int h, const Bytes &rgb)
{
	Bytes out;
	REQUIRE(stbi_write_jpg_to_func(appendBytes, &out, w, h, 3, rgb.data(), 100) != 0);
	return out;
}

// top half red, bottom half blue: shows the row order after decoding
Bytes redOverBlue(int w, int h)
{
	Bytes rgb((size_t)w * h * 3);
	for (int y = 0; y < h; ++y)
		for (int x = 0; x < w; ++x) {
			unsigned char *p = &rgb[((size_t)y * w + x) * 3];
			const bool top = y < h / 2;
			p[0] = top ? 255 : 0;
			p[1] = 0;
			p[2] = top ? 0 : 255;
		}
	return rgb;
}

bool near(int a, int b, int tol) { return std::abs(a - b) <= tol; }

const unsigned char *px(const BfmeImage::Image &img, unsigned x, unsigned y)
{
	return &img.bgra[((size_t)y * img.width + x) * 4];
}

} // namespace

TEST(sniff_magic_bytes)
{
	const unsigned char jpg[] = {0xFF, 0xD8, 0xFF, 0xE0};
	const unsigned char png[] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
	const unsigned char dds[] = {'D', 'D', 'S', ' '};
	const unsigned char tga[] = {0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00};
	REQUIRE_EQ(BfmeImage::Sniff(jpg, sizeof(jpg)), BfmeImage::KIND_JPEG);
	REQUIRE_EQ(BfmeImage::Sniff(png, sizeof(png)), BfmeImage::KIND_PNG);
	REQUIRE_EQ(BfmeImage::Sniff(dds, sizeof(dds)), BfmeImage::KIND_UNKNOWN);
	REQUIRE_EQ(BfmeImage::Sniff(tga, sizeof(tga)), BfmeImage::KIND_UNKNOWN);
	REQUIRE_EQ(BfmeImage::Sniff(jpg, 2), BfmeImage::KIND_UNKNOWN);
	REQUIRE_EQ(BfmeImage::Sniff(png, 3), BfmeImage::KIND_UNKNOWN);
	REQUIRE_EQ(BfmeImage::Sniff(nullptr, 8), BfmeImage::KIND_UNKNOWN);
}

TEST(info_reads_dimensions)
{
	unsigned w = 0, h = 0;
	const Bytes jpg = makeJpg(24, 8, redOverBlue(24, 8));
	REQUIRE(BfmeImage::Get_Info(jpg.data(), jpg.size(), w, h));
	REQUIRE_EQ(w, 24u);
	REQUIRE_EQ(h, 8u);
	const Bytes png = makePng(5, 7, 4, Bytes(5 * 7 * 4, 0x80));
	REQUIRE(BfmeImage::Get_Info(png.data(), png.size(), w, h));
	REQUIRE_EQ(w, 5u);
	REQUIRE_EQ(h, 7u);
	const unsigned char tga[18] = {};
	REQUIRE(!BfmeImage::Get_Info(tga, sizeof(tga), w, h));
}

TEST(png_rgba_alone_keeps_alpha_and_order)
{
	// 3x2, every pixel different; row 0 is the top row in the file.
	const Bytes rgba = {
		10, 20, 30, 40,    50, 60, 70, 80,    90, 100, 110, 120,
		130, 140, 150, 160, 170, 180, 190, 200, 210, 220, 230, 0,
	};
	const Bytes png = makePng(3, 2, 4, rgba);
	BfmeImage::Image img;
	const char *err = nullptr;
	REQUIRE(BfmeImage::Decode_BGRA(png.data(), png.size(), nullptr, 0, img, &err));
	REQUIRE_EQ(img.kind, BfmeImage::KIND_PNG);
	REQUIRE_EQ(img.alpha, BfmeImage::ALPHA_OWN);
	REQUIRE_EQ(std::string(BfmeImage::Describe(img)), std::string("png"));
	REQUIRE_EQ(img.width, 3u);
	REQUIRE_EQ(img.height, 2u);
	REQUIRE_EQ(img.bgra.size(), (size_t)3 * 2 * 4);
	for (unsigned i = 0; i < 6; ++i) {
		const unsigned char *s = &rgba[i * 4];
		const unsigned char *d = &img.bgra[i * 4];
		REQUIRE_EQ((int)d[0], (int)s[2]);	// B
		REQUIRE_EQ((int)d[1], (int)s[1]);	// G
		REQUIRE_EQ((int)d[2], (int)s[0]);	// R
		REQUIRE_EQ((int)d[3], (int)s[3]);	// A
	}
}

TEST(png_rgb_alone_is_opaque)
{
	const Bytes rgb = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};
	const Bytes png = makePng(2, 2, 3, rgb);
	BfmeImage::Image img;
	REQUIRE(BfmeImage::Decode_BGRA(png.data(), png.size(), nullptr, 0, img, nullptr));
	REQUIRE_EQ(img.alpha, BfmeImage::ALPHA_OWN);
	for (unsigned i = 0; i < 4; ++i) {
		REQUIRE_EQ((int)img.bgra[i * 4 + 0], (int)rgb[i * 3 + 2]);
		REQUIRE_EQ((int)img.bgra[i * 4 + 2], (int)rgb[i * 3 + 0]);
		REQUIRE_EQ((int)img.bgra[i * 4 + 3], 255);
	}
}

TEST(jpg_alone_is_opaque_and_top_down)
{
	const Bytes jpg = makeJpg(16, 16, redOverBlue(16, 16));
	BfmeImage::Image img;
	REQUIRE(BfmeImage::Decode_BGRA(jpg.data(), jpg.size(), nullptr, 0, img, nullptr));
	REQUIRE_EQ(img.kind, BfmeImage::KIND_JPEG);
	REQUIRE_EQ(img.alpha, BfmeImage::ALPHA_OPAQUE);
	REQUIRE_EQ(std::string(BfmeImage::Describe(img)), std::string("jpg"));
	REQUIRE_EQ(img.width, 16u);
	REQUIRE_EQ(img.height, 16u);
	// Row 0 (memory start) is the top of the picture: red. Last row: blue. JPEG is lossy, so
	// check away from the edge between the halves.
	const unsigned char *top = px(img, 8, 1);
	const unsigned char *bottom = px(img, 8, 14);
	REQUIRE(near(top[2], 255, 24) && near(top[1], 0, 24) && near(top[0], 0, 24));
	REQUIRE(near(bottom[0], 255, 24) && near(bottom[1], 0, 24) && near(bottom[2], 0, 24));
	for (size_t i = 3; i < img.bgra.size(); i += 4)
		REQUIRE_EQ((int)img.bgra[i], 255);
}

TEST(jpg_takes_sibling_png_alpha)
{
	const int w = 16, h = 16;
	const Bytes jpg = makeJpg(w, h, redOverBlue(w, h));
	// The sibling's colour is noise on purpose: only its alpha may be used.
	Bytes mask((size_t)w * h * 4);
	for (int y = 0; y < h; ++y)
		for (int x = 0; x < w; ++x) {
			unsigned char *p = &mask[((size_t)y * w + x) * 4];
			p[0] = (unsigned char)(x * 13);
			p[1] = (unsigned char)(y * 7);
			p[2] = 99;
			p[3] = (unsigned char)(x * 16 + y);	// distinct per pixel, row order visible
		}
	const Bytes png = makePng(w, h, 4, mask);
	BfmeImage::Image img;
	REQUIRE(BfmeImage::Decode_BGRA(jpg.data(), jpg.size(), png.data(), png.size(), img, nullptr));
	REQUIRE_EQ(img.kind, BfmeImage::KIND_JPEG);
	REQUIRE_EQ(img.alpha, BfmeImage::ALPHA_SIBLING_PNG);
	REQUIRE_EQ(std::string(BfmeImage::Describe(img)), std::string("jpg+png alpha"));
	for (int y = 0; y < h; ++y)
		for (int x = 0; x < w; ++x)
			REQUIRE_EQ((int)px(img, x, y)[3], x * 16 + y);
	// colour still from the JPEG
	REQUIRE(near(px(img, 8, 1)[2], 255, 24));
	REQUIRE(near(px(img, 8, 14)[0], 255, 24));
}

TEST(jpg_takes_gray_png_luminance_as_alpha)
{
	const int w = 8, h = 8;
	const Bytes jpg = makeJpg(w, h, redOverBlue(w, h));
	Bytes gray((size_t)w * h);
	for (size_t i = 0; i < gray.size(); ++i)
		gray[i] = (unsigned char)(i * 4);
	const Bytes png = makePng(w, h, 1, gray);
	BfmeImage::Image img;
	REQUIRE(BfmeImage::Decode_BGRA(jpg.data(), jpg.size(), png.data(), png.size(), img, nullptr));
	REQUIRE_EQ(img.alpha, BfmeImage::ALPHA_SIBLING_PNG);
	for (size_t i = 0; i < gray.size(); ++i)
		REQUIRE_EQ((int)img.bgra[i * 4 + 3], (int)gray[i]);
}

TEST(jpg_ignores_sibling_png_of_other_size)
{
	const Bytes jpg = makeJpg(16, 16, redOverBlue(16, 16));
	const Bytes png = makePng(8, 16, 4, Bytes(8 * 16 * 4, 0));
	BfmeImage::Image img;
	REQUIRE(BfmeImage::Decode_BGRA(jpg.data(), jpg.size(), png.data(), png.size(), img, nullptr));
	REQUIRE_EQ(img.alpha, BfmeImage::ALPHA_SIBLING_MISMATCH);
	REQUIRE_EQ(img.siblingWidth, 8u);
	REQUIRE_EQ(img.siblingHeight, 16u);
	REQUIRE_EQ(std::string(BfmeImage::Describe(img)), std::string("jpg"));
	for (size_t i = 3; i < img.bgra.size(); i += 4)
		REQUIRE_EQ((int)img.bgra[i], 255);
}

TEST(jpg_ignores_unreadable_sibling)
{
	const Bytes jpg = makeJpg(8, 8, redOverBlue(8, 8));
	Bytes truncated = makePng(8, 8, 4, Bytes(8 * 8 * 4, 7));
	truncated.resize(20);	// valid signature, broken body
	BfmeImage::Image img;
	REQUIRE(BfmeImage::Decode_BGRA(jpg.data(), jpg.size(), truncated.data(), truncated.size(), img, nullptr));
	REQUIRE_EQ(img.alpha, BfmeImage::ALPHA_SIBLING_UNREADABLE);
	// a second JPEG is not an alpha source either
	REQUIRE(BfmeImage::Decode_BGRA(jpg.data(), jpg.size(), jpg.data(), jpg.size(), img, nullptr));
	REQUIRE_EQ(img.alpha, BfmeImage::ALPHA_SIBLING_UNREADABLE);
	for (size_t i = 3; i < img.bgra.size(); i += 4)
		REQUIRE_EQ((int)img.bgra[i], 255);
}

TEST(png_input_ignores_sibling)
{
	const Bytes a = makePng(2, 1, 4, Bytes{1, 2, 3, 4, 5, 6, 7, 8});
	const Bytes b = makePng(2, 1, 4, Bytes{0, 0, 0, 200, 0, 0, 0, 201});
	BfmeImage::Image img;
	REQUIRE(BfmeImage::Decode_BGRA(a.data(), a.size(), b.data(), b.size(), img, nullptr));
	REQUIRE_EQ(img.alpha, BfmeImage::ALPHA_OWN);
	REQUIRE_EQ((int)img.bgra[3], 4);
	REQUIRE_EQ((int)img.bgra[7], 8);
}

TEST(broken_input_fails_cleanly)
{
	BfmeImage::Image img;
	const char *err = nullptr;
	const unsigned char tga[18] = {0, 0, 2};
	REQUIRE(!BfmeImage::Decode_BGRA(tga, sizeof(tga), nullptr, 0, img, &err));
	REQUIRE(err != nullptr);
	Bytes jpg = makeJpg(16, 16, redOverBlue(16, 16));
	jpg.resize(12);
	err = nullptr;
	REQUIRE(!BfmeImage::Decode_BGRA(jpg.data(), jpg.size(), nullptr, 0, img, &err));
	REQUIRE(err != nullptr);
	REQUIRE(img.bgra.empty());
}
