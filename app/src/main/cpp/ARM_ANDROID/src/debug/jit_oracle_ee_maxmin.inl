// SPDX-License-Identifier: GPL-3.0+
// COP1 MAX.S/MIN.S raw-bit integration tests. This fragment relies on
// the EE oracle and direct COP1 lifter helpers declared by jit_oracle.cpp.
extern "C" void EmuCoreXOracleGetEeIrCop1MaxMinCounts(u64* max_s, u64* min_s);

static u32 EeMaxMinExpectedBits(u32 a, u32 b, bool is_max)
{
	const s32 signed_a = static_cast<s32>(a);
	const s32 signed_b = static_cast<s32>(b);
	const bool both_negative = signed_a < 0 && signed_b < 0;
	if (is_max)
		return static_cast<u32>(both_negative ? std::min(signed_a, signed_b) : std::max(signed_a, signed_b));
	return static_cast<u32>(both_negative ? std::max(signed_a, signed_b) : std::min(signed_a, signed_b));
}

void EEIrCop1MaxMinTests()
{
	struct Pair { u32 a; u32 b; };
	constexpr std::array<Pair, 8> pairs = {{
		{0x00000000u, 0x80000000u}, // signed zero
		{0x00000001u, 0x80000001u}, // signed subnormal
		{0x3f800000u, 0xbf800000u}, // opposite finite signs
		{0xc0400000u, 0xc0000000u}, // both negative, different magnitudes
		{0x7f800000u, 0xff800000u}, // signed infinity
		{0x7fc12345u, 0x3f800000u}, // positive qNaN payload
		{0xffc54321u, 0xbf800000u}, // negative qNaN payload
		{0x7f812345u, 0xff854321u}, // signed sNaN payloads
	}};
	constexpr std::array<u32, 2> functions = {0x28, 0x29}; // MAX.S, MIN.S
	constexpr std::array<u32, 2> fcr31_seeds = {0x0080c058u, 0x00004028u};
	std::array<GPR_reg, 32> initial_gpr{};
	std::array<u32, 32> initial_fpr{}, initial_fcr{};
	std::array<u32, 32> initial_cp0{};
	initial_cp0[12] = 0x20000000u; // Status.CU1
	for (u32 reg = 1; reg < 32; ++reg)
	{
		initial_gpr[reg].UD[0] = 0x1234567800000000ull | (0x76543210u + reg);
		initial_gpr[reg].UD[1] = 0xabcdef0100000000ull | (0x89abcdefu + reg);
		initial_fpr[reg] = 0x51000000u ^ (reg * 0x01020408u);
		initial_fcr[reg] = 0x87654321u ^ (reg * 0x01020304u);
	}
	initial_gpr[0] = {};
	initial_fcr[0] = 0x2e00u;
	bool direct_lifted_all = true;

	const auto check_case = [&](u32 funct, u32 raw_a, u32 raw_b, u32 fs, u32 ft, u32 fd,
		u32 fcr31_seed) {
		auto fpr = initial_fpr;
		auto fcr = initial_fcr;
		fpr[fs] = raw_a;
		fpr[ft] = raw_b;
		fcr[31] = fcr31_seed;
		const std::vector<u32> code = {MipsCop1(0x10, ft, fs, fd, funct)};
		const u32 result_bits = EeMaxMinExpectedBits(raw_a, raw_b, funct == 0x28);
		auto expected_fpr = fpr;
		auto expected_fcr = fcr;
		expected_fpr[fd] = result_bits;
		expected_fcr[31] &= ~0x0000c000u; // clear current O/U causes only

		const EESnapshot interp = RunEEProgram(false, code, false, nullptr, initial_cp0.data(),
			initial_gpr.data(), fpr.data(), fcr.data());
		u64 before_max = 0, before_min = 0, legacy_max = 0, legacy_min = 0;
		EmuCoreXOracleGetEeIrCop1MaxMinCounts(&before_max, &before_min);
		const EESnapshot legacy = RunEEProgramWithIrMode(0, code, initial_gpr.data(), initial_cp0.data(),
			fpr.data(), fcr.data());
		EmuCoreXOracleGetEeIrCop1MaxMinCounts(&legacy_max, &legacy_min);
		const EESnapshot shared = RunEEProgramWithIrMode(1, code, initial_gpr.data(), initial_cp0.data(),
			fpr.data(), fcr.data());
		u64 shared_max = 0, shared_min = 0;
		EmuCoreXOracleGetEeIrCop1MaxMinCounts(&shared_max, &shared_min);
		const void* native = nullptr;
		u32 bytes = 0, selects = 0;
		const bool captured = EmuCoreXOracleGetEeCompiledBlock(EE_TEST_PC, &native, &bytes);
		if (captured)
		{
			for (u32 offset = 0; offset + 4 <= bytes; offset += 4)
			{
				u32 instruction;
				std::memcpy(&instruction, static_cast<const u8*>(native) + offset, sizeof(instruction));
				// Integer CSEL (either width), excluding CSINC/CSET and FP selects.
				selects += (instruction & 0x7fe00c00u) == 0x1a800000u;
			}
		}
		Check(captured && bytes != 0 && selects <= 1,
			"COP1 MAX/MIN raw-order selection needs at most one native conditional select");
		if (fs == 5 && fd == 17 && raw_a == 0 && fcr31_seed == fcr31_seeds[0])
			std::printf("EEMAXMIN funct=%02x bytes=%u frame=%u conditional_selects=%u cycles_interpreter=%u legacy=%u shared=%u\n",
				funct, bytes, EmuCoreXOracleGetEeIrLastFrameSize(), selects, interp.cycle, legacy.cycle, shared.cycle);

		CompareEE(interp, legacy, "COP1 MAX/MIN interpreter/legacy full state");
		CompareEE(interp, shared, "COP1 MAX/MIN interpreter/shared full state");
		CompareEE(legacy, shared, "COP1 MAX/MIN legacy/shared full state");
		Check(legacy.cycle == shared.cycle, "COP1 MAX.S/MIN.S legacy/shared exact cycles");

		bool raw_expected = std::memcmp(interp.fpr, expected_fpr.data(), sizeof(interp.fpr)) == 0 &&
			std::memcmp(legacy.fpr, expected_fpr.data(), sizeof(legacy.fpr)) == 0 &&
			std::memcmp(shared.fpr, expected_fpr.data(), sizeof(shared.fpr)) == 0 &&
			std::memcmp(interp.fcr, expected_fcr.data(), sizeof(interp.fcr)) == 0 &&
			std::memcmp(legacy.fcr, expected_fcr.data(), sizeof(legacy.fcr)) == 0 &&
			std::memcmp(shared.fcr, expected_fcr.data(), sizeof(shared.fcr)) == 0;
		for (u32 reg = 0; reg < 32; ++reg)
		{
			const u64 expected_low = reg == 0 ? 0 : initial_gpr[reg].UD[0];
			const u64 expected_high = reg == 0 ? 0 : initial_gpr[reg].UD[1];
			raw_expected &= interp.gpr[reg] == expected_low && legacy.gpr[reg] == expected_low &&
				shared.gpr[reg] == expected_low;
			raw_expected &= interp.gpr_upper[reg] == expected_high && legacy.gpr_upper[reg] == expected_high &&
				shared.gpr_upper[reg] == expected_high;
		}
		Check(raw_expected, "COP1 MAX/MIN matches independent raw-order/FCR model and preserves full GPR/FPR/FCR state");
		Check(funct == 0x28 ? (legacy_max == before_max && legacy_min == before_min &&
			shared_max == legacy_max + 1 && shared_min == legacy_min) :
			(legacy_max == before_max && legacy_min == before_min &&
			shared_min == legacy_min + 1 && shared_max == legacy_max),
			"COP1 MAX.S/MIN.S native successful-compile counter admits only selected shared opcode");

		EESnapshot direct{};
		const bool direct_ok = RunEEIrCop1Direct(code, initial_gpr.data(), fpr.data(), fcr.data(), direct);
		direct_lifted_all &= direct_ok;
		if (direct_ok)
		{
			CompareEE(interp, direct, "COP1 MAX/MIN interpreter/direct IR full state");
			Check(std::memcmp(direct.fpr, expected_fpr.data(), sizeof(direct.fpr)) == 0 &&
				std::memcmp(direct.fcr, expected_fcr.data(), sizeof(direct.fcr)) == 0,
				"COP1 MAX/MIN direct IR matches independent raw-order/FCR model");
		}
	};

	// Distinct operands/destination, fd==fs, and fd==ft for every raw pair.
	// Two cause/sticky states make the FCR31 mask observable on each opcode.
	for (u32 funct : functions)
	for (u32 i = 0; i < pairs.size(); ++i)
	for (u32 alias = 0; alias < 3; ++alias)
	for (u32 fcr31_seed : fcr31_seeds)
	{
		const u32 fs = 5, ft = 9;
		const u32 fd = alias == 0 ? 17 : (alias == 1 ? fs : ft);
		check_case(funct, pairs[i].a, pairs[i].b, fs, ft, fd, fcr31_seed);
	}

	// Same source operands with a distinct destination, and full
	// source/destination alias, exercise the read-before-write requirement.
	for (u32 funct : functions)
	for (const Pair& pair : pairs)
	for (u32 fcr31_seed : fcr31_seeds)
	{
		check_case(funct, pair.a, pair.a, 7, 7, 11, fcr31_seed);
		check_case(funct, pair.a, pair.a, 7, 7, 7, fcr31_seed);
	}

	Check(direct_lifted_all, "COP1 MAX.S/MIN.S direct lifter admits supported raw-bit operations");
}
