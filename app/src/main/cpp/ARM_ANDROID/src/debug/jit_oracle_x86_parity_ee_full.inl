// SPDX-FileCopyrightText: 2026 EmuCoreX Team
// SPDX-License-Identifier: GPL-3.0+
// x86-parity oracle, EE clamp mode 3 = fpuFullMode: software model of upstream iFPUd.cpp (the
// double-precision "DOUBLE" recompiler: ToDouble / ToPS2FPU_Full / FPU_ADD_SUB / recMaddsub / recDIVhelper1 ...).
// Suite: x86-parity-ee-full. Included from jit_oracle_x86_parity.inl's translation unit after that file.
// Rounding/FTZ details follow x86 SSE semantics (see the soft model in jit_oracle_x86_parity.inl); every
// mismatch FAILs, the allowlist stays empty (NaN-vs-NaN value exemption only, as in the other suites).

#if defined(EMUCOREX_ENABLE_NATIVE_SELF_TESTS) && !defined(NDEBUG)

namespace x86p
{
constexpr u32 kFlagO = 0x8000u, kFlagU = 0x4000u, kFlagSO = 0x10u, kFlagSU = 0x8u;
//------------------------------------------------------------------
// Exact double-precision values: sign, integer mantissa M, exponent E (value = M * 2^E)
//------------------------------------------------------------------
struct DV { bool s; u128 M; int E; };

static DV toDouble(u32 x, const Env& env)
{
	// ToDouble(): UCOMISS vs +/-Inf routes exponent-255 patterns (incl. NaN) through a "lower exponent, convert, raise exponent" path;
	// the net effect is that exponent 255 is an ordinary exponent (value (1.m) * 2^128). CVTSS2SD honours DAZ for denormals.
	const int ex = (x >> 23) & 0xff;
	const bool s = (x >> 31) != 0;
	const u32 f = x & 0x7fffffu;
	if (ex == 0)
	{
		if (env.daz || f == 0) return {s, 0, 0};
		return {s, f, -149};
	}
	return {s, static_cast<u128>(f | 0x800000u), ex - 150};
}

static DV dRound(bool s, u128 M, int E, bool sticky, int rm)
{
	if (M == 0) return {s, 0, 0};
	const int n = bitlen(M);
	const int e = E + n - 1;
	int q = e - 52;
	u128 Mr = roundToQuantum(M, E, sticky, q, s, rm);
	if (Mr >> 53) { Mr >>= 1; ++q; }
	return {s, Mr, q};
}

static DV dMul(const DV& a, const DV& b)
{
	const bool s = a.s != b.s;
	if (a.M == 0 || b.M == 0) return {s, 0, 0};
	return {s, a.M * b.M, a.E + b.E};
}

static DV dAddSub(const DV& a, DV b, bool sub, int rm)
{
	b.s = b.s != sub;
	if (a.M == 0 && b.M == 0) return {((a.s == b.s) ? a.s : (rm == 1)), 0, 0};
	if (a.M == 0) return b;
	if (b.M == 0) return a;
	const int E = std::min(a.E, b.E);
	const int da = a.E - E, db = b.E - E;
	if (da > 70 || db > 70) return a.E >= b.E ? a : b; // cannot happen after FPU_ADD_SUB masking
	const u128 Ma = a.M << da, Mb = b.M << db;
	if (a.s == b.s) return {a.s, Ma + Mb, E};
	if (Ma > Mb) return {a.s, Ma - Mb, E};
	if (Mb > Ma) return {b.s, Mb - Ma, E};
	return {rm == 1, 0, 0};
}

static DV dDiv(const DV& a, const DV& b, int rm)
{
	const bool s = a.s != b.s;
	if (a.M == 0) return {s, 0, 0};
	const u128 num = a.M << 80;
	const u128 q = num / b.M;
	return dRound(s, q, a.E - 80 - b.E, (num % b.M) != 0, rm);
}

static DV dSqrt(const DV& a, int rm)
{
	if (a.M == 0) return a;
	u128 M = a.M;
	int E = a.E;
	if (E & 1) { M <<= 1; E -= 1; }
	M <<= 100;
	bool exact;
	const u128 r = isqrt128(M, exact);
	return dRound(false, r, (E - 100) / 2, !exact, rm);
}

// ToPS2FPU_Full: returns the single result; updates fcr31 / accflag like the x86 code.
static u32 toPS2Full(const DV& d, bool flags, bool acc, bool addsub, const Env& env, u32& fcr, u32& accflag)
{
	if (flags) fcr &= ~(kFlagO | kFlagU);
	if (flags && acc) accflag &= ~1u;
	const u32 sg = d.s ? 0x80000000u : 0u;
	if (d.M != 0)
	{
		const int e = d.E + bitlen(d.M) - 1; // floor(log2(|d|))
		if (e >= 129) // overflow & clamp
		{
			if (flags) fcr |= (kFlagO | kFlagSO);
			if (flags && acc) accflag |= 1u;
			return sg | 0x7fffffffu;
		}
		if (e >= 128) // complex: halve, convert, double again (integer add of one exponent step)
		{
			const u32 r = pack(d.s, d.M, d.E - 1, false, env);
			return r + 0x00800000u;
		}
		if (e >= -126)
			return pack(d.s, d.M, d.E, false, env);
	}
	// underflow (or exact zero)
	if (flags && d.M != 0)
	{
		fcr |= (kFlagU | kFlagSU);
		if (addsub)
		{
			const u128 M53 = d.M << (53 - bitlen(d.M));
			return sg | static_cast<u32>((M53 >> 29) & 0x7fffffu);
		}
	}
	return sg; // CVTSD2SS under FTZ, then AND with the sign mask
}

constexpr u32 kPosMax3 = 0x7fffffffu; // fpuFullMode "max value" has exponent 255

struct EEmF { u32 fpr[32]; u32 acc; u32 fcr31; u32 accflag; };

static inline void guardMask(u32& d, u32& t)
{
	const int diff = static_cast<int>((d >> 23) & 0xff) - static_cast<int>((t >> 23) & 0xff);
	if (diff >= 25) t &= 0x80000000u;
	else if (diff > 0) t &= 0xffffffffu << (diff - 1);
	else if (diff == 0) {}
	else if (diff <= -25) d &= 0x80000000u;
	else d &= 0xffffffffu << (-diff - 1);
}

static void eeApplyFull(EEmF& e, int op, u32 fs, u32 ft, u32 fd, const ECfg& c)
{
	const Env& env = c.env;
	const u32 s = e.fpr[fs], t = e.fpr[ft];
	u32 res = 0;
	bool toAcc = false;
	u32& fcr = e.fcr31;
	switch (op)
	{
		case E_ADD: case E_ADDA: case E_SUB: case E_SUBA:
		{
			const bool sub = op == E_SUB || op == E_SUBA;
			toAcc = op == E_ADDA || op == E_SUBA;
			u32 a = s, b = t;
			guardMask(a, b);
			res = toPS2Full(dAddSub(toDouble(a, env), toDouble(b, env), sub, env.rm), true, toAcc, true, env, fcr, e.accflag);
			break;
		}
		case E_MUL: case E_MULA:
			toAcc = op == E_MULA;
			res = toPS2Full(dMul(toDouble(s, env), toDouble(t, env)), true, toAcc, false, env, fcr, e.accflag);
			break;
		case E_MADD: case E_MSUB: case E_MADDA: case E_MSUBA:
		{
			const bool sub = op == E_MSUB || op == E_MSUBA;
			toAcc = op == E_MADDA || op == E_MSUBA;
			u32 dummyAcc = e.accflag; // the multiply stage never touches ACCflag (acc=false)
			u32 mres = toPS2Full(dMul(toDouble(s, env), toDouble(t, env)), true, false, false, env, fcr, dummyAcc);
			u32 acc = e.acc;
			guardMask(acc, mres);
			if (fcr & kFlagO)
			{
				if (sub) mres ^= 0x80000000u;
				res = mres | kPosMax3;
				fcr &= ~(kFlagO | kFlagU);
				fcr |= (kFlagO | kFlagSO);
				if (toAcc) e.accflag |= 1u;
			}
			else if (e.accflag & 1u)
			{
				res = acc | kPosMax3;
				fcr &= ~(kFlagO | kFlagU);
				fcr |= (kFlagO | kFlagSO);
				if (toAcc) e.accflag |= 1u;
			}
			else
				res = toPS2Full(dAddSub(toDouble(acc, env), toDouble(mres, env), sub, env.rm), true, toAcc, true, env, fcr, e.accflag);
			break;
		}
		case E_ABS: fcr &= ~(kFlagO | kFlagU); res = s & 0x7fffffffu; break;
		case E_NEG: fcr &= ~(kFlagO | kFlagU); res = s ^ 0x80000000u; break;
		case E_MOV: res = s; break;
		case E_MAX: case E_MIN:
		{
			fcr &= ~(kFlagO | kFlagU);
			const s32 ks = ordKey(s), kt = ordKey(t);
			res = op == E_MAX ? (ks > kt ? s : t) : (ks < kt ? s : t);
			break;
		}
		case E_DIV:
		{
			fcr &= ~(kFlagI | kFlagD);
			if (zeroDaz(t, c.divEnv))
			{
				fcr |= zeroDaz(s, c.divEnv) ? (kFlagI | kFlagSI) : (kFlagD | kFlagSD);
				res = ((s ^ t) & 0x80000000u) | kPosMax3;
			}
			else
			{
				u32 dummy = 0, dummyAcc = 0;
				res = toPS2Full(dDiv(toDouble(s, c.divEnv), toDouble(t, c.divEnv), c.divEnv.rm), false, false, false, c.divEnv, dummy, dummyAcc);
			}
			break;
		}
		case E_SQRT:
		{
			fcr &= ~(kFlagI | kFlagD);
			u32 b = t;
			if (b & 0x80000000u) { fcr |= kFlagI | kFlagSI; b &= 0x7fffffffu; }
			u32 dummy = 0, dummyAcc = 0;
			res = toPS2Full(dSqrt(toDouble(b, c.sqrtEnv), c.sqrtEnv.rm), false, false, false, c.sqrtEnv, dummy, dummyAcc);
			break;
		}
		case E_RSQRT:
		{
			fcr &= ~(kFlagI | kFlagD);
			u32 b = t;
			if (b & 0x80000000u) { fcr |= kFlagI | kFlagSI; b &= 0x7fffffffu; }
			if (zeroDaz(b, c.sqrtEnv))
			{
				fcr |= zeroDaz(s, c.sqrtEnv) ? (kFlagI | kFlagSI) : (kFlagD | kFlagSD);
				res = s | kPosMax3;
			}
			else
			{
				u32 dummy = 0, dummyAcc = 0;
				const DV sq = dSqrt(toDouble(b, c.sqrtEnv), c.sqrtEnv.rm);
				res = toPS2Full(dDiv(toDouble(s, c.sqrtEnv), sq, c.sqrtEnv.rm), false, false, false, c.sqrtEnv, dummy, dummyAcc);
			}
			break;
		}
		case E_CVTS: res = sf_cvtdq2ps(s, env); break;
		case E_CVTW:
			res = sf_cvtt(s, env);
			if (static_cast<s32>(s) >= static_cast<s32>(0x4f000000u)) res = 0x7fffffffu;
			break;
		case E_CF: fcr &= ~kFlagC; return;
		case E_CEQ: case E_CLT: case E_CLE:
		{
			const DV a = toDouble(s, env), b = toDouble(t, env);
			const bool zz = a.M == 0 && b.M == 0;
			const u32 da = dazIn(s, env), db = dazIn(t, env);
			const bool eq = zz || da == db;
			const bool lt = !zz && ordKey(da) < ordKey(db);
			const bool cbit = op == E_CEQ ? eq : (op == E_CLT ? lt : (lt || eq));
			fcr = (fcr & ~kFlagC) | (cbit ? kFlagC : 0);
			return;
		}
		default: return;
	}
	if (toAcc) e.acc = res;
	else e.fpr[fd] = res;
}

static void eeRunJitFull(int op, u32 fs, u32 ft, u32 fd, const u32* fprInit, u32 accInit, u32 accflagInit, u32 fcrInit, EEObs& out)
{
	u32 fpr[32];
	std::memcpy(fpr, fprInit, sizeof(fpr));
	u32 fcr[32] = {};
	fcr[0] = 0x2e30u;
	fcr[31] = fcrInit;
	const std::vector<u32> code = {MipsCop1(kEE[op].fmt, ft, fs, fd, kEE[op].fn)};
	s_fpu_acc_init = &accInit;
	s_fpu_accflag_init = &accflagInit;
	const EESnapshot snap = RunEEProgramWithIrMode(0, code, nullptr, nullptr, fpr, fcr);
	s_fpu_acc_init = nullptr;
	s_fpu_accflag_init = nullptr;
	std::memcpy(out.fpr, snap.fpr, sizeof(out.fpr));
	out.acc = snap.acc;
	out.fcr31 = snap.fcr31;
	out.accflag = snap.accflag;
}

static void eeOneFull(int rnd, int op, u32 fs, u32 ft, u32 fd, const u32* fprInit, u32 accInit, u32 accflagInit, u32 fcrInit)
{
	ECfg c;
	c.mode = 3;
	c.env = makeEnv(rnd);
	Env nearest = c.env;
	nearest.rm = 0;
	c.divEnv = nearest;
	c.sqrtEnv = nearest;
	EEmF m{};
	std::memcpy(m.fpr, fprInit, sizeof(m.fpr));
	m.acc = accInit;
	m.fcr31 = fcrInit;
	m.accflag = accflagInit;
	eeApplyFull(m, op, fs, ft, fd, c);
	EEObs got{};
	eeRunJitFull(op, fs, ft, fd, fprInit, accInit, accflagInit, fcrInit, got);
	++g_cases;
	++g_groupCases;
	if (std::memcmp(m.fpr, got.fpr, sizeof(m.fpr)) == 0 && m.acc == got.acc && m.fcr31 == got.fcr31 && m.accflag == got.accflag) return;
	char kb[160];
	std::snprintf(kb, sizeof(kb), "ee c3 %s %s", rnd == 0 ? "chop" : "near", kEE[op].name);
	const u32 sv = fprInit[fs], tv = fprInit[ft];
	const auto rec = [&](const char* field, u32 expv, u32 gotv) {
		char key[320], ex[400];
		std::snprintf(key, sizeof(key), "%s [%s] Fs=%s Ft=%s ACC=%s", kb, field, clsName(sv), clsName(tv), clsName(accInit));
		std::snprintf(ex, sizeof(ex), "model=%08x jit=%08x in{Fs=%08x Ft=%08x ACC=%08x ACCflag=%u FCR31=%08x fs=%u ft=%u fd=%u}", expv, gotv, sv, tv, accInit, accflagInit, fcrInit, fs, ft, fd);
		std::string cause = std::string("ee c3 ") + (rnd ? "near " : "chop ") + field + " ";
		if (field[0] == 'F' && field[1] == 'C') cause += "FCR31 jit+{" + hex(gotv & ~expv) + "} jit-{" + hex(expv & ~gotv) + "}";
		else if (field[0] == 'A' && field[3] == 'f') cause += "ACCflag " + hex(expv) + "->" + hex(gotv);
		else cause += std::string(clsName(expv)) + "->" + clsName(gotv);
		record(key, ex, cause, kEE[op].name);
	};
	for (u32 i = 0; i < 32; ++i) if (m.fpr[i] != got.fpr[i]) { rec("FPR", m.fpr[i], got.fpr[i]); break; }
	if (m.acc != got.acc) rec("ACC", m.acc, got.acc);
	if (m.fcr31 != got.fcr31) rec("FCR31", m.fcr31, got.fcr31);
	if (m.accflag != got.accflag) rec("ACCflag", m.accflag, got.accflag);
}

static u32 eeFullOperand(Rng& r)
{
	const u32 k = r.below(12);
	if (k < 4) return kVals[r.below(kN)];
	if (k < 7) return (r.next() & 0x807fffffu) | ((110 + r.below(36)) << 23);
	if (k < 9) return (r.next() & 0x807fffffu) | ((r.below(2) ? 250 + r.below(6) : r.below(4)) << 23); // exponent extremes incl. 255
	if (k < 10) return (r.next() & 0x807fffffu) | ((1 + r.below(250)) << 23);
	return r.next();
}

static void X86ParityEEFull()
{
	const u32 step = envKnob("X86P_EESTEP", 4);
	const u32 perOp = envKnob("X86P_EERANDOM", 400);
	for (int rnd = rndLo(); rnd < rndHi(); ++rnd)
	{
		configure(3, rnd);
		EmuConfig.Cpu.Recompiler.fpuOverflow = true;
		EmuConfig.Cpu.Recompiler.fpuExtraOverflow = true;
		EmuConfig.Cpu.Recompiler.fpuFullMode = envKnob("X86P_EEFULLOFF", 0) == 0; // knob: sanity-check that the model discriminates
		EmuConfig.Gamefixes.FpuMulHack = false;
		EmuConfig.Cpu.FPUDivFPCR.bitmask = (1ull << 24);
		g_cpuRegistersPack.Cpu = EmuConfig.Cpu;
		for (int op = 0; op < E_COUNT; ++op)
		{
			for (u32 p = 0; p < kN * kN; p += (rnd ? step * 3 : step))
			{
				Rng r(op * 7907ull + p * 31ull + 3);
				u32 fpr[32];
				for (u32 i = 0; i < 32; ++i) fpr[i] = kVals[r.below(kN)];
				const u32 fs = 1 + r.below(6);
				u32 ft = 1 + r.below(6);
				const u32 fd = (p % 5 == 0) ? fs : (p % 5 == 1 ? ft : 1 + r.below(6));
				if (p % 9 == 0) ft = fs;
				fpr[fs] = kVals[p % kN];
				fpr[ft] = kVals[(p / kN) % kN];
				eeOneFull(rnd, op, fs, ft, fd, fpr, kVals[r.below(kN)], r.below(2), eeFcrInit(r));
			}
			for (u32 n = 0; n < (rnd ? perOp / 3 : perOp); ++n)
			{
				Rng r(op * 104729ull + n * 13ull + 11);
				u32 fpr[32];
				for (u32 i = 0; i < 32; ++i) fpr[i] = eeFullOperand(r);
				const u32 fs = 1 + r.below(6), ft = 1 + r.below(6), fd = 1 + r.below(6);
				eeOneFull(rnd, op, fs, ft, fd, fpr, eeFullOperand(r), r.below(3) == 0, eeFcrInit(r));
			}
			char nm[96];
			std::snprintf(nm, sizeof(nm), "ee c3 %s %s", rnd == 0 ? "chop" : "near", kEE[op].name);
			endGroup(nm);
		}
	}
	EmuConfig.Cpu.Recompiler.fpuFullMode = false;
	g_cpuRegistersPack.Cpu = EmuConfig.Cpu;
}
} // namespace x86p

#endif // EMUCOREX_ENABLE_NATIVE_SELF_TESTS
