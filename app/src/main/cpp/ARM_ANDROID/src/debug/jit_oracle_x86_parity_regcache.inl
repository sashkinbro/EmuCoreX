// SPDX-FileCopyrightText: 2026 EmuCoreX Team
// SPDX-License-Identifier: GPL-3.0+
// x86-parity oracle: microVU register-cache effects in micro-mode sequences. Suite: x86-parity-regcache.
// microVU keeps VF registers in host xmm registers between instructions of a block (microRegAlloc, microVU_IR.h).
// DIV with ftf = x allocates Ft read-only (allocReg(_Ft_) = the CACHED full copy) and SSE_DIVSS -> mVUclamp3 clamps it IN PLACE
// when the Extra clamp (clamp modes 2/3) is on, so later readers that hit the cache see the clamped x lane, and a partial write into
// the same VF merges into the cached copy which is then written back whole (carrying the clamped lane into memory).
// The model (runModel with g_modelRegCache, see jit_oracle_x86_parity.inl) implements exactly those rules for short straight-line
// sequences (<= 4 instructions, so host register pressure/eviction cannot occur). Limits: macro mode (COP2) uses the EE register
// allocator and is not modelled; sequences longer than the host xmm count are not generated.

#if defined(EMUCOREX_ENABLE_NATIVE_SELF_TESTS) && !defined(NDEBUG)

namespace x86p
{
static u32 rcMidVal(Rng& r) { return (r.next() & 0x807fffffu) | ((118 + r.below(14)) << 23); }

static void runRegCacheArea(int area, u32 count)
{
	static const char* const names[8] = {"DIV.x;MOVE", "DIV.x;MAX", "DIV.x;partialW;MOVE", "ADD-full;DIV.x;MAX", "ADD-full;DIV.x", "DIV.x;ADD-reader", "DIV.yzw;MOVE", "DIV.x;ABS/ITOF/FTOI"};
	static const u32 xVals[] = {0x7f800000u, 0xff800000u, 0x7fc00001u, 0xffc00001u, 0x7f800001u, 0x7fffffffu, 0xffffffffu, 0x7f7fffffu, 0x3f800000u, 0x3e800000u, 0x80000000u, 0xbf800000u};
	const int iADD = findUpper("ADD"), iMUL = findUpper("MUL"), iMAX = findUpper("MAX"), iMINI = findUpper("MINI"), iADDy = findUpper("ADDy"),
		iMULx = findUpper("MULx"), iABS = findUpper("ABS"), iFTOI0 = findUpper("FTOI0"), iITOF0 = findUpper("ITOF0");
	for (int rnd = rndLo(); rnd < rndHi(); ++rnd)
		for (int clamp = clampLo(); clamp < clampHi(); ++clamp)
			for (u32 variant = 0; variant < 8; ++variant)
			{
				const Ctx c = makeCtx(area, clamp, rnd);
				const u32 n = rnd ? count / 3 : count;
				g_modelRegCache = envKnob("X86P_RCOFF", 0) == 0;
				for (u32 k = 0; k < n; ++k)
				{
					Rng r(0xcac4e000ull + k * 7919ull + variant * 131ull + clamp * 17ull + area);
					FmacInit init;
					baseInit(init, r, true);
					for (u32 i = 4; i < 128; ++i) init.vf[i] = rcMidVal(r);
					for (u32 l = 0; l < 4; ++l) init.acc[l] = rcMidVal(r);
					init.i = rcMidVal(r); init.q = rcMidVal(r);
					const u32 A = 1, B = (k % 7 == 6) ? 1 : 2, C = 3, D = 4;
					init.vf[B * 4 + 0] = xVals[r.below(sizeof(xVals) / sizeof(xVals[0]))];
					const u32 divFtf = (variant == 6) ? 1 + r.below(3) : 0;
					Ins div; div.lower = true; div.op = L_DIV; div.fs = A; div.ft = B; div.fsf = r.below(4); div.ftf = divFtf;
					if (divFtf) init.vf[B * 4 + divFtf] = xVals[r.below(sizeof(xVals) / sizeof(xVals[0]))];
					const auto upper = [&](int op, u32 fs, u32 ft, u32 fd, u32 mask) { Ins i; i.op = op; i.fs = fs; i.ft = ft; i.fd = fd; i.mask = mask; return i; };
					const auto lowerMove = [&](int lop, u32 ft, u32 fs, u32 mask) { Ins i; i.lower = true; i.op = lop; i.ft = ft; i.fs = fs; i.mask = mask; return i; };
					std::vector<Ins> prog;
					const u32 partial = 1 + r.below(14);
					switch (variant)
					{
						case 0: case 6: prog = {div, lowerMove(r.below(2) ? L_MOVE : L_MR32, C, B, 1 + r.below(15))}; break;
						case 1: prog = {div, upper(r.below(2) ? iMAX : iMINI, B, D, C, 0xf)}; break;
						case 2: prog = {div, upper(r.below(2) ? iADDy : iMULx, A, D, B, partial), lowerMove(L_MOVE, C, B, 0xf)}; break;
						case 3: prog = {upper(r.below(2) ? iADD : iMUL, A, D, B, 0xf), div, upper(iMAX, B, D, C, 0xf)}; break;
						case 4: prog = {upper(r.below(2) ? iADD : iMUL, A, D, B, 0xf), div}; break;
						case 5: prog = {div, upper(iADD, B, D, C, 0xf)}; break;
						default:
						{
							const int ops[3] = {iABS, iFTOI0, iITOF0};
							const int pick = ops[r.below(3)];
							prog = {div, upper(pick, B, B, C, 1 + r.below(15))};
							prog.back().ft = C;
							prog.back().fs = B;
							break;
						}
					}
					runCase(c, std::string("REGCACHE ") + names[variant], prog, init, names[variant]);
				}
				endGroup(ctxKey(c, std::string("REGCACHE ") + names[variant]));
			}
}

static void X86ParityRegCache()
{
	const u32 n = envKnob("X86P_RCN", 300);
	runRegCacheArea(1, n);
	runRegCacheArea(2, n);
}
} // namespace x86p

#endif // EMUCOREX_ENABLE_NATIVE_SELF_TESTS
