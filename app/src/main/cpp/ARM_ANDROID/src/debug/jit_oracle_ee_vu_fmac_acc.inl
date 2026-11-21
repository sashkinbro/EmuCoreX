// SPDX-FileCopyrightText: 2026 EmuCoreX Team
// SPDX-License-Identifier: GPL-3.0+
// Exhaustive JIT-vs-interpreter oracle for the VU FMAC ops that touch ACC
// (ADDA/SUBA/MADDA/MSUBA/MULA in bc/i/q/plain forms) plus the Fd-writing
// ADD/SUB/MUL/MADD/MSUB families, in EE COP2 macro mode (VU0) and VU0/VU1
// micro mode. Include after the EE run helpers. Entry: EEVuFmacAccTests().
// Suite: ee-vu-fmac-acc.

namespace fmac_acc
{
struct Op
{
	std::string name;
	u32 fn6;
	u32 idx;     // fd field for the ACC ops (opcode selector)
	bool usesFd; // idx is a real destination register instead
	bool acc;
};

static std::vector<Op> BuildOps()
{
	std::vector<Op> ops;
	static const char* lane[4] = {"x", "y", "z", "w"};
	struct K { const char* n; u32 bcIdx; u32 iT, iIdx; u32 qT, qIdx; u32 pT, pIdx; };
	const K kinds[5] = {
		{"ADDA", 0, 2, 8, 0, 8, 0, 10},
		{"SUBA", 1, 2, 9, 0, 9, 0, 11},
		{"MADDA", 2, 3, 8, 1, 8, 1, 10},
		{"MSUBA", 3, 3, 9, 1, 9, 1, 11},
		{"MULA", 6, 2, 7, 0, 7, 2, 10},
	};
	for (const K& k : kinds)
	{
		for (u32 bc = 0; bc < 4; ++bc)
			ops.push_back({std::string(k.n) + lane[bc], 0x3cu + bc, k.bcIdx, false, true});
		ops.push_back({std::string(k.n) + "i", 0x3cu + k.iT, k.iIdx, false, true});
		ops.push_back({std::string(k.n) + "q", 0x3cu + k.qT, k.qIdx, false, true});
		ops.push_back({std::string(k.n), 0x3cu + k.pT, k.pIdx, false, true});
	}
	const struct { const char* n; u32 base; } bcs[5] = {
		{"ADD", 0x00}, {"SUB", 0x04}, {"MADD", 0x08}, {"MSUB", 0x0c}, {"MUL", 0x18}};
	for (const auto& b : bcs)
		for (u32 bc = 0; bc < 4; ++bc)
			ops.push_back({std::string(b.n) + lane[bc], b.base + bc, 0, true, false});
	const struct { const char* n; u32 fn; } others[] = {
		{"MULq", 0x1c}, {"MULi", 0x1e}, {"ADDq", 0x20}, {"MADDq", 0x21}, {"ADDi", 0x22},
		{"MADDi", 0x23}, {"SUBq", 0x24}, {"MSUBq", 0x25}, {"SUBi", 0x26}, {"MSUBi", 0x27},
		{"ADD", 0x28}, {"MADD", 0x29}, {"MUL", 0x2a}, {"SUB", 0x2c}, {"MSUB", 0x2d}};
	for (const auto& o : others)
		ops.push_back({o.n, o.fn, 0, true, false});
	return ops;
}

struct Instr
{
	u32 op, mask, fs, ft, fd;
};

struct Rng
{
	u64 s;
	explicit Rng(u64 seed) : s(seed * 0x9e3779b97f4a7c15ull + 0x1234567ull) { next(); next(); }
	u32 next()
	{
		s ^= s << 13;
		s ^= s >> 7;
		s ^= s << 17;
		return static_cast<u32>(s >> 16);
	}
	u32 below(u32 n) { return next() % n; }
};

// Operands with exponent 255 are outside the documented JIT contract at
// clamp level 1, so they are generated only for the separate "wild" class.
static bool s_wild = false;

static u32 PickRaw(Rng& r)
{
	static const u32 edge[] = {
		0x00000000u, 0x80000000u, 0x3f800000u, 0xbf800000u, 0x7f7fffffu, 0xff7fffffu,
		0x7f800000u, 0xff800000u, 0x7fffffffu, 0xffffffffu, 0x7fc00000u, 0x00000001u,
		0x80000001u, 0x007fffffu, 0x807fffffu, 0x00800000u, 0x80800000u, 0x7f000000u,
		0x7e800000u, 0xfe800000u, 0x3f800001u, 0x01000000u, 0x7f400000u, 0x40000000u,
		0x3effffffu, 0x7f7ffffeu};
	const u32 kind = r.below(100);
	if (kind < 35)
		return edge[r.below(sizeof(edge) / sizeof(edge[0]))];
	if (kind < 75)
	{
		const u32 exp = 100 + r.below(40);
		return (r.next() & 0x80000000u) | (exp << 23) | (r.next() & 0x7fffffu);
	}
	return r.next();
}

static u32 PickValue(Rng& r)
{
	u32 v = PickRaw(r);
	if (!s_wild && ((v >> 23) & 0xffu) == 0xffu)
		v &= ~0x00800000u;
	return v;
}

static FmacInit MakeInit(Rng& r)
{
	FmacInit init{};
	for (u32 reg = 1; reg < 32; ++reg)
		for (u32 l = 0; l < 4; ++l)
			init.vf[reg * 4 + l] = PickValue(r);
	init.vf[3] = 0x3f800000u; // VF0.w
	for (u32 l = 0; l < 4; ++l)
	{
		init.vf[l] = l == 3 ? 0x3f800000u : 0u;
		init.acc[l] = PickValue(r);
	}
	init.i = PickValue(r);
	init.q = PickValue(r);
	return init;
}

static u32 Encode(const std::vector<Op>& ops, const Instr& in)
{
	const Op& op = ops[in.op];
	return ((in.mask & 15u) << 21) | (in.ft << 16) | (in.fs << 11) |
		((op.usesFd ? in.fd : op.idx) << 6) | op.fn6;
}

struct Result
{
	u32 vf[128];
	u32 acc[4];
	u32 vi[32];
};

static Result RunMacro(bool jit, const std::vector<u32>& uppers, const FmacInit& init)
{
	// Macro mode runs with the EE FPCR, exactly as the EE recompiler's caller does.
	const FPControlRegisterBackup callerFPCR(EmuConfig.Cpu.FPUFPCR);
	std::vector<u32> code;
	for (u32 u : uppers)
		code.push_back((0x12u << 26) | (1u << 25) | u);
	s_fmac_init = &init;
	const EESnapshot snap = jit ?
		RunEEProgramWithIrMode(0, code, nullptr, nullptr, nullptr, nullptr, false, init.vf) :
		RunEEProgram(false, code, false, nullptr, nullptr, nullptr, nullptr, nullptr, init.vf);
	s_fmac_init = nullptr;
	Result out;
	std::memcpy(out.vf, snap.vu0_vf, sizeof(out.vf));
	std::memcpy(out.acc, snap.vu0_acc, sizeof(out.acc));
	std::memcpy(out.vi, snap.vu0_vi, sizeof(out.vi));
	return out;
}

static Result RunMicro(u32 vu, bool jit, const std::vector<u32>& uppers, const FmacInit& init)
{
	std::vector<u32> program;
	for (u32 u : uppers)
	{
		program.push_back(NOP_L);
		program.push_back(u);
	}
	program.push_back(NOP_L);
	program.push_back(NOP_U | 0x40000000u);
	program.push_back(NOP_L);
	program.push_back(NOP_U);
	s_fmac_init = &init;
	const Snapshot snap = RunVU(vu, jit, program, 0x80, 0, 128, false, nullptr, init.vf);
	s_fmac_init = nullptr;
	Result out;
	std::memcpy(out.vf, snap.regs.VF, sizeof(out.vf));
	std::memcpy(out.acc, &snap.regs.ACC, sizeof(out.acc));
	for (u32 i = 0; i < 32; ++i)
		out.vi[i] = snap.regs.VI[i].UL;
	return out;
}

static bool Run2(u32 mode, const std::vector<u32>& uppers, const FmacInit& init, Result& ref, Result& got)
{
	if (mode == 0)
	{
		ref = RunMacro(false, uppers, init);
		got = RunMacro(true, uppers, init);
	}
	else
	{
		ref = RunMicro(mode - 1, false, uppers, init);
		got = RunMicro(mode - 1, true, uppers, init);
	}
	// VI[0..15] hold integer registers that nothing here touches; compare all of
	// VI so the MAC/STATUS/CLIP/I/Q slots are included.
	return std::memcmp(ref.vf, got.vf, sizeof(ref.vf)) == 0 &&
		std::memcmp(ref.acc, got.acc, sizeof(ref.acc)) == 0 &&
		std::memcmp(ref.vi, got.vi, sizeof(ref.vi)) == 0;
}

static std::string FirstDiff(const Result& ref, const Result& got)
{
	char buf[160];
	for (u32 i = 0; i < 128; ++i)
		if (ref.vf[i] != got.vf[i])
		{
			std::snprintf(buf, sizeof(buf), "VF%u.%c int=%08x jit=%08x", i / 4, "xyzw"[i & 3], ref.vf[i], got.vf[i]);
			return buf;
		}
	for (u32 i = 0; i < 4; ++i)
		if (ref.acc[i] != got.acc[i])
		{
			std::snprintf(buf, sizeof(buf), "ACC.%c int=%08x jit=%08x", "xyzw"[i], ref.acc[i], got.acc[i]);
			return buf;
		}
	for (u32 i = 0; i < 32; ++i)
		if (ref.vi[i] != got.vi[i])
		{
			std::snprintf(buf, sizeof(buf), "VI[%u]%s int=%08x jit=%08x", i,
				i == REG_MAC_FLAG ? "(mac)" : i == REG_STATUS_FLAG ? "(status)" : i == REG_CLIP_FLAG ? "(clip)" : "",
				ref.vi[i], got.vi[i]);
			return buf;
		}
	return "?";
}

static std::string MaskStr(u32 mask)
{
	std::string s;
	for (u32 l = 0; l < 4; ++l)
		if (mask & (8u >> l))
			s += "xyzw"[l];
	return s;
}

static void SetClamp(u32 clamp)
{
	auto& rc = EmuConfig.Cpu.Recompiler;
	rc.vu0Overflow = rc.vu1Overflow = clamp >= 1;
	rc.vu0ExtraOverflow = rc.vu1ExtraOverflow = clamp >= 2;
	rc.vu0SignOverflow = rc.vu1SignOverflow = clamp >= 3;
}
} // namespace fmac_acc

