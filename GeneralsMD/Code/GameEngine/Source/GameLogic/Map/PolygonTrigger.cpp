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

// PolygonTrigger.cpp
// Class to encapsulate polygon trigger areas.
// Author: John Ahlquist, November 2001

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "Common/DataChunk.h"
#include "Common/MapObject.h"
#include "Common/MapReaderWriterInfo.h"
#include "Common/Xfer.h"
#include "GameLogic/PolygonTrigger.h"
#include "GameLogic/TerrainLogic.h"

#include <stdio.h>	// BFME: release builds have no DEBUG_LOG; version and numPoints errors go to stderr.

/* ********* PolygonTrigger class ****************************/
PolygonTrigger *PolygonTrigger::ThePolygonTriggerListPtr = nullptr;
Int PolygonTrigger::s_currentID = 1;
/**
 PolygonTrigger - Constructor.
*/
PolygonTrigger::PolygonTrigger(Int initialAllocation) :
m_nextPolygonTrigger(nullptr),
m_points(nullptr),
m_numPoints(0),
m_sizePoints(0),
m_exportWithScripts(false),
m_isWaterArea(false),
m_shouldRender(true),
m_selected(false),
m_isRiver(FALSE),
m_riverStart(0),
// BFME: v5 water parameters. v1–v4 maps never read them.
m_waterAdditive(false),
m_waterColorR(0),
m_waterColorG(0),
m_waterColorB(0),
m_waterColorPad(0),
m_waterUvScrollU(0),
m_waterUvScrollV(0),
m_waterAlpha(0)
{
	if (initialAllocation < 2) initialAllocation = 2;
	m_points = NEW ICoord3D[initialAllocation];		// pool[]ify
	m_sizePoints = initialAllocation;
	m_triggerID = s_currentID++;

	m_waterHandle.m_polygon = this;

}


/**
 PolygonTrigger - Destructor - note - if linked, deletes linked items.
*/
PolygonTrigger::~PolygonTrigger()
{
	delete [] m_points;
	m_points = nullptr;

	if (m_nextPolygonTrigger) {
		PolygonTrigger *cur = m_nextPolygonTrigger;
		PolygonTrigger *next;
		while (cur) {
			next = cur->getNext();
			cur->setNextPoly(nullptr); // prevents recursion.
			deleteInstance(cur);
			cur = next;
		}
	}
}


/**
 PolygonTrigger::reallocate - increases the size of the points list.
 NOTE: It is expected that this will only get called in the editor, as in the game
 the poly triggers don't change.
*/
void PolygonTrigger::reallocate()
{
	DEBUG_ASSERTCRASH(m_numPoints <= m_sizePoints, ("Invalid m_numPoints."));
	if (m_numPoints == m_sizePoints) {
		if (m_sizePoints > INT_MAX / 2) {
			DEBUG_CRASH(("Too many points to allocate."));
			return;
		}
		// Reallocate.
		m_sizePoints += m_sizePoints;
		ICoord3D *newPts = NEW ICoord3D[m_sizePoints];
		Int i;
		for (i=0; i<m_numPoints; i++) {
			newPts[i] = m_points[i];
		}
		delete [] m_points;
		m_points = newPts;
	}
}

/**
* Find the polygon trigger with the matching ID
*/
PolygonTrigger *PolygonTrigger::getPolygonTriggerByID(Int triggerID)
{

	for( PolygonTrigger *poly = PolygonTrigger::getFirstPolygonTrigger();
			 poly; poly = poly->getNext() )
		if( poly->getID() == triggerID )
			return poly;

	// not found
	return nullptr;

}

const AsciiString& PolygonTrigger::getWaterTexture(Int index) const
{
	// BFME: out-of-range index returns an empty name instead of a point past the array.
	static const AsciiString empty;
	if (index < 0 || index >= WATER_TEX_COUNT) {
		return empty;
	}
	return m_waterTextures[index];
}

