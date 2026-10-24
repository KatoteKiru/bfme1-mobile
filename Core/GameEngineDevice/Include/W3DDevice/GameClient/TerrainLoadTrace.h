// BFME: E1-42 timing trace for the terrain load path (Pixel 8 Pro rohan stall).
// A few lines per map load, always on, printed to stderr like [texchurn]/[launch].
#pragma once

#include <chrono>
#include <cstdio>

namespace TerrainLoadTrace {

inline double nowMs()
{
	using namespace std::chrono;
	return duration<double, std::milli>(steady_clock::now().time_since_epoch()).count();
}

// Scoped step: logs "begin" on entry and "end" with the elapsed time on exit.
class Step {
public:
	explicit Step(const char *name, int arg = -1) : m_name(name), m_arg(arg), m_t0(nowMs())
	{
		fprintf(stderr, "[terrainload] begin %s arg=%d\n", m_name, m_arg);
		fflush(stderr);
	}
	~Step()
	{
		fprintf(stderr, "[terrainload] end   %s arg=%d %.1f ms\n", m_name, m_arg, nowMs() - m_t0);
		fflush(stderr);
	}
	double elapsed() const { return nowMs() - m_t0; }
private:
	const char *m_name;
	int m_arg;
	double m_t0;
};

} // namespace TerrainLoadTrace

#define TERRAIN_TRACE(...) do { fprintf(stderr, "[terrainload] " __VA_ARGS__); fputc('\n', stderr); fflush(stderr); } while (0)
