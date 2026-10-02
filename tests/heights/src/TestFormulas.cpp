// BFME1 Mobile — tests (E1-25: 16-bit terrain heights)
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Equivalence of the height arithmetic that E1-25 changed or relies on in translation units that this
// standalone project cannot link (BaseHeightMap, TerrainLogic, W3DTerrainVisual, HeightMap: render
// objects, GameLogic). Each check evaluates the UPSTREAM expression (u8 samples, MAP_XY_FACTOR/16)
// and the E1-25 expression (u16 = u8 << 4, MAP_XY_FACTOR/256) copied from the source, exhaustively
// over the ZH input range where possible, and requires the same result bit for bit.
// The expressions are copies: when one of those functions changes, update the copy here.
#include "PreRTS.h"

#include "Common/MapObject.h"
#include "WWMath/vector3.h"

#include "../harness/TestHarness.h"
#include "SyntheticMaps.h"

#include <cmath>
#include <cstring>

namespace {

const Real ZH_SCALE = MAP_XY_FACTOR / 16.0f;   // upstream MAP_HEIGHT_SCALE
const Int ZH_K_MAX_HEIGHT = 255;                // upstream K_MAX_HEIGHT

uint32_t bits(float f)
{
	uint32_t b;
	std::memcpy(&b, &f, 4);
	return b;
}

float unitFloat(bfmeheights::XorShift& rng)   // [0, 1) with all 24 mantissa bits used
{
	return float(rng.next() >> 8) * (1.0f / 16777216.0f);
}

// BaseHeightMapRenderObjClass::getHeightMapHeight, triangle interpolation and smoothed normal
// (BaseHeightMap.cpp, "float p0 = data[idx]" .. "Normalized_Cross_Product").
template <typename Sample>
Real heightMapHeight(const Sample d[12], float fx, float fy, Real scale, Coord3D* normal)
{
	Real height;
	float p0 = d[0];
	float p2 = d[2];
	if (fy > fx) {
		float p3 = d[3];
		height = (p3 + (1.0f-fy)*(p0-p3) + fx*(p2-p3)) * scale;
	} else {
		float p1 = d[1];
		height = (p1 + fy*(p2-p1) + (1.0f-fx)*(p0-p1)) * scale;
	}
	Sample d0 = d[0], d1 = d[1], d2 = d[2], d3 = d[3], d4 = d[4], d5 = d[5], d6 = d[6], d7 = d[7], d8 = d[8],
	       d9 = d[9], d10 = d[10], d11 = d[11];
	(void)d10;
	Real deltaZ_X0 = d1-d11;
	Real deltaZ_X1 = d6-d0;
	Real deltaZ_X2 = d7-d3;
	Real deltaZ_X3 = d6-d0;
	Real deltaZ_Y0 = d3-d4;
	Real deltaZ_Y1 = d2-d5;
	Real deltaZ_Y2 = d8-d1;
	Real deltaZ_Y3 = d9-d0;
	Real deltaZ_X_Left = deltaZ_X0*(1.0f-fx) + fx*deltaZ_X3;
	Real deltaZ_X_Right = deltaZ_X1*(1.0f-fx) + fx*deltaZ_X2;
	Real deltaZ_X = deltaZ_X_Left*(1.0-fy) + fy*deltaZ_X_Right;
	Real deltaZ_Y_Left = deltaZ_Y0*(1.0f-fx) + fx*deltaZ_Y3;
	Real deltaZ_Y_Right = deltaZ_Y1*(1.0f-fx) + fx*deltaZ_Y2;
	Real deltaZ_Y = deltaZ_Y_Left*(1.0-fy) + fy*deltaZ_Y_Right;
	Vector3 l2r, n2f, normalAtTexel;
	l2r.Set(2*MAP_XY_FACTOR/scale, 0, deltaZ_X);
	n2f.Set(0, 2*MAP_XY_FACTOR/scale, deltaZ_Y);
	Vector3::Normalized_Cross_Product(l2r, n2f, &normalAtTexel);
	normal->x = normalAtTexel.X;
	normal->y = normalAtTexel.Y;
	normal->z = normalAtTexel.Z;
	return height;
}

// TerrainLogic::flattenTerrain: rawDataHeight (rounded) and centerHeight (floor), upstream and E1-25.
Int flattenRawOld(Real avgHeight) { return REAL_TO_INT_FLOOR(0.5f + avgHeight/ZH_SCALE); }
Int flattenRawNew(Real avgHeight) { return REAL_TO_INT_FLOOR(0.5f + avgHeight/MAP_HEIGHT_SCALE_ZH)*MAP_HEIGHT_ZH_STEP; }
Int flattenCenterOld(Real avgHeight) { return REAL_TO_INT_FLOOR(avgHeight/ZH_SCALE); }
Int flattenCenterNew(Real avgHeight) { return REAL_TO_INT_FLOOR(avgHeight/MAP_HEIGHT_SCALE_ZH)*MAP_HEIGHT_ZH_STEP; }

// TerrainLogic::createCraterInTerrain: targetHeight, upstream (u8 sample) and E1-25 (u16 = u8 << 4).
Int craterTargetOld(Int raw8, Real displacementAmount) { return MAX( 1, raw8 - displacementAmount ); }
Int craterTargetNew(Int raw8, Real displacementAmount)
{
	Int newTarget = MAX( 1, (raw8 << MAP_HEIGHT_ZH_SHIFT) / (Real)MAP_HEIGHT_ZH_STEP - displacementAmount );
	return newTarget * MAP_HEIGHT_ZH_STEP;
}

} // namespace

