// SPDX-FileCopyrightText: 2026 EmuCoreX Team
// SPDX-License-Identifier: GPL-3.0+
// Suite ee-mem-matrix: every EE load/store (LB..LQ, SB..SQ, LWL/LWR/LDL/LDR/SWL/SWR/SDL/SDR, LWC1/SWC1, LQC2/SQC2) x address class
// (RAM, scratchpad, TLB-mapped, RAM page boundary, hardware-register MMIO, unmapped) x alignment 0..15 x base register mode (runtime /
// constant folded) x fast-path state (cold, warm = after the first run has compiled and backpatched, TLB remapped after compile,
// store into a page that contains a compiled block).
// Reference: the EE interpreter for every access it models (aligned strict accesses, LWL/LWR/..., LQ/SQ silent alignment); a byte-level
// model for strict accesses at unaligned addresses (the interpreter cancels those, the recompiler - like upstream x86 - performs the access)
// and for unmapped addresses (the interpreter raises a TLB exception, the recompiler reads zero / discards writes: upstream compatibility).

#if defined(EMUCOREX_ENABLE_NATIVE_SELF_TESTS) && !defined(NDEBUG)

namespace gapee
{
struct MemOp { const char* name; u32 op; u32 width; u32 kind; bool load; }; // kind: 0 strict, 1 merge (any address), 2 quad (silent align), 3 coprocessor (strict)
static const MemOp kMemOps[] = {
	{"LB", 0x20, 1, 0, true}, {"LBU", 0x24, 1, 0, true}, {"LH", 0x21, 2, 0, true}, {"LHU", 0x25, 2, 0, true}, {"LW", 0x23, 4, 0, true},
	{"LWU", 0x27, 4, 0, true}, {"LD", 0x37, 8, 0, true}, {"LQ", 0x1e, 16, 2, true},
	{"SB", 0x28, 1, 0, false}, {"SH", 0x29, 2, 0, false}, {"SW", 0x2b, 4, 0, false}, {"SD", 0x3f, 8, 0, false}, {"SQ", 0x1f, 16, 2, false},
	{"LWL", 0x22, 4, 1, true}, {"LWR", 0x26, 4, 1, true}, {"LDL", 0x1a, 8, 1, true}, {"LDR", 0x1b, 8, 1, true},
	{"SWL", 0x2a, 4, 1, false}, {"SWR", 0x2e, 4, 1, false}, {"SDL", 0x2c, 8, 1, false}, {"SDR", 0x2d, 8, 1, false},
	{"LWC1", 0x31, 4, 3, true}, {"SWC1", 0x39, 4, 3, false}, {"LQC2", 0x36, 16, 3, true}, {"SQC2", 0x3e, 16, 3, false},
};
constexpr u32 kMemOpCount = sizeof(kMemOps) / sizeof(kMemOps[0]);

enum MemClass { MC_RAM, MC_SPR, MC_TLB, MC_PAGEB, MC_HW, MC_UNMAPPED, MC_COUNT };
static const char* const kMemClassName[MC_COUNT] = {"ram", "scratchpad", "tlb-mapped", "page-boundary", "hw-mmio", "unmapped"};

constexpr u32 SPR_WIN = 0x70000200u;       // 256-byte window inside the scratchpad
constexpr u32 TLB_VA = 0x41000000u;        // virtual page pair; maps to TLB_PA0 / TLB_PA1
constexpr u32 TLB_PA0 = 0x00300000u, TLB_PA1 = 0x00320000u, TLB_PA2 = 0x00340000u, TLB_PA3 = 0x00360000u;
constexpr u32 TLB_WIN = TLB_VA + 0x100u;
constexpr u32 PAGEB_WIN = EE_TEST_SCRATCH + 0xf80u; // 0x101f80: straddles the 0x102000 page boundary
constexpr u32 HW_BASE = 0x1000f000u;
constexpr u32 UNMAPPED_WIN = 0x50000100u;

static inline u8 memPattern(u32 i, u32 salt) { return static_cast<u8>(i * 29u + 3u + salt * 17u); }

//------------------------------------------------------------------
// TLB helpers (use the real WriteTLB / UnmapTLB path, then restore the COP0 registers it clobbers)
//------------------------------------------------------------------
static void tlbClearAll()
{
	for (int i = 0; i < 48; ++i)
		UnmapTLB(tlb[i], i);
	std::memset(tlb, 0, sizeof(tlb));
	cachedTlbs = {};
}

static void tlbInstall(u32 idx, u32 va, u32 pa0, u32 pa1, u32 flags0 = 0x1f, u32 flags1 = 0x1f, u32 pageMask = 0)
{
	const u32 s0 = cpuRegs.CP0.n.PageMask, s1 = cpuRegs.CP0.n.EntryHi, s2 = cpuRegs.CP0.n.EntryLo0, s3 = cpuRegs.CP0.n.EntryLo1;
	cpuRegs.CP0.n.PageMask = pageMask;
	cpuRegs.CP0.n.EntryHi = va;
	cpuRegs.CP0.n.EntryLo0 = ((pa0 >> 12) << 6) | flags0;
	cpuRegs.CP0.n.EntryLo1 = ((pa1 >> 12) << 6) | flags1;
	UnmapTLB(tlb[idx], static_cast<int>(idx));
	WriteTLB(static_cast<int>(idx));
	cpuRegs.CP0.n.PageMask = s0;
	cpuRegs.CP0.n.EntryHi = s1;
	cpuRegs.CP0.n.EntryLo0 = s2;
	cpuRegs.CP0.n.EntryLo1 = s3;
}

static void fillMem(u32 base, u32 len, u32 salt)
{
	for (u32 i = 0; i < len; ++i)
		memWrite8(base + i, memPattern(i, salt));
}

//------------------------------------------------------------------
// Class windows
//------------------------------------------------------------------
struct ClassInfo { u32 win; u32 center; };
static ClassInfo classInfo(u32 cls)
{
	switch (cls)
	{
		case MC_RAM: return {EE_TEST_SCRATCH, EE_TEST_SCRATCH + 0x80};
		case MC_SPR: return {SPR_WIN, SPR_WIN + 0x80};
		case MC_TLB: return {TLB_WIN, TLB_WIN + 0x80};
		case MC_PAGEB: return {PAGEB_WIN, 0x102000u - 8};
		case MC_HW: return {HW_BASE, HW_BASE + 8};
		default: return {UNMAPPED_WIN, UNMAPPED_WIN + 0x80};
	}
}

struct MemCtx
{
	u32 cls;
	u32 fpr1;
	u32 vf1[4];
	u32 tlbSalt;
};

static void memPrep(void* p)
{
	MemCtx& c = *static_cast<MemCtx*>(p);
	tlbClearAll();
	switch (c.cls)
	{
		case MC_SPR: tlbInstall(2, 0x70000000u, 0, 0, 0x8000001fu, 0x1fu); fillMem(SPR_WIN, 256, 1); break; // the scratchpad is only mapped through an S-bit TLB entry
		case MC_TLB:
			tlbInstall(0, TLB_VA, TLB_PA0, TLB_PA1);
			fillMem(TLB_PA0, 0x1000, 2);
			fillMem(TLB_PA1, 0x1000, 3);
			break;
		case MC_PAGEB: fillMem(PAGEB_WIN, 256, 4); break;
		case MC_HW: for (u32 i = 0; i < 0x40; ++i) eeHw[0xf000 + i] = static_cast<u8>(0x21 + i * 7); break;
		default: break;
	}
	fpuRegs.fpr[1].UL = c.fpr1;
	for (u32 l = 0; l < 4; ++l) VU0.VF[1].UL[l] = c.vf1[l];
}

//------------------------------------------------------------------
// Byte model
//------------------------------------------------------------------
struct MemModel
{
	std::vector<u8> mem;
	u32 base = 0;
	u8 rd8(u32 a) const { const u32 o = a - base; return o < mem.size() ? mem[o] : 0; }
	void wr8(u32 a, u8 v) { const u32 o = a - base; if (o < mem.size()) mem[o] = v; }
	u64 rdN(u32 a, u32 n) const { u64 v = 0; for (u32 i = 0; i < n; ++i) v |= static_cast<u64>(rd8(a + i)) << (8 * i); return v; }
	void wrN(u32 a, u32 n, u64 v) { for (u32 i = 0; i < n; ++i) wr8(a + i, static_cast<u8>(v >> (8 * i))); }
};

static const u32 LWL_MASK[4] = {0xffffff, 0x0000ffff, 0x000000ff, 0x00000000};
static const u32 LWR_MASK[4] = {0x000000, 0xff000000, 0xffff0000, 0xffffff00};
static const u8 LWL_SHIFT[4] = {24, 16, 8, 0};
static const u8 LWR_SHIFT[4] = {0, 8, 16, 24};
static const u32 SWL_MASK[4] = {0xffffff00, 0xffff0000, 0xff000000, 0x00000000};
static const u32 SWR_MASK[4] = {0x00000000, 0x000000ff, 0x0000ffff, 0x00ffffff};
static const u64 LDL_MASK[8] = {0x00ffffffffffffffull, 0x0000ffffffffffffull, 0x000000ffffffffffull, 0x00000000ffffffffull, 0x0000000000ffffffull, 0x000000000000ffffull, 0x00000000000000ffull, 0};
static const u64 LDR_MASK[8] = {0, 0xff00000000000000ull, 0xffff000000000000ull, 0xffffff0000000000ull, 0xffffffff00000000ull, 0xffffffffff000000ull, 0xffffffffffff0000ull, 0xffffffffffffff00ull};
static const u8 LDR_SHIFT[8] = {0, 8, 16, 24, 32, 40, 48, 56};
static const u8 LDL_SHIFT[8] = {56, 48, 40, 32, 24, 16, 8, 0};
static const u64 SDL_MASK[8] = {0xffffffffffffff00ull, 0xffffffffffff0000ull, 0xffffffffff000000ull, 0xffffffff00000000ull, 0xffffff0000000000ull, 0xffff000000000000ull, 0xff00000000000000ull, 0};
static const u64 SDR_MASK[8] = {0, 0xffull, 0xffffull, 0xffffffull, 0xffffffffull, 0xffffffffffull, 0xffffffffffffull, 0xffffffffffffffull};
static const u8 SDL_SHIFT[8] = {56, 48, 40, 32, 24, 16, 8, 0};
static const u8 SDR_SHIFT[8] = {0, 8, 16, 24, 32, 40, 48, 56};

// rt: in/out register (128 bit). Strict accesses at unaligned addresses perform the access byte-wise (little endian).
static void modelExec(const MemOp& m, u32 ea, GPR_reg& rt, GPR_reg& fpOrVf, MemModel& mm)
{
	const u32 s = ea & 3, d = ea & 7;
	switch (m.op)
	{
		case 0x20: rt.UD[0] = static_cast<u64>(static_cast<s64>(static_cast<s8>(mm.rdN(ea, 1)))); break;
		case 0x24: rt.UD[0] = mm.rdN(ea, 1); break;
		case 0x21: rt.UD[0] = static_cast<u64>(static_cast<s64>(static_cast<s16>(mm.rdN(ea, 2)))); break;
		case 0x25: rt.UD[0] = mm.rdN(ea, 2); break;
		case 0x23: rt.UD[0] = static_cast<u64>(static_cast<s64>(static_cast<s32>(mm.rdN(ea, 4)))); break;
		case 0x27: rt.UD[0] = mm.rdN(ea, 4); break;
		case 0x37: rt.UD[0] = mm.rdN(ea, 8); break;
		case 0x1e: { const u32 a = ea & ~15u; rt.UD[0] = mm.rdN(a, 8); rt.UD[1] = mm.rdN(a + 8, 8); break; }
		case 0x28: mm.wrN(ea, 1, rt.UD[0]); break;
		case 0x29: mm.wrN(ea, 2, rt.UD[0]); break;
		case 0x2b: mm.wrN(ea, 4, rt.UD[0]); break;
		case 0x3f: mm.wrN(ea, 8, rt.UD[0]); break;
		case 0x1f: { const u32 a = ea & ~15u; mm.wrN(a, 8, rt.UD[0]); mm.wrN(a + 8, 8, rt.UD[1]); break; }
		case 0x22: { const u32 w = static_cast<u32>(mm.rdN(ea & ~3u, 4)); rt.UD[0] = static_cast<u64>(static_cast<s64>(static_cast<s32>((rt.UL[0] & LWL_MASK[s]) | (w << LWL_SHIFT[s])))); break; }
		case 0x26:
		{
			const u32 w = static_cast<u32>(mm.rdN(ea & ~3u, 4));
			const u32 v = (rt.UL[0] & LWR_MASK[s]) | (w >> LWR_SHIFT[s]);
			if (s == 0) rt.UD[0] = static_cast<u64>(static_cast<s64>(static_cast<s32>(v))); else rt.UL[0] = v;
			break;
		}
		case 0x1a: rt.UD[0] = (rt.UD[0] & LDL_MASK[d]) | (mm.rdN(ea & ~7u, 8) << LDL_SHIFT[d]); break;
		case 0x1b: rt.UD[0] = (rt.UD[0] & LDR_MASK[d]) | (mm.rdN(ea & ~7u, 8) >> LDR_SHIFT[d]); break;
		case 0x2a: mm.wrN(ea & ~3u, 4, (rt.UL[0] >> LWL_SHIFT[s]) | (static_cast<u32>(mm.rdN(ea & ~3u, 4)) & SWL_MASK[s])); break;
		case 0x2e: mm.wrN(ea & ~3u, 4, (rt.UL[0] << LWR_SHIFT[s]) | (static_cast<u32>(mm.rdN(ea & ~3u, 4)) & SWR_MASK[s])); break;
		case 0x2c: mm.wrN(ea & ~7u, 8, (rt.UD[0] >> SDL_SHIFT[d]) | (mm.rdN(ea & ~7u, 8) & SDL_MASK[d])); break;
		case 0x2d: mm.wrN(ea & ~7u, 8, (rt.UD[0] << SDR_SHIFT[d]) | (mm.rdN(ea & ~7u, 8) & SDR_MASK[d])); break;
		case 0x31: fpOrVf.UL[0] = static_cast<u32>(mm.rdN(ea, 4)); break;   // LWC1 -> f1
		case 0x39: mm.wrN(ea, 4, fpOrVf.UL[0]); break;                       // SWC1 f1
		case 0x36: for (u32 l = 0; l < 4; ++l) fpOrVf.UL[l] = static_cast<u32>(mm.rdN(ea + 4 * l, 4)); break;
		case 0x3e: for (u32 l = 0; l < 4; ++l) mm.wrN(ea + 4 * l, 4, fpOrVf.UL[l]); break;
		default: break;
	}
}

//------------------------------------------------------------------
// One matrix cell
//------------------------------------------------------------------
struct MemCase
{
	u32 op, cls, align, imm;
	u32 rtSel;      // 0: $5, 1: $0, 2: $4 (the base register itself)
	bool constBase;
	GPR_reg init[32];     // what the program starts with (const variants fold base/data into the code and start with zero)
	GPR_reg logical[32];  // the architectural initial register values
	u32 ea;
	std::vector<u32> code;
	MemCtx ctx;
};

static bool eaValidForInterp(const MemOp& m, u32 ea)
{
	if (m.kind == 1 || m.kind == 2) return true;
	return (ea & (m.width - 1)) == 0;
}

static void buildMemCase(Rng& r, u32 op, u32 cls, u32 align, bool constBase, MemCase& c)
{
	const MemOp& m = kMemOps[op];
	const ClassInfo ci = classInfo(cls);
	c.op = op;
	c.cls = cls;
	c.align = align;
	c.constBase = constBase;
	c.ea = ci.center + align;
	if (cls == MC_HW) c.ea = HW_BASE + 8 + (align & 7);
	if (cls == MC_PAGEB) c.ea = 0x102000u - 8 + align;
	static const u32 imms[] = {0, 8, 0xfff8, 0x7ff0, 0x8010, 0x1234, 4, 0xfffc};
	c.imm = imms[r.below(8)] & 0xffffu;
	c.rtSel = r.below(10) < 6 ? 0 : r.below(2) ? 1 : 2;
	std::memset(c.init, 0, sizeof(c.init));
	const u32 baseVal = c.ea - static_cast<u32>(static_cast<s32>(static_cast<s16>(c.imm)));
	for (u32 reg = 1; reg < 24; ++reg) rand128(r, c.init[reg]);
	c.init[4].UD[0] = baseVal;
	c.init[4].UD[1] = 0;
	std::memcpy(c.logical, c.init, sizeof(c.logical));
	std::memset(&c.ctx, 0, sizeof(c.ctx));
	c.ctx.cls = cls;
	c.ctx.fpr1 = randW(r);
	for (u32 l = 0; l < 4; ++l) c.ctx.vf1[l] = randW(r);
	const u32 rt = c.rtSel == 0 ? 5 : c.rtSel == 1 ? 0 : 4;
	const bool cop = m.kind == 3;
	c.code.assign(PAD_WORDS, NOP);
	if (constBase)
	{
		emitConst64(c.code, 4, baseVal, 29);
		if (!cop)
		{
			emitConst128(c.code, 5, c.init[5].UD[0], c.init[5].UD[1], 29, 30);
			c.init[5] = GPR_reg{};
		}
		c.init[4] = GPR_reg{};
	}
	c.code.push_back(iI(m.op, 4, cop ? 1 : rt, c.imm));
	appendSentinel(c.code);
}

static std::string memTag(const MemCase& c, const char* variant)
{
	char b[160];
	std::snprintf(b, sizeof(b), "%s class=%s align=%u imm=%x rt=%s base=%s ea=%x %s", kMemOps[c.op].name, kMemClassName[c.cls], c.align, c.imm,
		c.rtSel == 0 ? "$5" : c.rtSel == 1 ? "$0" : "$4", c.constBase ? "const" : "runtime", c.ea, variant);
	return b;
}

static DiffOpt memDiffOpt(const MemOp& m)
{
	DiffOpt d;
	d.cmpScratch = true;
	d.cmpFpr = m.op == 0x31;
	d.cmpVf = m.op == 0x36;
	return d;
}

static void memObs(RunOpt& ro, u32 cls)
{
	const ClassInfo ci = classInfo(cls);
	if (cls == MC_RAM) return; // the standard scratch window is the observation window
	ro.obs.push_back({ci.win, cls == MC_HW ? 0x20u : 256u});
	if (cls == MC_TLB) { ro.obs.push_back({TLB_PA0 + 0x100, 256}); }
}

// Interpreter-referenced check of one cell.
static void runMemVsInterp(const MemCase& c, bool warm)
{
	const MemOp& m = kMemOps[c.op];
	RunOpt ro;
	ro.prep = memPrep;
	ro.prepCtx = const_cast<MemCtx*>(&c.ctx);
	ro.interpSteps = static_cast<u32>(c.code.size()) + 8;
	memObs(ro, c.cls);
	const EESnapshot ref = gapRun(false, c.code, c.init, nullptr, ro);
	const std::vector<u8> refObs = g_lastObs;
	ro.jitVariant = warm ? 1 : 0;
	const EESnapshot got = gapRun(true, c.code, c.init, nullptr, ro);
	const std::vector<u8> gotObs = g_lastObs;
	DiffOpt d = memDiffOpt(m);
	std::vector<Dif> dd;
	snapDiff(ref, got, d, dd);
	if (dd.empty() && refObs != gotObs)
	{
		size_t i = 0;
		while (i < refObs.size() && refObs[i] == gotObs[i]) ++i;
		dd.push_back({"MEM-window+" + hx(i), refObs[i], gotObs[i]});
	}
	gCase();
	if (dd.empty()) return;
	gFail(diffFamily(dd[0].f), memTag(c, warm ? "warm" : "cold") + " " + dd[0].f + " interp=" + hx(dd[0].a) + " jit=" + hx(dd[0].b) + " init{" + gprHex(c.init) + "} code{" + codeHex(c.code) + "}");
}

// Model-referenced check (JIT only): strict unaligned accesses and unmapped addresses.
static void runMemVsModel(const MemCase& c, bool warm)
{
	const MemOp& m = kMemOps[c.op];
	RunOpt ro;
	ro.prep = memPrep;
	ro.prepCtx = const_cast<MemCtx*>(&c.ctx);
	memObs(ro, c.cls);
	ro.jitVariant = warm ? 1 : 0;
	const EESnapshot got = gapRun(true, c.code, c.init, nullptr, ro);
	const std::vector<u8> gotObs = g_lastObs;

	// expected
	MemModel mm;
	const ClassInfo ci = classInfo(c.cls);
	mm.base = ci.win;
	if (c.cls != MC_UNMAPPED)
	{
		mm.mem.resize(256);
		for (u32 i = 0; i < 256; ++i)
		{
			u32 salt = c.cls == MC_SPR ? 1 : c.cls == MC_TLB ? 2 : c.cls == MC_PAGEB ? 4 : 0;
			u8 b = memPattern(i, salt);
			if (c.cls == MC_RAM) b = static_cast<u8>(i * 13 + 7);
			mm.mem[i] = b;
		}
	}
	const u32 rt = c.rtSel == 0 ? 5 : c.rtSel == 1 ? 0 : 4;
	GPR_reg fp{};
	fp.UL[0] = c.ctx.fpr1;
	if (m.op == 0x36 || m.op == 0x3e)
		for (u32 l = 0; l < 4; ++l) fp.UL[l] = c.ctx.vf1[l];
	GPR_reg rtOut = c.logical[rt];
	if (rt == 0) rtOut = GPR_reg{};
	modelExec(m, c.ea, rtOut, fp, mm);
	std::vector<Dif> dd;
	const bool isStore = !m.load;
	for (u32 i = 1; i < 32; ++i)
	{
		if (c.constBase && (i == 29 || i == 30)) continue; // constant-materialisation temporaries
		GPR_reg want = c.logical[i];
		if (!isStore && m.kind != 3 && i == rt && rt != 0) want = rtOut;
		if (got.gpr[i] != want.UD[0]) { dd.push_back({"GPR" + std::to_string(i) + ".lo", want.UD[0], got.gpr[i]}); break; }
		if (got.gpr_upper[i] != want.UD[1]) { dd.push_back({"GPR" + std::to_string(i) + ".hi", want.UD[1], got.gpr_upper[i]}); break; }
	}
	if (dd.empty() && m.op == 0x31 && got.fpr[1] != fp.UL[0]) dd.push_back({"FPR1", fp.UL[0], got.fpr[1]});
	if (dd.empty() && m.op == 0x36)
		for (u32 l = 0; l < 4; ++l)
			if (got.vu0_vf[4 + l] != fp.UL[l]) { dd.push_back({"VF1." + std::string(1, "xyzw"[l]), fp.UL[l], got.vu0_vf[4 + l]}); break; }
	// memory
	if (dd.empty() && !mm.mem.empty())
	{
		const u8* actual = c.cls == MC_RAM ? got.scratch : gotObs.data();
		for (u32 i = 0; i < 256; ++i)
			if (actual[i] != mm.mem[i]) { dd.push_back({"MEM-window+" + hx(i), mm.mem[i], actual[i]}); break; }
	}
	if (dd.empty() && c.cls == MC_UNMAPPED)
	{
		// no exception state was touched: Status/Cause/EPC/BadVAddr stay at their initial zero
		for (u32 reg : {4u, 8u, 12u, 13u, 14u})
			if (got.cp0[reg] != 0) { dd.push_back({"CP0[" + std::to_string(reg) + "]", 0, got.cp0[reg]}); break; }
		if (dd.empty() && got.pc != EE_TEST_PC + static_cast<u32>(c.code.size() - 2) * 4) dd.push_back({"PC", 0, got.pc});
	}
	gCase();
	if (dd.empty()) return;
	gFail(diffFamily(dd[0].f), memTag(c, warm ? "warm" : "cold") + " " + dd[0].f + " model=" + hx(dd[0].a) + " jit=" + hx(dd[0].b) + " init{" + gprHex(c.init) + "} code{" + codeHex(c.code) + "}");
}

//------------------------------------------------------------------
// Two-phase fast-path state tests
//------------------------------------------------------------------
static void tlbRemapPrep(void*)
{
	tlbClearAll();
	tlbInstall(0, TLB_VA, TLB_PA0, TLB_PA1);
	fillMem(TLB_PA0, 0x1000, 11);
	fillMem(TLB_PA1, 0x1000, 12);
	fillMem(TLB_PA2, 0x1000, 13);
	fillMem(TLB_PA3, 0x1000, 14);
}

static u32 g_remapMode = 0;
static void tlbRemapMid(void*)
{
	// The compiled code now sees a different mapping for the same virtual pages.
	if (g_remapMode == 0) tlbInstall(0, TLB_VA, TLB_PA2, TLB_PA3);
	else if (g_remapMode == 1) tlbInstall(0, TLB_VA, TLB_PA1, TLB_PA0); // swap the pair
	else { tlbInstall(0, TLB_VA, TLB_PA0, TLB_PA1); tlbInstall(1, TLB_VA + 0x10000, TLB_PA2, TLB_PA3); } // same mapping, extra entry
}

static void runTlbRemapAfterCompile(u32 reps)
{
	for (u32 mode = 0; mode < 3; ++mode)
	for (u32 constBase = 0; constBase < 2; ++constBase)
	{
		char nm[96];
		std::snprintf(nm, sizeof(nm), "ee-mem-matrix tlb-remap-after-compile mode=%u %s", mode, constBase ? "const" : "runtime");
		if (!onlyMatch(nm)) continue;
		gBegin(nm);
		Rng r(baseSeed() * 7919ull + mode * 131ull + constBase * 17ull + 41);
		for (u32 k = 0; k < reps * 8; ++k)
		{
			GPR_reg init[32];
			std::memset(init, 0, sizeof(init));
			for (u32 reg = 5; reg < 24; ++reg) rand128(r, init[reg]);
			const u32 baseVal = TLB_VA + 0x800 * r.below(4); // page 0 and page 1 of the pair
			std::vector<u32> code(PAD_WORDS, NOP);
			if (constBase) emitConst64(code, 4, baseVal, 29); else init[4].UD[0] = baseVal;
			static const u32 kLoads[] = {0x23, 0x37, 0x1e, 0x20, 0x25, 0x22, 0x1b};
			static const u32 kStores[] = {0x2b, 0x3f, 0x1f, 0x28, 0x29, 0x2a, 0x2d};
			// loads feed registers, stores write memory in both pages; the interleaving exercises the cached translation per access
			for (u32 n = 0; n < 3; ++n)
			{
				code.push_back(iI(kLoads[r.below(7)], 4, 5 + n, 0x40 + 8 * r.below(8)));
				code.push_back(iI(kStores[r.below(7)], 4, 8 + n, 0x100 + 8 * r.below(8)));
			}
			appendSentinel(code);
			g_remapMode = mode;
			RunOpt ro;
			ro.prep = tlbRemapPrep;
			ro.twoPhase = true;
			ro.mid = tlbRemapMid;
			ro.interpSteps = static_cast<u32>(code.size()) + 8;
			for (u32 pa : {TLB_PA0, TLB_PA1, TLB_PA2, TLB_PA3}) ro.obs.push_back({pa, 0x200});
			const EESnapshot ref = gapRun(false, code, init, nullptr, ro);
			const std::vector<u8> refObs = g_lastObs;
			const EESnapshot got = gapRun(true, code, init, nullptr, ro);
			DiffOpt d;
			std::vector<Dif> dd;
			snapDiff(ref, got, d, dd);
			if (dd.empty() && refObs != g_lastObs)
			{
				size_t i = 0;
				while (i < refObs.size() && refObs[i] == g_lastObs[i]) ++i;
				dd.push_back({"MEM-phys+" + hx(i), refObs[i], g_lastObs[i]});
			}
			gCase();
			if (!dd.empty())
				gFail(diffFamily(dd[0].f), "tlb remap mode=" + std::to_string(mode) + " " + dd[0].f + " interp=" + hx(dd[0].a) + " jit=" + hx(dd[0].b) + " base=" + hx(baseVal) + " init{" + gprHex(init) + "} code{" + codeHex(code) + "}");
		}
		gEnd();
	}
	tlbClearAll();
}

// Guest store into a page that contains a compiled block (second run): the block must be invalidated and the new instruction executed.
static void runSmcStoreIntoCompiledPage(u32 reps)
{
	for (u32 constBase = 0; constBase < 2; ++constBase)
	{
		char nm[96];
		std::snprintf(nm, sizeof(nm), "ee-mem-matrix smc-store-into-compiled-page %s", constBase ? "const" : "runtime");
		if (!onlyMatch(nm)) continue;
		gBegin(nm);
		Rng r(baseSeed() * 104729ull + constBase * 31ull + 77);
		for (u32 k = 0; k < reps * 20; ++k)
		{
			std::vector<u32> code(PAD_WORDS, NOP);
			const u32 slotIdx = PAD_WORDS + 8 + (constBase ? 0 : 0);
			const u32 slotAddr = EE_TEST_PC + slotIdx * 4;
			GPR_reg init1[32], init2[32];
			std::memset(init1, 0, sizeof(init1));
			std::memset(init2, 0, sizeof(init2));
			const u32 w1 = ADDIU(10, 0, 0x11 + r.below(100)), w2 = r.below(4) == 0 ? w1 : ADDIU(10, 0, 0x200 + r.below(100));
			init1[9].UD[0] = w1;
			init2[9].UD[0] = w2;
			rand128(r, init1[12]);
			init2[12] = init1[12];
			code.push_back(LUI(8, slotAddr >> 16));
			code.push_back(ORI(8, 8, slotAddr & 0xffff));
			code.push_back(iI(0x2b, 8, 9, 0));                  // SW $9, 0($8): patches the first word of the next block
			if (constBase) { code.push_back(ADDIU(13, 0, 7)); } else code.push_back(NOP);
			code.push_back(JTYPE(2, slotAddr));
			code.push_back(NOP);
			code.push_back(NOP);
			code.push_back(NOP);
			// slot: original word is patched by the store above before it is ever reached in this run
			code.push_back(ADDIU(10, 0, 1));
			code.push_back(ADDIU(11, 10, 5));
			appendSentinel(code);
			RunOpt ro;
			ro.twoPhase = true;
			ro.gpr2 = init2;
			ro.interpSteps = static_cast<u32>(code.size()) + 12;
			const EESnapshot ref = gapRun(false, code, init1, nullptr, ro);
			const EESnapshot got = gapRun(true, code, init1, nullptr, ro);
			gCase();
			DiffOpt d;
			std::vector<Dif> dd;
			snapDiff(ref, got, d, dd);
			if (!dd.empty())
				gFail(diffFamily(dd[0].f), "smc second run " + dd[0].f + " interp=" + hx(dd[0].a) + " jit=" + hx(dd[0].b) + " w1=" + hx(w1) + " w2=" + hx(w2) + " code{" + codeHex(code) + "}");
		}
		gEnd();
	}
}

//------------------------------------------------------------------
// Matrix driver
//------------------------------------------------------------------
static bool hwCellAllowed(const MemOp& m)
{
	return m.op != 0x1f && m.op != 0x3e; // no 128-bit stores into the hardware register file
}

static void runMemMatrix()
{
	const u32 reps = knob("GAP_N", 1);
	// A: interpreter-referenced (all accesses the interpreter models)
	for (u32 cls = MC_RAM; cls <= MC_HW; ++cls)
	{
		const std::string gname = std::string("ee-mem-matrix ") + kMemClassName[cls] + " vs-interpreter";
		if (!onlyMatch(gname)) continue;
		gBegin(gname);
		Rng r(baseSeed() * 0x9e3779b1ull + cls * 7919ull + 5);
		for (u32 op = 0; op < kMemOpCount; ++op)
		{
			if (cls == MC_HW && !hwCellAllowed(kMemOps[op])) continue;
			for (u32 align = 0; align < 16; ++align)
				for (u32 constBase = 0; constBase < 2; ++constBase)
					for (u32 rep = 0; rep < reps; ++rep)
					{
						MemCase c;
						buildMemCase(r, op, cls, align, constBase != 0, c);
						if (!eaValidForInterp(kMemOps[op], c.ea)) continue;
						if (cls == MC_HW && (kMemOps[op].kind == 2 || kMemOps[op].kind == 3) && kMemOps[op].width == 16 && kMemOps[op].load == false) continue;
						runMemVsInterp(c, false);
						runMemVsInterp(c, true);
					}
		}
		gEnd();
	}
	// B: strict accesses at unaligned addresses, JIT vs byte model
	for (u32 cls = MC_RAM; cls <= MC_PAGEB; ++cls)
	{
		const std::string gname = std::string("ee-mem-matrix ") + kMemClassName[cls] + " strict-unaligned vs-model";
		if (!onlyMatch(gname)) continue;
		gBegin(gname);
		Rng r(baseSeed() * 0x85ebca6bull + cls * 7919ull + 9);
		for (u32 op = 0; op < kMemOpCount; ++op)
		{
			const MemOp& m = kMemOps[op];
			if (!(m.kind == 0 || m.op == 0x31 || m.op == 0x39) || m.width == 1) continue;
			for (u32 align = 0; align < 16; ++align)
				for (u32 constBase = 0; constBase < 2; ++constBase)
					for (u32 rep = 0; rep < reps; ++rep)
					{
						MemCase c;
						buildMemCase(r, op, cls, align, constBase != 0, c);
						if (eaValidForInterp(m, c.ea)) continue;
						runMemVsModel(c, false);
						runMemVsModel(c, true);
					}
		}
		gEnd();
	}
	// C: unmapped addresses, JIT vs model (zero reads, discarded writes, no exception state)
	if (onlyMatch("ee-mem-matrix unmapped vs-model"))
	{
		gBegin("ee-mem-matrix unmapped vs-model");
		Rng r(baseSeed() * 0xc2b2ae35ull + 13);
		for (u32 op = 0; op < kMemOpCount; ++op)
			for (u32 align = 0; align < 16; ++align)
				for (u32 constBase = 0; constBase < 2; ++constBase)
					for (u32 rep = 0; rep < reps; ++rep)
					{
						MemCase c;
						buildMemCase(r, op, MC_UNMAPPED, align, constBase != 0, c);
						runMemVsModel(c, false);
						runMemVsModel(c, true);
					}
		gEnd();
	}
	runTlbRemapAfterCompile(reps);
	runSmcStoreIntoCompiledPage(reps);
}

} // namespace gapee

#endif // EMUCOREX_ENABLE_NATIVE_SELF_TESTS
