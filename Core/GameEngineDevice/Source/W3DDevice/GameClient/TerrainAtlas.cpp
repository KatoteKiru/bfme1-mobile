/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2025 Electronic Arts Inc.
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
*/

// BFME: E1-51 terrain tile atlas packer. See TerrainAtlas.h.
#include "W3DDevice/GameClient/TerrainAtlas.h"

#include "Common/Debug.h"
#include "W3DDevice/GameClient/TerrainTex.h"
#include "W3DDevice/GameClient/TileData.h"

#include <cstdio>
#include <cstring>
#include <vector>

// Defined by d3d8gles after the GL context is current. Absent on the desktop DXVK build
// and in the unit tests (the address is null); those use the override or 4096.
extern "C" int d3d8gles_MaxTextureEdge __attribute__((weak));

static Int s_maxEdgeOverride = 0;
static Int s_forceWidth = 0;
static Int s_forceHeight = 0;
static Bool s_forceMulti = false;

void bfmeTerrainAtlasSetMaxEdge(Int edge)
{
	s_maxEdgeOverride = edge;
}

Int bfmeTerrainAtlasMaxEdge()
{
	if (s_maxEdgeOverride > 0) return s_maxEdgeOverride;
	if (&d3d8gles_MaxTextureEdge != nullptr && d3d8gles_MaxTextureEdge > 0) return d3d8gles_MaxTextureEdge;
	return TERRAIN_ATLAS_EDGE;
}

void bfmeTerrainAtlasForceShape(Int width, Int height, Bool multiPage)
{
	s_forceWidth = width;
	s_forceHeight = height;
	s_forceMulti = multiPage;
}

Int bfmeAtlasPow2(Int n)
{
	Int p = 1;
	if (n < 1) n = 1;
	while (p < n) {
		if (p > 0x3FFFFFFF) return p;
		p *= 2;
	}
	return p;
}

// Square fallback page. 2048 when the device can hold it; otherwise the largest
// power of two that still contains one slot (64+8).
static Int chooseSquareEdge(Int maxEdge)
{
	if (maxEdge >= TERRAIN_ATLAS_PAGE_EDGE) return TERRAIN_ATLAS_PAGE_EDGE;
	const Int slot = TILE_PIXEL_EXTENT + TILE_OFFSET;
	Int p = 1;
	while (p * 2 <= maxEdge && p < TERRAIN_ATLAS_PAGE_EDGE) p *= 2;
	return p >= slot ? p : slot;
}

void bfmeLogTerrainAtlasDetail(Int *detailed, const char *line)
{
	if (detailed == nullptr || line == nullptr) return;
	if (*detailed >= TERRAIN_ATLAS_LOG_LIMIT) return;
	*detailed += 1;
	DEBUG_LOG(("TerrainAtlas: overflow #%d %s", *detailed, line));
}

void bfmeLogTerrainAtlasSummary(const char *kind, const TerrainAtlasStats *stats)
{
	if (stats == nullptr) return;
	char buf[512];
	snprintf(buf, sizeof(buf),
		"%s maxEdge %d page %dx%d pages %d cols %d rows %d; placed classes %d/%d, tiles %d/%d; "
		"overflow classes %d, tiles %d; rejected tiles %d",
		kind != nullptr ? kind : "?", stats->maxEdge, stats->pageWidth, stats->pageHeight,
		stats->pageCount, stats->tilesPerRow, stats->rowsPerPage,
		stats->classesPlaced, stats->classesIn, stats->tilesPlaced, stats->tilesIn,
		stats->overflowClasses, stats->overflowTiles, stats->rejectedTiles);
#ifdef DEBUG_LOGGING
	DEBUG_LOG(("TerrainAtlas: %s; per-case lines: first %d", buf, TERRAIN_ATLAS_LOG_LIMIT));
#else
	if (stats->overflowClasses || stats->overflowTiles || stats->rejectedTiles) {
		fprintf(stderr, "WARNING: TerrainAtlas: %s\n", buf);
	}
#endif
}

static Bool cellFree(const std::vector<unsigned char> &grid, Int cols, Int rows, Int page, Int row, Int col)
{
	if (page < 0 || row < 0 || col < 0 || row >= rows || col >= cols) return false;
	const size_t idx = (size_t(page) * size_t(rows) + size_t(row)) * size_t(cols) + size_t(col);
	if (idx >= grid.size()) return false;
	return grid[idx] != 0;
}

static void cellTake(std::vector<unsigned char> &grid, Int cols, Int rows, Int page, Int row, Int col)
{
	if (page < 0 || row < 0 || col < 0 || row >= rows || col >= cols) return;
	const size_t idx = (size_t(page) * size_t(rows) + size_t(row)) * size_t(cols) + size_t(col);
	if (idx < grid.size()) grid[idx] = 0;
}