/**
* PolygonTrigger::ParsePolygonTriggersDataChunk - read a polygon triggers chunk.
* Format is the newer CHUNKY format.
*	See PolygonTrigger::WritePolygonTriggersDataChunk for the writer.
*	Input: DataChunkInput
*
*/
Bool PolygonTrigger::ParsePolygonTriggersDataChunk(DataChunkInput &file, DataChunkInfo *info, void *userData)
{
	Int count;
	Int numPoints;
	Int triggerID;
	Int maxTriggerId = 0;
	Bool isWater;
	Bool isRiver;
	Int riverStart;
	AsciiString triggerName;
	AsciiString layerName;
	// BFME: a version this parser has never seen must not be read as v4. v5 inserts
	// water fields before numPoints; the next version would be read as a point count.
	if (info->version > K_TRIGGERS_VERSION_5) {
		DEBUG_LOG(("PolygonTriggers: unsupported version v%d (newest supported v%d)", info->version, K_TRIGGERS_VERSION_5));
		fprintf(stderr, "PolygonTriggers: unsupported version v%d (newest supported v%d)\n", info->version, K_TRIGGERS_VERSION_5);
		// BFME: the throw used to land before deleteTriggers and keep the previous map's polygons.
		PolygonTrigger::deleteTriggers();
		throw ERROR_CORRUPT_FILE_FORMAT;
	}
	// Remove any existing polygon triggers, if any.
	PolygonTrigger::deleteTriggers(); // just in case.
	PolygonTrigger *pPrevTrig = nullptr;
	ICoord3D loc;
	count = file.readInt();
	while (count>0) {
		count--;
		triggerName = file.readAsciiString();
		if (info->version >= K_TRIGGERS_VERSION_4) {
			layerName = file.readAsciiString();
		}
		triggerID = file.readInt();
		isWater = false;
		if (info->version >= K_TRIGGERS_VERSION_2) {
			isWater = file.readByte();
		}
		isRiver = false;
		riverStart = 0;
		if (info->version >= K_TRIGGERS_VERSION_3) {
			isRiver = file.readByte();
			riverStart = file.readInt();
		}

		// BFME: v5 fields sit between riverStart and numPoints. v1–v4 do not have them.
		// Order: 6 texture names, additive, RGB + pad, uv scroll (2 floats), alpha.
		AsciiString waterTextures[WATER_TEX_COUNT];
		Bool waterAdditive = false;
		Byte waterR = 0;
		Byte waterG = 0;
		Byte waterB = 0;
		Byte waterPad = 0;
		Real uvU = 0;
		Real uvV = 0;
		Real waterAlpha = 0;
		if (info->version >= K_TRIGGERS_VERSION_5) {
			Int t;
			for (t = 0; t < WATER_TEX_COUNT; t++) {
				waterTextures[t] = file.readAsciiString();
			}
			waterAdditive = file.readByte() ? true : false;
			waterR = file.readByte();
			waterG = file.readByte();
			waterB = file.readByte();
			waterPad = file.readByte();
			uvU = file.readReal();
			uvV = file.readReal();
			waterAlpha = file.readReal();
			// BFME: readAsciiString only asserts a past-end read in debug. Release reads the
			// bytes anyway, and dataLeft goes negative. Reject that before numPoints.
			const UnsignedInt chunkBytes = file.getChunkDataSize();
			const UnsignedInt bytesLeft = file.getChunkDataSizeLeft();
			if (bytesLeft > chunkBytes || bytesLeft < sizeof(Int)) {
				DEBUG_LOG(("PolygonTriggers: '%s' v5 water fields do not fit the chunk (%u bytes left of %u)",
						triggerName.str(), bytesLeft, chunkBytes));
				fprintf(stderr, "PolygonTriggers: '%s' v5 water fields do not fit the chunk (%u bytes left of %u)\n",
						triggerName.str(), bytesLeft, chunkBytes);
				PolygonTrigger::deleteTriggers();
				throw ERROR_CORRUPT_FILE_FORMAT;
			}
		}

		numPoints = file.readInt();
		// BFME: one point is 3 Ints, 12 bytes. The check is before the allocation: a v4
		// read of a v5 texture name yields numPoints 1465122829 and a ~17.6 GB new[].
		// bytesLeft above the chunk size means an earlier read already passed the end.
		{
			const UnsignedInt chunkBytes = file.getChunkDataSize();
			const UnsignedInt bytesLeft = file.getChunkDataSizeLeft();
			const UnsignedInt maxPoints = bytesLeft / 12;
			if (bytesLeft > chunkBytes || numPoints < 0 || (UnsignedInt)numPoints > maxPoints) {
				DEBUG_LOG(("PolygonTriggers: '%s' numPoints %d is outside 0..%u (%u bytes left)",
						triggerName.str(), numPoints, maxPoints, bytesLeft));
				fprintf(stderr, "PolygonTriggers: '%s' numPoints %d is outside 0..%u (%u bytes left)\n",
						triggerName.str(), numPoints, maxPoints, bytesLeft);
				// Drop polygons already taken from this chunk. The bad one is not allocated yet.
				PolygonTrigger::deleteTriggers();
				throw ERROR_CORRUPT_FILE_FORMAT;
			}
		}
		PolygonTrigger *pTrig = newInstance(PolygonTrigger)(numPoints+1);
		pTrig->setTriggerName(triggerName);
		if (info->version >= K_TRIGGERS_VERSION_4) {
			pTrig->setLayerName(layerName);
		}
		pTrig->setWaterArea(isWater);
		pTrig->setRiver(isRiver);
		pTrig->setRiverStart(riverStart);
		if (info->version >= K_TRIGGERS_VERSION_5) {
			Int t;
			for (t = 0; t < WATER_TEX_COUNT; t++) {
				pTrig->m_waterTextures[t] = waterTextures[t];
			}
			pTrig->m_waterAdditive = waterAdditive;
			pTrig->m_waterColorR = waterR;
			pTrig->m_waterColorG = waterG;
			pTrig->m_waterColorB = waterB;
			pTrig->m_waterColorPad = waterPad;
			pTrig->m_waterUvScrollU = uvU;
			pTrig->m_waterUvScrollV = uvV;
			pTrig->m_waterAlpha = waterAlpha;
		}
		pTrig->m_triggerID = triggerID;
		if (triggerID > maxTriggerId) {
			maxTriggerId = triggerID;
		}
		Int i;
		for (i=0; i<numPoints; i++) {
			loc.x = file.readInt();
			loc.y = file.readInt();
			loc.z = file.readInt();
			pTrig->addPoint(loc);
		}
		if (numPoints<2) {
			DEBUG_LOG(("Deleting polygon trigger '%s' with %d points.",
					pTrig->getTriggerName().str(), numPoints));
			deleteInstance(pTrig);
			continue;
		}
		if (pPrevTrig) {
			pPrevTrig->setNextPoly(pTrig);
		} else {
			PolygonTrigger::addPolygonTrigger(pTrig);
		}
		pPrevTrig = pTrig;
	}
	if (info->version == K_TRIGGERS_VERSION_1)
	{
		// before water areas existed, so create a default one.
		PolygonTrigger *pTrig = newInstance(PolygonTrigger)(4);
		pTrig->setWaterArea(true);
#ifdef RTS_DEBUG
		pTrig->setTriggerName("AutoAddedWaterAreaTrigger");
#endif
		pTrig->m_triggerID = maxTriggerId++;
		loc.x = -30*MAP_XY_FACTOR;
		loc.y = -30*MAP_XY_FACTOR;
		loc.z = 7;  // The old water position.
		pTrig->addPoint(loc);
		loc.x = 30*MAP_XY_FACTOR + TheGlobalData->m_waterExtentX;
		pTrig->addPoint(loc);
		loc.y = 30*MAP_XY_FACTOR + TheGlobalData->m_waterExtentY;
		pTrig->addPoint(loc);
		loc.x = -30*MAP_XY_FACTOR;
		pTrig->addPoint(loc);
		if (pPrevTrig) {
			pPrevTrig->setNextPoly(pTrig);
		} else {
			PolygonTrigger::addPolygonTrigger(pTrig);
		}
		pPrevTrig = pTrig;
	}
	s_currentID = maxTriggerId+1;
	DEBUG_ASSERTCRASH(file.atEndOfChunk(), ("Incorrect data file length."));
	return true;
}

