// SPDX-License-Identifier: GPL-3.0+
// EE COP2 MAX/MINI macro integration tests. Included after the EE
// run helpers; this exercises the VU0 macro dispatcher, not microVU code.

extern "C" u64 EmuCoreXOracleVUIRMaxMinInstructions(u32 vu);

static u32 EEVuMaxMinExpectedBits(u32 a, u32 b, bool maximum)
{
	const bool negative_a = (a >> 31) != 0;
	const bool negative_b = (b >> 31) != 0;
	const bool a_greater = negative_a != negative_b ? !negative_a :
		(negative_a ? (a & 0x7fffffffu) < (b & 0x7fffffffu) : a > b);
	if (a == b)
		return b;
	return maximum == a_greater ? a : b;
}

void EEVuMaxMinMacroTests()
{
	const FPControlRegister original_ee_fpcr = EmuConfig.Cpu.FPUFPCR;
	const FPControlRegister original_vu0_fpcr = EmuConfig.Cpu.VU0FPCR;
	EmuConfig.Cpu.FPUFPCR.SetRoundMode(FPRoundMode::Nearest);
	EmuConfig.Cpu.VU0FPCR.SetRoundMode(FPRoundMode::Nearest);
	const FPControlRegisterBackup host_fpcr(EmuConfig.Cpu.FPUFPCR);
	const auto macro = [](bool maximum, u32 mask, u32 fs, u32 ft, u32 fd) {
		return (0x12u << 26) | ((0x10u | (mask & 15u)) << 21) | (ft << 16) |
			(fs << 11) | (fd << 6) | (maximum ? 0x2bu : 0x2fu);
	};
	using VfWords = std::array<u32, 128>;
	VfWords initial_vf{};
	initial_vf[3] = 0x3f800000u; // VF0 is immutable (+0,+0,+0,+1).
	for (u32 reg = 1; reg < 32; ++reg)
		for (u32 lane = 0; lane < 4; ++lane)
			initial_vf[reg * 4 + lane] = 0x5a13579bu ^ (reg * 0x01020408u) ^ (lane * 0x137abdeu);
	constexpr std::array<u32, 24> edges = {
		0x00000000u, 0x80000000u, 0x00000001u, 0x80000001u,
		0x007fffffu, 0x807fffffu, 0x00800000u, 0x80800000u,
		0x3f800000u, 0xbf800000u, 0x7f7fffffu, 0xff7fffffu,
		0x7f800000u, 0xff800000u, 0x7f800001u, 0xff800001u,
		0x7fa12345u, 0xffa54321u, 0x7fc12345u, 0xffc54321u,
		0x7fffffffu, 0xffffffffu, 0x40000000u, 0xc0000000u};
	constexpr std::array<std::array<u32, 3>, 9> registers = {{
		{7, 8, 9}, {7, 8, 7}, {7, 8, 8}, {7, 7, 9}, {7, 7, 7},
		{0, 8, 9}, {7, 0, 9}, {0, 0, 9}, {7, 8, 0}}};
	std::array<GPR_reg, 32> initial_gpr{};
	for (u32 reg = 1; reg < 32; ++reg)
	{
		initial_gpr[reg].UD[0] = 0x1234567800000000ull | (0x76543210u + reg);
		initial_gpr[reg].UD[1] = 0xabcdef0100000000ull | (0x89abcdefu + reg);
	}
	initial_gpr[0] = {};
	std::array<u32, 32> initial_fpr{}, initial_fcr{};
	initial_fcr[0] = 0x2e00u;
	for (u32 reg = 1; reg < 32; ++reg)
	{
		initial_fpr[reg] = 0x51000000u ^ (reg * 0x01020408u);
		initial_fcr[reg] = 0x87654321u ^ (reg * 0x01020304u);
	}

	const auto run_case = [&](bool maximum, u32 mask, const std::array<u32, 3>& regs, u32 case_id) {
		VfWords input = initial_vf;
		const u32 fs = regs[0], ft = regs[1], fd = regs[2];
		for (u32 lane = 0; lane < 4; ++lane)
		{
			if (fs != 0)
				input[fs * 4 + lane] = edges[(case_id * 3 + mask + lane * 5) % edges.size()];
			if (ft != 0 && ft != fs)
				input[ft * 4 + lane] = edges[(case_id * 7 + mask * 2 + lane * 11 + 9) % edges.size()];
		}
		VfWords expected_vf = input;
		if (fd != 0)
			for (u32 lane = 0; lane < 4; ++lane)
				if (mask & (8u >> lane))
					expected_vf[fd * 4 + lane] = EEVuMaxMinExpectedBits(
						input[fs * 4 + lane], input[ft * 4 + lane], maximum);
		const std::vector<u32> code = {macro(maximum, mask, fs, ft, fd)};
		const u64 vu1_before = EmuCoreXOracleVUIRMaxMinInstructions(1);
		EmuCoreXOracleSetVUIR(0, 0);
		const u64 legacy_before = EmuCoreXOracleVUIRMaxMinInstructions(0);
		const EESnapshot legacy = RunEEProgramWithIrMode(0, code, initial_gpr.data(), nullptr,
			initial_fpr.data(), initial_fcr.data(), false, input.data());
		Check(EmuCoreXOracleVUIRMaxMinInstructions(0) == legacy_before,
			"EE COP2 MAX/MINI forced legacy does not increment the shared route counter");
		const EESnapshot interp = RunEEProgram(false, code, false, nullptr, nullptr,
			initial_gpr.data(), initial_fpr.data(), initial_fcr.data(), input.data());
		EmuCoreXOracleSetVUIR(0, 1);
		const u64 shared_before = EmuCoreXOracleVUIRMaxMinInstructions(0);
		const EESnapshot shared = RunEEProgramWithIrMode(0, code, initial_gpr.data(), nullptr,
			initial_fpr.data(), initial_fcr.data(), false, input.data());
		const bool admitted = fd != 0 && mask != 0;
		Check(EmuCoreXOracleVUIRMaxMinInstructions(0) == shared_before + (admitted ? 1u : 0u) &&
			EmuCoreXOracleVUIRMaxMinInstructions(1) == vu1_before,
			"EE COP2 MAX/MINI admission is exact to VU0 and excludes VF0/empty masks");
		CompareEE(interp, legacy, "EE COP2 MAX/MINI interpreter/legacy complete state");
		CompareEE(legacy, shared, "EE COP2 MAX/MINI legacy/shared complete state");
		Check(legacy.cycle == shared.cycle,
			"EE COP2 MAX/MINI preserves exact legacy/shared cycle counts");

		bool exact = std::memcmp(interp.vu0_vf, expected_vf.data(), sizeof(expected_vf)) == 0 &&
			std::memcmp(legacy.vu0_vf, expected_vf.data(), sizeof(expected_vf)) == 0 &&
			std::memcmp(shared.vu0_vf, expected_vf.data(), sizeof(expected_vf)) == 0 &&
			std::memcmp(interp.fpr, initial_fpr.data(), sizeof(interp.fpr)) == 0 &&
			std::memcmp(legacy.fpr, initial_fpr.data(), sizeof(legacy.fpr)) == 0 &&
			std::memcmp(shared.fpr, initial_fpr.data(), sizeof(shared.fpr)) == 0 &&
			std::memcmp(interp.fcr, initial_fcr.data(), sizeof(interp.fcr)) == 0 &&
			std::memcmp(legacy.fcr, initial_fcr.data(), sizeof(legacy.fcr)) == 0 &&
			std::memcmp(shared.fcr, initial_fcr.data(), sizeof(shared.fcr)) == 0;
		for (u32 reg = 0; reg < 32; ++reg)
		{
			const u64 expected_low = initial_gpr[reg].UD[0];
			const u64 expected_high = initial_gpr[reg].UD[1];
			exact &= interp.gpr[reg] == expected_low && legacy.gpr[reg] == expected_low &&
				shared.gpr[reg] == expected_low && interp.gpr_upper[reg] == expected_high &&
				legacy.gpr_upper[reg] == expected_high && shared.gpr_upper[reg] == expected_high;
		}
		const std::array<u32, 4> zero_acc{};
		exact &= std::memcmp(interp.vu0_acc, zero_acc.data(), sizeof(zero_acc)) == 0 &&
			std::memcmp(legacy.vu0_acc, zero_acc.data(), sizeof(zero_acc)) == 0 &&
			std::memcmp(shared.vu0_acc, zero_acc.data(), sizeof(zero_acc)) == 0 &&
			std::memcmp(interp.vu0_vi, legacy.vu0_vi, sizeof(interp.vu0_vi)) == 0 &&
			std::memcmp(legacy.vu0_vi, shared.vu0_vi, sizeof(legacy.vu0_vi)) == 0;
		Check(exact,
			"EE COP2 MAX/MINI matches independent raw VF results and preserves full GPR/COP1/VU state");
	};

	for (bool maximum : {false, true})
	for (u32 mask = 1; mask < 16; ++mask)
	for (u32 case_id = 0; case_id < registers.size(); ++case_id)
		run_case(maximum, mask, registers[case_id], case_id);
	for (bool maximum : {false, true})
	{
		run_case(maximum, 0, registers[0], 31);
		run_case(maximum, 15, registers[8], 32); // VF0 destination never routes.
	}

	EmuCoreXOracleSetVUIR(0, -1);
	EmuConfig.Cpu.FPUFPCR = original_ee_fpcr;
	EmuConfig.Cpu.VU0FPCR = original_vu0_fpcr;
}
