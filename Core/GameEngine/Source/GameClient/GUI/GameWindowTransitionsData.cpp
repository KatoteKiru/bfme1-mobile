// BFME1 Mobile — engine
// SPDX-License-Identifier: GPL-3.0-or-later
//
// BFME: E1-43. WindowTransitions.ini Window block (Zero Hour and BFME forms) and the hold
// transition. See GameWindowTransitionsData.h.
#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "GameClient/GameWindowTransitions.h"

#include <algorithm>

TransitionParams::TransitionParams()
{
	m_present = FALSE;
	m_startFrame = 0;
	m_endFrame = 0;
	m_leaveSilent = FALSE;
	m_fadeInUnfrozenSounds = FALSE;
	m_fadeColor.red = m_fadeColor.green = m_fadeColor.blue = 0.0f;
}

TransitionWindowData::TransitionWindowData()
{
	m_frameDelay = 0;
	m_style = TRANSITION_FLASH;
}

const FieldParse TransitionWindowData::s_fieldParseTable[] =
{
	{ "WinName",				INI::parseAsciiString,		nullptr,							offsetof( TransitionWindowData, m_winName ) },
	{ "Style",					INI::parseLookupList,			TransitionStyleNames,	offsetof( TransitionWindowData, m_style ) },
	{ "FrameDelay",			INI::parseInt,						nullptr,							offsetof( TransitionWindowData, m_frameDelay ) },
	{ "Transition",			TransitionWindowData::parseTransition,	nullptr,	0 },
	{ nullptr,							nullptr,									nullptr,							0 }
};

void TransitionWindowData::parseTransition( INI* ini, void *instance, void * /*store*/, const void * /*userData*/ )
{
	static const FieldParse paramsFieldParse[] =
	{
		{ "StartFrame",						INI::parseInt,					nullptr,	offsetof( TransitionParams, m_startFrame ) },
		{ "EndFrame",							INI::parseInt,					nullptr,	offsetof( TransitionParams, m_endFrame ) },
		{ "ViewsToFade",					INI::parseAsciiString,	nullptr,	offsetof( TransitionParams, m_viewsToFade ) },
		{ "LeaveSilent",					INI::parseBool,					nullptr,	offsetof( TransitionParams, m_leaveSilent ) },
		{ "FadeInUnfrozenSounds",	INI::parseBool,					nullptr,	offsetof( TransitionParams, m_fadeInUnfrozenSounds ) },
		{ "FadeImage",						INI::parseAsciiString,	nullptr,	offsetof( TransitionParams, m_fadeImage ) },
		{ "CrossFadeImage",				INI::parseAsciiString,	nullptr,	offsetof( TransitionParams, m_crossFadeImage ) },
		{ "FadeColor",						INI::parseRGBColor,			nullptr,	offsetof( TransitionParams, m_fadeColor ) },
		{ nullptr,								nullptr,								nullptr,	0 }
	};

	TransitionWindowData *data = (TransitionWindowData *)instance;
	// The style is the rest of the header line: "Transition = FULLFADE". An unknown one throws
	// before the sub-block is read; the lenient parser then skips the sub-block.
	const Int style = INI::scanLookupList( ini->getNextToken(), TransitionStyleNames );

	TransitionParams params;
	ini->initFromINI( &params, paramsFieldParse );
	params.m_present = TRUE;

	data->m_style = style;
	data->m_transition = params;
}

Bool TransitionWindowData::styleWorksWithoutWindow( Int style )
{
	// These two never touch the window: one fades the whole screen, one plays a sound.
	return style == SCREEN_FADE_TRANSITION || style == REVERSE_SOUND_TRANSITION;
}

Int TransitionWindowData::resolveStyle( Bool haveWindow ) const
{
	if( m_style < 0 || m_style >= SOUND_FADE_TRANSITION )
		return TRANSITION_HOLD;	// unknown, or BFME-only with no Zero Hour code
	if( !haveWindow && !styleWorksWithoutWindow( m_style ) )
		return TRANSITION_HOLD;	// FLASH, FULLFADE, ... on no window never finish or crash
	return m_style;
}

Int TransitionWindowData::getHoldFrames( Int fallbackFrames ) const
{
	Int frames = fallbackFrames;
	if( m_transition.m_present && m_transition.m_endFrame > 0 )
		frames = m_transition.m_endFrame;
	return std::max( frames, 1 );
}

//-----------------------------------------------------------------------------
namespace
{

class HoldTransition : public Transition
{
public:
	explicit HoldTransition( Int frameLength )
	{
		m_frameLength = std::max( frameLength, 1 );
		m_win = nullptr;
		m_isFinished = FALSE;
		m_isForward = TRUE;
		m_isReversed = FALSE;
	}

	virtual void init( GameWindow * /*win*/ ) override
	{
		// The window, if any, is left as it is: a hold shows and hides nothing.
		m_isFinished = FALSE;
		m_isForward = TRUE;
	}

	virtual void update( Int frame ) override
	{
		if( m_isForward ? frame >= m_frameLength : frame <= 0 )
			m_isFinished = TRUE;
	}

	virtual void reverse() override
	{
		m_isFinished = FALSE;
		m_isForward = FALSE;
	}

	virtual void draw() override {}

	virtual void skip() override
	{
		m_isFinished = TRUE;
	}
};

} // namespace

Transition *newHoldTransition( Int frameLength )
{
	return NEW HoldTransition( frameLength );
}
