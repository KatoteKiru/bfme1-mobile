// BFME1 Mobile — tests
// SPDX-License-Identifier: GPL-3.0-or-later
//
// BFME: E1-48 software S3TC decode (Core/Libraries/Source/d3d8gles/src/bc_decode.h) on synthetic
// blocks. Expected values follow EXT_texture_compression_s3tc / D3D8 DXT1-DXT5.
#include "TestHarness.h"
#include "bc_decode.h"

#include <cstdint>
#include <vector>

using namespace bcdecode;

namespace {

struct Px {
	int r, g, b, a;
};

Px texel(const uint8_t *rgba, int i)
{
	return Px{rgba[i * 4 + 0], rgba[i * 4 + 1], rgba[i * 4 + 2], rgba[i * 4 + 3]};
}

void requirePx(const Px &p, int r, int g, int b, int a)
{
	REQUIRE_EQ(p.r, r);
	REQUIRE_EQ(p.g, g);
	REQUIRE_EQ(p.b, b);
	REQUIRE_EQ(p.a, a);
}

// BC1 block: colors little-endian, then 16 2-bit indices (texel 0 in the lowest bits).
void bc1Block(uint8_t out[8], uint16_t c0, uint16_t c1, const int idx[16])
{
	out[0] = (uint8_t)(c0 & 0xFF);
	out[1] = (uint8_t)(c0 >> 8);
	out[2] = (uint8_t)(c1 & 0xFF);
	out[3] = (uint8_t)(c1 >> 8);
	uint32_t bits = 0;
	for (int i = 0; i < 16; i++) bits |= (uint32_t)(idx[i] & 3) << (2 * i);
	out[4] = (uint8_t)bits;
	out[5] = (uint8_t)(bits >> 8);
	out[6] = (uint8_t)(bits >> 16);
	out[7] = (uint8_t)(bits >> 24);
}

void bc3AlphaBlock(uint8_t out[8], uint8_t a0, uint8_t a1, const int idx[16])
{
	out[0] = a0;
	out[1] = a1;
	uint64_t bits = 0;
	for (int i = 0; i < 16; i++) bits |= (uint64_t)(idx[i] & 7) << (3 * i);
	for (int i = 0; i < 6; i++) out[2 + i] = (uint8_t)(bits >> (8 * i));
}

const uint16_t RED = 0xF800, BLUE = 0x001F, WHITE = 0xFFFF, BLACK = 0x0000;

} // namespace

TEST(rgb565_expands_to_full_range)
{
	uint8_t r, g, b;
	unpackRGB565(WHITE, &r, &g, &b);
	REQUIRE_EQ((int)r, 255); REQUIRE_EQ((int)g, 255); REQUIRE_EQ((int)b, 255);
	unpackRGB565(BLACK, &r, &g, &b);
	REQUIRE_EQ((int)r, 0); REQUIRE_EQ((int)g, 0); REQUIRE_EQ((int)b, 0);
	unpackRGB565(0x07E0, &r, &g, &b);	// pure green, 6 bits
	REQUIRE_EQ((int)r, 0); REQUIRE_EQ((int)g, 255); REQUIRE_EQ((int)b, 0);
	unpackRGB565(0x8410, &r, &g, &b);	// 16/31, 32/63, 16/31
	REQUIRE_EQ((int)r, 16 * 255 / 31); REQUIRE_EQ((int)g, 32 * 255 / 63); REQUIRE_EQ((int)b, 16 * 255 / 31);
}

TEST(bc1_four_color_palette)
{
	const int idx[16] = {0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3};
	uint8_t blk[8], rgba[64];
	bc1Block(blk, RED, BLUE, idx);	// c0 > c1: 4-color mode
	decodeBC1Block(blk, true, rgba);
	requirePx(texel(rgba, 0), 255, 0, 0, 255);
	requirePx(texel(rgba, 1), 0, 0, 255, 255);
	requirePx(texel(rgba, 2), 170, 0, 85, 255);	// 2/3 c0 + 1/3 c1
	requirePx(texel(rgba, 3), 85, 0, 170, 255);	// 1/3 c0 + 2/3 c1
	for (int i = 0; i < 16; i++) REQUIRE_EQ(texel(rgba, i).a, 255);	// no transparent texel
}

