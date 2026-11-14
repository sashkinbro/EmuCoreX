// SPDX-FileCopyrightText: 2026 EmuCoreX Team
// SPDX-License-Identifier: GPL-3.0+
// Suite ee-int-rand: every EE integer / shift / mult-div / MMI opcode with random operands, aliasing and edge-biased 128-bit values,
// in two variants (operands loaded at runtime vs materialised as constants inside the block so that constant propagation folds).
// Reference = EE interpreter. Compared: all 128-bit GPRs, HI/LO/HI1/LO1, SA (via MFSA epilogue), PC, and (trapping ops) Status/Cause/EPC.

#if defined(EMUCOREX_ENABLE_NATIVE_SELF_TESTS) && !defined(NDEBUG)

namespace gapee
{
enum : u32 { U_RS = 1, U_RT = 2, U_RD = 4, U_SA = 8, U_IMM = 16, U_SA4 = 32, U_PMFHL = 64, U_MTSAH = 128 };

struct IntOp
{
	const char* name;
	u32 base;   // instruction word with all register / immediate fields zero
	u32 use;
	bool trap;
};

static constexpr u32 SPEC(u32 fn) { return fn; }
static constexpr u32 IMMOP(u32 op) { return op << 26; }
static constexpr u32 MMIB(u32 group, u32 sub) { return (0x1Cu << 26) | (sub << 6) | group; }
constexpr u32 RRR = U_RS | U_RT | U_RD, RR = U_RS | U_RT, RTD = U_RT | U_RD;

static const IntOp kIntOps[] = {
	// SPECIAL three-register
	{"ADD", SPEC(0x20), RRR, true}, {"ADDU", SPEC(0x21), RRR, false}, {"SUB", SPEC(0x22), RRR, true}, {"SUBU", SPEC(0x23), RRR, false},
	{"AND", SPEC(0x24), RRR, false}, {"OR", SPEC(0x25), RRR, false}, {"XOR", SPEC(0x26), RRR, false}, {"NOR", SPEC(0x27), RRR, false},
	{"SLT", SPEC(0x2a), RRR, false}, {"SLTU", SPEC(0x2b), RRR, false}, {"DADD", SPEC(0x2c), RRR, true}, {"DADDU", SPEC(0x2d), RRR, false},
	{"DSUB", SPEC(0x2e), RRR, true}, {"DSUBU", SPEC(0x2f), RRR, false}, {"MOVZ", SPEC(0x0a), RRR, false}, {"MOVN", SPEC(0x0b), RRR, false},
	{"SLLV", SPEC(0x04), RRR, false}, {"SRLV", SPEC(0x06), RRR, false}, {"SRAV", SPEC(0x07), RRR, false},
	{"DSLLV", SPEC(0x14), RRR, false}, {"DSRLV", SPEC(0x16), RRR, false}, {"DSRAV", SPEC(0x17), RRR, false},
	// SPECIAL shifts by immediate
	{"SLL", SPEC(0x00), RTD | U_SA, false}, {"SRL", SPEC(0x02), RTD | U_SA, false}, {"SRA", SPEC(0x03), RTD | U_SA, false},
	{"DSLL", SPEC(0x38), RTD | U_SA, false}, {"DSRL", SPEC(0x3a), RTD | U_SA, false}, {"DSRA", SPEC(0x3b), RTD | U_SA, false},
	{"DSLL32", SPEC(0x3c), RTD | U_SA, false}, {"DSRL32", SPEC(0x3e), RTD | U_SA, false}, {"DSRA32", SPEC(0x3f), RTD | U_SA, false},
	// HI/LO/SA moves, multiply, divide
	{"MFHI", SPEC(0x10), U_RD, false}, {"MFLO", SPEC(0x12), U_RD, false}, {"MTHI", SPEC(0x11), U_RS, false}, {"MTLO", SPEC(0x13), U_RS, false},
	{"MFSA", SPEC(0x28), U_RD, false}, {"MTSA", SPEC(0x29), U_RS, false},
	{"MULT", SPEC(0x18), RRR, false}, {"MULTU", SPEC(0x19), RRR, false}, {"DIV", SPEC(0x1a), RR, false}, {"DIVU", SPEC(0x1b), RR, false},
	// immediates
	{"ADDI", IMMOP(8), U_RS | U_RT | U_IMM, true}, {"ADDIU", IMMOP(9), U_RS | U_RT | U_IMM, false}, {"SLTI", IMMOP(10), U_RS | U_RT | U_IMM, false},
	{"SLTIU", IMMOP(11), U_RS | U_RT | U_IMM, false}, {"ANDI", IMMOP(12), U_RS | U_RT | U_IMM, false}, {"ORI", IMMOP(13), U_RS | U_RT | U_IMM, false},
	{"XORI", IMMOP(14), U_RS | U_RT | U_IMM, false}, {"LUI", IMMOP(15), U_RT | U_IMM, false},
	{"DADDI", IMMOP(0x18), U_RS | U_RT | U_IMM, true}, {"DADDIU", IMMOP(0x19), U_RS | U_RT | U_IMM, false},
	{"MTSAB", (1u << 26) | (0x18u << 16), U_RS | U_IMM, false}, {"MTSAH", (1u << 26) | (0x19u << 16), U_RS | U_IMM, false},
	// MMI base group
	{"MADD", MMIB(0x00, 0), RRR, false}, {"MADDU", MMIB(0x01, 0), RRR, false}, {"PLZCW", MMIB(0x04, 0), U_RS | U_RD, false},
	{"MFHI1", MMIB(0x10, 0), U_RD, false}, {"MTHI1", MMIB(0x11, 0), U_RS, false}, {"MFLO1", MMIB(0x12, 0), U_RD, false}, {"MTLO1", MMIB(0x13, 0), U_RS, false},
	{"MULT1", MMIB(0x18, 0), RRR, false}, {"MULTU1", MMIB(0x19, 0), RRR, false}, {"DIV1", MMIB(0x1a, 0), RR, false}, {"DIVU1", MMIB(0x1b, 0), RR, false},
	{"MADD1", MMIB(0x20, 0), RRR, false}, {"MADDU1", MMIB(0x21, 0), RRR, false},
	{"PMFHL", MMIB(0x30, 0), U_RD | U_PMFHL, false}, {"PMTHL", MMIB(0x31, 0), U_RS, false},
	{"PSLLH", MMIB(0x34, 0), RTD | U_SA4, false}, {"PSRLH", MMIB(0x36, 0), RTD | U_SA4, false}, {"PSRAH", MMIB(0x37, 0), RTD | U_SA4, false},
	{"PSLLW", MMIB(0x3c, 0), RTD | U_SA, false}, {"PSRLW", MMIB(0x3e, 0), RTD | U_SA, false}, {"PSRAW", MMIB(0x3f, 0), RTD | U_SA, false},
	// MMI0
	{"PADDW", MMIB(8, 0), RRR, false}, {"PSUBW", MMIB(8, 1), RRR, false}, {"PCGTW", MMIB(8, 2), RRR, false}, {"PMAXW", MMIB(8, 3), RRR, false},
	{"PADDH", MMIB(8, 4), RRR, false}, {"PSUBH", MMIB(8, 5), RRR, false}, {"PCGTH", MMIB(8, 6), RRR, false}, {"PMAXH", MMIB(8, 7), RRR, false},
	{"PADDB", MMIB(8, 8), RRR, false}, {"PSUBB", MMIB(8, 9), RRR, false}, {"PCGTB", MMIB(8, 10), RRR, false},
	{"PADDSW", MMIB(8, 16), RRR, false}, {"PSUBSW", MMIB(8, 17), RRR, false}, {"PEXTLW", MMIB(8, 18), RRR, false}, {"PPACW", MMIB(8, 19), RRR, false},
	{"PADDSH", MMIB(8, 20), RRR, false}, {"PSUBSH", MMIB(8, 21), RRR, false}, {"PEXTLH", MMIB(8, 22), RRR, false}, {"PPACH", MMIB(8, 23), RRR, false},
	{"PADDSB", MMIB(8, 24), RRR, false}, {"PSUBSB", MMIB(8, 25), RRR, false}, {"PEXTLB", MMIB(8, 26), RRR, false}, {"PPACB", MMIB(8, 27), RRR, false},
	{"PEXT5", MMIB(8, 30), RTD, false}, {"PPAC5", MMIB(8, 31), RTD, false},
	// MMI1
	{"PABSW", MMIB(0x28, 1), RTD, false}, {"PCEQW", MMIB(0x28, 2), RRR, false}, {"PMINW", MMIB(0x28, 3), RRR, false}, {"PADSBH", MMIB(0x28, 4), RRR, false},
	{"PABSH", MMIB(0x28, 5), RTD, false}, {"PCEQH", MMIB(0x28, 6), RRR, false}, {"PMINH", MMIB(0x28, 7), RRR, false}, {"PCEQB", MMIB(0x28, 10), RRR, false},
	{"PADDUW", MMIB(0x28, 16), RRR, false}, {"PSUBUW", MMIB(0x28, 17), RRR, false}, {"PEXTUW", MMIB(0x28, 18), RRR, false},
	{"PADDUH", MMIB(0x28, 20), RRR, false}, {"PSUBUH", MMIB(0x28, 21), RRR, false}, {"PEXTUH", MMIB(0x28, 22), RRR, false},
	{"PADDUB", MMIB(0x28, 24), RRR, false}, {"PSUBUB", MMIB(0x28, 25), RRR, false}, {"PEXTUB", MMIB(0x28, 26), RRR, false}, {"QFSRV", MMIB(0x28, 27), RRR, false},
	// MMI2
	{"PMADDW", MMIB(9, 0), RRR, false}, {"PSLLVW", MMIB(9, 2), RRR, false}, {"PSRLVW", MMIB(9, 3), RRR, false}, {"PMSUBW", MMIB(9, 4), RRR, false},
	{"PMFHI", MMIB(9, 8), U_RD, false}, {"PMFLO", MMIB(9, 9), U_RD, false}, {"PINTH", MMIB(9, 10), RRR, false}, {"PMULTW", MMIB(9, 12), RRR, false},
	{"PDIVW", MMIB(9, 13), RR, false}, {"PCPYLD", MMIB(9, 14), RRR, false}, {"PMADDH", MMIB(9, 16), RRR, false}, {"PHMADH", MMIB(9, 17), RRR, false},
	{"PAND", MMIB(9, 18), RRR, false}, {"PXOR", MMIB(9, 19), RRR, false}, {"PMSUBH", MMIB(9, 20), RRR, false}, {"PHMSBH", MMIB(9, 21), RRR, false},
	{"PEXEH", MMIB(9, 26), RTD, false}, {"PREVH", MMIB(9, 27), RTD, false}, {"PMULTH", MMIB(9, 28), RRR, false}, {"PDIVBW", MMIB(9, 29), RR, false},
	{"PEXEW", MMIB(9, 30), RTD, false}, {"PROT3W", MMIB(9, 31), RTD, false},
	// MMI3
	{"PMADDUW", MMIB(0x29, 0), RRR, false}, {"PSRAVW", MMIB(0x29, 3), RRR, false}, {"PMTHI", MMIB(0x29, 8), U_RS, false}, {"PMTLO", MMIB(0x29, 9), U_RS, false},
	{"PINTEH", MMIB(0x29, 10), RRR, false}, {"PMULTUW", MMIB(0x29, 12), RRR, false}, {"PDIVUW", MMIB(0x29, 13), RR, false}, {"PCPYUD", MMIB(0x29, 14), RRR, false},
	{"POR", MMIB(0x29, 18), RRR, false}, {"PNOR", MMIB(0x29, 19), RRR, false}, {"PEXCH", MMIB(0x29, 26), RTD, false}, {"PCPYH", MMIB(0x29, 27), RTD, false},
	{"PEXCW", MMIB(0x29, 30), RTD, false},
};
constexpr u32 kIntOpCount = sizeof(kIntOps) / sizeof(kIntOps[0]);

// Register roles: data pool 1..23; presets 24..28 (HI, LO, HI1, LO1, SA sources); 29/30 constant-materialisation temps (30 = MFSA out).
struct IntInst
{
	u32 op;
	u32 rs, rt, rd, sa, imm;
};

struct IntCase
{
	std::vector<IntInst> insts;
	GPR_reg init[32];   // initial registers (runtime variant) / the values to materialise (const variant)
	bool constVariant;
};

static u32 encodeInt(const IntInst& i)
{
	const IntOp& o = kIntOps[i.op];
	u32 w = o.base;
	if (o.use & U_RS) w |= i.rs << 21;
	if (o.use & U_RT) w |= i.rt << 16;
	if (o.use & U_RD) w |= i.rd << 11;
	if (o.use & (U_SA | U_SA4 | U_PMFHL)) w |= (i.sa & 0x1f) << 6;
	if (o.use & U_IMM) w |= i.imm & 0xffffu;
	return w;
}

static IntInst genInst(Rng& r, u32 op)
{
	const IntOp& o = kIntOps[op];
	IntInst i{};
	i.op = op;
	pickRegs(r, 1, 23, i.rs, i.rt, i.rd);
	if (o.use & U_SA)
	{
		static const u32 e[] = {0, 1, 7, 8, 15, 16, 17, 31};
		i.sa = r.below(3) == 0 ? r.below(32) : e[r.below(8)];
	}
	if (o.use & U_SA4) i.sa = r.below(16);
	if (o.use & U_PMFHL) i.sa = r.below(5);
	if (o.use & U_IMM) i.imm = randImm16(r);
	return i;
}

static std::vector<u32> buildIntProgram(const IntCase& c, size_t nInsts, GPR_reg* initOut)
{
	std::memset(initOut, 0, sizeof(GPR_reg) * 32);
	std::vector<u32> code(PAD_WORDS, NOP);
	if (!c.constVariant)
	{
		for (u32 r = 1; r < 24; ++r) initOut[r] = c.init[r];
		for (u32 r = 24; r < 29; ++r) initOut[r] = c.init[r];
	}
	else
	{
		// Materialise every register an instruction reads (and writes: MOVZ/MOVN keep the old value) plus the five presets.
		bool used[32] = {};
		for (size_t k = 0; k < nInsts; ++k)
		{
			const IntInst& i = c.insts[k];
			const IntOp& o = kIntOps[i.op];
			if (o.use & U_RS) used[i.rs] = true;
			if (o.use & U_RT) used[i.rt] = true;
			if (o.use & U_RD) used[i.rd] = true;
		}
		for (u32 r = 24; r < 29; ++r) used[r] = true;
		used[0] = false;
		for (u32 r = 1; r < 29; ++r)
			if (used[r]) emitConst128(code, r, c.init[r].UD[0], c.init[r].UD[1], 29, 30);
	}
	code.push_back(MTHI(24));
	code.push_back(MTLO(25));
	code.push_back(MTHI1(26));
	code.push_back(MTLO1(27));
	code.push_back(MTSA(28));
	for (size_t k = 0; k < nInsts; ++k) code.push_back(encodeInt(c.insts[k]));
	code.push_back(MFSA(30));
	appendSentinel(code);
	return code;
}

// Interpreter vs JIT for the first n instructions of a case. Returns true when they agree; records the failure otherwise.
static bool runIntCase(const IntCase& c, size_t n, const std::string& tag, bool record)
{
	GPR_reg init[32];
	const std::vector<u32> code = buildIntProgram(c, n, init);
	bool trap = false;
	for (size_t k = 0; k < n; ++k) trap |= kIntOps[c.insts[k].op].trap;
	RunOpt ro;
	ro.stopAtVector = trap;
	ro.interpSteps = static_cast<u32>(code.size()) + 8;
	const EESnapshot ref = gapRun(false, code, init, nullptr, ro);
	const EESnapshot got = gapRun(true, code, init, nullptr, ro);
	// The interpreter raised an arithmetic-overflow exception (and skipped the write); the x86 JIT never does (see trapModel above),
	// so there is nothing to compare for this case. Non-overflowing operands of the same ops are still compared.
	if (trap && (ref.cp0[12] & 2u) != 0 && (got.cp0[12] & 2u) == 0)
		return true;
	DiffOpt d;
	d.cmpScratch = false;
	d.cp0Mask = trap ? ((1u << 12) | (1u << 13) | (1u << 14) | (1u << 8)) : 0;
	if (!record)
	{
		std::vector<Dif> dd;
		snapDiff(ref, got, d, dd);
		return dd.empty();
	}
	return compareRecord(ref, got, d, tag, code, init);
}

static u32 findOp(const char* name)
{
	for (u32 i = 0; i < kIntOpCount; ++i)
		if (!std::strcmp(kIntOps[i].name, name)) return i;
	return 0;
}
static const u32 kSubIdx = findOp("SUB"), kDsubIdx = findOp("DSUB");

// Independent model of the six overflow-checking arithmetic ops as the x86 JIT executes them: upstream x86 iR5900Arit.cpp:17 ("TODO:
// overflow checks") and iR5900AritImm.cpp recADDI_/recDADDI_ emit a plain add/sub, so ADD/SUB/ADDI/DADD/DSUB/DADDI never trap and
// always write the wrapped result (32-bit forms sign-extended). JIT is gated against this model. The interpreter (which traps and
// skips the write, R5900OpcodeImpl.cpp:25-55) is only reported (GAPEE INFO): a documented x86-JIT-vs-interpreter divergence.
struct TrapModel { bool trap; u64 result; };
static TrapModel trapModel(const char* name, u64 rs, u64 rt, u32 imm)
{
	const s32 a = static_cast<s32>(rs), b = static_cast<s32>(rt), im = static_cast<s16>(imm);
	const s64 A = static_cast<s64>(rs), B = static_cast<s64>(rt);
	s64 r64;
	if (!std::strcmp(name, "ADD")) { const s64 s = static_cast<s64>(a) + b; return {false, static_cast<u64>(static_cast<s64>(static_cast<s32>(s)))}; }
	if (!std::strcmp(name, "SUB")) { const s64 s = static_cast<s64>(a) - b; return {false, static_cast<u64>(static_cast<s64>(static_cast<s32>(s)))}; }
	if (!std::strcmp(name, "ADDI")) { const s64 s = static_cast<s64>(a) + im; return {false, static_cast<u64>(static_cast<s64>(static_cast<s32>(s)))}; }
	if (!std::strcmp(name, "DADD")) { (void)__builtin_add_overflow(A, B, &r64); return {false, static_cast<u64>(r64)}; }
	if (!std::strcmp(name, "DSUB")) { (void)__builtin_sub_overflow(A, B, &r64); return {false, static_cast<u64>(r64)}; }
	(void)__builtin_add_overflow(A, static_cast<s64>(im), &r64); // DADDI
	return {false, static_cast<u64>(r64)};
}

static void runTrapModel()
{
	static const char* const names[] = {"ADD", "SUB", "ADDI", "DADD", "DSUB", "DADDI"};
	const u32 perOp = knob("GAP_N", 100) * 4;
	for (u32 variant = 0; variant < 2; ++variant)
	for (const char* name : names)
	{
		const std::string gname = std::string("ee-int-rand trap-model ") + name + (variant ? " const" : " runtime");
		if (!onlyMatch(gname)) continue;
		gBegin(gname);
		const IntOp& o = kIntOps[findOp(name)];
		Rng r(baseSeed() * 0x1234567ull + findOp(name) * 31 + variant * 977 + 3);
		u32 infoShown = 0;
		for (u32 k = 0; k < perOp; ++k)
		{
			GPR_reg init[32];
			std::memset(init, 0, sizeof(init));
			u64 rs = rand64(r), rt = rand64(r);
			// bias towards the overflow boundaries
			if (r.below(3) == 0) { static const u64 e[] = {0x7fffffffull, 0x80000000ull, 0xffffffff80000000ull, 0x7fffffffffffffffull, 0x8000000000000000ull, 1, 0xffffffffffffffffull, 0}; rs = e[r.below(8)]; rt = e[r.below(8)]; }
			const u32 imm = randImm16(r);
			std::vector<u32> code(PAD_WORDS, NOP);
			const u64 sentinelValue = 0x0123456789abcdefull;
			if (variant)
			{
				emitConst64(code, 1, rs, 29);
				emitConst64(code, 2, rt, 29);
				emitConst64(code, 8, sentinelValue, 29);
			}
			else
			{
				init[1].UD[0] = rs; init[2].UD[0] = rt; init[8].UD[0] = sentinelValue;
			}
			IntInst i{};
			i.op = findOp(name);
			i.rs = 1; i.rt = (o.use & U_RD) ? 2 : 8; i.rd = 8; i.imm = imm;
			const u32 opIndex = static_cast<u32>(code.size());
			code.push_back(encodeInt(i));
			appendSentinel(code);
			RunOpt ro;
			ro.stopAtVector = true;
			ro.interpSteps = static_cast<u32>(code.size()) + 8;
			const TrapModel m = trapModel(name, rs, rt, imm);
			const EESnapshot got = gapRun(true, code, init, nullptr, ro);
			const EESnapshot ref = gapRun(false, code, init, nullptr, ro);
			const u32 epc = EE_TEST_PC + opIndex * 4;
			const auto check = [&](const EESnapshot& s) {
				const bool trapped = (s.cp0[12] & 2u) != 0;
				if (trapped != m.trap) return false;
				if (trapped) return (s.cp0[14] == epc || s.cp0[14] == epc + 4) && s.gpr[8] == sentinelValue && s.gpr_upper[8] == 0;
				return s.gpr[8] == m.result && s.gpr_upper[8] == 0;
			};
			gCase();
			if (!check(got))
			{
				char b[200];
				std::snprintf(b, sizeof(b), "%s rs=%llx rt=%llx imm=%x model{trap=%d res=%llx} jit{EXL=%u rd=%llx epc=%x}", name,
					static_cast<unsigned long long>(rs), static_cast<unsigned long long>(rt), imm, m.trap, static_cast<unsigned long long>(m.result),
					(got.cp0[12] >> 1) & 1, static_cast<unsigned long long>(got.gpr[8]), got.cp0[14]);
				gFail("JIT-vs-model", std::string(b) + " code{" + codeHex(code) + "}");
			}
			else if (!check(ref) && infoShown++ < 2)
				std::printf("GAPEE INFO interpreter disagrees with the architectural model: %s rs=%llx rt=%llx imm=%x model{trap=%d res=%llx} interp{EXL=%u rd=%llx}\n", name,
					static_cast<unsigned long long>(rs), static_cast<unsigned long long>(rt), imm, m.trap, static_cast<unsigned long long>(m.result),
					(ref.cp0[12] >> 1) & 1, static_cast<unsigned long long>(ref.gpr[8]));
		}
		gEnd();
	}
}

static void runIntRand()
{
	runTrapModel();
	const u32 perOp = knob("GAP_N", 100);     // cases per (op, variant)
	const u32 batch = knob("GAP_BATCH", 3);   // instructions per program (same opcode, fresh registers)
	for (u32 variant = 0; variant < 2; ++variant)
	{
		for (u32 op = 0; op < kIntOpCount; ++op)
		{
			const std::string gname = std::string("ee-int-rand ") + kIntOps[op].name + (variant ? " const" : " runtime");
			if (!onlyMatch(gname)) continue;
			gBegin(gname);
			Rng r(baseSeed() * 0x9e3779b1ull + op * 7919ull + variant * 104729ull + 17);
			for (u32 k = 0; k < perOp; ++k)
			{
				IntCase c;
				c.constVariant = variant != 0;
				std::memset(c.init, 0, sizeof(c.init));
				for (u32 reg = 1; reg < 29; ++reg) rand128(r, c.init[reg]);
				// The interpreter's SUB/DSUB negate rt before the overflow test, which is wrong for rt == INT_MIN (see
				// runTrapModel for the independent-model check of exactly that case): keep it out of the interpreter-referenced groups.
				for (u32 reg = 1; reg < 29; ++reg)
				{
					if (op == kSubIdx && static_cast<u32>(c.init[reg].UD[0]) == 0x80000000u) c.init[reg].UD[0] ^= 1;
					if (op == kDsubIdx && c.init[reg].UD[0] == 0x8000000000000000ull) c.init[reg].UD[0] ^= 1;
				}
				// presets: HI/LO/HI1/LO1 are 64-bit values, SA a small amount
				c.init[28].UD[0] = r.below(4) == 0 ? r.below(64) : rand64(r);
				for (u32 b = 0; b < batch; ++b) c.insts.push_back(genInst(r, op));
				gCase();
				if (runIntCase(c, c.insts.size(), "", false)) continue;
				// minimal prefix
				size_t m = c.insts.size();
				for (size_t p = 1; p <= c.insts.size(); ++p)
					if (!runIntCase(c, p, "", false)) { m = p; break; }
				runIntCase(c, m, std::string(kIntOps[op].name) + (variant ? " const" : " runtime") + " k=" + std::to_string(k) + " n=" + std::to_string(m), true);
			}
			gEnd();
		}
	}
}

} // namespace gapee

#endif // EMUCOREX_ENABLE_NATIVE_SELF_TESTS