/**
* PolygonTrigger::WritePolygonTriggersDataChunk - Writes a Polygon triggers chunk.
* Format is the newer CHUNKY format.
*	See PolygonTrigger::ParsePolygonTriggersDataChunk for the reader.
*	Input: DataChunkInput
*
*/
void PolygonTrigger::WritePolygonTriggersDataChunk(DataChunkOutput &chunkWriter)
{
	// BFME: TODO(E1-29): still writes v4, so the six water texture names, additive,
	// color, uv scroll and alpha are dropped. WorldBuilder is unchanged. See docs/E1-BACKLOG.md.
	chunkWriter.openDataChunk("PolygonTriggers", 	K_TRIGGERS_VERSION_4);

		PolygonTrigger *pTrig;
		Int count = 0;
		for (pTrig=PolygonTrigger::getFirstPolygonTrigger(); pTrig; pTrig = pTrig->getNext()) {
			count++;
		}
		chunkWriter.writeInt(count);
		for (pTrig=PolygonTrigger::getFirstPolygonTrigger(); pTrig; pTrig = pTrig->getNext()) {
			chunkWriter.writeAsciiString(pTrig->getTriggerName());
			chunkWriter.writeAsciiString(pTrig->getLayerName());
			chunkWriter.writeInt(pTrig->getID());
			chunkWriter.writeByte(pTrig->isWaterArea());
			chunkWriter.writeByte(pTrig->isRiver());
			chunkWriter.writeInt(pTrig->getRiverStart());
			chunkWriter.writeInt(pTrig->getNumPoints());
			Int i;
			for (i=0; i<pTrig->getNumPoints(); i++) {
				ICoord3D loc = *pTrig->getPoint(i);
				chunkWriter.writeInt( loc.x);
				chunkWriter.writeInt( loc.y);
				chunkWriter.writeInt( loc.z);
			}
		}

	chunkWriter.closeDataChunk();
}

