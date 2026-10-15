// SPDX-FileCopyrightText: 2026 EmuCoreX Team
// SPDX-License-Identifier: GPL-3.0+
// Isolated production-route fixtures for VU MAXi/MINIi. Include after
// jit_oracle_vu_maxmin_integration.inl and call the integration and benchmark entry points.

extern "C" u64 EmuCoreXOracleVUIRMaxMinIInstructions(u32 core);
extern "C" u64 EmuCoreXOracleVUIRMaxMinInstructions(u32 core);
extern "C" u64 EmuCoreXOracleVUIRMaxMinBroadcastInstructions(u32 core);

static u32 VuMaxMinIIntegrationExpected(u32 a, u32 b, bool maximum)
{
	const bool negative_a = (a >> 31) != 0;
	const bool negative_b = (b >> 31) != 0;
	const bool a_greater = negative_a != negative_b ? !negative_a :
		(negative_a ? (a & 0x7fffffffu) < (b & 0x7fffffffu) : a > b);
	if (a == b)
		return b;
	return maximum == a_greater ? a : b;
}

static u32 VuMaxMinIClamp(u32 value, bool overflow_enabled)
{
	if (overflow_enabled && (value & 0x7fffffffu) >= 0x7f800000u)
		return (value & 0x80000000u) | 0x7f7fffffu;
	return value;
}

