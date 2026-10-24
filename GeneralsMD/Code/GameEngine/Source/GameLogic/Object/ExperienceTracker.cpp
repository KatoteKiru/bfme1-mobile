/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2025 Electronic Arts Inc.
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
**
**	This program is distributed in the hope that it will be useful,
**	but WITHOUT ANY WARRANTY; without even the implied warranty of
**	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**	GNU General Public License for more details.
**
**	You should have received a copy of the GNU General Public License
**	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

////////////////////////////////////////////////////////////////////////////////
//																																						//
//  (c) 2001-2003 Electronic Arts Inc.																				//
//																																						//
////////////////////////////////////////////////////////////////////////////////

// FILE: ExperienceTracker.cpp //////////////////////////////////////////////////////////////////////
// Author: Graham Smallwood, February 2002
// Desc:   Keeps track of experience points so Veterance levels can be gained
///////////////////////////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "Common/Xfer.h"
#include "Common/ThingTemplate.h"
#include "GameLogic/BfmeAttributeModifiers.h"	// BFME: E2-04
#include "GameLogic/ExperienceTracker.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object.h"



//-------------------------------------------------------------------------------------------------
ExperienceTracker::ExperienceTracker(Object *parent) :
	m_parent(parent),
	m_currentLevel(LEVEL_REGULAR),
	m_experienceSink(INVALID_ID),
	m_experienceScalar( 1.0f ),
	m_currentExperience(0)
{
	m_bfmeLevel = 0;	// BFME: E2-04
	m_bfmeTable = nullptr;
	m_bfmeTableResolved = FALSE;
	resetTrainable();
}

//-------------------------------------------------------------------------------------------------
ExperienceTracker::~ExperienceTracker()
{
}

//-------------------------------------------------------------------------------------------------
Int ExperienceTracker::getExperienceValue( const Object* killer ) const
{
	// No experience for killing an ally, cheater.
	if( killer->getRelationship( m_parent ) == ALLIES )
		return 0;

	// BFME: E2-04. ExperienceAward of the level the victim is at.
	if( const BfmeLevelTable *table = getBfmeLevelTable() )
	{
		const BfmeExperienceLevel *level = table->getLevel( getBfmeLevel() );
		return level ? level->m_experienceAward : 0;
	}

	return m_parent->getTemplate()->getExperienceValue(m_currentLevel);
}

//-------------------------------------------------------------------------------------------------
// BFME: E2-04. BFME1 experience levels.
//-------------------------------------------------------------------------------------------------
const BfmeLevelTable* ExperienceTracker::getBfmeLevelTable() const
{
	if( !m_bfmeTableResolved )
	{
		m_bfmeTableResolved = TRUE;
		m_bfmeTable = TheBfmeAttributeStore ? TheBfmeAttributeStore->findLevelTable( m_parent->getTemplate()->getName() ) : nullptr;
		if( m_bfmeTable && m_bfmeTable->getMaxLevel() == 0 )
			m_bfmeTable = nullptr;
	}
	return m_bfmeTable;
}

//-------------------------------------------------------------------------------------------------
Int ExperienceTracker::getBfmeLevel() const
{
	const BfmeLevelTable *table = getBfmeLevelTable();
	if( table == nullptr )
		return 0;
	return max( m_bfmeLevel, table->levelForExperience( m_currentExperience ) );
}

//-------------------------------------------------------------------------------------------------
/** Applies the effects of every level between the last one reached and the one m_currentExperience
	gives: the level's AttributeModifiers (they stay, so the bonuses of successive levels stack) and
	its Upgrades. Then VeterancyLevel follows, for the Zero Hour code that reads it. */
void ExperienceTracker::refreshBfmeLevel( Bool provideFeedback )
{
	const BfmeLevelTable *table = getBfmeLevelTable();
	if( table == nullptr )
		return;

	const Int newLevel = table->levelForExperience( m_currentExperience );
	while( m_bfmeLevel < newLevel )
	{
		++m_bfmeLevel;
		const BfmeExperienceLevel *level = table->getLevel( m_bfmeLevel );
		if( level == nullptr )
			continue;
		for( size_t i = 0; i < level->m_attributeModifiers.size(); ++i )
			m_parent->bfmeApplyModifierList( TheBfmeAttributeStore->findModifierList( level->m_attributeModifiers[i] ), 0 );
		for( size_t i = 0; i < level->m_upgrades.size(); ++i )
		{
			const UpgradeTemplate *upgradeT = TheUpgradeCenter ? TheUpgradeCenter->findUpgrade( level->m_upgrades[i] ) : nullptr;
			if( upgradeT && !m_parent->hasUpgrade( upgradeT ) )
				m_parent->giveUpgrade( upgradeT );
		}
		if( m_bfmeLevel > 1 )
			fprintf( stderr, "[levels] frame %u %s id %u reached level %d (xp %d, %s)\n", TheGameLogic->getFrame(),
				m_parent->getTemplate()->getName().str(), (unsigned)m_parent->getID(), m_bfmeLevel, m_currentExperience, level->m_name.str() );
	}

	const VeterancyLevel oldVet = m_currentLevel;
	const VeterancyLevel newVet = BfmeLevelTable::veterancyForLevel( max( m_bfmeLevel, newLevel ) );
	if( newVet != oldVet )
	{
		m_currentLevel = newVet;
		m_parent->onVeterancyLevelChanged( oldVet, newVet, provideFeedback );
	}
}

