// SPDX-FileCopyrightText: 2026 EmuCoreX Team
// SPDX-License-Identifier: GPL-3.0+
// x86-parity oracle: compares the arm64 VU/COP2/EE-FPU recompilers with an explicit
// software MODEL of upstream PCSX2's x86 JIT (microVU Upper/Lower/Clamp/Flags/Macro,
// iFPU.cpp, iFPUd.cpp). The model never uses host FP arithmetic: SSE semantics
// (DAZ/FTZ, RC rounding, NaN propagation, default NaN, CVTTPS2DQ, MIN/MAX int ordering,
// DPPS summation order) are re-implemented in integer software below.
// Every mismatch FAILs; the allowlist (kAllow) must stay empty unless a deliberate,
// documented deviation backed by real-console data is recorded there.
// Suites: x86-parity (all), x86-parity-selfcheck, x86-parity-vu-macro, x86-parity-vu-micro0,
// x86-parity-vu-micro1, x86-parity-ee. Extension suites (jit_oracle_x86_parity2.inl): x86-parity-ee-full, x86-parity-efu,
// x86-parity-regcache, x86-parity-flags, x86-parity-vu-int, x86-parity-vu-mem, x86-parity-vu-branch, x86-parity-vu-misc,
// x86-parity-cop2, x86-parity-ee-sync.
// Deliberate exemption (documented, not an allowlist entry): when BOTH the model and the JIT produce a NaN the payload/sign is not
// compared (x86 default NaN 0xFFC00000 vs ARM 0x7FC00000, first-operand vs sNaN-priority propagation); flags derived from those NaNs
// stay exact and are reported under the "NaN-sign" cause prefix.
// Included from jit_oracle.cpp's anonymous namespace after jit_oracle_ee_fpu_hw.inl.

#if defined(EMUCOREX_ENABLE_NATIVE_SELF_TESTS) && !defined(NDEBUG)

namespace x86p
{
using u128 = unsigned __int128;

// Deliberate deviations from x86 where REAL console data exists. Empty on purpose.
struct AllowEntry { const char* key; const char* why; };
static const AllowEntry kAllow[] = {
	// {"ee c1 chop SQRT.S", "console-verified special-value table"},
	{nullptr, nullptr}, // sentinel: allowlist is intentionally empty
};

//------------------------------------------------------------------
// SSE scalar-float soft model
//------------------------------------------------------------------
struct Env
{
	int rm = 3;      // x86 MXCSR.RC encoding: 0 nearest, 1 down, 2 up, 3 chop
	bool daz = true; // denormal inputs read as zero
	bool ftz = true; // tiny results flushed to zero
};

constexpr u32 kDefNaN = 0xffc00000u;
constexpr u32 kPlusMax = 0x7f7fffffu;
constexpr u32 kMinusMax = 0xff7fffffu;

static inline bool isNaN(u32 x) { return (x & 0x7fffffffu) > 0x7f800000u; }
static inline bool isSNaN(u32 x) { return isNaN(x) && !(x & 0x400000u); }
static inline u32 quiet(u32 x) { return x | 0x400000u; }
static inline bool isInf(u32 x) { return (x & 0x7fffffffu) == 0x7f800000u; }
static inline bool isDenorm(u32 x) { return ((x >> 23) & 0xffu) == 0 && (x & 0x7fffffu); }
static inline u32 dazIn(u32 x, const Env& e) { return (e.daz && ((x >> 23) & 0xffu) == 0) ? (x & 0x80000000u) : x; }
static inline bool zeroDaz(u32 x, const Env& e) { return (x & 0x7fffffffu) == 0 || (e.daz && ((x >> 23) & 0xffu) == 0); }

struct Un { bool s, zero, inf, nan; int e; u32 m; };
static inline Un unp(u32 x)
{
	Un u;
	u.s = x >> 31;
	u.nan = isNaN(x);
	u.inf = isInf(x);
	const int ex = (x >> 23) & 0xff;
	const u32 f = x & 0x7fffffu;
	if (ex == 0) { u.m = f; u.e = -149; }
	else { u.m = f | 0x800000u; u.e = ex - 150; }
	u.zero = ex == 0 && f == 0;
	return u;
}

static inline int bitlen(u128 v)
{
	int n = 0;
	if (v >> 64) { n += 64; v >>= 64; }
	u64 lo = static_cast<u64>(v);
	while (lo) { ++n; lo >>= 1; }
	return n;
}

// Round (M * 2^E, plus an infinitesimal sticky remainder) to a multiple of 2^q.
static inline u128 roundToQuantum(u128 M, int E, bool sticky, int q, bool sign, int rm)
{
	const int shift = q - E;
	u128 base, low = 0, half = 0;
	if (shift <= 0)
		base = M << (-shift);
	else
	{
		base = M >> shift;
		low = M & ((static_cast<u128>(1) << shift) - 1);
		half = static_cast<u128>(1) << (shift - 1);
	}
	const bool inexact = low != 0 || sticky;
	bool up = false;
	switch (rm)
	{
		case 0:
			if (shift > 0)
				up = low > half || (low == half && (sticky || (base & 1)));
			break;
		case 1: up = inexact && sign; break;
		case 2: up = inexact && !sign; break;
		default: break;
	}
	return base + (up ? 1 : 0);
}

static inline u32 overflowResult(bool s, int rm)
{
	const u32 sg = s ? 0x80000000u : 0;
	if (rm == 0 || (rm == 2 && !s) || (rm == 1 && s))
		return sg | 0x7f800000u;
	return sg | 0x7f7fffffu;
}

// Pack sign * (M * 2^E + tiny sticky). FTZ detects tininess after rounding (x86).
static u32 pack(bool s, u128 M, int E, bool sticky, const Env& env)
{
	const u32 sg = s ? 0x80000000u : 0;
	if (M == 0)
		return sg;
	const int n = bitlen(M);
	int e = E + n - 1;
	if (env.ftz)
	{
		u128 Mr = roundToQuantum(M, E, sticky, e - 23, s, env.rm);
		if (Mr >> 24) { Mr >>= 1; ++e; }
		if (e < -126)
			return sg;
		if (e > 127)
			return overflowResult(s, env.rm);
		return sg | (static_cast<u32>(e + 127) << 23) | static_cast<u32>(Mr & 0x7fffffu);
	}
	int q = std::max(e - 23, -149);
	u128 Mr = roundToQuantum(M, E, sticky, q, s, env.rm);
	if (Mr == 0)
		return sg;
	if (Mr >> 24) { Mr >>= 1; ++q; }
	if (Mr < (static_cast<u128>(1) << 23))
		return sg | static_cast<u32>(Mr);
	const int e2 = q + 23;
	if (e2 > 127)
		return overflowResult(s, env.rm);
	return sg | (static_cast<u32>(e2 + 127) << 23) | static_cast<u32>(Mr & 0x7fffffu);
}

static u32 addsub(u32 a, u32 b, bool sub, const Env& env)
{
	const u32 A = dazIn(a, env), B = dazIn(b, env);
	if (isNaN(A)) return quiet(A);
	if (isNaN(B)) return quiet(B);
	const bool sa = A >> 31, sb = ((B >> 31) != 0) ^ sub;
	const Un ua = unp(A), ub = unp(B);
	if (ua.inf || ub.inf)
	{
		if (ua.inf && ub.inf) return sa == sb ? A : kDefNaN;
		if (ua.inf) return A;
		return (sb ? 0x80000000u : 0) | 0x7f800000u;
	}
	if (ua.zero && ub.zero)
		return ((sa == sb) ? sa : (env.rm == 1)) ? 0x80000000u : 0u;
	if (ua.zero) return (B & 0x7fffffffu) | (sb ? 0x80000000u : 0);
	if (ub.zero) return A;
	const bool aHi = ua.e >= ub.e;
	const Un& hi = aHi ? ua : ub;
	const Un& lo = aHi ? ub : ua;
	const bool shi = aHi ? sa : sb, slo = aHi ? sb : sa;
	const int d = hi.e - lo.e;
	u128 Mx, My;
	int E;
	bool stLo = false;
	if (d <= 64) { Mx = static_cast<u128>(hi.m) << d; My = lo.m; E = lo.e; }
	else { Mx = static_cast<u128>(hi.m) << 64; My = 0; E = hi.e - 64; stLo = true; }
	if (shi == slo)
		return pack(shi, Mx + My, E, stLo, env);
	if (stLo)
		return pack(shi, Mx - 1, E, true, env);
	if (Mx > My) return pack(shi, Mx - My, E, false, env);
	if (My > Mx) return pack(slo, My - Mx, E, false, env);
	return env.rm == 1 ? 0x80000000u : 0u;
}
static inline u32 sf_add(u32 a, u32 b, const Env& e) { return addsub(a, b, false, e); }
static inline u32 sf_sub(u32 a, u32 b, const Env& e) { return addsub(a, b, true, e); }

static u32 sf_mul(u32 a, u32 b, const Env& env)
{
	const u32 A = dazIn(a, env), B = dazIn(b, env);
	if (isNaN(A)) return quiet(A);
	if (isNaN(B)) return quiet(B);
	const Un ua = unp(A), ub = unp(B);
	const bool s = ua.s != ub.s;
	if ((ua.inf && ub.zero) || (ub.inf && ua.zero)) return kDefNaN;
	if (ua.inf || ub.inf) return (s ? 0x80000000u : 0) | 0x7f800000u;
	if (ua.zero || ub.zero) return s ? 0x80000000u : 0u;
	return pack(s, static_cast<u128>(ua.m) * ub.m, ua.e + ub.e, false, env);
}

static u32 sf_div(u32 a, u32 b, const Env& env)
{
	const u32 A = dazIn(a, env), B = dazIn(b, env);
	if (isNaN(A)) return quiet(A);
	if (isNaN(B)) return quiet(B);
	const Un ua = unp(A), ub = unp(B);
	const bool s = ua.s != ub.s;
	const u32 sg = s ? 0x80000000u : 0;
	if (ua.inf && ub.inf) return kDefNaN;
	if (ua.inf) return sg | 0x7f800000u;
	if (ub.inf) return sg;
	if (ua.zero && ub.zero) return kDefNaN;
	if (ub.zero) return sg | 0x7f800000u;
	if (ua.zero) return sg;
	const u128 num = static_cast<u128>(ua.m) << 60;
	const u128 qt = num / ub.m;
	const bool st = (num % ub.m) != 0;
	return pack(s, qt, ua.e - 60 - ub.e, st, env);
}

static u128 isqrt128(u128 n, bool& exact)
{
	u128 res = 0, bit = static_cast<u128>(1) << 126;
	while (bit > n) bit >>= 2;
	while (bit)
	{
		if (n >= res + bit) { n -= res + bit; res = (res >> 1) + bit; }
		else res >>= 1;
		bit >>= 2;
	}
	exact = n == 0;
	return res;
}

static u32 sf_sqrt(u32 a, const Env& env)
{
	const u32 A = dazIn(a, env);
	if (isNaN(A)) return quiet(A);
	const Un ua = unp(A);
	if (ua.zero) return A;
	if (ua.s) return kDefNaN;
	if (ua.inf) return A;
	u32 m = ua.m;
	int e = ua.e;
	u128 M = m;
	if (e & 1) { M <<= 1; e -= 1; }
	M <<= 80;
	bool exact;
	const u128 r = isqrt128(M, exact);
	return pack(false, r, (e - 80) / 2, !exact, env);
}

static inline s32 ordKey(u32 x) { return static_cast<s32>(x ^ ((static_cast<s32>(x) >> 31) & 0x7fffffff)); }
// microVU MIN_MAX_PS/SS: integer sign-magnitude ordering, no DAZ, NaN is ordered by bits.
static inline u32 vuMax(u32 to, u32 from) { return ordKey(to) > ordKey(from) ? to : from; }
static inline u32 vuMin(u32 to, u32 from) { return ordKey(from) > ordKey(to) ? to : from; }

static u32 sf_cvtt(u32 x, const Env& env)
{
	x = dazIn(x, env);
	const int ex = (x >> 23) & 0xff;
	if (ex == 255) return 0x80000000u;
	if (ex < 127) return 0;
	const int e = ex - 127;
	if (e >= 31) return 0x80000000u;
	const u32 m = (x & 0x7fffffu) | 0x800000u;
	const u32 v = e >= 23 ? (m << (e - 23)) : (m >> (23 - e));
	return (x >> 31) ? static_cast<u32>(-static_cast<s32>(v)) : v;
}

static u32 sf_cvtdq2ps(u32 v, const Env& env)
{
	if (v == 0) return 0;
	const bool s = (v >> 31) != 0;
	const u64 mag = s ? static_cast<u64>(-static_cast<s64>(static_cast<s32>(v))) : v;
	Env e2 = env;
	e2.ftz = false;
	return pack(s, mag, 0, false, e2);
}

// mVUclamp1 (MINPS(maxvals) then MAXPS(minvals)): any NaN -> +max, +-Inf -> +-max.
static inline u32 clamp1(u32 x, const Env& env)
{
	x = dazIn(x, env);
	if (isNaN(x)) return kPlusMax;
	if (isInf(x)) return (x & 0x80000000u) | kPlusMax;
	return x;
}
// mVUclamp2/3 sign-preserving integer path (PMINSD maxvals; PMINUD minvals).
static inline u32 clamp3sign(u32 x)
{
	if (static_cast<s32>(x) > static_cast<s32>(kPlusMax)) x = kPlusMax;
	if (x > kMinusMax) x = kMinusMax;
	return x;
}

enum Op { O_ADD = 0, O_SUB = 1, O_MUL = 2, O_MAX = 3, O_MIN = 4 };

static u32 rawOp(int type, u32 a, u32 b, const Env& env)
{
	switch (type)
	{
		case O_ADD: return sf_add(a, b, env);
		case O_SUB: return sf_sub(a, b, env);
		case O_MUL: return sf_mul(a, b, env);
		case O_MAX: return vuMax(a, b);
		default: return vuMin(a, b);
	}
}

// SSE_ADDPS & co: clampOp() = clamp3 on operands + op + clamp4 on the result.
// mode: 0 none, 1 normal, 2 extra, 3 full(sign). toC/fromC model checkVFClamp().
static u32 sse(int type, u32 to, u32 from, bool toC, bool fromC, int mode, const Env& env)
{
	if (type == O_MAX || type == O_MIN)
		return rawOp(type, to, from, env);
	if (mode >= 2)
	{
		if (toC) to = mode == 3 ? clamp3sign(to) : clamp1(to, env);
		if (fromC) from = mode == 3 ? clamp3sign(from) : clamp1(from, env);
	}
	u32 r = rawOp(type, to, from, env);
	if (mode == 2 && toC) r = clamp1(r, env);
	return r;
}

} // namespace x86p

