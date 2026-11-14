// SPDX-FileCopyrightText: 2026 EmuCoreX Team
// SPDX-License-Identifier: GPL-3.0+
// x86-parity oracle: COP2 macro transfer ops and multi-block EE/VU0 synchronisation, JIT vs interpreter.
// Suites: x86-parity-cop2 (QMFC2/QMTC2/CFC2/CTC2 for every register, LQC2/SQC2, VCALLMS/VCALLMSR with a VU0 micro program,
// interlock .I bits, BC2F/T/FL/TL, macro flag reads with VU0 running) and x86-parity-ee-sync (multi-block EE programs, loops and
// branches containing COP2 interlock/sync points; guest-visible cycle probes at every block start, plus final state).
// The interpreter is the reference where it agrees with the x86 JIT (hardware COP2/VU0 interlock semantics).
// Harness limits: cpuRegs.nextEventCycle is overwritten by the oracle's forced-exit hook, and cpuRegs.cycle/VU0.cycle at the very
// end of a program depend on how long each engine spins in the trailing self-branch, so neither can be compared directly. The cycle
// state is compared through guest-visible probes instead: MFC0 Count (and CFC2 VPU_STAT / VI reads) at the first instruction of
// every block (where the interpreter's pending block cycles are flushed).

#if defined(EMUCOREX_ENABLE_NATIVE_SELF_TESTS) && !defined(NDEBUG)

