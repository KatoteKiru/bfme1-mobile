// BFME1 Mobile — tests (E1-43): WindowTransitions.ini Window blocks, Zero Hour and BFME forms.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The real INI.cpp and the real Window table (GameWindowTransitionsData.cpp) parse synthetic
// text written for these tests (no EA data). The WindowTransition block parser below stands in
// for the one in GameWindowTransitions.cpp, which needs the window system: it keeps the group's
// windows in a vector and feeds them to the real Window table. The BFME form follows the
// OpenSAGE schema (WindowTransitionWindow / WindowTransitionTransition).
#include "PreRTS.h"

#include "Common/FileSystem.h"
#include "Common/INI.h"
#include "Common/INILenient.h"
#include "Common/LocalFileSystem.h"
#include "GameClient/GameWindowTransitions.h"
#include "StdDevice/Common/StdLocalFileSystem.h"

#include "../vfs/TestHarness.h"

#include <cmath>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <string>
#include <unistd.h>
#include <vector>

// Transition's constructor and destructor live in GameWindowTransitionsStyles.cpp (with every
// style that draws); the hold needs only these two.
Transition::Transition() {}
Transition::~Transition() {}

namespace {

struct TestGroup {
	std::string name;
	Bool fireOnce = FALSE;
	std::vector<TransitionWindowData> windows;
};
std::vector<TestGroup> s_groups;

void parseTestWindow(INI *ini, void *instance, void *, const void *)
{
	TransitionWindowData window;
	ini->initFromINI(&window, TransitionWindowData::getFieldParse());
	static_cast<TestGroup *>(instance)->windows.push_back(window);
}

const FieldParse s_testGroupTable[] = {
	{ "Window", parseTestWindow, nullptr, 0 },
	{ "FireOnce", INI::parseBool, nullptr, offsetof(TestGroup, fireOnce) },
	{ nullptr, nullptr, nullptr, 0 },
};

std::string writeTransitionsIni(const char *name, const char *text)
{
	static std::string dir;
	if (dir.empty()) {
		char tmpl[] = "/tmp/bfme-ini-transitions-XXXXXX";
		REQUIRE(mkdtemp(tmpl) != nullptr);
		dir = tmpl;
	}
	const std::string path = dir + "/" + name;
	std::ofstream(path, std::ios::binary) << text;
	return path;
}

// TRUE if INI::load threw.
bool loadTransitionsThrows(const char *name, const char *text, bool lenient)
{
	if (TheLocalFileSystem == nullptr) {
		TheLocalFileSystem = new StdLocalFileSystem;
		TheLocalFileSystem->init();
	}
	if (TheFileSystem == nullptr)
		TheFileSystem = new FileSystem;
	const std::string path = writeTransitionsIni(name, text);
	INILenient::reset();
	INILenient::setEnabled(lenient ? TRUE : FALSE);
	s_groups.clear();
	bool threw = false;
	try {
		INI ini;
		ini.load(AsciiString(path.c_str()), INI_LOAD_OVERWRITE, nullptr);
	} catch (...) {
		threw = true;
	}
	INILenient::setEnabled(FALSE);
	return threw;
}

UnsignedInt skips()
{
	return INILenient::counters().fields + INILenient::counters().blocks + INILenient::counters().values;
}

const TestGroup &group(const char *name)
{
	for (const TestGroup &g : s_groups)
		if (g.name == name)
			return g;
	REQUIRE(!"group not parsed");
	return s_groups.front();
}

// Own text in the BFME shape: one window with a FULLFADE sub-block, then a Zero Hour style window.
const char *const kBfmeGroup =
	"WindowTransition FadeTest\n"
	"  FireOnce = Yes\n"
	"  Window\n"
	"    WinName = TestShell.wnd:Parent\n"
	"    FrameDelay = 3\n"
	"    Transition = FULLFADE\n"
	"      StartFrame = 2\n"
	"      EndFrame = 20\n"
	"      FadeColor = R:10 G:20 B:30\n"
	"      ViewsToFade = TestView\n"
	"      LeaveSilent = Yes\n"
	"    End\n"
	"  End\n"
	"  Window\n"
	"    WinName = TestShell.wnd:Button\n"
	"    Style = WINSCALEUP\n"
	"    FrameDelay = 1\n"
	"  End\n"
	"End\n"
	"WindowTransition SecondGroup\n"
	"  Window\n"
	"    WinName = TestShell.wnd:Other\n"
	"    Transition = FLASH\n"
	"    End\n"
	"  End\n"
	"End\n";

const char *const kZhGroup =
	"WindowTransition ZhTest\n"
	"  FireOnce = No\n"
	"  Window\n"
	"    WinName = TestMenu.wnd:Button\n"
	"    Style = BUTTONFLASH\n"
	"    FrameDelay = 4\n"
	"  End\n"
	"End\n";

const char *const kBfmeOnlyStyles =
	"WindowTransition BfmeOnly\n"
	"  Window\n    Transition = SOUNDFADE\n      EndFrame = 12\n      FadeInUnfrozenSounds = Yes\n    End\n  End\n"
	"  Window\n    Transition = FREEZE_POST_LOAD_SOUNDS\n    End\n  End\n"
	"  Window\n    Transition = IMAGEFADE\n      FadeImage = TestImage\n    End\n  End\n"
	"  Window\n    Transition = IMAGECROSSFADE\n      CrossFadeImage = TestImage2\n    End\n  End\n"
	"End\n";

// A style no table knows, then a field after the sub-block and a second window.
const char *const kUnknownStyle =
	"WindowTransition Unknown\n"
	"  Window\n"
	"    Transition = NOSUCHSTYLE\n"
	"      EndFrame = 9\n"
	"    End\n"
	"    FrameDelay = 5\n"
	"  End\n"
	"  Window\n"
	"    Style = WINFADE\n"
	"  End\n"
	"End\n";

// A field the sub-block table does not know, followed by known ones.
const char *const kUnknownParam =
	"WindowTransition UnknownParam\n"
	"  Window\n"
	"    Transition = FULLFADE\n"
	"      NewerField = 1\n"
	"      EndFrame = 7\n"
	"    End\n"
	"    FrameDelay = 2\n"
	"  End\n"
	"End\n";

} // namespace

