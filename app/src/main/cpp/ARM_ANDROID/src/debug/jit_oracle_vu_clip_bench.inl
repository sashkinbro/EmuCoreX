// SPDX-FileCopyrightText: 2026 EmuCoreX Team
// SPDX-License-Identifier: GPL-3.0+
// Bounded mixed VU CLIP benchmark. Include after jit_oracle_vu_clip_integration.inl.

void VUSharedClipBenchmarks()
{
	const FPControlRegisterBackup caller_fpcr(EmuConfig.Cpu.FPUFPCR);
	const auto clip = [](u32 fs, u32 ft) {
		return 0x1ffu | (fs << 11) | (ft << 16);
	};
	const auto fcset = [](u32 history) {
		return (0x11u << 25) | (history & 0x00ffffffu);
	};
	const auto fcget = [](u32 it) {
		return (0x1cu << 25) | (it << 16);
	};
	const auto iadd = [](u32 is, u32 it, u32 id) {
		return 0x80000000u | 0x30u | (is << 11) | (it << 16) | (id << 6);
	};
	using VfWords = std::array<u32, 128>;
	VfWords initial_vf{};
	initial_vf[3] = 0x3f800000u;
	for (u32 reg = 1; reg < 32; ++reg)
		for (u32 lane = 0; lane < 4; ++lane)
			initial_vf[reg * 4 + lane] = 0x5a13579bu ^ (reg * 0x01020408u) ^ (lane * 0x0137abdeu);
	std::array<u32, 16> initial_vi{};
	for (u32 reg = 1; reg < initial_vi.size(); ++reg)
		initial_vi[reg] = 0x2100u + reg * 3u;
	initial_vi[1] = 0x1234u;
	initial_vi[2] = 0xfffeu;
	constexpr std::array<u32, 4> initial_acc = {
		0x3fa00000u, 0x40200000u, 0xc0700000u, 0x40900000u};
	constexpr u32 history_seed = 0x00a1b2c3u;

	std::vector<u32> program;
	program.reserve(2 * (1 + 32 + 6 + 1 + 2));
	program.insert(program.end(), {fcset(history_seed), NOP_U});
	std::array<u32, 16> expected_vi = initial_vi;
	for (u32 step = 0; step < 32; ++step)
	{
		// IADD is independent of CLIP but occupies its lower half, giving the
		// measured block a small realistic integer workload around the flag path.
		program.push_back(iadd(1, 2, 3));
		program.push_back(clip(1 + (step % 15), 16 + (step % 15)));
	}
	for (u32 bubble = 0; bubble < 6; ++bubble)
		program.insert(program.end(), {NOP_L, NOP_U});
	program.insert(program.end(), {fcget(5), NOP_U});
	program.insert(program.end(), {NOP_L, NOP_U | 0x40000000u, NOP_L, NOP_U});
	const s32 iadd_sum = static_cast<s16>(initial_vi[1]) + static_cast<s16>(initial_vi[2]);
	expected_vi[3] = static_cast<u16>(iadd_sum);

	VfWords expected_vf = initial_vf;
	u32 expected_history = history_seed;
	for (u32 step = 0; step < 32; ++step)
	{
		const u32 fs = 1 + (step % 15), ft = 16 + (step % 15);
		const std::array<u32, 4> source = {
			initial_vf[fs * 4], initial_vf[fs * 4 + 1],
			initial_vf[fs * 4 + 2], initial_vf[fs * 4 + 3]};
		expected_history = VuClipHistoryExpected(expected_history, source, initial_vf[ft * 4 + 3]);
	}
	expected_vi[5] = expected_history & 0xfffu;

	for (u32 core = 0; core < 2; ++core)
	{
		FPControlRegister& vu_fpcr = core ? EmuConfig.Cpu.VU1FPCR : EmuConfig.Cpu.VU0FPCR;
		FPControlRegister& ee_fpcr = EmuConfig.Cpu.FPUFPCR;
		const FPControlRegister saved_vu_fpcr = vu_fpcr;
		const FPControlRegister saved_ee_fpcr = ee_fpcr;
		vu_fpcr.SetRoundMode(FPRoundMode::Nearest);
		ee_fpcr = vu_fpcr;
		std::array<Snapshot, 2> results{};
		std::array<u64, 2> native_bytes{};
		std::array<u64, 2> cold_counter_delta{};
		std::array<double, 2> cold_median_us{}, hot_median_ns{};
		for (u32 mode = 0; mode < 2; ++mode)
		{
			EmuCoreXOracleSetVUIR(core, mode ? 1 : 0);
			std::array<double, 7> cold_samples{};
			bool exact_cold_counter = true;
			for (u32 sample = 0; sample < cold_samples.size(); ++sample)
			{
				const auto before = VuClipCounts();
				const auto start = std::chrono::steady_clock::now();
				results[mode] = RunVU(core, true, program, 0x80, 0, 256, false,
					nullptr, initial_vf.data(), initial_acc.data(), initial_vi.data());
				cold_samples[sample] = std::chrono::duration<double, std::micro>(
					std::chrono::steady_clock::now() - start).count();
				const auto after = VuClipCounts();
				cold_counter_delta[mode] = after[core] - before[core];
				exact_cold_counter &= cold_counter_delta[mode] == (mode ? 32u : 0u) &&
					after[core ^ 1u] == before[core ^ 1u];
			}
			std::sort(cold_samples.begin(), cold_samples.end());
			cold_median_us[mode] = cold_samples[cold_samples.size() / 2];
			Check(exact_cold_counter,
				"VU CLIP benchmark compiles exactly 32 routed CLIPs per shared cold run and none in legacy mode");

			bool independent = std::memcmp(results[mode].regs.VF, expected_vf.data(), sizeof(expected_vf)) == 0 &&
				std::memcmp(&results[mode].regs.ACC, initial_acc.data(), sizeof(initial_acc)) == 0 &&
				results[mode].regs.VI[REG_CLIP_FLAG].UL == expected_history;
			for (u32 reg = 1; reg < expected_vi.size(); ++reg)
				if (reg != REG_TPC && reg != REG_STATUS_FLAG && reg != REG_MAC_FLAG && reg != REG_CLIP_FLAG)
					independent &= results[mode].regs.VI[reg].UL == expected_vi[reg];
			Check(independent,
				"VU CLIP benchmark matches independent raw VF/history and integer VI models");
			Check(!results[mode].fallback, "VU CLIP benchmark remains on native VU dispatch");

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
				"VU CLIP benchmark reports complete native blocks within the guest fixture bounds");

			VURegs& regs = core ? VU1 : VU0;
			BaseVUmicroCPU* cpu = core ? static_cast<BaseVUmicroCPU*>(&CpuMicroVU1) :
				static_cast<BaseVUmicroCPU*>(&CpuMicroVU0);
			const auto before_hot = VuClipCounts();
			std::array<double, 7> hot_samples{};
			bool hot_state = true;
			for (u32 sample = 0; sample < hot_samples.size(); ++sample)
			{
				constexpr u32 iterations = 512;
				const auto start = std::chrono::steady_clock::now();
				for (u32 iteration = 0; iteration < iterations; ++iteration)
				{
					std::memcpy(regs.VF, initial_vf.data(), sizeof(regs.VF));
					std::memset(regs.VI, 0, sizeof(regs.VI));
					for (u32 reg = 1; reg < initial_vi.size(); ++reg)
						regs.VI[reg].UL = initial_vi[reg];
					std::memcpy(&regs.ACC, initial_acc.data(), sizeof(initial_acc));
					regs.clipflag = 0;
					regs.cycle = 0;
					regs.VI[REG_TPC].UL = 0x80 / 8;
					VU0.VI[REG_VPU_STAT].UL = core ? 0x100 : 1;
					cpu->SetStartPC(0x80);
					for (u32 call = 0; call < 128 &&
						(VU0.VI[REG_VPU_STAT].UL & (core ? 0x100 : 1)); ++call)
						cpu->Execute(256);
					hot_state &= VU0.VI[REG_VPU_STAT].UL == 0 &&
						std::memcmp(regs.VF, expected_vf.data(), sizeof(regs.VF)) == 0 &&
						std::memcmp(&regs.ACC, initial_acc.data(), sizeof(initial_acc)) == 0 &&
						regs.VI[REG_CLIP_FLAG].UL == expected_history &&
						regs.VI[REG_STATUS_FLAG].UL == results[mode].regs.VI[REG_STATUS_FLAG].UL &&
						regs.VI[REG_MAC_FLAG].UL == results[mode].regs.VI[REG_MAC_FLAG].UL &&
						regs.clipflag == results[mode].regs.clipflag &&
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
			const auto after_hot = VuClipCounts();
			Check(hot_state, "VU CLIP resident replay preserves independent state and exact legacy cycles");
			Check(after_hot == before_hot && cold_counter_delta[mode] == (mode ? 32u : 0u),
				"VU CLIP resident replay performs no additional routed-op compilation");
		}
		Compare(results[0], results[1], "VU CLIP mixed benchmark complete legacy/shared state parity");
		Check(results[0].regs.cycle == results[1].regs.cycle,
			"VU CLIP mixed benchmark preserves exact legacy/shared cycles");
		std::printf("VUIRCLIPBENCH core=%u clip_ops=32 mixed_iadd_ops=32 fcset_history=0x%06x fcget_consumer=vi5 cold_reset_setup_compile_execute_samples=7 hot_samples=7 hot_iterations=512 hot_order=legacy_then_shared hot_timing=state_reset_dispatch_execute_validate legacy_cold_median_us=%.3f shared_cold_median_us=%.3f legacy_hot_median_ns_per_replay=%.3f shared_hot_median_ns_per_replay=%.3f legacy_native_block_bytes=%llu shared_native_block_bytes=%llu\n",
			core, history_seed, cold_median_us[0], cold_median_us[1], hot_median_ns[0], hot_median_ns[1],
			static_cast<unsigned long long>(native_bytes[0]),
			static_cast<unsigned long long>(native_bytes[1]));
		vu_fpcr = saved_vu_fpcr;
		ee_fpcr = saved_ee_fpcr;
	}
	EmuCoreXOracleSetVUIR(0, -1);
	EmuCoreXOracleSetVUIR(1, -1);
}