void VUSharedMaxMinIIntegrationTests()
{
	const auto saved_fixes = EmuConfig.Gamefixes;
	const auto saved_recompiler = EmuConfig.Cpu.Recompiler;
	const auto upper = [](bool maximum, u32 fs, u32 fd, u32 mask) {
		return (maximum ? 0x1du : 0x1fu) | (fd << 6) | (fs << 11) | ((mask & 15u) << 21);
	};
	const auto move = [](u32 fs, u32 ft, u32 mask) {
		return 0x8000033cu | (fs << 11) | (ft << 16) | ((mask & 15u) << 21);
	};
	using VfWords = std::array<u32, 128>;
	VfWords initial{};
	initial[3] = 0x3f800000u;
	for (u32 reg = 1; reg < 32; ++reg)
		for (u32 lane = 0; lane < 4; ++lane)
			initial[reg * 4 + lane] = 0x5a13579bu ^ (reg * 0x01020408u) ^ (lane * 0x137abdeu);
	std::array<u32, 16> initial_vi{};
	for (u32 reg = 1; reg < initial_vi.size(); ++reg)
		initial_vi[reg] = 0x2100u + reg * 3;
	constexpr std::array<u32, 4> initial_acc = {0x3fa00000u, 0x40200000u, 0xc0700000u, 0x40900000u};
	constexpr std::array<u32, 12> immediates = {
		0x00000000u, 0x80000000u, 0x3f800000u, 0xbf800000u,
		0x7f7fffffu, 0xff7fffffu, 0x7f800000u, 0xff800000u,
		0x7fc12345u, 0xffc54321u, 0x40000000u, 0xc0000000u};
	constexpr std::array<std::array<u32, 2>, 5> aliases = {{{7, 9}, {7, 7}, {0, 9}, {7, 0}, {0, 0}}};

	for (u32 core = 0; core < 2; ++core)
	{
		FPControlRegister& vu_fpcr = core ? EmuConfig.Cpu.VU1FPCR : EmuConfig.Cpu.VU0FPCR;
		FPControlRegister& ee_fpcr = EmuConfig.Cpu.FPUFPCR;
		const FPControlRegister saved_vu_fpcr = vu_fpcr;
		const FPControlRegister saved_ee_fpcr = ee_fpcr;
		vu_fpcr.SetRoundMode(FPRoundMode::Nearest);
		ee_fpcr = vu_fpcr;
		for (bool ibit_hack : {false, true})
		for (bool overflow_enabled : {false, true})
		{
			EmuConfig.Gamefixes.IbitHack = ibit_hack;
			if (core) EmuConfig.Cpu.Recompiler.vu1Overflow = overflow_enabled;
			else EmuConfig.Cpu.Recompiler.vu0Overflow = overflow_enabled;
			for (bool maximum : {false, true})
			for (u32 mask = 0; mask < 16; ++mask)
			for (u32 alias = 0; alias < aliases.size(); ++alias)
			{
				const u32 fs = aliases[alias][0], fd = aliases[alias][1];
				const u32 raw_i = immediates[(mask + alias * 3 + (maximum ? 5u : 0u)) % immediates.size()];
				VfWords input = initial;
				for (u32 lane = 0; lane < 4; ++lane)
					if (fs != 0)
						input[fs * 4 + lane] = immediates[(mask * 2 + alias + lane * 5) % immediates.size()];
				VfWords expected_interp = input;
				VfWords expected_legacy = input;
				const u32 effective_i = ibit_hack ? raw_i : VuMaxMinIClamp(raw_i, overflow_enabled);
				if (fd != 0)
					for (u32 lane = 0; lane < 4; ++lane)
						if (mask & (8u >> lane))
						{
							expected_interp[fd * 4 + lane] = VuMaxMinIIntegrationExpected(input[fs * 4 + lane], raw_i, maximum);
							expected_legacy[fd * 4 + lane] = VuMaxMinIIntegrationExpected(input[fs * 4 + lane], effective_i, maximum);
						}
				const std::vector<u32> program = {raw_i, NOP_U | 0x80000000u, NOP_L, upper(maximum, fs, fd, mask),
					NOP_L, NOP_U | 0x40000000u, NOP_L, NOP_U};
				EmuCoreXOracleSetVUIR(core, 0);
				const u64 before = EmuCoreXOracleVUIRMaxMinIInstructions(core);
				const u64 base_before = EmuCoreXOracleVUIRMaxMinInstructions(core);
				const u64 broadcast_before = EmuCoreXOracleVUIRMaxMinBroadcastInstructions(core);
				const u64 other_i_before = EmuCoreXOracleVUIRMaxMinIInstructions(core ^ 1u);
				const u64 other_base_before = EmuCoreXOracleVUIRMaxMinInstructions(core ^ 1u);
				const u64 other_broadcast_before = EmuCoreXOracleVUIRMaxMinBroadcastInstructions(core ^ 1u);
				const Snapshot interp = RunVU(core, false, program, 0x80, 0, 128, false,
					nullptr, input.data(), initial_acc.data(), initial_vi.data());
				const Snapshot legacy = RunVU(core, true, program, 0x80, 0, 128, false,
					nullptr, input.data(), initial_acc.data(), initial_vi.data());
				Check(EmuCoreXOracleVUIRMaxMinIInstructions(core) == before,
					"forced interpreter and legacy leave the VU MAXi/MINIi shared counter unchanged");
				Check(EmuCoreXOracleVUIRMaxMinInstructions(core) == base_before &&
					EmuCoreXOracleVUIRMaxMinBroadcastInstructions(core) == broadcast_before &&
					EmuCoreXOracleVUIRMaxMinIInstructions(core ^ 1u) == other_i_before &&
					EmuCoreXOracleVUIRMaxMinInstructions(core ^ 1u) == other_base_before &&
					EmuCoreXOracleVUIRMaxMinBroadcastInstructions(core ^ 1u) == other_broadcast_before,
					"forced MAXi/MINIi routes leave base, broadcast, and other-core counters unchanged");
				EmuCoreXOracleSetVUIR(core, 1);
				const Snapshot shared = RunVU(core, true, program, 0x80, 0, 128, false,
					nullptr, input.data(), initial_acc.data(), initial_vi.data());
				const u64 expected_delta = (mask != 0 && fd != 0) ? 1u : 0u;
				Check(EmuCoreXOracleVUIRMaxMinIInstructions(core) == before + expected_delta,
					"MAXi/MINIi admission follows nonzero destination and lane mask with either Ibit mode");
				Check(EmuCoreXOracleVUIRMaxMinInstructions(core) == base_before &&
					EmuCoreXOracleVUIRMaxMinBroadcastInstructions(core) == broadcast_before &&
					EmuCoreXOracleVUIRMaxMinIInstructions(core ^ 1u) == other_i_before &&
					EmuCoreXOracleVUIRMaxMinInstructions(core ^ 1u) == other_base_before &&
					EmuCoreXOracleVUIRMaxMinBroadcastInstructions(core ^ 1u) == other_broadcast_before,
					"MAXi/MINIi increments only its selected per-core operation counter");
				if (raw_i == effective_i)
					Compare(interp, legacy, "MAXi/MINIi interpreter/legacy full-state parity when I semantics coincide");
				Compare(legacy, shared, "MAXi/MINIi shared/legacy full-state parity");
				Check(legacy.regs.cycle == shared.regs.cycle,
					"MAXi/MINIi shared route preserves exact legacy cycles on both VU cores");
				Check(std::memcmp(interp.regs.VF, expected_interp.data(), sizeof(expected_interp)) == 0 &&
					std::memcmp(legacy.regs.VF, expected_legacy.data(), sizeof(expected_legacy)) == 0 &&
					std::memcmp(shared.regs.VF, expected_legacy.data(), sizeof(expected_legacy)) == 0 &&
					interp.regs.VI[REG_I].UL == raw_i && legacy.regs.VI[REG_I].UL == effective_i &&
					shared.regs.VI[REG_I].UL == effective_i &&
					std::memcmp(&shared.regs.ACC, initial_acc.data(), sizeof(initial_acc)) == 0,
					"MAXi/MINIi matches raw interpreter and configured legacy I models and preserves VF0/ACC");
			}
		}

		// A lower MOVE dirties Fs in the preceding pair; a following pair
		// produces I and MAXi/MINIi then consumes both updated inputs.
		EmuConfig.Gamefixes.IbitHack = false;
		if (core) EmuConfig.Cpu.Recompiler.vu1Overflow = false;
		else EmuConfig.Cpu.Recompiler.vu0Overflow = false;
		for (bool maximum : {false, true})
		{
			VfWords input = initial, expected = initial;
			for (u32 lane = 0; lane < 4; ++lane)
				input[7 * 4 + lane] = initial[6 * 4 + lane];
			expected = input;
			const u32 raw_i = 0xc0000000u;
			for (u32 lane = 0; lane < 4; ++lane)
				expected[9 * 4 + lane] = VuMaxMinIIntegrationExpected(input[7 * 4 + lane], raw_i, maximum);
			const std::vector<u32> program = {move(6, 7, 15), NOP_U, raw_i, NOP_U | 0x80000000u, NOP_L,
				upper(maximum, 7, 9, 15), NOP_L, NOP_U | 0x40000000u, NOP_L, NOP_U};
			EmuCoreXOracleSetVUIR(core, 0);
			const Snapshot legacy = RunVU(core, true, program, 0x80, 0, 128, false,
				nullptr, initial.data(), initial_acc.data(), initial_vi.data());
			EmuCoreXOracleSetVUIR(core, 1);
			const u64 before = EmuCoreXOracleVUIRMaxMinIInstructions(core);
			const Snapshot shared = RunVU(core, true, program, 0x80, 0, 128, false,
				nullptr, initial.data(), initial_acc.data(), initial_vi.data());
			Check(EmuCoreXOracleVUIRMaxMinIInstructions(core) == before + 1,
				"dirty-Fs MAXi/MINIi compiles through the shared route");
			Compare(legacy, shared, "dirty-Fs MAXi/MINIi legacy/shared state parity");
			Check(std::memcmp(shared.regs.VF, expected.data(), sizeof(expected)) == 0 &&
				legacy.regs.cycle == shared.regs.cycle,
				"dirty-Fs MAXi/MINIi observes the prior MOVE and current I with exact cycles");
		}

		// A same-pair lower MOVE writes Fs while the upper MAXi/MINIi reads it.
		// The upper result must use the old Fs value; the lower write still lands.
		for (bool maximum : {false, true})
		{
			constexpr u32 raw_i = 0x40400000u;
			VfWords expected = initial;
			for (u32 lane = 0; lane < 4; ++lane)
			{
				expected[7 * 4 + lane] = initial[6 * 4 + lane];
				expected[9 * 4 + lane] = VuMaxMinIIntegrationExpected(initial[7 * 4 + lane], raw_i, maximum);
			}
			const std::vector<u32> program = {raw_i, NOP_U | 0x80000000u,
				move(6, 7, 15), upper(maximum, 7, 9, 15),
				NOP_L, NOP_U | 0x40000000u, NOP_L, NOP_U};
			EmuCoreXOracleSetVUIR(core, 0);
			const Snapshot legacy = RunVU(core, true, program, 0x80, 0, 128, false,
				nullptr, initial.data(), initial_acc.data(), initial_vi.data());
			EmuCoreXOracleSetVUIR(core, 1);
			const u64 before = EmuCoreXOracleVUIRMaxMinIInstructions(core);
			const Snapshot shared = RunVU(core, true, program, 0x80, 0, 128, false,
				nullptr, initial.data(), initial_acc.data(), initial_vi.data());
			Check(EmuCoreXOracleVUIRMaxMinIInstructions(core) == before + 1,
				"same-pair lower-MOVE WAW MAXi/MINIi compiles through shared route");
			Compare(legacy, shared, "same-pair lower-MOVE WAW MAXi/MINIi legacy/shared state parity");
			Check(std::memcmp(shared.regs.VF, expected.data(), sizeof(expected)) == 0 &&
				legacy.regs.cycle == shared.regs.cycle,
				"same-pair WAW MAXi/MINIi reads old Fs and preserves exact cycles");
		}

		// Exercise a taken branch with MAXi/MINIi in its delay pair and I
		// already produced. Equal seeded VI operands force the taken path.
		for (bool maximum : {false, true})
		{
			constexpr u32 raw_i = 0x40400000u;
			VfWords expected = initial;
			for (u32 lane = 0; lane < 4; ++lane)
				expected[9 * 4 + lane] = VuMaxMinIIntegrationExpected(initial[7 * 4 + lane], raw_i, maximum);
			std::array<u32, 16> branch_vi = initial_vi;
			branch_vi[1] = branch_vi[2];
			const u32 branch = (0x28u << 25) | (1u << 11) | (2u << 16) | 2u;
			const std::vector<u32> program = {raw_i, NOP_U | 0x80000000u,
				branch, NOP_U, NOP_L, upper(maximum, 7, 9, 15),
				NOP_L, NOP_U, NOP_L, NOP_U | 0x40000000u, NOP_L, NOP_U};
			EmuCoreXOracleSetVUIR(core, 0);
			const Snapshot interp = RunVU(core, false, program, 0x80, 0, 128, false,
				nullptr, initial.data(), initial_acc.data(), branch_vi.data());
			const Snapshot legacy = RunVU(core, true, program, 0x80, 0, 128, false,
				nullptr, initial.data(), initial_acc.data(), branch_vi.data());
			EmuCoreXOracleSetVUIR(core, 1);
			const u64 before = EmuCoreXOracleVUIRMaxMinIInstructions(core);
			const Snapshot shared = RunVU(core, true, program, 0x80, 0, 128, false,
				nullptr, initial.data(), initial_acc.data(), branch_vi.data());
			Check(EmuCoreXOracleVUIRMaxMinIInstructions(core) == before + 1,
				"taken-delay MAXi/MINIi compiles through shared route");
			Compare(interp, legacy, "taken-delay MAXi/MINIi interpreter/legacy parity");
			Compare(legacy, shared, "taken-delay MAXi/MINIi legacy/shared parity");
			Check(std::memcmp(shared.regs.VF, expected.data(), sizeof(expected)) == 0 &&
				legacy.regs.cycle == shared.regs.cycle,
				"taken-delay MAXi/MINIi uses produced I and preserves exact cycles");
		}

		// If the same upper word performs MAXi and carries Ibit, MAXi consumes
		// old I first and the current pair's lower word becomes new I for MINIi.
		for (bool ibit_hack : {false, true})
		for (bool overflow_enabled : {false, true})
		{
			EmuConfig.Gamefixes.IbitHack = ibit_hack;
			if (core) EmuConfig.Cpu.Recompiler.vu1Overflow = overflow_enabled;
			else EmuConfig.Cpu.Recompiler.vu0Overflow = overflow_enabled;
			constexpr u32 old_raw_i = 0x7f800000u;
			constexpr u32 new_raw_i = 0xff800000u;
			const u32 old_i = ibit_hack ? old_raw_i : VuMaxMinIClamp(old_raw_i, overflow_enabled);
			const u32 new_i = ibit_hack ? new_raw_i : VuMaxMinIClamp(new_raw_i, overflow_enabled);
			VfWords expected_interp = initial, expected_legacy = initial;
			for (u32 lane = 0; lane < 4; ++lane)
			{
				expected_interp[9 * 4 + lane] = VuMaxMinIIntegrationExpected(initial[7 * 4 + lane], old_raw_i, true);
				expected_interp[10 * 4 + lane] = VuMaxMinIIntegrationExpected(initial[8 * 4 + lane], new_raw_i, false);
				expected_legacy[9 * 4 + lane] = VuMaxMinIIntegrationExpected(initial[7 * 4 + lane], old_i, true);
				expected_legacy[10 * 4 + lane] = VuMaxMinIIntegrationExpected(initial[8 * 4 + lane], new_i, false);
			}
			const std::vector<u32> program = {old_raw_i, NOP_U | 0x80000000u,
				new_raw_i, upper(true, 7, 9, 15) | 0x80000000u,
				NOP_L, upper(false, 8, 10, 15), NOP_L, NOP_U | 0x40000000u, NOP_L, NOP_U};
			std::array<Snapshot, 3> results{};
			for (u32 route = 0; route < results.size(); ++route)
			{
				EmuCoreXOracleSetVUIR(core, route == 2 ? 1 : 0);
				const u64 before = EmuCoreXOracleVUIRMaxMinIInstructions(core);
				results[route] = RunVU(core, route != 0, program, 0x80, 0, 128, false,
					nullptr, initial.data(), initial_acc.data(), initial_vi.data());
				Check(EmuCoreXOracleVUIRMaxMinIInstructions(core) == before +
					(route == 2 ? 2u : 0u),
					"same-pair Ibit producer admits both MAXi/MINIi operations in either Ibit mode");
				const VfWords& expected = route == 0 ? expected_interp : expected_legacy;
				Check(std::memcmp(results[route].regs.VF, expected.data(), sizeof(expected)) == 0 &&
					results[route].regs.VI[REG_I].UL == (route == 0 ? new_raw_i : new_i),
					"MAXi consumes old I before same-pair update and MINIi uses route-specific new I semantics");
			}
			if (old_raw_i == old_i && new_raw_i == new_i)
				Compare(results[0], results[1], "same-pair Ibit interpreter/legacy full-state parity when I semantics coincide");
			Compare(results[1], results[2], "same-pair Ibit shared/legacy full-state parity");
			Check(results[1].regs.cycle == results[2].regs.cycle,
				"same-pair Ibit producer preserves exact legacy cycles");
		}

		// Distinct old/new producer pairs additionally catch stale reads across
		// successive upper operations rather than only within one pair.
		for (bool ibit_hack : {false, true})
		{
			EmuConfig.Gamefixes.IbitHack = ibit_hack;
			VfWords expected = initial;
			constexpr u32 old_i = 0x3f800000u;
			constexpr u32 new_i = 0xc0000000u;
			for (u32 lane = 0; lane < 4; ++lane)
			{
				expected[9 * 4 + lane] = VuMaxMinIIntegrationExpected(initial[7 * 4 + lane], old_i, true);
				expected[10 * 4 + lane] = VuMaxMinIIntegrationExpected(initial[8 * 4 + lane], new_i, false);
			}
			const std::vector<u32> program = {old_i, NOP_U | 0x80000000u, NOP_L, upper(true, 7, 9, 15),
				new_i, NOP_U | 0x80000000u, NOP_L, upper(false, 8, 10, 15),
				NOP_L, NOP_U | 0x40000000u, NOP_L, NOP_U};
			std::array<Snapshot, 3> results{};
			for (u32 route = 0; route < results.size(); ++route)
			{
				EmuCoreXOracleSetVUIR(core, route == 2 ? 1 : 0);
				const u64 before = EmuCoreXOracleVUIRMaxMinIInstructions(core);
				results[route] = RunVU(core, route != 0, program, 0x80, 0, 128, false,
					nullptr, initial.data(), initial_acc.data(), initial_vi.data());
				Check(EmuCoreXOracleVUIRMaxMinIInstructions(core) == before +
					(route == 2 ? 2u : 0u),
					"bracketed I producers admit both operations in either Ibit mode");
				Check(std::memcmp(results[route].regs.VF, expected.data(), sizeof(expected)) == 0,
					"bracketed old/current/new I ordering matches independent model");
			}
			Compare(results[0], results[1], "I producer ordering interpreter/legacy parity");
			Compare(results[1], results[2], "I producer ordering shared/legacy parity");
			Check(results[1].regs.cycle == results[2].regs.cycle,
				"I producer ordering retains exact legacy cycles");
		}

		// Flag preservation starts from nonzero architectural MAC/STATUS/CLIP
		// values generated by real arithmetic/clip instructions, never by
		// indexing those special registers through the 16-entry initial-VI array.
		for (u32 route = 0; route < 3; ++route)
		{
			EmuConfig.Gamefixes.IbitHack = false;
			const u32 add = 0x28u | (12u << 6) | (7u << 11) | (8u << 16) | (15u << 21);
			const u32 clip = 0x3fu | (7u << 6) | (7u << 11) | (8u << 16);
			const std::vector<u32> seed_program = {NOP_L, add, NOP_L, clip,
				NOP_L, NOP_U, NOP_L, NOP_U, NOP_L, NOP_U, NOP_L, NOP_U,
				NOP_L, NOP_U | 0x40000000u, NOP_L, NOP_U};
			VfWords input = initial;
			constexpr std::array<u32, 4> a = {0x40000000u, 0xc0000000u, 0x40000000u, 0xc0000000u};
			constexpr std::array<u32, 4> b = {0xbf800000u, 0xbf800000u, 0xbf800000u, 0x3f800000u};
			constexpr std::array<u32, 4> sum = {0x3f800000u, 0xc0400000u, 0x3f800000u, 0xbf800000u};
			for (u32 lane = 0; lane < 4; ++lane) { input[28 + lane] = a[lane]; input[32 + lane] = b[lane]; }
			VfWords expected = input;
			for (u32 lane = 0; lane < 4; ++lane) expected[48 + lane] = sum[lane];
			for (u32 lane = 0; lane < 4; ++lane)
				expected[9 * 4 + lane] = VuMaxMinIIntegrationExpected(sum[lane], 0x3f800000u, true);
			EmuCoreXOracleSetVUIR(core, route == 2 ? 1 : 0);
			const u64 before = EmuCoreXOracleVUIRMaxMinIInstructions(core);
			const Snapshot seeded = RunVU(core, route != 0, seed_program, 0x80, 0, 128, false,
				nullptr, input.data(), initial_acc.data(), initial_vi.data());
			Check(seeded.regs.VI[REG_MAC_FLAG].UL != 0 && seeded.regs.VI[REG_STATUS_FLAG].UL != 0 &&
				seeded.regs.VI[REG_CLIP_FLAG].UL != 0 &&
				std::memcmp(&seeded.regs.ACC, initial_acc.data(), sizeof(initial_acc)) == 0,
				"MAXi/MINIi flag fixture seeds nonzero MAC/STATUS/CLIP and ACC");
			const std::vector<u32> operation = {0x3f800000u, NOP_U | 0x80000000u, NOP_L, upper(true, 12, 9, 15),
				NOP_L, NOP_U | 0x40000000u, NOP_L, NOP_U};
			const Snapshot result = RunVU(core, route != 0, operation, 0x100, 0, 128, true);
			Check(std::memcmp(result.regs.VF, expected.data(), sizeof(expected)) == 0 &&
				std::memcmp(&result.regs.ACC, initial_acc.data(), sizeof(initial_acc)) == 0 &&
				result.regs.VI[REG_MAC_FLAG].UL == seeded.regs.VI[REG_MAC_FLAG].UL &&
				result.regs.VI[REG_STATUS_FLAG].UL == seeded.regs.VI[REG_STATUS_FLAG].UL &&
				result.regs.VI[REG_CLIP_FLAG].UL == seeded.regs.VI[REG_CLIP_FLAG].UL,
				"MAXi/MINIi preserves seeded architectural flags/ACC and matches raw result");
			Check(EmuCoreXOracleVUIRMaxMinIInstructions(core) == before +
				(route == 2 ? 1u : 0u), "seeded-flag MAXi/MINIi route counter is exact");
		}
		vu_fpcr = saved_vu_fpcr;
		ee_fpcr = saved_ee_fpcr;
	}
	EmuConfig.Gamefixes = saved_fixes;
	EmuConfig.Cpu.Recompiler = saved_recompiler;
	EmuCoreXOracleSetVUIR(0, -1);
	EmuCoreXOracleSetVUIR(1, -1);
}