// The "WindowTransition" block of theTypeTable; the real one lives in GameWindowTransitions.cpp.
void INI::parseWindowTransitions(INI *ini)
{
	TestGroup g;
	g.name = ini->getNextToken();
	ini->initFromINI(&g, s_testGroupTable);
	s_groups.push_back(g);
}

TEST(transitions_bfme_sub_block_parsed)
{
	REQUIRE(!loadTransitionsThrows("bfme.ini", kBfmeGroup, true));
	REQUIRE(skips() == 0);
	REQUIRE(s_groups.size() == 2);
	const TestGroup &g = group("FadeTest");
	REQUIRE(g.fireOnce == TRUE);
	REQUIRE(g.windows.size() == 2);

	const TransitionWindowData &fade = g.windows[0];
	REQUIRE(fade.m_winName == "TestShell.wnd:Parent");
	REQUIRE(fade.m_frameDelay == 3);
	REQUIRE(fade.m_style == FULL_FADE_TRANSITION);
	REQUIRE(fade.m_transition.m_present == TRUE);
	REQUIRE(fade.m_transition.m_startFrame == 2);
	REQUIRE(fade.m_transition.m_endFrame == 20);
	REQUIRE(std::fabs(fade.m_transition.m_fadeColor.red - 10.0f / 255.0f) < 1e-4f);
	REQUIRE(std::fabs(fade.m_transition.m_fadeColor.blue - 30.0f / 255.0f) < 1e-4f);
	REQUIRE(fade.m_transition.m_viewsToFade == "TestView");
	REQUIRE(fade.m_transition.m_leaveSilent == TRUE);

	const TransitionWindowData &button = g.windows[1];
	REQUIRE(button.m_style == WIN_SCALE_UP_TRANSITION);
	REQUIRE(button.m_frameDelay == 1);
	REQUIRE(button.m_transition.m_present == FALSE);

	const TestGroup &second = group("SecondGroup");
	REQUIRE(second.windows.size() == 1);
	REQUIRE(second.windows[0].m_style == TRANSITION_FLASH);
	REQUIRE(second.windows[0].m_transition.m_present == TRUE);
}

// The BFME form is valid in Zero Hour (strict) mode too: nothing is skipped, nothing throws.
TEST(transitions_bfme_sub_block_parses_strict)
{
	REQUIRE(!loadTransitionsThrows("bfme-strict.ini", kBfmeGroup, false));
	REQUIRE(group("FadeTest").windows[0].m_transition.m_endFrame == 20);
}

TEST(transitions_zh_style_field_unchanged)
{
	REQUIRE(!loadTransitionsThrows("zh.ini", kZhGroup, false));
	const TestGroup &g = group("ZhTest");
	REQUIRE(g.fireOnce == FALSE);
	REQUIRE(g.windows.size() == 1);
	REQUIRE(g.windows[0].m_winName == "TestMenu.wnd:Button");
	REQUIRE(g.windows[0].m_style == BUTTON_TRANSITION_FLASH);
	REQUIRE(g.windows[0].m_frameDelay == 4);
	REQUIRE(g.windows[0].m_transition.m_present == FALSE);
}