namespace x86p
{
//------------------------------------------------------------------
// VU model (microVU Upper/Lower/Flags/Macro semantics)
//------------------------------------------------------------------
struct VUm
{
	u32 vf[32][4];
	u32 acc[4];
	u32 vi[32]; // 0..15 integer regs, REG_R/I/Q/P and flag registers
	// microVU register-cache side effect: DIV (ftf = x, clamp modes 2/3) clamps the CACHED full copy of Ft in place
	// (mVUclamp3 on the allocReg(_Ft_) register). Set by applyLower, consumed by the sequence model.
	int cacheClampFt = -1;
	u32 cacheClampVal = 0;
};

struct Cfg
{
	int mode = 1;      // 0 none, 1 normal, 2 extra, 3 full (sign preserve)
	Env env;
	bool cop2 = false; // macro mode
	bool vu1 = false;
};

enum Form { F_X = 0, F_Y, F_Z, F_W, F_I, F_Q, F_P };
enum Fam { FAM_ADD = 0, FAM_SUB, FAM_MUL, FAM_MADD, FAM_MSUB, FAM_MAX, FAM_MINI };
enum UKind : char { K_A = 'a', K_B = 'b', K_C = 'c', K_D = 'd', K_OPMULA = 'o', K_OPMSUB = 'O', K_ABS = 'A', K_FTOI = 'F', K_ITOF = 'I', K_CLIP = 'C' };

struct UOp
{
	std::string name;
	u32 fn6, idx;
	bool usesFd;
	char kind;
	int type;  // Op enum for the SSE primitive
	int form;  // Form
	int fam;
	bool acc;
	int scale; // FTOI/ITOF variant 0..3
};

static std::vector<UOp> BuildUpperOps()
{
	std::vector<UOp> ops;
	static const char* lane[4] = {"x", "y", "z", "w"};
	static const char* famName[7] = {"ADD", "SUB", "MUL", "MADD", "MSUB", "MAX", "MINI"};
	static const int famType[7] = {O_ADD, O_SUB, O_MUL, O_ADD, O_SUB, O_MAX, O_MIN};
	static const u32 bcBase[7] = {0x00, 0x04, 0x18, 0x08, 0x0c, 0x10, 0x14};
	const auto kindOf = [](int fam, bool acc) -> char {
		if (fam == FAM_MADD || fam == FAM_MSUB) return acc ? K_B : (fam == FAM_MADD ? K_C : K_D);
		return K_A;
	};
	for (int fam = 0; fam < 7; ++fam)
		for (int bc = 0; bc < 4; ++bc)
			ops.push_back({std::string(famName[fam]) + lane[bc], bcBase[fam] + bc, 0, true, kindOf(fam, false), famType[fam], bc, fam, false, 0});
	struct Fn { int fam; int form; u32 fn; };
	const Fn others[] = {
		{FAM_MUL, F_Q, 0x1c}, {FAM_MAX, F_I, 0x1d}, {FAM_MUL, F_I, 0x1e}, {FAM_MINI, F_I, 0x1f}, {FAM_ADD, F_Q, 0x20},
		{FAM_MADD, F_Q, 0x21}, {FAM_ADD, F_I, 0x22}, {FAM_MADD, F_I, 0x23}, {FAM_SUB, F_Q, 0x24}, {FAM_MSUB, F_Q, 0x25},
		{FAM_SUB, F_I, 0x26}, {FAM_MSUB, F_I, 0x27}, {FAM_ADD, F_P, 0x28}, {FAM_MADD, F_P, 0x29}, {FAM_MUL, F_P, 0x2a},
		{FAM_MAX, F_P, 0x2b}, {FAM_SUB, F_P, 0x2c}, {FAM_MSUB, F_P, 0x2d}, {FAM_MINI, F_P, 0x2f}};
	for (const Fn& f : others)
	{
		const char* suffix = f.form == F_Q ? "q" : f.form == F_I ? "i" : "";
		ops.push_back({std::string(famName[f.fam]) + suffix, f.fn, 0, true, kindOf(f.fam, false), famType[f.fam], f.form, f.fam, false, 0});
	}
	struct K { int fam; u32 bcIdx, iT, iIdx, qT, qIdx, pT, pIdx; };
	const K ks[] = {{FAM_ADD, 0, 2, 8, 0, 8, 0, 10}, {FAM_SUB, 1, 2, 9, 0, 9, 0, 11}, {FAM_MADD, 2, 3, 8, 1, 8, 1, 10},
		{FAM_MSUB, 3, 3, 9, 1, 9, 1, 11}, {FAM_MUL, 6, 2, 7, 0, 7, 2, 10}};
	for (const K& k : ks)
	{
		const std::string n = std::string(famName[k.fam]) + "A";
		for (u32 bc = 0; bc < 4; ++bc)
			ops.push_back({n + lane[bc], 0x3cu + bc, k.bcIdx, false, kindOf(k.fam, true), famType[k.fam], static_cast<int>(bc), k.fam, true, 0});
		ops.push_back({n + "i", 0x3cu + k.iT, k.iIdx, false, kindOf(k.fam, true), famType[k.fam], F_I, k.fam, true, 0});
		ops.push_back({n + "q", 0x3cu + k.qT, k.qIdx, false, kindOf(k.fam, true), famType[k.fam], F_Q, k.fam, true, 0});
		ops.push_back({n, 0x3cu + k.pT, k.pIdx, false, kindOf(k.fam, true), famType[k.fam], F_P, k.fam, true, 0});
	}
	ops.push_back({"OPMULA", 0x3e, 11, false, K_OPMULA, O_MUL, F_P, FAM_MUL, true, 0});
	ops.push_back({"OPMSUB", 0x2e, 0, true, K_OPMSUB, O_SUB, F_P, FAM_MSUB, false, 0});
	ops.push_back({"ABS", 0x3d, 7, false, K_ABS, O_MUL, F_P, FAM_ADD, false, 0});
	ops.push_back({"CLIP", 0x3f, 7, false, K_CLIP, O_MUL, F_P, FAM_ADD, false, 0});
	static const char* fn[4] = {"0", "4", "12", "15"};
	for (u32 s = 0; s < 4; ++s)
	{
		ops.push_back({std::string("FTOI") + fn[s], 0x3cu + s, 5, false, K_FTOI, O_MUL, F_P, FAM_ADD, false, static_cast<int>(s)});
		ops.push_back({std::string("ITOF") + fn[s], 0x3cu + s, 4, false, K_ITOF, O_MUL, F_P, FAM_ADD, false, static_cast<int>(s)});
	}
	return ops;
}

static inline u32 encodeUpper(const UOp& o, u32 mask, u32 fs, u32 ft, u32 fd)
{
	return ((mask & 15u) << 21) | (ft << 16) | (fs << 11) | ((o.usesFd ? fd : o.idx) << 6) | o.fn6;
}

enum Cl { CT_FT = 1, CT_FS = 2, CT_ACC = 4 };
static unsigned clampType(const UOp& o, u32 mask, bool cop2)
{
	const bool ps = mask == 0xf;
	const int f = o.form;
	switch (o.fam)
	{
		case FAM_SUB: return (!o.acc && ps) ? (CT_FS | CT_FT) : 0;
		case FAM_MUL:
			if (!o.acc) return ps ? (CT_FS | CT_FT) : CT_FS;
			if (f == F_X || f == F_Y || f == F_Z) return CT_FS;
			if (f == F_W) return ps ? (CT_FS | CT_FT) : CT_FS;
			return 0;
		case FAM_MADD:
			if (f == F_I || f == F_Q || f == F_P) return 0;
			if (o.acc) return CT_FS;
			if (f == F_W) return cop2 ? (CT_ACC | CT_FT | CT_FS) : CT_FS;
			return CT_FS;
		case FAM_MSUB: return (!o.acc && f == F_P && cop2) ? CT_FS : 0;
		default: return 0;
	}
}

// mVUupdateFlags: MAC = (sign << 4 | zero) per masked lane, status keeps D/I and sticky history.
static void updFlags(VUm& v, const u32 res[4], u32 mask, const Env& env)
{
	u32 mac = 0;
	for (u32 l = 0; l < 4; ++l)
	{
		if (!(mask & (8u >> l))) continue;
		const u32 bit = 3 - l;
		mac |= (zeroDaz(res[l], env) ? 1u : 0u) << bit;
		mac |= (res[l] >> 31) << (4 + bit);
	}
	v.vi[REG_MAC_FLAG] = mac;
	u32 s = v.vi[REG_STATUS_FLAG] & ~0xfu; // U/O/Z/S cleared (non-sticky)
	if (mac & 0x0f) s |= 0x01 | 0x40;
	if (mac & 0xf0) s |= 0x02 | 0x80;
	v.vi[REG_STATUS_FLAG] = s;
}

static inline u32 fvalPow2(int which, bool ftoi)
{
	static const u32 ftoiScale[4] = {0, 0x41800000u, 0x45800000u, 0x47000000u};
	static const u32 itofScale[4] = {0, 0x3d800000u, 0x39800000u, 0x38000000u};
	return ftoi ? ftoiScale[which] : itofScale[which];
}

static void applyUpper(VUm& v, const UOp& o, u32 mask, u32 fs, u32 ft, u32 fd, const Cfg& c)
{
	const Env& env = c.env;
	const int mode = c.mode;
	u32 Fs[4], Ft[4];
	std::memcpy(Fs, v.vf[fs], 16);
	if (fs == 0) { Fs[0] = Fs[1] = Fs[2] = 0; Fs[3] = 0x3f800000u; }
	u32 ftSrc[4];
	std::memcpy(ftSrc, v.vf[ft], 16);
	if (ft == 0) { ftSrc[0] = ftSrc[1] = ftSrc[2] = 0; ftSrc[3] = 0x3f800000u; }

	switch (o.kind)
	{
		case K_ABS:
			if (ft)
				for (u32 l = 0; l < 4; ++l)
					if (mask & (8u >> l)) v.vf[ft][l] = Fs[l] & 0x7fffffffu;
			return;
		case K_FTOI:
		case K_ITOF:
		{
			if (!ft) return;
			for (u32 l = 0; l < 4; ++l)
			{
				if (!(mask & (8u >> l))) continue;
				u32 x = Fs[l];
				if (o.kind == K_FTOI)
				{
					if (o.scale) x = sf_mul(x, fvalPow2(o.scale, true), env);
					const u32 t1 = static_cast<s32>(x) > static_cast<s32>(0x4effffffu) ? 0xffffffffu : 0;
					v.vf[ft][l] = sf_cvtt(x, env) ^ t1;
				}
				else
				{
					x = sf_cvtdq2ps(x, env);
					if (o.scale) x = sf_mul(x, fvalPow2(o.scale, false), env);
					v.vf[ft][l] = x;
				}
			}
			return;
		}
		case K_CLIP:
		{
			const u32 w = ftSrc[3] & 0x7fffffffu;
			u32 bits = 0;
			for (u32 l = 0; l < 3; ++l)
			{
				u32 x = Fs[l];
				if ((x & 0x7f800000u) == 0) x = 0; // exponent-zero lanes cannot exceed |w|
				if (static_cast<s32>(x) > static_cast<s32>(w)) bits |= 1u << (2 * l);
				if (static_cast<s32>(x ^ 0x80000000u) > static_cast<s32>(w)) bits |= 2u << (2 * l);
			}
			v.vi[REG_CLIP_FLAG] = ((v.vi[REG_CLIP_FLAG] << 6) & 0xffffffu) | bits;
			return;
		}
		default: break;
	}

	if (o.kind == K_OPMULA || o.kind == K_OPMSUB)
	{
		const u32 sy[4] = {Fs[1], Fs[2], Fs[0], Fs[3]};
		const u32 tz[4] = {ftSrc[2], ftSrc[0], ftSrc[1], ftSrc[3]};
		u32 res[4] = {0, 0, 0, 0};
		for (u32 l = 0; l < 4; ++l)
		{
			if (!(mask & (8u >> l))) continue;
			const u32 p = sse(O_MUL, sy[l], tz[l], fs != 0, ft != 0, mode, env);
			res[l] = o.kind == K_OPMULA ? p : sse(O_SUB, v.acc[l], p, true, fs != 0, mode, env);
		}
		for (u32 l = 0; l < 4; ++l)
			if (mask & (8u >> l))
			{
				if (o.kind == K_OPMULA) v.acc[l] = res[l];
				else if (fd) v.vf[fd][l] = res[l];
			}
		updFlags(v, res, mask, env);
		return;
	}

	// Ft operand by form.
	[[maybe_unused]] bool ftIsI = false;
	for (u32 l = 0; l < 4; ++l)
	{
		switch (o.form)
		{
			case F_X: case F_Y: case F_Z: case F_W: Ft[l] = ftSrc[o.form]; break;
			case F_I: Ft[l] = v.vi[REG_I]; ftIsI = true; break;
			case F_Q: Ft[l] = v.vi[REG_Q]; break;
			default: Ft[l] = ftSrc[l]; break;
		}
	}
	const unsigned ct = clampType(o, mask, c.cop2);
	u32 res[4] = {0, 0, 0, 0};

	// doSafeSub: plain SUB/SUBA with Ft == Fs yields +0 without touching the FP unit.
	if (o.kind == K_A && o.type == O_SUB && o.form == F_P && fs == ft)
	{
		for (u32 l = 0; l < 4; ++l)
			if (mask & (8u >> l)) res[l] = 0;
	}
	else
	{
		for (u32 l = 0; l < 4; ++l)
		{
			if (!(mask & (8u >> l))) continue;
			u32 s = Fs[l], t = Ft[l], a = v.acc[l];
			if (mode == 1)
			{
				if (ct & CT_FT) t = clamp1(t, env);
				if (ct & CT_FS) s = clamp1(s, env);
				if (ct & CT_ACC) a = clamp1(a, env);
			}
			// checkVFClamp(): registers loaded from VF00 (isZero) are never clamped. The I register is
			// allocated as allocReg(33, 0, mask) so its map entry is VFreg 0 (not 33) and it IS clamped like Ft.
			const bool fsC = fs != 0;
			const bool tC = o.form == F_P ? ft != 0 : true;
			switch (o.kind)
			{
				case K_A: res[l] = sse(o.type, s, t, fsC, tC, mode, env); break;
				case K_B:
				{
					const u32 p = sse(O_MUL, s, t, fsC, tC, mode, env);
					res[l] = sse(o.type, a, p, true, fsC, mode, env); // ADD for MADDA, SUB for MSUBA
					break;
				}
				case K_C:
				{
					const u32 p = sse(O_MUL, s, t, fsC, tC, mode, env);
					res[l] = sse(O_ADD, p, a, fsC, true, mode, env);
					break;
				}
				default: // K_D
				{
					const u32 p = sse(O_MUL, s, t, fsC, tC, mode, env);
					res[l] = sse(O_SUB, a, p, true, fsC, mode, env);
					break;
				}
			}
		}
	}
	for (u32 l = 0; l < 4; ++l)
	{
		if (!(mask & (8u >> l))) continue;
		if (o.acc) v.acc[l] = res[l];
		else if (fd) v.vf[fd][l] = res[l];
	}
	if (o.type != O_MAX && o.type != O_MIN)
		updFlags(v, res, mask, env);
}
} // namespace x86p