//-------------------------------------------------------------------------------------------------
void ExperienceTracker::setBfmeLevel( Int level, Bool provideFeedback )
{
	const BfmeLevelTable *table = getBfmeLevelTable();
	if( table == nullptr || level <= getBfmeLevel() )
		return;
	m_currentExperience = max( m_currentExperience, table->experienceForLevel( level ) );
	refreshBfmeLevel( provideFeedback );
}

//-------------------------------------------------------------------------------------------------
void ExperienceTracker::gainBfmeLevels( Int levels, Int levelCap )
{
	const BfmeLevelTable *table = getBfmeLevelTable();
	if( table == nullptr || levels <= 0 )
		return;
	Int target = getBfmeLevel() + levels;
	if( levelCap > 0 )
		target = min( target, levelCap );
	setBfmeLevel( min( target, table->getMaxLevel() ), TRUE );
}

//-------------------------------------------------------------------------------------------------
Bool ExperienceTracker::isTrainable() const
{
	return m_isTrainable;
}

//-------------------------------------------------------------------------------------------------
void ExperienceTracker::setTrainable(Bool trainable)
{
	m_isTrainable = trainable;
}

//-------------------------------------------------------------------------------------------------
void ExperienceTracker::resetTrainable()
{
	m_isTrainable = m_parent->getTemplate()->isTrainable();
}

//-------------------------------------------------------------------------------------------------
Bool ExperienceTracker::isAcceptingExperiencePoints() const
{
	return isTrainable() || (m_experienceSink != INVALID_ID);
}

//-------------------------------------------------------------------------------------------------
void ExperienceTracker::setExperienceSink( ObjectID sink )
{
	m_experienceSink = sink;
}

//-------------------------------------------------------------------------------------------------
ObjectID ExperienceTracker::getExperienceSink() const
{
	return m_experienceSink;
}

//-------------------------------------------------------------------------------------------------
// Set Level to AT LEAST this... if we are already >= this level, do nothing.
void ExperienceTracker::setMinVeterancyLevel( VeterancyLevel newLevel, Bool provideFeedback )
{
	// This does not check for IsTrainable, because this function is for explicit setting,
	// so the setter is assumed to know what they are doing.  The game function
	// of addExperiencePoints cares about Trainability.
	if (m_currentLevel < newLevel)
	{
		VeterancyLevel oldLevel = m_currentLevel;
		m_currentLevel = newLevel;
		m_currentExperience = m_parent->getTemplate()->getExperienceRequired(m_currentLevel); //Minimum for this level
		if (m_parent)
			m_parent->onVeterancyLevelChanged( oldLevel, newLevel, provideFeedback );
	}
}

//-------------------------------------------------------------------------------------------------
void ExperienceTracker::setVeterancyLevel( VeterancyLevel newLevel, Bool provideFeedback )
{
	// This does not check for IsTrainable, because this function is for explicit setting,
	// so the setter is assumed to know what they are doing.  The game function
	// of addExperiencePoints cares about Trainability, if flagged thus.
	if (m_currentLevel != newLevel)
	{
		VeterancyLevel oldLevel = m_currentLevel;
		m_currentLevel = newLevel;
		m_currentExperience = m_parent->getTemplate()->getExperienceRequired(m_currentLevel); //Minimum for this level
		if (m_parent)
			m_parent->onVeterancyLevelChanged( oldLevel, newLevel, provideFeedback );
	}
}

//-------------------------------------------------------------------------------------------------
Bool ExperienceTracker::gainExpForLevel(Int levelsToGain, Bool canScaleForBonus)
{
	Int newLevel = (Int)m_currentLevel + levelsToGain;
	if (newLevel > LEVEL_LAST)
		newLevel = LEVEL_LAST;
	// gain what levels we can, even if we can't use 'em all
	if (newLevel > m_currentLevel)
	{
		Int experienceNeeded = m_parent->getTemplate()->getExperienceRequired(newLevel) - m_currentExperience;
		addExperiencePoints( experienceNeeded, canScaleForBonus );
		return true;
	}
	return false;
}

//-------------------------------------------------------------------------------------------------
Bool ExperienceTracker::canGainExpForLevel(Int levelsToGain) const
{
	Int newLevel = (Int)m_currentLevel + levelsToGain;
	// return true if we can gain levels, even if we can't gain ALL the levels requested
	if (newLevel > LEVEL_LAST)
		newLevel = LEVEL_LAST;
	return (newLevel > m_currentLevel);
}

