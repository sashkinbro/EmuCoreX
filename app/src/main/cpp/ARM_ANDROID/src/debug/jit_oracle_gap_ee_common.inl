// SPDX-FileCopyrightText: 2026 EmuCoreX Team
// SPDX-License-Identifier: GPL-3.0+
// EE coverage-gap oracle suites: shared harness (knobs, RNG, encoders, reporting, runners, diffing).
// Included from jit_oracle.cpp's anonymous namespace after jit_oracle_x86_parity.inl.
//
// Suites (all FAIL-gated, deterministic): ee-int-rand, ee-block-rand, ee-exc-mid-block, ee-mem-matrix, ee-cop0-tlb.
// Knobs (environment): GAP_SEED (default 1), GAP_N (cases per group, per-suite default), GAP_ONLY (substring filter on group
// names), GAP_V (verbose), GAP_REPROS (repros printed per group, default 3).
// Reference: the EE interpreter for integer/branch/COP0 behaviour; a byte-level model where the interpreter is known to differ
// from the x86 recompiler by design (unaligned strict accesses are cancelled by the interpreter, unmapped accesses raise TLB
// exceptions in the interpreter but read zero / discard in the recompiler like upstream x86).

#if defined(EMUCOREX_ENABLE_NATIVE_SELF_TESTS) && !defined(NDEBUG)