TEST(bc1_texel_order_is_row_major_low_bits_first)
{
	// Only texel 5 (row 1, column 1) and texel 14 (row 3, column 2) use color1.
	int idx[16] = {0};
	idx[5] = 1;
	idx[14] = 1;
	uint8_t blk[8], rgba[64];
	bc1Block(blk, WHITE, BLACK, idx);
	decodeBC1Block(blk, true, rgba);
	for (int i = 0; i < 16; i++) {
		const int expect = (i == 5 || i == 14) ? 0 : 255;
		REQUIRE_EQ(texel(rgba, i).r, expect);
	}
}

TEST(bc1_three_color_mode_has_transparent_black)
{
	const int idx[16] = {0, 1, 2, 3, 3, 3, 3, 3, 0, 0, 0, 0, 1, 1, 1, 1};
	uint8_t blk[8], rgba[64];
	bc1Block(blk, BLUE, RED, idx);	// c0 <= c1: 3 colors + transparent
	decodeBC1Block(blk, true, rgba);
	requirePx(texel(rgba, 0), 0, 0, 255, 255);
	requirePx(texel(rgba, 1), 255, 0, 0, 255);
	requirePx(texel(rgba, 2), 127, 0, 127, 255);	// (c0 + c1) / 2
	requirePx(texel(rgba, 3), 0, 0, 0, 0);
	requirePx(texel(rgba, 7), 0, 0, 0, 0);
	// Without punch-through (the colour half of BC2/BC3) the same block is 4-color.
	decodeBC1Block(blk, false, rgba);
	requirePx(texel(rgba, 3), 170, 0, 85, 255);
}

TEST(bc1_equal_endpoints_solid_block_stays_opaque)
{
	// Encoders write a solid colour as c0 == c1 with all indices 0: opaque. Index 3 there would be
	// transparent black (D3D does the same), which an encoder never emits for an opaque texture.
	const int zeros[16] = {0};
	uint8_t blk[8], rgba[64];
	bc1Block(blk, 0x4208, 0x4208, zeros);
	decodeBC1Block(blk, true, rgba);
	for (int i = 0; i < 16; i++) {
		REQUIRE_EQ(texel(rgba, i).a, 255);
		REQUIRE(texel(rgba, i).r > 0);
	}
}

TEST(bc2_explicit_alpha_nibbles)
{
	uint8_t blk[16] = {0};
	// texel i alpha nibble = i, low nibble first
	for (int i = 0; i < 16; i += 2) blk[i / 2] = (uint8_t)(i | ((i + 1) << 4));
	const int zeros[16] = {0};
	bc1Block(blk + 8, BLUE, RED, zeros);	// c0 <= c1, still 4-color in BC2
	std::vector<uint8_t> out;
	REQUIRE(decodeLevel(KIND_BC2, 4, 4, blk, sizeof(blk), &out));
	REQUIRE_EQ(out.size(), (size_t)64);
	for (int i = 0; i < 16; i++) {
		requirePx(texel(out.data(), i), 0, 0, 255, i * 17);
	}
}

TEST(bc3_eight_alpha_mode)
{
	const int idx[16] = {0, 1, 2, 3, 4, 5, 6, 7, 0, 0, 0, 0, 1, 1, 1, 1};
	uint8_t a[8], alpha[16];
	bc3AlphaBlock(a, 210, 0, idx);	// a0 > a1: 6 interpolated values
	decodeBC3AlphaBlock(a, alpha);
	REQUIRE_EQ((int)alpha[0], 210);
	REQUIRE_EQ((int)alpha[1], 0);
	for (int i = 1; i <= 6; i++) REQUIRE_EQ((int)alpha[1 + i], (7 - i) * 210 / 7);
	REQUIRE_EQ((int)alpha[12], 0);
}