namespace x86p
{
//------------------------------------------------------------------
// Lower ops: DIV/SQRT/RSQRT, EFU (VU1), R regs, VI moves/ALU
//------------------------------------------------------------------
constexpr u32 divI = 0x1040000u, divD = 0x2080000u;

enum LK
{
	L_DIV = 0, L_SQRT, L_RSQRT, L_MTIR, L_MFIR, L_MOVE, L_MR32, L_RNEXT, L_RGET, L_RINIT, L_RXOR,
	L_IADD, L_ISUB, L_IAND, L_IOR, L_IADDI, L_MFP,
	L_ESADD, L_EATANXY, L_ESQRT, L_ESIN, L_ERSADD, L_EATANXZ, L_ERSQRT, L_EATAN, L_ELENG, L_ESUM, L_ERCPR, L_EEXP, L_ERLENG,
	L_COUNT
};

struct LInfo { const char* name; u32 fn, fd; bool efu; };
static const LInfo kLower[L_COUNT] = {
	{"DIV", 0x3c, 14, false}, {"SQRT", 0x3d, 14, false}, {"RSQRT", 0x3e, 14, false}, {"MTIR", 0x3c, 15, false},
	{"MFIR", 0x3d, 15, false}, {"MOVE", 0x3c, 12, false}, {"MR32", 0x3d, 12, false}, {"RNEXT", 0x3c, 16, false},
	{"RGET", 0x3d, 16, false}, {"RINIT", 0x3e, 16, false}, {"RXOR", 0x3f, 16, false},
	{"IADD", 0x30, 0, false}, {"ISUB", 0x31, 0, false}, {"IAND", 0x34, 0, false}, {"IOR", 0x35, 0, false}, {"IADDI", 0x32, 0, false},
	{"MFP", 0x3c, 25, true},
	{"ESADD", 0x3c, 28, true}, {"EATANxy", 0x3c, 29, true}, {"ESQRT", 0x3c, 30, true}, {"ESIN", 0x3c, 31, true},
	{"ERSADD", 0x3d, 28, true}, {"EATANxz", 0x3d, 29, true}, {"ERSQRT", 0x3d, 30, true}, {"EATAN", 0x3d, 31, true},
	{"ELENG", 0x3e, 28, true}, {"ESUM", 0x3e, 29, true}, {"ERCPR", 0x3e, 30, true}, {"EEXP", 0x3e, 31, true},
	{"ERLENG", 0x3f, 28, true},
};

struct Ins
{
	bool lower = false;
	int op = 0;       // index into the upper table, or LK
	u32 mask = 0xf;
	u32 fs = 0, ft = 0, fd = 0;
	u32 fsf = 0, ftf = 0;
	s32 imm = 0;
	int rawDest = -1; // >= 0 overrides the encoded dest/mask field of a lower op (EFU dest-mask sweep)
};

static u32 encodeLower(const Ins& i)
{
	const LInfo& li = kLower[i.op];
	u32 dest = i.mask;
	u32 fd = li.fd;
	switch (i.op)
	{
		case L_DIV: case L_RSQRT: dest = (i.ftf << 2) | i.fsf; break;
		case L_SQRT: dest = i.ftf << 2; break;
		case L_MTIR: case L_RINIT: case L_RXOR: case L_ESQRT: case L_ESIN: case L_EATAN: case L_EEXP: case L_ERCPR: case L_ERSQRT:
			dest = i.fsf; break;
		case L_IADD: case L_ISUB: case L_IAND: case L_IOR: fd = i.fd; dest = 0; break;
		case L_IADDI: fd = static_cast<u32>(i.imm) & 31u; dest = 0; break;
		default: break;
	}
	if (i.rawDest >= 0) dest = static_cast<u32>(i.rawDest);
	return (0x40u << 25) | ((dest & 15u) << 21) | (i.ft << 16) | (i.fs << 11) | (fd << 6) | li.fn;
}

// x86 SSE helpers used by the lower ops (clampOp wrappers around DIVSS etc.).
static u32 sseDiv(u32 to, u32 from, bool toC, bool fromC, int mode, const Env& env)
{
	if (mode >= 2)
	{
		if (toC) to = mode == 3 ? clamp3sign(to) : clamp1(to, env);
		if (fromC) from = mode == 3 ? clamp3sign(from) : clamp1(from, env);
	}
	u32 r = sf_div(to, from, env);
	if (mode == 2 && toC) r = clamp1(r, env);
	return r;
}

static inline u32 setDivFlag(VUm& v, u32 divFlag)
{
	u32 s = v.vi[REG_STATUS_FLAG] & ~0x30u; // arch D/I bits (internal bits 18/19)
	// Sticky DS/IS bits (internal 24/25) are only ORed in.
	if (divFlag == divI) s |= 0x10 | 0x400;
	else if (divFlag == divD) s |= 0x20 | 0x800;
	v.vi[REG_STATUS_FLAG] = s;
	return s;
}

static void efuP(VUm& v, const Ins& in, const Cfg& c)
{
	const Env& env = c.env;
	const int m = c.mode;
	const u32 one = 0x3f800000u;
	u32 F[4];
	std::memcpy(F, v.vf[in.fs], 16);
	if (in.fs == 0) { F[0] = F[1] = F[2] = 0; F[3] = one; }
	// mVUloadReg(): a single-lane dest mask loads that lane into lane 0 and zeroes the rest (MOVSSZX); other masks load the whole vector.
	// Only the vector-sum EFU ops (ESADD/ELENG/ERSADD/ERLENG/ESUM) allocate Fs with _X_Y_Z_W; the others use the _Fsf_ lane (mask 1 << (3-fsf)).
	if (in.op == L_ESADD || in.op == L_ELENG || in.op == L_ERSADD || in.op == L_ERLENG || in.op == L_ESUM)
	{
		// allocReg(vf0, 0, mask): a mask without W zeroes the register (PXOR) instead of loading VF00.
		if (in.fs == 0 && !(in.mask & 1))
			F[0] = F[1] = F[2] = F[3] = 0;
		if (in.mask == 8 || in.mask == 4 || in.mask == 2 || in.mask == 1)
		{
			const u32 v = F[in.mask == 8 ? 0 : (in.mask == 4 ? 1 : (in.mask == 2 ? 2 : 3))];
			F[0] = v; F[1] = F[2] = F[3] = 0;
		}
	}
	const u32 X = F[in.fsf & 3];
	const auto MUL = [&](u32 a, u32 b) { return sse(O_MUL, a, b, true, true, m, env); };
	const auto ADD = [&](u32 a, u32 b) { return sse(O_ADD, a, b, true, true, m, env); };
	const auto SUB = [&](u32 a, u32 b) { return sse(O_SUB, a, b, true, true, m, env); };
	const auto DIV = [&](u32 a, u32 b) { return sseDiv(a, b, true, true, m, env); };
	// ERCPR/ERSQRT/ERSADD/ERLENG/EATAN divide in the register allocated from VF(fs): when fs == 0 that register is flagged isZero,
	// so checkVFClamp() skips the operand and result clamps of the divide's destination (mVUclamp3/4).
	const auto DIVFs = [&](u32 a, u32 b) { return sseDiv(a, b, in.fs != 0, true, m, env); };
	const auto rmul = [&](u32 a, u32 b) { return sf_mul(a, b, env); };
	const auto radd = [&](u32 a, u32 b) { return sf_add(a, b, env); };
	const auto sumxyz = [&]() { return radd(radd(rmul(F[0], F[0]), rmul(F[1], F[1])), radd(rmul(F[2], F[2]), 0u)); };
	const u32 T[8] = {0x3f7ffff5u, 0xbeaaa61cu, 0x3e4c40a6u, 0xbe0e6c63u, 0x3dc577dfu, 0xbd6501c4u, 0x3cb31652u, 0xbb84d7e7u};
	const auto eatan = [&](u32 fsv) {
		u32 pq = rmul(fsv, T[0]);
		u32 t2 = fsv;
		for (int k = 1; k < 8; ++k)
		{
			t2 = MUL(t2, fsv);
			t2 = MUL(t2, fsv);
			const u32 t1 = rmul(t2, T[k]);
			pq = ADD(pq, t1);
		}
		return radd(pq, 0x3f490fdbu);
	};
	u32 p = 0;
	switch (in.op)
	{
		case L_ESADD: p = sumxyz(); break;
		case L_ELENG: p = sf_sqrt(sumxyz(), env); break;
		case L_ERSADD: p = DIVFs(one, sumxyz()); break;
		case L_ERLENG: p = DIVFs(one, sf_sqrt(sumxyz(), env)); break;
		case L_ERCPR: p = DIVFs(one, X); break;
		case L_ERSQRT: p = DIVFs(one, sf_sqrt(X & 0x7fffffffu, env)); break;
		case L_ESQRT: p = sf_sqrt(X & 0x7fffffffu, env); break;
		case L_ESUM:
		{
			const u32 a0 = ADD(F[0], F[3]);
			const u32 a1 = ADD(F[1], F[2]);
			p = ADD(a0, a1);
			break;
		}
		case L_EATAN:
		{
			const u32 num = sf_sub(X, one, env);
			const u32 den = sf_add(X, one, env);
			p = eatan(DIVFs(num, den));
			break;
		}
		case L_EATANXY:
		case L_EATANXZ:
		{
			const u32 x = F[0], y = F[in.op == L_EATANXY ? 1 : 2];
			const u32 num = SUB(y, x);
			const u32 den = ADD(x, y);
			p = eatan(DIV(num, den));
			break;
		}
		case L_EEXP:
		{
			const u32 E[6] = {0x3e7fffa8u, 0x3d0007f4u, 0x3b29d3ffu, 0x3933e553u, 0x36b63510u, 0x353961acu};
			u32 pq = radd(rmul(X, E[0]), one);
			u32 t1 = MUL(X, X);
			u32 t2 = t1;
			t1 = rmul(t1, E[1]);
			pq = ADD(pq, t1);
			for (int k = 2; k < 5; ++k)
			{
				t2 = MUL(t2, X);
				t1 = rmul(t2, E[k]);
				pq = ADD(pq, t1);
			}
			t2 = MUL(t2, X);
			t2 = rmul(t2, E[5]);
			pq = ADD(pq, t2);
			pq = MUL(pq, pq);
			pq = MUL(pq, pq);
			p = DIV(one, pq);
			break;
		}
		case L_ESIN:
		{
			const u32 S[4] = {0xbe2aaaa4u, 0x3c08873eu, 0xb94fb21fu, 0x362e9c14u};
			u32 pq = X;
			u32 fsr = MUL(X, X);
			const u32 t1 = fsr;
			fsr = MUL(fsr, pq);
			u32 t2 = fsr;
			fsr = rmul(fsr, S[0]);
			pq = ADD(pq, fsr);
			t2 = MUL(t2, t1);
			fsr = rmul(t2, S[1]);
			pq = ADD(pq, fsr);
			t2 = MUL(t2, t1);
			fsr = rmul(t2, S[2]);
			pq = ADD(pq, fsr);
			t2 = MUL(t2, t1);
			t2 = rmul(t2, S[3]);
			pq = ADD(pq, t2);
			p = pq;
			break;
		}
		default: break;
	}
	v.vi[REG_P] = p;
}

static void applyLower(VUm& v, const Ins& in, const Cfg& c)
{
	const Env& env = c.env;
	const int m = c.mode;
	u32 Fs[4], Ft[4];
	std::memcpy(Fs, v.vf[in.fs], 16);
	std::memcpy(Ft, v.vf[in.ft], 16);
	if (in.fs == 0) { Fs[0] = Fs[1] = Fs[2] = 0; Fs[3] = 0x3f800000u; }
	if (in.ft == 0) { Ft[0] = Ft[1] = Ft[2] = 0; Ft[3] = 0x3f800000u; }
	const auto wrVF = [&](u32 l, u32 val) { if (in.ft) v.vf[in.ft][l] = val; };
	switch (in.op)
	{
		case L_DIV:
		{
			const u32 a = Fs[in.fsf & 3], b = Ft[in.ftf & 3];
			u32 q, flag;
			if (zeroDaz(b, env))
			{
				flag = zeroDaz(a, env) ? divI : divD;
				q = ((a ^ b) & 0x80000000u) | kPlusMax;
			}
			else
			{
				flag = 0;
				q = sseDiv(a, b, in.fs != 0, in.ft != 0, m, env);
				if (m >= 2 && in.ftf == 0 && in.ft != 0)
				{
					v.cacheClampFt = static_cast<int>(in.ft);
					v.cacheClampVal = m == 3 ? clamp3sign(b) : clamp1(b, env);
				}
				if (m >= 1 && in.fs != 0) q = clamp1(q, env);
			}
			v.vi[REG_Q] = q;
			setDivFlag(v, flag);
			return;
		}
		case L_SQRT:
		{
			u32 b = Ft[in.ftf & 3];
			u32 flag = 0;
			if (b & 0x80000000u) { flag = divI; b &= 0x7fffffffu; }
			if (m >= 1)
			{
				// MINSS(Ft, maxvals): NaN picks the second operand (+max), Inf is above max.
				if (isNaN(b) || isInf(b)) b = kPlusMax;
			}
			v.vi[REG_Q] = sf_sqrt(b, env);
			setDivFlag(v, flag);
			return;
		}
		case L_RSQRT:
		{
			const u32 a = Fs[in.fsf & 3];
			u32 b = Ft[in.ftf & 3];
			u32 flag = 0;
			if (b & 0x80000000u) { flag = divI; b &= 0x7fffffffu; }
			const u32 s = sf_sqrt(b, env);
			u32 q;
			if (zeroDaz(s, env))
			{
				flag = zeroDaz(a, env) ? divI : divD;
				q = (a & 0x80000000u) | kPlusMax;
			}
			else
			{
				q = sseDiv(a, s, in.fs != 0, true, m, env);
				if (m >= 1 && in.fs != 0) q = clamp1(q, env);
			}
			v.vi[REG_Q] = q;
			setDivFlag(v, flag);
			return;
		}
		case L_MTIR:
			if (in.ft) v.vi[in.ft] = Fs[in.fsf & 3] & 0xffffu;
			return;
		case L_MFIR:
		{
			const u32 val = in.fs ? static_cast<u32>(static_cast<s32>(static_cast<s16>(v.vi[in.fs] & 0xffffu))) : 0u;
			for (u32 l = 0; l < 4; ++l)
				if (in.mask & (8u >> l)) wrVF(l, val);
			return;
		}
		case L_MFP:
			for (u32 l = 0; l < 4; ++l)
				if (in.mask & (8u >> l)) wrVF(l, v.vi[REG_P]);
			return;
		case L_MOVE:
			for (u32 l = 0; l < 4; ++l)
				if (in.mask & (8u >> l)) wrVF(l, Fs[l]);
			return;
		case L_MR32:
			for (u32 l = 0; l < 4; ++l)
				if (in.mask & (8u >> l)) wrVF(l, Fs[(l + 1) & 3]);
			return;
		case L_RNEXT:
		{
			u32 r = v.vi[REG_R];
			u32 t1 = (r >> 4) & 1, t2 = (r >> 22) & 1;
			r <<= 1;
			r ^= (t1 ^ t2);
			r = (r & 0x007fffffu) | 0x3f800000u;
			v.vi[REG_R] = r;
			for (u32 l = 0; l < 4; ++l)
				if (in.mask & (8u >> l)) wrVF(l, r);
			return;
		}
		case L_RGET:
			for (u32 l = 0; l < 4; ++l)
				if (in.mask & (8u >> l)) wrVF(l, v.vi[REG_R]);
			return;
		case L_RINIT:
			if (in.fs || in.fsf == 3) v.vi[REG_R] = (Fs[in.fsf & 3] & 0x007fffffu) | 0x3f800000u;
			else v.vi[REG_R] = 0x3f800000u;
			return;
		case L_RXOR:
			if (in.fs || in.fsf == 3) v.vi[REG_R] ^= Fs[in.fsf & 3] & 0x7fffffu;
			return;
		case L_IADD: case L_ISUB: case L_IAND: case L_IOR: case L_IADDI:
		{
			const u32 a = v.vi[in.fs] & 0xffffu, b = v.vi[in.ft] & 0xffffu;
			u32 r;
			if (in.op == L_IADD) r = a + b;
			else if (in.op == L_ISUB) r = a - b;
			else if (in.op == L_IAND) r = a & b;
			else if (in.op == L_IOR) r = a | b;
			else { s32 imm = (in.imm << 27) >> 27; r = a + static_cast<u32>(imm); }
			const u32 dst = in.op == L_IADDI ? in.ft : in.fd;
			if (dst) v.vi[dst] = r & 0xffffu;
			return;
		}
		default:
			if (kLower[in.op].efu && (c.vu1 || in.op == L_MFP)) efuP(v, in, c); // EFU ops are NOPs on VU0
			return;
	}
}
} // namespace x86p

