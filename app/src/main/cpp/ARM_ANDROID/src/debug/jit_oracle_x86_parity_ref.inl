// SPDX-FileCopyrightText: 2026 EmuCoreX Team
// SPDX-License-Identifier: GPL-3.0+
// x86-parity oracle, interpreter-referenced micro-mode suites for behaviour where the PCSX2 interpreter and the x86 microVU agree
// (hardware pipeline semantics): flag pipeline timing, integer ops, branches/delay slots, VU memory ops, flag-register ops,
// Q/P latency, E/M/D/T/I bits. The arm64 JIT is compared with CpuIntVU0/1 over randomised programs; operand data is kept
// finite/normal-range so the O/U/NaN differences between the interpreter and the x86 JIT (modelled in the other suites) do not
// leak in. Every mismatch FAILs; the allowlist stays empty.
// Suites: x86-parity-flags, x86-parity-vu-int, x86-parity-vu-branch, x86-parity-vu-mem, x86-parity-vu-misc.

#if defined(EMUCOREX_ENABLE_NATIVE_SELF_TESTS) && !defined(NDEBUG)

namespace x86p
{
namespace enc
{
static inline u32 i11(int v) { return static_cast<u32>(v) & 0x7ffu; }
static inline u32 LQ(u32 dest, u32 ft, u32 is, int imm) { return (0x00u << 25) | (dest << 21) | (ft << 16) | (is << 11) | i11(imm); }
static inline u32 SQ(u32 dest, u32 fs, u32 it, int imm) { return (0x01u << 25) | (dest << 21) | (it << 16) | (fs << 11) | i11(imm); }
static inline u32 ILW(u32 dest, u32 it, u32 is, int imm) { return (0x04u << 25) | (dest << 21) | (it << 16) | (is << 11) | i11(imm); }
static inline u32 ISW(u32 dest, u32 it, u32 is, int imm) { return (0x05u << 25) | (dest << 21) | (it << 16) | (is << 11) | i11(imm); }
static inline u32 imm15(u32 op, u32 it, u32 is, u32 v) { return (op << 25) | (((v >> 11) & 0xfu) << 21) | (it << 16) | (is << 11) | (v & 0x7ffu); }
static inline u32 IADDIU(u32 it, u32 is, u32 v) { return imm15(0x08, it, is, v); }
static inline u32 ISUBIU(u32 it, u32 is, u32 v) { return imm15(0x09, it, is, v); }
static inline u32 imm12(u32 op, u32 it, u32 v) { return (op << 25) | (((v >> 11) & 1u) << 21) | (it << 16) | (v & 0x7ffu); }
static inline u32 FSEQ(u32 it, u32 v) { return imm12(0x14, it, v); }
static inline u32 FSSET(u32 v) { return imm12(0x15, 0, v); }
static inline u32 FSAND(u32 it, u32 v) { return imm12(0x16, it, v); }
static inline u32 FSOR(u32 it, u32 v) { return imm12(0x17, it, v); }
static inline u32 imm24(u32 op, u32 v) { return (op << 25) | (v & 0xffffffu); }
static inline u32 FCEQ(u32 v) { return imm24(0x10, v); }
static inline u32 FCSET(u32 v) { return imm24(0x11, v); }
static inline u32 FCAND(u32 v) { return imm24(0x12, v); }
static inline u32 FCOR(u32 v) { return imm24(0x13, v); }
static inline u32 FMEQ(u32 it, u32 is) { return (0x18u << 25) | (it << 16) | (is << 11); }
static inline u32 FMAND(u32 it, u32 is) { return (0x1au << 25) | (it << 16) | (is << 11); }
static inline u32 FMOR(u32 it, u32 is) { return (0x1bu << 25) | (it << 16) | (is << 11); }
static inline u32 FCGET(u32 it) { return (0x1cu << 25) | (it << 16); }
static inline u32 B(int d) { return (0x20u << 25) | i11(d); }
static inline u32 BAL(u32 it, int d) { return (0x21u << 25) | (it << 16) | i11(d); }
static inline u32 JR(u32 is) { return (0x24u << 25) | (is << 11); }
static inline u32 JALR(u32 it, u32 is) { return (0x25u << 25) | (it << 16) | (is << 11); }
static inline u32 BR(u32 op, u32 it, u32 is, int d) { return (op << 25) | (it << 16) | (is << 11) | i11(d); }
static inline u32 IBEQ(u32 it, u32 is, int d) { return BR(0x28, it, is, d); }
static inline u32 IBNE(u32 it, u32 is, int d) { return BR(0x29, it, is, d); }
static inline u32 IBLTZ(u32 is, int d) { return BR(0x2c, 0, is, d); }
static inline u32 IBGTZ(u32 is, int d) { return BR(0x2d, 0, is, d); }
static inline u32 IBLEZ(u32 is, int d) { return BR(0x2e, 0, is, d); }
static inline u32 IBGEZ(u32 is, int d) { return BR(0x2f, 0, is, d); }
static inline u32 sp(u32 dest, u32 ft, u32 fs, u32 idx, u32 fn) { return (0x40u << 25) | (dest << 21) | (ft << 16) | (fs << 11) | (idx << 6) | fn; }
static inline u32 IADD(u32 id, u32 is, u32 it) { return sp(0, it, is, id, 0x30); }
static inline u32 ISUB(u32 id, u32 is, u32 it) { return sp(0, it, is, id, 0x31); }
static inline u32 IADDI(u32 it, u32 is, int imm5) { return sp(0, it, is, static_cast<u32>(imm5) & 31u, 0x32); }
static inline u32 IAND(u32 id, u32 is, u32 it) { return sp(0, it, is, id, 0x34); }
static inline u32 IOR(u32 id, u32 is, u32 it) { return sp(0, it, is, id, 0x35); }
static inline u32 MOVE(u32 dest, u32 ft, u32 fs) { return sp(dest, ft, fs, 12, 0x3c); }
static inline u32 LQI(u32 dest, u32 ft, u32 is) { return sp(dest, ft, is, 13, 0x3c); }
static inline u32 MTIR(u32 fsf, u32 it, u32 fs) { return sp(fsf, it, fs, 15, 0x3c); }
static inline u32 XTOP(u32 it) { return sp(0, it, 0, 26, 0x3c); }
static inline u32 MR32(u32 dest, u32 ft, u32 fs) { return sp(dest, ft, fs, 12, 0x3d); }
static inline u32 SQI(u32 dest, u32 fs, u32 it) { return sp(dest, it, fs, 13, 0x3d); }
static inline u32 MFIR(u32 dest, u32 ft, u32 is) { return sp(dest, ft, is, 15, 0x3d); }
static inline u32 XITOP(u32 it) { return sp(0, it, 0, 26, 0x3d); }
static inline u32 LQD(u32 dest, u32 ft, u32 is) { return sp(dest, ft, is, 13, 0x3e); }
static inline u32 ILWR(u32 dest, u32 it, u32 is) { return sp(dest, it, is, 15, 0x3e); }
static inline u32 SQD(u32 dest, u32 fs, u32 it) { return sp(dest, it, fs, 13, 0x3f); }
static inline u32 WAITQ() { return sp(0, 0, 0, 14, 0x3f); }
static inline u32 ISWR(u32 dest, u32 it, u32 is) { return sp(dest, it, is, 15, 0x3f); }
static inline u32 WAITP() { return sp(0, 0, 0, 30, 0x3f); }
static inline u32 DIVq(u32 fsf, u32 ftf, u32 ft, u32 fs) { return sp((ftf << 2) | fsf, ft, fs, 14, 0x3c); }
static inline u32 SQRTq(u32 ftf, u32 ft) { return sp(ftf << 2, ft, 0, 14, 0x3d); }
static inline u32 RSQRTq(u32 fsf, u32 ftf, u32 ft, u32 fs) { return sp((ftf << 2) | fsf, ft, fs, 14, 0x3e); }
static inline u32 MFP(u32 dest, u32 ft) { return sp(dest, ft, 0, 25, 0x3c); }
static inline u32 ESIN(u32 fsf, u32 fs) { return sp(fsf, 0, fs, 31, 0x3c); }
static inline u32 ESQRT(u32 fsf, u32 fs) { return sp(fsf, 0, fs, 30, 0x3c); }
static inline u32 ESUMv(u32 fs) { return sp(0xf, 0, fs, 29, 0x3e); }
} // namespace enc

constexpr u32 kEbit = 0x40000000u, kMbit = 0x20000000u, kDbit = 0x10000000u, kTbit = 0x08000000u, kIbit = 0x80000000u;

struct VProg
{
	std::vector<u32> w; // lower, upper pairs
	u32 size() const { return static_cast<u32>(w.size() / 2); }
	void pair(u32 lo, u32 up) { w.push_back(lo); w.push_back(up); }
	void lower(u32 lo) { pair(lo, NOP_U); }
	void upper(u32 up) { pair(NOP_L, up); }
	void nop(u32 n = 1) { for (u32 i = 0; i < n; ++i) pair(NOP_L, NOP_U); }
};

static inline u32 upperWord(const char* name, u32 mask, u32 fs, u32 ft, u32 fd) { return encodeUpper(upperOps()[findUpper(name)], mask, fs, ft, fd); }

// RefInit: everything RunVU needs to start from a deterministic state.
struct RefInit
{
	FmacInit fm;
	u32 vi[16];
};

struct RefObs
{
	Snapshot s;
	u32 vu1vf[128];
	u32 vu1vi[32];
};

static void refExec(int index, bool jit, const VProg& prog, const RefInit& init, RefObs& out)
{
	std::vector<u32> words = prog.w;
	words.push_back(NOP_L); words.push_back(NOP_U);
	words.push_back(NOP_L); words.push_back(NOP_U);
	std::memset(&VU1.VF, 0, sizeof(VU1.VF));
	for (u32 r = 0; r < 32; ++r) for (u32 l = 0; l < 4; ++l) VU1.VF[r].UL[l] = 0xa5000000u + r * 16 + l; // VU0 reaches these through 0x4xxx
	for (u32 r = 0; r < 32; ++r) VU1.VI[r].UL = 0x00005a00u + r;
	s_fmac_init = &init.fm;
	out.s = RunVU(index, jit, words, 0x80, 0, 512, false, nullptr, init.fm.vf, nullptr, init.vi);
	s_fmac_init = nullptr;
	std::memcpy(out.vu1vf, VU1.VF, sizeof(out.vu1vf));
	for (u32 r = 0; r < 32; ++r) out.vu1vi[r] = VU1.VI[r].UL;
}

struct RefCfg
{
	const char* suite;
	bool cmpCycle = false; // the JIT keeps its own (block based) cycle accounting; cycles are only compared where both sides model them (ee-sync)
	bool cmpFlags = true;
	bool cmpPQ = true;
};

static std::string progHex(const VProg& p)
{
	std::string s;
	for (size_t i = 0; i + 1 < p.w.size() && i < 120; i += 2) s += hex(p.w[i]) + "/" + hex(p.w[i + 1]) + " ";
	return s;
}

// One interpreter-vs-JIT case. 'kind' groups the mismatch report; 'tag' is reproduction info.
static void refCase(const RefCfg& cfg, int index, const std::string& kind, const VProg& prog, const RefInit& init, const std::string& tag)
{
	RefObs e{}, g{};
	refExec(index, false, prog, init, e);
	refExec(index, true, prog, init, g);
	++g_cases;
	++g_groupCases;
	struct D { std::string f; u32 a, b; };
	std::vector<D> d;
	for (u32 i = 0; i < 128; ++i) if (e.s.regs.VF[i >> 2].UL[i & 3] != g.s.regs.VF[i >> 2].UL[i & 3]) d.push_back({"VF" + std::to_string(i >> 2) + "." + "xyzw"[i & 3], e.s.regs.VF[i >> 2].UL[i & 3], g.s.regs.VF[i >> 2].UL[i & 3]});
	for (u32 i = 0; i < 16; ++i) if ((e.s.regs.VI[i].UL & 0xffff) != (g.s.regs.VI[i].UL & 0xffff)) d.push_back({"VI" + std::to_string(i), e.s.regs.VI[i].UL & 0xffff, g.s.regs.VI[i].UL & 0xffff});
	const u32* ea = reinterpret_cast<const u32*>(&e.s.regs.ACC);
	const u32* ga = reinterpret_cast<const u32*>(&g.s.regs.ACC);
	for (u32 l = 0; l < 4; ++l) if (ea[l] != ga[l]) d.push_back({std::string("ACC.") + "xyzw"[l], ea[l], ga[l]});
	if (cfg.cmpFlags)
	{
		if (e.s.regs.VI[REG_STATUS_FLAG].UL != g.s.regs.VI[REG_STATUS_FLAG].UL) d.push_back({"STATUS", e.s.regs.VI[REG_STATUS_FLAG].UL, g.s.regs.VI[REG_STATUS_FLAG].UL});
		if (e.s.regs.VI[REG_MAC_FLAG].UL != g.s.regs.VI[REG_MAC_FLAG].UL) d.push_back({"MAC", e.s.regs.VI[REG_MAC_FLAG].UL, g.s.regs.VI[REG_MAC_FLAG].UL});
		if (e.s.regs.VI[REG_CLIP_FLAG].UL != g.s.regs.VI[REG_CLIP_FLAG].UL) d.push_back({"CLIP", e.s.regs.VI[REG_CLIP_FLAG].UL, g.s.regs.VI[REG_CLIP_FLAG].UL});
	}
	if (cfg.cmpPQ)
	{
		if (e.s.regs.VI[REG_Q].UL != g.s.regs.VI[REG_Q].UL) d.push_back({"Q", e.s.regs.VI[REG_Q].UL, g.s.regs.VI[REG_Q].UL});
		if (index == 1 && e.s.regs.VI[REG_P].UL != g.s.regs.VI[REG_P].UL) d.push_back({"P", e.s.regs.VI[REG_P].UL, g.s.regs.VI[REG_P].UL});
	}
	if (e.s.regs.VI[REG_I].UL != g.s.regs.VI[REG_I].UL) d.push_back({"I", e.s.regs.VI[REG_I].UL, g.s.regs.VI[REG_I].UL});
	if (e.s.regs.VI[REG_R].UL != g.s.regs.VI[REG_R].UL) d.push_back({"R", e.s.regs.VI[REG_R].UL, g.s.regs.VI[REG_R].UL});
	for (u32 i = 0; i < e.s.memory.size(); ++i)
		if (e.s.memory[i] != g.s.memory[i]) { d.push_back({"MEM@" + hex(i & ~3u), static_cast<u32>(e.s.memory[i & ~3u]) | (static_cast<u32>(e.s.memory[(i & ~3u) + 1]) << 8), static_cast<u32>(g.s.memory[i & ~3u]) | (static_cast<u32>(g.s.memory[(i & ~3u) + 1]) << 8)}); break; }
	if (index == 0)
	{
		for (u32 i = 0; i < 128; ++i) if (e.vu1vf[i] != g.vu1vf[i]) { d.push_back({"VU1.VF" + std::to_string(i >> 2) + "." + "xyzw"[i & 3], e.vu1vf[i], g.vu1vf[i]}); break; }
		for (u32 i = 0; i < 32; ++i) if (i != REG_TPC && i != REG_VPU_STAT && e.vu1vi[i] != g.vu1vi[i]) { d.push_back({"VU1.VI" + std::to_string(i), e.vu1vi[i], g.vu1vi[i]}); break; }
	}
	if (cfg.cmpCycle && e.s.regs.cycle != g.s.regs.cycle) d.push_back({"CYCLE", e.s.regs.cycle, g.s.regs.cycle});
	if (e.s.busy != g.s.busy) d.push_back({"BUSY", e.s.busy, g.s.busy});
	if (d.empty()) return;
	int shown = 0;
	for (const D& x : d)
	{
		if (++shown > 3) break;
		// Group by field family so VF0..VF31 do not explode the key space.
		std::string fam = x.f;
		if (fam.rfind("VF", 0) == 0) fam = "VF";
		else if (fam.rfind("MEM", 0) == 0) fam = "MEM";
		else if (fam.rfind("VU1.VF", 0) == 0) fam = "VU1.VF";
		const std::string key = std::string(cfg.suite) + " vu" + std::to_string(index) + " " + kind + " [" + fam + "]";
		char ex[1700];
		std::snprintf(ex, sizeof(ex), "%s interp=%08x jit=%08x tag{%s} prog{%s}", x.f.c_str(), x.a, x.b, tag.c_str(), progHex(prog).c_str());
		record(key, ex, std::string(cfg.suite) + " vu" + std::to_string(index) + " " + fam, kind);
	}
}

//------------------------------------------------------------------
// Data helpers
//------------------------------------------------------------------
static u32 refNormal(Rng& r) { return (r.next() & 0x807fffffu) | ((118 + r.below(14)) << 23); }

static void refInitData(RefInit& ri, Rng& r, bool randomFlags)
{
	baseInit(ri.fm, r, randomFlags);
	// Keep U/O (and their sticky bits) out of the initial flag state: the O/U deviation of both the interpreter and the arm64 JIT from
	// x86 is covered by the FMAC suites; here only the pipeline/timing behaviour is under test.
	ri.fm.status &= 0xcf3u;
	ri.fm.mac &= 0x00ffu;
	for (u32 i = 4; i < 128; ++i) ri.fm.vf[i] = refNormal(r);
	for (u32 l = 0; l < 4; ++l) ri.fm.acc[l] = refNormal(r);
	ri.fm.i = refNormal(r);
	ri.fm.q = refNormal(r);
	ri.fm.p = refNormal(r);
	static const u32 edge[] = {0, 1, 2, 3, 0x7fff, 0x8000, 0xffff, 0x00ff, 0x0100, 0x03ff, 0x0400, 0x0401, 0x043f, 0x7ffe, 0x4000, 0x00fe, 0xfffe};
	ri.vi[0] = 0;
	for (u32 i = 1; i < 16; ++i) ri.vi[i] = r.below(3) ? edge[r.below(sizeof(edge) / sizeof(edge[0]))] : (r.next() & 0xffff);
}

static const RefCfg kCfgInt = {"x86-parity-vu-int"};

//------------------------------------------------------------------
// x86-parity-flags: FMAC flag pipeline vs flag-reading lower ops at varying distances
//------------------------------------------------------------------
static void runFlagsSuite(int index, u32 count)
{
	static const RefCfg cfg = {"x86-parity-flags"};
	configure(1, 0);
	const auto& ops = upperOps();
	for (u32 variant = 0; variant < 4; ++variant)
	{
		static const char* const vn[4] = {"FMAC-then-FMAND/FSAND", "FMAC-chain+CLIP-then-FCAND/FCEQ", "FSSET/FCSET-then-FMAC-then-read", "mixed-dense"};
		for (u32 k = 0; k < count; ++k)
		{
			Rng r(0xf1a90000ull + k * 7919ull + variant * 131ull + index);
			RefInit ri;
			refInitData(ri, r, true);
			// Edge-case operands so Z/S flags occur: zeros and negatives.
			for (u32 i = 4; i < 40; ++i) if (r.below(5) == 0) ri.fm.vf[i] = r.below(2) ? 0u : 0x80000000u;
			VProg p;
			const u32 len = 3 + r.below(7);
			const auto fmac = [&]() {
				u32 op;
				do op = r.below(static_cast<u32>(ops.size())); while (ops[op].kind == K_FTOI || ops[op].kind == K_ITOF || ops[op].kind == K_OPMULA || ops[op].kind == K_OPMSUB || ops[op].fam == FAM_MAX || ops[op].fam == FAM_MINI);
				u32 mask = r.below(3) == 0 ? 0xf : 1 + r.below(15);
				if (ops[op].kind == K_CLIP) mask = 0xf;
				p.upper(encodeUpper(ops[op], mask, 1 + r.below(6), 1 + r.below(6), 1 + r.below(6)));
			};
			const auto reader = [&]() {
				const u32 it = 7 + r.below(6), is = 1 + r.below(6);
				switch (r.below(10))
				{
					case 0: p.lower(enc::FMAND(it, is)); break;
					case 1: p.lower(enc::FMEQ(it, is)); break;
					case 2: p.lower(enc::FMOR(it, is)); break;
					case 3: p.lower(enc::FSAND(it, r.next() & 0xcf3)); break;
					case 4: p.lower(enc::FSEQ(it, r.next() & 0xcf3)); break;
					case 5: p.lower(enc::FSOR(it, r.next() & 0xcf3)); break;
					case 6: p.lower(enc::FCAND(r.next() & 0xffffff)); break;
					case 7: p.lower(enc::FCEQ(r.next() & 0xffffff)); break;
					case 8: p.lower(enc::FCOR(r.next() & 0xffffff)); break;
					default: p.lower(enc::FCGET(it)); break;
				}
			};
			// The register state (VI 7..12) of every reader is part of the compared state.
			if (variant == 2)
			{
				p.lower(enc::FSSET(r.next() & 0xcf3));
				p.lower(enc::FCSET(r.next() & 0xffffff));
			}
			for (u32 j = 0; j < len; ++j)
			{
				if (variant == 3 && r.below(3) == 0) reader(); else fmac();
				if (variant != 3 && r.below(2)) p.nop(r.below(4));
			}
			// Distance sweep: readers 0..5 instructions after the last producer.
			const u32 dist = r.below(6);
			p.nop(dist);
			const u32 nread = 1 + r.below(3);
			for (u32 j = 0; j < nread; ++j) { reader(); if (r.below(2)) p.nop(r.below(3)); }
			p.pair(NOP_L, NOP_U | kEbit);
			p.nop(1);
			refCase(cfg, index, vn[variant], p, ri, "k=" + std::to_string(k));
		}
		char nm[96];
		std::snprintf(nm, sizeof(nm), "flags vu%d %s", index, vn[variant]);
		endGroup(nm);
	}
}

//------------------------------------------------------------------
// x86-parity-vu-int: integer ALU, VI0 writes, MFIR/MTIR, XTOP/XITOP
//------------------------------------------------------------------
static void runIntSuite(int index, u32 count)
{
	configure(1, 0);
	for (u32 k = 0; k < count; ++k)
	{
		Rng r(0x1a7a0000ull + k * 7919ull + index);
		RefInit ri;
		refInitData(ri, r, false);
		VProg p;
		const u32 len = 3 + r.below(10);
		static const u32 imm15s[] = {0, 1, 0x7fff, 0x4000, 0x7800, 0x0800, 0x07ff, 0x7ffe, 0x0001};
		for (u32 j = 0; j < len; ++j)
		{
			const u32 a = r.below(6), b = r.below(6), c = r.below(6);
			const u32 pick = r.below(15);
			const u32 mask = 1 + r.below(15);
			switch (pick)
			{
				case 0: p.lower(enc::IADD(a, b, c)); break;
				case 1: p.lower(enc::ISUB(a, b, c)); break;
				case 2: p.lower(enc::IAND(a, b, c)); break;
				case 3: p.lower(enc::IOR(a, b, c)); break;
				case 4: p.lower(enc::IADDI(a, b, static_cast<int>(r.below(32)) - 16)); break;
				case 5: p.lower(enc::IADDIU(a, b, r.below(2) ? imm15s[r.below(9)] : (r.next() & 0x7fff))); break;
				case 6: p.lower(enc::ISUBIU(a, b, r.below(2) ? imm15s[r.below(9)] : (r.next() & 0x7fff))); break;
				case 7: p.lower(enc::MTIR(r.below(4), a, 1 + r.below(8))); break;
				case 8: p.lower(enc::MFIR(mask, 1 + r.below(8), b)); break;
				case 9: p.lower(enc::FCGET(a)); break;
				case 10: if (index == 1) p.lower(enc::XTOP(a)); else p.lower(enc::IADD(a, b, c)); break;
				case 11: if (index == 1) p.lower(enc::XITOP(a)); else p.lower(enc::ISUB(a, b, c)); break;
				case 12: p.upper(upperWord("ADD", 1 + r.below(15), 1 + r.below(6), 1 + r.below(6), 1 + r.below(6))); break; // upper/lower overlap
				default: p.lower(enc::IADD(a, a, a)); break;
			}
		}
		p.pair(NOP_L, NOP_U | kEbit);
		p.nop(1);
		refCase(kCfgInt, index, "INT", p, ri, "k=" + std::to_string(k));
	}
	char nm[64];
	std::snprintf(nm, sizeof(nm), "vu-int vu%d INT", index);
	endGroup(nm);
}

//------------------------------------------------------------------
// x86-parity-vu-mem: LQ/SQ/LQI/SQI/LQD/SQD/ILW/ISW/ILWR/ISWR incl. address wrap and the VU0 -> VU1 register window at 0x400
//------------------------------------------------------------------
static void runMemSuite(int index, u32 count)
{
	static const RefCfg cfg = {"x86-parity-vu-mem"};
	configure(1, 0);
	static const char* const kn[10] = {"LQ", "SQ", "LQI", "SQI", "LQD", "SQD", "ILW", "ISW", "ILWR", "ISWR"};
	for (u32 pick = 0; pick < 10; ++pick)
	{
		for (u32 k = 0; k < count; ++k)
		{
			Rng r(0x3e300000ull + k * 7919ull + pick * 131ull + index);
			RefInit ri;
			refInitData(ri, r, false);
			static const u32 addr[] = {0, 1, 0x3e, 0xff, 0x100, 0x3ff, 0x400, 0x401, 0x420, 0x43f, 0x7fff, 0x8000, 0xffff, 0xfffe, 0x0fff, 0x1fff, 0x2000};
			for (u32 i = 1; i < 6; ++i) ri.vi[i] = r.below(4) ? addr[r.below(sizeof(addr) / sizeof(addr[0]))] : (r.next() & 0xffff);
			VProg p;
			const u32 n = 1 + r.below(3);
			for (u32 j = 0; j < n; ++j)
			{
				const u32 dest = 1 + r.below(15), vf = 1 + r.below(8), vi = r.below(6), base = r.below(6);
				const int imm = r.below(3) == 0 ? static_cast<int>(r.below(2048)) - 1024 : static_cast<int>(r.below(16)) - 4;
				switch (pick)
				{
					case 0: p.lower(enc::LQ(dest, vf, base, imm)); break;
					case 1: p.lower(enc::SQ(dest, vf, base, imm)); break;
					case 2: p.lower(enc::LQI(dest, vf, base)); break;
					case 3: p.lower(enc::SQI(dest, vf, base)); break;
					case 4: p.lower(enc::LQD(dest, vf, base)); break;
					case 5: p.lower(enc::SQD(dest, vf, base)); break;
					case 6: p.lower(enc::ILW(dest, vi, base, imm)); break;
					case 7: p.lower(enc::ISW(dest, vi, base, imm)); break;
					case 8: p.lower(enc::ILWR(dest, vi, base)); break;
					default: p.lower(enc::ISWR(dest, vi, base)); break;
				}
				if (r.below(3) == 0) p.nop(1);
			}
			// Read back what was written so a wrong lane/zero-half is visible even without a memory diff.
			p.lower(enc::LQ(0xf, 9, 0, static_cast<int>(r.below(64))));
			p.pair(NOP_L, NOP_U | kEbit);
			p.nop(1);
			refCase(cfg, index, kn[pick], p, ri, "k=" + std::to_string(k));
		}
		char nm[64];
		std::snprintf(nm, sizeof(nm), "vu-mem vu%d %s", index, kn[pick]);
		endGroup(nm);
	}
}

//------------------------------------------------------------------
// x86-parity-vu-branch: B/BAL/JR/JALR/IB* with delay slots, branch on a VI written by the previous instruction, branch in delay slot
//------------------------------------------------------------------
static void runBranchSuite(int index, u32 count)
{
	static const RefCfg cfg = {"x86-parity-vu-branch"};
	configure(1, 0);
	constexpr u32 base = 0x80 / 8;
	static const char* const bn[10] = {"B", "BAL", "JR", "JALR", "IBEQ", "IBNE", "IBLTZ", "IBGTZ", "IBLEZ", "IBGEZ"};
	for (u32 bk = 0; bk < 10; ++bk)
	{
		for (u32 k = 0; k < count; ++k)
		{
			Rng r(0xb2a00000ull + k * 7919ull + bk * 131ull + index);
			RefInit ri;
			refInitData(ri, r, false);
			static const u32 edge[] = {0, 1, 2, 0x7fff, 0x8000, 0xffff, 0x00ff};
			for (u32 i = 1; i < 6; ++i) ri.vi[i] = r.below(4) ? edge[r.below(sizeof(edge) / sizeof(edge[0]))] : (r.next() & 0xffff);
			VProg p;
			const u32 blocks = 1 + r.below(3);
			// Per block: optional VI writer (prev-instruction hazard), branch, delay slot, 0..2 skipped filler pairs, landing marker.
			std::vector<u32> branchIdx, branchKind;
			for (u32 b = 0; b < blocks; ++b)
			{
				const u32 a = 1 + r.below(4), c = 1 + r.below(4);
				const bool writePrev = r.below(2) != 0;
				if (r.below(2)) p.lower(enc::IADDI(1 + r.below(4), 1 + r.below(4), static_cast<int>(r.below(32)) - 16));
				const u32 fill = r.below(3);
				const int d = static_cast<int>(1 + fill + r.below(2)); // target = branch + 1 + d pairs
				// JR/JALR need the absolute index: written by the previous instruction (new value is used) or earlier.
				const u32 branchPair = p.size() + ((writePrev || bk == 2 || bk == 3) ? 1 : 0);
				const u32 target = base + branchPair + 1 + static_cast<u32>(d);
				if (bk == 2 || bk == 3) p.lower(enc::IADDIU(a, 0, target)); // sets VI[a] = absolute target index
				else if (writePrev) p.lower(enc::IADDI(a, a, 1 - static_cast<int>(r.below(3))));
				switch (bk)
				{
					case 0: p.lower(enc::B(d)); break;
					case 1: p.lower(enc::BAL(1 + r.below(5), d)); break;
					case 2: p.lower(enc::JR(a)); break;
					case 3: p.lower(enc::JALR(1 + r.below(5), a)); break;
					case 4: p.lower(enc::IBEQ(a, c, d)); break;
					case 5: p.lower(enc::IBNE(a, c, d)); break;
					case 6: p.lower(enc::IBLTZ(a, d)); break;
					case 7: p.lower(enc::IBGTZ(a, d)); break;
					case 8: p.lower(enc::IBLEZ(a, d)); break;
					default: p.lower(enc::IBGEZ(a, d)); break;
				}
				// Delay slot.
				switch (r.below(5))
				{
					case 0: p.nop(1); break;
					case 1: p.lower(enc::IADDIU(7 + b, 0, 0x100 + b)); break;
					case 2: p.lower(enc::IADDI(a, a, 3)); break;                 // modifies the compared reg in the delay slot
					case 3: p.upper(upperWord("ADD", 0xf, 1, 2, 3 + b)); break;
					default: p.lower(enc::ISUBIU(1 + r.below(4), 1 + r.below(4), r.below(100))); break;
				}
				for (u32 f = 0; f < fill + (static_cast<u32>(d) > fill + 1 ? 1 : 0); ++f) p.lower(enc::IADDIU(10 + b, 10 + b, 1 + f)); // skipped if taken
				p.lower(enc::IADDIU(13, 13, 1 + b));                               // landing marker (always executed)
			}
			p.nop(4);
			p.pair(NOP_L, NOP_U | kEbit);
			p.nop(2);
			refCase(cfg, index, bn[bk], p, ri, "k=" + std::to_string(k));
		}
		char nm[64];
		std::snprintf(nm, sizeof(nm), "vu-branch vu%d %s", index, bn[bk]);
		endGroup(nm);
	}
}

//------------------------------------------------------------------
// x86-parity-vu-misc: flag-register set/get ops, Q/P latency, WAITQ/WAITP, I-bit, E/M/D/T bits
//------------------------------------------------------------------
static void runMiscSuite(int index, u32 count)
{
	static const RefCfg cfg = {"x86-parity-vu-misc"};
	configure(1, 0);
	static const char* const mn[8] = {"flagset+read", "Q-latency", "P-latency", "I-bit LOI", "E/D/T bits", "WAITQ/WAITP", "DIV/SQRT/RSQRT flags+Q", "E/M/D/T bits (M-bit)"};
	for (u32 variant = 0; variant < 8; ++variant)
	{
		if (variant == 7 && index != 0) continue; // the M bit only matters in VU0 micro mode
		const bool mVariant = variant == 7;
		if ((variant == 2) && index != 1) continue; // EFU/P are VU1 only
		for (u32 k = 0; k < count; ++k)
		{
			Rng r(0x7a5c0000ull + k * 7919ull + variant * 131ull + index);
			RefInit ri;
			refInitData(ri, r, true);
			VProg p;
			const int addq = findUpper("ADDq"), mulq = findUpper("MULq"), maddq = findUpper("MADDq"), addi = findUpper("ADDi"), muli = findUpper("MULi"), maddi = findUpper("MADDi");
			(void)maddq;
			switch (variant)
			{
				case 0:
				{
					const u32 n = 2 + r.below(6);
					for (u32 j = 0; j < n; ++j)
					{
						const u32 it = 7 + r.below(6), is = 1 + r.below(5);
						switch (r.below(13))
						{
							case 0: p.lower(enc::FSSET(r.next() & 0xcf3)); break;
							case 1: p.lower(enc::FCSET(r.next() & 0xffffff)); break;
							case 2: p.lower(enc::FSAND(it, r.next() & 0xcf3)); break;
							case 3: p.lower(enc::FSEQ(it, r.next() & 0xcf3)); break;
							case 4: p.lower(enc::FSOR(it, r.next() & 0xcf3)); break;
							case 5: p.lower(enc::FCAND(r.next() & 0xffffff)); break;
							case 6: p.lower(enc::FCEQ(r.next() & 0xffffff)); break;
							case 7: p.lower(enc::FCOR(r.next() & 0xffffff)); break;
							case 8: p.lower(enc::FCGET(it)); break;
							case 9: p.lower(enc::FMAND(it, is)); break;
							case 10: p.lower(enc::FMEQ(it, is)); break;
							case 11: p.lower(enc::FMOR(it, is)); break;
							default: p.upper(upperWord("MUL", 0xf, 1 + r.below(5), 1 + r.below(5), 1 + r.below(5))); break;
						}
						if (r.below(2)) p.nop(r.below(5));
					}
					break;
				}
				case 1: case 6:
				{
					const u32 which = variant == 6 ? r.below(3) : 0;
					const u32 fsf = r.below(4), ftf = r.below(4);
					if (r.below(4) == 0) ri.fm.vf[2 * 4 + ftf] = 0; // divide by zero
					if (r.below(6) == 0) ri.fm.vf[2 * 4 + ftf] = 0x80000000u | ri.fm.vf[2 * 4 + ftf];
					if (which == 0) p.lower(enc::DIVq(fsf, ftf, 2, 1));
					else if (which == 1) p.lower(enc::SQRTq(ftf, 2));
					else p.lower(enc::RSQRTq(fsf, ftf, 2, 1));
					const u32 dist = r.below(16);
					p.nop(dist);
					if (r.below(3) == 0) p.lower(enc::WAITQ());
					const int cons = r.below(3) == 0 ? mulq : addq;
					p.upper(encodeUpper(upperOps()[cons], 0xf, 3, 4, 5));
					if (r.below(2)) p.upper(encodeUpper(upperOps()[addq], 0xf, 3, 4, 6));
					p.nop(40);
					break;
				}
				case 2:
				{
					const u32 fsf = r.below(4);
					const u32 e = r.below(3);
					p.lower(e == 0 ? enc::ESIN(fsf, 1) : (e == 1 ? enc::ESQRT(fsf, 1) : enc::ESUMv(1)));
					p.nop(r.below(60));
					if (r.below(2)) p.lower(enc::WAITP());
					p.lower(enc::MFP(1 + r.below(15), 6));
					p.upper(encodeUpper(upperOps()[findUpper("ADD")], 0xf, 6, 3, 5)); // P-independent consumer of MFP result
					p.nop(64);
					break;
				}
				case 3:
				{
					const u32 n = 1 + r.below(4);
					for (u32 j = 0; j < n; ++j)
					{
						const int op = r.below(3) == 0 ? maddi : (r.below(2) ? addi : muli);
						p.pair(refNormal(r), kIbit | encodeUpper(upperOps()[op], 0xf, 1 + r.below(5), 1 + r.below(5), 1 + r.below(5)));
						if (r.below(2)) p.upper(encodeUpper(upperOps()[addi], 0xf, 1 + r.below(5), 0, 1 + r.below(5)));
					}
					break;
				}
				case 4: case 7:
				{
					// D/T bits are ignored without FBRST DE/TE; M bit is a micro-mode no-op; E ends the program (with delay slot).
					const u32 n = 2 + r.below(5);
					for (u32 j = 0; j < n; ++j)
					{
						u32 up = encodeUpper(upperOps()[findUpper("ADD")], 0xf, 1 + r.below(5), 1 + r.below(5), 1 + r.below(5));
						up |= (mVariant && r.below(3) == 0 ? kMbit : 0) | (r.below(3) == 0 ? kDbit : 0) | (r.below(3) == 0 ? kTbit : 0);
						p.pair(r.below(3) == 0 ? enc::IADDI(1 + r.below(4), 1 + r.below(4), 1) : NOP_L, up);
					}
					p.pair(enc::IADDIU(9, 0, 0x55), NOP_U | kEbit | (mVariant && r.below(2) ? kMbit : 0));
					p.pair(enc::IADDIU(10, 0, 0x66), upperWord("ADD", 0xf, 1, 2, 7)); // E-bit delay slot executes
					p.pair(enc::IADDIU(11, 0, 0x77), upperWord("ADD", 0xf, 1, 2, 8));  // must NOT execute
					break;
				}
				default:
				{
					const u32 fsf = r.below(4), ftf = r.below(4);
					p.lower(enc::DIVq(fsf, ftf, 2, 1));
					p.nop(r.below(4));
					p.lower(enc::WAITQ());
					p.upper(encodeUpper(upperOps()[addq], 0xf, 3, 4, 5));
					if (index == 1)
					{
						p.lower(enc::ESIN(r.below(4), 1));
						p.nop(r.below(3));
						p.lower(enc::WAITP());
						p.lower(enc::MFP(0xf, 6));
					}
					p.nop(32);
					break;
				}
			}
			p.pair(NOP_L, NOP_U | kEbit);
			p.nop(1);
			refCase(cfg, index, mn[variant], p, ri, "k=" + std::to_string(k));
		}
		char nm[96];
		std::snprintf(nm, sizeof(nm), "vu-misc vu%d %s", index, mn[variant]);
		endGroup(nm);
	}
}
} // namespace x86p

#endif // EMUCOREX_ENABLE_NATIVE_SELF_TESTS