TEST(formula_get_height_map_height_and_normal)
{
	bfmeheights::XorShift rng(0x5EED1u);
	for (int iter = 0; iter < 400000; ++iter) {
		UnsignedByte zh[12];
		UnsignedShort u16[12];
		const int mode = iter % 4;   // full range, cliffs, flat +-1, extremes
		for (int k = 0; k < 12; ++k) {
			uint32_t r = rng.next();
			zh[k] = mode == 0 ? UnsignedByte(r) : mode == 1 ? UnsignedByte((r & 1) ? 255 : r % 8)
			      : mode == 2 ? UnsignedByte(128 + (r % 3) - 1) : UnsignedByte((r & 1) ? 255 : 0);
			u16[k] = UnsignedShort(zh[k] << MAP_HEIGHT_ZH_SHIFT);
		}
		float fx = unitFloat(rng), fy = unitFloat(rng);
		if (iter % 97 == 0) fy = fx;   // diagonal edge case
		Coord3D nOld, nNew;
		const Real hOld = heightMapHeight(zh, fx, fy, ZH_SCALE, &nOld);
		const Real hNew = heightMapHeight(u16, fx, fy, MAP_HEIGHT_SCALE, &nNew);
		REQUIRE_EQ(bits(hOld), bits(hNew));
		REQUIRE_EQ(bits(nOld.x), bits(nNew.x));
		REQUIRE_EQ(bits(nOld.y), bits(nNew.y));
		REQUIRE_EQ(bits(nOld.z), bits(nNew.z));
	}
}

// getMaxCellHeight, isClearLineOfSight, setCellCliffFlagFromHeights, W3DTerrainBackground,
// W3DProjectedShadow, ...: data[i]*MAP_HEIGHT_SCALE and differences of such products.
TEST(formula_raw_times_scale)
{
	for (int a = 0; a < 256; ++a) {
		REQUIRE_EQ(bits(a * ZH_SCALE), bits((a << MAP_HEIGHT_ZH_SHIFT) * MAP_HEIGHT_SCALE));
		for (int b = 0; b < 256; ++b) {
			REQUIRE_EQ(bits(a * ZH_SCALE - b * ZH_SCALE),
			           bits((a << MAP_HEIGHT_ZH_SHIFT) * MAP_HEIGHT_SCALE - (b << MAP_HEIGHT_ZH_SHIFT) * MAP_HEIGHT_SCALE));
		}
	}
}

// WorldHeightMap::getUVForTileIndex, cliff texture split (belowLimit/aboveLimit). This one is real code
// in the linked WorldHeightMap.cpp but needs texture classes; the arithmetic is checked here.
TEST(formula_uv_cliff_limits)
{
	for (Int minH = 0; minH < 256; ++minH) {
		for (Int deltaH = 0; minH + deltaH < 256; ++deltaH) {
			const Int oldBelow = minH+(2*deltaH+1)/3;
			const Int oldAbove = minH+(deltaH+1)/3;
			const Int minH16 = minH*MAP_HEIGHT_ZH_STEP, deltaH16 = deltaH*MAP_HEIGHT_ZH_STEP;
			const Int newBelow = minH16+((2*deltaH16+MAP_HEIGHT_ZH_STEP)/(3*MAP_HEIGHT_ZH_STEP))*MAP_HEIGHT_ZH_STEP;
			const Int newAbove = minH16+((deltaH16+MAP_HEIGHT_ZH_STEP)/(3*MAP_HEIGHT_ZH_STEP))*MAP_HEIGHT_ZH_STEP;
			REQUIRE_EQ(newBelow, oldBelow*MAP_HEIGHT_ZH_STEP);
			REQUIRE_EQ(newAbove, oldAbove*MAP_HEIGHT_ZH_STEP);
			// the h < limit comparisons on u16 = u8 << 4 then give the same counts
			for (Int h = minH; h <= minH + deltaH; ++h) {
				REQUIRE_EQ(h*MAP_HEIGHT_ZH_STEP < newBelow, h < oldBelow);
				REQUIRE_EQ(h*MAP_HEIGHT_ZH_STEP < newAbove, h < oldAbove);
			}
		}
	}
}

