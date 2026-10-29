// SPDX-FileCopyrightText: 2026 ARMSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+
// Adapted from ARMSX2 aa52a1fdc7cd03fb02bd404ebaa9940944e7fc60.
#pragma once
#include "common/Pcsx2Defs.h"
namespace vuaot {
static constexpr u32 armPackLaneWeight(int lane, u32 keepMask, bool reverse, int shift,
	bool withUnderflow = false, bool withOverflow = false)
{
	const int bit = reverse ? (3 - lane) : lane;
	if (!(keepMask & (1u << bit)))
		return 0;
	const u32 w = 1u << (bit + shift);
	return w | (w << 4) | (withUnderflow ? (w << 8) : 0) | (withOverflow ? (w << 12) : 0);
}

enum mVUmacWeightVariant
{
	mVUmacW_ZS   = 0,
	mVUmacW_ZSO  = 1,
	mVUmacW_ZSU  = 2,
	mVUmacW_ZSUO = 3,
};

struct mVU_MacWeights
{
	u32 byMask[4][2][16][4];
	u32 bySSShift[4][4][4];
};

static constexpr mVU_MacWeights mVUmakeMacWeights()
{
	mVU_MacWeights t{};
	for (int v = 0; v < 4; v++)
	{
		const bool wu = (v & mVUmacW_ZSU) != 0;
		const bool wo = (v & mVUmacW_ZSO) != 0;
		for (int rev = 0; rev < 2; rev++)
			for (u32 mask = 0; mask < 16; mask++)
				for (int lane = 0; lane < 4; lane++)
					t.byMask[v][rev][mask][lane] = armPackLaneWeight(lane, mask, rev != 0, 0, wu, wo);
		// Single-scalar: the result sits in lane 0 and SHIFT_XYZW rotates it
		// into the field's own MAC bit. shift <= 3 keeps the zero half inside
		// the low nibble, so the SLI trick still holds.
		for (int shift = 0; shift < 4; shift++)
			for (int lane = 0; lane < 4; lane++)
				t.bySSShift[v][shift][lane] = armPackLaneWeight(lane, 1, false, shift, wu, wo);
	}
	return t;
}

// bySSShift for a single-scalar result held in lane 1, 2 or 3 instead of lane
// 0 (allocReg's natural slots): the same weight, taken from that lane.
// Indexed [variant][lane][shift]; the lane-0 row is bySSShift's and unused.
struct mVU_SSLaneWeights
{
	u32 w[4][4][4][4];
};

static constexpr mVU_SSLaneWeights mVUmakeSSLaneWeights()
{
	mVU_SSLaneWeights t{};
	for (int v = 0; v < 4; v++)
		for (int at = 0; at < 4; at++)
			for (int shift = 0; shift < 4; shift++)
				t.w[v][at][shift][at] = armPackLaneWeight(0, 1, false, shift,
					(v & mVUmacW_ZSU) != 0, (v & mVUmacW_ZSO) != 0);
	return t;
}

struct mVU_Globals
{
#define __four(val) { val, val, val, val }
	u32   absclip [4] = __four(0x7fffffff);
	u32   signbit [4] = __four(0x80000000);
	u32   minvals [4] = __four(0xff7fffff);
	u32   maxvals [4] = __four(0x7f7fffff);
	u32   exponent[4] = __four(0x7f800000);
	u32   one     [4] = __four(0x3f800000);
	// Match EmuCoreX VUDef.h exactly; the upstream fork used 0x3f490fda.
	u32   Pi4     [4] = __four(0x3f490fdb);
	// EATAN's odd-power atan series, c1 c3 c5 ... c15. ⚠️ This table is a COPY of the
	// one in x86/microVU_Misc.h and the two must stay name-for-name identical: the
	// arm64 build reads this one, the x86 build reads that one, and mVU_EATAN_arm and
	// mVU_EATAN_ index them by the SAME names. Upstream renamed T2..T5 there in
	// aae9438f98 so the names finally ascend with the power; this copy is renamed to
	// match. Values never moved -- only the labels -- so codegen is unchanged.
	u32   T1      [4] = __four(0x3f7ffff5);
	u32   T2      [4] = __four(0xbeaaa61c);
	u32   T3      [4] = __four(0x3e4c40a6);
	u32   T4      [4] = __four(0xbe0e6c63);
	u32   T5      [4] = __four(0x3dc577df);
	u32   T6      [4] = __four(0xbd6501c4);
	u32   T7      [4] = __four(0x3cb31652);
	u32   T8      [4] = __four(0xbb84d7e7);
	u32   S2      [4] = __four(0xbe2aaaa4);
	u32   S3      [4] = __four(0x3c08873e);
	u32   S4      [4] = __four(0xb94fb21f);
	u32   S5      [4] = __four(0x362e9c14);
	u32   E1      [4] = __four(0x3e7fffa8);
	u32   E2      [4] = __four(0x3d0007f4);
	u32   E3      [4] = __four(0x3b29d3ff);
	u32   E4      [4] = __four(0x3933e553);
	u32   E5      [4] = __four(0x36b63510);
	u32   E6      [4] = __four(0x353961ac);
	u32   I32MAXF [4] = __four(0x4effffff);
	float FTOI_4  [4] = __four(16.0);
	float FTOI_12 [4] = __four(4096.0);
	float FTOI_15 [4] = __four(32768.0);
	float ITOF_4  [4] = __four(0.0625f);
	float ITOF_12 [4] = __four(0.000244140625);
	float ITOF_15 [4] = __four(0.000030517578125);
	// Sign-preserving operand clamp bounds for mVUclamp2 (SMIN/UMIN on the
	// integer representation), mirroring x86's sse4_maxvals/sse4_minvals
	// (x86/microVU_Clamp.inl): row 0 = single-lane (SS) — real bound in lane
	// 0, sentinel no-op bounds (INT_MAX for SMIN, UINT_MAX for UMIN) in
	// lanes 1-3 so anything parked there survives; row 1 = all-lane (PS).
	// A row's max comes first and its min second, which is the order and the
	// adjacency mVUclamp2's Ldp wants. Appended at the end of the struct so
	// existing [x25, #imm] offsets in emitted code keep their values.
	u32 signBounds[2][2][4] = {{{0x7f7fffff, 0x7fffffff, 0x7fffffff, 0x7fffffff},
	                            {0xff7fffff, 0xffffffff, 0xffffffff, 0xffffffff}},
	                           {{0x7f7fffff, 0x7f7fffff, 0x7f7fffff, 0x7f7fffff},
	                            {0xff7fffff, 0xff7fffff, 0xff7fffff, 0xff7fffff}}};
	// Also appended at the end — see mVU_MacWeights.
	mVU_MacWeights macWeights = mVUmakeMacWeights();
	// CLIP's six result bits, one weight per comparison lane in the order
	// mVU_CLIP's UZP1 leaves them: the four +component lanes then the four
	// -component ones, w weightless in both. Appended for the reason above.
	u16 clipWeights[8] = {1, 4, 16, 0, 2, 8, 32, 0};
	mVU_SSLaneWeights ssLaneWeights = mVUmakeSSLaneWeights();
#undef __four
};

alignas(32) inline constexpr struct mVU_Globals mVUglob;

}