Bool bfmeAtlasTilePixel(const TerrainAtlasStats *stats, const TerrainAtlasClassOut *placed, Int width,
	Int i, Int j, Int *page, Int *x, Int *y)
{
	if (stats == nullptr || placed == nullptr || placed->page < 0 || width <= 0) return false;
	if (i < 0 || j < 0 || i >= width || j >= width) return false;
	const Int px = placed->x + i * TILE_PIXEL_EXTENT;
	const Int py = placed->y + (width - j - 1) * TILE_PIXEL_EXTENT;
	if (px < 0 || py < 0) return false;
	if (stats->pageWidth < TILE_PIXEL_EXTENT || stats->pageHeight < TILE_PIXEL_EXTENT) return false;
	if (px > stats->pageWidth - TILE_PIXEL_EXTENT || py > stats->pageHeight - TILE_PIXEL_EXTENT) return false;
	if (page) *page = placed->page;
	if (x) *x = px;
	if (y) *y = py;
	return true;
}

// One packing pass into pages of pageWidth x pageHeight. `multi` adds a page when a
// class does not fit (the class stays one rectangle). `emit` writes the per-class
// overflow lines; the 4096x2048 probe passes false so a later 2048 repack is not
// preceded by a false "dropped" log.
static void packInto(const TerrainAtlasClassIn *classes, Int classCount, TerrainAtlasClassOut *out,
	const std::vector<Int> &order, TerrainAtlasStats *stats, Int *detailed, const char *kind,
	Int pageWidth, Int pageHeight, Bool multi, Bool emit)
{
	const Int tilesIn = stats->tilesIn;
	const Int classesIn = stats->classesIn;
	const Int maxEdge = stats->maxEdge;
	memset(stats, 0, sizeof(*stats));
	stats->tilesIn = tilesIn;
	stats->classesIn = classesIn;
	stats->maxEdge = maxEdge;
	stats->pageWidth = pageWidth;
	stats->pageHeight = pageHeight;
	const Int slot = TILE_PIXEL_EXTENT + TILE_OFFSET;
	stats->tilesPerRow = pageWidth / slot;
	stats->rowsPerPage = pageHeight / slot;
	if (stats->tilesPerRow < 1) stats->tilesPerRow = 1;
	if (stats->rowsPerPage < 1) stats->rowsPerPage = 1;
	stats->pageCount = 1;
	const Int cols = stats->tilesPerRow;
	const Int rows = stats->rowsPerPage;
	const char *kindName = kind != nullptr ? kind : "?";

	for (Int c = 0; c < classCount; c++) {
		out[c].page = -1;
		out[c].x = out[c].y = 0;
		out[c].row = out[c].column = 0;
	}

	std::vector<unsigned char> grid(size_t(rows) * size_t(cols), 1);

	for (size_t n = 0; n < order.size(); n++) {
		const TerrainAtlasClassIn &cls = classes[order[n]];
		TerrainAtlasClassOut &dst = out[order[n]];
		const Int width = cls.width;
		if (width <= 0) continue;
		if (width > cols || width > rows) {
			stats->overflowClasses++;
			stats->overflowTiles += width * width;
			if (emit) {
				char line[320];
				snprintf(line, sizeof(line),
					"%s class %d \"%s\" width %d firstTile %d: does not fit a %dx%d page (%d cols x %d rows)",
					kindName, cls.index, cls.name != nullptr ? cls.name : "", width, cls.firstTile,
					pageWidth, pageHeight, cols, rows);
				bfmeLogTerrainAtlasDetail(detailed, line);
			}
			continue;
		}

		Int foundPage = -1, foundRow = 0, foundCol = 0;
		for (Int pass = 0; pass < 2 && foundPage < 0; pass++) {
			const Int pages = stats->pageCount;
			for (Int page = 0; page < pages && foundPage < 0; page++) {
				for (Int row = 0; row < rows - width + 1 && foundPage < 0; row++) {
					for (Int column = 0; column < cols - width + 1; column++) {
						if (!cellFree(grid, cols, rows, page, row, column)) continue;
						Bool open = true;
						for (Int i = 0; i < width && open; i++) {
							for (Int j = 0; j < width && open; j++) {
								if (!cellFree(grid, cols, rows, page, row + j, column + i)) open = false;
							}
						}
						if (open) {
							foundPage = page;
							foundRow = row;
							foundCol = column;
						}
						break; // upstream: the first free cell of the row, then the next row
					}
				}
			}
			if (foundPage >= 0 || pass == 1 || !multi) break;
			if (stats->pageCount >= TERRAIN_ATLAS_MAX_PAGES) break;
			stats->pageCount++;
			grid.resize(size_t(stats->pageCount) * size_t(rows) * size_t(cols), 1);
		}

		if (foundPage < 0) {
			stats->overflowClasses++;
			stats->overflowTiles += width * width;
			if (emit) {
				char line[320];
				snprintf(line, sizeof(line),
					"%s class %d \"%s\" width %d firstTile %d: no %dx%d block on %d page(s) of %dx%d",
					kindName, cls.index, cls.name != nullptr ? cls.name : "", width, cls.firstTile,
					width, width, stats->pageCount, pageWidth, pageHeight);
				bfmeLogTerrainAtlasDetail(detailed, line);
			}
			continue;
		}

		for (Int i = 0; i < width; i++) {
			for (Int j = 0; j < width; j++) {
				cellTake(grid, cols, rows, foundPage, foundRow + j, foundCol + i);
			}
		}
		const Int xOrigin = TILE_OFFSET / 2 + foundCol * slot;
		const Int yOrigin = TILE_OFFSET / 2 + foundRow * slot;
		dst.page = foundPage;
		dst.x = xOrigin;
		dst.y = yOrigin;
		dst.row = foundRow;
		dst.column = foundCol;
		const Int classHeight = yOrigin + width * TILE_PIXEL_EXTENT + TILE_OFFSET / 2;
		if (foundPage >= 0 && foundPage < TERRAIN_ATLAS_MAX_PAGES &&
				classHeight > stats->pageContentHeight[foundPage]) {
			stats->pageContentHeight[foundPage] = classHeight;
		}
		stats->classesPlaced++;
		stats->tilesPlaced += width * width;
	}
}

