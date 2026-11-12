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
	std::printf("fmac-acc oracle: %u informational mismatches with exponent-255 operands below clamp 3 (not gated)\n", wild_failures);
	EmuCoreXOracleSetEeIrMode(-1);
	EmuCoreXOracleSetVUIR(0, -1);
	EmuCoreXOracleSetVUIR(1, -1);
	EmuConfig = saved;
}