void EEVuFmacAccTests()
{
	using namespace fmac_acc;
	const auto saved = EmuConfig;
	const std::vector<Op> ops = BuildOps();
	const char* const mode_names[3] = {"macro", "micro0", "micro1"};
	constexpr u32 kSets = 10;
	constexpr u32 kSequences = 600;
	EmuCoreXOracleSetEeIrMode(0);
	EmuCoreXOracleSetVUIR(0, 0);
	EmuCoreXOracleSetVUIR(1, 0);
	std::printf("fmac-acc oracle: %zu ops, %u operand sets per op/mask, %u sequences per mode/clamp\n",
		ops.size(), kSets, kSequences);

	u32 wild_failures = 0;
	for (u32 wild = 0; wild < 2; ++wild)
	for (u32 clamp = 1; clamp <= 3; ++clamp)
	for (u32 mode = 0; mode < 3; ++mode)
	{
		SetClamp(clamp);
		s_wild = wild != 0;
		const char* const cls = wild ? "wild" : "finite";
		for (u32 oi = 0; oi < ops.size(); ++oi)
		{
			u32 fail_masks = 0, failures = 0;
			for (u32 mask = 1; mask < 16; ++mask)
			for (u32 n = 0; n < kSets; ++n)
			{
				Rng r(oi * 100003ull + mask * 1009ull + n * 17ull + 1);
				const FmacInit init = MakeInit(r);
				Instr in{oi, mask, r.below(5), r.below(5), r.below(5)};
				if (n == 0)
				{
					// Aliased/plain register shapes for the first set.
					in.fs = 1; in.ft = 2; in.fd = 3;
				}
				else if (n == 1)
					in.fd = in.fs = in.ft;
				const std::vector<u32> uppers = {Encode(ops, in)};
				Result ref, got;
				if (!Run2(mode, uppers, init, ref, got))
				{
					++failures;
					fail_masks |= 1u << mask;
					if (failures <= 2)
						std::printf("FMAC FAIL %s clamp=%u %s %s mask=%s fs=%u ft=%u fd=%u set=%u: %s | "
							"Fs=%08x,%08x,%08x,%08x Ft=%08x,%08x,%08x,%08x acc=%08x,%08x,%08x,%08x I=%08x Q=%08x\n",
							cls, clamp, mode_names[mode], ops[oi].name.c_str(), MaskStr(mask).c_str(), in.fs, in.ft, in.fd, n,
							FirstDiff(ref, got).c_str(),
							init.vf[in.fs * 4], init.vf[in.fs * 4 + 1], init.vf[in.fs * 4 + 2], init.vf[in.fs * 4 + 3],
							init.vf[in.ft * 4], init.vf[in.ft * 4 + 1], init.vf[in.ft * 4 + 2], init.vf[in.ft * 4 + 3],
							init.acc[0], init.acc[1], init.acc[2], init.acc[3], init.i, init.q);
				}
			}
			char name[128];
			std::snprintf(name, sizeof(name), "FMAC %s clamp=%u %s %s exact (failing masks 0x%04x, %u cases)",
				cls, clamp, mode_names[mode], ops[oi].name.c_str(), fail_masks >> 1, failures);
			// Exponent-255 operands are only informational below clamp 3; the JIT is not
			// contractually bit-exact for them there. Clamp 3 (full) must match exactly.
			if (wild && clamp != 3)
			{
				wild_failures += failures;
				if (failures)
					std::printf("INFO %s\n", name);
			}
			else
				Check(failures == 0, name);
		}
		// Random multi-instruction sequences: register cache and partial ACC bugs.
		u32 seq_failures = 0;
		for (u32 n = 0; n < kSequences; ++n)
		{
			Rng r(0xabcdef00ull + n * 7919ull);
			const FmacInit init = MakeInit(r);
			const u32 len = 2 + r.below(4);
			std::vector<u32> uppers;
			std::string desc;
			for (u32 k = 0; k < len; ++k)
			{
				Instr in{r.below(static_cast<u32>(ops.size())), 1 + r.below(15), r.below(5), r.below(5), r.below(5)};
				if (r.below(3) == 0)
					in.mask = (r.below(2) ? 8u >> r.below(4) : in.mask);
				uppers.push_back(Encode(ops, in));
				desc += " " + ops[in.op].name + "." + MaskStr(in.mask) + "(" + std::to_string(in.fd) + "," +
					std::to_string(in.fs) + "," + std::to_string(in.ft) + ")";
			}
			Result ref, got;
			if (!Run2(mode, uppers, init, ref, got))
			{
				++seq_failures;
				if (seq_failures <= 3)
					std::printf("FMAC SEQ FAIL %s clamp=%u %s n=%u:%s : %s\n", cls, clamp, mode_names[mode], n,
						desc.c_str(), FirstDiff(ref, got).c_str());
			}
		}
		char name[128];
		std::snprintf(name, sizeof(name), "FMAC %s clamp=%u %s random sequences exact (%u/%u failing)",
			cls, clamp, mode_names[mode], seq_failures, kSequences);
		if (wild && clamp != 3)
		{
			wild_failures += seq_failures;
			if (seq_failures)
				std::printf("INFO %s\n", name);
		}
		else
			Check(seq_failures == 0, name);
	}
	s_wild = false;

	// x86-model checks at clamp level 1 (Normal). The interpreter does not model x86's
	// per-opcode operand clamps, so expected values are asserted directly: upstream
	// microVU clamps Fs for MADDx/y/z/w and, for COP2 MADDw, ACC and Ft as well.
	SetClamp(1);
	{
		const auto find_op = [&](const char* n) {
			for (u32 i = 0; i < ops.size(); ++i)
				if (ops[i].name == n)
					return i;
			return 0u;
		};
		const u32 inf_p = 0x7f800000u, inf_n = 0xff800000u, nan_p = 0x7fc00000u, one = 0x3f800000u;
		const u32 max_p = 0x7f7fffffu, max_n = 0xff7fffffu;
		for (u32 mode = 0; mode < 3; ++mode)
		for (u32 l = 0; l < 4; ++l)
		{
			// Fs is non-finite, Ft lane is zero, ACC = 1.0: x86 gives max*0 + 1 = 1.0 in every lane.
			const char* const nm[4] = {"MADDx", "MADDy", "MADDz", "MADDw"};
			FmacInit init{};
			init.vf[3] = one;
			const u32 fsv[4] = {inf_p, inf_n, inf_p, nan_p};
			for (u32 k = 0; k < 4; ++k)
			{
				init.vf[1 * 4 + k] = fsv[k];
				init.acc[k] = one;
			}
			const u32 oi = find_op(nm[l]);
			const std::vector<u32> uppers = {Encode(ops, Instr{oi, 15, 1, 2, 3})};
			const Result r = mode == 0 ? RunMacro(true, uppers, init) : RunMicro(mode - 1, true, uppers, init);
			char name[128];
			std::snprintf(name, sizeof(name), "FMAC x86-model clamp=1 %s %s non-finite Fs * 0 + 1.0 == 1.0",
				mode_names[mode], nm[l]);
			Check(r.vf[12] == one && r.vf[13] == one && r.vf[14] == one && r.vf[15] == one, name);
		}
		{
			// COP2 MADDw also clamps ACC (and Ft). Fs = 0, Ft.w = 1.0: result is the clamped ACC
			// (+/-Inf -> +/-max, +/-qNaN -> +max like MINPS/MAXPS).
			FmacInit init{};
			init.vf[3] = one;
			init.acc[0] = inf_p; init.acc[1] = inf_n; init.acc[2] = 0x7fc00001u; init.acc[3] = 0xffc00001u; // quiet NaNs (FMINNM folds signaling NaNs differently from MINPS)
			for (u32 k = 0; k < 4; ++k)
				init.vf[2 * 4 + k] = one;
			const std::vector<u32> uppers = {Encode(ops, Instr{find_op("MADDw"), 15, 1, 2, 3})};
			const Result r = RunMacro(true, uppers, init);
			Check(r.vf[12] == max_p && r.vf[13] == max_n && r.vf[14] == max_p && r.vf[15] == max_p, // Normal clamp folds any NaN to +max
				"FMAC x86-model clamp=1 macro MADDw clamps ACC");
			// Ft.w = +Inf with Fs = 0: Ft is clamped, so 0 * max + 1.0 == 1.0.
			FmacInit init2{};
			init2.vf[3] = one;
			for (u32 k = 0; k < 4; ++k)
			{
				init2.acc[k] = one;
				init2.vf[2 * 4 + k] = inf_p;
			}
			const Result r2 = RunMacro(true, uppers, init2);
			Check(r2.vf[12] == one && r2.vf[13] == one && r2.vf[14] == one && r2.vf[15] == one,
				"FMAC x86-model clamp=1 macro MADDw clamps Ft");
		}
	}

	std::printf("fmac-acc oracle: %u informational mismatches with exponent-255 operands below clamp 3 (not gated)\n", wild_failures);
	// CTC2 -> CFC2 round trip for every VU0 control register, JIT vs interpreter (VI regs, I, R,
	// flags). Regs written before and after in the same block exercise the EE VI register cache.
	for (u32 rd = 1; rd < 32; ++rd)
	for (u32 variant = 0; variant < 3; ++variant)
	{
		const u32 pat[3] = {0xdeadbeefu, 0x00000005u, 0xfffff0f3u};
		GPR_reg gpr[32]{};
		gpr[8].UD[0] = 0xffffffff00000000ull | pat[variant];
		if (rd == 28) // FBRST: the interpreter stores before the reset bits clear it, x86/ours after; compare without reset/break bits
			gpr[8].UD[0] &= ~0x303ull;
		gpr[10].UD[0] = 0x1111;
		const auto cop2 = [](u32 rs, u32 rt, u32 fs) { return (0x12u << 26) | (rs << 21) | (rt << 16) | (fs << 11); };
		std::vector<u32> code = {cop2(6, 10, 1), cop2(6, 8, rd), cop2(2, 9, rd), cop2(2, 11, 1)}; // CTC2 vi1; CTC2 rd; CFC2 rd; CFC2 vi1
		if (variant == 2)
			code = {cop2(6, 8, rd), cop2(6, 10, 1), cop2(2, 9, rd), cop2(2, 11, 1), cop2(6, 8, rd), cop2(2, 12, rd)};
		const EESnapshot interp = RunEEProgram(false, code, false, nullptr, nullptr, gpr);
		const EESnapshot jit = RunEEProgram(true, code, false, nullptr, nullptr, gpr);
		char name[96];
		std::snprintf(name, sizeof(name), "CTC2/CFC2 round trip vi%u variant %u JIT == interpreter", rd, variant);
		CompareEE(interp, jit, name);
	}

	EmuCoreXOracleSetEeIrMode(-1);
	EmuCoreXOracleSetVUIR(0, -1);
	EmuCoreXOracleSetVUIR(1, -1);
	EmuConfig = saved;
}