void VUSharedMaxMinIBenchmarks()
{
	using VfWords = std::array<u32, 128>;
	const FPControlRegisterBackup caller_fpcr(EmuConfig.Cpu.FPUFPCR);
	const bool saved_ibit_hack = EmuConfig.Gamefixes.IbitHack;
	const auto iadd = [](u32 is, u32 it, u32 id) {
		return 0x80000000u | 0x30u | (is << 11) | (it << 16) | (id << 6);
	};
	const auto upper = [](bool maximum, u32 fs, u32 fd) {
		return (maximum ? 0x1du : 0x1fu) | (fd << 6) | (fs << 11) | (15u << 21);
	};
	VfWords initial_vf{};
	initial_vf[3] = 0x3f800000u;
	for (u32 reg = 1; reg < 32; ++reg)
		for (u32 lane = 0; lane < 4; ++lane)
			initial_vf[reg * 4 + lane] = 0x5a13579bu ^ (reg * 0x01020408u) ^ (lane * 0x137abdeu);
	std::array<u32, 16> initial_vi{};
	for (u32 reg = 1; reg < initial_vi.size(); ++reg) initial_vi[reg] = reg * 11u;
	constexpr std::array<u32, 4> initial_acc = {0x3fa00000u, 0x40200000u, 0xc0700000u, 0x40900000u};
	for (u32 core = 0; core < 2; ++core)
	{
		FPControlRegister& vu_fpcr = core ? EmuConfig.Cpu.VU1FPCR : EmuConfig.Cpu.VU0FPCR;
		FPControlRegister& ee_fpcr = EmuConfig.Cpu.FPUFPCR;
		const FPControlRegister saved_vu_fpcr = vu_fpcr, saved_ee_fpcr = ee_fpcr;
		vu_fpcr.SetRoundMode(FPRoundMode::Nearest);
		ee_fpcr = vu_fpcr;
		EmuConfig.Gamefixes.IbitHack = false;
		std::vector<u32> program;
		VfWords expected_vf = initial_vf;
		std::array<u32, 16> expected_vi = initial_vi;
		for (u32 step = 0; step < 24; ++step)
		{
			const u32 is = 1 + step % 7, it = 8 + step % 5, id = 13 + step % 3;
			const u32 fs = 1 + ((step * 5 + 3) % 31), fd = 1 + ((step * 13 + 9) % 31);
			const u32 immediate = (step & 1) ? 0xbf800000u : 0x3f800000u;
			const bool maximum = (step & 1) == 0;
			program.push_back(immediate);
			program.push_back(NOP_U | 0x80000000u);
			program.push_back(iadd(is, it, id));
			program.push_back(upper(maximum, fs, fd));
			for (u32 lane = 0; lane < 4; ++lane)
				expected_vf[fd * 4 + lane] = VuMaxMinIIntegrationExpected(expected_vf[fs * 4 + lane], immediate, maximum);
			expected_vi[id] = static_cast<u16>(static_cast<s16>(expected_vi[is]) + static_cast<s16>(expected_vi[it]));
		}
		program.insert(program.end(), {NOP_L, NOP_U | 0x40000000u, NOP_L, NOP_U});
		std::array<Snapshot, 2> results{};
		std::array<double, 2> cold_median_us{}, hot_median_ns{};
		std::array<u64, 2> native_bytes{}, cold_op_delta{};
		for (u32 mode = 0; mode < 2; ++mode)
		{
			EmuCoreXOracleSetVUIR(core, mode ? 1 : 0);
			std::array<double, 7> cold_samples{};
			bool exact_cold_count = true;
			for (u32 sample = 0; sample < cold_samples.size(); ++sample)
			{
				const u64 before = EmuCoreXOracleVUIRMaxMinIInstructions(core);
				const auto start = std::chrono::steady_clock::now();
				results[mode] = RunVU(core, true, program, 0x80, 0, 256, false,
					nullptr, initial_vf.data(), initial_acc.data(), initial_vi.data());
				cold_samples[sample] = std::chrono::duration<double, std::micro>(
					std::chrono::steady_clock::now() - start).count();
				cold_op_delta[mode] = EmuCoreXOracleVUIRMaxMinIInstructions(core) - before;
				exact_cold_count &= cold_op_delta[mode] == (mode ? 24u : 0u);
			}
			std::sort(cold_samples.begin(), cold_samples.end());
			cold_median_us[mode] = cold_samples[cold_samples.size() / 2];
			Check(exact_cold_count, "MAXi/MINIi mixed benchmark compiles exactly 24 eligible ops only in shared mode");
			Check(std::memcmp(results[mode].regs.VF, expected_vf.data(), sizeof(expected_vf)) == 0 &&
				std::memcmp(&results[mode].regs.ACC, initial_acc.data(), sizeof(initial_acc)) == 0,
				"MAXi/MINIi mixed benchmark matches independent VF and ACC model");
			for (u32 reg = 1; reg < expected_vi.size(); ++reg)
				if (reg != REG_TPC && reg != REG_STATUS_FLAG && reg != REG_MAC_FLAG && reg != REG_CLIP_FLAG)
					Check(results[mode].regs.VI[reg].UL == expected_vi[reg],
						"MAXi/MINIi mixed benchmark IADD state matches independent model");
			std::vector<JitBlockProfile> profiles;
			if (core) VU1_JitGetBlockProfiles(profiles); else VU0_JitGetBlockProfiles(profiles);
			for (const auto& profile : profiles)
			{
				Check(profile.startpc >= 0x80 && profile.startpc < 0x80 + program.size() * sizeof(u32),
					"MAXi/MINIi benchmark native blocks remain within fixture bounds");
				native_bytes[mode] += profile.host_size;
			}
			Check(!profiles.empty() && native_bytes[mode] != 0,
				"MAXi/MINIi benchmark observes compiled native blocks");
			VURegs& regs = core ? VU1 : VU0;
			BaseVUmicroCPU* cpu = core ? static_cast<BaseVUmicroCPU*>(&CpuMicroVU1) :
				static_cast<BaseVUmicroCPU*>(&CpuMicroVU0);
			const u64 before_hot = EmuCoreXOracleVUIRMaxMinIInstructions(core);
			std::array<double, 9> hot_samples{};
			bool hot_state = true;
			for (u32 sample = 0; sample < hot_samples.size(); ++sample)
			{
				constexpr u32 iterations = 1024;
				const auto start = std::chrono::steady_clock::now();
				for (u32 iteration = 0; iteration < iterations; ++iteration)
				{
					std::memcpy(regs.VF, initial_vf.data(), sizeof(regs.VF));
					std::memset(regs.VI, 0, sizeof(regs.VI));
					for (u32 reg = 1; reg < initial_vi.size(); ++reg) regs.VI[reg].UL = initial_vi[reg];
					std::memcpy(&regs.ACC, initial_acc.data(), sizeof(initial_acc));
					regs.cycle = 0;
					regs.VI[REG_TPC].UL = 0x80 / 8;
					VU0.VI[REG_VPU_STAT].UL = core ? 0x100 : 1;
					cpu->SetStartPC(0x80);
					for (u32 call = 0; call < 128 && (VU0.VI[REG_VPU_STAT].UL & (core ? 0x100 : 1)); ++call)
						cpu->Execute(256);
					hot_state &= VU0.VI[REG_VPU_STAT].UL == 0 &&
						std::memcmp(regs.VF, expected_vf.data(), sizeof(regs.VF)) == 0 &&
						std::memcmp(&regs.ACC, initial_acc.data(), sizeof(initial_acc)) == 0 &&
						regs.cycle == results[mode].regs.cycle;
					for (u32 reg = 1; reg < expected_vi.size(); ++reg)
						if (reg != REG_TPC && reg != REG_STATUS_FLAG && reg != REG_MAC_FLAG && reg != REG_CLIP_FLAG)
							hot_state &= regs.VI[reg].UL == expected_vi[reg];
				}
				hot_samples[sample] = std::chrono::duration<double, std::nano>(
					std::chrono::steady_clock::now() - start).count() / iterations;
			}
			std::sort(hot_samples.begin(), hot_samples.end());
			hot_median_ns[mode] = hot_samples[hot_samples.size() / 2];
			Check(hot_state, "MAXi/MINIi hot replay preserves independent state and exact cycles");
			Check(EmuCoreXOracleVUIRMaxMinIInstructions(core) == before_hot &&
				cold_op_delta[mode] == (mode ? 24u : 0u),
				"MAXi/MINIi hot replay performs no additional guest-op compilation");
		}
		Compare(results[0], results[1], "MAXi/MINIi mixed benchmark full-state legacy/shared parity");
		Check(results[0].regs.cycle == results[1].regs.cycle,
			"MAXi/MINIi mixed benchmark preserves exact cycles");
		std::printf("VUIRMAXMINIBENCH core=%u ops=24 mixed_iadd_ops=24 cold_reset_setup_compile_execute_samples=7 hot_samples=9 hot_iterations=1024 legacy_cold_median_us=%.3f shared_cold_median_us=%.3f legacy_hot_median_ns_per_replay=%.3f shared_hot_median_ns_per_replay=%.3f legacy_native_bytes=%llu shared_native_bytes=%llu\n",
			core, cold_median_us[0], cold_median_us[1], hot_median_ns[0], hot_median_ns[1],
			static_cast<unsigned long long>(native_bytes[0]), static_cast<unsigned long long>(native_bytes[1]));
		vu_fpcr = saved_vu_fpcr;
		ee_fpcr = saved_ee_fpcr;
	}
	EmuCoreXOracleSetVUIR(0, -1);
	EmuCoreXOracleSetVUIR(1, -1);
	EmuConfig.Gamefixes.IbitHack = saved_ibit_hack;
}