TEST(bc3_six_alpha_mode_has_0_and_255)
{
	const int idx[16] = {0, 1, 2, 3, 4, 5, 6, 7, 6, 6, 6, 6, 7, 7, 7, 7};
	uint8_t a[8], alpha[16];
	bc3AlphaBlock(a, 50, 100, idx);	// a0 <= a1: 4 interpolated + 0 + 255
	decodeBC3AlphaBlock(a, alpha);
	REQUIRE_EQ((int)alpha[0], 50);
	REQUIRE_EQ((int)alpha[1], 100);
	for (int i = 1; i <= 4; i++) REQUIRE_EQ((int)alpha[1 + i], ((5 - i) * 50 + i * 100) / 5);
	REQUIRE_EQ((int)alpha[6], 0);
	REQUIRE_EQ((int)alpha[7], 255);
	REQUIRE_EQ((int)alpha[15], 255);
}

TEST(bc3_color_block_never_punch_through)
{
	uint8_t blk[16];
	const int a255[16] = {0};
	bc3AlphaBlock(blk, 255, 255, a255);
	const int threes[16] = {3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3};
	bc1Block(blk + 8, BLUE, RED, threes);	// would be transparent black in DXT1
	std::vector<uint8_t> out;
	REQUIRE(decodeLevel(KIND_BC3, 4, 4, blk, sizeof(blk), &out));
	for (int i = 0; i < 16; i++) requirePx(texel(out.data(), i), 170, 0, 85, 255);
}

TEST(level_clips_partial_blocks)
{
	// 6x2: two blocks across, one down. Block 0 red, block 1 blue.
	const int zeros[16] = {0};
	uint8_t src[16];
	bc1Block(src, RED, BLUE, zeros);
	bc1Block(src + 8, BLUE, RED, zeros);
	std::vector<uint8_t> out;
	REQUIRE(decodeLevel(KIND_BC1, 6, 2, src, sizeof(src), &out));
	REQUIRE_EQ(out.size(), (size_t)6 * 2 * 4);
	for (int y = 0; y < 2; y++) {
		for (int x = 0; x < 6; x++) {
			const Px p = texel(out.data(), y * 6 + x);
			if (x < 4) requirePx(p, 255, 0, 0, 255);
			else requirePx(p, 0, 0, 255, 255);
		}
	}
}

TEST(level_sub_4x4_mip_reads_one_block)
{
	REQUIRE_EQ(levelBytes(KIND_BC1, 1, 1), (size_t)8);
	REQUIRE_EQ(levelBytes(KIND_BC3, 2, 1), (size_t)16);
	REQUIRE_EQ(levelBytes(KIND_BC1, 256, 256), (size_t)256 * 256 / 2);
	REQUIRE_EQ(levelBytes(KIND_BC3, 256, 256), (size_t)256 * 256);
	int idx[16] = {0};
	idx[0] = 1;	// texel (0,0) is color1
	uint8_t src[8];
	bc1Block(src, RED, BLUE, idx);
	std::vector<uint8_t> out;
	REQUIRE(decodeLevel(KIND_BC1, 1, 1, src, sizeof(src), &out));
	REQUIRE_EQ(out.size(), (size_t)4);
	requirePx(texel(out.data(), 0), 0, 0, 255, 255);
}

TEST(level_short_buffer_fails_cleanly)
{
	uint8_t src[24] = {0};
	std::vector<uint8_t> out(3, 7);
	REQUIRE(!decodeLevel(KIND_BC3, 8, 4, src, sizeof(src), &out));	// needs 32 bytes
	REQUIRE_EQ(out.size(), (size_t)3);	// untouched
	REQUIRE(!decodeLevel(KIND_BC1, 8, 4, nullptr, 16, &out));
	REQUIRE(decodeLevel(KIND_BC1, 8, 4, src, 16, &out));
}