// TerrainLogic::flattenTerrain (rawDataHeight, centerHeight) and createCraterInTerrain (targetHeight).
TEST(formula_flatten_and_crater)
{
	bfmeheights::XorShift rng(0xF1A7u);
	for (int iter = 0; iter < 2000000; ++iter) {
		Real avgHeight = unitFloat(rng) * 260.0f - 50.0f;
		if (iter < 256 * 4) avgHeight = (iter / 4) * ZH_SCALE + (iter % 4 - 1.5f) * 1e-4f;   // around every step
		REQUIRE_EQ(flattenRawNew(avgHeight), flattenRawOld(avgHeight)*MAP_HEIGHT_ZH_STEP);
		REQUIRE_EQ(flattenCenterNew(avgHeight), flattenCenterOld(avgHeight)*MAP_HEIGHT_ZH_STEP);
	}
	for (Int raw8 = 0; raw8 < 256; ++raw8) {
		for (int k = 0; k < 2000; ++k) {
			const Real displacementAmount = unitFloat(rng) * 300.0f;
			REQUIRE_EQ(craterTargetNew(raw8, displacementAmount), craterTargetOld(raw8, displacementAmount)*MAP_HEIGHT_ZH_STEP);
		}
	}
}

// Explicit boundaries (QA, #4): the rounding points of floor(0.5f + avg/0.625) are avg = (k+0.5)*0.625,
// those of floor(avg/0.625) are avg = k*0.625; both are exact floats. Each one and its float neighbours
// (nextafterf, 4 steps each way) must give the same result upstream and after E1-25.
TEST(formula_flatten_rounding_boundaries)
{
	int checked = 0;
	for (Int k = -100; k <= 300; ++k) {
		const Real half = (k + 0.5f) * ZH_SCALE;      // (2k+1)*5/16, exact
		const Real whole = k * ZH_SCALE;              // 5k/8, exact
		REQUIRE_EQ(double(half), (k + 0.5) * 0.625);
		REQUIRE_EQ(double(whole), k * 0.625);
		// at the exact x.5 point avg/0.625 is exactly k+0.5, so the upstream rounding gives k+1 (heights
		// are >= 0). Below 0 REAL_TO_INT_FLOOR (fast_float_floor: f - 0.99999994, then trunc) maps an
		// integral -n to -n-1 once n >= 2: an upstream quirk, the same before and after, checked below.
		if (k >= 0) {
			REQUIRE_EQ(flattenRawOld(half), k + 1);
			REQUIRE_EQ(flattenCenterOld(whole), k);
		}
		for (const Real base : {half, whole}) {
			Real lo = base, hi = base;
			for (int step = 0; step <= 4; ++step) {
				for (const Real avg : {lo, hi}) {
					REQUIRE_EQ(flattenRawNew(avg), flattenRawOld(avg)*MAP_HEIGHT_ZH_STEP);
					REQUIRE_EQ(flattenCenterNew(avg), flattenCenterOld(avg)*MAP_HEIGHT_ZH_STEP);
					++checked;
				}
				lo = std::nextafterf(lo, -INFINITY);
				hi = std::nextafterf(hi, INFINITY);
			}
		}
	}
	REQUIRE_EQ(checked, 401 * 2 * 5 * 2);
}