// CTC2 STATUS shadow flags and VU0 micro-program / EE VI access ordering (cycle commit before VU0 sync).
void EEVuMicroSyncTests()
{
	const auto saved = EmuConfig;
	EmuCoreXOracleSetEeIrMode(0);
	EmuCoreXOracleSetVUIR(0, 0);
	EmuCoreXOracleSetVUIR(1, 0);
	// CTC2 STATUS must refresh the microVU sticky/status shadow (micro_statusflags) exactly like
	// x86 mVUallocSFLAGd (SHL 11 / SHL 14, not SHR). The interpreter does not maintain the shadow,
	// so check the JIT result against the architectural reference.
	for (u32 pat : {0x00000000u, 0x00000fc0u, 0x00000040u, 0x00000080u, 0x00000100u, 0x00000200u, 0x00000400u, 0x00000800u, 0xffffffffu, 0x5a5a5a5au})
	{
		GPR_reg gpr[32]{};
		gpr[8].UD[0] = pat;
		const auto cop2 = [](u32 rs, u32 rt, u32 fs) { return (0x12u << 26) | (rs << 21) | (rt << 16) | (fs << 11); };
		const std::vector<u32> code = {cop2(6, 8, REG_STATUS_FLAG)};
		RunEEProgram(true, code, false, nullptr, nullptr, gpr);
		const u32 st = pat & 0xfc0u;
		const u32 ref = ((st >> 3) & 0x18u) | ((st << 11) & 0x1800u) | ((st << 14) & 0x3cf0000u);
		bool ok = true;
		for (u32 lane = 0; lane < 4; ++lane)
			ok = ok && VU0.micro_statusflags[lane] == ref;
		Check(ok && VU0.VI[REG_STATUS_FLAG].UL == st, "JIT CTC2 STATUS shadow flags match mVUallocSFLAGd (SHL)");
	}

	// A running VU0 micro program writes VI1 after N micro cycles while the EE executes M more
	// instructions inside the same block before CTC2/CFC2 touch VI1: the cycle commit before the
	// VU0 sync decides whether the micro write lands before or after the EE access.
	{
		const auto cop2 = [](u32 rs, u32 rt, u32 fs) { return (0x12u << 26) | (rs << 21) | (rt << 16) | (fs << 11); };
		const u32 NOP_L = 0x8000033cu, NOP_U = 0x000002ffu, E_U = 0x400002ffu;
		const u32 IADDIU_VI1_0A = 0x1001000au;
		for (u32 delay : {1u, 4u, 12u, 30u})
		for (u32 ee_nops : {0u, 2u, 6u, 14u, 40u, 120u})
		for (u32 mode = 0; mode < 6; ++mode)
		{
			std::memset(VU0.Micro, 0, 4096);
			u32 pc = 0;
			for (u32 i = 0; i < delay; ++i, pc += 8)
			{
				std::memcpy(VU0.Micro + pc, &NOP_L, 4);
				std::memcpy(VU0.Micro + pc + 4, &NOP_U, 4);
			}
			std::memcpy(VU0.Micro + pc, &IADDIU_VI1_0A, 4);
			std::memcpy(VU0.Micro + pc + 4, &NOP_U, 4);
			pc += 8;
			std::memcpy(VU0.Micro + pc, &NOP_L, 4);
			std::memcpy(VU0.Micro + pc + 4, &E_U, 4);
			pc += 8;
			std::memcpy(VU0.Micro + pc, &NOP_L, 4);
			std::memcpy(VU0.Micro + pc + 4, &NOP_U, 4);
			CpuVU0->Clear(0, 4096);

			GPR_reg gpr[32]{};
			gpr[8].UD[0] = 0x77;
			gpr[10].UD[0] = 0x1;
			std::vector<u32> code;
			code.push_back(cop2(6, 10, 1)); // CTC2 vi1 = 1
			code.push_back((0x12u << 26) | (1u << 25) | (0u << 6) | 0x38u); // VCALLMS 0
			for (u32 i = 0; i < ee_nops; ++i)
				code.push_back(0);
			switch (mode)
			{
				case 0: code.push_back(cop2(6, 8, 1)); code.push_back(cop2(2, 9, 1)); break; // CTC2 then CFC2
				case 1: code.push_back(cop2(2, 9, 1)); code.push_back(cop2(6, 8, 1)); code.push_back(cop2(2, 11, 1)); break;
				case 2: code.push_back(cop2(2, 9, 1)); break; // CFC2 alone
				case 3: code.push_back(cop2(6, 8, 1) | 1u); code.push_back(cop2(2, 9, 1)); break; // interlocked CTC2
				case 4: code.push_back(cop2(2, 9, 1) | 1u); break; // interlocked CFC2
				default: code.push_back(cop2(6, 8, 1)); code.push_back(0); code.push_back(cop2(2, 9, 1)); code.push_back(cop2(6, 10, 1)); code.push_back(cop2(2, 11, 1)); break;
			}
			const EESnapshot interp = RunEEProgram(false, code, false, nullptr, nullptr, gpr);
			const EESnapshot jit = RunEEProgram(true, code, false, nullptr, nullptr, gpr);
			char name[112];
			std::snprintf(name, sizeof(name), "VU0 micro VI1 write vs EE CTC2/CFC2 delay %u nops %u mode %u JIT == interpreter", delay, ee_nops, mode);
			CompareEE(interp, jit, name);
		}
	}

	EmuCoreXOracleSetEeIrMode(-1);
	EmuCoreXOracleSetVUIR(0, -1);
	EmuCoreXOracleSetVUIR(1, -1);
	EmuConfig = saved;
}