TEST(transitions_bfme_only_styles_parsed)
{
	REQUIRE(!loadTransitionsThrows("bfme-only.ini", kBfmeOnlyStyles, false));
	const TestGroup &g = group("BfmeOnly");
	REQUIRE(g.windows.size() == 4);
	REQUIRE(g.windows[0].m_style == SOUND_FADE_TRANSITION);
	REQUIRE(g.windows[0].m_transition.m_endFrame == 12);
	REQUIRE(g.windows[0].m_transition.m_fadeInUnfrozenSounds == TRUE);
	REQUIRE(g.windows[1].m_style == FREEZE_POST_LOAD_SOUNDS_TRANSITION);
	REQUIRE(g.windows[2].m_style == IMAGE_FADE_TRANSITION);
	REQUIRE(g.windows[2].m_transition.m_fadeImage == "TestImage");
	REQUIRE(g.windows[3].m_style == IMAGE_CROSS_FADE_TRANSITION);
	REQUIRE(g.windows[3].m_transition.m_crossFadeImage == "TestImage2");
}

TEST(transitions_unknown_style_skips_sub_block_lenient)
{
	REQUIRE(!loadTransitionsThrows("unknown.ini", kUnknownStyle, true));
	REQUIRE(INILenient::counters().values == 1);
	const TestGroup &g = group("Unknown");
	REQUIRE(g.windows.size() == 2);
	REQUIRE(g.windows[0].m_style == TRANSITION_FLASH);	// default kept
	REQUIRE(g.windows[0].m_transition.m_present == FALSE);
	REQUIRE(g.windows[0].m_frameDelay == 5);	// the field after the skipped sub-block
	REQUIRE(g.windows[1].m_style == WIN_FADE_TRANSITION);
}

TEST(transitions_unknown_style_throws_strict)
{
	REQUIRE(loadTransitionsThrows("unknown-strict.ini", kUnknownStyle, false));
}

TEST(transitions_unknown_param_skipped_lenient)
{
	REQUIRE(!loadTransitionsThrows("param.ini", kUnknownParam, true));
	REQUIRE(INILenient::counters().fields == 1);
	const TestGroup &g = group("UnknownParam");
	REQUIRE(g.windows.size() == 1);
	REQUIRE(g.windows[0].m_style == FULL_FADE_TRANSITION);
	REQUIRE(g.windows[0].m_transition.m_endFrame == 7);
	REQUIRE(g.windows[0].m_frameDelay == 2);
}

TEST(transitions_resolve_style)
{
	TransitionWindowData w;
	w.m_style = TRANSITION_FLASH;
	REQUIRE(w.resolveStyle(TRUE) == TRANSITION_FLASH);
	REQUIRE(w.resolveStyle(FALSE) == TRANSITION_HOLD);	// never finished without a window
	w.m_style = FULL_FADE_TRANSITION;
	REQUIRE(w.resolveStyle(TRUE) == FULL_FADE_TRANSITION);
	REQUIRE(w.resolveStyle(FALSE) == TRANSITION_HOLD);	// dereferenced the missing window
	w.m_style = SCREEN_FADE_TRANSITION;
	REQUIRE(w.resolveStyle(FALSE) == SCREEN_FADE_TRANSITION);
	w.m_style = REVERSE_SOUND_TRANSITION;
	REQUIRE(w.resolveStyle(FALSE) == REVERSE_SOUND_TRANSITION);
	for (Int s = SOUND_FADE_TRANSITION; s < MAX_TRANSITION_WINDOW_STYLES; ++s) {
		w.m_style = s;
		REQUIRE(w.resolveStyle(TRUE) == TRANSITION_HOLD);
	}
	w.m_style = MAX_TRANSITION_WINDOW_STYLES;
	REQUIRE(w.resolveStyle(TRUE) == TRANSITION_HOLD);
	w.m_style = -3;
	REQUIRE(w.resolveStyle(TRUE) == TRANSITION_HOLD);
}

TEST(transitions_hold_frames)
{
	TransitionWindowData w;
	REQUIRE(w.getHoldFrames(10) == 10);
	REQUIRE(w.getHoldFrames(0) == 1);
	w.m_transition.m_present = TRUE;
	w.m_transition.m_endFrame = 24;
	REQUIRE(w.getHoldFrames(10) == 24);
	w.m_transition.m_endFrame = 0;
	REQUIRE(w.getHoldFrames(10) == 10);
}

// A hold finishes forward at its length and backward at frame 0, as TransitionGroup steps it.
TEST(transitions_hold_finishes)
{
	std::unique_ptr<Transition> t(newHoldTransition(5));
	REQUIRE(t->getFrameLength() == 5);
	t->init(nullptr);
	REQUIRE(!t->isFinished());
	for (Int frame = 1; frame < 5; ++frame) {
		t->update(frame);
		REQUIRE(!t->isFinished());
	}
	t->update(5);
	REQUIRE(t->isFinished());

	t->reverse();
	REQUIRE(!t->isFinished());
	for (Int frame = 4; frame > 0; --frame) {
		t->update(frame);
		REQUIRE(!t->isFinished());
	}
	t->update(0);
	REQUIRE(t->isFinished());

	t->init(nullptr);
	t->skip();
	REQUIRE(t->isFinished());

	std::unique_ptr<Transition> empty(newHoldTransition(0));
	REQUIRE(empty->getFrameLength() == 1);
}