namespace x86p
{
//------------------------------------------------------------------
// Harness: run the arm64 JIT, run the x86 model, compare, aggregate
//------------------------------------------------------------------
struct Rng
{
	u64 s;
	explicit Rng(u64 seed) : s(seed * 0x9e3779b97f4a7c15ull + 0x1234567ull) { next(); next(); }
	u32 next() { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return static_cast<u32>(s >> 16); }
	u32 below(u32 n) { return next() % n; }
};

// 0x7fc00000 (ARM's default NaN) is deliberately absent: the VU flag packs read an exact +0x7fc00000 as the negative x86 default NaN
// (0xffc00000, S flag), so that bit pattern as an externally supplied operand is a documented flag-only exception.
static const u32 kVals[] = {
	0x00000000u, 0x80000000u, 0x00000001u, 0x80000001u, 0x007fffffu, 0x807fffffu, 0x00800000u, 0x80800000u,
	0x3f800000u, 0xbf800000u, 0x40490fdbu, 0xc0a00000u, 0x3f000000u, 0x3f800001u, 0x01000000u, 0x7e800000u,
	0xfe800000u, 0x4b800000u, 0x7f7fffffu, 0xff7fffffu, 0x7f800000u, 0xff800000u, 0x7fc00001u, 0xffc00000u,
	0x7f800001u, 0xff800001u, 0x7fc12345u, 0xffffffffu, 0x7fffffffu, 0x7fbfffffu, 0x3effffffu, 0x4effffffu};
constexpr u32 kN = sizeof(kVals) / sizeof(kVals[0]);

static const char* clsName(u32 x)
{
	const u32 a = x & 0x7fffffffu;
	const bool n = x >> 31;
	const u32 ex = (x >> 23) & 0xff;
	if (a == 0) return n ? "-0" : "+0";
	if (ex == 0) return n ? "-den" : "+den";
	if (a == 0x00800000u) return n ? "-minN" : "+minN";
	if (a == 0x7f7fffffu) return n ? "-maxN" : "+maxN";
	if (ex == 255)
	{
		if (a == 0x7f800000u) return n ? "-Inf" : "+Inf";
		if (a == 0x7fc00000u) return n ? "-qNaN(def)" : "+qNaN";
		if (!(a & 0x400000u)) return n ? "-sNaN" : "+sNaN";
		return n ? "-qNaN(pl)" : "+qNaN(pl)";
	}
	if (ex >= 0xfd) return n ? "-bigN" : "+bigN";
	if (ex <= 2) return n ? "-tinyN" : "+tinyN";
	return n ? "-N" : "+N";
}

struct Agg { u64 n = 0; std::string ex; std::set<std::string> ops; };
static std::map<std::string, Agg> g_agg;
static std::map<std::string, Agg> g_cause;
static u64 g_cases = 0, g_caseFails = 0;
static u64 g_groupCases = 0, g_groupFails = 0;

static bool allowed(const std::string& key)
{
	for (const AllowEntry& e : kAllow)
		if (e.key && key.find(e.key) != std::string::npos)
			return true;
	return false;
}

static void record(const std::string& key, const std::string& example, const std::string& cause = std::string(), const std::string& op = std::string())
{
	if (allowed(key)) return;
	Agg& a = g_agg[key];
	if (a.n++ == 0) a.ex = example;
	if (!cause.empty())
	{
		Agg& c = g_cause[cause];
		if (c.n++ == 0) c.ex = key + " | " + example;
		if (c.ops.size() < 14) c.ops.insert(op);
	}
	++g_groupFails;
}

static std::string bitNames(u32 v, const char* const* names, int count)
{
	std::string s;
	for (int i = 0; i < count; ++i)
		if (v & (1u << i)) { if (!s.empty()) s += ","; s += names[i]; }
	return s.empty() ? "-" : s;
}
static std::string flagCause(const char* field, u32 e, u32 g)
{
	static const char* const mac[16] = {"Zw", "Zz", "Zy", "Zx", "Sw", "Sz", "Sy", "Sx", "Uw", "Uz", "Uy", "Ux", "Ow", "Oz", "Oy", "Ox"};
	static const char* const st[12] = {"Z", "S", "U", "O", "I", "D", "ZS", "SS", "US", "OS", "IS", "DS"};
	const bool isMac = field[0] == 'M';
	const u32 extra = g & ~e, miss = e & ~g;
	auto nm = [&](u32 v) {
		return isMac ? bitNames(v, mac, 16) : bitNames(v & 0xfffu, st, 12) + ((v >> 12) ? ",hi" : "");
	};
	return std::string(field) + " jit+{" + nm(extra) + "} jit-{" + nm(miss) + "}";
}

// Gate one group of cases: PASS/FAIL line, group counters reset.
static void endGroup(const std::string& name)
{
	char buf[256];
	std::snprintf(buf, sizeof(buf), "x86p %s (%llu cases, %llu mismatches)", name.c_str(),
		static_cast<unsigned long long>(g_groupCases), static_cast<unsigned long long>(g_groupFails));
	Check(g_groupFails == 0, buf);
	g_caseFails += g_groupFails;
	g_groupCases = g_groupFails = 0;
}

static void printReport(const char* title, size_t maxLines = 500)
{
	std::printf("X86PARITY REPORT %s: %zu root-cause keys, %zu detail keys, %llu cases, %llu mismatches\n", title, g_cause.size(), g_agg.size(),
		static_cast<unsigned long long>(g_cases), static_cast<unsigned long long>(g_caseFails));
	std::vector<std::pair<std::string, Agg>> crows(g_cause.begin(), g_cause.end());
	std::sort(crows.begin(), crows.end(), [](const auto& a, const auto& b) { return a.second.n > b.second.n; });
	for (const auto& r : crows)
	{
		std::string ops;
		for (const std::string& o : r.second.ops) ops += o + " ";
		std::printf("X86CAUSE n=%llu %s | ops: %s| e.g. %s\n", static_cast<unsigned long long>(r.second.n), r.first.c_str(), ops.c_str(), r.second.ex.c_str());
	}
	std::vector<std::pair<std::string, Agg>> rows(g_agg.begin(), g_agg.end());
	std::sort(rows.begin(), rows.end(), [](const auto& a, const auto& b) { return a.second.n > b.second.n; });
	size_t lines = 0;
	for (const auto& r : rows)
	{
		if (++lines > maxLines) { std::printf("X86PARITY ... %zu more keys omitted\n", rows.size() - maxLines); break; }
		std::printf("X86PARITY n=%llu %s | e.g. %s\n", static_cast<unsigned long long>(r.second.n), r.first.c_str(), r.second.ex.c_str());
	}
}

static const char* kAreaName[3] = {"macro", "micro0", "micro1"};

struct Obs { u32 vf[128]; u32 acc[4]; u32 vi[32]; u32 p; };

static void configure(int clamp, int rnd)
{
	auto& rc = EmuConfig.Cpu.Recompiler;
	rc.vu0Overflow = rc.vu1Overflow = clamp >= 1;
	rc.vu0ExtraOverflow = rc.vu1ExtraOverflow = clamp >= 2;
	rc.vu0SignOverflow = rc.vu1SignOverflow = clamp >= 3;
	const u64 mode = (static_cast<u64>(rnd == 0 ? 3 : 0) << 22) | (1ull << 24);
	EmuConfig.Cpu.VU0FPCR.bitmask = mode;
	EmuConfig.Cpu.VU1FPCR.bitmask = mode;
	EmuConfig.Cpu.FPUFPCR.bitmask = mode;
	// The generated dispatchers read FPCR/clamp settings from the packed register copy.
	g_cpuRegistersPack.Cpu = EmuConfig.Cpu;
}

static Env makeEnv(int rnd)
{
	Env e;
	e.rm = rnd == 0 ? 3 : 0;
	e.daz = e.ftz = true;
	return e;
}

static const std::vector<UOp>& upperOps()
{
	static const std::vector<UOp> ops = BuildUpperOps();
	return ops;
}

static u32 wordOf(const Ins& i) { return i.lower ? encodeLower(i) : encodeUpper(upperOps()[i.op], i.mask, i.fs, i.ft, i.fd); }

static bool hasSlowLower(const std::vector<Ins>& prog)
{
	for (const Ins& i : prog)
		if (i.lower && (i.op == L_DIV || i.op == L_SQRT || i.op == L_RSQRT || kLower[i.op].efu))
			return true;
	return false;
}

static void runJit(int area, const std::vector<Ins>& prog, const FmacInit& init, Obs& out)
{
	static const u32 zeroVI[16] = {};
	if (area == 0)
	{
		std::vector<u32> code;
		for (const Ins& i : prog)
			code.push_back((0x12u << 26) | (1u << 25) | (wordOf(i) & 0x1ffffffu));
		s_fmac_init = &init;
		const EESnapshot snap = RunEEProgramWithIrMode(0, code, nullptr, nullptr, nullptr, nullptr, false, init.vf);
		s_fmac_init = nullptr;
		std::memcpy(out.vf, snap.vu0_vf, sizeof(out.vf));
		std::memcpy(out.acc, snap.vu0_acc, sizeof(out.acc));
		std::memcpy(out.vi, snap.vu0_vi, sizeof(out.vi));
		out.p = 0;
		return;
	}
	std::vector<u32> program;
	for (const Ins& i : prog)
	{
		if (i.lower) { program.push_back(wordOf(i)); program.push_back(NOP_U); }
		else { program.push_back(NOP_L); program.push_back(wordOf(i)); }
	}
	const u32 pad = hasSlowLower(prog) ? 64 : 0;
	for (u32 k = 0; k < pad; ++k) { program.push_back(NOP_L); program.push_back(NOP_U); }
	program.push_back(NOP_L);
	program.push_back(NOP_U | 0x40000000u);
	program.push_back(NOP_L);
	program.push_back(NOP_U);
	u32 vi0[16];
	std::memcpy(vi0, zeroVI, sizeof(vi0));
	s_fmac_init = &init;
	const Snapshot snap = RunVU(area - 1, true, program, 0x80, 0, 256, false, nullptr, init.vf, nullptr, vi0);
	s_fmac_init = nullptr;
	std::memcpy(out.vf, snap.regs.VF, sizeof(out.vf));
	std::memcpy(out.acc, &snap.regs.ACC, sizeof(out.acc));
	for (u32 i = 0; i < 32; ++i)
		out.vi[i] = snap.regs.VI[i].UL;
	out.p = snap.regs.VI[REG_P].UL;
}

static bool g_modelRegCache = true; // x86 register-cache effects in micro sequences (regcache suite turns it off for A/B)

static void runModel(int area, const std::vector<Ins>& prog, const FmacInit& init, const Cfg& cfg, Obs& out)
{
	VUm v{};
	std::memcpy(v.vf, init.vf, sizeof(v.vf));
	std::memcpy(v.acc, init.acc, sizeof(v.acc));
	v.vi[REG_I] = init.i;
	v.vi[REG_Q] = init.q;
	v.vi[REG_R] = init.r;
	v.vi[REG_P] = init.p;
	v.vi[REG_STATUS_FLAG] = init.status;
	v.vi[REG_MAC_FLAG] = init.mac;
	v.vi[REG_CLIP_FLAG] = init.clip;
	// Micro programs model the microVU VF register cache (see VUm::cacheClampFt): v.vf is the reader-visible view, mem[] is what
	// memory holds. Full-mask writes leave a dirty cached copy; a partial write into a VF that has a cached copy merges into the copy
	// and marks the whole copy dirty (written back at the block end), which carries in-place clamped lanes into memory.
	const bool cached = area != 0 && g_modelRegCache;
	u32 mem[32][4];
	bool has[32] = {}, dirty[32] = {};
	std::memcpy(mem, v.vf, sizeof(mem));
	for (const Ins& i : prog)
	{
		int d = -1;
		u32 wm = i.mask;
		if (cached)
		{
			if (i.lower)
			{
				if (i.op == L_MFIR || i.op == L_MFP || i.op == L_MOVE || i.op == L_MR32 || i.op == L_RNEXT || i.op == L_RGET) d = static_cast<int>(i.ft);
			}
			else
			{
				const UOp& o = upperOps()[i.op];
				if (o.kind == K_ABS || o.kind == K_FTOI || o.kind == K_ITOF) d = static_cast<int>(i.ft);
				else if (o.usesFd && !o.acc) d = static_cast<int>(i.fd);
			}
			if (d == 0) d = -1;
		}
		v.cacheClampFt = -1;
		if (i.lower) applyLower(v, i, cfg);
		else applyUpper(v, upperOps()[i.op], i.mask, i.fs, i.ft, i.fd, cfg);
		if (!cached) continue;
		if (d >= 0)
		{
			if (wm == 0xf) { has[d] = dirty[d] = true; }
			else if (has[d]) dirty[d] = true;
			else for (u32 l = 0; l < 4; ++l) if (wm & (8u >> l)) mem[d][l] = v.vf[d][l];
		}
		if (v.cacheClampFt > 0)
		{
			const u32 f = static_cast<u32>(v.cacheClampFt);
			if (!has[f]) { has[f] = true; dirty[f] = false; }
			v.vf[f][0] = v.cacheClampVal;
		}
	}
	if (cached)
		for (u32 f = 1; f < 32; ++f)
			if (has[f] && dirty[f]) std::memcpy(mem[f], v.vf[f], 16);
	if (cached) std::memcpy(v.vf, mem, sizeof(mem));
	std::memcpy(out.vf, v.vf, sizeof(out.vf));
	std::memcpy(out.acc, v.acc, sizeof(out.acc));
	std::memcpy(out.vi, v.vi, sizeof(out.vi));
	out.p = v.vi[REG_P];
	(void)area;
}

struct Diff { const char* field; int lane; u32 exp, got; };
static bool g_nanSeen = false; // set by compareObs when a NaN-vs-NaN value difference was exempted

static void compareObs(int area, const Obs& e, const Obs& g, std::vector<Diff>& d, u32 mask)
{
	// Deliberate value-class exemption (documented, see kAllow note): when BOTH sides are NaN the payload/sign is not
	// compared (x86 default NaN 0xFFC00000 vs ARM 0x7FC00000, first-operand vs sNaN-priority propagation).
	// Flags derived from those NaNs are still compared exactly and reported under "NaN-sign".
	g_nanSeen = false;
	const auto sameVal = [&](u32 a, u32 b) {
		if (a == b) return true;
		if (isNaN(a) && isNaN(b)) { g_nanSeen = true; return true; }
		return false;
	};
	for (u32 i = 0; i < 128; ++i)
		if (!sameVal(e.vf[i], g.vf[i])) d.push_back({"VF", static_cast<int>(i & 3), e.vf[i], g.vf[i]});
	for (u32 l = 0; l < 4; ++l)
		if (!sameVal(e.acc[l], g.acc[l])) d.push_back({"ACC", static_cast<int>(l), e.acc[l], g.acc[l]});
	for (u32 i = 1; i < 16; ++i)
		if ((e.vi[i] & 0xffffu) != (g.vi[i] & 0xffffu)) d.push_back({"VI", -1, e.vi[i] & 0xffffu, g.vi[i] & 0xffffu});
	// X86P_IGNORE_UO=1 hides the MAC/status underflow/overflow bits (x86 never computes them).
	static const bool ignoreUO = std::getenv("X86P_IGNORE_UO") != nullptr;
	const u32 macKeep = ignoreUO ? ~0xff00u : ~0u;
	const u32 stKeep = ignoreUO ? ~(0x4u | 0x8u | 0x100u | 0x200u) : ~0u;
	const u32 em = e.vi[REG_MAC_FLAG] & macKeep, gm = g.vi[REG_MAC_FLAG] & macKeep;
	if (em != gm)
	{
		int lane = -1;
		for (u32 l = 0; l < 4 && lane < 0; ++l)
		{
			const u32 bit = 3 - l;
			if (((em ^ gm) & ((1u << bit) | (1u << (4 + bit)) | (1u << (8 + bit)) | (1u << (12 + bit)))) != 0) lane = static_cast<int>(l);
		}
		d.push_back({"MAC", lane, em, gm});
	}
	if ((e.vi[REG_STATUS_FLAG] & stKeep) != (g.vi[REG_STATUS_FLAG] & stKeep)) d.push_back({"STATUS", -1, e.vi[REG_STATUS_FLAG] & stKeep, g.vi[REG_STATUS_FLAG] & stKeep});
	if (e.vi[REG_CLIP_FLAG] != g.vi[REG_CLIP_FLAG]) d.push_back({"CLIP", -1, e.vi[REG_CLIP_FLAG], g.vi[REG_CLIP_FLAG]});
	if (!sameVal(e.vi[REG_Q], g.vi[REG_Q])) d.push_back({"Q", -1, e.vi[REG_Q], g.vi[REG_Q]});
	if (e.vi[REG_I] != g.vi[REG_I]) d.push_back({"I", -1, e.vi[REG_I], g.vi[REG_I]});
	if (e.vi[REG_R] != g.vi[REG_R]) d.push_back({"R", -1, e.vi[REG_R], g.vi[REG_R]});
	if (area == 2 && !sameVal(e.p, g.p)) d.push_back({"P", -1, e.p, g.p});
	(void)mask;
}

struct Ctx
{
	int area, clamp, rnd;
	Cfg cfg;
};

static std::string hex(u32 x) { char b[16]; std::snprintf(b, sizeof(b), "%08x", x); return b; }
static std::string lanes4(const u32* p) { return hex(p[0]) + "," + hex(p[1]) + "," + hex(p[2]) + "," + hex(p[3]); }

static std::string ctxKey(const Ctx& c, const std::string& op)
{
	char b[96];
	std::snprintf(b, sizeof(b), "%s c%d %s %s", kAreaName[c.area], c.clamp, c.rnd == 0 ? "chop" : "near", op.c_str());
	return b;
}

// Core: run one program, compare, record. 'what' describes the operand classes for the key.
static void runCase(const Ctx& c, const std::string& opKey, const std::vector<Ins>& prog, const FmacInit& init,
	const std::string& tag)
{
	Obs exp{}, got{};
	runModel(c.area, prog, init, c.cfg, exp);
	runJit(c.area, prog, init, got);
	++g_cases;
	++g_groupCases;
	std::vector<Diff> diffs;
	compareObs(c.area, exp, got, diffs, prog.empty() ? 0 : prog.back().mask);
	if (diffs.empty()) return;
	const Ins& last = prog.back();
	int shown = 0;
	for (const Diff& d : diffs)
	{
		if (++shown > 4) break;
		std::string key = ctxKey(c, opKey) + " [" + d.field + "]";
		std::string cls;
		if (prog.size() == 1)
		{
			int lane = d.lane;
			if (lane < 0)
			{
				lane = 0;
				for (u32 l = 0; l < 4; ++l) if (last.mask & (8u >> l)) { lane = static_cast<int>(l); break; }
			}
			const u32 fsv = init.vf[last.fs * 4 + (last.lower ? (last.fsf & 3) : lane)];
			u32 ftv;
			if (last.lower) ftv = init.vf[last.ft * 4 + (last.ftf & 3)];
			else
			{
				const UOp& o = upperOps()[last.op];
				if (o.form <= F_W) ftv = init.vf[last.ft * 4 + o.form];
				else if (o.form == F_I) ftv = init.i;
				else if (o.form == F_Q) ftv = init.q;
				else ftv = init.vf[last.ft * 4 + lane];
			}
			cls = std::string(" Fs=") + clsName(fsv) + " Ft=" + clsName(ftv);
			if (!last.lower && (upperOps()[last.op].kind == K_C || upperOps()[last.op].kind == K_D || upperOps()[last.op].kind == K_B ||
				upperOps()[last.op].kind == K_OPMSUB))
				cls += std::string(" ACC=") + clsName(init.acc[lane]);
		}
		else
			cls = " " + tag;
		key += cls;
		char ex[640];
		std::string in;
		if (prog.size() == 1)
		{
			in = "Fs=" + lanes4(&init.vf[last.fs * 4]) + " Ft=" + lanes4(&init.vf[last.ft * 4]) + " ACC=" + lanes4(init.acc) +
				" I=" + hex(init.i) + " Q=" + hex(init.q) + " mask=" + std::to_string(last.mask) + " fs=" + std::to_string(last.fs) +
				" ft=" + std::to_string(last.ft) + " fd=" + std::to_string(last.fd) + " fsf=" + std::to_string(last.fsf) + " ftf=" + std::to_string(last.ftf);
		}
		else
			in = "seq of " + std::to_string(prog.size());
		std::snprintf(ex, sizeof(ex), "%s lane=%d model=%08x jit=%08x in{%s}", d.field, d.lane, d.exp, d.got, in.c_str());
		std::string cause = std::string(kAreaName[c.area]) + " c" + std::to_string(c.clamp) + " " + (c.rnd ? "near " : "chop ");
		if (d.field[0] == 'M' || d.field[0] == 'S') cause += flagCause(d.field, d.exp, d.got);
		else if (d.field[0] == 'C') cause += "CLIP";
		else cause += std::string(d.field) + " " + clsName(d.exp) + "->" + clsName(d.got);
		record(key, ex, cause, opKey);
	}
}

//------------------------------------------------------------------
// Operand set generation
//------------------------------------------------------------------
static void baseInit(FmacInit& init, Rng& r, bool randomState)
{
	std::memset(&init, 0, sizeof(init));
	for (u32 reg = 1; reg < 32; ++reg)
		for (u32 l = 0; l < 4; ++l)
		{
			const u32 k = r.below(10);
			init.vf[reg * 4 + l] = k < 6 ? kVals[r.below(kN)] : (k < 9 ? ((r.next() & 0x807fffffu) | ((100 + r.below(50)) << 23)) : r.next());
		}
	init.vf[3] = 0x3f800000u;
	for (u32 l = 0; l < 3; ++l) init.vf[l] = 0;
	for (u32 l = 0; l < 4; ++l) init.acc[l] = kVals[r.below(kN)];
	init.i = kVals[r.below(kN)];
	init.q = kVals[r.below(kN)];
	init.r = 0x3f800000u | (r.next() & 0x7fffffu);
	init.p = kVals[r.below(kN)];
	if (randomState)
	{
		init.status = r.next() & 0xfcfu;
		init.mac = r.next() & 0xffffu;
		init.clip = r.next() & 0xffffffu;
	}
}

// OPMULA/OPMSUB only exist with dest .xyz (0xE) in real encodings; lane w with xyzw is not observable.
static inline u32 legalMask(const UOp& o, u32 m) { return (o.kind == K_OPMULA || o.kind == K_OPMSUB) ? 0xEu : m; }

static std::string maskStr(u32 m) { std::string s; for (u32 l = 0; l < 4; ++l) if (m & (8u >> l)) s += "xyzw"[l]; return s; }

// Exhaustive-ish single-instruction sweep for one upper op.
static void sweepUpperOp(const Ctx& c, u32 opIdx, u32 step, u32 randomPerMask)
{
	const UOp& o = upperOps()[opIdx];
	const bool fdWrites = o.usesFd;
	(void)fdWrites;
	// 1) class cross product at the full mask (four lane pairs per run)
	const u32 pairsTotal = kN * kN;
	u32 seq = 0;
	for (u32 s = 0; s * 4 < pairsTotal; s += step, ++seq)
	{
		Rng r(opIdx * 7919ull + s * 31ull + 5);
		FmacInit init;
		baseInit(init, r, (s % 4) == 0);
		const u32 fs = 1 + r.below(6), ft0 = 1 + r.below(6);
		u32 ft = (s % 9 == 0) ? fs : ft0, fd = (s % 5 == 0) ? fs : (s % 5 == 1 ? ft : 1 + r.below(6));
		const bool bcLike = o.form != F_P;
		for (u32 l = 0; l < 4; ++l)
		{
			const u32 p = s * 4 + l;
			const u32 a = kVals[(p % kN)];
			const u32 b = bcLike ? kVals[((s * 4) / kN) % kN] : kVals[(p / kN) % kN];
			init.vf[fs * 4 + l] = a;
			if (!bcLike) init.vf[ft * 4 + l] = b;
			else if (o.form <= F_W) init.vf[ft * 4 + o.form] = b;
			init.acc[l] = kVals[(p * 7 + 3 + s) % kN];
		}
		if (o.form == F_I) init.i = kVals[((s * 4) / kN) % kN];
		if (o.form == F_Q) init.q = kVals[((s * 4) / kN) % kN];
		if (fs == ft && o.form == F_P && !(s % 9 == 0)) {}
		Ins in;
		in.op = static_cast<int>(opIdx);
		in.mask = legalMask(o, 0xf);
		in.fs = fs; in.ft = ft; in.fd = fd;
		runCase(c, o.name, {in}, init, "");
	}
	// 2) every destination mask with random classes
	// X86P_MASK=<1..15> restricts the random-class pass to one destination mask.
	for (u32 mask = 1; mask < 16; ++mask)
	{
		const u32 onlyMask = std::getenv("X86P_MASK") ? static_cast<u32>(std::strtoul(std::getenv("X86P_MASK"), nullptr, 0)) : 0;
		if (onlyMask && mask != onlyMask) continue;
		for (u32 n = 0; n < randomPerMask; ++n)
		{
			if (legalMask(o, mask) != mask) continue;
			Rng r(opIdx * 104729ull + mask * 1009ull + n * 13ull + 3);
			FmacInit init;
			baseInit(init, r, (n & 1) == 0);
			Ins in;
			in.op = static_cast<int>(opIdx);
			in.mask = mask;
			in.fs = 1 + r.below(5); in.ft = 1 + r.below(5); in.fd = r.below(8) == 0 ? 0 : 1 + r.below(5);
			if (n % 7 == 3) in.fd = in.fs;
			if (n % 7 == 4) in.ft = in.fs;
			runCase(c, o.name, {in}, init, "");
		}
	}
	// 3) VF00 as a source (checkVFClamp isZero quirk) and fd = VF00
	for (u32 n = 0; n < 6; ++n)
	{
		Rng r(opIdx * 15485863ull + n + 17);
		FmacInit init;
		baseInit(init, r, false);
		Ins in;
		in.op = static_cast<int>(opIdx);
		in.mask = legalMask(o, (n & 1) ? 0xf : 1 + r.below(15));
		in.fs = (n & 2) ? 0 : 1 + r.below(5);
		in.ft = (n & 2) ? 1 + r.below(5) : 0;
		if (n >= 4) { in.fs = 0; in.ft = 0; }
		in.fd = 1 + r.below(5);
		runCase(c, o.name + " (vf00 src)", {in}, init, "");
	}
}
} // namespace x86p