//-------------------------------------------------------------------------------------------------
void ExperienceTracker::addExperiencePoints( Int experienceGain, Bool canScaleForBonus)
{
	if( m_experienceSink != INVALID_ID )
	{
		// I have been set up to give my experience to someone else
		Object *sinkPointer = TheGameLogic->findObjectByID( m_experienceSink );
		if( sinkPointer )
		{
			// Not a fatal failure if not valid, he died when I was in the air.
			sinkPointer->getExperienceTracker()->addExperiencePoints( experienceGain * m_experienceScalar, canScaleForBonus );
			return;
		}
	}

	if( !isTrainable() )
		return; //safety

	// BFME: E2-04. BFME levels: EXPERIENCE modifiers (leadership) multiply the gain, the level comes from
	// experiencelevels.ini instead of the template's ExperienceRequired.
	if( getBfmeLevelTable() )
	{
		Real gain = (Real)experienceGain;
		if( canScaleForBonus )
			gain *= m_experienceScalar * m_parent->getBfmeAttributes().experienceMult( TheGameLogic->getFrame() );
		m_currentExperience += (Int)gain;
		refreshBfmeLevel( TRUE );
		return;
	}

	VeterancyLevel oldLevel = m_currentLevel;

	Int amountToGain = experienceGain;
	if ( canScaleForBonus )
		amountToGain *= m_experienceScalar;


	m_currentExperience += amountToGain;

	Int levelIndex = 0;
	while( ( (levelIndex + 1) < LEVEL_COUNT)
		&&  m_currentExperience >= m_parent->getTemplate()->getExperienceRequired(levelIndex + 1)
		)
	{
		// If there is a higher level to qualify for, and I qualify for it, advance the index
		levelIndex++;
	}

	m_currentLevel = (VeterancyLevel)levelIndex;

	if( oldLevel != m_currentLevel )
	{
		// Edge trigger special level gain effects.
		m_parent->onVeterancyLevelChanged( oldLevel, m_currentLevel );
	}

}
//-------------------------------------------------------------------------------------------------
void ExperienceTracker::setExperienceAndLevel( Int experienceIn, Bool provideFeedback )
{
	if( m_experienceSink != INVALID_ID )
	{
		// I have been set up to give my experience to someone else
		Object *sinkPointer = TheGameLogic->findObjectByID( m_experienceSink );
		if( sinkPointer )
		{
			// Not a fatal failure if not valid, he died when I was in the air.
			sinkPointer->getExperienceTracker()->setExperienceAndLevel( experienceIn, provideFeedback );
			return;
		}
	}

	if( !isTrainable() )
		return; //safety

	// BFME: E2-04. Levels already reached are kept (their bonuses are not taken back).
	if( getBfmeLevelTable() )
	{
		m_currentExperience = experienceIn;
		refreshBfmeLevel( provideFeedback );
		return;
	}

	VeterancyLevel oldLevel = m_currentLevel;

	m_currentExperience = experienceIn;

	Int levelIndex = 0;
	while( ( (levelIndex + 1) < LEVEL_COUNT)
		&&  m_currentExperience >= m_parent->getTemplate()->getExperienceRequired(levelIndex + 1)
		)
	{
		// If there is a level to qualify for, and I qualify for it, advance the index
		levelIndex++;
	}

	m_currentLevel = (VeterancyLevel)levelIndex;

	if( oldLevel != m_currentLevel )
	{
		// Edge trigger special level gain effects.
		m_parent->onVeterancyLevelChanged( oldLevel, m_currentLevel, provideFeedback ); //<<== paradox! this may be a level lost!
	}

}

//-----------------------------------------------------------------------------
void ExperienceTracker::crc( Xfer *xfer )
{
	xfer->xferInt( &m_currentExperience );
	xfer->xferUser( &m_currentLevel, sizeof( VeterancyLevel ) );
#if !RETAIL_COMPATIBLE_CRC
	xfer->xferBool(&m_isTrainable);
	xfer->xferInt(&m_bfmeLevel);	// BFME: E2-04
#endif
}

//-----------------------------------------------------------------------------
/** Xfer method
	* Version Info:
	* 1: Initial version
	* 2: TheSuperHackers @tweak Serialize m_isTrainable
	* 3: BFME: E2-04. BFME level reached (m_bfmeLevel)
	*/
// ----------------------------------------------------------------------------
void ExperienceTracker::xfer( Xfer *xfer )
{

	// version
#if RETAIL_COMPATIBLE_XFER_SAVE
	XferVersion currentVersion = 1;
#else
	XferVersion currentVersion = 3;
#endif
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	// no need to save the m_parent pointer, it is connected on allocation time
	// m_parent

	// current level
	xfer->xferUser( &m_currentLevel, sizeof( VeterancyLevel ) );

	// current experience
	xfer->xferInt( &m_currentExperience );

	// experience sink
	xfer->xferObjectID( &m_experienceSink );

	// experience scalar
	xfer->xferReal( &m_experienceScalar );

	if (version >= 2)
		xfer->xferBool(&m_isTrainable);

	if (version >= 3)
		xfer->xferInt(&m_bfmeLevel);
}

//-----------------------------------------------------------------------------
void ExperienceTracker::loadPostProcess()
{

}