namespace x86p
{
namespace ee
{
static inline u32 cop2mv(u32 rs, u32 rt, u32 rd, u32 i) { return (0x12u << 26) | (rs << 21) | (rt << 16) | (rd << 11) | (i & 1u); }
static inline u32 QMFC2(u32 rt, u32 vf, u32 i) { return cop2mv(1, rt, vf, i); }
static inline u32 CFC2(u32 rt, u32 vi, u32 i) { return cop2mv(2, rt, vi, i); }
static inline u32 QMTC2(u32 rt, u32 vf, u32 i) { return cop2mv(5, rt, vf, i); }
static inline u32 CTC2(u32 rt, u32 vi, u32 i) { return cop2mv(6, rt, vi, i); }
static inline u32 VCALLMS(u32 addr) { return (0x12u << 26) | (1u << 25) | ((addr / 8u) << 6) | 0x38u; }
static inline u32 VCALLMSR() { return (0x12u << 26) | (1u << 25) | 0x39u; }
static inline u32 LQC2(u32 vf, u32 base, u32 off) { return (0x36u << 26) | (base << 21) | (vf << 16) | (off & 0xffffu); }
static inline u32 SQC2(u32 vf, u32 base, u32 off) { return (0x3eu << 26) | (base << 21) | (vf << 16) | (off & 0xffffu); }
static inline u32 BC2(u32 cc, int off) { return (0x12u << 26) | (8u << 21) | (cc << 16) | (static_cast<u32>(off) & 0xffffu); }
static inline u32 macro(u32 word25) { return (0x12u << 26) | (1u << 25) | (word25 & 0x1ffffffu); }
static inline u32 ADDU(u32 rd, u32 rs, u32 rt) { return MipsR(rs, rt, rd, 0, 0x21); }
static inline u32 SW(u32 rt, u32 base, u32 off) { return MipsI(0x2b, base, rt, off); }
static inline u32 SD(u32 rt, u32 base, u32 off) { return MipsI(0x3f, base, rt, off); }
static inline u32 ADDIU(u32 rt, u32 rs, int imm) { return MipsI(9, rs, rt, static_cast<u32>(imm)); }
static inline u32 BNE(u32 rs, u32 rt, int off) { return MipsI(5, rs, rt, static_cast<u32>(off)); }
static inline u32 BEQ(u32 rs, u32 rt, int off) { return MipsI(4, rs, rt, static_cast<u32>(off)); }
static inline u32 MFC0(u32 rt, u32 rd) { return (0x10u << 26) | (rt << 16) | (rd << 11); }
} // namespace ee

struct EeDiffD { std::string f; u64 a, b; };

static void eeStateDiff(const EESnapshot& a, const EESnapshot& b, std::vector<EeDiffD>& d, bool cmpScratch = true, u32 skipGprMask = 0)
{
	for (u32 i = 1; i < 32; ++i)
	{
		if (skipGprMask & (1u << i)) continue;
		if (a.gpr[i] != b.gpr[i]) d.push_back({"GPR" + std::to_string(i), a.gpr[i], b.gpr[i]});
		if (a.gpr_upper[i] != b.gpr_upper[i]) d.push_back({"GPRhi" + std::to_string(i), a.gpr_upper[i], b.gpr_upper[i]});
	}
	for (u32 i = 0; i < 128; ++i)
		if (a.vu0_vf[i] != b.vu0_vf[i]) { d.push_back({"VF" + std::to_string(i >> 2) + "." + "xyzw"[i & 3], a.vu0_vf[i], b.vu0_vf[i]}); break; }
	for (u32 i = 0; i < 32; ++i)
	{
		if (i == REG_TPC) continue; // micro program counter after a completed run is not architecturally observable via CFC2
		if (a.vu0_vi[i] != b.vu0_vi[i]) d.push_back({"VI" + std::to_string(i), a.vu0_vi[i], b.vu0_vi[i]});
	}
	for (u32 l = 0; l < 4; ++l) if (a.vu0_acc[l] != b.vu0_acc[l]) { d.push_back({std::string("ACC.") + "xyzw"[l], a.vu0_acc[l], b.vu0_acc[l]}); break; }
	if (a.pc != b.pc) d.push_back({"PC", a.pc, b.pc});
	if (cmpScratch)
		for (u32 i = 0; i < EE_SCRATCH_SIZE; i += 4)
		{
			u32 x, y;
			std::memcpy(&x, a.scratch + i, 4);
			std::memcpy(&y, b.scratch + i, 4);
			if (x != y) { d.push_back({"MEM" + std::to_string(i), x, y}); break; }
		}
}

// GPR targets of CFC2 in a program. x86 (and the arm64 JIT) sign-extend the 32-bit VI value into the whole 64-bit GPR; this interpreter only
// writes the low word (UL[0]) and keeps the stale upper half, so for these registers only the low 32 bits are compared.
static u32 cfc2TargetMask(const std::vector<u32>& code)
{
	u32 m = 0;
	for (u32 w : code)
		if ((w >> 26) == 0x12u && ((w >> 21) & 0x1fu) == 2u) m |= 1u << ((w >> 16) & 0x1fu);
	return m;
}

static std::string eeProgHex(const std::vector<u32>& code)
{
	std::string s;
	for (size_t i = 0; i < code.size() && i < 90; ++i) s += hex(code[i]) + " ";
	return s;
}

static void eeRecordDiffs(const char* suite, const std::string& kind, const std::vector<EeDiffD>& d, const std::vector<u32>& code, const std::string& tag)
{
	int shown = 0;
	for (const EeDiffD& x : d)
	{
		if (++shown > 3) break;
		std::string fam = x.f;
		if (fam.rfind("GPRhi", 0) == 0) fam = "GPRhi";
		else if (fam.rfind("GPR", 0) == 0) fam = "GPR";
		else if (fam.rfind("MEM", 0) == 0) fam = "MEM";
		else if (fam.rfind("VF", 0) == 0) fam = "VF";
		else if (fam.rfind("CYCLE", 0) == 0) fam = "CYCLE-PROBE";
		const std::string key = std::string(suite) + " " + kind + " [" + fam + "]";
		char ex[1800];
		std::snprintf(ex, sizeof(ex), "%s interp=%llx jit=%llx tag{%s} code{%s}", x.f.c_str(), static_cast<unsigned long long>(x.a), static_cast<unsigned long long>(x.b), tag.c_str(), eeProgHex(code).c_str());
		record(key, ex, std::string(suite) + " " + fam, kind);
	}
}

static void prepareVu0Micro(const std::vector<std::pair<u32, std::array<u32, 3>>>& progs);
static void prepareVu0MicroFwd(const std::vector<std::pair<u32, std::array<u32, 3>>>& progs) { prepareVu0Micro(progs); }

// Load a small VU0 micro program: 'delay' NOP pairs, a few register writes, E-bit. Returns the byte size used.
static u32 loadVu0Micro(u32 at, u32 delay, u32 flavor, u32 imm)
{
	VProg p;
	// A branch loop (not a straight NOP run): the micro VU executes a whole straight-line block at once, which would finish the
	// program inside VCALLMS and never make the EE wait; loop iterations are separate blocks the EE has to wait for at an interlock.
	if (delay >= 4)
	{
		// Chain of unconditional forward branches: every branch ends a microVU block, so the VU0 stops at the cycle budget between
		// blocks and the EE has to run/wait for the remainder at an interlock. (A VI-compare loop is not usable here: see report.)
		for (u32 i = 0; i < delay / 2; ++i)
		{
			p.lower(enc::B(1));
			p.nop(1);
		}
	}
	else p.nop(delay);
	switch (flavor % 4)
	{
		case 0: p.lower(enc::IADDIU(1, 0, imm & 0x7fff)); break;
		case 1: p.lower(enc::IADDIU(1, 1, imm & 0x7ff)); p.lower(enc::IADDIU(2, 1, 3)); break;
		case 2: p.upper(upperWord("ADD", 0xf, 1, 2, 3)); p.lower(enc::IADDIU(1, 0, 0x55)); break;
		default: p.upper(upperWord("MUL", 0xf, 4, 5, 6)); p.lower(enc::ISUBIU(2, 1, 1)); p.lower(enc::MTIR(0, 3, 4)); break;
	}
	p.pair(NOP_L, NOP_U | kEbit);
	p.nop(1);
	std::memcpy(VU0.Micro + at, p.w.data(), p.w.size() * 4);
	return static_cast<u32>(p.w.size() * 4);
}

// Runs an EE program with the cycle-accurate VU0 INTERPRETER as the micro engine (CpuVU0), for both the EE interpreter and the EE
// recompiler. The default microVU0 JIT backend runs a short micro program to completion inside VCALLMS, so the EE never has to wait for it
// and the COP2 interlock helpers never add VU0 cycles to cpuRegs.cycle; with the interpreter backend the EE really waits, which is the
// situation that exposes a pinned-cycle-register (W24) that is not reloaded after _vu0WaitMicro/_vu0FinishMicro/ExecuteBlockJIT.
static EESnapshot runEeWithIntVu0(bool jit, const std::vector<std::pair<u32, std::array<u32, 3>>>& micro, const std::vector<u32>& code, const GPR_reg* gpr)
{
	BaseVUmicroCPU* const saved = CpuVU0;
	CpuVU0 = &CpuIntVU0;
	CpuVU0->Reset();
	prepareVu0MicroFwd(micro);
	// The real EE interpreter runs with EnableEE=false: _vu0run then flushes the pending block cycles (intUpdateCPUCycles) before it waits.
	const bool savedEnableEE = EmuConfig.Cpu.Recompiler.EnableEE;
	EmuConfig.Cpu.Recompiler.EnableEE = jit;
	const EESnapshot snap = RunEEProgram(jit, code, true, nullptr, nullptr, gpr);
	EmuConfig.Cpu.Recompiler.EnableEE = savedEnableEE;
	CpuVU0 = saved;
	return snap;
}

static void prepareVu0Micro(const std::vector<std::pair<u32, std::array<u32, 3>>>& progs)
{
	std::memset(VU0.Micro, 0, 4096);
	for (u32 i = 0; i < 4096; i += 8)
	{
		std::memcpy(VU0.Micro + i, &NOP_L, 4);
		std::memcpy(VU0.Micro + i + 4, &NOP_U, 4);
	}
	for (const auto& p : progs) loadVu0Micro(p.first, p.second[0], p.second[1], p.second[2]);
	CpuVU0->Clear(0, 4096);
}

static void eeRandomGpr(GPR_reg* g, Rng& r)
{
	std::memset(g, 0, sizeof(GPR_reg) * 32);
	for (u32 i = 1; i < 32; ++i)
	{
		const u32 k = r.below(4);
		for (u32 h = 0; h < 2; ++h)
		{
			const u32 a = k == 0 ? kVals[r.below(kN)] : refNormal(r), b = k == 0 ? kVals[r.below(kN)] : refNormal(r);
			g[i].UD[h] = (static_cast<u64>(b) << 32) | a;
		}
	}
	g[16].UD[0] = EE_TEST_SCRATCH;
	g[16].UD[1] = 0;
}

//------------------------------------------------------------------
// x86-parity-cop2
//------------------------------------------------------------------
static void runCop2Suite(u32 count)
{
	static const RefCfg dummy = {"x86-parity-cop2"};
	(void)dummy;
	configure(1, 0);
	static const char* const names[8] = {"QMFC2/QMTC2 all VF", "CFC2/CTC2 all VI", "LQC2/SQC2", "VCALLMS+sync", "VCALLMSR+CTC2 CMSAR0", "BC2F/T/FL/TL", "macro op + CFC2 flags", "mixed"};
	const auto& ops = upperOps();
	for (u32 variant = 0; variant < 8; ++variant)
	{
		for (u32 k = 0; k < count; ++k)
		{
			Rng r(0xc02c0000ull + k * 7919ull + variant * 131ull);
			GPR_reg gpr[32];
			eeRandomGpr(gpr, r);
			std::vector<u32> code;
			const u32 ndelay = 1 + r.below(30);
			prepareVu0Micro({{0, {ndelay, r.below(4), r.next()}}, {0x200, {1 + r.below(20), r.below(4), r.next()}}});
			const auto sync = [&]() { code.push_back(ee::QMFC2(25, 0, 1)); }; // interlocked read waits for the micro program
			const u32 n = 2 + r.below(8);
			const auto item = [&](u32 v) {
				const u32 i = r.below(2);
				switch (v)
				{
					case 0:
						if (r.below(2)) code.push_back(ee::QMTC2(1 + r.below(15), r.below(32), i));
						else code.push_back(ee::QMFC2(1 + r.below(15), r.below(32), i));
						break;
					case 1:
					{
						static const u32 viSet[] = {0, 1, 2, 3, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 29, 30};
						const u32 vi = viSet[r.below(sizeof(viSet) / sizeof(viSet[0]))];
						if (r.below(2)) code.push_back(ee::CTC2(1 + r.below(15), vi, i));
						else code.push_back(ee::CFC2(1 + r.below(15), vi, i));
						break;
					}
					case 2:
						if (r.below(2)) code.push_back(ee::LQC2(r.below(32), 16, (r.below(15)) * 16));
						else code.push_back(ee::SQC2(r.below(32), 16, (r.below(15)) * 16));
						break;
					case 3: code.push_back(ee::VCALLMS(0)); break;
					case 4: code.push_back(ee::CTC2(17, 27, r.below(2))); code.push_back(ee::VCALLMSR()); break; // CMSAR0 = a GPR that holds an address
					case 5:
					{
						code.push_back(ee::BC2(r.below(4), 2));
						code.push_back(ee::ADDIU(20, 20, 1));                       // delay slot
						code.push_back(ee::ADDIU(21, 21, 1));                       // skipped when taken
						code.push_back(ee::ADDIU(22, 22, 1));
						break;
					}
					case 6:
					{
						u32 op;
						do op = r.below(static_cast<u32>(ops.size())); while (ops[op].kind == K_FTOI || ops[op].kind == K_ITOF);
						code.push_back(ee::macro(encodeUpper(ops[op], 1 + r.below(15), 1 + r.below(6), 1 + r.below(6), 1 + r.below(6))));
						code.push_back(ee::CFC2(1 + r.below(15), 16 + r.below(3), r.below(2)));
						break;
					}
					default: break;
				}
			};
			gpr[17].UD[0] = (r.below(2) ? 0u : 0x200u); // address for CMSAR0
			for (u32 j = 0; j < n; ++j)
			{
				item(variant == 7 ? r.below(7) : variant);
				for (u32 e = r.below(6); e > 0; --e) code.push_back(ee::ADDU(26, 26, 1)); // EE instructions while VU0 runs
			}
			sync();
			code.push_back(ee::CFC2(24, REG_VPU_STAT, 1));
			const EESnapshot interp = RunEEProgram(false, code, true, nullptr, nullptr, gpr);
			const EESnapshot jit = RunEEProgram(true, code, true, nullptr, nullptr, gpr);
			++g_cases;
			++g_groupCases;
			std::vector<EeDiffD> d;
			const u32 cfcMask = cfc2TargetMask(code);
			eeStateDiff(interp, jit, d, true, cfcMask);
			for (u32 i = 1; i < 32; ++i)
				if ((cfcMask & (1u << i)) && static_cast<u32>(interp.gpr[i]) != static_cast<u32>(jit.gpr[i])) d.push_back({"GPR" + std::to_string(i) + "(CFC2 low32)", static_cast<u32>(interp.gpr[i]), static_cast<u32>(jit.gpr[i])});
			if (!d.empty()) eeRecordDiffs("x86-parity-cop2", names[variant], d, code, "k=" + std::to_string(k));
		}
		char nm[96];
		std::snprintf(nm, sizeof(nm), "cop2 %s", names[variant]);
		endGroup(nm);
	}
}

//------------------------------------------------------------------
// x86-parity-ee-sync: multi-block programs with COP2 sync points; cycle probes at block starts
//------------------------------------------------------------------
// Scratch layout: one word per probe / result, written through a moving pointer ($s0). 'probeSlots' lists the words that hold MFC0 Count
// readings; everything else is compared exactly. Count readings are compared against the interpreter (run with EnableEE=false like the
// real interpreter) under an x86-parity model, see the cycle-probe comment below.
struct SyncCase { std::vector<u32> code; std::vector<u32> probeSlots; };

static void runEeSyncSuite(u32 count)
{
	configure(1, 0);
	static const char* const names[4] = {"loop+VCALLMS+CTC2/CFC2.I", "loop+QMFC2.I/BC2", "branch ladder + VCALLMS + hw store", "Twinsanity VI1 handshake"};
	s32 maxSeen[4] = {0, 0, 0, 0}, pi1Seen[4] = {0, 0, 0, 0};
	const bool strict = envKnob("X86P_SYNCSTRICT", 0) != 0;
	for (u32 variant = 0; variant < 4; ++variant)
	{
		for (u32 k = 0; k < count; ++k)
		{
			Rng r(0x5c1c0000ull + k * 7919ull + variant * 131ull);
			GPR_reg gpr[32];
			eeRandomGpr(gpr, r);
			gpr[19].UD[0] = 0x10000010ull + (static_cast<u64>(r.below(4)) << 11); // T0..T3_MODE
			static const u32 kDelays[] = {2, 12, 40, 90, 160, 300};
			const std::vector<std::pair<u32, std::array<u32, 3>>> micro = {{0, {(envKnob("X86P_SYNCDELAY",0)?envKnob("X86P_SYNCDELAY",0):kDelays[r.below(6)]), r.below(4), r.next()}}};
			SyncCase sc;
			std::vector<u32>& code = sc.code;
			u32 slot = 0;
			const auto store = [&](u32 reg, bool isProbe) {
				code.push_back(ee::SW(reg, 16, 0));
				code.push_back(ee::ADDIU(16, 16, 4));
				(void)isProbe;
				++slot;
			};
			const auto probe = [&]() { code.push_back(ee::MFC0(8, 9)); store(8, true); };
			const u32 iters = 2 + r.below(3);
			code.push_back(ee::ADDIU(18, 0, static_cast<int>(iters)));
			const u32 loopTop = static_cast<u32>(code.size());
			probe();
			bool lazySync = false; // a CTC2/CFC2 without the .I bit: the recompiler (like x86) does not sync VU0 there, the interpreter does (vu0Sync)
			const auto syncItem = [&]() {
				const u32 i = r.below(2);
				if (variant == 0 && i == 0) lazySync = true;
				switch (variant)
				{
					case 0:
						code.push_back(ee::VCALLMS(0));
						for (u32 e = r.below(6); e > 0; --e) code.push_back(ee::ADDU(26, 26, 1));
						if (r.below(2)) { code.push_back(ee::CTC2(9, 1, i)); code.push_back(ee::CFC2(10, 1, i)); }
						else { code.push_back(ee::CFC2(10, 1, i)); code.push_back(ee::CTC2(9, 1, i)); }
						break;
					case 1:
						code.push_back(ee::VCALLMS(0));
						for (u32 e = r.below(6); e > 0; --e) code.push_back(ee::ADDU(26, 26, 1));
						code.push_back(ee::QMFC2(10, 0, 1));
						code.push_back(ee::BC2(r.below(4), 1));
						code.push_back(ee::ADDIU(20, 20, 1));
						break;
					case 2:
						code.push_back(ee::VCALLMS(0));
						code.push_back(ee::SW(0, 19, 0)); // hardware register store (EE timer MODE): goes through the hwWrite helper and may reschedule events
						for (u32 e = r.below(6); e > 0; --e) code.push_back(ee::ADDU(27, 27, 1));
						code.push_back(ee::CFC2(10, 1, 1));
						break;
					default: // Twinsanity: EE writes VI1 interlocked while VU0 microcode also writes VI1
						code.push_back(ee::CTC2(9, 1, 0));
						code.push_back(ee::VCALLMS(0));
						for (u32 e = r.below(6); e > 0; --e) code.push_back(ee::ADDU(26, 26, 1));
						code.push_back(ee::CTC2(11, 1, 1));
						code.push_back(ee::CFC2(10, 1, 1));
						break;
				}
				store(10, false);
			};
			syncItem();
			// forward branch over a filler -> new block (target is a fresh block start, never the sequential next instruction)
			code.push_back(ee::BEQ(0, 0, 2));
			code.push_back(0);
			code.push_back(ee::ADDIU(23, 23, 1)); // skipped filler
			probe();
			if (variant != 3) syncItem();
			// Every block boundary below is a TAKEN branch: the interpreter only flushes its pending block cycles on taken branches,
			// so Count at a not-taken boundary would not be comparable with the JIT.
			code.push_back(ee::ADDIU(18, 18, -1));
			code.push_back(ee::BEQ(18, 0, 3)); // exit (taken only on the last iteration)
			code.push_back(0);
			code.push_back(ee::BEQ(0, 0, -static_cast<int>(code.size() + 1 - loopTop))); // back edge (always taken)
			code.push_back(0);
			probe();
			code.push_back(ee::QMFC2(25, 0, 1)); // final interlocked sync
			code.push_back(ee::CFC2(24, REG_VPU_STAT, 1));
			store(24, false);
			// Dynamic probe slots: per iteration [probeA, syncA, probeB, (syncB)], then [probeC, final VPU_STAT].
			{
				const u32 perIter = variant != 3 ? 4 : 3;
				for (u32 it = 0; it < iters; ++it) { sc.probeSlots.push_back(it * perIter); sc.probeSlots.push_back(it * perIter + 2); }
				sc.probeSlots.push_back(iters * perIter);
			}
			const EESnapshot interp = runEeWithIntVu0(false, micro, code, gpr);
			const EESnapshot jit = runEeWithIntVu0(true, micro, code, gpr);
			if (envKnob("X86P_SYNCDBG", 0) && k < envKnob("X86P_SYNCDBG", 0))
			{
				std::printf("SYNCDBG v%u k%u iters=%u probes int:", variant, k, iters);
				for (u32 w : sc.probeSlots) { u32 x; std::memcpy(&x, interp.scratch + w * 4, 4); std::printf(" %x", x); }
				std::printf(" | jit:");
				for (u32 w : sc.probeSlots) { u32 x; std::memcpy(&x, jit.scratch + w * 4, 4); std::printf(" %x", x); }
				std::printf("\n");
			}
			++g_cases;
			++g_groupCases;
			// Exact state compare (scratch handled separately so Count slots are excluded).
			std::vector<EeDiffD> d;
			eeStateDiff(interp, jit, d, false, (1u << 8) | (1u << 10) | (1u << 24) | (1u << 25)); // $t0 = last Count reading; $t2/$t8/$t9 = CFC2/QMFC2 results, which are stored to (and compared in) scratch
			for (u32 w = 0; w < EE_SCRATCH_SIZE / 4; ++w)
			{
				if (std::find(sc.probeSlots.begin(), sc.probeSlots.end(), w) != sc.probeSlots.end()) continue;
				u32 x, y;
				std::memcpy(&x, interp.scratch + w * 4, 4);
				std::memcpy(&y, jit.scratch + w * 4, 4);
				if (x != y) { d.push_back({"MEM" + std::to_string(w * 4), x, y}); break; }
			}
			// Cycle probes. Target is x86-JIT parity; the EE interpreter is only a proxy and differs from the x86 recompiler in one defined way:
			// the interpreter starts a micro program (vu0ExecMicro: VU0.cycle = cpuRegs.cycle) with cpuRegs.cycle lacking the pending cycles of the
			// block it is executing (they are only flushed at the next taken branch), whereas the recompiler - like upstream x86 iR5900/microVU
			// (mVUAddBlockCyclesToCpuCycle before VCALLMS) - adds the block cycles first. So after every VCALLMS that the EE then has to wait for,
			// jit - interp is positive by up to the cycles of the instructions that precede VCALLMS in its block (one MFC0/SW/ADDIU probe, <= 13).
			// A lost or unreloaded VU0 wait (pinned W24 bug) makes the JIT lag instead, by the micro program's remaining run time, so the negative
			// side keeps a tight bound: the recompiler (like x86) drops the fractional 1/8 cycle of s_nBlockCycles at every block end while the
			// interpreter carries it (cpuBlockCycles &= 7), so jit - interp falls by < 1 cycle per block boundary (about one per probe).
			// Lazy-sync programs (a CTC2/CFC2 without .I, VU0 left running into the next VCALLMS): the interpreter's vu0Sync lets VU0 finish by itself,
			// while recompiler and x86 resolve it in the next VCALLMS' _vu0FinishMicro, which sets cpuRegs.cycle to the VU0 end time even when that
			// lies behind the EE (upstream _vu0run code, shared by both). The JIT then lags by the EE time elapsed since the micro start (<= ~40).
			const u32 perVc = variant != 3 ? 2 : 1;
			const u32 itersTotal = static_cast<u32>(sc.probeSlots.size() - 1) / 2;
			for (size_t pi = 0; pi < sc.probeSlots.size(); ++pi)
			{
				u32 x, y;
				std::memcpy(&x, interp.scratch + sc.probeSlots[pi] * 4, 4);
				std::memcpy(&y, jit.scratch + sc.probeSlots[pi] * 4, 4);
				const s32 delta = static_cast<s32>(y - x);
				const u32 it = static_cast<u32>(pi / 2);
				const s32 nVc = static_cast<s32>(it >= itersTotal ? itersTotal * perVc : it * perVc + static_cast<u32>(pi & 1));
				const s32 lo = -(3 + static_cast<s32>(pi)) - (lazySync ? 40 * nVc : 0);
				const s32 hi = strict ? 2 + 14 * nVc : 4 + 20 * nVc;
				if (delta > maxSeen[variant]) maxSeen[variant] = delta;
				if (-delta > maxSeen[variant]) maxSeen[variant] = -delta;
				if (pi == 1 && std::abs(delta) > pi1Seen[variant]) pi1Seen[variant] = std::abs(delta);
				if (delta > hi || delta < lo) { d.push_back({"CYCLE-PROBE" + std::to_string(pi), x, y}); break; }
			}
			if (!d.empty()) eeRecordDiffs("x86-parity-ee-sync", names[variant], d, code, "k=" + std::to_string(k));
		}
		std::printf("SYNCSTAT %s: max |jit-interp| cycle-probe delta = %d, at first block-start probe after a sync = %d\n", names[variant], maxSeen[variant], pi1Seen[variant]);
		char nm[96];
		std::snprintf(nm, sizeof(nm), "ee-sync %s", names[variant]);
		endGroup(nm);
	}
}

} // namespace x86p

#endif // EMUCOREX_ENABLE_NATIVE_SELF_TESTS
