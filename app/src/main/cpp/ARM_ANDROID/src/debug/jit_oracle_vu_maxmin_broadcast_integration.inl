// SPDX-FileCopyrightText: 2026 EmuCoreX Team
// SPDX-License-Identifier: GPL-3.0+
// Production-route fixture for the MAXx/y/z/w and MINIx/y/z/w shared
// VU path. Include after jit_oracle_vu_maxmin_integration.inl and call
// VUSharedMaxMinBroadcastIntegrationTests().

extern "C" u64 EmuCoreXOracleVUIRMaxMinBroadcastInstructions(u32 core);
extern "C" u64 EmuCoreXOracleVUIRMaxMinInstructions(u32 core);

void VUSharedMaxMinBroadcastIntegrationTests()
{
	const auto upper = [](bool maximum, u32 selector, u32 fs, u32 ft, u32 fd, u32 mask) {
		return (maximum ? 0x10u : 0x14u) + selector | (fd << 6) | (fs << 11) |
			(ft << 16) | ((mask & 15u) << 21);
	};
	const auto move = [](u32 fs, u32 ft, u32 mask) {
		return 0x8000033cu | (fs << 11) | (ft << 16) | ((mask & 15u) << 21);
	};
	const auto single = [&](bool maximum, u32 selector, u32 fs, u32 ft, u32 fd, u32 mask) {
		return std::vector<u32>{NOP_L, upper(maximum, selector, fs, ft, fd, mask),
			NOP_L, NOP_U | 0x40000000u, NOP_L, NOP_U};
	};
	using VfWords = std::array<u32, 128>;
	const auto model = [](VfWords& expected, const VfWords& input, bool maximum,
		u32 selector, u32 fs, u32 ft, u32 fd, u32 mask) {
		if (fd == 0) return;
		const u32 rhs = input[ft * 4 + selector];
		for (u32 lane = 0; lane < 4; ++lane)
			if (mask & (8u >> lane))
				expected[fd * 4 + lane] = VuMaxMinExpected(
					input[fs * 4 + lane], rhs, maximum);
	};
	constexpr std::array<u32, 24> edges = {
		0, 0x80000000u, 1, 0x80000001u, 0x007fffffu, 0x807fffffu,
		0x00800000u, 0x80800000u, 0x3f800000u, 0xbf800000u,
		0x7f7fffffu, 0xff7fffffu, 0x7f800000u, 0xff800000u,
		0x7f800001u, 0xff800001u, 0x7fa12345u, 0xffa54321u,
		0x7fc12345u, 0xffc54321u, 0x7fffffffu, 0xffffffffu,
		0x40000000u, 0xc0000000u};
	VfWords initial{};
	initial[3] = 0x3f800000u;
	for (u32 reg = 1; reg < 32; ++reg)
		for (u32 lane = 0; lane < 4; ++lane)
			initial[reg * 4 + lane] = 0x5a13579bu ^ (reg * 0x01020408u) ^ (lane * 0x137abdeu);
	constexpr std::array<u32, 4> acc = {0x3fa00000u, 0x40200000u, 0xc0700000u, 0x40900000u};
	std::array<u32, 16> vi{};
	for (u32 reg = 1; reg < 16; ++reg) vi[reg] = 0x1200u + reg;
	vi[1] = vi[2];

	for (u32 core = 0; core < 2; ++core)
	{
		FPControlRegister& core_fpcr = core ? EmuConfig.Cpu.VU1FPCR : EmuConfig.Cpu.VU0FPCR;
		const FPControlRegister saved_core = core_fpcr, saved_ee = EmuConfig.Cpu.FPUFPCR;
		core_fpcr.SetRoundMode(FPRoundMode::Nearest);
		EmuConfig.Cpu.FPUFPCR = core_fpcr;
		const auto run_case = [&](const std::vector<u32>& program, const VfWords& input,
			const VfWords& expected, u32 broadcast_admitted, u32 base_admitted,
			const char* label, bool run_interpreter = true) {
			std::printf("MAXMINBROADCASTCASE core=%u %s\n", core, label);
			EmuCoreXOracleSetVUIR(core, 0);
			const u64 broadcast_before = EmuCoreXOracleVUIRMaxMinBroadcastInstructions(core);
			const u64 base_before = EmuCoreXOracleVUIRMaxMinInstructions(core);
			const u64 other_broadcast_before = EmuCoreXOracleVUIRMaxMinBroadcastInstructions(core ^ 1u);
			const u64 other_base_before = EmuCoreXOracleVUIRMaxMinInstructions(core ^ 1u);
			Snapshot interp{};
			if (run_interpreter)
				interp = RunVU(core, false, program, 0x80, 0, 128, false,
					nullptr, input.data(), acc.data(), vi.data());
			const Snapshot legacy = RunVU(core, true, program, 0x80, 0, 128, false,
				nullptr, input.data(), acc.data(), vi.data());
			Check(EmuCoreXOracleVUIRMaxMinBroadcastInstructions(core) == broadcast_before &&
				EmuCoreXOracleVUIRMaxMinInstructions(core) == base_before,
				"forced legacy broadcast MAX/MINI leaves both shared operation counters unchanged");
			EmuCoreXOracleSetVUIR(core, 1);
			const Snapshot shared = RunVU(core, true, program, 0x80, 0, 128, false,
				nullptr, input.data(), acc.data(), vi.data());
			if (run_interpreter)
				Compare(interp, legacy, "broadcast MAX/MINI interpreter/legacy complete VU parity");
			Compare(legacy, shared, "broadcast MAX/MINI shared/legacy complete VU parity");
			Check((!run_interpreter || std::memcmp(interp.regs.VF, expected.data(), sizeof(expected)) == 0) &&
				std::memcmp(legacy.regs.VF, expected.data(), sizeof(expected)) == 0 &&
				std::memcmp(shared.regs.VF, expected.data(), sizeof(expected)) == 0,
				"broadcast MAX/MINI full VF array matches independent raw-bit selector model");
			Check(shared.regs.cycle == legacy.regs.cycle && !legacy.fallback && !shared.fallback,
				"broadcast MAX/MINI retains exact legacy cycles through native dispatcher");
			Check(std::memcmp(&shared.regs.ACC, acc.data(), sizeof(acc)) == 0 &&
				shared.regs.VI[REG_MAC_FLAG].UL == 0 &&
				shared.regs.VI[REG_STATUS_FLAG].UL == 0 &&
				shared.regs.VI[REG_CLIP_FLAG].UL == 0,
				"broadcast MAX/MINI preserves initial ACC and zero canonical flags (initial VI seed covers general VIs only)");
			Check(EmuCoreXOracleVUIRMaxMinBroadcastInstructions(core) == broadcast_before + broadcast_admitted &&
				EmuCoreXOracleVUIRMaxMinInstructions(core) == base_before + base_admitted &&
				EmuCoreXOracleVUIRMaxMinBroadcastInstructions(core ^ 1u) == other_broadcast_before &&
				EmuCoreXOracleVUIRMaxMinInstructions(core ^ 1u) == other_base_before,
				"broadcast MAX/MINI admission increments only its operation counter on the selected VU core");
		};

		// Every broadcast selector and destination mask uses a canonical primary
		// case plus RHS/destination/source aliases. In particular fd==ft proves
		// the selected RHS lane is captured before the destination write.
		for (bool maximum : {false, true})
		for (u32 selector = 0; selector < 4; ++selector)
		for (u32 mask = 1; mask < 16; ++mask)
		for (const std::array<u32, 3> regs : {std::array<u32, 3>{7, 8, 9}, {7, 8, 8},
			{7, 8, 7}, {7, 7, 9}, {7, 7, 7}, {0, 8, 9}, {7, 0, 9}, {0, 0, 9}})
		{
			VfWords input = initial;
			for (u32 lane = 0; lane < 4; ++lane)
			{
				input[7 * 4 + lane] = edges[(mask + selector * 3 + lane * 5) % edges.size()];
				input[8 * 4 + lane] = edges[(mask * 3 + selector * 5 + lane * 7) % edges.size()];
			}
			VfWords expected = input;
			model(expected, input, maximum, selector, regs[0], regs[1], regs[2], mask);
			run_case(single(maximum, selector, regs[0], regs[1], regs[2], mask),
				input, expected, 1, 0, "selector/mask/alias/VF0 raw corpus");
		}
		for (bool maximum : {false, true})
		for (u32 selector = 0; selector < 4; ++selector)
		{
			run_case(single(maximum, selector, 7, 8, 0, 15), initial, initial, 0, 0,
				"VF0 destination is not admitted");
			run_case(single(maximum, selector, 7, 8, 9, 0), initial, initial, 0, 0,
				"empty mask is not admitted");
		}

		for (bool maximum : {false, true})
		for (u32 selector = 0; selector < 4; ++selector)
		{
			// Lower MOVE changes one Ft lane before the consumer pair. The model
			// reads the post-MOVE source, catching stale RHS lane selection.
			VfWords expected = initial;
			for (u32 lane = 0; lane < 4; ++lane) expected[32 + lane] = initial[28 + lane];
			const VfWords after_move = expected;
			model(expected, after_move, maximum, selector, 8, 6, 9, 15);
			run_case({move(7, 8, 15), NOP_U, NOP_L,
				upper(maximum, selector, 8, 6, 9, 15), NOP_L,
				NOP_U | 0x40000000u, NOP_L, NOP_U}, initial, expected, 1, 0,
				"lower producer updates broadcast source before upper consumer");

			// Same-pair lower reads the old destination while upper reads the
			// old selected RHS before any same-pair writeback.
			expected = initial;
			for (u32 lane = 0; lane < 4; ++lane) expected[40 + lane] = initial[36 + lane];
			model(expected, initial, maximum, selector, 7, 8, 9, 5);
			run_case({move(9, 10, 15), upper(maximum, selector, 7, 8, 9, 5),
				NOP_L, NOP_U | 0x40000000u, NOP_L, NOP_U}, initial, expected, 1, 0,
				"same-pair lower reads old destination around broadcast upper write");

			// Same-pair lower writes the whole source after the upper operation
			// has sampled its old selected lane.
			expected = initial;
			for (u32 lane = 0; lane < 4; ++lane) expected[28 + lane] = initial[24 + lane];
			model(expected, initial, maximum, selector, 7, 8, 9, 10);
			run_case({move(6, 7, 15), upper(maximum, selector, 7, 8, 9, 10),
				NOP_L, NOP_U | 0x40000000u, NOP_L, NOP_U}, initial, expected, 1, 0,
				"same-pair upper samples old lower-written broadcast source");

			// The upper operation must also sample the selected old Ft lane when
			// the paired lower MOVE overwrites Ft itself.
			expected = initial;
			for (u32 lane = 0; lane < 4; ++lane) expected[32 + lane] = initial[24 + lane];
			model(expected, initial, maximum, selector, 7, 8, 9, 15);
			run_case({move(6, 8, 15), upper(maximum, selector, 7, 8, 9, 15),
				NOP_L, NOP_U | 0x40000000u, NOP_L, NOP_U}, initial, expected, 1, 0,
				"same-pair upper samples old selected Ft lane before lower overwrite");

			// The broadcast operation itself occupies the branch delay pair.
			// The branch is taken using the seeded equal VI values above.
			expected = initial;
			model(expected, initial, maximum, selector, 7, 8, 9, 10);
			const u32 branch = (0x28u << 25) | (1u << 11) | (2u << 16) | 2u;
			run_case({branch, NOP_U, NOP_L,
				upper(maximum, selector, 7, 8, 9, 10), NOP_L,
				NOP_U, NOP_L, NOP_U | 0x40000000u, NOP_L, NOP_U},
				initial, expected, 1, 0, "taken branch delay slot broadcast operation");
		}

		// Seed nonzero architectural MAC/STATUS/CLIP flags through real
		// instructions, drain their pipelines, then resume each route into all
		// eight broadcast variants. RunVU's initial_vi parameter only seeds the
		// 16 general VI registers; these special flags are checked from the
		// resulting architectural state instead of indexing that array.
		for (bool maximum : {false, true})
		for (u32 selector = 0; selector < 4; ++selector)
		{
			VfWords input = initial;
			constexpr std::array<u32, 4> a = {0x40000000u, 0xc0000000u, 0x40000000u, 0xc0000000u};
			constexpr std::array<u32, 4> b = {0xbf800000u, 0xbf800000u, 0xbf800000u, 0x3f800000u};
			for (u32 lane = 0; lane < 4; ++lane)
			{
				input[28 + lane] = a[lane];
				input[32 + lane] = b[lane];
			}
			const u32 add = 0x28u | (12u << 6) | (7u << 11) | (8u << 16) | (15u << 21);
			const u32 clip = 0x3fu | (7u << 6) | (7u << 11) | (8u << 16);
			const std::vector<u32> seed_program = {NOP_L, add, NOP_L, clip,
				NOP_L, NOP_U, NOP_L, NOP_U, NOP_L, NOP_U, NOP_L, NOP_U,
				NOP_L, NOP_U | 0x40000000u, NOP_L, NOP_U};
			VfWords expected = input;
			constexpr std::array<u32, 4> sum = {0x3f800000u, 0xc0400000u, 0x3f800000u, 0xbf800000u};
			for (u32 lane = 0; lane < 4; ++lane) expected[48 + lane] = sum[lane];
			model(expected, input, maximum, selector, 7, 8, 10, 15);
			std::array<Snapshot, 3> results{};
			for (u32 route = 0; route < 3; ++route)
			{
				EmuCoreXOracleSetVUIR(core, route == 2 ? 1 : 0);
				const u64 before_broadcast = EmuCoreXOracleVUIRMaxMinBroadcastInstructions(core);
				const u64 before_base = EmuCoreXOracleVUIRMaxMinInstructions(core);
				const Snapshot seeded = RunVU(core, route != 0, seed_program, 0x80, 0, 128,
					false, nullptr, input.data(), acc.data(), vi.data());
				Check(seeded.regs.VI[REG_MAC_FLAG].UL != 0 &&
					seeded.regs.VI[REG_STATUS_FLAG].UL != 0 &&
					seeded.regs.VI[REG_CLIP_FLAG].UL != 0 &&
					std::memcmp(&seeded.regs.ACC, acc.data(), sizeof(acc)) == 0,
					"broadcast fixture seeds all three nonzero canonical flags and ACC");
				results[route] = RunVU(core, route != 0,
					single(maximum, selector, 7, 8, 10, 15), 0x100, 0, 128, true);
				const Snapshot& result = results[route];
				Check(std::memcmp(result.regs.VF, expected.data(), sizeof(expected)) == 0 &&
					std::memcmp(&result.regs.ACC, acc.data(), sizeof(acc)) == 0 &&
					result.regs.VI[REG_MAC_FLAG].UL == seeded.regs.VI[REG_MAC_FLAG].UL &&
					result.regs.VI[REG_STATUS_FLAG].UL == seeded.regs.VI[REG_STATUS_FLAG].UL &&
					result.regs.VI[REG_CLIP_FLAG].UL == seeded.regs.VI[REG_CLIP_FLAG].UL,
					"broadcast MAX/MINI preserves seeded flags/ACC and matches independent full VF model");
				Check(EmuCoreXOracleVUIRMaxMinBroadcastInstructions(core) ==
					before_broadcast + (route == 2 ? 1u : 0u) &&
					EmuCoreXOracleVUIRMaxMinInstructions(core) == before_base,
					"seeded broadcast admission increments only its selected route counter");
			}
			Compare(results[0], results[1], "seeded broadcast interpreter/legacy complete VU parity");
			Compare(results[1], results[2], "seeded broadcast shared/legacy complete VU parity");
			Check(results[1].regs.cycle == results[2].regs.cycle,
				"seeded broadcast MAX/MINI preserves exact legacy cycles");
		}

		EmuCoreXOracleSetVUIR(core, -1);
		core_fpcr = saved_core;
		EmuConfig.Cpu.FPUFPCR = saved_ee;
	}
}
