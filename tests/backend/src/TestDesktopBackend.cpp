// BFME: E1-17b Desktop env -> backend. The Android cfg reader is not here.

#include "d3d8gles_desktop_backend.h"

#include <cstdio>
#include <cstring>

namespace {

struct Row
{
	const char *env;
	bool unset;
	bool useVulkan;
	BfmeDesktopBackendNotice notice;
	const char *stderrLine;
};

const Row kRows[] = {
	{ nullptr, true, true, BFME_DESKTOP_NOTICE_NONE, nullptr },
	{ "", false, true, BFME_DESKTOP_NOTICE_NONE, nullptr },
	{ "vulkan", false, true, BFME_DESKTOP_NOTICE_NONE, nullptr },
	{ "Vulkan", false, true, BFME_DESKTOP_NOTICE_NONE, nullptr },
	{ "VULKAN", false, true, BFME_DESKTOP_NOTICE_NONE, nullptr },
	{ "dxvk", false, true, BFME_DESKTOP_NOTICE_NONE, nullptr },
	{ "DXVK", false, true, BFME_DESKTOP_NOTICE_NONE, nullptr },
	{ "DxVk", false, true, BFME_DESKTOP_NOTICE_NONE, nullptr },
	{ "gles", false, false, BFME_DESKTOP_NOTICE_NONE, nullptr },
	{ "GLES", false, false, BFME_DESKTOP_NOTICE_NONE, nullptr },
	{ "Gles", false, false, BFME_DESKTOP_NOTICE_NONE, nullptr },
	{ "gles_angle", false, false, BFME_DESKTOP_NOTICE_ANGLE, "ANGLE недоступен на десктопе" },
	{ "GLES_ANGLE", false, false, BFME_DESKTOP_NOTICE_ANGLE, "ANGLE недоступен на десктопе" },
	{ "Gles_Angle", false, false, BFME_DESKTOP_NOTICE_ANGLE, "ANGLE недоступен на десктопе" },
	{ "foo", false, true, BFME_DESKTOP_NOTICE_UNKNOWN, "[d3d8gles] unknown GENERALSX_RENDER_BACKEND=foo, using vulkan" },
	{ "angle", false, true, BFME_DESKTOP_NOTICE_UNKNOWN, "[d3d8gles] unknown GENERALSX_RENDER_BACKEND=angle, using vulkan" },
	{ "dxvk ", false, true, BFME_DESKTOP_NOTICE_UNKNOWN, "[d3d8gles] unknown GENERALSX_RENDER_BACKEND=dxvk , using vulkan" },
	{ "vulkanx", false, true, BFME_DESKTOP_NOTICE_UNKNOWN, "[d3d8gles] unknown GENERALSX_RENDER_BACKEND=vulkanx, using vulkan" },
};

const char *Shown(const Row &row)
{
	if (row.unset) {
		return "(unset)";
	}
	if (row.env[0] == '\0') {
		return "(empty)";
	}
	return row.env;
}

const char *Result(bool useVulkan)
{
	return useVulkan ? "DXVK" : "GLES";
}

} // namespace

int main()
{
	int failed = 0;
	std::printf("GENERALSX_RENDER_BACKEND -> backend (desktop)\n");
	for (const Row &row : kRows) {
		const char *raw = row.unset ? nullptr : row.env;
		const BfmeDesktopBackendChoice choice = BfmeChooseDesktopBackend(raw);
		const bool useVulkan = (choice.backend == BFME_DESKTOP_DXVK);
		char buf[512];
		const char *line = BfmeDesktopBackendNoticeLine(choice, raw, buf, sizeof(buf));
		const bool backendOk = useVulkan == row.useVulkan;
		const bool noticeOk = choice.notice == row.notice;
		const bool lineOk = (row.stderrLine == nullptr && line == nullptr)
			|| (row.stderrLine != nullptr && line != nullptr && std::strcmp(line, row.stderrLine) == 0);
		if (!backendOk || !noticeOk || !lineOk) {
			++failed;
			std::printf("FAIL  %-12s -> %s notice=%d line='%s'\n",
				Shown(row), Result(useVulkan), static_cast<int>(choice.notice),
				line != nullptr ? line : "");
		} else {
			std::printf("ok    %-12s -> %s", Shown(row), Result(useVulkan));
			if (line != nullptr) {
				std::printf("  | %s", line);
			}
			std::printf("\n");
		}
	}
	if (std::strcmp(BfmeDesktopAngleUnavailableLine(), "ANGLE недоступен на десктопе") != 0) {
		std::printf("FAIL  ANGLE line text drifted\n");
		++failed;
	}
	if (failed != 0) {
		std::printf("%d row(s) failed\n", failed);
		return 1;
	}
	std::printf("%zu rows passed\n", sizeof(kRows) / sizeof(kRows[0]));
	return 0;
}
