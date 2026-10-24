// BFME1 Mobile — engine
// SPDX-License-Identifier: GPL-3.0-or-later
//
// BFME: E1-43. What WindowTransitions.ini describes for one window, split out of
// GameWindowTransitions.h so that the parser can be tested without the window system.
//
// Zero Hour names the style on a field of the Window block:
//
//   Window
//     WinName = MainMenu.wnd:ButtonSinglePlayer
//     Style = WINSCALEUP
//     FrameDelay = 0
//   End
//
// BFME1 names it on a Transition sub-block of the Window block, with parameters of its own
// (schema from OpenSAGE WindowTransitionWindow / WindowTransitionTransition):
//
//   Window
//     WinName = ...
//     FrameDelay = 0
//     Transition = FULLFADE
//       StartFrame = 0
//       EndFrame = 20
//       FadeColor = R:0 G:0 B:0
//     End
//   End
//
// Both forms land in TransitionWindowData. Before E1-43 the lenient parser skipped the
// Transition sub-block, every BFME window kept style 0 (FLASH), and a FLASH on a window that
// does not exist never finishes: FadeWholeScreen held the end of every map load.
#pragma once

#include "Common/INI.h"

class Transition;

enum
{
TRANSITION_FLASH = 0,
BUTTON_TRANSITION_FLASH,
WIN_FADE_TRANSITION,
WIN_SCALE_UP_TRANSITION,
MAINMENU_SCALE_UP_TRANSITION,
TEXT_TYPE_TRANSITION,
SCREEN_FADE_TRANSITION,
COUNT_UP_TRANSITION,
FULL_FADE_TRANSITION,
TEXT_ON_FRAME_TRANSITION,
MAINMENU_MEDIUM_SCALE_UP_TRANSITION,
MAINMENU_SMALL_SCALE_DOWN_TRANSITION,
CONTROL_BAR_ARROW_TRANSITION,
SCORE_SCALE_UP_TRANSITION,
REVERSE_SOUND_TRANSITION,
// BFME: E1-43 styles Zero Hour has no code for. They run as a HoldTransition (timing only).
SOUND_FADE_TRANSITION,
FREEZE_POST_LOAD_SOUNDS_TRANSITION,
IMAGE_FADE_TRANSITION,
IMAGE_CROSS_FADE_TRANSITION,

MAX_TRANSITION_WINDOW_STYLES
};

static const LookupListRec TransitionStyleNames[] =
{
	{ "FLASH",					TRANSITION_FLASH },
	{ "BUTTONFLASH",		BUTTON_TRANSITION_FLASH },
	{ "WINFADE",				WIN_FADE_TRANSITION },
	{ "WINSCALEUP",			WIN_SCALE_UP_TRANSITION },
	{ "MAINMENUSCALEUP",			MAINMENU_SCALE_UP_TRANSITION },
	{ "TYPETEXT",			TEXT_TYPE_TRANSITION },
	{ "SCREENFADE",			SCREEN_FADE_TRANSITION },
	{ "COUNTUP",			COUNT_UP_TRANSITION },
	{ "FULLFADE",			FULL_FADE_TRANSITION },
	{ "TEXTONFRAME",			TEXT_ON_FRAME_TRANSITION },
	{ "MAINMENUMEDIUMSCALEUP",			MAINMENU_MEDIUM_SCALE_UP_TRANSITION },
	{ "MAINMENUSMALLSCALEDOWN",			MAINMENU_SMALL_SCALE_DOWN_TRANSITION },
	{ "CONTROLBARARROW",			CONTROL_BAR_ARROW_TRANSITION },
	{ "SCORESCALEUP",			SCORE_SCALE_UP_TRANSITION },
	{ "REVERSESOUND",			REVERSE_SOUND_TRANSITION },
	{ "SOUNDFADE",			SOUND_FADE_TRANSITION },
	{ "FREEZE_POST_LOAD_SOUNDS",			FREEZE_POST_LOAD_SOUNDS_TRANSITION },
	{ "IMAGEFADE",			IMAGE_FADE_TRANSITION },
	{ "IMAGECROSSFADE",			IMAGE_CROSS_FADE_TRANSITION },
	{ nullptr, 0	}
};
static_assert(ARRAY_SIZE(TransitionStyleNames) == MAX_TRANSITION_WINDOW_STYLES + 1, "Incorrect array size");

/// The BFME `Transition = <STYLE> ... End` sub-block of a Window. Only the style and EndFrame are
/// used so far; the rest is kept so that the fields parse and are there for later work.
struct TransitionParams
{
	TransitionParams();

	Bool m_present;							///< the Window had a Transition sub-block
	Int m_startFrame;
	Int m_endFrame;
	AsciiString m_viewsToFade;
	Bool m_leaveSilent;
	Bool m_fadeInUnfrozenSounds;
	AsciiString m_fadeImage;
	AsciiString m_crossFadeImage;
	RGBColor m_fadeColor;
};

/// INI part of a TransitionWindow.
struct TransitionWindowData
{
	TransitionWindowData();

	AsciiString m_winName;
	Int m_frameDelay;					// what frame number we start on
	Int m_style;							// the style we'll be using
	TransitionParams m_transition;

	/// Window block fields, Zero Hour and BFME.
	static const FieldParse *getFieldParse() { return s_fieldParseTable; }
	static const FieldParse s_fieldParseTable[];
	/// `Transition = <STYLE>` line: reads the style, then the sub-block to its End.
	static void parseTransition( INI* ini, void *instance, void *store, const void *userData );

	/// TRUE if the style does something useful when the window was not found (or not named).
	static Bool styleWorksWithoutWindow( Int style );
	/// The style to build for this window, or TRANSITION_HOLD. haveWindow: the named window
	/// exists. A style that needs a window and has none, a BFME-only style and an unknown
	/// style all become a hold, which draws nothing and finishes on time, so a group with such
	/// a window still finishes.
	Int resolveStyle( Bool haveWindow ) const;
	/// Length of a hold: the BFME EndFrame when the sub-block gave one, else fallbackFrames
	/// (the length of the style it stands in for); at least 1.
	Int getHoldFrames( Int fallbackFrames ) const;
};

/// BFME: E1-43. Not a style name: TransitionWindowData::resolveStyle() for "run a hold".
enum { TRANSITION_HOLD = -1 };

/// BFME: E1-43. Draws nothing, touches no window, finishes after its frame length forward
/// and at frame 0 backward.
Transition *newHoldTransition( Int frameLength );