// Explicit crater cases (QA, #4): integer displacementAmount 0..300 and its float neighbours
// (nextafterf, 2 steps each way) for every ZH height, so MAX(1, h - d) hits the integer boundaries.
TEST(formula_crater_integer_displacements)
{
	int checked = 0;
	for (Int raw8 = 0; raw8 < 256; ++raw8) {
		for (Int d = 0; d <= 300; ++d) {
			Real lo = Real(d), hi = Real(d);
			for (int step = 0; step <= 2; ++step) {
				for (const Real displacementAmount : {lo, hi}) {
					REQUIRE_EQ(craterTargetNew(raw8, displacementAmount), craterTargetOld(raw8, displacementAmount)*MAP_HEIGHT_ZH_STEP);
					++checked;
				}
				lo = std::nextafterf(lo, -INFINITY);
				hi = std::nextafterf(hi, INFINITY);
			}
		}
		// exact integers: h - d is exact, so the target is MAX(1, h - d)
		for (Int d = 0; d <= 300; ++d) {
			REQUIRE_EQ(craterTargetOld(raw8, Real(d)), MAX(1, raw8 - d));
		}
	}
	REQUIRE_EQ(checked, 256 * 301 * 3 * 2);
}

// W3DTerrainVisual::updateSeismicSimulations (dead code upstream: DO_SEISMIC_SIMULATIONS is never
// defined) and W3DTerrainVisual::xfer v2/v3 (u8 layout: >> 4 on save, << 4 on load).
TEST(formula_seismic_and_save_layout)
{
	bfmeheights::XorShift rng(0x5E15u);
	for (Int h8 = 0; h8 < 256; ++h8) {
		for (int k = 0; k < 2000; ++k) {
			const Real newSpeed = unitFloat(rng) * 40.0f - 20.0f;
			Int oldUse = h8 + newSpeed;   // upstream: Int heightToUse = getHeight(x, y) + newSpeed;
			const bool clamped = oldUse > 255;
			if (clamped) oldUse = 255;
			const Int h16 = h8 << MAP_HEIGHT_ZH_SHIFT;
			Int newUse = (Int)(h16 / (Real)MAP_HEIGHT_ZH_STEP + newSpeed) * MAP_HEIGHT_ZH_STEP;
			if (newUse > 0xFFFF) newUse = 0xFFFF;
			if (!clamped) REQUIRE_EQ(newUse, oldUse*MAP_HEIGHT_ZH_STEP);  // above 255 the u16 clamp differs (open question)
		}
		const UnsignedShort stored = UnsignedShort(h8 << MAP_HEIGHT_ZH_SHIFT);
		const UnsignedByte saved = UnsignedByte(stored >> MAP_HEIGHT_ZH_SHIFT);
		REQUIRE_EQ(int(saved), h8);
		REQUIRE_EQ(int(UnsignedShort(saved << MAP_HEIGHT_ZH_SHIFT)), int(stored));
	}
}

// BaseHeightMapRenderObjClass::Cast_Ray boxes and constructor m_maxHeight.
TEST(formula_cast_ray_boxes)
{
	REQUIRE_EQ(bits(ZH_SCALE*ZH_K_MAX_HEIGHT+MAP_XY_FACTOR), bits(MAX(255*MAP_HEIGHT_SCALE_ZH, 0.0f)+MAP_XY_FACTOR));
	REQUIRE_EQ(float((std::pow(256.0, 1) - 1.0) * ZH_SCALE), float((std::pow(256.0, sizeof(UnsignedByte)) - 1.0) * MAP_HEIGHT_SCALE_ZH));
	for (Int ht = 0; ht < 256; ++ht) {
		// on ZH maps m_maxHeight <= 255 ZH units, so the upper bound stays the upstream one
		REQUIRE_EQ(bits(ZH_SCALE*ZH_K_MAX_HEIGHT+MAP_XY_FACTOR), bits(MAX(255*MAP_HEIGHT_SCALE_ZH, ht*ZH_SCALE)+MAP_XY_FACTOR));
		const Int ht16 = ht << MAP_HEIGHT_ZH_SHIFT;
		REQUIRE_EQ(bits(ZH_SCALE*(ht-1)), bits(MAP_HEIGHT_SCALE*(ht16-MAP_HEIGHT_ZH_STEP)));
		REQUIRE_EQ(bits(ZH_SCALE*(ht+1)), bits(MAP_HEIGHT_SCALE*(ht16+MAP_HEIGHT_ZH_STEP)));
	}
	// HeightMapRenderObjClass::updateCenter: intersectionZ = minHt (raw u8 as world z upstream)
	for (Int ht = 0; ht < 256; ++ht) {
		REQUIRE_EQ(bits((float)ht), bits((float)(ht << MAP_HEIGHT_ZH_SHIFT)/MAP_HEIGHT_ZH_STEP));
	}
}
