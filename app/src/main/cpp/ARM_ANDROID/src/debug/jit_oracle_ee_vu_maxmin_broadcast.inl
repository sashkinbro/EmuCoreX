// SPDX-FileCopyrightText: 2026 EmuCoreX Team
// SPDX-License-Identifier: GPL-3.0+
// EE COP2 macro integration tests for shared VU0 MAXx/y/z/w and MINIx/y/z/w.
// Include after jit_oracle_ee_vu_maxmin.inl; call
// EEVuMaxMinBroadcastMacroTests().

extern "C" u64 EmuCoreXOracleVUIRMaxMinBroadcastInstructions(u32 vu);
extern "C" u64 EmuCoreXOracleVUIRMaxMinInstructions(u32 vu);

void EEVuMaxMinBroadcastMacroTests()
{
	const FPControlRegister original_ee_fpcr = EmuConfig.Cpu.FPUFPCR;
	const FPControlRegister original_vu0_fpcr = EmuConfig.Cpu.VU0FPCR;
	EmuConfig.Cpu.FPUFPCR.SetRoundMode(FPRoundMode::Nearest);
	EmuConfig.Cpu.VU0FPCR.SetRoundMode(FPRoundMode::Nearest);
	const FPControlRegisterBackup host_fpcr(EmuConfig.Cpu.FPUFPCR);
	const auto macro = [](bool maximum, u32 selector, u32 mask, u32 fs, u32 ft, u32 fd) {
		const u32 function = (maximum ? 0x10u : 0x14u) + selector;
		return (0x12u << 26) | ((0x10u | (mask & 15u)) << 21) | (ft << 16) |
			(fs << 11) | (fd << 6) | function;
	};
	using VfWords = std::array<u32, 128>;
	VfWords initial_vf{};
	initial_vf[3] = 0x3f800000u;
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
	const auto macro_flags = [] {
		return std::array<u32, 3>{VU0.macflag, VU0.statusflag, VU0.clipflag};
	};

	const auto run_case = [&](bool maximum, u32 selector, u32 mask,
		const std::array<u32, 3>& regs, u32 case_id) {
		VfWords input = initial_vf;
		const u32 fs = regs[0], ft = regs[1], fd = regs[2];
		for (u32 lane = 0; lane < 4; ++lane)
		{
			if (fs != 0)
				input[fs * 4 + lane] = edges[(case_id * 3 + mask + selector * 5 + lane * 7) % edges.size()];
			if (ft != 0 && ft != fs)
				input[ft * 4 + lane] = edges[(case_id * 11 + mask * 2 + selector * 3 + lane * 13 + 9) % edges.size()];
		}
		VfWords expected_vf = input;
		const u32 selected_rhs = input[ft * 4 + selector];
		if (fd != 0)
			for (u32 lane = 0; lane < 4; ++lane)
				if (mask & (8u >> lane))
					expected_vf[fd * 4 + lane] = EEVuMaxMinExpectedBits(
						input[fs * 4 + lane], selected_rhs, maximum);
		const std::vector<u32> code = {macro(maximum, selector, mask, fs, ft, fd)};
		const bool admitted = fd != 0 && mask != 0;
		EmuCoreXOracleSetEeIrMode(0);
		EmuCoreXOracleSetVUIR(0, 0);
		const u64 broadcast_before = EmuCoreXOracleVUIRMaxMinBroadcastInstructions(0);
		const u64 base_before = EmuCoreXOracleVUIRMaxMinInstructions(0);
		const u64 broadcast_other_before = EmuCoreXOracleVUIRMaxMinBroadcastInstructions(1);
		const u64 base_other_before = EmuCoreXOracleVUIRMaxMinInstructions(1);
		const EESnapshot legacy = RunEEProgramWithIrMode(0, code, initial_gpr.data(), nullptr,
			initial_fpr.data(), initial_fcr.data(), false, input.data());
		const auto legacy_flags = macro_flags();
		Check(EmuCoreXOracleVUIRMaxMinBroadcastInstructions(0) == broadcast_before &&
			EmuCoreXOracleVUIRMaxMinInstructions(0) == base_before,
			"EE COP2 broadcast MAX/MINI forced legacy leaves both route counters unchanged");
		const EESnapshot interp = RunEEProgram(false, code, false, nullptr, nullptr,
			initial_gpr.data(), initial_fpr.data(), initial_fcr.data(), input.data());
		const auto interp_flags = macro_flags();
		EmuCoreXOracleSetVUIR(0, 1);
		const EESnapshot shared = RunEEProgramWithIrMode(0, code, initial_gpr.data(), nullptr,
			initial_fpr.data(), initial_fcr.data(), false, input.data());
		const auto shared_flags = macro_flags();
		Check(EmuCoreXOracleVUIRMaxMinBroadcastInstructions(0) == broadcast_before +
			(admitted ? 1u : 0u) && EmuCoreXOracleVUIRMaxMinInstructions(0) == base_before &&
			EmuCoreXOracleVUIRMaxMinBroadcastInstructions(1) == broadcast_other_before &&
			EmuCoreXOracleVUIRMaxMinInstructions(1) == base_other_before,
			"EE COP2 broadcast admission increments only VU0 broadcast counter; base and VU1 stay unchanged");
		CompareEE(interp, legacy, "EE COP2 broadcast MAX/MINI interpreter/legacy full-state parity");
		CompareEE(legacy, shared, "EE COP2 broadcast MAX/MINI legacy/shared full-state parity");
		Check(legacy.cycle == shared.cycle,
			"EE COP2 broadcast MAX/MINI preserves exact legacy/shared cycle counts");
		bool exact = std::memcmp(interp.vu0_vf, expected_vf.data(), sizeof(expected_vf)) == 0 &&
			std::memcmp(legacy.vu0_vf, expected_vf.data(), sizeof(expected_vf)) == 0 &&
			std::memcmp(shared.vu0_vf, expected_vf.data(), sizeof(expected_vf)) == 0 &&
			std::memcmp(interp.fpr, initial_fpr.data(), sizeof(interp.fpr)) == 0 &&
			std::memcmp(legacy.fpr, initial_fpr.data(), sizeof(legacy.fpr)) == 0 &&
			std::memcmp(shared.fpr, initial_fpr.data(), sizeof(shared.fpr)) == 0 &&
			std::memcmp(interp.fcr, initial_fcr.data(), sizeof(interp.fcr)) == 0 &&
			std::memcmp(legacy.fcr, initial_fcr.data(), sizeof(legacy.fcr)) == 0 &&
			std::memcmp(shared.fcr, initial_fcr.data(), sizeof(shared.fcr)) == 0 &&
			std::memcmp(interp.vu0_vi, legacy.vu0_vi, sizeof(interp.vu0_vi)) == 0 &&
			std::memcmp(legacy.vu0_vi, shared.vu0_vi, sizeof(legacy.vu0_vi)) == 0 &&
			std::memcmp(interp.vu0_acc, legacy.vu0_acc, sizeof(interp.vu0_acc)) == 0 &&
			std::memcmp(legacy.vu0_acc, shared.vu0_acc, sizeof(legacy.vu0_acc)) == 0;
		for (u32 reg = 0; reg < 32; ++reg)
		{
			const u64 low = initial_gpr[reg].UD[0];
			const u64 high = initial_gpr[reg].UD[1];
			exact &= interp.gpr[reg] == low && legacy.gpr[reg] == low && shared.gpr[reg] == low &&
				interp.gpr_upper[reg] == high && legacy.gpr_upper[reg] == high && shared.gpr_upper[reg] == high;
		}
		Check(exact,
			"EE COP2 broadcast MAX/MINI matches independent raw VF result and preserves GPR/FPR/FCR/VI/ACC state");
		Check(interp_flags == legacy_flags && legacy_flags == shared_flags,
			"EE COP2 broadcast MAX/MINI preserves legacy MAC/STATUS/CLIP flag contract");
	};

	for (bool maximum : {false, true})
	for (u32 selector = 0; selector < 4; ++selector)
	for (u32 mask = 1; mask < 16; ++mask)
	for (u32 case_id = 0; case_id < registers.size(); ++case_id)
		run_case(maximum, selector, mask, registers[case_id],
			case_id + selector * 19 + mask * 37 + (maximum ? 101u : 0u));
	for (bool maximum : {false, true})
	for (u32 selector = 0; selector < 4; ++selector)
	{
		run_case(maximum, selector, 0, registers[0], 701u + selector);
		run_case(maximum, selector, 15, registers[8], 711u + selector); // VF0 destination never routes.
	}

	EmuCoreXOracleSetVUIR(0, -1);
	EmuCoreXOracleSetEeIrMode(-1);
	EmuConfig.Cpu.FPUFPCR = original_ee_fpcr;
	EmuConfig.Cpu.VU0FPCR = original_vu0_fpcr;
}
