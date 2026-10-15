// SPDX-FileCopyrightText: 2026 EmuCoreX Team
// SPDX-License-Identifier: GPL-3.0+
// EE COP1 raw scalar compare integration tests. Included from jit_oracle.cpp
// after the common EE oracle helpers.

extern "C" void EmuCoreXOracleGetEeIrCompareCounts(u64* clear, u64* equal, u64* less, u64* less_equal);

static u32 EeCop1CompareNormalize(u32 raw)
{
	const u32 exponent = raw & 0x7f800000u;
	if (exponent == 0)
		return 0; // Canonical comparison zero; fpuDouble preserves its zero sign.
	if (exponent == 0x7f800000u)
		return (raw & 0x80000000u) | 0x7f7fffffu; // Includes infinities and every NaN payload.
	return raw;
}

static u32 EeCop1CompareOrderKey(u32 normalized)
{
	return (normalized & 0x80000000u) ? ~normalized : (normalized ^ 0x80000000u);
}

static bool EeCop1CompareExpected(u32 funct, u32 lhs, u32 rhs)
{
	if (funct == 0x30) // C.F always clears the condition bit.
		return false;
	const u32 a = EeCop1CompareNormalize(lhs);
	const u32 b = EeCop1CompareNormalize(rhs);
	const bool equal = a == b;
	const bool less = EeCop1CompareOrderKey(a) < EeCop1CompareOrderKey(b);
	switch (funct)
	{
		case 0x32: return equal;          // C.EQ.S
		case 0x34: return less;            // C.LT.S
		case 0x36: return less || equal;   // C.LE.S
		default: return false;
	}
}

static void EeCop1CompareReadCounts(u64 counts[4])
{
	EmuCoreXOracleGetEeIrCompareCounts(&counts[0], &counts[1], &counts[2], &counts[3]);
}