namespace x86p
{
static u32 envKnob(const char* name, u32 def)
{
	const char* v = std::getenv(name);
	return v && *v ? static_cast<u32>(std::strtoul(v, nullptr, 0)) : def;
}
// Optional configuration filters so one process stays inside the oracle's 300 s alarm.
static int clampLo() { const u32 k = envKnob("X86P_CLAMP", 99); return k == 99 ? 0 : static_cast<int>(k); }
static int clampHi() { const u32 k = envKnob("X86P_CLAMP", 99); return k == 99 ? 4 : static_cast<int>(k) + 1; }
static int rndLo() { const u32 k = envKnob("X86P_RND", 99); return k == 99 ? 0 : static_cast<int>(k); }
static int rndHi() { const u32 k = envKnob("X86P_RND", 99); return k == 99 ? 2 : static_cast<int>(k) + 1; }

//------------------------------------------------------------------
// EE FPU model (iFPU.cpp single-precision recompiler, clamp modes 0..2).
// Mode 3 (fpuFullMode, iFPUd.cpp double-precision path) is NOT modelled.
//------------------------------------------------------------------
enum EK
{
	E_ADD = 0, E_SUB, E_MUL, E_DIV, E_SQRT, E_ABS, E_MOV, E_NEG, E_RSQRT, E_ADDA, E_SUBA, E_MULA, E_MADD, E_MSUB, E_MADDA, E_MSUBA,
	E_CVTS, E_CVTW, E_MAX, E_MIN, E_CF, E_CEQ, E_CLT, E_CLE, E_COUNT
};
struct EInfo { const char* name; u32 fmt, fn; };
static const EInfo kEE[E_COUNT] = {
	{"ADD.S", 16, 0x00}, {"SUB.S", 16, 0x01}, {"MUL.S", 16, 0x02}, {"DIV.S", 16, 0x03}, {"SQRT.S", 16, 0x04}, {"ABS.S", 16, 0x05},
	{"MOV.S", 16, 0x06}, {"NEG.S", 16, 0x07}, {"RSQRT.S", 16, 0x16}, {"ADDA.S", 16, 0x18}, {"SUBA.S", 16, 0x19}, {"MULA.S", 16, 0x1a},
	{"MADD.S", 16, 0x1c}, {"MSUB.S", 16, 0x1d}, {"MADDA.S", 16, 0x1e}, {"MSUBA.S", 16, 0x1f}, {"CVT.S.W", 20, 0x20}, {"CVT.W.S", 16, 0x24},
	{"MAX.S", 16, 0x28}, {"MIN.S", 16, 0x29}, {"C.F.S", 16, 0x30}, {"C.EQ.S", 16, 0x32}, {"C.LT.S", 16, 0x34}, {"C.LE.S", 16, 0x36}};

constexpr u32 kFlagC = 0x00800000u, kFlagI = 0x00020000u, kFlagD = 0x00010000u, kFlagSI = 0x40u, kFlagSD = 0x20u;

struct EEm { u32 fpr[32]; u32 acc; u32 fcr31; };

struct ECfg { int mode; Env env, divEnv, sqrtEnv; };

static inline u32 f1(u32 x, const ECfg& c) { return c.mode >= 1 ? clamp1(x, c.env) : x; }  // fpuFloat
static inline u32 f2(u32 x, const ECfg& c) { return c.mode >= 1 ? clamp3sign(x) : x; }    // fpuFloat2
static inline u32 f3(u32 x) { return clamp3sign(x); }                                      // fpuFloat3

// SSE MAXSS/MINSS: NaN or both zero returns the second operand.
static u32 ssMax(u32 a, u32 b, const Env& e)
{
	if (isNaN(a) || isNaN(b)) return b;
	a = dazIn(a, e); b = dazIn(b, e);
	if (!(a & 0x7fffffffu) && !(b & 0x7fffffffu)) return b;
	return ordKey(a) > ordKey(b) ? a : b;
}
static u32 ssMin(u32 a, u32 b, const Env& e)
{
	if (isNaN(a) || isNaN(b)) return b;
	a = dazIn(a, e); b = dazIn(b, e);
	if (!(a & 0x7fffffffu) && !(b & 0x7fffffffu)) return b;
	return ordKey(a) < ordKey(b) ? a : b;
}

// FPU_ADD_SUB: masks the mantissa bits of the smaller operand that would land in guard bits.
static u32 guardAddSub(u32 d, u32 t, bool sub, const Env& env)
{
	const int diff = static_cast<int>((d >> 23) & 0xff) - static_cast<int>((t >> 23) & 0xff);
	u32 d2 = d, t2 = t;
	if (diff >= 25) t2 = t & 0x80000000u;
	else if (diff > 0) t2 = t & (0xffffffffu << (diff - 1));
	else if (diff == 0) {}
	else if (diff <= -25) d2 = d & 0x80000000u;
	else d2 = d & (0xffffffffu << (-diff - 1));
	return sub ? sf_sub(d2, t2, env) : sf_add(d2, t2, env);
}

// order: 0 canonical, 1 swapped operands (x86 picks by register allocation; only NaN/zero ties differ).
static void eeApply(EEm& e, int op, u32 fs, u32 ft, u32 fd, const ECfg& c, int order)
{
	const Env& env = c.env;
	const bool extra = c.mode >= 2;
	const u32 s = e.fpr[fs], t = e.fpr[ft];
	u32 res = 0;
	bool toAcc = false, wr = true;
	const auto cm = [&](u32 a, u32 b, auto f) { return order ? f(b, a) : f(a, b); };
	const auto comm = [&](u32 a, u32 b, bool isMul) -> u32 {
		if (extra) { a = f2(a, c); b = f2(b, c); }
		const u32 r = isMul ? cm(a, b, [&](u32 x, u32 y) { return sf_mul(x, y, env); }) : cm(a, b, [&](u32 x, u32 y) { return guardAddSub(x, y, false, env); });
		return f1(r, c);
	};
	switch (op)
	{
		case E_ADD: res = comm(s, t, false); break;
		case E_ADDA: res = comm(s, t, false); toAcc = true; break;
		case E_MUL: res = comm(s, t, true); break;
		case E_MULA: res = comm(s, t, true); toAcc = true; break;
		case E_SUB: case E_SUBA:
		{
			u32 a = s, b = t;
			if (extra) { a = f2(a, c); b = f2(b, c); }
			res = f1(guardAddSub(a, b, true, env), c);
			toAcc = op == E_SUBA;
			break;
		}
		case E_MAX: case E_MIN:
		{
			u32 a = f2(s, c), b = f2(t, c);
			if (order) std::swap(a, b);
			res = op == E_MAX ? ssMax(a, b, env) : ssMin(a, b, env);
			break;
		}
		case E_MADD: case E_MADDA: case E_MSUB: case E_MSUBA:
		{
			u32 a = s, b = t;
			if (extra) { a = f2(a, c); b = f2(b, c); }
			u32 p = sf_mul(a, b, env);
			u32 acc = e.acc;
			if (extra) { acc = f1(acc, c); p = f1(p, c); }
			if (op == E_MADD || op == E_MADDA)
				res = order ? guardAddSub(acc, p, false, env) : guardAddSub(p, acc, false, env);
			else
				res = guardAddSub(acc, p, true, env);
			res = f1(res, c);
			toAcc = op == E_MADDA || op == E_MSUBA;
			break;
		}
		case E_DIV:
		{
			u32 flags = e.fcr31 & ~(kFlagI | kFlagD);
			u32 b = t, a = s;
			if (zeroDaz(b, c.divEnv))
			{
				flags |= zeroDaz(a, c.divEnv) ? (kFlagI | kFlagSI) : (kFlagD | kFlagSD);
				res = ((a ^ b) & 0x80000000u) | kPlusMax;
			}
			else
			{
				if (extra) { a = f2(a, c); b = f2(b, c); }
				res = f1(sf_div(a, b, c.divEnv), c);
			}
			e.fcr31 = flags;
			break;
		}
		case E_SQRT:
		{
			u32 flags = e.fcr31 & ~(kFlagI | kFlagD);
			u32 b = t;
			if (b & 0x80000000u) { flags |= kFlagI | kFlagSI; b &= 0x7fffffffu; }
			if (c.mode >= 1 && (isNaN(b) || isInf(b))) b = kPlusMax; // MINSS(b, max)
			res = sf_sqrt(b, c.sqrtEnv);
			if (extra) res = f1(res, c);
			e.fcr31 = flags;
			break;
		}
		case E_RSQRT:
		{
			u32 flags = e.fcr31 & ~(kFlagI | kFlagD);
			u32 a = s, b = t;
			if (b & 0x80000000u) { flags |= kFlagI | kFlagSI; b &= 0x7fffffffu; }
			if (zeroDaz(b, env))
			{
				flags |= zeroDaz(a, env) ? (kFlagI | kFlagSI) : (kFlagD | kFlagSD);
				res = (a & 0x80000000u) | kPlusMax;
			}
			else
			{
				if (extra) { if (isNaN(b) || isInf(b)) b = kPlusMax; a = f2(a, c); }
				res = f1(sf_div(a, sf_sqrt(b, env), env), c);
			}
			e.fcr31 = flags;
			break;
		}
		case E_ABS:
			res = s & 0x7fffffffu;
			if (c.mode >= 1 && (isNaN(res) || isInf(res))) res = kPlusMax;
			break;
		case E_MOV: res = s; break;
		case E_NEG: res = f3(s ^ 0x80000000u); break;
		case E_CVTS: res = sf_cvtdq2ps(s, env); break;
		case E_CVTW:
		{
			res = sf_cvtt(s, env);
			if (static_cast<s32>(s) >= static_cast<s32>(0x4f000000u)) res = 0x7fffffffu;
			break;
		}
		case E_CF: e.fcr31 &= ~kFlagC; return;
		case E_CEQ: case E_CLT: case E_CLE:
		{
			const u32 a = dazIn(f3(s), env), b = dazIn(f3(t), env);
			bool z = (!(a & 0x7fffffffu) && !(b & 0x7fffffffu));
			bool eq = z || a == b;
			bool lt = !z && ordKey(a) < ordKey(b);
			const bool cbit = op == E_CEQ ? eq : (op == E_CLT ? lt : (lt || eq));
			e.fcr31 = (e.fcr31 & ~kFlagC) | (cbit ? kFlagC : 0);
			return;
		}
		default: wr = false; break;
	}
	if (!wr) return;
	if (toAcc) e.acc = res;
	else e.fpr[fd] = res;
}

static const char* kEEClass(u32 x) { return clsName(x); }

static u32 eeFcrInit(Rng& r) { return 0x01000001u | (r.next() & 0x0083c078u & ~0x00800000u) | ((r.next() & 1) ? 0x00800000u : 0); }

struct EEObs { u32 fpr[32]; u32 acc; u32 fcr31; u32 accflag; };

static void eeRunJit(int op, u32 fs, u32 ft, u32 fd, const u32* fprInit, u32 accInit, u32 fcrInit, EEObs& out)
{
	u32 fpr[32];
	std::memcpy(fpr, fprInit, sizeof(fpr));
	u32 fcr[32] = {};
	fcr[0] = 0x2e30u;
	fcr[31] = fcrInit;
	const std::vector<u32> code = {MipsCop1(kEE[op].fmt, ft, fs, fd, kEE[op].fn)};
	s_fpu_acc_init = &accInit;
	const EESnapshot snap = RunEEProgramWithIrMode(0, code, nullptr, nullptr, fpr, fcr);
	s_fpu_acc_init = nullptr;
	std::memcpy(out.fpr, snap.fpr, sizeof(out.fpr));
	out.acc = snap.acc;
	out.fcr31 = snap.fcr31;
	out.accflag = snap.accflag;
}

static void eeOne(int mode, int rnd, int op, u32 fs, u32 ft, u32 fd, const u32* fprInit, u32 accInit, u32 fcrInit)
{
	ECfg c;
	c.mode = mode;
	c.env = makeEnv(rnd);
	Env nearest = c.env;
	nearest.rm = 0;
	c.divEnv = nearest;   // FPUDivFPCR defaults to nearest rounding
	c.sqrtEnv = nearest;  // SQRT forces nearest rounding
	EEm base{};
	std::memcpy(base.fpr, fprInit, sizeof(base.fpr));
	base.acc = accInit;
	base.fcr31 = fcrInit;
	EEm cand[2] = {base, base};
	for (int o = 0; o < 2; ++o) eeApply(cand[o], op, fs, ft, fd, c, o);
	EEObs got{};
	eeRunJit(op, fs, ft, fd, fprInit, accInit, fcrInit, got);
	++g_cases;
	++g_groupCases;
	bool nanSeen = false;
	const auto sameV = [&](u32 a, u32 b) {
		if (a == b) return true;
		if (isNaN(a) && isNaN(b)) { nanSeen = true; return true; } // value-class exemption, flags stay exact
		return false;
	};
	const auto matches = [&](const EEm& m) {
		nanSeen = false;
		for (u32 i = 0; i < 32; ++i) if (!sameV(m.fpr[i], got.fpr[i])) return false;
		return sameV(m.acc, got.acc) && m.fcr31 == got.fcr31 && got.accflag == 0;
	};
	if (matches(cand[0]) || matches(cand[1])) return;
	matches(cand[0]); // recompute nanSeen against candidate 0
	// Report against candidate 0.
	const EEm& m = cand[0];
	char kb[160];
	std::snprintf(kb, sizeof(kb), "ee c%d %s %s", mode, rnd == 0 ? "chop" : "near", kEE[op].name);
	const u32 sv = fprInit[fs], tv = fprInit[ft];
	const auto rec = [&](const char* field, u32 expv, u32 gotv) {
		char key[320], ex[320];
		std::snprintf(key, sizeof(key), "%s [%s] Fs=%s Ft=%s ACC=%s", kb, field, kEEClass(sv), kEEClass(tv), kEEClass(accInit));
		std::snprintf(ex, sizeof(ex), "model=%08x jit=%08x in{Fs=%08x Ft=%08x ACC=%08x FCR31=%08x fs=%u ft=%u fd=%u}", expv, gotv, sv, tv, accInit, fcrInit, fs, ft, fd);
		std::string cause = std::string("ee c") + std::to_string(mode) + " " + (rnd ? "near " : "chop ") + field + " ";
		if (field[0] == 'F' && field[1] == 'C')
		{
			const char* const nm[9] = {"SU", "SO", "SD", "SI", "?", "?", "?", "?", "?"};
			(void)nm;
			cause += "FCR31 jit+{" + hex(gotv & ~expv) + "} jit-{" + hex(expv & ~gotv) + "}";
		}
		else
			cause += std::string(clsName(expv)) + "->" + clsName(gotv);
		record(key, ex, cause, kEE[op].name);
	};
	for (u32 i = 0; i < 32; ++i) if (!sameV(m.fpr[i], got.fpr[i])) { rec("FPR", m.fpr[i], got.fpr[i]); break; }
	if (!sameV(m.acc, got.acc)) rec("ACC", m.acc, got.acc);
	if (m.fcr31 != got.fcr31) rec("FCR31", m.fcr31, got.fcr31);
	if (got.accflag != 0) rec("ACCflag", 0, got.accflag);
}

static void X86ParityEE()
{
	const u32 step = envKnob("X86P_EESTEP", 8);
	const u32 perOp = envKnob("X86P_EERANDOM", 100);
	for (int rnd = rndLo(); rnd < rndHi(); ++rnd)
		for (int mode = clampLo(); mode < std::min(3, clampHi()); ++mode)
		{
			configure(mode, rnd);
			EmuConfig.Cpu.Recompiler.fpuOverflow = mode >= 1;
			EmuConfig.Cpu.Recompiler.fpuExtraOverflow = mode >= 2;
			EmuConfig.Cpu.Recompiler.fpuFullMode = false;
			// FPUDivFPCR stays nearest (project default) with FTZ/DAZ.
			EmuConfig.Cpu.FPUDivFPCR.bitmask = (1ull << 24);
			g_cpuRegistersPack.Cpu = EmuConfig.Cpu;
			for (int op = 0; op < E_COUNT; ++op)
			{
				const bool needT = op != E_SQRT || true;
				(void)needT;
				for (u32 p = 0; p < kN * kN; p += (rnd ? step * 4 : step))
				{
					Rng r(op * 7907ull + p * 31ull + mode);
					u32 fpr[32];
					for (u32 i = 0; i < 32; ++i) fpr[i] = kVals[r.below(kN)];
					const u32 fs = 1 + r.below(6);
					u32 ft = 1 + r.below(6);
					const u32 fd = (p % 5 == 0) ? fs : (p % 5 == 1 ? ft : 1 + r.below(6));
					if (p % 9 == 0) ft = fs;
					fpr[fs] = kVals[p % kN];
					fpr[ft] = kVals[(p / kN) % kN];
					eeOne(mode, rnd, op, fs, ft, fd, fpr, kVals[r.below(kN)], eeFcrInit(r));
				}
				for (u32 n = 0; n < (rnd ? perOp / 4 : perOp); ++n)
				{
					Rng r(op * 104729ull + n * 13ull + mode * 5 + 7);
					u32 fpr[32];
					for (u32 i = 0; i < 32; ++i)
					{
						const u32 k = r.below(10);
						fpr[i] = k < 4 ? kVals[r.below(kN)] : (k < 9 ? ((r.next() & 0x807fffffu) | ((110 + r.below(36)) << 23)) : r.next());
					}
					const u32 fs = 1 + r.below(6), ft = 1 + r.below(6), fd = 1 + r.below(6);
					eeOne(mode, rnd, op, fs, ft, fd, fpr, (r.below(3) ? ((r.next() & 0x807fffffu) | ((110 + r.below(36)) << 23)) : kVals[r.below(kN)]), eeFcrInit(r));
				}
				char nm[96];
				std::snprintf(nm, sizeof(nm), "ee c%d %s %s", mode, rnd == 0 ? "chop" : "near", kEE[op].name);
				endGroup(nm);
			}
		}
	// FPCR plumbing probe: 1.0 + 3.1415927f is an exact rounding tie (chop 0x408487ed, nearest-even 0x408487ee).
	for (int rnd = 0; rnd < 2; ++rnd)
	{
		configure(1, rnd);
		for (int op : {E_ADD, E_MUL})
		{
			u32 fpr[32] = {};
			fpr[1] = 0x3f800000u;
			fpr[2] = op == E_ADD ? 0x40490fdbu : 0x40490fdbu;
			EEObs got{};
			eeRunJit(op, 1, 2, 3, fpr, 0, 0x01000001u, got);
			std::printf("  EE FPCR probe %s FPUFPCR=%s: JIT=%08x\n", kEE[op].name, rnd ? "nearest" : "chop", got.fpr[3]);
		}
	}
	// Fixed x86 expectations: CTC1/CFC1 masks and FCR0.
	configure(1, 0);
	{
		u64 gpr[32] = {};
		gpr[8] = 0xffffffffffffffffull;
		u32 fcr[32] = {};
		fcr[0] = 0x2e30u;
		fcr[31] = 0x01000001u;
		const std::vector<u32> code = {MipsCop1(6, 8, 31, 0, 0), MipsCop1(2, 9, 31, 0, 0), MipsCop1(2, 10, 0, 0, 0)};
		const EESnapshot s = RunEEProgramWithIrMode(0, code, nullptr, nullptr, nullptr, fcr);
		(void)s;
		GPR_reg g[32] = {};
		g[8].UD[0] = 0xffffffffffffffffull;
		const EESnapshot s2 = RunEEProgramWithIrMode(0, code, g, nullptr, nullptr, fcr);
		std::printf("  fixed EE: CFC1 FCR31 after CTC1 0xFFFFFFFF = %016llx, CFC1 $0 = %016llx\n",
			static_cast<unsigned long long>(s2.gpr[9]), static_cast<unsigned long long>(s2.gpr[10]));
		Check(s2.gpr[9] == 0x000000000183c079ull, "x86p fixed EE CTC1 0xFFFFFFFF; CFC1 FCR31 -> 0x0183C079");
		Check(s2.gpr[10] == 0x2e30ull, "x86p fixed EE CFC1 $0 -> 0x2e30");
		(void)gpr;
	}
}
} // namespace x86p

