/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2025 Electronic Arts Inc.
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
*/

// BFME: E1-51 terrain tile atlas. Upstream packs every texture class into one
// TEXTURE_WIDTH (2048) grid: tilesPerRow = 2048/(64+8) = 28, 28*28 = 784 slots,
// and the search stops at the first free cell of a row (WorldHeightMap.cpp, the
// old break). Fords of Isen is 984 tiles: that one page drops 17 classes (200
// tiles). Lead decision (M1, measured on 2.22): one 4096x2048 texture when
// GL_MAX_TEXTURE_SIZE >= 4096 and the map fits (56 x 28 slots). 4096² is 56x56
// = 3136 slots and does not hold Trilogy (3396 tiles). Maps that do not fit,
// and devices below 4096, use square pages of 2048. A class of side >= 15
// occupies a whole 28x28 page, so Eastfold is 4 pages and Trilogy is 5. A class
// is never split. Mips stop at MIP_LEVELS_3: a deeper trilinear sample crosses
// the 4px border and mixes neighbouring tiles.
#pragma once

#include "Lib/BaseType.h"

#define TERRAIN_ATLAS_EDGE 4096
#define TERRAIN_ATLAS_WIDE_HEIGHT 2048
#define TERRAIN_ATLAS_PAGE_EDGE 2048 // TEXTURE_WIDTH, TileData.h
#define TERRAIN_ATLAS_MAX_PAGES 256
#define TERRAIN_ATLAS_MIP_LEVELS 3 // MIP_LEVELS_3; level 2 still covers only the 4px border
#define TERRAIN_ATLAS_LOG_LIMIT 16 // same cap as BLEND_ANOMALY_LOG_LIMIT (review of PR #10)

struct TerrainAtlasClassIn
{
	Int index;
	Int firstTile;
	Int numTiles;
	Int width;
	const char *name;
};

struct TerrainAtlasClassOut
{
	Int page; ///< -1 if the block was not placed
	Int x, y; ///< pixel origin of the block inside the page
	Int row, column;
};

struct TerrainAtlasStats
{
	Int maxEdge;
	Int pageWidth; ///< 4096 for the wide page, 2048 for a fallback page
	Int pageHeight; ///< 2048 in both paths (the wide page is 4096x2048, not 4096²)
	Int tilesPerRow; ///< columns
	Int rowsPerPage;
	Int pageCount;
	Int classesIn;
	Int classesPlaced;
	Int tilesIn; ///< sum of width*width over classes with width > 0
	Int tilesPlaced; ///< slots of placed classes, minus rejectedTiles
	Int overflowClasses;
	Int overflowTiles; ///< width*width of classes that did not fit
	Int rejectedTiles; ///< placed-class slots refused by the index / pixel check
	Int pageContentHeight[TERRAIN_ATLAS_MAX_PAGES]; ///< raw pixels used on each page
};

// BFME: E1-51 test hook. 0 clears it and the atlas asks the GLES device (or 4096).
void bfmeTerrainAtlasSetMaxEdge(Int edge);
Int bfmeTerrainAtlasMaxEdge();

// BFME: E1-51 test hook. width <= 0 clears it. Otherwise the packer uses this
// rectangle and does not apply the 4096x2048-then-2048 policy. multiPage adds a
// page when a class does not fit; false is the old single-page behaviour.
void bfmeTerrainAtlasForceShape(Int width, Int height, Bool multiPage);

// Packs classes. `out` has classCount entries, parallel to `classes`. `detailed` counts
// detail log lines already emitted (shared with the tile-index check, cap TERRAIN_ATLAS_LOG_LIMIT).
// `largestFirst` is the base-atlas walk (width descending, file order within a width). Edge tiles
// pass false: upstream places those in file order.
void bfmePackTerrainAtlas(const TerrainAtlasClassIn *classes, Int classCount, TerrainAtlasClassOut *out,
	TerrainAtlasStats *stats, Int *detailed, const char *kind, Bool largestFirst);

// Pixel origin of tile (i, j) inside a placed class. False if the class was not placed
// or the 64x64 rect would leave the page.
Bool bfmeAtlasTilePixel(const TerrainAtlasStats *stats, const TerrainAtlasClassOut *placed, Int width,
	Int i, Int j, Int *page, Int *x, Int *y);

Int bfmeAtlasPow2(Int n);

// Detail line (first TERRAIN_ATLAS_LOG_LIMIT across every caller sharing `detailed`) and the summary.
// Release builds (no DEBUG_LOGGING) print the summary to stderr only when something overflowed.
void bfmeLogTerrainAtlasDetail(Int *detailed, const char *line);
void bfmeLogTerrainAtlasSummary(const char *kind, const TerrainAtlasStats *stats);