/**
 PolygonTrigger::updateBounds - Updates the bounds.
*/
void PolygonTrigger::updateBounds()	const
{
	const Int BIG_INT=0x7ffff0;
	m_bounds.lo.x = m_bounds.lo.y = BIG_INT;
	m_bounds.hi.x = m_bounds.hi.y = -BIG_INT;
	Int i;
	for (i=0; i<m_numPoints; i++) {
		if (m_points[i].x < m_bounds.lo.x) m_bounds.lo.x = m_points[i].x;
		if (m_points[i].y < m_bounds.lo.y) m_bounds.lo.y = m_points[i].y;
		if (m_points[i].x > m_bounds.hi.x) m_bounds.hi.x = m_points[i].x;
		if (m_points[i].y > m_bounds.hi.y) m_bounds.hi.y = m_points[i].y;
	}
	m_boundsNeedsUpdate = 0;
	Real halfWidth = (m_bounds.hi.x - m_bounds.lo.x) / 2.0f;
	Real halfHeight = (m_bounds.hi.y + m_bounds.lo.y) / 2.0f;

	m_radius = sqrt(halfHeight*halfHeight + halfWidth*halfWidth);
}


/**
 PolygonTrigger::addPolygonTrigger adds a trigger to the list of triggers.
*/
void PolygonTrigger::addPolygonTrigger(PolygonTrigger *pTrigger)
{
	for (PolygonTrigger *pTrig=getFirstPolygonTrigger(); pTrig; pTrig = pTrig->getNext()) {
		DEBUG_ASSERTCRASH(pTrig != pTrigger, ("Attempting to add trigger already in list."));
		if (pTrig==pTrigger) return;
	}
	pTrigger->m_nextPolygonTrigger = ThePolygonTriggerListPtr;
	ThePolygonTriggerListPtr = pTrigger;
}

