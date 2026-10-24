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

// FILE: W3DModuleFactory.cpp /////////////////////////////////////////////////////////////////////
// Author: Colin Day, April 2001
// Desc:	 W3D specific module
///////////////////////////////////////////////////////////////////////////////////////////////////

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include "W3DDevice/Common/W3DModuleFactory.h"
#include "W3DDevice/GameClient/Module/W3DDebrisDraw.h"
#include "W3DDevice/GameClient/Module/W3DDefaultDraw.h"
#include "W3DDevice/GameClient/Module/W3DDependencyModelDraw.h"
#include "W3DDevice/GameClient/Module/W3DModelDraw.h"
#include "W3DDevice/GameClient/Module/W3DLaserDraw.h"
#include "W3DDevice/GameClient/Module/W3DOverlordTankDraw.h"
#include "W3DDevice/GameClient/Module/W3DOverlordTruckDraw.h"
#include "W3DDevice/GameClient/Module/W3DOverlordAircraftDraw.h"
#include "W3DDevice/GameClient/Module/W3DPoliceCarDraw.h"
#include "W3DDevice/GameClient/Module/W3DProjectileStreamDraw.h"
#include "W3DDevice/GameClient/Module/W3DRopeDraw.h"
#include "W3DDevice/GameClient/Module/W3DSupplyDraw.h"
#include "W3DDevice/GameClient/Module/W3DScienceModelDraw.h"
#include "W3DDevice/GameClient/Module/W3DTankDraw.h"
#include "W3DDevice/GameClient/Module/W3DTruckDraw.h"
#include "W3DDevice/GameClient/Module/W3DTankTruckDraw.h"
#include "W3DDevice/GameClient/Module/W3DTracerDraw.h"
#include "W3DDevice/GameClient/Module/W3DTreeDraw.h"
#include "W3DDevice/GameClient/Module/W3DPropDraw.h"
#include "Common/INILenient.h"	// BFME: E1-46

//-------------------------------------------------------------------------------------------------
/** Initialize method */
//-------------------------------------------------------------------------------------------------
void W3DModuleFactory::init()
{

	// extending functionality
	ModuleFactory::init();

	// add the specific module templates we need for the draw methods
	addModule( W3DDefaultDraw );
	addModule( W3DDebrisDraw );
	addModule( W3DModelDraw );
	addModule( W3DLaserDraw );
	addModule( W3DOverlordTankDraw );
	addModule( W3DOverlordTruckDraw );
	addModule( W3DOverlordAircraftDraw );
	addModule( W3DProjectileStreamDraw );
	addModule( W3DPoliceCarDraw );
	addModule( W3DRopeDraw );
	addModule( W3DScienceModelDraw );
	addModule( W3DSupplyDraw );
	addModule( W3DDependencyModelDraw );
	addModule( W3DTankDraw );
	addModule( W3DTruckDraw );
	addModule( W3DTracerDraw );
	addModule( W3DTankTruckDraw );
	addModule( W3DTreeDraw );
	addModule( W3DPropDraw );

	// BFME: E1-46. BFME1 2.22 draws nearly every object with W3DScriptedModelDraw (2483 uses),
	// hordes with W3DHordeModelDraw and the mumakil with W3DQuadrupedDraw
	// (docs/bfme-draw-modules.md). Unregistered, the lenient INI skipped those Draw blocks, so the
	// objects had no draw module and no render object at all (Pixel: perf-draws models=0 skin=0).
	// Until E1-34 gives them their own classes, they are W3DModelDraw: a static model from
	// DefaultModelConditionState / ModelConditionState, no AnimationState yet.
	// The lenient mode is on before TheModuleFactory is created (archive file system comes first).
	if (INILenient::isEnabled())
	{
		static const char *const bfmeModelDrawAliases[] =
		{
			"W3DScriptedModelDraw", "W3DHordeModelDraw", "W3DQuadrupedDraw",
		};
		for (size_t i = 0; i < sizeof(bfmeModelDrawAliases) / sizeof(bfmeModelDrawAliases[0]); ++i)
			addModuleInternal( W3DModelDraw::friend_newModuleInstance, W3DModelDraw::friend_newModuleData,
				W3DModelDraw::getModuleType(), AsciiString( bfmeModelDrawAliases[i] ), W3DModelDraw::getInterfaceMask() );
	}

}