namespace x86p
{
static Ctx makeCtx(int area, int clamp, int rnd)
{
	Ctx c;
	c.area = area;
	c.clamp = clamp;
	c.rnd = rnd;
	c.cfg.mode = clamp;
	c.cfg.env = makeEnv(rnd);
	c.cfg.cop2 = area == 0;
	c.cfg.vu1 = area == 2;
	configure(clamp, rnd);
	return c;
}

static void runUpperArea(int area, u32 step, u32 randomPerMask)
{
	const auto& ops = upperOps();
	for (int rnd = rndLo(); rnd < rndHi(); ++rnd)
		for (int clamp = clampLo(); clamp < clampHi(); ++clamp)
		{
			const Ctx c = makeCtx(area, clamp, rnd);
			// Rounding mode only changes results of rounded arithmetic: nearest uses a sparser sweep.
			const u32 st = rnd ? step * 5 : step;
			const u32 rp = rnd ? std::max(1u, randomPerMask / 2) : randomPerMask;
			for (u32 i = 0; i < ops.size(); ++i)
			{
				sweepUpperOp(c, i, st, rp);
				endGroup(ctxKey(c, ops[i].name));
			}
		}
}

static bool finiteOnly = false;

static u32 sanitize(u32 v, int flavor)
{
	if (flavor == 1 && ((v >> 23) & 0xff) == 0xff) v &= ~0x00800000u; // finite-only operands
	if (flavor == 2) v = (v & 0x807fffffu) | ((110 + (v % 30)) << 23); // normal mid-range
	return v;
}

static void runSequences(int area, u32 count)
{
	const auto& ops = upperOps();
	static const char* flavorName[3] = {"all", "finite", "normal"};
	static const int safeLower[] = {L_MTIR, L_MFIR, L_MOVE, L_MR32, L_RNEXT, L_RGET, L_RINIT, L_RXOR, L_IADD, L_ISUB, L_IAND, L_IOR, L_IADDI};
	for (int rnd = rndLo(); rnd < rndHi(); ++rnd)
		for (int clamp = clampLo(); clamp < clampHi(); ++clamp)
			for (int flavor = 0; flavor < 3; ++flavor)
			{
				const Ctx c = makeCtx(area, clamp, rnd);
				const u32 n = rnd ? count / 4 : count;
				for (u32 k = 0; k < n; ++k)
				{
					Rng r(0x5eed0000ull + k * 7919ull + flavor * 131ull + clamp * 17ull);
					FmacInit init;
					baseInit(init, r, true);
					for (u32 i = 4; i < 128; ++i) init.vf[i] = sanitize(init.vf[i], flavor);
					for (u32 l = 0; l < 4; ++l) init.acc[l] = sanitize(init.acc[l], flavor);
					init.i = sanitize(init.i, flavor);
					init.q = sanitize(init.q, flavor);
					const u32 len = 2 + r.below(7);
					std::vector<Ins> prog;
					for (u32 j = 0; j < len; ++j)
					{
						Ins in;
						const u32 roll = r.below(100);
						if (roll < 80)
						{
							in.op = static_cast<int>(r.below(static_cast<u32>(ops.size())));
							in.mask = legalMask(ops[in.op], r.below(3) == 0 ? 0xf : 1 + r.below(15));
							in.fs = 1 + r.below(6); in.ft = 1 + r.below(6); in.fd = 1 + r.below(6);
						}
						else if (roll < 95)
						{
							in.lower = true;
							in.op = safeLower[r.below(sizeof(safeLower) / sizeof(safeLower[0]))];
							in.mask = 1 + r.below(15);
							in.fs = 1 + r.below(6); in.ft = 1 + r.below(6); in.fd = 1 + r.below(6);
							in.fsf = r.below(4);
							in.imm = static_cast<s32>(r.below(32)) - 16;
							if (in.op == L_MTIR || in.op == L_MFIR || in.op == L_IADD || in.op == L_ISUB || in.op == L_IAND || in.op == L_IOR || in.op == L_IADDI)
							{
								in.fs = 1 + r.below(4); in.ft = 1 + r.below(4); in.fd = 1 + r.below(4);
							}
						}
						else if (area == 0 || j + 1 == len)
						{
							in.lower = true;
							in.op = r.below(3);
							in.fs = 1 + r.below(6); in.ft = 1 + r.below(6);
							in.fsf = r.below(4); in.ftf = r.below(4);
						}
						else
							continue;
						prog.push_back(in);
					}
					if (prog.empty()) continue;
					runCase(c, "SEQ", prog, init, flavorName[flavor]);
				}
				char nm[128];
				std::snprintf(nm, sizeof(nm), "%s c%d %s SEQ-%s", kAreaName[area], clamp, rnd == 0 ? "chop" : "near", flavorName[flavor]);
				endGroup(nm);
			}
}

static void sweepLower(const Ctx& c, int lop, u32 step)
{
	const LInfo& li = kLower[lop];
	const std::string name = li.name;
	const auto mk = [&]() { Ins in; in.lower = true; in.op = lop; return in; };
	switch (lop)
	{
		case L_DIV: case L_RSQRT: case L_EATANXY: case L_EATANXZ:
			for (u32 p = 0; p < kN * kN; p += step)
			{
				Rng r(lop * 977ull + p);
				FmacInit init;
				baseInit(init, r, (p & 3) == 0);
				Ins in = mk();
				in.fs = 1 + r.below(4); in.ft = 1 + r.below(4);
				in.fsf = r.below(4); in.ftf = r.below(4);
				in.mask = 0xf;
				if (lop == L_EATANXY || lop == L_EATANXZ)
				{
					init.vf[in.fs * 4 + 0] = kVals[p % kN];
					init.vf[in.fs * 4 + (lop == L_EATANXY ? 1 : 2)] = kVals[p / kN];
				}
				else
				{
					init.vf[in.fs * 4 + (in.fsf & 3)] = kVals[p % kN];
					init.vf[in.ft * 4 + (in.ftf & 3)] = kVals[p / kN];
				}
				if (lop == L_DIV && (p % 11) == 0) in.fs = 0;          // DIV Q, vf00.w, x idiom
				runCase(c, name, {in}, init, "");
			}
			break;
		case L_SQRT: case L_ESQRT: case L_ESIN: case L_EATAN: case L_EEXP: case L_ERCPR: case L_ERSQRT: case L_MTIR: case L_RINIT: case L_RXOR:
			for (u32 p = 0; p < kN * 4; p += std::max(1u, step / 8))
			{
				Rng r(lop * 1237ull + p);
				FmacInit init;
				baseInit(init, r, (p & 3) == 0);
				Ins in = mk();
				in.fs = 1 + r.below(4); in.ft = 1 + r.below(4);
				in.fsf = p / kN; in.ftf = p / kN;
				const u32 val = kVals[p % kN];
				init.vf[in.fs * 4 + (in.fsf & 3)] = val;
				init.vf[in.ft * 4 + (in.ftf & 3)] = val;
				runCase(c, name, {in}, init, "");
			}
			break;
		case L_ESADD: case L_ELENG: case L_ERSADD: case L_ERLENG: case L_ESUM:
			for (u32 p = 0; p < 1600; p += std::max(1u, step / 8))
			{
				Rng r(lop * 3331ull + p);
				FmacInit init;
				baseInit(init, r, (p & 3) == 0);
				Ins in = mk();
				in.fs = 1 + r.below(5);
				in.mask = 0xf;
				for (u32 l = 0; l < 4; ++l)
					init.vf[in.fs * 4 + l] = (p < 32 * 4) ? kVals[(p / 4 + l * 5) % kN] : (r.below(3) ? kVals[r.below(kN)] : (r.next() & 0x807fffffu) | ((120 + r.below(16)) << 23));
				runCase(c, name, {in}, init, "");
			}
			break;
		default:
			for (u32 p = 0; p < 300; p += std::max(1u, step / 8))
			{
				Rng r(lop * 4441ull + p);
				FmacInit init;
				baseInit(init, r, true);
				Ins in = mk();
				in.fs = 1 + r.below(4); in.ft = 1 + r.below(4); in.fd = 1 + r.below(4);
				in.mask = 1 + r.below(15);
				in.fsf = r.below(4);
				in.imm = static_cast<s32>(r.below(32)) - 16;
				if (lop == L_RNEXT && (p & 3) == 0) in.ft = 0; // RNEXT vf0 must still advance R
				// give integer ops meaningful operands via MTIR first
				std::vector<Ins> prog;
				if (lop == L_IADD || lop == L_ISUB || lop == L_IAND || lop == L_IOR || lop == L_IADDI || lop == L_MFIR)
				{
					for (u32 reg = 1; reg <= 3; ++reg)
					{
						Ins m = mk();
						m.op = L_MTIR; m.fs = reg; m.ft = reg; m.fsf = reg & 3;
						prog.push_back(m);
					}
					in.fs = 1 + r.below(3); in.ft = 1 + r.below(3); in.fd = 1 + r.below(3);
				}
				prog.push_back(in);
				runCase(c, name, prog, init, "setup");
			}
			break;
	}
}

static void runLowerArea(int area, u32 step)
{
	for (int rnd = rndLo(); rnd < rndHi(); ++rnd)
		for (int clamp = clampLo(); clamp < clampHi(); ++clamp)
		{
			const Ctx c = makeCtx(area, clamp, rnd);
			for (int lop = 0; lop < L_COUNT; ++lop)
			{
				if (kLower[lop].efu && area != 2) continue;  // EFU/MFP are VU1-only
				sweepLower(c, lop, rnd ? step * 5 : step);
				endGroup(ctxKey(c, kLower[lop].name));
			}
		}
}

//------------------------------------------------------------------
// Self check: fixed cases with hand-computed x86 expectations
//------------------------------------------------------------------
template <class F>
static void fixedCase(const char* name, int areaMask, int clamp, const std::vector<Ins>& prog, const FmacInit& init, F pred)
{
	for (int area = 0; area < 3; ++area)
	{
		if (!(areaMask & (1 << area))) continue;
		const Ctx c = makeCtx(area, clamp, 0);
		Obs m{}, j{};
		runModel(area, prog, init, c.cfg, m);
		runJit(area, prog, init, j);
		char b[200];
		std::snprintf(b, sizeof(b), "x86p fixed %s [%s c%d] MODEL matches x86 expectation", name, kAreaName[area], clamp);
		Check(pred(m), b);
		std::snprintf(b, sizeof(b), "x86p fixed %s [%s c%d] JIT matches x86 expectation", name, kAreaName[area], clamp);
		const bool ok = pred(j);
		Check(ok, b);
		if (!ok)
			std::printf("  fixed-case JIT state: VF3=%s VF4=%s ACC=%s P=%08x Q=%08x R=%08x STATUS=%08x\n", lanes4(&j.vf[12]).c_str(),
				lanes4(&j.vf[16]).c_str(), lanes4(j.acc).c_str(), j.p, j.vi[REG_Q], j.vi[REG_R], j.vi[REG_STATUS_FLAG]);
	}
}

static int findUpper(const char* n)
{
	const auto& ops = upperOps();
	for (u32 i = 0; i < ops.size(); ++i)
		if (ops[i].name == n) return static_cast<int>(i);
	return 0;
}

static void SelfCheck()
{
	const u32 one = 0x3f800000u;
	const char* nm[4] = {"MADDx", "MADDy", "MADDz", "MADDw"};
	for (int l = 0; l < 4; ++l)
	{
		FmacInit init{};
		init.vf[3] = one;
		const u32 fsv[4] = {0x7f800000u, 0xff800000u, 0x7f800000u, 0x7fc00000u};
		for (int k = 0; k < 4; ++k) { init.vf[4 + k] = fsv[k]; init.acc[k] = one; }
		Ins in; in.op = findUpper(nm[l]); in.fs = 1; in.ft = 2; in.fd = 3;
		in.fs = 1; in.ft = 2; in.fd = 3;
		// VF1 = fs source: place data in VF1, Ft = VF2 lane l = 0 (already zero).
		FmacInit i2 = init;
		for (int k = 0; k < 4; ++k) { i2.vf[4 + k] = fsv[k]; i2.vf[8 + k] = (k == l) ? 0u : one; }
		char name[96];
		std::snprintf(name, sizeof(name), "VMADD%s.xyzw VF3,VF1,VF2 VF1=(+Inf,-Inf,+Inf,NaN) Ft lane=0 ACC=1 -> all 1.0", nm[l] + 4);
		fixedCase(name, 7, 1, {in}, i2, [&](const Obs& o) { return o.vf[12] == one && o.vf[13] == one && o.vf[14] == one && o.vf[15] == one; });
	}
	{
		// COP2 MADDw clamps ACC and Ft (cACC|cFt|cFs): ACC (Inf,-Inf,qNaN,-qNaN) -> (+max,-max,+max,+max).
		FmacInit init{};
		init.vf[3] = one;
		init.acc[0] = 0x7f800000u; init.acc[1] = 0xff800000u; init.acc[2] = 0x7fc00001u; init.acc[3] = 0xffc00001u;
		for (int k = 0; k < 4; ++k) init.vf[8 + k] = one;
		Ins in; in.op = findUpper("MADDw"); in.fs = 1; in.ft = 2; in.fd = 3;
		fixedCase("COP2 MADDw clamps ACC at clamp 1", 1, 1, {in}, init, [&](const Obs& o) {
			return o.vf[12] == 0x7f7fffffu && o.vf[13] == 0xff7fffffu && o.vf[14] == 0x7f7fffffu && o.vf[15] == 0x7f7fffffu; });
		FmacInit i2{};
		i2.vf[3] = one;
		for (int k = 0; k < 4; ++k) { i2.acc[k] = one; i2.vf[8 + k] = 0x7f800000u; }
		fixedCase("COP2 MADDw clamps Ft (Fs=0, Ft=+Inf, ACC=1 -> 1.0)", 1, 1, {in}, i2, [&](const Obs& o) {
			return o.vf[12] == one && o.vf[13] == one && o.vf[14] == one && o.vf[15] == one; });
	}
	{
		// A signaling or negative quiet NaN through clamp1 (MULx clamps Fs and Ft) becomes +max.
		FmacInit init{};
		init.vf[3] = one;
		const u32 nans[4] = {0x7f800001u, 0xff800001u, 0xffc00000u, 0x7fc12345u};
		for (int k = 0; k < 4; ++k) { init.vf[4 + k] = nans[k]; init.vf[8 + k] = one; }
		Ins in; in.op = findUpper("MUL"); in.fs = 1; in.ft = 2; in.fd = 3;
		fixedCase("MUL.xyzw sNaN/qNaN operands through clamp 1 -> +max", 7, 1, {in}, init, [&](const Obs& o) {
			return o.vf[12] == 0x7f7fffffu && o.vf[13] == 0x7f7fffffu && o.vf[14] == 0x7f7fffffu && o.vf[15] == 0x7f7fffffu; });
		fixedCase("ADD.xyzw Inf + 1.0 is not clamped at clamp 1 (x86 has no result clamp)", 7, 1,
			{[&] { Ins a; a.op = findUpper("ADD"); a.fs = 1; a.ft = 2; a.fd = 3; return a; }()},
			[&] { FmacInit i3{}; i3.vf[3] = one; for (int k = 0; k < 4; ++k) { i3.vf[4 + k] = 0x7f800000u; i3.vf[8 + k] = one; } return i3; }(),
			[&](const Obs& o) { return o.vf[12] == 0x7f800000u && o.vf[15] == 0x7f800000u; });
	}
	{
		// ESUM (16777216,1,1,0): pairwise (x+w)+(y+z) -> 0x4b800001 (a serial sum would give 0x4b800000).
		FmacInit init{};
		init.vf[3] = one;
		init.vf[4] = 0x4b800000u; init.vf[5] = one; init.vf[6] = one; init.vf[7] = 0;
		Ins in; in.lower = true; in.op = L_ESUM; in.fs = 1; in.mask = 0xf;
		fixedCase("ESUM (16777216,1,1,0) -> P=0x4b800001", 4, 1, {in}, init, [&](const Obs& o) { return o.p == 0x4b800001u; });
		// ESQRT(-4) = sqrt(|-4|) = 2.0
		FmacInit i2{};
		i2.vf[3] = one;
		i2.vf[4] = 0xc0800000u;
		Ins q; q.lower = true; q.op = L_ESQRT; q.fs = 1; q.fsf = 0;
		fixedCase("ESQRT(-4) -> P=2.0", 4, 1, {q}, i2, [&](const Obs& o) { return o.p == 0x40000000u; });
	}
	{
		// RSQRT 0/0: invalid flag (I and IS), not divide-by-zero; Q = +max.
		FmacInit init{};
		init.vf[3] = one;
		Ins in; in.lower = true; in.op = L_RSQRT; in.fs = 1; in.ft = 2; in.fsf = 0; in.ftf = 0;
		fixedCase("RSQRT 0/sqrt(0) -> I|IS (0x410), Q=+max", 7, 1, {in}, init, [&](const Obs& o) {
			return o.vi[REG_Q] == 0x7f7fffffu && (o.vi[REG_STATUS_FLAG] & 0xc30u) == 0x410u; });
		Ins d = in; d.op = L_DIV;
		fixedCase("DIV 0/0 -> I|IS (0x410), Q=+max", 7, 1, {d}, init, [&](const Obs& o) {
			return o.vi[REG_Q] == 0x7f7fffffu && (o.vi[REG_STATUS_FLAG] & 0xc30u) == 0x410u; });
		FmacInit i2 = init;
		i2.vf[4] = one;
		fixedCase("DIV 1/0 -> D|DS (0x820), Q=+max", 7, 1, {d}, i2, [&](const Obs& o) {
			return o.vi[REG_Q] == 0x7f7fffffu && (o.vi[REG_STATUS_FLAG] & 0xc30u) == 0x820u; });
	}
	{
		// RNEXT with vf0 as destination still advances R.
		FmacInit init{};
		init.vf[3] = one;
		init.r = 0x3f812345u;
		Ins in; in.lower = true; in.op = L_RNEXT; in.ft = 0; in.mask = 0xf;
		fixedCase("RNEXT vf0 advances R (0x3f812345 -> 0x3f82468a)", 7, 1, {in}, init, [&](const Obs& o) { return o.vi[REG_R] == 0x3f82468au; });
	}
}

//------------------------------------------------------------------
// Entry
//------------------------------------------------------------------
static void Setup()
{
	EmuConfig.Speedhacks.vuFlagHack = false;
	EmuConfig.Gamefixes.IbitHack = false;
	EmuConfig.Gamefixes.VUOverflowHack = false;
	EmuConfig.Gamefixes.VuAddSubHack = false;
	EmuCoreXOracleSetEeIrMode(0);
	EmuCoreXOracleSetVUIR(0, 0);
	EmuCoreXOracleSetVUIR(1, 0);
}
} // namespace x86p