/**
 PolygonTrigger::removePolygonTrigger removes a trigger to the list of
	triggers.  note - does NOT delete pTrigger.
*/
void PolygonTrigger::removePolygonTrigger(PolygonTrigger *pTrigger)
{
	PolygonTrigger *pPrev = nullptr;
	PolygonTrigger *pTrig=getFirstPolygonTrigger();
	for (; pTrig; pTrig = pTrig->getNext()) {
		if (pTrig==pTrigger) break;
		pPrev = pTrig;
	}
	DEBUG_ASSERTCRASH(pTrig, ("Attempting to remove a polygon not in the list."));
	if (pTrig) {
		if (pPrev) {
			DEBUG_ASSERTCRASH(pTrigger==pPrev->m_nextPolygonTrigger, ("Logic error.  jba."));
			pPrev->m_nextPolygonTrigger = pTrig->m_nextPolygonTrigger;
		} else {
			DEBUG_ASSERTCRASH(pTrigger==ThePolygonTriggerListPtr, ("Logic error.  jba."));
			ThePolygonTriggerListPtr = pTrig->m_nextPolygonTrigger;
		}
	}
	pTrigger->m_nextPolygonTrigger = nullptr;
}

/**
 PolygonTrigger::deleteTriggers Deletes list of triggers.
*/
void PolygonTrigger::deleteTriggers()
{
	PolygonTrigger *pList = ThePolygonTriggerListPtr;
	ThePolygonTriggerListPtr = nullptr;
	s_currentID = 1;
	deleteInstance(pList);
}

/**
 PolygonTrigger::addPoint adds a point at the end of the polygon.
 NOTE: It is expected that this will only get called in the editor, as in the game
 the poly triggers don't change.
*/
void PolygonTrigger::addPoint(const ICoord3D &point)
{
	DEBUG_ASSERTCRASH(m_numPoints <= m_sizePoints, ("Invalid m_numPoints."));
	if (m_numPoints == m_sizePoints) {
		reallocate();
	}
	m_points[m_numPoints] = point;
	m_numPoints++;
	m_boundsNeedsUpdate = true;
}

/**
 PolygonTrigger::setPoint sets the point at index ndx.
 NOTE: It is expected that this will only get called in the editor, as in the game
 the poly triggers don't change.
*/
void PolygonTrigger::setPoint(const ICoord3D &point, Int ndx)
{
	DEBUG_ASSERTCRASH(ndx>=0 && ndx <= m_numPoints, ("Invalid ndx."));
	if (ndx<0) return;
	if (ndx == m_numPoints) {	// we are setting first available unused point
		addPoint(point);
		return;
	}
	if (ndx>m_numPoints) { // Can't skip points.
		return;
	}
	m_points[ndx] = point;
	m_boundsNeedsUpdate = true;
}

/**
 PolygonTrigger::insertPoint .
 NOTE: It is expected that this will only get called in the editor, as in the game
 the poly triggers don't change.
*/
void PolygonTrigger::insertPoint(const ICoord3D &point, Int ndx)
{
	DEBUG_ASSERTCRASH(ndx>=0 && ndx <= m_numPoints, ("Invalid ndx."));
	if (ndx<0) return;
	if (ndx == m_numPoints) {	// we are setting first available unused point
		addPoint(point);
		return;
	}
	if (m_numPoints == m_sizePoints) {
		reallocate();
	}
	Int i;
	for (i=m_numPoints; i>ndx; i--) {
		m_points[i] = m_points[i-1];
	}
	m_points[ndx] = point;
	m_numPoints++;
	m_boundsNeedsUpdate = true;
}