void bfmePackTerrainAtlas(const TerrainAtlasClassIn *classes, Int classCount, TerrainAtlasClassOut *out,
	TerrainAtlasStats *stats, Int *detailed, const char *kind, Bool largestFirst)
{
	if (stats == nullptr) return;
	memset(stats, 0, sizeof(*stats));
	const Int maxEdge = bfmeTerrainAtlasMaxEdge();
	stats->maxEdge = maxEdge;
	stats->classesIn = classCount > 0 ? classCount : 0;
	stats->pageCount = 1;
	if (classCount <= 0 || classes == nullptr || out == nullptr) {
		if (s_forceWidth > 0 && s_forceHeight > 0) {
			stats->pageWidth = s_forceWidth;
			stats->pageHeight = s_forceHeight;
		} else if (maxEdge >= TERRAIN_ATLAS_EDGE) {
			stats->pageWidth = TERRAIN_ATLAS_EDGE;
			stats->pageHeight = TERRAIN_ATLAS_WIDE_HEIGHT;
		} else {
			const Int edge = chooseSquareEdge(maxEdge);
			stats->pageWidth = edge;
			stats->pageHeight = edge;
		}
		const Int slot = TILE_PIXEL_EXTENT + TILE_OFFSET;
		stats->tilesPerRow = stats->pageWidth / slot;
		stats->rowsPerPage = stats->pageHeight / slot;
		return;
	}

	for (Int c = 0; c < classCount; c++) {
		if (classes[c].width > 0) stats->tilesIn += classes[c].width * classes[c].width;
	}

	std::vector<Int> order;
	order.reserve(size_t(classCount));
	for (Int c = 0; c < classCount; c++) order.push_back(c);
	// Base atlas: width descending, file order within a width (upstream's tileWidth loop).
	// Edge atlas: file order, which is how upstream walks m_edgeTextureClasses.
	if (largestFirst) {
		for (size_t a = 0; a < order.size(); a++) {
			for (size_t b = a + 1; b < order.size(); b++) {
				const Int wa = classes[order[a]].width;
				const Int wb = classes[order[b]].width;
				const Bool swap = wb > wa || (wb == wa && order[b] < order[a]);
				if (swap) {
					const Int tmp = order[a];
					order[a] = order[b];
					order[b] = tmp;
				}
			}
		}
	}

	if (s_forceWidth > 0 && s_forceHeight > 0) {
		packInto(classes, classCount, out, order, stats, detailed, kind,
			s_forceWidth, s_forceHeight, s_forceMulti, true);
		return;
	}

	// M1: one 4096x2048 page when the device can hold a 4096 edge and the map fits.
	// CreateTexture always returns D3D_OK (the upload is lazy), so the choice is the
	// queried GL_MAX_TEXTURE_SIZE, not a glTexImage2D error.
	if (maxEdge >= TERRAIN_ATLAS_EDGE) {
		packInto(classes, classCount, out, order, stats, detailed, kind,
			TERRAIN_ATLAS_EDGE, TERRAIN_ATLAS_WIDE_HEIGHT, false, false);
		if (stats->overflowClasses == 0) return;
		const Int wideClasses = stats->overflowClasses;
		const Int wideTiles = stats->overflowTiles;
		const Int edge = chooseSquareEdge(maxEdge);
		packInto(classes, classCount, out, order, stats, detailed, kind, edge, edge, true, true);
		DEBUG_LOG(("TerrainAtlas: %s does not fit one %dx%d page (overflow classes %d, tiles %d); "
			"repacked into %d page(s) of %dx%d",
			kind != nullptr ? kind : "?", TERRAIN_ATLAS_EDGE, TERRAIN_ATLAS_WIDE_HEIGHT,
			wideClasses, wideTiles, stats->pageCount, stats->pageWidth, stats->pageHeight));
		return;
	}

	const Int edge = chooseSquareEdge(maxEdge);
	packInto(classes, classCount, out, order, stats, detailed, kind, edge, edge, true, true);
}
