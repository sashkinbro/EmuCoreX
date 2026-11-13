// SPDX-FileCopyrightText: 2026 EmuCoreX Team
// SPDX-License-Identifier: GPL-3.0+
// x86-parity oracle: EFU ops with every dest-mask encoding at every clamp mode. Suite: x86-parity-efu.
// x86 facts (microVU_Lower.inl): EFU ops write P only. The 4-bit dest field is NOT a write mask for them:
//  - ESQRT/ESIN/EATAN/EEXP/ERCPR/ERSQRT take the source lane from _Fsf_ = code bits 22:21 (= dest & 3);
//  - ESADD/ERSADD/ELENG/ERLENG/ESUM/EATANxy/EATANxz use the whole vector (dest ignored);
//  - bits 24:23 (_Ftf_) are ignored by every EFU op.
// On VU0 (micro and macro) every EFU op is a NOP.

#if defined(EMUCOREX_ENABLE_NATIVE_SELF_TESTS) && !defined(NDEBUG)

namespace x86p
{
static u32 efuOperand(Rng& r, u32 n)
{
	switch (n % 4)
	{
		case 0: return kVals[r.below(kN)];
		case 1: return (r.next() & 0x807fffffu) | ((118 + r.below(14)) << 23); // |x| in ~[2^-9, 2^4]: EATAN/ESIN series territory
		case 2: return (r.next() & 0x807fffffu) | ((124 + r.below(6)) << 23);  // |x| near 1
		default: return r.below(3) ? kVals[r.below(kN)] : r.next();
	}
}

static void sweepEfuDest(const Ctx& c, int lop, u32 perDest)
{
	const std::string name = kLower[lop].name;
	for (u32 dest = 0; dest < 16; ++dest)
		for (u32 n = 0; n < perDest; ++n)
		{
			Rng r(lop * 6151ull + dest * 977ull + n * 31ull + 7);
			FmacInit init;
			baseInit(init, r, (n & 3) == 0);
			Ins in;
			in.lower = true;
			in.op = lop;
			in.rawDest = static_cast<int>(dest);
			in.fsf = dest & 3;
			in.ftf = (dest >> 2) & 3;
			in.fs = (n % 11 == 5) ? 0 : 1 + r.below(4);
			in.mask = dest;
			for (u32 l = 0; l < 4; ++l)
				init.vf[(in.fs ? in.fs : 1) * 4 + l] = efuOperand(r, n + l);
			runCase(c, name, {in}, init, "");
		}
}

// Dest-mask semantics isolated from the series/rounding arithmetic: operands are small exact values (0, +-1, +-2, +-0.5, 3, 4), so sums of
// squares, sums and reciprocals of powers of two are exact and any remaining difference comes from which lanes the op reads.
static void sweepEfuMaskExact(const Ctx& c, int lop)
{
	static const u32 ex[] = {0x00000000u, 0x3f800000u, 0xbf800000u, 0x40000000u, 0xc0000000u, 0x3f000000u, 0x40400000u, 0x40800000u, 0x80000000u};
	const std::string name = kLower[lop].name;
	for (u32 dest = 0; dest < 16; ++dest)
		for (u32 n = 0; n < 48; ++n)
		{
			Rng r(lop * 7717ull + dest * 313ull + n * 17ull + 5);
			FmacInit init;
			baseInit(init, r, false);
			Ins in;
			in.lower = true;
			in.op = lop;
			in.rawDest = static_cast<int>(dest);
			in.fsf = dest & 3;
			in.ftf = (dest >> 2) & 3;
			in.fs = 1 + r.below(4);
			in.mask = dest;
			for (u32 l = 0; l < 4; ++l) init.vf[in.fs * 4 + l] = ex[r.below(sizeof(ex) / sizeof(ex[0]))];
			runCase(c, name + " (mask-exact)", {in}, init, "dest=" + std::to_string(dest));
		}
}

static void X86ParityEfu()
{
	const u32 perDest = envKnob("X86P_EFUN", 100);
	for (int area = 1; area <= 2 && !envKnob("X86P_EFUEXACTONLY", 0); ++area)
		for (int rnd = rndLo(); rnd < rndHi(); ++rnd)
			for (int clamp = clampLo(); clamp < clampHi(); ++clamp)
			{
				const Ctx c = makeCtx(area, clamp, rnd);
				for (int lop = L_ESADD; lop < L_COUNT; ++lop)
				{
					sweepEfuDest(c, lop, area == 2 ? (rnd ? perDest / 3 : perDest) : 4);
					endGroup(ctxKey(c, std::string(kLower[lop].name) + " dest-sweep"));
				}
			}
	for (int clamp = clampLo(); clamp < clampHi(); ++clamp)
	{
		const Ctx c = makeCtx(2, clamp, 0);
		for (int lop : {L_ESADD, L_ELENG, L_ERSADD, L_ERLENG, L_ESUM, L_ESQRT, L_ERCPR, L_ERSQRT})
		{
			sweepEfuMaskExact(c, lop);
			endGroup(ctxKey(c, std::string(kLower[lop].name) + " dest-mask (exact operands)"));
		}
	}
	// Macro mode (COP2 VCALL-less single op): EFU must be a NOP.
	for (int clamp = clampLo(); clamp < clampHi(); ++clamp)
	{
		const Ctx c = makeCtx(0, clamp, 0);
		for (int lop = L_ESADD; lop < L_COUNT; ++lop)
		{
			sweepEfuDest(c, lop, 4);
			endGroup(ctxKey(c, std::string(kLower[lop].name) + " dest-sweep"));
		}
	}
}
} // namespace x86p

#endif // EMUCOREX_ENABLE_NATIVE_SELF_TESTS