/**
 PolygonTrigger::deletePoint .
 NOTE: It is expected that this will only get called in the editor, as in the game
 the poly triggers don't change.
*/
void PolygonTrigger::deletePoint(Int ndx)
{
	DEBUG_ASSERTCRASH(ndx>=0 && ndx < m_numPoints, ("Invalid ndx."));
	if (ndx<0 || ndx>=m_numPoints) return;
	Int i;
	for (i=ndx; i<m_numPoints-1; i++) {
		m_points[i] = m_points[i+1];
	}
	m_numPoints--;
	m_boundsNeedsUpdate = true;
}

void PolygonTrigger::getCenterPoint(Coord3D* pOutCoord)	const
{
	DEBUG_ASSERTCRASH(pOutCoord != nullptr, ("pOutCoord was null. Non-Fatal, but shouldn't happen."));
	if (!pOutCoord) {
		return;
	}

	if (m_boundsNeedsUpdate) {
		updateBounds();
	}
	(*pOutCoord).x = (m_bounds.lo.x + m_bounds.hi.x) / 2.0f;
	(*pOutCoord).y = (m_bounds.lo.y + m_bounds.hi.y) / 2.0f;

	(*pOutCoord).z = TheTerrainLogic->getGroundHeight(pOutCoord->x, pOutCoord->y);
}

Real PolygonTrigger::getRadius()	const
{
	if (m_boundsNeedsUpdate) {
		updateBounds();
	}
	return m_radius;
}


/**
 PolygonTrigger - pointInTrigger.
*/
Bool PolygonTrigger::pointInTrigger(ICoord3D &point) const
{
	if (m_boundsNeedsUpdate) {
		updateBounds();
	}
	if (point.x < m_bounds.lo.x) return false;
	if (point.y < m_bounds.lo.y) return false;
	if (point.x > m_bounds.hi.x) return false;
	if (point.y > m_bounds.hi.y) return false;

	Bool inside = false;
	Int i;
	for (i=0; i<m_numPoints; i++) {
		ICoord3D pt1 = m_points[i];
		ICoord3D pt2;
		if (i==m_numPoints-1) {
			pt2 = m_points[0];
		} else {
			pt2 = m_points[i+1];
		}
		if (pt1.y == pt2.y) {
			continue; // ignore horizontal lines.
		}
		if (pt1.y < point.y && pt2.y < point.y) continue;
		if (pt1.y >= point.y && pt2.y >= point.y) continue;
		if (pt1.x<point.x && pt2.x < point.x) continue;
		// Line segment crosses ray from point x->infinity.
		Int dy = pt2.y-pt1.y;
		Int dx = pt2.x-pt1.x;

		Real intersectionX = pt1.x + (dx * (point.y-pt1.y)) / ((Real)dy);
		if (intersectionX >= point.x) {
			inside = !inside;
		}
	}
	return inside;
}

// ------------------------------------------------------------------------------------------------
const WaterHandle* PolygonTrigger::getWaterHandle()	const
{

	if( isWaterArea() )
		return &m_waterHandle;

	return nullptr;  // this polygon trigger is not a water area

}

Bool PolygonTrigger::isValid() const
{
	if (m_numPoints == 0) {
		return FALSE;
	}

	return TRUE;
}

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void PolygonTrigger::crc( Xfer *xfer )
{

}

// ------------------------------------------------------------------------------------------------
/** Xfer method
	* Version Info:
	* 1: Initial version */
// ------------------------------------------------------------------------------------------------
void PolygonTrigger::xfer( Xfer *xfer )
{

	// version
	XferVersion currentVersion = 1;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	// number of data points
	xfer->xferInt( &m_numPoints );

	// xfer all data points
	ICoord3D *point;
	for( Int i = 0; i < m_numPoints; ++i )
	{

		// get this point
		point = &m_points[ i ];

		// xfer point
		xfer->xferICoord3D( point );

	}

	// bounds
	xfer->xferIRegion2D( &m_bounds );

	// radius
	xfer->xferReal( &m_radius );

	// bounds need update
	xfer->xferBool( &m_boundsNeedsUpdate );

}

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void PolygonTrigger::loadPostProcess()
{

}
