#pragma once

// BFME: K6. Shared by the DX8 caps path, the GLES backend, and tests/texturebudget.
//
// Headless never calls WW3D::Init (W3DDisplay.cpp), so CurrentCaps and the
// D3D device stay null. DX8IndexBufferClass then null-dereferences
// Get_Current_Caps() (dx8indexbuffer.cpp). Device-resource managers must
// not run when deviceResourcesAvailable is false.
//
// Mesa llvmpipe / lavapipe report max texture edge 16384, and lavapipe's only
// memory heap is DEVICE_LOCAL|HOST_VISIBLE sized to host RAM. textureloader
// and DX8Wrapper::Create_Render_Target assign an oversized request *to*
// MaxTextureWidth rather than rejecting it, so that edge becomes a real
// allocation in system RAM (a 16384^2 A8R8G8B8 image is 1 GiB at mip 0,
// about 1.33 GiB for the full chain, and the GLES CPU shadow zero-fills it).
// The 4096 ceiling applies only to those software rasterizers. A real GPU
// (Android/Mali reports 8192) keeps the edge the driver reported.

namespace BfmeHostLimits {

constexpr unsigned kMaxTextureEdge = 4096u;

// Defensive ceiling for the GLES CPU shadow. A full 4096^2 A8R8G8B8 chain is
// ~85 MiB, so this does not reject a texture that already passed that edge.
// CreateTexture skips it when the adopted edge is larger.
constexpr unsigned long long kMaxCommittedTextureBytes = 128ull * 1024ull * 1024ull;

inline unsigned clampTextureEdge(unsigned reported)
{
	if (reported == 0 || reported > kMaxTextureEdge)
		return kMaxTextureEdge;
	return reported;
}

inline char asciiLower(char c)
{
	if (c >= 'A' && c <= 'Z')
		return static_cast<char>(c - 'A' + 'a');
	return c;
}

inline bool containsAsciiIgnoreCase(const char *haystack, const char *needle)
{
	if (haystack == nullptr || needle == nullptr || needle[0] == '\0')
		return false;
	for (const char *h = haystack; *h != '\0'; ++h) {
		const char *a = h;
		const char *b = needle;
		while (*a != '\0' && *b != '\0' && asciiLower(*a) == asciiLower(*b)) {
			++a;
			++b;
		}
		if (*b == '\0')
			return true;
	}
	return false;
}

// GL_RENDERER / D3DADAPTER_IDENTIFIER8::Description. "swrast" is the older
// Mesa software name alongside llvmpipe, lavapipe and softpipe.
inline bool softwareRasterizerName(const char *name)
{
	return containsAsciiIgnoreCase(name, "llvmpipe")
		|| containsAsciiIgnoreCase(name, "lavapipe")
		|| containsAsciiIgnoreCase(name, "softpipe")
		|| containsAsciiIgnoreCase(name, "swrast");
}

// android is always true under __ANDROID__. A phone keeps `reported` even
// when the string happens to match a software rasterizer.
inline unsigned textureEdgeForDevice(unsigned reported, const char *deviceName, bool android)
{
	if (android || !softwareRasterizerName(deviceName))
		return reported;
	return clampTextureEdge(reported);
}

// Bytes of `levels` mip levels (0 means the full chain down to 1x1).
// Returns 0 if a level's byte count does not fit in 64 bits. The D3D8
// pitch path multiplies in 32-bit and would wrap that into a huge request.
inline unsigned long long mipChainBytes(unsigned width, unsigned height, unsigned bpp, unsigned levels)
{
	if (width == 0 || height == 0 || bpp == 0)
		return 0;
	if (levels == 0) {
		levels = 1;
		unsigned tw = width;
		unsigned th = height;
		while (tw > 1 || th > 1) {
			tw = tw > 1 ? tw / 2 : 1;
			th = th > 1 ? th / 2 : 1;
			++levels;
		}
	}
	unsigned long long total = 0;
	unsigned tw = width;
	unsigned th = height;
	for (unsigned i = 0; i < levels; ++i) {
		if (tw != 0 && bpp > ~0ull / tw)
			return 0;
		const unsigned long long row = (unsigned long long)tw * bpp;
		if (th != 0 && row > ~0ull / th)
			return 0;
		const unsigned long long level = row * th;
		if (total > ~0ull - level)
			return 0;
		total += level;
		tw = tw > 1 ? tw / 2 : 1;
		th = th > 1 ? th / 2 : 1;
	}
	return total;
}

inline bool deviceResourcesAvailable(bool headless, const void *device)
{
	return !headless && device != nullptr;
}

}
