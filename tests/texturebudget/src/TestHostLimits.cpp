// BFME: K6. The numbers below are what Mesa llvmpipe / lavapipe reported on
// this host: GL_MAX_TEXTURE_SIZE and maxImageDimension2D are 16384, and the
// only Vulkan heap is DEVICE_LOCAL|HOST_VISIBLE and the size of host RAM.
#include "bfme_host_limits.h"

#include <cstdio>
#include <cstdlib>

static int g_fails = 0;

static void expect(bool cond, const char *msg)
{
	if (cond)
		return;
	std::fprintf(stderr, "FAIL %s\n", msg);
	++g_fails;
}

int main()
{
	using namespace BfmeHostLimits;

	int sentinel = 0;
	expect(!deviceResourcesAvailable(true, &sentinel), "headless ignores a non-null device");
	expect(!deviceResourcesAvailable(false, nullptr), "null device is not usable");
	expect(deviceResourcesAvailable(false, &sentinel), "windowed with a device is usable");

	expect(clampTextureEdge(16384) == kMaxTextureEdge, "llvmpipe 16384 clamps to 4096");
	expect(clampTextureEdge(8192) == kMaxTextureEdge, "8192 clamps to 4096");
	expect(clampTextureEdge(kMaxTextureEdge) == kMaxTextureEdge, "4096 stays");
	expect(clampTextureEdge(2048) == 2048, "2048 stays");
	expect(clampTextureEdge(0) == kMaxTextureEdge, "a zero cap is not unlimited");

	expect(softwareRasterizerName("llvmpipe (LLVM 15.0.7, 256 bits)"), "llvmpipe renderer");
	expect(softwareRasterizerName("Mesa Lavapipe (LLVM 15.0.7, 256 bits)"), "lavapipe device");
	expect(softwareRasterizerName("softpipe"), "softpipe");
	expect(softwareRasterizerName("llvmpipe"), "bare llvmpipe");
	expect(softwareRasterizerName("swrast"), "swrast");
	expect(!softwareRasterizerName("Mali-G78"), "Mali is not software");
	expect(!softwareRasterizerName("NVIDIA GeForce RTX 3060"), "NVIDIA is not software");
	expect(!softwareRasterizerName("llv"), "a prefix of llvmpipe is not enough");
	expect(!softwareRasterizerName(nullptr), "null name is not software");

	expect(textureEdgeForDevice(16384, "llvmpipe (LLVM 15.0.7, 256 bits)", false) == kMaxTextureEdge,
		"desktop llvmpipe 16384 clamps");
	expect(textureEdgeForDevice(8192, "Mali-G78", false) == 8192, "Mali 8192 stays");
	expect(textureEdgeForDevice(16384, "NVIDIA GeForce RTX 3060", false) == 16384, "real GPU 16384 stays");
	expect(textureEdgeForDevice(8192, "llvmpipe", true) == 8192, "Android never clamps, even llvmpipe");
	expect(textureEdgeForDevice(8192, "Lavapipe (LLVM 15.0.7, 256 bits)", true) == 8192,
		"Android lavapipe string stays");
	expect(textureEdgeForDevice(2048, "softpipe", false) == 2048, "software edge under 4096 stays");
	expect(textureEdgeForDevice(0, "Mali-G78", false) == 0, "a real GPU zero cap is left alone");

	const unsigned long long uncapped = mipChainBytes(16384, 16384, 4, 0);
	const unsigned long long capped = mipChainBytes(kMaxTextureEdge, kMaxTextureEdge, 4, 0);
	const unsigned long long atlas = mipChainBytes(4096, 2048, 4, 3);
	expect(uncapped > (1ull << 30), "uncapped 16384 chain is over 1 GiB");
	expect(capped < kMaxCommittedTextureBytes, "4096 chain fits the commit budget");
	expect(capped > 0, "4096 chain is non-zero");
	expect(atlas > 0 && atlas < kMaxCommittedTextureBytes, "4096x2048 atlas at 3 mips fits");
	expect(mipChainBytes(0xffffffffu, 0xffffffffu, 4, 1) == 0, "32-bit pitch overflow is rejected");

	std::printf("llvmpipe 16384 full chain %llu bytes\n", uncapped);
	std::printf("capped 4096 full chain %llu bytes (budget %llu)\n", capped, kMaxCommittedTextureBytes);
	std::printf("atlas 4096x2048 x3 %llu bytes\n", atlas);
	if (g_fails != 0) {
		std::fprintf(stderr, "%d checks failed\n", g_fails);
		return 1;
	}
	std::printf("ok\n");
	return 0;
}
