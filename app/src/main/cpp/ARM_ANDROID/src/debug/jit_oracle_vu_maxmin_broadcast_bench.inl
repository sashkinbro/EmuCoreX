// SPDX-License-Identifier: GPL-3.0+
// Bounded mixed VU broadcast MAX/MINI benchmark. Include after
// jit_oracle_vu_maxmin_broadcast_integration.inl.

void VUSharedMaxMinBroadcastBenchmarks()
{
	const FPControlRegisterBackup caller_fpcr(EmuConfig.Cpu.FPUFPCR);
	const auto iadd = [](u32 is, u32 it, u32 id) {
		return 0x80000000u | 0x30u | (is << 11) | (it << 16) | (id << 6);
	};
	const auto broadcast = [](bool maximum, u32 selector, u32 fs, u32 ft, u32 fd, u32 mask) {
		return ((maximum ? 0x10u : 0x14u) + selector) | (fd << 6) | (fs << 11) |
			(ft << 16) | ((mask & 15u) << 21);
	};
	std::array<u32, 128> initial_vf{};
	initial_vf[3] = 0x3f800000u;
	for (u32 reg = 1; reg < 32; ++reg)
		for (u32 lane = 0; lane < 4; ++lane)
			initial_vf[reg * 4 + lane] = 0x5a13579bu ^ (reg * 0x01020408u) ^ (lane * 0x137abdeu);
	std::array<u32, 16> initial_vi{};
	for (u32 reg = 1; reg < initial_vi.size(); ++reg)
		initial_vi[reg] = reg * 11u;
	constexpr std::array<u32, 4> initial_acc = {0x3fa00000u, 0x40200000u, 0xc0700000u, 0x40900000u};

	for (u32 core = 0; core < 2; ++core)
	{
		FPControlRegister& vu_fpcr = core ? EmuConfig.Cpu.VU1FPCR : EmuConfig.Cpu.VU0FPCR;
		FPControlRegister& ee_fpcr = EmuConfig.Cpu.FPUFPCR;
		const FPControlRegister saved_vu_fpcr = vu_fpcr;
		const FPControlRegister saved_ee_fpcr = ee_fpcr;
		vu_fpcr.SetRoundMode(FPRoundMode::Nearest);
		ee_fpcr = vu_fpcr;
		const auto iadd_expected = [&](std::array<u32, 16>& vi, u32 is, u32 it, u32 id) {
			const s32 sum = static_cast<s16>(vi[is]) + static_cast<s16>(vi[it]);
			vi[id] = static_cast<u16>(sum);
		};
		std::vector<u32> program;
		std::array<u32, 128> expected_vf = initial_vf;
		std::array<u32, 16> expected_vi = initial_vi;
		for (u32 step = 0; step < 24; ++step)
		{
			const u32 is = 1 + (step % 7), it = 8 + (step % 5), id = 13 + (step % 3);
			const u32 fs = 1 + ((step * 5 + 3) % 31);
			const u32 ft = 1 + ((step * 11 + 7) % 31);
			const u32 fd = 1 + ((step * 13 + 9) % 31);
			const u32 selector = step % 4;
			const bool maximum = (step & 1) == 0;
			program.push_back(iadd(is, it, id));
			program.push_back(broadcast(maximum, selector, fs, ft, fd, 15));
			iadd_expected(expected_vi, is, it, id);
			const u32 rhs = expected_vf[ft * 4 + selector];
			for (u32 lane = 0; lane < 4; ++lane)
				expected_vf[fd * 4 + lane] = VuMaxMinExpected(
					expected_vf[fs * 4 + lane], rhs, maximum);
		}
		program.insert(program.end(), {NOP_L, NOP_U | 0x40000000u, NOP_L, NOP_U});

		std::array<Snapshot, 2> result{};
		std::array<u64, 2> native_bytes{};
		std::array<double, 2> cold_median_us{}, hot_median_ns{};
		std::array<u64, 2> cold_broadcast_delta{};
		for (u32 mode = 0; mode < 2; ++mode)
		{
			EmuCoreXOracleSetVUIR(core, mode ? 1 : 0);
			std::array<double, 7> cold_samples{};
			bool exact_cold_counts = true;
			for (u32 sample = 0; sample < cold_samples.size(); ++sample)
			{
				const u64 before_broadcast = EmuCoreXOracleVUIRMaxMinBroadcastInstructions(core);
				const u64 before_base = EmuCoreXOracleVUIRMaxMinInstructions(core);
				const auto start = std::chrono::steady_clock::now();
				result[mode] = RunVU(core, true, program, 0x80, 0, 128, false,
					nullptr, initial_vf.data(), initial_acc.data(), initial_vi.data());
				cold_samples[sample] = std::chrono::duration<double, std::micro>(
					std::chrono::steady_clock::now() - start).count();
				const u64 delta = EmuCoreXOracleVUIRMaxMinBroadcastInstructions(core) - before_broadcast;
				exact_cold_counts &= delta == (mode ? 24u : 0u) &&
					EmuCoreXOracleVUIRMaxMinInstructions(core) == before_base;
				cold_broadcast_delta[mode] = delta;
			}
			std::sort(cold_samples.begin(), cold_samples.end());
			cold_median_us[mode] = cold_samples[cold_samples.size() / 2];
			Check(exact_cold_counts,
				"broadcast MAX/MINI benchmark compiles exactly 24 selected ops only in shared cold samples");
			bool independent = std::memcmp(result[mode].regs.VF, expected_vf.data(), sizeof(expected_vf)) == 0 &&
				std::memcmp(&result[mode].regs.ACC, initial_acc.data(), sizeof(initial_acc)) == 0;
			for (u32 reg = 1; reg < expected_vi.size(); ++reg)
				if (reg != REG_TPC && reg != REG_STATUS_FLAG && reg != REG_MAC_FLAG && reg != REG_CLIP_FLAG)
					independent &= result[mode].regs.VI[reg].UL == expected_vi[reg];
			Check(independent,
				"broadcast MAX/MINI benchmark VF, IADD VI and ACC match independent raw/integer models");

			std::vector<JitBlockProfile> profiles;
			if (core) VU1_JitGetBlockProfiles(profiles); else VU0_JitGetBlockProfiles(profiles);
			bool bounded = !profiles.empty();
			for (const auto& profile : profiles)
			{
				bounded &= profile.startpc >= 0x80 &&
					profile.startpc < 0x80 + program.size() * sizeof(u32);
				native_bytes[mode] += profile.host_size;
			}
			Check(bounded && native_bytes[mode] != 0,
				"broadcast MAX/MINI benchmark reports native bytes from bounded mixed guest blocks");

			// Measure each cache while it is resident. The next mode's cold
			// RunVU resets the microVU cache. This is a combined broadcast/IADD
			// workload because the shared-VU gate also controls the IADD route.
			VURegs& regs = core ? VU1 : VU0;
			BaseVUmicroCPU* cpu = core ? static_cast<BaseVUmicroCPU*>(&CpuMicroVU1) :
				static_cast<BaseVUmicroCPU*>(&CpuMicroVU0);
			const u64 before_hot_broadcast = EmuCoreXOracleVUIRMaxMinBroadcastInstructions(core);
			const u64 before_hot_base = EmuCoreXOracleVUIRMaxMinInstructions(core);
			const u64 before_hot_ir = EmuCoreXOracleVUIRInstructions(core);
			std::array<double, 9> hot_samples{};
			bool hot_state = true;
			for (u32 sample = 0; sample < hot_samples.size(); ++sample)
			{
				constexpr u32 iterations = 4096;
				const auto start = std::chrono::steady_clock::now();
				for (u32 iteration = 0; iteration < iterations; ++iteration)
				{
					std::memcpy(regs.VF, initial_vf.data(), sizeof(regs.VF));
					std::memset(regs.VI, 0, sizeof(regs.VI));
					for (u32 reg = 1; reg < initial_vi.size(); ++reg)
						regs.VI[reg].UL = initial_vi[reg];
					std::memcpy(&regs.ACC, initial_acc.data(), sizeof(initial_acc));
					regs.cycle = 0;
					regs.VI[REG_TPC].UL = 0x80 / 8;
					VU0.VI[REG_VPU_STAT].UL = core ? 0x100 : 1;
					cpu->SetStartPC(0x80);
					for (u32 call = 0; call < 128 &&
						(VU0.VI[REG_VPU_STAT].UL & (core ? 0x100 : 1)); ++call)
						cpu->Execute(128);
					hot_state &= VU0.VI[REG_VPU_STAT].UL == 0 &&
						std::memcmp(regs.VF, expected_vf.data(), sizeof(regs.VF)) == 0 &&
						std::memcmp(&regs.ACC, initial_acc.data(), sizeof(initial_acc)) == 0 &&
						regs.cycle == result[mode].regs.cycle;
					for (u32 reg = 1; reg < expected_vi.size(); ++reg)
						if (reg != REG_TPC && reg != REG_STATUS_FLAG && reg != REG_MAC_FLAG && reg != REG_CLIP_FLAG)
							hot_state &= regs.VI[reg].UL == expected_vi[reg];
				}
				hot_samples[sample] = std::chrono::duration<double, std::nano>(
					std::chrono::steady_clock::now() - start).count() / iterations;
			}
			std::sort(hot_samples.begin(), hot_samples.end());
			hot_median_ns[mode] = hot_samples[hot_samples.size() / 2];
			Check(hot_state,
				"broadcast MAX/MINI hot replay preserves independent VF/VI/ACC and exact cycles");
			Check(EmuCoreXOracleVUIRMaxMinBroadcastInstructions(core) == before_hot_broadcast &&
				EmuCoreXOracleVUIRMaxMinInstructions(core) == before_hot_base &&
				EmuCoreXOracleVUIRInstructions(core) == before_hot_ir &&
				cold_broadcast_delta[mode] == (mode ? 24u : 0u),
				"broadcast MAX/MINI hot benchmark replay performs no new selected-op or base-op compiles");
		}
		Compare(result[0], result[1], "broadcast MAX/MINI benchmark legacy/shared complete state");
		Check(result[0].regs.cycle == result[1].regs.cycle,
			"broadcast MAX/MINI mixed benchmark preserves exact legacy/shared cycles");
		std::printf("VUIRMAXMINBROADCASTBENCH core=%u broadcast_ops=24 mixed_iadd_ops=24 cold_samples=7 hot_samples=9 hot_iterations=4096 hot_order=legacy_then_shared legacy_cold_median_us=%.3f shared_cold_median_us=%.3f legacy_hot_median_ns_per_replay=%.3f shared_hot_median_ns_per_replay=%.3f legacy_native_block_bytes=%llu shared_native_block_bytes=%llu\n",
			core, cold_median_us[0], cold_median_us[1], hot_median_ns[0], hot_median_ns[1],
			static_cast<unsigned long long>(native_bytes[0]),
			static_cast<unsigned long long>(native_bytes[1]));
		vu_fpcr = saved_vu_fpcr;
		ee_fpcr = saved_ee_fpcr;
	}
	EmuCoreXOracleSetVUIR(0, -1);
	EmuCoreXOracleSetVUIR(1, -1);
}
