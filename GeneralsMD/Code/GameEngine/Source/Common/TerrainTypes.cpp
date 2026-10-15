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

// FILE: TerrainTypes.cpp /////////////////////////////////////////////////////////////////////////
// Author: Colin Day, December 2001
// Desc:   Terrain type descriptions and collection
///////////////////////////////////////////////////////////////////////////////////////////////////

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#define DEFINE_TERRAIN_TYPE_NAMES

#include "Common/INI.h"
#include "Common/TerrainTypes.h"
#include "Common/INILenient.h"	// BFME: E1-26b

// BFME: E1-26b. BFME1 2.22 terrain.ini writes "Class = Type Grass NEXT Region Edoras NEXT Region
// Rivendell": a terrain type word plus World Builder region groups. Zero Hour expects one
// TerrainClass name, and INI::parseIndexList threw on "Type". In the lenient (BFME) mode the word
// after "Type" is looked up case-insensitively in terrainTypeNames (Dirt, Grass, Rock, Cliff, Sand,
// Wood map to the Zero Hour classes; Misc, Snow, ... give TERRAIN_NONE), the "NEXT Region" tail is
// read and dropped; both are logged by INILenient::note. Off the BFME mode this is
// INI::parseIndexList, as before. The class only groups textures in the editor.
static void parseTerrainClass( INI *ini, void *instance, void *store, const void *userData )
{
	if (!INILenient::isEnabled())
	{
		INI::parseIndexList(ini, instance, store, userData);
		return;
	}
	const char *token = ini->getNextToken();
	const Bool bfmeForm = stricmp(token, "Type") == 0;
	if (bfmeForm)
		token = ini->getNextToken();
	Int index = -1;
	for (Int i = 0; terrainTypeNames[i]; ++i)
	{
		if (stricmp(terrainTypeNames[i], token) == 0)
		{
			index = i;
			break;
		}
	}
	if (index >= 0)
	{
		*(Int *)store = index;
	}
	else
	{
		*(Int *)store = TERRAIN_NONE;
		AsciiString what;
		what.format("Class.Type=%s", token);
		INILenient::note("value", ini->getFilename().str(), ini->getLineNum(), "Terrain", what.str(), "not a Zero Hour terrain class, TERRAIN_NONE");
	}
	Int regions = 0;
	for (const char *t = ini->getNextTokenOrNull(); t; t = ini->getNextTokenOrNull())
	{
		if (stricmp(t, "Region") == 0)
			++regions;
	}
	if (regions > 0)
	{
		AsciiString detail;
		detail.format("NEXT Region tail dropped (%d regions)", regions);
		INILenient::note("value", ini->getFilename().str(), ini->getLineNum(), "Terrain", "Class.Region", detail.str());
	}
}

// PUBLIC DATA ////////////////////////////////////////////////////////////////////////////////////
TerrainTypeCollection *TheTerrainTypes = nullptr;

// PRIVATE DATA ///////////////////////////////////////////////////////////////////////////////////
const FieldParse TerrainType::m_terrainTypeFieldParseTable[] =
{

	{ "Texture",		INI::parseAsciiString,			nullptr,		offsetof( TerrainType, m_texture ) },
	{ "BlendEdges", INI::parseBool,							nullptr,		offsetof( TerrainType, m_blendEdgeTexture ) },
	{ "Class",			parseTerrainClass,					terrainTypeNames, offsetof( TerrainType, m_class ) },	// BFME: E1-26b
	{ "RestrictConstruction", INI::parseBool,		nullptr,		offsetof( TerrainType, m_restrictConstruction ) },

	{ nullptr,					nullptr,												nullptr,		0 },

};

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
TerrainType::TerrainType()
{

	m_name.clear();
	m_texture.clear();
	m_blendEdgeTexture = FALSE;
	m_class = TERRAIN_NONE;
	m_restrictConstruction = FALSE;
	m_next = nullptr;

}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
TerrainType::~TerrainType()
{

}

///////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
TerrainTypeCollection::TerrainTypeCollection()
{

	m_terrainList = nullptr;

}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
TerrainTypeCollection::~TerrainTypeCollection()
{
	TerrainType *temp;

	// delete all the type instances
	while( m_terrainList )
	{

		// get the next element
		temp = m_terrainList->friend_getNext();

		// delete the head of the type list
		deleteInstance(m_terrainList);

		// set the new head of the type list
		m_terrainList = temp;

	}

}

//-------------------------------------------------------------------------------------------------
/** Find a terrain type given the name */
//-------------------------------------------------------------------------------------------------
TerrainType *TerrainTypeCollection::findTerrain( AsciiString name )
{
	TerrainType *terrain;

	for( terrain = m_terrainList; terrain; terrain = terrain->friend_getNext() )
	{

		if( terrain->getName() == name )
			return terrain;

	}

	// not found
	return nullptr;

}

//-------------------------------------------------------------------------------------------------
/** Allocate a new type, assign the name, and tie to type list */
//-------------------------------------------------------------------------------------------------
TerrainType *TerrainTypeCollection::newTerrain( AsciiString name )
{
	TerrainType *terrain = nullptr;

	// allocate a new type
	terrain = newInstance(TerrainType);

	// copy default values from the default terrain entry
	TerrainType *defaultTerrain = findTerrain( "DefaultTerrain" );
	if( defaultTerrain )
		*terrain = *defaultTerrain;
/*
	{

		terrain->friend_setTexture( defaultTerrain->getTexture() );
		terrain->friend_setClass( defaultTerrain->getClass() );
		terrain->friend_setBlendEdge( defaultTerrain->isBlendEdge() );

	}
*/

	// assign a name
	terrain->friend_setName( name );

	// tie to list
	terrain->friend_setNext( m_terrainList );
	m_terrainList = terrain;

	// return the new terrain
	return terrain;

}
