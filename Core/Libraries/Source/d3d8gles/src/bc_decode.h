/*
** bc_decode.h - software S3TC/BC1-BC3 (DXT1-DXT5) block decoder.
**
** BFME: E1-48. Moved out of gles_pipeline.cpp unchanged in behaviour so that
** tests/bcdecode can check it without D3D or GL. gles_pipeline.cpp uses it as
** the fallback when the GL driver has no S3TC (Mali on Pixel 8 Pro:
** "DXT texture but WEBGL_compressed_texture_s3tc missing").
**
** Layout per the S3TC spec (EXT_texture_compression_s3tc) and D3D8:
**   BC1/DXT1: color0 u16 LE, color1 u16 LE (RGB565), 32 bits of 2-bit
**             indices, texel 0 in the lowest bits, row-major.
**             color0 > color1: 4-color block; color0 <= color1: 3 colors +
**             index 3 = transparent black (D3D decodes DXT1 this way always).
**   BC2/DXT2-3: 64 bits of 4-bit explicit alpha, then a BC1 color block that
**             is always decoded as 4-color.
**   BC3/DXT4-5: alpha0, alpha1, 48 bits of 3-bit indices, then a 4-color BC1
**             color block.
** DXT2/DXT4 (premultiplied) decode like DXT3/DXT5, as D3D8 does on upload.
**
** Header-only, no engine or D3D types. SPDX-License-Identifier: GPL-3.0-or-later
*/
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace bcdecode {

enum Kind {
	KIND_BC1 = 1,	///< DXT1
	KIND_BC2 = 2,	///< DXT2, DXT3
	KIND_BC3 = 3,	///< DXT4, DXT5
};

inline unsigned blockBytes(Kind kind)
{
	return kind == KIND_BC1 ? 8u : 16u;
}

inline void unpackRGB565(uint16_t c, uint8_t *r, uint8_t *g, uint8_t *b)
{
	*r = (uint8_t)(((c >> 11) & 0x1F) * 255 / 31);
	*g = (uint8_t)(((c >> 5)  & 0x3F) * 255 / 63);
	*b = (uint8_t)(( c        & 0x1F) * 255 / 31);
}

// One 8-byte BC1 color block -> 16 RGBA8 texels, row-major. allowPunchThrough
// selects DXT1's 3-color mode when color0 <= color1; BC2/BC3 pass false (their
// color block is always 4-color, alpha comes from the separate alpha block).
inline void decodeBC1Block(const uint8_t block[8], bool allowPunchThrough, uint8_t outRGBA[16 * 4])
{
	const uint16_t c0 = (uint16_t)(block[0] | (block[1] << 8));
	const uint16_t c1 = (uint16_t)(block[2] | (block[3] << 8));
	uint8_t r0, g0, b0, r1, g1, b1;
	unpackRGB565(c0, &r0, &g0, &b0);
	unpackRGB565(c1, &r1, &g1, &b1);

	uint8_t pal[4][4]; // [index][RGBA]
	pal[0][0] = r0; pal[0][1] = g0; pal[0][2] = b0; pal[0][3] = 255;
	pal[1][0] = r1; pal[1][1] = g1; pal[1][2] = b1; pal[1][3] = 255;
	const bool punchThrough = allowPunchThrough && c0 <= c1;
	if (!punchThrough) {
		pal[2][0] = (uint8_t)((2 * r0 + r1) / 3);
		pal[2][1] = (uint8_t)((2 * g0 + g1) / 3);
		pal[2][2] = (uint8_t)((2 * b0 + b1) / 3);
		pal[2][3] = 255;
		pal[3][0] = (uint8_t)((r0 + 2 * r1) / 3);
		pal[3][1] = (uint8_t)((g0 + 2 * g1) / 3);
		pal[3][2] = (uint8_t)((b0 + 2 * b1) / 3);
		pal[3][3] = 255;
	} else {
		pal[2][0] = (uint8_t)((r0 + r1) / 2);
		pal[2][1] = (uint8_t)((g0 + g1) / 2);
		pal[2][2] = (uint8_t)((b0 + b1) / 2);
		pal[2][3] = 255;
		pal[3][0] = 0; pal[3][1] = 0; pal[3][2] = 0; pal[3][3] = 0; // transparent black
	}

	const uint32_t indices = (uint32_t)block[4] | ((uint32_t)block[5] << 8) |
	                         ((uint32_t)block[6] << 16) | ((uint32_t)block[7] << 24);
	for (int i = 0; i < 16; i++) {
		const int idx = (indices >> (i * 2)) & 0x3;
		outRGBA[i * 4 + 0] = pal[idx][0];
		outRGBA[i * 4 + 1] = pal[idx][1];
		outRGBA[i * 4 + 2] = pal[idx][2];
		outRGBA[i * 4 + 3] = pal[idx][3];
	}
}

// One 8-byte BC3 interpolated alpha block -> 16 alpha values, row-major.
inline void decodeBC3AlphaBlock(const uint8_t block[8], uint8_t outAlpha[16])
{
	const uint8_t a0 = block[0];
	const uint8_t a1 = block[1];
	uint8_t pal[8];
	pal[0] = a0;
	pal[1] = a1;
	if (a0 > a1) {
		for (int i = 1; i <= 6; i++)
			pal[1 + i] = (uint8_t)(((7 - i) * a0 + i * a1) / 7);
	} else {
		for (int i = 1; i <= 4; i++)
			pal[1 + i] = (uint8_t)(((5 - i) * a0 + i * a1) / 5);
		pal[6] = 0;
		pal[7] = 255;
	}
	uint64_t bits = 0;
	for (int i = 0; i < 6; i++)
		bits |= (uint64_t)block[2 + i] << (8 * i);
	for (int i = 0; i < 16; i++) {
		const int idx = (int)((bits >> (i * 3)) & 0x7);
		outAlpha[i] = pal[idx];
	}
}

// One 8-byte BC2 explicit alpha block -> 16 alpha values (4 bit -> 8 bit).
inline void decodeBC2AlphaBlock(const uint8_t block[8], uint8_t outAlpha[16])
{
	for (int i = 0; i < 16; i++) {
		const uint8_t nibble = (block[i / 2] >> ((i & 1) * 4)) & 0xF;
		outAlpha[i] = (uint8_t)(nibble * 17);
	}
}

// Bytes a w x h level takes: whole 4x4 blocks, also for the last row/column
// and for mips smaller than 4x4.
inline size_t levelBytes(Kind kind, unsigned w, unsigned h)
{
	return (size_t)((w + 3) / 4) * ((h + 3) / 4) * blockBytes(kind);
}

// One whole level -> tightly packed w*h RGBA8 (row-major, top row first, as
// stored). Returns false and leaves out alone when srcSize is short.
inline bool decodeLevel(Kind kind, unsigned w, unsigned h, const uint8_t *src, size_t srcSize,
                        std::vector<uint8_t> *out)
{
	const unsigned bb = blockBytes(kind);
	const unsigned blocksWide = (w + 3) / 4;
	const unsigned blocksHigh = (h + 3) / 4;
	if (src == nullptr || out == nullptr || levelBytes(kind, w, h) > srcSize)
		return false;

	out->resize((size_t)w * h * 4);
	uint8_t *dst = out->data();

	for (unsigned by = 0; by < blocksHigh; by++) {
		for (unsigned bx = 0; bx < blocksWide; bx++) {
			const uint8_t *blockSrc = src + ((size_t)by * blocksWide + bx) * bb;
			uint8_t rgba[16 * 4];
			uint8_t alpha[16];

			if (kind == KIND_BC1) {
				decodeBC1Block(blockSrc, /*allowPunchThrough=*/true, rgba);
			} else {
				if (kind == KIND_BC2)
					decodeBC2AlphaBlock(blockSrc, alpha);
				else
					decodeBC3AlphaBlock(blockSrc, alpha);
				decodeBC1Block(blockSrc + 8, /*allowPunchThrough=*/false, rgba);
				for (int i = 0; i < 16; i++) rgba[i * 4 + 3] = alpha[i];
			}

			const unsigned baseX = bx * 4, baseY = by * 4;
			const unsigned maxX = (baseX + 4 <= w) ? 4 : (w - baseX);
			const unsigned maxY = (baseY + 4 <= h) ? 4 : (h - baseY);
			for (unsigned ty = 0; ty < maxY; ty++) {
				for (unsigned tx = 0; tx < maxX; tx++) {
					const uint8_t *s = &rgba[(ty * 4 + tx) * 4];
					uint8_t *d = &dst[((size_t)(baseY + ty) * w + (baseX + tx)) * 4];
					d[0] = s[0]; d[1] = s[1]; d[2] = s[2]; d[3] = s[3];
				}
			}
		}
	}
	return true;
}

} // namespace bcdecode