// VU0 micro program ending in an E-bit whose last FMAC writes MAC, followed by EE reads of MAC/STATUS/CLIP and a
// COP2 macro op, optionally with the MAC reader in a successor block. Port-only "MAC flag-finalise parking" once
// left the final MAC instance in the wrong ring slot (Zone of the Enders 2 robot arm); the interpreter is the reference.
void EEVuMicroMacTailTests()
{
	const auto saved = EmuConfig;
	EmuCoreXOracleSetEeIrMode(0);
	EmuCoreXOracleSetVUIR(0, 0);
	EmuCoreXOracleSetVUIR(1, 0);
	const auto cop2 = [](u32 rs, u32 rt, u32 fs) { return (0x12u << 26) | (rs << 21) | (rt << 16) | (fs << 11); };
	const u32 NOP_L = 0x8000033cu, NOP_U = 0x000002ffu, E_BIT = 0x40000000u;
	const u32 UP_SUB_A = 0x01e00000u | (2u << 16) | (1u << 11) | (3u << 6) | 0x2cu; // SUB vf3 = vf1 - vf2
	const u32 UP_ADD_B = 0x01e00000u | (2u << 16) | (1u << 11) | (6u << 6) | 0x28u; // ADD vf6 = vf1 + vf2
	const u32 LO_FMAND = (0x1au << 25) | (3u << 16) | (4u << 11);
	const u32 LO_FSAND = (0x16u << 25) | (3u << 16) | (1u << 21) | 0x7ffu;
	const u32 LO_FSSET = (0x15u << 25) | 0x080u;
	const u32 readers[4] = {NOP_L, LO_FMAND, LO_FSAND, LO_FSSET};
	const char* const reader_names[4] = {"none", "FMAND", "FSAND", "FSSET"};
	u32 vf[128]{};
	const u32 one = 0x3f800000u, mone = 0xbf800000u;
	vf[4 + 0] = one; vf[4 + 1] = mone; vf[4 + 2] = 0; vf[4 + 3] = 0x7f000000u;
	vf[8 + 0] = mone; vf[8 + 1] = one; vf[8 + 2] = 0; vf[8 + 3] = 0x7f000000u;
	struct Slot { u32 lo, up; };
	u32 cases = 0;
	const auto run_case = [&](const std::vector<Slot>& slots, bool msr, u32 nops, u32 tail, const char* what) {
		std::memset(VU0.Micro, 0, 4096);
		u32 pc = 0;
		for (const Slot& sl : slots)
		{
			std::memcpy(VU0.Micro + pc, &sl.lo, 4);
			std::memcpy(VU0.Micro + pc + 4, &sl.up, 4);
			pc += 8;
		}
		for (u32 k = 0; k < 2; ++k, pc += 8)
		{
			std::memcpy(VU0.Micro + pc, &NOP_L, 4);
			std::memcpy(VU0.Micro + pc + 4, &NOP_U, 4);
		}
		CpuVU0->Clear(0, 4096);
		GPR_reg gpr[32]{};
		gpr[10].UD[0] = 0xffff;
		std::vector<u32> code;
		code.push_back(cop2(6, 10, 4)); // CTC2 vi4 = 0xffff
		if (msr)
		{
			code.push_back(cop2(6, 0, 27)); // CTC2 CMSAR0 = 0
			code.push_back((0x12u << 26) | (1u << 25) | 0x39u); // VCALLMSR
		}
		else
			code.push_back((0x12u << 26) | (1u << 25) | 0x38u); // VCALLMS 0
		for (u32 i = 0; i < nops; ++i)
			code.push_back(0);
		const u32 vadd5 = (0x12u << 26) | (1u << 25) | (0xfu << 21) | (2u << 16) | (1u << 11) | (5u << 6) | 0x28u;
		if (tail == 0)
		{
			code.push_back(cop2(2, 20, 17)); code.push_back(cop2(2, 21, 16)); code.push_back(cop2(2, 22, 18));
		}
		else if (tail == 1)
		{
			code.push_back(vadd5); code.push_back(cop2(2, 20, 17)); code.push_back(cop2(2, 21, 16));
		}
		else
		{
			code.push_back(cop2(2, 21, 16)); code.push_back(vadd5); code.push_back(cop2(2, 22, 18)); code.push_back(cop2(2, 20, 17));
		}
		const EESnapshot ref = RunEEProgram(false, code, false, nullptr, nullptr, gpr, nullptr, nullptr, vf);
		const EESnapshot jit = RunEEProgram(true, code, false, nullptr, nullptr, gpr, nullptr, nullptr, vf);
		char name[160];
		std::snprintf(name, sizeof(name), "VU0 MAC tail %s %s nops %u tail %u JIT == interpreter", what, msr ? "VCALLMSR" : "VCALLMS", nops, tail);
		CompareEE(ref, jit, name);
		++cases;
	};
	// Inline: last FMAC fm_off slots relative to the E-bit slot (e=4), MAC/STATUS reader rd slots relative to the FMAC.
	for (int fm_off = -2; fm_off <= 1; ++fm_off)
	for (u32 rk = 0; rk < 4; ++rk)
	for (int rd = -1; rd <= 1; ++rd)
	for (u32 msr = 0; msr < 2; ++msr)
	for (u32 nops : {0u, 40u})
	for (u32 tail = 0; tail < 3; ++tail)
	{
		const int e = 4, fm = e + fm_off, r = fm + rd;
		if (r < 1 || r > e + 1 || (rk == 0 && rd != 0))
			continue;
		std::vector<Slot> slots(e + 2, Slot{NOP_L, NOP_U});
		slots[0].up = UP_SUB_A;
		slots[fm].up = UP_ADD_B;
		slots[e].up |= E_BIT;
		slots[r].lo = readers[rk];
		char what[64];
		std::snprintf(what, sizeof(what), "inline fm%+d %s@%+d", fm_off, reader_names[rk], rd);
		run_case(slots, msr != 0, nops, tail, what);
	}
	// Successor block: branch (taken IBEQ / not-taken IBNE), last FMAC in the branch slot or its delay slot, reader in the successor.
	for (u32 taken = 0; taken < 2; ++taken)
	for (u32 fm = 1; fm <= 2; ++fm)
	for (u32 rk = 0; rk < 4; ++rk)
	for (u32 msr = 0; msr < 2; ++msr)
	for (u32 nops : {0u, 40u})
	for (u32 tail = 0; tail < 3; ++tail)
	{
		std::vector<Slot> slots(7, Slot{NOP_L, NOP_U});
		slots[0].up = UP_SUB_A;
		slots[1].lo = ((taken ? 0x28u : 0x29u) << 25) | 2u; // IBEQ / IBNE vi0,vi0 -> slot 4
		slots[fm].up = UP_ADD_B;
		slots[3].lo = readers[rk];
		slots[4].lo = readers[rk];
		slots[5].up |= E_BIT;
		char what[64];
		std::snprintf(what, sizeof(what), "branch %s fm@%u %s", taken ? "taken" : "nottaken", fm, reader_names[rk]);
		run_case(slots, msr != 0, nops, tail, what);
	}
	// Writer block with a flag reader after the last FMAC (so the finalised MAC write is observable inside the block),
	// then a branch to successor blocks that end the program with the E-bit and may read MAC themselves (CFC2 sees the
	// MAC the writer block hands over).
	for (u32 taken = 0; taken < 2; ++taken)
	for (u32 fm = 1; fm <= 2; ++fm)
	for (u32 rd = 0; rd <= 1; ++rd)
	for (u32 rk = 1; rk < 4; ++rk)
	for (u32 srk = 0; srk < 4; ++srk)
	for (u32 msr = 0; msr < 2; ++msr)
	for (u32 nops : {0u, 40u})
	for (u32 tail = 0; tail < 3; ++tail)
	{
		const u32 r = fm + rd, b = fm + 2;
		std::vector<Slot> slots(b + 6, Slot{NOP_L, NOP_U});
		slots[0].up = UP_SUB_A;
		slots[fm].up = UP_ADD_B;
		slots[r].lo = readers[rk];
		slots[b].lo = ((taken ? 0x28u : 0x29u) << 25) | 2u; // IBEQ / IBNE vi0,vi0 -> slot b+3
		slots[b + 2].up |= E_BIT;
		slots[b + 2].lo = readers[srk];
		slots[b + 3].up |= E_BIT;
		slots[b + 3].lo = readers[srk];
		char what[64];
		std::snprintf(what, sizeof(what), "reader-block %s fm@%u %s@%+d succ %s", taken ? "taken" : "nottaken", fm, reader_names[rk], static_cast<int>(rd), reader_names[srk]);
		run_case(slots, msr != 0, nops, tail, what);
	}
	// Random VU0 programs (FMAC ops with random dest masks, flag readers, forward branches and short counted loops, E-bit),
	// one or two run back to back, read back through COP2 CFC2 of MAC/STATUS/CLIP. Diagnostic sweep
	// (EMUCOREX_MACTAIL_RAND=<count>, EMUCOREX_MACTAIL_DENSE=1 for denser FMAC/readers): used to find end-of-program
	// hand-off mismatches that appear with one flag-finalise scheme but not another.
	if (const char* rc = std::getenv("EMUCOREX_MACTAIL_RAND"))
	{
		const u32 count = static_cast<u32>(std::atoi(rc));
		const bool dense = std::getenv("EMUCOREX_MACTAIL_DENSE") != nullptr;
		const bool mbit = std::getenv("EMUCOREX_MACTAIL_MBIT") != nullptr; // M-bits on some uppers, EE reads MAC while the program may still run
		u64 rng = 0x9e3779b97f4a7c15ull;
		const auto rnd = [&](u32 n) { rng = rng * 6364136223846793005ull + 1442695040888963407ull; return static_cast<u32>((rng >> 33) % n); };
		const u32 fopsU[3] = {0x28u, 0x2cu, 0x2au};
		const bool loops = std::getenv("EMUCOREX_MACTAIL_LOOP") != nullptr; // counted backward loop (IADDIU/ISUBIU vi1 + IBNE) in every program
		const auto genProgram = [&](u32 base, u32 n) {
			const u32 e = n - 1;
			std::vector<Slot> slots(n + 2, Slot{NOP_L, NOP_U});
			u32 branchAt = loops ? 0u : 0xffffu; // loop programs have no random forward branches
			for (u32 s = 0; s < n; ++s)
			{
				if (rnd(100) < (dense ? 80u : 55u))
				{
					const u32 mask = rnd(4) == 0 ? 15u : 1 + rnd(15);
					slots[s].up = (1u << 25) | (mask << 21) | ((1 + rnd(2)) << 16) | ((1 + rnd(2)) << 11) | ((3 + rnd(7)) << 6) | fopsU[rnd(3)];
				}
				if (mbit && s < e && rnd(100) < 12)
					slots[s].up |= 0x20000000u;
				if (s == branchAt + 1)
					continue;
				const u32 lr = rnd(100);
				if (lr < (dense ? 45u : 30u))
				{
					const u32 it = 2 + rnd(6), is = 2 + rnd(6);
					switch (rnd(7))
					{
						case 0: slots[s].lo = (0x1au << 25) | (it << 16) | (is << 11); break; // FMAND
						case 1: slots[s].lo = (0x18u << 25) | (it << 16) | (is << 11); break; // FMEQ
						case 2: slots[s].lo = (0x1bu << 25) | (it << 16) | (is << 11); break; // FMOR
						case 3: slots[s].lo = (0x16u << 25) | (it << 16) | (is << 21) | (rnd(0x1000)); break; // FSAND
						case 4: slots[s].lo = (0x12u << 25) | (rnd(0x1000)); break; // FCAND
						case 5: slots[s].lo = (0x17u << 25) | (it << 16) | (rnd(0x1000)); break; // FSOR
						default: slots[s].lo = (0x15u << 25) | (rnd(0x1000)); break; // FSSET
					}
				}
				else if (lr < (dense ? 55u : 38u) && branchAt == 0xffff && s + 3 <= e && s >= 1)
				{
					branchAt = s;
					const u32 target = s + 2 + rnd(e - s - 1); // pairs ahead of the delay slot, never past the E-bit slot
					slots[s].lo = ((rnd(2) ? 0x28u : 0x29u) << 25) | (target - s - 1);
				}
			}
			slots[e].up |= E_BIT;
			if (loops)
			{
				const u32 m = 1 + rnd(std::max(1u, e - 4)); // loop body is slots 1..m, then ISUBIU, IBNE with its delay slot
				if (m + 3 <= e)
				{
					slots[0].lo = 0x10010002u + (rnd(2) << 0); // IADDIU vi1 = vi0 + 2/3
					slots[m + 1].lo = (0x09u << 25) | (1u << 16) | (1u << 11) | 1u; // ISUBIU vi1 = vi1 - 1
					slots[m + 2].lo = (0x29u << 25) | (1u << 16) | ((1u - (m + 2) - 1u) & 0x7ffu); // IBNE vi1, vi0 -> slot 1
					slots[m + 3].lo = NOP_L;
				}
			}
			for (u32 s = 0; s < n + 2; ++s)
			{
				std::memcpy(VU0.Micro + base + s * 8, &slots[s].lo, 4);
				std::memcpy(VU0.Micro + base + s * 8 + 4, &slots[s].up, 4);
			}
		};
		for (u32 k = 0; k < count; ++k)
		{
			const bool two = rnd(2) != 0;
			std::memset(VU0.Micro, 0, 4096);
			for (u32 base = 0; base < 0x400; base += 8)
			{
				std::memcpy(VU0.Micro + base, &NOP_L, 4);
				std::memcpy(VU0.Micro + base + 4, &NOP_U, 4);
			}
			const u32 n1 = 7 + rnd(dense ? 14 : 6), n2 = 5 + rnd(8);
			genProgram(0, n1);
			if (two)
				genProgram(0x200, n2);
			CpuVU0->Clear(0, 4096);
			GPR_reg gpr[32]{};
			gpr[10].UD[0] = 0xffff;
			std::vector<u32> code;
			code.push_back(cop2(6, 10, 4));
			code.push_back((0x12u << 26) | (1u << 25) | 0x38u);
			const u32 nops = mbit ? rnd(30) : (rnd(3) == 0 ? 0u : (rnd(2) ? 12u : 40u));
			for (u32 i = 0; i < nops; ++i)
				code.push_back(0);
			if (mbit)
			{
				code.push_back(cop2(2, 23, 17)); // CFC2 MAC while the program may be stopped at an M-bit
				for (u32 i = 0, m = rnd(3) * 20; i < m; ++i)
					code.push_back(0);
			}
			if (two)
			{
				code.push_back((0x12u << 26) | (1u << 25) | (0x200u >> 3 << 6) | 0x38u); // VCALLMS 0x200
				const u32 nops2 = rnd(3) == 0 ? 0u : 40u;
				for (u32 i = 0; i < nops2; ++i)
					code.push_back(0);
			}
			const u32 tail = rnd(3);
			if (tail == 0) { code.push_back(cop2(2, 20, 17)); code.push_back(cop2(2, 21, 16)); code.push_back(cop2(2, 22, 18)); }
			else if (tail == 1) { code.push_back(cop2(2, 22, 18)); code.push_back(cop2(2, 20, 17)); }
			else { code.push_back(cop2(2, 21, 16)); code.push_back(cop2(2, 20, 17)); }
			const EESnapshot ref = RunEEProgram(false, code, false, nullptr, nullptr, gpr, nullptr, nullptr, vf);
			const EESnapshot jit = RunEEProgram(true, code, false, nullptr, nullptr, gpr, nullptr, nullptr, vf);
			char name[160];
			std::snprintf(name, sizeof(name), "VU0 MAC tail rand %u two %u n %u/%u nops %u tail %u JIT == interpreter", k, two ? 1u : 0u, n1, n2, nops, tail);
			CompareEE(ref, jit, name);
			++cases;
		}
	}
	std::printf("VU0 MAC tail: %u cases\n", cases);
	EmuCoreXOracleSetEeIrMode(-1);
	EmuCoreXOracleSetVUIR(0, -1);
	EmuCoreXOracleSetVUIR(1, -1);
	EmuConfig = saved;
}
