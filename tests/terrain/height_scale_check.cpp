// BFME1 Mobile QA: E1-25, same "load" check as height_scale_check.py but on real float32
// hardware (build with the engine's determinism flags, ADR-0003):
//   g++ -std=c++17 -O2 -ffp-contract=off -fno-strict-aliasing -fwrapv height_scale_check.cpp
// Exit 0 if all 256 h8 values give bit-identical world heights in the old and new scale.
#include <cstdint>
#include <cstdio>
#include <cstring>

static uint32_t bits(float f) { uint32_t u; std::memcpy(&u, &f, 4); return u; }

int main()
{
	volatile float oldScale = 0.625f;      // MAP_HEIGHT_SCALE (ZH)
	volatile float newScale = 0.0390625f;  // BFME1 HeightMapData v5
	int bad = 0;
	for (int h8 = 0; h8 < 256; ++h8)
	{
		const float a = (float)h8 * oldScale;
		const float b = (float)(h8 << 4) * newScale;
		if (bits(a) != bits(b)) { std::printf("h8=%d old=%08X new=%08X\n", h8, bits(a), bits(b)); ++bad; }
	}
	std::printf("load: %d/256 bit-exact\n", 256 - bad);
	return bad == 0 ? 0 : 1;
}
