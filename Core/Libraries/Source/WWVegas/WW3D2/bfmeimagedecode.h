// BFME1 Mobile — engine
// SPDX-License-Identifier: GPL-3.0-or-later
//
// BFME: E1-44 JPEG/PNG textures. BFME1 2.22 keeps about a quarter of art\textures\ as .jpg/.png
// (often a pair: <name>.jpg colour + <name>.png with the alpha), while INI and W3D ask for
// <name>.tga. GameFileClass::Set_Name resolves the request to the real file; textureloader.cpp
// sniffs the magic bytes and decodes here instead of going through Targa.
//
// No D3D, WW3D or engine file system dependency on purpose: tests/texdecode builds this file
// alone. Decoder: stb_image v2.30 (Dependencies/stb), JPEG + PNG only.
#pragma once

#include <cstddef>
#include <vector>

namespace BfmeImage
{
	enum Kind
	{
		KIND_UNKNOWN = 0,
		KIND_JPEG,	///< FF D8 FF
		KIND_PNG,	///< 89 50 4E 47
	};

	/// Where the decoded alpha came from.
	enum AlphaSource
	{
		ALPHA_OPAQUE = 0,			///< JPEG without a usable sibling PNG: alpha 255
		ALPHA_OWN,					///< PNG decoded alone: its own alpha (255 if it has none)
		ALPHA_SIBLING_PNG,			///< JPEG colour + alpha of <name>.png
		ALPHA_SIBLING_MISMATCH,		///< sibling PNG had other dimensions: ignored, alpha 255
		ALPHA_SIBLING_UNREADABLE,	///< sibling PNG given but not decodable: ignored, alpha 255
	};

	struct Image
	{
		/// width*height*4 bytes, rows top-down (row 0 = top of the picture, like D3D surfaces
		/// and like the Targa path after its TGAIDF_YORIGIN flip). Byte order per pixel
		/// B, G, R, A = WW3D_FORMAT_A8R8G8B8 / D3DFMT_A8R8G8B8 in little-endian memory.
		std::vector<unsigned char> bgra;
		unsigned width = 0;
		unsigned height = 0;
		Kind kind = KIND_UNKNOWN;
		AlphaSource alpha = ALPHA_OPAQUE;
		unsigned siblingWidth = 0;		///< set for ALPHA_SIBLING_PNG / ALPHA_SIBLING_MISMATCH
		unsigned siblingHeight = 0;
	};

	/// Magic-byte check; KIND_UNKNOWN for anything else (Targa, DDS, short data).
	Kind Sniff(const void *data, size_t size);

	/// Header-only dimensions of a JPEG/PNG. False if not one or the header is broken.
	bool Get_Info(const void *data, size_t size, unsigned &width, unsigned &height);

	/// Decode a JPEG or PNG to top-down BGRA. For a JPEG, alpha_png (optional, may be null) is the
	/// sibling <name>.png: when it decodes to the same dimensions its alpha channel becomes the
	/// texture alpha (a PNG without an alpha channel contributes its luminance). Otherwise the
	/// JPEG stays opaque and out.alpha says why. alpha_png is ignored for a PNG input.
	/// On failure returns false and, if error is not null, points it at a static reason string.
	bool Decode_BGRA(const void *data, size_t size, const void *alpha_png, size_t alpha_size, Image &out, const char **error);

	/// Short label for logs: "jpg+png alpha", "jpg", "png".
	const char *Describe(const Image &img);
}
