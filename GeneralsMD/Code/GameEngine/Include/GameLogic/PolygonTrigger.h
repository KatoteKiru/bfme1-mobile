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


// PolygonTrigger.h
// Class to encapsulate polygon triggers for maps.
// Note - Polygons are used for two reasons - one is area triggers for
// scripts, so units can be tested for entering or exiting areas, and
// second to specify areas that are filled with water in the map.
// See the m_isWaterArea to differentiate.
// Author: John Ahlquist, November 2001

#pragma once

#include "Common/GameMemory.h"
#include "Common/Snapshot.h"
#include "Common/STLTypedefs.h"

class DataChunkInput;
class DataChunkOutput;
struct DataChunkInfo;
class PolygonTrigger;
class Xfer;

// ------------------------------------------------------------------------------------------------
/** Water handles are used to represent instances of areas of water, no matter which type
	* of implementation the water is (grid, trigger area, etc) */
// ------------------------------------------------------------------------------------------------
class WaterHandle
{

public:

	WaterHandle() { m_polygon = nullptr; }

	///@todo we need to formalize the water systems
	PolygonTrigger *m_polygon;	///< valid when water is a polygon area, nullptr if water is a grid

};

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
class PolygonTrigger : public MemoryPoolObject,
											 public Snapshot
{
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE(PolygonTrigger, "PolygonTrigger")

public:
	// BFME: v5 texture-name count. Order is river, noise, alpha edge, sparkle, bump, sky.
	enum { WATER_TEX_COUNT = 6 };

protected:
	PolygonTrigger*		m_nextPolygonTrigger;		///< linked list.
	AsciiString				m_triggerName;		///< The name of this polygon area.
	Int								m_triggerID; ///< Unique int id for the trigger.
	WaterHandle				m_waterHandle;		///< handle to use this polygon as a water table
	ICoord3D*					m_points;	///< Points that are the polygon.
	Int								m_numPoints; ///< Num points in m_points.
	Int								m_sizePoints; ///< Space allocated for m_points.
	mutable IRegion2D	m_bounds;  ///< 2D bounding box for quick checks.
	mutable Real			m_radius;
	Int								m_riverStart;	///< Identifies the start point of the river.
	mutable Bool			m_boundsNeedsUpdate;
	Bool							m_exportWithScripts;
	Bool							m_isWaterArea; ///< Used to specify water areas in the map.
	Bool							m_isRiver;		///< Used to specify that a water area is a river.
	AsciiString			m_layerName;  ///< Used to specify the layer in the World Builder.
	Bool				m_shouldRender;
	Bool				m_selected;
	// BFME: PolygonTriggers v5, stored for the water renderer (E1-29). File order of the names:
	// river, noise, alpha edge, sparkle, bump, sky. v1–v4 leave these at the defaults.
	AsciiString			m_waterTextures[WATER_TEX_COUNT];
	Bool				m_waterAdditive;
	Byte				m_waterColorR;
	Byte				m_waterColorG;
	Byte				m_waterColorB;
	Byte				m_waterColorPad;
	Real				m_waterUvScrollU;
	Real				m_waterUvScrollV;
	Real				m_waterAlpha;

	static PolygonTrigger* ThePolygonTriggerListPtr;
	static Int s_currentID; ///< Current id for new triggers.

protected:
	void reallocate();
	void updateBounds() const;

	// snapshot methods
	virtual void crc( Xfer *xfer ) override;
	virtual void xfer( Xfer *xfer ) override;
	virtual void loadPostProcess() override;

public:
	PolygonTrigger(Int initialAllocation);
	//~PolygonTrigger();		///< Note that deleting the head of a list deletes all linked objects in the list.

public:
	static PolygonTrigger *getFirstPolygonTrigger() {return ThePolygonTriggerListPtr;}
	static PolygonTrigger *getPolygonTriggerByID(Int triggerID);
	static Bool ParsePolygonTriggersDataChunk(DataChunkInput &file, DataChunkInfo *info, void *userData);
	/// Writes Triggers Info
	static void WritePolygonTriggersDataChunk(DataChunkOutput &chunkWriter);
	static void deleteTriggers();

public:
	static void addPolygonTrigger(PolygonTrigger *pTrigger);
	static void removePolygonTrigger(PolygonTrigger *pTrigger);
	void setNextPoly(PolygonTrigger *nextPoly) {m_nextPolygonTrigger = nextPoly;} ///< Link the next map object.
	void addPoint(const ICoord3D &point);
	void setPoint(const ICoord3D &point, Int ndx);
	void insertPoint(const ICoord3D &point, Int ndx);
	void deletePoint(Int ndx);
	void setTriggerName(AsciiString name) {m_triggerName = name;};

	void setLayerName(AsciiString name) {m_layerName = name;};
	AsciiString getLayerName()  const {return m_layerName;}

	void setShouldRender(Bool toggle) {m_shouldRender = toggle;}
	Bool getShouldRender() {return m_shouldRender;}

	void setSelected(Bool toggle) {m_selected = toggle;}
	Bool getSelected() {return m_selected;}

	void getCenterPoint(Coord3D* pOutCoord) const;
	Real getRadius() const;

public:
	const ICoord3D *getPoint(Int ndx) const {if (ndx<0) ndx=0; if (ndx>=m_numPoints) ndx=m_numPoints-1; return m_points+ndx;} ///< Get a point.
	Int getNumPoints() const {return m_numPoints;}
	Int getID() const {return m_triggerID;}
	PolygonTrigger *getNext() {return m_nextPolygonTrigger;}
	const PolygonTrigger *getNext() const {return m_nextPolygonTrigger;}
	const AsciiString& getTriggerName()  const {return m_triggerName;} ///< Gets the trigger name.
	Bool pointInTrigger(ICoord3D &point) const;
	Bool doExportWithScripts() const {return m_exportWithScripts;}
	void setDoExportWithScripts(Bool val) {m_exportWithScripts = val;}
	Bool isWaterArea() const {return m_isWaterArea;}
	void setWaterArea(Bool val) {m_isWaterArea = val;}
	Bool isRiver() const {return m_isRiver;}
	void setRiver(Bool val) {m_isRiver = val;}
	Int getRiverStart() const {return m_riverStart;}
	void setRiverStart(Int val) {m_riverStart = val;}
	// BFME: v5 water parameters. Index 0..5 is river, noise, alpha edge, sparkle, bump, sky.
	const AsciiString& getWaterTexture(Int index) const;
	Bool isWaterAdditive() const {return m_waterAdditive;}
	Byte getWaterColorR() const {return m_waterColorR;}
	Byte getWaterColorG() const {return m_waterColorG;}
	Byte getWaterColorB() const {return m_waterColorB;}
	Byte getWaterColorPad() const {return m_waterColorPad;}
	Real getWaterUvScrollU() const {return m_waterUvScrollU;}
	Real getWaterUvScrollV() const {return m_waterUvScrollV;}
	Real getWaterAlpha() const {return m_waterAlpha;}
	const WaterHandle* getWaterHandle() const;
	Bool isValid() const;
};