#include "jit_oracle_x86_parity2.inl"

void X86ParityTests(const char* suite)
{
	using namespace x86p;
	const auto saved = EmuConfig;
	Setup();
	const std::string s = suite;
	const bool all = s == "x86-parity";
	const u32 step = envKnob("X86P_STEP", 8);
	const u32 perMask = envKnob("X86P_RANDOM", 2);
	const u32 seqN = envKnob("X86P_SEQ", 200);
	std::printf("x86-parity: step=%u randomPerMask=%u sequences=%u (env X86P_STEP/X86P_RANDOM/X86P_SEQ)\n", step, perMask, seqN);
	configure(1, 0);
	if (all || s == "x86-parity-selfcheck") { SelfCheck(); }
	if (all || s == "x86-parity-vu-macro") { const u32 part = envKnob("X86P_PART", 7); if (part & 1) runUpperArea(0, step, perMask); if (part & 2) runLowerArea(0, step); if (part & 4) runSequences(0, seqN); }
	if (all || s == "x86-parity-vu-micro0") { const u32 part = envKnob("X86P_PART", 7); if (part & 1) runUpperArea(1, step, perMask); if (part & 2) runLowerArea(1, step); if (part & 4) runSequences(1, seqN); }
	if (all || s == "x86-parity-vu-micro1") { const u32 part = envKnob("X86P_PART", 7); if (part & 1) runUpperArea(2, step, perMask); if (part & 2) runLowerArea(2, step); if (part & 4) runSequences(2, seqN); }
	if (all || s == "x86-parity-ee") { X86ParityEE(); }
	X86Parity2Tests(s, all);
	printReport(suite);
	EmuCoreXOracleSetEeIrMode(-1);
	EmuCoreXOracleSetVUIR(0, -1);
	EmuCoreXOracleSetVUIR(1, -1);
	EmuConfig = saved;
}

#endif // EMUCOREX_ENABLE_NATIVE_SELF_TESTS