void EEIrCop1CompareTests()
{
	struct Pair { u32 lhs; u32 rhs; };
	constexpr std::array<Pair, 12> pairs = {{
		{0x00000000u, 0x80000000u}, // signed zeros normalize to equal +0.
		{0x00000001u, 0x80000001u}, // signed subnormals normalize to equal +0.
		{0x007fffffu, 0x00800000u}, // largest positive subnormal vs smallest normal.
		{0x807fffffu, 0x80800000u}, // largest negative subnormal vs smallest negative normal.
		{0x3f800000u, 0xbf800000u}, // opposite signs.
		{0xc0400000u, 0xc0000000u}, // two negative finite values.
		{0x3f800000u, 0x3f800000u}, // same finite encoding.
		{0x7f800000u, 0x7f7fffffu}, // +infinity clamps to +max finite.
		{0xff800000u, 0xff7fffffu}, // -infinity clamps to -max finite.
		{0x7fc12345u, 0x7f812345u}, // positive qNaN and sNaN clamp to same value.
		{0xffc54321u, 0xff854321u}, // negative qNaN and sNaN clamp to same value.
		{0x7fa12345u, 0xffc54321u}, // opposite-sign NaN payloads clamp then order.
	}};
	constexpr std::array<u32, 4> functions = {0x30, 0x32, 0x34, 0x36}; // C.F, C.EQ.S, C.LT.S, C.LE.S.
	constexpr std::array<std::array<u32, 2>, 4> sources = {{{{5, 9}}, {{7, 7}}, {{0, 9}}, {{5, 0}}}};
	constexpr std::array<u32, 4> counter_index = {0, 1, 2, 3};
	std::array<GPR_reg, 32> initial_gpr{};
	std::array<u32, 32> initial_fpr{}, initial_fcr{}, initial_cp0{};
	initial_cp0[12] = 0x20000000u; // Status.CU1.
	for (u32 reg = 1; reg < 32; ++reg)
	{
		initial_gpr[reg].UD[0] = 0x1234567800000000ull | (0x76543210u + reg);
		initial_gpr[reg].UD[1] = 0xabcdef0100000000ull | (0x89abcdefu + reg);
		initial_fpr[reg] = 0x51000000u ^ (reg * 0x01020408u);
		initial_fcr[reg] = 0x87654321u ^ (reg * 0x01020304u);
	}
	initial_gpr[0] = {};
	initial_fcr[0] = 0x2e00u;
	const FPControlRegister original_fpcr = EmuConfig.Cpu.FPUFPCR;
	bool direct_all = true;

	const auto check_case = [&](u32 funct, const Pair& pair, u32 alias, u32 fpcr_case)
	{
		// Exercise all four rounding modes and each combination of the host's
		// flush-to-zero/default-NaN controls. Compare semantics themselves are integer
		// predicates over fpuDouble-normalized words and must not depend on these.
		FPControlRegister fpcr = original_fpcr;
		fpcr.SetRoundMode(static_cast<FPRoundMode>(fpcr_case & 3u));
		fpcr.SetFlushToZero((fpcr_case & 4u) != 0);
		if (fpcr_case & 8u) fpcr.bitmask |= 1ull << 25;
		else fpcr.bitmask &= ~(1ull << 25);
		EmuConfig.Cpu.FPUFPCR = fpcr;
		const FPControlRegisterBackup host_fpcr(fpcr);

		auto gpr = initial_gpr;
		auto fpr = initial_fpr;
		auto fcr = initial_fcr;
		const u32 fs = sources[alias][0];
		const u32 ft = sources[alias][1];
		fpr[fs] = pair.lhs;
		if (fs != ft)
			fpr[ft] = pair.rhs;
		const u32 lhs = fpr[fs];
		const u32 rhs = fpr[ft];
		// Poison every FCR31 field and alternate C so this proves replacement of
		// just the condition bit, not a broad clear or accidental sticky update.
		fcr[31] = 0xa5c35a69u ^ (0x01010101u * (fpcr_case + 1u));
		const bool condition = EeCop1CompareExpected(funct, lhs, rhs);
		const u32 expected_fcr31 = (fcr[31] & ~0x00800000u) | (condition ? 0x00800000u : 0u);
		auto expected_fcr = fcr;
		expected_fcr[31] = expected_fcr31;
		const std::vector<u32> code = {MipsCop1(0x10, ft, fs, 0, funct)};

		const EESnapshot interp = RunEEProgram(false, code, false, nullptr, initial_cp0.data(),
			gpr.data(), fpr.data(), fcr.data());
		u64 before[4]{}, after_legacy[4]{}, after_shared[4]{};
		EeCop1CompareReadCounts(before);
		const EESnapshot legacy = RunEEProgramWithIrMode(0, code, gpr.data(), initial_cp0.data(),
			fpr.data(), fcr.data());
		EeCop1CompareReadCounts(after_legacy);
		const EESnapshot shared = RunEEProgramWithIrMode(1, code, gpr.data(), initial_cp0.data(),
			fpr.data(), fcr.data());
		EeCop1CompareReadCounts(after_shared);

		CompareEE(interp, legacy, "COP1 compare interpreter/legacy full state");
		CompareEE(interp, shared, "COP1 compare interpreter/shared full state");
		CompareEE(legacy, shared, "COP1 compare legacy/shared full state");
		Check(legacy.cycle == shared.cycle, "COP1 compare legacy/shared exact cycles");
		bool expected_state = std::memcmp(interp.fpr, fpr.data(), sizeof(interp.fpr)) == 0 &&
			std::memcmp(shared.fpr, fpr.data(), sizeof(shared.fpr)) == 0 &&
			std::memcmp(interp.fcr, expected_fcr.data(), sizeof(interp.fcr)) == 0 &&
			std::memcmp(shared.fcr, expected_fcr.data(), sizeof(shared.fcr)) == 0 &&
			interp.fcr31 == expected_fcr31 && legacy.fcr31 == expected_fcr31 && shared.fcr31 == expected_fcr31;
		for (u32 reg = 1; reg < 32; ++reg)
			expected_state &= interp.gpr[reg] == gpr[reg].UD[0] && legacy.gpr[reg] == gpr[reg].UD[0] &&
				shared.gpr[reg] == gpr[reg].UD[0] && interp.gpr_upper[reg] == gpr[reg].UD[1] &&
				legacy.gpr_upper[reg] == gpr[reg].UD[1] && shared.gpr_upper[reg] == gpr[reg].UD[1];
		Check(expected_state,
			"COP1 compare independent normalized-raw model changes only FCR31.C and preserves GPR/FPR/FCR state");

		const u32 index = counter_index[funct == 0x30 ? 0 : funct == 0x32 ? 1 : funct == 0x34 ? 2 : 3];
		bool counters = true;
		for (u32 i = 0; i < 4; ++i)
			counters &= after_legacy[i] == before[i] && after_shared[i] == after_legacy[i] + (i == index ? 1u : 0u);
		Check(counters, "COP1 compare native compile counter admits only the selected shared predicate");

		if (fpcr_case == 0 && alias == 0 && pair.lhs == pairs[4].lhs && pair.rhs == pairs[4].rhs)
		{
			EESnapshot direct{};
			const bool direct_ok = RunEEIrCop1Direct(code, gpr.data(), fpr.data(), fcr.data(), direct);
			direct_all &= direct_ok;
			if (direct_ok)
			{
				CompareEE(interp, direct, "COP1 compare interpreter/direct IR full state");
				Check(direct.fcr31 == expected_fcr31,
					"COP1 compare direct IR matches independent condition-bit result");
			}
		}
	};

	for (u32 fpcr_case = 0; fpcr_case < 16; ++fpcr_case)
	{
		for (u32 funct : functions)
		for (u32 pair_index = 0; pair_index < pairs.size(); ++pair_index)
		for (u32 alias = 0; alias < sources.size(); ++alias)
			check_case(funct, pairs[pair_index], alias, fpcr_case);
	}
	EmuConfig.Cpu.FPUFPCR = original_fpcr;
	Check(direct_all, "COP1 C.F/C.EQ.S/C.LT.S/C.LE.S direct IR admits the bounded compare family");

	// CTC1 seeds the opposite condition, the compare replaces it, CFC1 observes
	// that exact FCR31 word, then normal/likely BC1 consumes the new condition.
	const auto check_branch = [&](u32 funct, u32 rt, bool expected_condition)
	{
		auto gpr = initial_gpr;
		auto fpr = initial_fpr;
		auto fcr = initial_fcr;
		fpr[5] = 0x3f800000u;
		fpr[9] = expected_condition ? 0x3f800000u : 0xbf800000u;
		fcr[31] = expected_condition ? 0x0034c058u : 0x00b4c058u; // CTC1 starts opposite.
		gpr[8].UD[0] = fcr[31];
		const std::vector<u32> code = {
			MipsCop1(6, 8, 31, 0, 0), MipsCop1(0x10, 9, 5, 0, funct),
			MipsCop1(2, 11, 31, 0, 0), MipsI(0x11, 8, rt, 4),
			MipsI(9, 0, 10, 1), MipsI(9, 0, 9, 10),
			MipsJ(2, EE_TEST_PC + 40), 0, MipsI(9, 0, 9, 20),
			MipsJ(2, EE_TEST_PC + 40), MipsJ(2, EE_TEST_PC + 40), 0};
		const EESnapshot interp = RunEEProgram(false, code, true, nullptr, initial_cp0.data(),
			gpr.data(), fpr.data(), fcr.data());
		u64 before[4]{}, legacy_counts[4]{}, shared_counts[4]{};
		EeCop1CompareReadCounts(before);
		const EESnapshot legacy = RunEEProgramWithIrMode(0, code, gpr.data(), initial_cp0.data(),
			fpr.data(), fcr.data(), true);
		EeCop1CompareReadCounts(legacy_counts);
		const EESnapshot shared = RunEEProgramWithIrMode(1, code, gpr.data(), initial_cp0.data(),
			fpr.data(), fcr.data(), true);
		EeCop1CompareReadCounts(shared_counts);
		CompareEE(interp, legacy, "COP1 compare CTC1/CFC1/BC1 interpreter/legacy");
		CompareEE(interp, shared, "COP1 compare CTC1/CFC1/BC1 interpreter/shared");
		Check(legacy.cycle == shared.cycle, "COP1 compare plus BC1 legacy/shared exact cycles");
		const bool branch_on_true = (rt & 1u) != 0;
		const bool likely = (rt & 2u) != 0;
		const bool taken = branch_on_true ? expected_condition : !expected_condition;
		const bool slot_executes = !likely || taken;
		const u64 expected_r10 = slot_executes ? 1u : gpr[10].UD[0];
		Check(shared.gpr[11] == shared.fcr31 && shared.gpr[9] == (taken ? 20u : 10u) &&
			shared.gpr[10] == expected_r10,
			"COP1 compare condition is visible through CFC1 and controls BC1 delay/annul behavior");
		bool counters = true;
		const u32 index = funct == 0x30 ? 0 : funct == 0x32 ? 1 : funct == 0x34 ? 2 : 3;
		for (u32 i = 0; i < 4; ++i)
			counters &= legacy_counts[i] == before[i] && shared_counts[i] == legacy_counts[i] + (i == index ? 1u : 0u);
		Check(counters, "COP1 compare branch path counts only its selected shared compare opcode");
	};
	check_branch(0x32, 1, true);  // C.EQ.S true; BC1T taken.
	check_branch(0x32, 1, false); // C.EQ.S false; BC1T untaken normal slot.
	check_branch(0x34, 3, false); // C.LT.S false; BC1TL untaken and annulled.
	check_branch(0x36, 3, true);  // C.LE.S true; BC1TL taken with slot.
	check_branch(0x30, 1, false); // C.F clears C before BC1T.

	// Nonzero condition-code and reserved low bits remain outside this slice.
	bool rejected_reserved = true;
	for (u32 funct : {0x30u, 0x32u, 0x34u, 0x36u})
	for (const u32 word : {MipsCop1(0x10, 9, 5, 1, funct), MipsCop1(0x10, 9, 5, 8, funct)})
	{
		ir::Function fn;
		u32 end_pc = 0;
		std::string error;
		rejected_reserved &= !EeIr::LiftBlock(&word, {EE_TEST_PC, 1}, fn, &end_pc, &error);
	}
	Check(rejected_reserved, "COP1 compare rejects unsupported condition-code and reserved-bit encodings");
}
