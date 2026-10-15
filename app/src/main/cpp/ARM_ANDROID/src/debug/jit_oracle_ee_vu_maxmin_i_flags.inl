// SPDX-FileCopyrightText: 2026 EmuCoreX Team
// SPDX-License-Identifier: GPL-3.0+
// EE COP2 MAXi/MINIi live-flag preservation fixture.
// Include after jit_oracle_ee_vu_maxmin_i.inl and the EE run helpers.
// Entry: EEVuMaxMinIFlagTests().

extern "C" u64 EmuCoreXOracleVUIRMaxMinIInstructions(u32 vu);

static bool s_ee_vu_maxmin_i_flags_diagnostic_printed = false;

void EEVuMaxMinIFlagTests()
{
	const auto macro = [](bool maximum, u32 mask, u32 fs, u32 fd) {
		return (0x12u << 26) | ((0x10u | (mask & 15u)) << 21) |
			(fs << 11) | (fd << 6) | (maximum ? 0x1du : 0x1fu);
	};
	const auto add = [](u32 mask, u32 fs, u32 ft, u32 fd) {
		return (0x12u << 26) | ((0x10u | (mask & 15u)) << 21) |
			(ft << 16) | (fs << 11) | (fd << 6) | 0x28u;
	};
	const auto clip = [](u32 fs, u32 ft) {
		// COP2 SPECIAL2 table index 31: (code & 3) | ((code >> 4) & 0x7c).
		return (0x12u << 26) | (0x1fu << 21) | (ft << 16) |
			(fs << 11) | 0x1ffu;
	};
	const auto ctc_i = [](u32 rt) {
		return (0x12u << 26) | (6u << 21) | (rt << 16) | (REG_I << 11);
	};
	const auto cfc_i = [](u32 rt) {
		return (0x12u << 26) | (2u << 21) | (rt << 16) | (REG_I << 11);
	};
	const auto sign32 = [](u32 raw) -> u64 {
		return (raw & 0x80000000u) ? (0xffffffff00000000ull | raw) : raw;
	};
	using VfWords = std::array<u32, 128>;
	constexpr u32 raw_i = 0x3f800000u; // I contains the raw +1.0 bit pattern.
	constexpr std::array<u32, 3> masks = {1, 5, 15};
	constexpr std::array<std::array<u32, 2>, 4> aliases = {{{7, 9}, {7, 7}, {0, 9}, {7, 0}}};

	for (bool maximum : {false, true})
	for (u32 mask : masks)
	for (const auto& alias : aliases)
	{
		const u32 fs = alias[0];
		const u32 fd = alias[1];
		VfWords input{};
		input[3] = 0x3f800000u; // Immutable VF0.w.
		// CLIP seed operands: +x and -y exceed the positive/negative unit bound.
		input[1 * 4 + 0] = 0x40000000u;
		input[1 * 4 + 1] = 0xc0000000u;
		input[1 * 4 + 2] = 0x3f000000u;
		input[2 * 4 + 3] = 0x3f800000u;
		// VADD seed operands are exact zeros in every lane, producing live nonzero
		// MAC and STATUS flags without NaNs, denormals, or FPCR-sensitive results.
		for (u32 lane = 0; lane < 4; ++lane)
		{
			input[3 * 4 + lane] = 0;
			input[4 * 4 + lane] = 0;
		}
		input[7 * 4 + 0] = 0x3f000000u;
		input[7 * 4 + 1] = 0xbf000000u;
		input[7 * 4 + 2] = 0x7fc12345u;
		input[7 * 4 + 3] = 0x80000000u;
		for (u32 lane = 0; lane < 4; ++lane)
			input[9 * 4 + lane] = 0x5a13579bu ^ (lane * 0x137abdeu);

		std::array<GPR_reg, 32> initial_gpr{};
		for (u32 reg = 1; reg < 32; ++reg)
		{
			initial_gpr[reg].UD[0] = 0x1234567800000000ull | (0x76543210u + reg);
			initial_gpr[reg].UD[1] = 0xabcdef0100000000ull | (0x89abcdefu + reg);
		}
		initial_gpr[10].UL[0] = raw_i;
		std::array<u32, 32> initial_fpr{}, initial_fcr{}, initial_cp0{};
		initial_cp0[12] = 0x20000000u;
		initial_fcr[0] = 0x2e00u;
		for (u32 reg = 1; reg < 32; ++reg)
		{
			initial_fpr[reg] = 0x51000000u ^ (reg * 0x01020408u);
			initial_fcr[reg] = 0x87654321u ^ (reg * 0x01020304u);
		}

		std::vector<u32> code = {
			ctc_i(10),
			add(15, 3, 4, 5),
			clip(1, 2),
			macro(maximum, mask, fs, fd),
			cfc_i(24)};
		VfWords expected_vf = input;
		for (u32 lane = 0; lane < 4; ++lane)
			expected_vf[5 * 4 + lane] = 0;
		if (fd != 0)
			for (u32 lane = 0; lane < 4; ++lane)
				if (mask & (8u >> lane))
					expected_vf[fd * 4 + lane] = EEVuMaxMinExpectedBits(
					input[fs * 4 + lane], raw_i, maximum);
		auto expected_gpr = initial_gpr;
		expected_gpr[24].UD[0] = sign32(raw_i);

		const auto counters = [] {
			return std::array<u64, 2>{EmuCoreXOracleVUIRMaxMinIInstructions(0),
				EmuCoreXOracleVUIRMaxMinIInstructions(1)};
		};
		const auto flags = [] {
			return std::array<u32, 3>{VU0.macflag, VU0.statusflag, VU0.clipflag};
		};
		constexpr std::array<u32, 3> seeded_vi_flags = {0x0000000fu, 0x00000041u, 0x00000009u};
		const auto independent = [&](const EESnapshot& snap) {
			bool exact = std::memcmp(snap.vu0_vf, expected_vf.data(), sizeof(snap.vu0_vf)) == 0 &&
				std::memcmp(snap.fpr, initial_fpr.data(), sizeof(snap.fpr)) == 0 &&
				std::memcmp(snap.fcr, initial_fcr.data(), sizeof(snap.fcr)) == 0 &&
				snap.vu0_vi[REG_I] == raw_i && snap.vu0_vi[REG_MAC_FLAG] == 0x0fu &&
				snap.vu0_vi[REG_STATUS_FLAG] == 0x41u && snap.vu0_vi[REG_CLIP_FLAG] == 0x09u;
			for (u32 reg = 0; reg < 32; ++reg)
				exact &= snap.gpr[reg] == expected_gpr[reg].UD[0] &&
					snap.gpr_upper[reg] == expected_gpr[reg].UD[1] &&
					(reg == REG_I || reg == REG_MAC_FLAG || reg == REG_STATUS_FLAG ||
					 reg == REG_CLIP_FLAG || snap.vu0_vi[reg] == 0);
			for (u32 lane = 0; lane < 4; ++lane)
				exact &= snap.vu0_acc[lane] == 0;
			return exact;
		};
		char name[128];
		std::snprintf(name, sizeof(name), "EE MAXi/MINIi live flags max=%u mask=%u fs=%u fd=%u",
			maximum ? 1u : 0u, mask, fs, fd);

		EmuCoreXOracleSetEeIrMode(0);
		EmuCoreXOracleSetVUIR(0, 0);
		const auto before = counters();
		const EESnapshot legacy = RunEEProgramWithIrMode(0, code, initial_gpr.data(), initial_cp0.data(),
			initial_fpr.data(), initial_fcr.data(), false, input.data());
		const auto legacy_flags = flags();
		Check(counters() == before, "EE MAXi/MINIi live-flags forced legacy counter unchanged");
		const EESnapshot interp = RunEEProgram(false, code, false, nullptr, initial_cp0.data(),
			initial_gpr.data(), initial_fpr.data(), initial_fcr.data(), input.data());
		const auto interp_flags = flags();
		EmuCoreXOracleSetVUIR(0, 1);
		const EESnapshot shared = RunEEProgramWithIrMode(0, code, initial_gpr.data(), initial_cp0.data(),
			initial_fpr.data(), initial_fcr.data(), false, input.data());
		const auto shared_flags = flags();
		if (!s_ee_vu_maxmin_i_flags_diagnostic_printed)
		{
			s_ee_vu_maxmin_i_flags_diagnostic_printed = true;
			const EESnapshot add_only = RunEEProgram(false, {code[1]}, false, nullptr, initial_cp0.data(),
				initial_gpr.data(), initial_fpr.data(), initial_fcr.data(), input.data());
			const auto add_only_flags = flags();
			const EESnapshot clip_only = RunEEProgram(false, {code[2]}, false, nullptr, initial_cp0.data(),
				initial_gpr.data(), initial_fpr.data(), initial_fcr.data(), input.data());
			const auto clip_only_flags = flags();
			std::printf("EE MAXi live-flags diagnostic interp=%08x/%08x/%08x VI=%08x/%08x/%08x "
				"legacy=%08x/%08x/%08x VI=%08x/%08x/%08x shared=%08x/%08x/%08x VI=%08x/%08x/%08x\n",
				interp_flags[0], interp_flags[1], interp_flags[2], interp.vu0_vi[REG_MAC_FLAG],
				interp.vu0_vi[REG_STATUS_FLAG], interp.vu0_vi[REG_CLIP_FLAG], legacy_flags[0],
				legacy_flags[1], legacy_flags[2], legacy.vu0_vi[REG_MAC_FLAG], legacy.vu0_vi[REG_STATUS_FLAG],
				legacy.vu0_vi[REG_CLIP_FLAG], shared_flags[0], shared_flags[1], shared_flags[2],
				shared.vu0_vi[REG_MAC_FLAG], shared.vu0_vi[REG_STATUS_FLAG], shared.vu0_vi[REG_CLIP_FLAG]);
			std::printf("EE MAXi seed diagnostic add=%08x flags=%08x/%08x/%08x VI=%08x/%08x/%08x "
				"clip=%08x flags=%08x/%08x/%08x VI=%08x/%08x/%08x\n",
				code[1], add_only_flags[0], add_only_flags[1], add_only_flags[2],
				add_only.vu0_vi[REG_MAC_FLAG], add_only.vu0_vi[REG_STATUS_FLAG], add_only.vu0_vi[REG_CLIP_FLAG],
				code[2], clip_only_flags[0], clip_only_flags[1], clip_only_flags[2],
				clip_only.vu0_vi[REG_MAC_FLAG], clip_only.vu0_vi[REG_STATUS_FLAG],
				clip_only.vu0_vi[REG_CLIP_FLAG]);
		}
		const u64 expected_routes = (fd != 0 && mask != 0) ? 1u : 0u;
		Check(counters()[0] == before[0] + expected_routes && counters()[1] == before[1],
			"EE MAXi/MINIi live-flags admits only expected VU0 route");
		CompareEE(interp, legacy, name);
		CompareEE(legacy, shared, name);
		Check(legacy.cycle == shared.cycle, "EE MAXi/MINIi live-flags exact legacy/shared cycles");
		Check(independent(interp) && independent(legacy) && independent(shared), name);
		const auto vi_flags = [](const EESnapshot& snap) {
			return std::array<u32, 3>{snap.vu0_vi[REG_MAC_FLAG], snap.vu0_vi[REG_STATUS_FLAG],
				snap.vu0_vi[REG_CLIP_FLAG]};
		};
		Check(vi_flags(interp) == seeded_vi_flags && vi_flags(legacy) == seeded_vi_flags &&
			vi_flags(shared) == seeded_vi_flags,
			"EE MAXi/MINIi preserves explicitly seeded nonzero MAC/STATUS/CLIP flags");
	}
	EmuCoreXOracleSetVUIR(0, -1);
}