namespace gapee
{
using x86p::Rng;

static inline u32 knob(const char* name, u32 def) { return x86p::envKnob(name, def); }
static inline u64 baseSeed() { return knob("GAP_SEED", 1); }
static inline bool onlyMatch(const std::string& name)
{
	const char* f = std::getenv("GAP_ONLY");
	return !f || !*f || name.find(f) != std::string::npos;
}

//------------------------------------------------------------------
// Group reporting
//------------------------------------------------------------------
struct Group
{
	std::string name;
	u64 cases = 0, fails = 0;
	u32 shown = 0;
	std::map<std::string, u64> keys;
	std::chrono::steady_clock::time_point t0;
};
static Group g_grp;
static u64 g_totalCases = 0, g_totalFails = 0;

static void gBegin(const std::string& name)
{
	g_grp = Group();
	g_grp.name = name;
	g_grp.t0 = std::chrono::steady_clock::now();
}

static void gEnd()
{
	char buf[320];
	const double sec = std::chrono::duration<double>(std::chrono::steady_clock::now() - g_grp.t0).count();
	std::snprintf(buf, sizeof(buf), "gapee %s (%llu cases, %llu mismatches, %.1fs)", g_grp.name.c_str(),
		static_cast<unsigned long long>(g_grp.cases), static_cast<unsigned long long>(g_grp.fails), sec);
	Check(g_grp.fails == 0, buf);
	for (const auto& k : g_grp.keys)
		std::printf("GAPEE KEY n=%llu %s | %s\n", static_cast<unsigned long long>(k.second), g_grp.name.c_str(), k.first.c_str());
	g_totalCases += g_grp.cases;
	g_totalFails += g_grp.fails;
}

static void gCase() { ++g_grp.cases; }

// key: root-cause family ("GPR", "HI", "MEM", "PC", ...), repro: free text, printed for the first few failing cases per key.
static bool g_suppressRepro = false; // a program that already printed a repro for one JIT variant stays quiet for the others
static inline bool knownIncluded() { return knob("GAP_KNOWN", 0) != 0; }

static void gFail(const std::string& key, const std::string& repro)
{
	++g_grp.fails;
	u64& n = g_grp.keys[key];
	if (g_suppressRepro) { ++n; return; }
	if (n++ < knob("GAP_REPROS", 2))
		std::printf("GAPEE REPRO %s [%s] %s\n", g_grp.name.c_str(), key.c_str(), repro.c_str());
}

static std::string hx(u64 v)
{
	char b[24];
	std::snprintf(b, sizeof(b), "%llx", static_cast<unsigned long long>(v));
	return b;
}

static std::string codeHex(const std::vector<u32>& code, size_t maxWords = 120)
{
	std::string s;
	for (size_t i = 0; i < code.size() && i < maxWords; ++i)
	{
		char b[12];
		std::snprintf(b, sizeof(b), "%08x ", code[i]);
		s += b;
	}
	return s;
}

static std::string gprHex(const GPR_reg* g)
{
	std::string s;
	for (u32 i = 1; i < 32; ++i)
	{
		if (!g[i].UD[0] && !g[i].UD[1]) continue;
		s += "r" + std::to_string(i) + "=" + hx(g[i].UD[1]) + ":" + hx(g[i].UD[0]) + " ";
	}
	return s;
}

//------------------------------------------------------------------
// Encoders (EE)
//------------------------------------------------------------------
constexpr u32 PAD_WORDS = 4;                         // reserved words before the program (variant 3 patches a jump here)
constexpr u32 PROG_BASE = EE_TEST_PC + PAD_WORDS * 4;
static constexpr u32 NOP = 0;
static inline u32 iR(u32 rs, u32 rt, u32 rd, u32 sa, u32 fn) { return MipsR(rs, rt, rd, sa, fn); }
static inline u32 iI(u32 op, u32 rs, u32 rt, u32 imm) { return MipsI(op, rs, rt, imm); }
static inline u32 ADDU(u32 rd, u32 rs, u32 rt) { return iR(rs, rt, rd, 0, 0x21); }
static inline u32 OR(u32 rd, u32 rs, u32 rt) { return iR(rs, rt, rd, 0, 0x25); }
static inline u32 ADDIU(u32 rt, u32 rs, int imm) { return iI(9, rs, rt, static_cast<u32>(imm)); }
static inline u32 LUI(u32 rt, u32 imm) { return iI(15, 0, rt, imm); }
static inline u32 ORI(u32 rt, u32 rs, u32 imm) { return iI(13, rs, rt, imm); }
static inline u32 SLTIU(u32 rt, u32 rs, u32 imm) { return iI(11, rs, rt, imm); }
static inline u32 DSLL32(u32 rd, u32 rt, u32 sa) { return iR(0, rt, rd, sa, 0x3c); }
static inline u32 DSRL32(u32 rd, u32 rt, u32 sa) { return iR(0, rt, rd, sa, 0x3e); }
static inline u32 MFC0(u32 rt, u32 rd) { return (0x10u << 26) | (rt << 16) | (rd << 11); }
static inline u32 MTC0(u32 rt, u32 rd) { return (0x10u << 26) | (4u << 21) | (rt << 16) | (rd << 11); }
static inline u32 MFHI(u32 rd) { return iR(0, 0, rd, 0, 0x10); }
static inline u32 MFLO(u32 rd) { return iR(0, 0, rd, 0, 0x12); }
static inline u32 MTHI(u32 rs) { return iR(rs, 0, 0, 0, 0x11); }
static inline u32 MTLO(u32 rs) { return iR(rs, 0, 0, 0, 0x13); }
static inline u32 MTHI1(u32 rs) { return Mmi(0x11, 0, rs, 0, 0); }
static inline u32 MTLO1(u32 rs) { return Mmi(0x13, 0, rs, 0, 0); }
static inline u32 MFSA(u32 rd) { return iR(0, 0, rd, 0, 0x28); }
static inline u32 MTSA(u32 rs) { return iR(rs, 0, 0, 0, 0x29); }
static inline u32 PCPYLD(u32 rd, u32 rs, u32 rt) { return Mmi(9, 14, rs, rt, rd); }
static inline u32 JTYPE(u32 op, u32 target) { return MipsJ(op, target); }
static inline u32 BR(u32 op, u32 rs, u32 rt, int off) { return iI(op, rs, rt, static_cast<u32>(off)); }
static inline u32 ERET() { return 0x42000018u; }
static inline u32 BREAK() { return 0x0000000du; }
static inline u32 SYSCALL() { return 0x0000000cu; }
static inline u32 JR(u32 rs) { return iR(rs, 0, 0, 0, 0x08); }
static inline u32 JALR(u32 rd, u32 rs) { return iR(rs, 0, rd, 0, 0x09); }
static inline u32 BREL(u32 branchPc, u32 target) { return static_cast<u32>(static_cast<s32>(target - (branchPc + 4)) >> 2) & 0xffffu; }

// Materialize a 64-bit constant into reg using only LUI/ORI/ADDIU/DSLL32/DSRL32/OR (tmp must differ from reg).
static void emitConst64(std::vector<u32>& c, u32 reg, u64 v, u32 tmp)
{
	const s64 sv = static_cast<s64>(v);
	if (sv >= -32768 && sv <= 32767)
	{
		c.push_back(ADDIU(reg, 0, static_cast<int>(sv)));
		return;
	}
	if (static_cast<s64>(static_cast<s32>(static_cast<u32>(v))) == sv)
	{
		c.push_back(LUI(reg, static_cast<u32>(v) >> 16));
		if (v & 0xffffu) c.push_back(ORI(reg, reg, static_cast<u32>(v) & 0xffffu));
		return;
	}
	const u32 hi = static_cast<u32>(v >> 32), lo = static_cast<u32>(v);
	c.push_back(LUI(reg, hi >> 16));
	if (hi & 0xffffu) c.push_back(ORI(reg, reg, hi & 0xffffu));
	c.push_back(DSLL32(reg, reg, 0));
	if (lo)
	{
		c.push_back(LUI(tmp, lo >> 16));
		if (lo & 0xffffu) c.push_back(ORI(tmp, tmp, lo & 0xffffu));
		c.push_back(DSLL32(tmp, tmp, 0));
		c.push_back(DSRL32(tmp, tmp, 0));
		c.push_back(OR(reg, reg, tmp));
	}
}

// 128-bit constant: lo64 into reg, hi64 via PCPYLD (needs two temps).
static void emitConst128(std::vector<u32>& c, u32 reg, u64 lo, u64 hi, u32 tmp, u32 tmp2)
{
	emitConst64(c, reg, lo, tmp);
	if (hi)
	{
		emitConst64(c, tmp2, hi, tmp);
		c.push_back(PCPYLD(reg, tmp2, reg));
	}
}

//------------------------------------------------------------------
// Values
//------------------------------------------------------------------
static const u32 kW[] = {0u, 1u, 2u, 3u, 0x7fu, 0x80u, 0xffu, 0x100u, 0x7fffu, 0x8000u, 0xffffu, 0x10000u, 0x7fffffffu, 0x80000000u,
	0x80000001u, 0xffffffffu, 0xfffffffeu, 0xffff0000u, 0xffff8000u, 0x0000ffffu, 0x00ff00ffu, 0xff00ff00u, 0x12345678u, 0xdeadbeefu,
	0x7f7f7f7fu, 0x80808080u, 0x01010101u, 0xfffffff0u, 0x0000001fu, 0x00000020u, 0x0000003fu, 0x00000040u};
constexpr u32 kWN = sizeof(kW) / sizeof(kW[0]);

static u32 randW(Rng& r)
{
	switch (r.below(8))
	{
		case 0: return r.next() ^ (r.next() << 16);
		case 1: return static_cast<u32>(r.below(0x20000)) - 0x10000u;
		default: return kW[r.below(kWN)];
	}
}

static u64 rand64(Rng& r)
{
	switch (r.below(10))
	{
		case 0: return (static_cast<u64>(r.next()) << 32) ^ (static_cast<u64>(r.next()) << 16) ^ r.next();
		case 1: return static_cast<u64>(static_cast<s64>(static_cast<s32>(randW(r))));            // sign-extended 32
		case 2: return static_cast<u64>(randW(r));                                                 // zero-extended 32
		case 3: { static const u64 e[] = {0x7fffffffffffffffull, 0x8000000000000000ull, 0xffffffffffffffffull, 0x100000000ull, 0xffffffffull, 0x8000000000000001ull, 0xfffffffeffffffffull}; return e[r.below(7)]; }
		default: return (static_cast<u64>(randW(r)) << 32) | randW(r);
	}
}

static void rand128(Rng& r, GPR_reg& g)
{
	g.UD[0] = rand64(r);
	g.UD[1] = r.below(10) < 3 ? g.UD[0] : rand64(r);
}

static u32 randImm16(Rng& r)
{
	switch (r.below(6))
	{
		case 0: return r.next() & 0xffffu;
		default: { static const u32 e[] = {0, 1, 0xffffu, 0x7fffu, 0x8000u, 0x8001u, 0xfffeu, 2, 0x00ffu, 0xff00u, 0x1234u}; return e[r.below(11)]; }
	}
}

// Register pick with aliasing: out[0..2] = rs, rt, rd from [lo..hi] (+ $0 occasionally), ~30% alias between operands.
static void pickRegs(Rng& r, u32 lo, u32 hi, u32& rs, u32& rt, u32& rd)
{
	const auto one = [&]() -> u32 { return r.below(12) == 0 ? 0u : lo + r.below(hi - lo + 1); };
	rs = one();
	rt = r.below(10) < 3 ? rs : one();
	const u32 k = r.below(20);
	rd = k < 3 ? rs : k < 6 ? rt : k < 8 ? 0u : one();
}

//------------------------------------------------------------------
// Snapshot diffing
//------------------------------------------------------------------
struct Dif { std::string f; u64 a, b; };

struct DiffOpt
{
	u32 gprSkip = 0;          // GPR numbers excluded (bit per register)
	u32 cp0Mask = 0;          // COP0 registers compared (bit per register); Random/Count/Compare never compared here
	bool cmpHiLo = true;
	bool cmpScratch = true;
	std::vector<u32> scratchSkipWords; // 4-byte word indices excluded
	u32 pcMode = 1;           // 0: not compared, 1: compared
	bool cmpFpr = false;      // FPU registers (LWC1/SWC1)
	bool cmpVf = false;       // VU0 vector registers (LQC2/SQC2)
};

static void snapDiff(const EESnapshot& a, const EESnapshot& b, const DiffOpt& o, std::vector<Dif>& d)
{
	for (u32 i = 1; i < 32; ++i)
	{
		if (o.gprSkip & (1u << i)) continue;
		if (a.gpr[i] != b.gpr[i]) d.push_back({"GPR" + std::to_string(i) + ".lo", a.gpr[i], b.gpr[i]});
		if (a.gpr_upper[i] != b.gpr_upper[i]) d.push_back({"GPR" + std::to_string(i) + ".hi", a.gpr_upper[i], b.gpr_upper[i]});
	}
	if (o.cmpHiLo)
	{
		if (a.hi != b.hi) d.push_back({"HI", a.hi, b.hi});
		if (a.lo != b.lo) d.push_back({"LO", a.lo, b.lo});
		if (a.hi1 != b.hi1) d.push_back({"HI1", a.hi1, b.hi1});
		if (a.lo1 != b.lo1) d.push_back({"LO1", a.lo1, b.lo1});
	}
	if (o.pcMode && a.pc != b.pc) d.push_back({"PC", a.pc, b.pc});
	for (u32 i = 0; i < 32; ++i)
		if ((o.cp0Mask & (1u << i)) && a.cp0[i] != b.cp0[i]) d.push_back({"CP0[" + std::to_string(i) + "]", a.cp0[i], b.cp0[i]});
	if (o.cmpFpr)
		for (u32 i = 0; i < 32; ++i)
			if (a.fpr[i] != b.fpr[i]) { d.push_back({"FPR" + std::to_string(i), a.fpr[i], b.fpr[i]}); break; }
	if (o.cmpVf)
		for (u32 i = 0; i < 128; ++i)
			if (a.vu0_vf[i] != b.vu0_vf[i]) { d.push_back({"VF" + std::to_string(i >> 2) + "." + "xyzw"[i & 3], a.vu0_vf[i], b.vu0_vf[i]}); break; }
	if (o.cmpScratch)
		for (u32 w = 0; w < EE_SCRATCH_SIZE / 4; ++w)
		{
			if (std::find(o.scratchSkipWords.begin(), o.scratchSkipWords.end(), w) != o.scratchSkipWords.end()) continue;
			u32 x, y;
			std::memcpy(&x, a.scratch + w * 4, 4);
			std::memcpy(&y, b.scratch + w * 4, 4);
			if (x != y) { d.push_back({"MEM+" + hx(w * 4), x, y}); break; }
		}
}

static std::string diffFamily(const std::string& f)
{
	if (f.rfind("GPR", 0) == 0) return "GPR";
	if (f.rfind("MEM", 0) == 0) return "MEM";
	if (f.rfind("CP0", 0) == 0) return f;
	return f;
}

//------------------------------------------------------------------
// Runner
//------------------------------------------------------------------
struct RunOpt
{
	u32 interpSteps = 0;      // 0 -> code.size() + 16
	bool stopAtVector = false; // interpreter: stop when pc enters kernel space (exception vector)
	u32 jitVariant = 0;       // 0 cold, 1 warm (re-run without recompile), 2 recompile (rewrite + Clear)
	void (*prep)(void*) = nullptr; // called after the standard state init (both engines), before execution
	void* prepCtx = nullptr;
	u32 scratchFill = 0;      // 0: i*13+7 pattern
	u32 budget = 4096;
	// two-phase run: execute, re-initialise state (gpr2 if given), call 'mid' (e.g. remap a TLB page), execute again; the
	// snapshot is taken after the second execution. The JIT does not reset or clear its caches in between.
	bool twoPhase = false;
	const GPR_reg* gpr2 = nullptr;
	void (*mid)(void*) = nullptr;
	void* midCtx = nullptr;
	// Real event processing in the recompiler. Instructions that end a block with an event check (TLBWI/TLBWR, SYSCALL, ...) make the
	// forced-exit hook of the other suites leave the program early; here the full event test runs (with an idle IOP, no pending
	// interrupts) and the recompiler is asked to exit only when it compiles the terminating self-branch block.
	bool realEvents = false;
	// memory windows captured after the run (through the virtual map; EE hardware register addresses read the register file directly)
	std::vector<std::pair<u32, u32>> obs;
};

// Side captures of the most recent gapRun (copied by the caller when needed).
static std::vector<u8> g_lastObs;
static u8 g_lastTlb[48 * sizeof(tlbs)];

static void gapInitState(const std::vector<u32>& code, const GPR_reg* gpr, const u32* cp0, const RunOpt& o, bool writeCode)
{
	std::memset(&cpuRegs, 0, sizeof(cpuRegs));
	std::memset(&fpuRegs, 0, sizeof(fpuRegs));
	std::memset(&VU0.VF, 0, sizeof(VU0.VF));
	std::memset(&VU0.VI, 0, sizeof(VU0.VI));
	std::memset(&VU0.ACC, 0, sizeof(VU0.ACC));
	VU0.q.UL = VU0.p.UL = 0;
	VU0.macflag = VU0.statusflag = VU0.clipflag = 0;
	if (writeCode)
		for (u32 i = 0; i < code.size(); ++i)
			memWrite32(EE_TEST_PC + i * 4, code[i]);
	for (u32 i = 0; i < EE_SCRATCH_SIZE; ++i)
		memWrite8(EE_TEST_SCRATCH + i, static_cast<u8>(i * 13 + 7 + o.scratchFill));
	cpuRegs.pc = EE_TEST_PC;
	cpuRegs.cycle = 0;
	cpuRegs.branch = 0;
	cpuRegs.nextEventCycle = 0x7fffffffu;
	EEsCycle = 0;
	EEoCycle = 0;
	if (gpr)
		for (u32 r = 1; r < 32; ++r)
			cpuRegs.GPR.r[r] = gpr[r];
	if (cp0)
		std::memcpy(cpuRegs.CP0.r, cp0, sizeof(cpuRegs.CP0.r));
	if (o.prep)
		o.prep(o.prepCtx);
}

static void interpExecute(const std::vector<u32>& code, const RunOpt& o)
{
	Cpu = &intCpu;
	Cpu->Reset();
	const u32 steps = o.interpSteps ? o.interpSteps : static_cast<u32>(code.size()) + 16;
	if (o.stopAtVector)
	{
		for (u32 i = 0; i < steps; ++i)
		{
			EmuCoreXOracleEESteps(1);
			if (cpuRegs.pc & 0x80000000u) break;
		}
	}
	else
		EmuCoreXOracleEESteps(steps);
}

static u32 g_gapExitPc = 0;
static void gapExitCallback(u32 pc)
{
	if (pc == g_gapExitPc) EmuCoreXEEForceExitAfterCycles(1);
}

// Idle IOP / counters / interrupt controllers so that the complete EE event test is side-effect free (same state as EeIRQEventTests).
static void gapPrepRealEvents()
{
	std::memset(&psxRegs, 0, sizeof(psxRegs));
	psxRegs.pc = IOP_TEST_SCRATCH;
	psxRegs.iopNextEventCycle = 0x7fffffffu;
	iopMemWrite32(IOP_TEST_SCRATCH, 0x1000ffffu);
	iopMemWrite32(IOP_TEST_SCRATCH + 4, 0);
	psxNextStartCounter = psxRegs.cycle;
	psxNextDeltaCounter = 0x40000000;
	nextStartCounter = cpuRegs.cycle;
	nextDeltaCounter = 0x40000000;
	cpuRegs.lastCOP0Cycle = cpuRegs.cycle;
	cpuRegs.lastEventCycle = cpuRegs.cycle;
	cpuRegs.nextEventCycle = 0x7fffffffu;
	cpuRegs.interrupt = 0;
	EEsCycle = -1000000;
	EEoCycle = cpuRegs.cycle;
	iopEventAction = false;
	VU0.VI[REG_VPU_STAT].UL = 0;
	vu1Thread.mtvuInterrupts.store(0);
	for (u32 i = 0; i < 4; ++i)
	{
		counters[i] = {};
		counters[i].count = 123 + 333 * i;
		counters[i].rate = 1;
		counters[i].hold = 0xffffffff;
	}
	psHu32(INTC_STAT) = 0;
	psHu32(INTC_MASK) = 0;
	psHu16(DMAC_STAT) = 0;
	psHu16(DMAC_STAT + 2) = 0;
	dmacRegs.ctrl.DMAE = 0;
	psHu8(DMAC_ENABLER + 2) = 0;
}

static void jitExecute(const std::vector<u32>& code, const RunOpt& o)
{
	if (!o.realEvents)
	{
		EmuCoreXEEForceExitAfterCycles(o.budget);
		Cpu->Execute();
		return;
	}
	gapPrepRealEvents();
	g_gapExitPc = EE_TEST_PC + static_cast<u32>(code.size() - 2) * 4;
	Cpu->Clear(g_gapExitPc, 4); // make sure the terminating block is (re)compiled so the callback fires
	EmuCoreXEEOracleSetCompileCallback(gapExitCallback);
	Cpu->Execute();
	EmuCoreXEEOracleSetCompileCallback(nullptr);
}

static EESnapshot gapRun(bool jit, const std::vector<u32>& code, const GPR_reg* gpr, const u32* cp0, const RunOpt& o)
{
	const bool savedEnableEE = EmuConfig.Cpu.Recompiler.EnableEE;
	EmuConfig.Cpu.Recompiler.EnableEE = jit;
	const FPControlRegisterBackup eeFpcrBackup(EmuConfig.Cpu.FPUFPCR);
	gapInitState(code, gpr, cp0, o, true);
	if (jit)
	{
		Cpu = &recCpu;
		Cpu->Reset();
		jitExecute(code, o);
		if (o.twoPhase)
		{
			gapInitState(code, o.gpr2 ? o.gpr2 : gpr, cp0, o, false);
			if (o.mid) o.mid(o.midCtx);
			jitExecute(code, o);
		}
		else if (o.jitVariant == 1 || o.jitVariant == 2)
		{
			gapInitState(code, gpr, cp0, o, o.jitVariant == 2);
			if (o.jitVariant == 2)
				Cpu->Clear(EE_TEST_PC, static_cast<u32>(code.size()));
			jitExecute(code, o);
		}
	}
	else
	{
		interpExecute(code, o);
		if (o.twoPhase)
		{
			gapInitState(code, o.gpr2 ? o.gpr2 : gpr, cp0, o, false);
			if (o.mid) o.mid(o.midCtx);
			interpExecute(code, o);
		}
	}
	EESnapshot out;
	CaptureEE(out);
	g_lastObs.clear();
	for (const auto& w : o.obs)
		for (u32 i = 0; i < w.second; ++i)
		{
			const u32 a = w.first + i;
			if (a >= 0x10000000u && a < 0x10010000u)
				g_lastObs.push_back(eeHw[a & 0xffff]);
			else
				g_lastObs.push_back(memRead8(a));
		}
	std::memcpy(g_lastTlb, tlb, sizeof(g_lastTlb));
	EmuConfig.Cpu.Recompiler.EnableEE = savedEnableEE;
	// Normalise: spinning in the terminating self-branch, or running in the kernel exception area.
	const u32 sentinel = EE_TEST_PC + static_cast<u32>(code.size() - 2) * 4; // code ends with the 'b .' + nop pair
	if (out.pc >= sentinel && out.pc < sentinel + 8) out.pc = sentinel;
	if (out.pc & 0x80000000u) out.pc = 0x80000000u;
	return out;
}

// Standard terminator: 'b .' + delay slot nop (the program must end with it).
static void appendSentinel(std::vector<u32>& c)
{
	c.push_back(0x1000ffffu);
	c.push_back(0);
}

// Comparison helper: compare, record failure under 'group' semantics. Returns true if equal.
static bool compareRecord(const EESnapshot& ref, const EESnapshot& got, const DiffOpt& o, const std::string& tag,
	const std::vector<u32>& code, const GPR_reg* gpr)
{
	std::vector<Dif> d;
	snapDiff(ref, got, o, d);
	if (d.empty()) return true;
	const Dif& f = d[0];
	std::string repro = tag + " " + f.f + " interp=" + hx(f.a) + " jit=" + hx(f.b);
	if (d.size() > 1) repro += " (+" + std::to_string(d.size() - 1) + " more: " + d[1].f + ")";
	repro += " init{" + (gpr ? gprHex(gpr) : std::string()) + "} code{" + codeHex(code) + "}";
	gFail(diffFamily(f.f), repro);
	return false;
}

} // namespace gapee

#endif // EMUCOREX_ENABLE_NATIVE_SELF_TESTS
