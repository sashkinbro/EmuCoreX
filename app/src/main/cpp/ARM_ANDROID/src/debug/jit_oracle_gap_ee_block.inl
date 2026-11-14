// SPDX-FileCopyrightText: 2026 EmuCoreX Team
// SPDX-License-Identifier: GPL-3.0+
// Suites ee-block-rand (random legal multi-block programs, bounded loops, branch-likely, J/JAL/JR/JALR, delay-slot hazards, scratch
// memory ops, Count probes) and ee-exc-mid-block (the same programs with an injected fault at a random position, including delay
// slots). Reference = EE interpreter.

#if defined(EMUCOREX_ENABLE_NATIVE_SELF_TESTS) && !defined(NDEBUG)

namespace gapee
{
//------------------------------------------------------------------
// Tiny assembler with labels
//------------------------------------------------------------------
struct Asm
{
	std::vector<u32> w;
	std::vector<s32> labelPos;
	struct Fix { u32 idx, kind, label; };
	std::vector<Fix> fix;
	u32 newLabel() { labelPos.push_back(-1); return static_cast<u32>(labelPos.size() - 1); }
	void bind(u32 l) { labelPos[l] = static_cast<s32>(w.size()); }
	void emit(u32 x) { w.push_back(x); }
	u32 pcOf(u32 idx) const { return PROG_BASE + idx * 4; }
	void branch(u32 op, u32 rs, u32 rt, u32 label) { fix.push_back({static_cast<u32>(w.size()), 0, label}); w.push_back(iI(op, rs, rt, 0)); }
	void jump(u32 op, u32 label) { fix.push_back({static_cast<u32>(w.size()), 1, label}); w.push_back(JTYPE(op, 0)); }
	void loadAddr(u32 reg, u32 label) { fix.push_back({static_cast<u32>(w.size()), 2, label}); w.push_back(LUI(reg, 0)); w.push_back(ORI(reg, reg, 0)); }
	void finalize()
	{
		for (const Fix& f : fix)
		{
			const u32 tgt = pcOf(static_cast<u32>(labelPos[f.label]));
			if (f.kind == 0) w[f.idx] = (w[f.idx] & 0xffff0000u) | BREL(pcOf(f.idx), tgt);
			else if (f.kind == 1) w[f.idx] = (w[f.idx] & 0xfc000000u) | ((tgt >> 2) & 0x03ffffffu);
			else { w[f.idx] = (w[f.idx] & 0xffff0000u) | (tgt >> 16); w[f.idx + 1] = (w[f.idx + 1] & 0xffff0000u) | (tgt & 0xffffu); }
		}
	}
};

//------------------------------------------------------------------
// Body (non-branch) instruction generator
//------------------------------------------------------------------
struct BodyInst
{
	u32 word;
	int destKind; // 0 none, 1 rd field, 2 rt field
};

struct BodyCtx
{
	std::vector<u32> pool;   // data registers (read and written)
	u32 memLimit = 0x100;    // offsets stay below this
	bool allowMem = true;
};

static u32 poolReg(Rng& r, const BodyCtx& cx) { return r.below(12) == 0 ? 0u : cx.pool[r.below(static_cast<u32>(cx.pool.size()))]; }
static u32 poolDest(Rng& r, const BodyCtx& cx) { return cx.pool[r.below(static_cast<u32>(cx.pool.size()))]; }

static BodyInst genMem(Rng& r, const BodyCtx& cx)
{
	struct M { u32 op; u32 align; bool load; };
	static const M m[] = {
		{0x20, 1, true}, {0x21, 2, true}, {0x23, 4, true}, {0x24, 1, true}, {0x25, 2, true}, {0x27, 4, true}, {0x37, 8, true}, {0x1e, 1, true},
		{0x22, 1, true}, {0x26, 1, true}, {0x1a, 1, true}, {0x1b, 1, true},
		{0x28, 1, false}, {0x29, 2, false}, {0x2b, 4, false}, {0x3f, 8, false}, {0x1f, 1, false},
		{0x2a, 1, false}, {0x2e, 1, false}, {0x2c, 1, false}, {0x2d, 1, false},
	};
	const M& o = m[r.below(sizeof(m) / sizeof(m[0]))];
	const u32 off = (r.below(cx.memLimit / o.align)) * o.align;
	const u32 rt = o.load ? poolDest(r, cx) : poolReg(r, cx);
	return {iI(o.op, 16, rt, off), o.load ? 2 : 0};
}

// Trap-free integer instruction (any non-trapping table entry), registers from the pool.
static BodyInst genAlu(Rng& r, const BodyCtx& cx)
{
	for (;;)
	{
		const u32 op = r.below(kIntOpCount);
		const IntOp& o = kIntOps[op];
		if (o.trap) continue;
		IntInst i{};
		i.op = op;
		i.rs = poolReg(r, cx);
		i.rt = poolReg(r, cx);
		i.rd = r.below(14) == 0 ? 0u : poolDest(r, cx);
		if (r.below(6) == 0) i.rt = i.rs;
		if (r.below(8) == 0 && (o.use & U_RD)) i.rd = i.rs;
		if (o.use & U_SA) i.sa = r.below(32);
		if (o.use & U_SA4) i.sa = r.below(16);
		if (o.use & U_PMFHL) i.sa = r.below(5);
		if (o.use & U_IMM) i.imm = randImm16(r);
		const bool imm = (o.use & U_IMM) && !(o.use & U_RD);
		return {encodeInt(i), (o.use & U_RD) ? 1 : (imm && (o.use & U_RT)) ? 2 : 0};
	}
}

static BodyInst genBody(Rng& r, const BodyCtx& cx)
{
	if (cx.allowMem && r.below(10) < 3) return genMem(r, cx);
	return genAlu(r, cx);
}

static u32 forceDest(const BodyInst& b, u32 reg)
{
	if (b.destKind == 1) return (b.word & ~(0x1fu << 11)) | (reg << 11);
	if (b.destKind == 2) return (b.word & ~(0x1fu << 16)) | (reg << 16);
	return b.word;
}

//------------------------------------------------------------------
// Program generation
//------------------------------------------------------------------
enum ProgMode { PM_PLAIN = 0, PM_PROBE = 1 };

struct Prog
{
	std::vector<u32> code;      // program part (starts at PROG_BASE), no sentinel
	GPR_reg init[32];
	u32 probeBase = 0;          // scratch offset of the probe region (probe mode)
	u32 faultAddr = 0;          // address of the injected fault instruction
	bool faultInDelay = false;  // ... and whether it sits in a delay slot (then faultAddr - 4 is the branch)
};

struct FaultSpec
{
	bool enabled = false;
	u32 kind = 0;
	bool inDelaySlot = false;
};

constexpr u32 PROBE_OFF = 0x80;

// Register roles for block programs.
//  $16 scratch base, $20..$22 loop counters, $26 probe pointer, $28 jump-target temp, $29 probe temp, $30 loop tmp, $31 link
static const u32 kBlockPool[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 17, 18, 19, 23, 24, 25, 27};

static std::vector<u32> faultWords(u32 kind, Rng& r, bool& willFault)
{
	// $24 = 0x7fffffff, $25 = 1, $26 = 0x7fffffffffffffff, $27 = 0xffffffff80000000, $28 = 0x8000000000000000
	const u32 d = 1 + r.below(15);
	willFault = true;
	switch (kind)
	{
		case 0: return {iR(24, 25, d, 0, 0x20)};                  // ADD overflow
		case 1: return {iR(27, 25, d, 0, 0x22)};                  // SUB overflow
		case 2: return {iI(8, 24, d, 1)};                         // ADDI overflow
		case 3: return {iR(26, 25, d, 0, 0x2c)};                  // DADD overflow
		case 4: return {iR(28, 25, d, 0, 0x2e)};                  // DSUB overflow
		case 5: return {iI(0x18, 26, d, 1)};                      // DADDI overflow
		case 6: return {BREAK() | (r.next() & 0x03ffffc0u)};
		case 7: return {SYSCALL() | (r.next() & 0x03ffffc0u)};
		case 8: return {iR(25, 25, 0, 0, 0x34)};                  // TEQ
		case 9: return {iR(24, 25, 0, 0, 0x36)};                  // TNE
		case 10: return {iR(24, 25, 0, 0, 0x30)};                 // TGE
		case 11: return {iR(25, 24, 0, 0, 0x32)};                 // TLT
		case 12: return {iR(24, 25, 0, 0, 0x31)};                 // TGEU
		case 13: return {iR(25, 24, 0, 0, 0x33)};                 // TLTU
		case 14: return {iI(1, 25, 0xc, 1)};                      // TEQI
		case 15: return {iI(1, 25, 0xe, 2)};                      // TNEI
		case 16: return {iI(1, 24, 0x8, 1)};                      // TGEI
		case 17: return {iI(1, 24, 0x9, 1)};                      // TGEIU
		case 18: return {iI(1, 25, 0xa, 0x7fff)};                 // TLTI
		case 19: return {iI(1, 25, 0xb, 2)};                      // TLTIU
		// controls: look like faults but do not trap
		case 20: willFault = false; return {iR(25, 25, d, 0, 0x20)};
		case 21: willFault = false; return {iR(25, 25, 0, 0, 0x36)};
		case 22: willFault = false; return {iR(24, 25, 0, 0, 0x34)};
		case 23: willFault = false; return {iI(8, 25, d, 1)};
		case 24: willFault = false; return {iI(1, 24, 0xa, 1)};   // TLTI 0x7fffffff < 1 false
		default: willFault = false; return {iR(26, 25, d, 0, 0x2d)}; // DADDU overflow does not trap
	}
}
constexpr u32 kFaultKinds = 26;

struct BlockGenCfg
{
	u32 mode = PM_PLAIN;
	bool fault = false;
	u32 faultKind = 0;
	bool faultInDelay = false;
	u32 faultSlot = 0xffffffffu;   // index of the candidate slot that receives the fault
	u32 slotsSeen = 0;             // out: number of candidate slots in the program
	u32 faultSeed = 1;
};

static void genProgram(Rng& r, BlockGenCfg& cfg, Prog& p)
{
	Asm a;
	BodyCtx cx;
	Rng fr(cfg.faultSeed);
	const bool probe = cfg.mode == PM_PROBE;
	if (cfg.fault)
	{
		// $24..$28 hold the fault operands: not data registers
		for (u32 reg : {1, 2, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 17, 18, 19, 23}) cx.pool.push_back(reg); // $3 (v1) is the SYSCALL number: kept at 1 so no HLE (SetGsCrt -> MTGS) runs
	}
	else
		for (u32 reg : kBlockPool) cx.pool.push_back(reg);
	cx.memLimit = probe ? PROBE_OFF : 0x100;
	std::memset(p.init, 0, sizeof(p.init));
	for (u32 reg : cx.pool) rand128(r, p.init[reg]);
	p.init[16].UD[0] = EE_TEST_SCRATCH;
	p.init[16].UD[1] = 0;
	if (cfg.fault)
	{
		p.init[3].UD[0] = 1;
		p.init[24].UD[0] = 0x7fffffffull;
		p.init[25].UD[0] = 1;
		p.init[26].UD[0] = 0x7fffffffffffffffull;
		p.init[27].UD[0] = 0xffffffff80000000ull;
		p.init[28].UD[0] = 0x8000000000000000ull;
	}
	if (probe) p.init[26].UD[0] = EE_TEST_SCRATCH + PROBE_OFF;

	const u32 nb = 1 + r.below(6);
	std::vector<u32> L(nb + 1);
	for (u32 i = 0; i <= nb; ++i) L[i] = a.newLabel();
	const u32 Lend = a.newLabel();
	struct SubReq { u32 label; u32 kind; u32 exitBlock; };
	std::vector<SubReq> subs;
	u32 loopCounters = 0;
	u32 opsLeft = 8 + r.below(33); // overall budget of body instructions

	// Every candidate slot always consumes the same random numbers, so a second pass with faultSlot set sees the same program.
	const auto slotEmit = [&](u32 word, bool delaySlot) {
		if (cfg.fault && delaySlot == cfg.faultInDelay)
		{
			const u32 idx = cfg.slotsSeen++;
			if (idx == cfg.faultSlot)
			{
				p.faultAddr = a.pcOf(static_cast<u32>(a.w.size()));
				p.faultInDelay = delaySlot;
				bool will;
				for (u32 x : faultWords(cfg.faultKind, fr, will)) a.emit(x);
				return;
			}
		}
		a.emit(word);
	};
	const auto emitProbe = [&]() {
		a.emit(MFC0(29, 9));
		a.emit(iI(0x2b, 26, 29, 0));
		a.emit(ADDIU(26, 26, 4));
	};
	const auto emitBody = [&](u32 n) {
		for (u32 k = 0; k < n; ++k) slotEmit(genBody(r, cx).word, false);
	};
	// delay slot: random non-branch op, biased to write one of 'hazards' (branch source / link)
	const auto emitDelay = [&](const std::vector<u32>& hazards, bool allowHazard31, u32 linkReg = 0) {
		BodyInst b = genBody(r, cx);
		u32 word = b.word;
		const u32 roll = r.below(10);
		if (!hazards.empty() && roll < 4)
		{
			const u32 h = hazards[r.below(static_cast<u32>(hazards.size()))];
			if (h != 0 && (h != 31 || allowHazard31)) word = forceDest(b, h);
		}
		// Known difference (GAP_LINKWAW=1 includes it): a slot that writes the JALR link register itself. The recompiler lets the link win,
		// the interpreter the slot. The random body can still pick the link register as its destination, so keep it away from it.
		if (linkReg != 0 && !knob("GAP_LINKWAW", 0) && ((b.destKind == 1 && ((word >> 11) & 31) == linkReg) || (b.destKind == 2 && ((word >> 16) & 31) == linkReg)))
			word = NOP;
		// Known x86 quirk (see GAP_KNOWN): TrySwapDelaySlot's MMI case only checks rd against the slot's rd, so a swapped MMI slot that
		// reads the JALR link register sees the stale value. The interpreter sees the new link; upstream x86 matches our JIT. Avoid it.
		if (linkReg != 0 && (word >> 26) == 0x1c && (((word >> 21) & 31) == linkReg || ((word >> 16) & 31) == linkReg) && !knownIncluded()) word = NOP;
		slotEmit(word, true);
	};
	const auto dataReg = [&]() { return r.below(8) == 0 ? 0u : cx.pool[r.below(static_cast<u32>(cx.pool.size()))]; };

	for (u32 i = 0; i < nb; ++i)
	{
		a.bind(L[i]);
		if (probe) emitProbe();
		const u32 n = std::min<u32>(opsLeft, r.below(6));
		opsLeft -= n;
		emitBody(n);

		const bool last = i == nb - 1;
		enum { T_FALL, T_COND, T_LIKELY, T_LINK, T_B, T_J, T_JAL, T_JR, T_JALR, T_LOOP };
		u32 t;
		if (probe)
		{
			static const u32 kProbeT[] = {T_B, T_J, T_JAL, T_JR, T_JALR, T_LOOP, T_FALL};
			t = kProbeT[r.below(7)];
		}
		else
		{
			static const u32 kT[] = {T_FALL, T_COND, T_COND, T_COND, T_LIKELY, T_LIKELY, T_LINK, T_B, T_J, T_JAL, T_JR, T_JALR, T_LOOP, T_COND, T_LIKELY};
			t = kT[r.below(sizeof(kT) / sizeof(kT[0]))];
		}
		if (t == T_LOOP && loopCounters >= 3) t = probe ? T_B : T_COND;
		const auto fwdTarget = [&]() -> u32 { return L[i + 1 + r.below(nb - i)]; }; // i+1 .. nb (exit)
		// probe mode: a taken branch to the sequential next block needs a skipped filler so it is a real block change
		const auto skipFiller = [&](u32 target) { if (probe && target == L[i + 1]) a.emit(ADDIU(23, 23, 1)); };
		switch (t)
		{
			case T_FALL:
				if (probe && !last)
				{
					a.emit(BR(4, 0, 0, 2)); // beq $0,$0,+2 : always taken, so the next block start follows a taken branch
					emitDelay({}, true);
					a.emit(ADDIU(23, 23, 1)); // skipped filler
				}
				break;
			case T_COND:
			case T_LIKELY:
			{
				const u32 rs = dataReg();
				const u32 rt = r.below(3) == 0 ? rs : dataReg();
				const u32 k = r.below(4); // BEQ BNE BLEZ BGTZ (+0x10 for the likely forms)
				const u32 op = (t == T_LIKELY ? 0x14u : 4u) + k;
				a.branch(op, rs, k >= 2 ? 0u : rt, fwdTarget());
				emitDelay({rs, rt}, true);
				break;
			}
			case T_LINK:
			{
				// REGIMM: BLTZ 0 BGEZ 1 BLTZL 2 BGEZL 3 BLTZAL 0x10 BGEZAL 0x11 BLTZALL 0x12 BGEZALL 0x13
				static const u32 kR[] = {0, 1, 2, 3, 0x10, 0x11, 0x12, 0x13, 0x10, 0x11};
				const u32 rs = dataReg();
				a.branch(1, rs, kR[r.below(10)], fwdTarget());
				emitDelay({rs, 31}, true);
				break;
			}
			case T_B:
			{
				const u32 tgt = fwdTarget();
				if (r.below(2)) a.branch(4, 0, 0, tgt); else a.branch(1, 0, 1, tgt); // beq $0,$0 / bgez $0
				emitDelay({}, true);
				skipFiller(tgt);
				break;
			}
			case T_J:
			{
				const u32 tgt = fwdTarget();
				a.jump(2, tgt);
				emitDelay({}, true);
				skipFiller(tgt);
				break;
			}
			case T_JAL:
			{
				if (subs.size() >= 2) { a.emit(NOP); break; }
				const u32 kind = r.below(2); // 0: sub returns with JR $ra; 1: sub exits with J to a later block
				const SubReq s{a.newLabel(), kind, i + 1 + r.below(nb - i)};
				subs.push_back(s);
				a.jump(3, s.label);
				emitDelay({31}, kind == 1); // kind 0 needs $31 intact across the delay slot
				break;
			}
			case T_JR:
			case T_JALR:
			{
				const u32 tgt = fwdTarget();
				a.loadAddr(29, tgt);
				if (t == T_JR)
				{
					a.emit(JR(29));
					emitDelay({29}, true);
				}
				else
				{
					u32 link = r.below(2) ? 31u : cx.pool[r.below(static_cast<u32>(cx.pool.size()))];
					// Known difference (see GAP_KNOWN): TrySwapDelaySlot moves a trapping ADD/SUB/... delay slot before the JALR link
					// write, so the link register is lost when that delay slot raises the exception.
					if (cfg.fault && cfg.faultInDelay && !knownIncluded()) link = 0;
					a.emit(JALR(link, 29));
					// A delay slot that writes the JALR link register itself is excluded by default: the recompiler (like upstream x86
					// recJALR via TrySwapDelaySlot, which does not check write-after-write on rd) lets the link win while the
					// interpreter lets the delay slot win. GAP_LINKWAW=1 re-enables it to reproduce the difference.
					if (knob("GAP_LINKWAW", 0)) emitDelay({29, link}, true, link); else emitDelay({29}, true, link);
				}
				skipFiller(tgt);
				break;
			}
			case T_LOOP:
			{
				// loop header h in [0..i]; the counter saturates so the loop is bounded even when re-entered
				const u32 h = r.below(i + 1);
				const u32 cnt = 20 + loopCounters++;
				const u32 limit = 1 + r.below(4);
				a.emit(ADDIU(cnt, cnt, 1));
				a.emit(SLTIU(30, cnt, limit + 1));
				const bool likely = r.below(3) == 0;
				a.branch(likely ? 0x15 : 5, 30, 0, L[h]);
				emitDelay({30}, true);
				if (probe && !last)
				{
					a.emit(BR(4, 0, 0, 2));
					a.emit(NOP);
					a.emit(ADDIU(23, 23, 1));
				}
				break;
			}
		}
	}
	a.bind(L[nb]);
	// Exit: jump over the subroutines to the end sentinel
	a.jump(2, Lend);
	a.emit(NOP);
	for (const SubReq& s : subs)
	{
		a.bind(s.label);
		if (probe) emitProbe();
		const u32 n = 1 + r.below(3);
		for (u32 k = 0; k < n; ++k) a.emit(genBody(r, cx).word);
		if (s.kind == 0)
		{
			a.emit(JR(31));
			a.emit(genBody(r, cx).word);
		}
		else
		{
			a.jump(2, L[s.exitBlock]);
			a.emit(genBody(r, cx).word);
		}
	}
	a.bind(Lend);
	a.finalize();
	p.code = a.w;
	p.probeBase = PROBE_OFF;
}

//------------------------------------------------------------------
// ee-block-rand
//------------------------------------------------------------------
static std::vector<u32> withPad(const Prog& p, bool linked)
{
	std::vector<u32> c(PAD_WORDS, NOP);
	c[0] = linked ? JTYPE(2, PROG_BASE) : BR(4, 0, 0, 3); // taken branch / jump into the program: the first program block follows a taken branch
	c.insert(c.end(), p.code.begin(), p.code.end());
	appendSentinel(c);
	return c;
}

struct CountStat { s32 maxPos = 0, maxNeg = 0; u64 probes = 0; };
static CountStat g_countStat;

static void compareBlockRun(const Prog& p, bool probe, const std::vector<u32>& code, const EESnapshot& ref, const EESnapshot& got, const std::string& tag)
{
	DiffOpt d;
	d.cmpScratch = true;
	d.cp0Mask = 0;
	u32 nProbes = 0;
	if (probe)
	{
		const u32 end = static_cast<u32>(ref.gpr[26]);
		nProbes = end > EE_TEST_SCRATCH + PROBE_OFF ? (end - EE_TEST_SCRATCH - PROBE_OFF) / 4 : 0;
		if (nProbes > 32) nProbes = 32;
		for (u32 k = 0; k < nProbes; ++k) d.scratchSkipWords.push_back(PROBE_OFF / 4 + k);
		d.gprSkip |= (1u << 29); // last Count reading
	}
	std::vector<Dif> diffs;
	snapDiff(ref, got, d, diffs);
	if (!diffs.empty())
	{
		compareRecord(ref, got, d, tag, code, p.init);
		return;
	}
	if (probe)
	{
		for (u32 k = 0; k < nProbes; ++k)
		{
			u32 x, y;
			std::memcpy(&x, ref.scratch + PROBE_OFF + k * 4, 4);
			std::memcpy(&y, got.scratch + PROBE_OFF + k * 4, 4);
			const s32 delta = static_cast<s32>(y - x);
			++g_countStat.probes;
			if (delta > g_countStat.maxPos) g_countStat.maxPos = delta;
			if (delta < g_countStat.maxNeg) g_countStat.maxNeg = delta;
			// x86-parity bound (see x86-parity-ee-sync): the recompiler drops the fractional 1/8 cycle at every block end, so
			// jit - interp may fall by about one cycle per probe, and may exceed the interpreter by a few cycles.
			// The recompiler carries the fractional block cycles in a compile-order static (s_nBlockCycles &= 7), the interpreter in
			// execution order, so the error grows with every block boundary (probes sit at block starts, with a few non-probed
			// boundaries between them): allow two cycles per probe.
			// x86 model: MFC0 Count adds scaleblockcycles_clear() including the MFC0's own cost (iCOP0.cpp:139), i.e. one cycle more than the interpreter.
			const s32 lo = -(2 + 2 * static_cast<s32>(k)), hi = 5 + static_cast<s32>(k);
			if (delta > hi || delta < lo)
			{
				std::string all;
				for (u32 q = 0; q < nProbes; ++q)
				{
					u32 xx, yy;
					std::memcpy(&xx, ref.scratch + PROBE_OFF + q * 4, 4);
					std::memcpy(&yy, got.scratch + PROBE_OFF + q * 4, 4);
					all += std::to_string(static_cast<s32>(yy - xx)) + " ";
				}
				char b[200];
				std::snprintf(b, sizeof(b), "COUNT probe#%u interp=%x jit=%x delta=%d (bounds %d..%d) %s deltas[%s] init{", k, x, y, delta, lo, hi, tag.c_str(), all.c_str());
				gFail("COUNT", std::string(b) + gprHex(p.init) + "} code{" + codeHex(code) + "}");
				return;
			}
		}
	}
}

static void runBlockPrograms(u32 mode, u32 count, u32 seedSalt)
{
	for (u32 k = 0; k < count; ++k)
	{
		Rng r(baseSeed() * 0x2545f4914f6cdd1dull + seedSalt * 7919ull + k * 6151ull + 99);
		Prog p;
		BlockGenCfg cfg;
		cfg.mode = mode;
		genProgram(r, cfg, p);
		RunOpt ro;
		ro.interpSteps = 1800;
		const bool probe = mode == PM_PROBE;
		const std::vector<u32> plain = withPad(p, false), linked = withPad(p, true);
		const EESnapshot ref = gapRun(false, plain, p.init, nullptr, ro);
		const EESnapshot refLinked = gapRun(false, linked, p.init, nullptr, ro);
		gCase();
		g_suppressRepro = false;
		static const char* const names[4] = {"cold", "warm", "recompile", "linked"};
		for (u32 v = 0; v < 4; ++v)
		{
			RunOpt o = ro;
			o.jitVariant = v == 3 ? 0 : v;
			const EESnapshot got = gapRun(true, v == 3 ? linked : plain, p.init, nullptr, o);
			const u64 failsBefore = g_grp.fails;
			compareBlockRun(p, probe, v == 3 ? linked : plain, v == 3 ? refLinked : ref, got, std::string(names[v]) + " k=" + std::to_string(k));
			if (g_grp.fails != failsBefore) g_suppressRepro = true;
		}
		g_suppressRepro = false;
	}
}

static void runBlockRand()
{
	const u32 n = knob("GAP_N", 1500);
	if (onlyMatch("ee-block-rand plain"))
	{
		gBegin("ee-block-rand plain");
		runBlockPrograms(PM_PLAIN, n, 1);
		gEnd();
	}
	if (onlyMatch("ee-block-rand count-probes"))
	{
		gBegin("ee-block-rand count-probes");
		g_countStat = CountStat();
		runBlockPrograms(PM_PROBE, n, 2);
		std::printf("GAPEE COUNTSTAT probes=%llu max(jit-interp)=%d min(jit-interp)=%d\n", static_cast<unsigned long long>(g_countStat.probes), g_countStat.maxPos, g_countStat.maxNeg);
		gEnd();
	}
}

//------------------------------------------------------------------
// ee-exc-mid-block
//------------------------------------------------------------------
static u32 excStatus(Rng& r)
{
	u32 s = 0;
	s |= r.below(2);                       // IE
	if (r.below(3) == 0) s |= 2;           // EXL
	s |= (r.next() & 0xff) << 8;           // IM0..IM7 (the interpreter does not run events here, so they are inert)
	if (r.below(2)) s |= 1u << 16;         // EIE
	s |= 0x20000000u;                      // CU0 (as the existing exception tests)
	return s;
}

static void runExcMidBlock()
{
	const u32 n = knob("GAP_N", 1500);
	for (u32 kind = 0; kind < kFaultKinds; ++kind)
	{
		for (u32 where = 0; where < 2; ++where)
		{
			char nm[96];
			std::snprintf(nm, sizeof(nm), "ee-exc-mid-block kind=%u %s", kind, where ? "delay-slot" : "body");
			if (!onlyMatch(nm)) continue;
			// kinds 0..5 (ADD/SUB/ADDI/DADD/DSUB/DADDI overflow) never trap on the x86 JIT (iR5900Arit.cpp:17, iR5900AritImm.cpp), while
			// the interpreter does: no interpreter reference exists. The JIT behaviour is gated by "ee-int-rand trap-model".
			if (kind <= 5) continue;
			gBegin(nm);
			const u32 per = std::max<u32>(10, n / 20);
			for (u32 k = 0; k < per; ++k)
			{
				Rng r(baseSeed() * 0x9e3779b97f4a7c15ull + kind * 1315423911ull + where * 2654435761ull + k * 40503ull + 5);
				Prog p;
				BlockGenCfg cfg;
				cfg.mode = PM_PLAIN;
				cfg.fault = true;
				cfg.faultKind = kind;
				cfg.faultInDelay = where != 0;
				cfg.faultSeed = static_cast<u32>(r.next()) | 1;
				{
					Rng r1 = r;
					BlockGenCfg c1 = cfg;
					Prog p1;
					genProgram(r1, c1, p1);
					if (c1.slotsSeen == 0 && cfg.faultInDelay)
					{
						cfg.faultInDelay = false; // no delay slot in this program: fault in the body instead
						Rng r2 = r;
						BlockGenCfg c2 = cfg;
						Prog p2;
						genProgram(r2, c2, p2);
						c1.slotsSeen = c2.slotsSeen;
					}
					Rng pick(cfg.faultSeed * 3ull + 1);
					cfg.faultSlot = pick.below(std::min<u32>(std::max<u32>(c1.slotsSeen, 1), 8));
				}
				genProgram(r, cfg, p);
				u32 cp0[32] = {};
				cp0[12] = excStatus(r);
				cp0[13] = r.next();
				cp0[14] = r.next();
				cp0[8] = r.next();
				cp0[4] = r.next();
				cp0[10] = r.next();
				cp0[30] = r.next();
				RunOpt ro;
				ro.interpSteps = 1800;
				ro.stopAtVector = true;
				const std::vector<u32> plain = withPad(p, false), linked = withPad(p, true);
				const EESnapshot ref = gapRun(false, plain, p.init, cp0, ro);
				const EESnapshot refLinked = gapRun(false, linked, p.init, cp0, ro);
				gCase();
				DiffOpt d;
				d.cp0Mask = (1u << 4) | (1u << 8) | (1u << 10) | (1u << 12) | (1u << 13) | (1u << 14) | (1u << 30);
				// A fault in a delay slot: the interpreter only knows it is in a delay slot when the branch was taken. The x86 JIT
				// never sets cpuRegs.branch, so BREAK/SYSCALL/trap there reach cpuException(code, 0) (iR5900.cpp:792-799 recCall
				// -> R5900OpcodeImpl.cpp:1207/1212/1243) with cpuRegs.pc = delay-slot address, Interpreter does pc -= 4: Cause.BD=0 and
				// EPC=branch address. Gate Cause.BD/EPC against that x86 model instead of against the interpreter.
				// Cause's exception code is still compared (BD masked out below).
				// Only when the fault slot was actually executed: the interpreter reached the vector with EPC at the slot (or EXL was already set). Programs
				// can branch over the chosen slot, and the overflow kinds (ADD/SUB/DADD/DSUB/ADDI/DADDI) never trap on the upstream EE, so
				// without this gate the model "expects" an exception that never happened.
				const auto reachedFault = [&](const EESnapshot& s) { return (s.pc & 0x80000000u) && ((cp0[12] & 2u) || (s.cp0[14] >= p.faultAddr - 4 && s.cp0[14] <= p.faultAddr)); };
				const bool bdModel = p.faultInDelay && reachedFault(ref) && reachedFault(refLinked);
				if (bdModel) d.cp0Mask &= ~((1u << 13) | (1u << 14));
				g_suppressRepro = false;
				for (u32 v = 0; v < 4; ++v)
				{
					RunOpt o = ro;
					o.jitVariant = v == 3 ? 0 : v;
					const EESnapshot got = gapRun(true, v == 3 ? linked : plain, p.init, cp0, o);
					static const char* const names[4] = {"cold", "warm", "recompile", "linked"};
					char tag[96];
					std::snprintf(tag, sizeof(tag), "%s k=%u status=%x", names[v], k, cp0[12]);
					const EESnapshot& rf = v == 3 ? refLinked : ref;
					const u64 failsBefore = g_grp.fails;
					// x86 quirk: a BREAK/SYSCALL in a delay slot raises the exception inside the slot (recCall, iR5900.cpp:792-799), but the
					// branch tail then stores the branch target into cpuRegs.pc and ends the block (SetBranchImm/SetBranchReg,
					// iR5900.cpp:887-905), so execution continues at the target instead of the vector. The interpreter jumps to the
					// vector. Only the exception state (Cause/EPC, below) is gated for these kinds.
					if (!(p.faultInDelay && (kind == 6 || kind == 7)))
						compareRecord(rf, got, d, tag, v == 3 ? linked : plain, p.init);
					if (bdModel)
					{
						const bool exl0 = !(cp0[12] & 2u);
						const u32 wantCause = rf.cp0[13] & 0x7fffffffu;   // x86 JIT: BD never set (cpuException bd == 0)
						const u32 wantEpc = exl0 ? p.faultAddr - 4 : cp0[14];
						if (got.cp0[13] != wantCause || got.cp0[14] != wantEpc)
						{
							char b[260];
							std::snprintf(b, sizeof(b), "%s k=%u BD model: Cause jit=%x want=%x (interp %x) EPC jit=%x want=%x (interp %x) status=%x faultAddr=%x", names[v], k, got.cp0[13], wantCause, rf.cp0[13], got.cp0[14], wantEpc, rf.cp0[14], cp0[12], p.faultAddr);
							gFail("BD-model", std::string(b) + " code{" + codeHex(v == 3 ? linked : plain) + "}");
						}
					}
					if (g_grp.fails != failsBefore) g_suppressRepro = true;
				}
				g_suppressRepro = false;
			}
			gEnd();
		}
	}
}

} // namespace gapee

#endif // EMUCOREX_ENABLE_NATIVE_SELF_TESTS
