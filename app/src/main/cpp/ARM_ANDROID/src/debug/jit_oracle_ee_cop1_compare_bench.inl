// SPDX-License-Identifier: GPL-3.0+
// Bounded EE COP1 compare benchmark. Include after
// jit_oracle_ee_cop1_compare.inl and the EE oracle helpers.

extern "C" u64 EmuCoreXOracleEeIrCompareFusions();

namespace
{
	constexpr std::array<u32, 4> EECop1CompareBenchFuncts = {0x30, 0x32, 0x34, 0x36};
	constexpr std::array<u32, 12> EECop1CompareBenchEdges = {
		0x00000000u, 0x80000000u, 0x00000001u, 0x807fffffu,
		0x00800000u, 0x80800000u, 0x3f800000u, 0xc0400000u,
		0x7f800000u, 0xff800000u, 0x7fc12345u, 0xffc54321u};

	struct EECop1CompareBenchState
	{
		std::vector<u32> code;
		std::array<GPR_reg, 32> gpr{};
		std::array<u32, 32> fpr{};
		std::array<u32, 32> fcr{};
		std::array<u32, 32> cp0{};
		std::array<u32, 32> expected_fcr{};
		u64 expected_gpr10 = 0;
		u64 expected_gpr11 = 0;
	};

	EECop1CompareBenchState MakeEECop1CompareBenchState()
	{
		EECop1CompareBenchState state;
		state.cp0[12] = 0x20000000u; // Status.CU1.
		for (u32 reg = 1; reg < 32; ++reg)
		{
			state.gpr[reg].UD[0] = 0x1234567800000000ull | (0x76543210u + reg * 13u);
			state.gpr[reg].UD[1] = 0xabcdef0100000000ull | (0x89abcdefu + reg * 17u);
			state.fpr[reg] = 0x51000000u ^ (reg * 0x01020408u);
			state.fcr[reg] = 0x87654321u ^ (reg * 0x01020304u);
		}
		state.gpr[0] = {};
		state.fcr[0] = 0x2e00u;
		state.fcr[31] = 0x0055a0f0u; // Poison non-C bits, with C initially clear.
		state.gpr[10].UD[0] = 0x1111222233334444ull;
		state.gpr[10].UD[1] = 0x5555666677778888ull;
		state.gpr[11].UD[0] = 0x1234u;
		state.gpr[11].UD[1] = 0x9999aaaabbbbccccull;
		for (u32 i = 0; i < 8; ++i)
		{
			state.fpr[1u + i] = EECop1CompareBenchEdges[(i * 5u + 2u) % EECop1CompareBenchEdges.size()];
			state.fpr[9u + i] = EECop1CompareBenchEdges[(i * 7u + 5u) % EECop1CompareBenchEdges.size()];
		}

		for (u32 i = 0; i < 32; ++i)
		{
			const u32 funct = EECop1CompareBenchFuncts[i % EECop1CompareBenchFuncts.size()];
			const u32 fs = 1u + (i % 8u);
			const u32 ft = 9u + (i % 8u);
			const u32 lhs = state.fpr[fs];
			const u32 rhs = state.fpr[ft];
			state.code.push_back(MipsCop1(0x10, ft, fs, 0, funct));
			if ((i & 1u) == 0)
				state.code.push_back(MipsI(0x09, 11, 11, 1)); // Interleaved addiu t3, t3, 1.
			const bool condition = EeCop1CompareExpected(funct, lhs, rhs);
			state.fcr[31] = (state.fcr[31] & ~0x00800000u) | (condition ? 0x00800000u : 0u);
		}
		state.code.push_back(MipsCop1(0x02, 10, 31, 0, 0)); // cfc1 t2, fcr31 consumes the final result.
		state.expected_fcr = state.fcr;
		state.expected_gpr10 = static_cast<u64>(static_cast<s64>(static_cast<s32>(state.fcr[31])));
		state.expected_gpr11 = 0x1244u;
		return state;
	}

	void EECop1CompareBenchResidentExecute(const EECop1CompareBenchState& state)
	{
		std::memset(&cpuRegs, 0, sizeof(cpuRegs));
		std::memset(&fpuRegs, 0, sizeof(fpuRegs));
		std::memset(&VU0.VF, 0, sizeof(VU0.VF));
		std::memset(&VU0.VI, 0, sizeof(VU0.VI));
		std::memset(&VU0.ACC, 0, sizeof(VU0.ACC));
		std::memcpy(cpuRegs.GPR.r, state.gpr.data(), sizeof(cpuRegs.GPR.r));
		std::memcpy(cpuRegs.CP0.r, state.cp0.data(), sizeof(cpuRegs.CP0.r));
		for (u32 reg = 0; reg < 32; ++reg)
		{
			fpuRegs.fpr[reg].UL = state.fpr[reg];
			fpuRegs.fprc[reg] = state.fcr[reg];
		}
		cpuRegs.pc = EE_TEST_PC;
		cpuRegs.cycle = 0;
		cpuRegs.branch = 0;
		cpuRegs.IsDelaySlot = 0;
		cpuRegs.nextEventCycle = 0x7fffffffu;
		EEsCycle = EEoCycle = 0;
		EmuCoreXEEForceExitAfterFirstBlock();
		Cpu->Execute();
	}

	bool EECop1CompareBenchMatches(const EESnapshot& actual, const EESnapshot& reference,
		const EECop1CompareBenchState& expected)
	{
		bool same = actual.pc == EE_TEST_PC + static_cast<u32>(expected.code.size() * 4u) &&
			actual.gpr[10] == expected.expected_gpr10 && actual.gpr[11] == expected.expected_gpr11 &&
			std::memcmp(actual.fpr, expected.fpr.data(), sizeof(actual.fpr)) == 0 &&
			std::memcmp(actual.fcr, expected.expected_fcr.data(), sizeof(actual.fcr)) == 0;
		for (u32 reg = 0; reg < 32; ++reg)
		{
			if (reg == 0)
				same &= actual.gpr[reg] == 0 && actual.gpr_upper[reg] == 0;
			else if (reg != 10 && reg != 11)
				same &= actual.gpr[reg] == expected.gpr[reg].UD[0] &&
					actual.gpr_upper[reg] == expected.gpr[reg].UD[1];
			else if (reg == 10 || reg == 11)
				same &= actual.gpr_upper[reg] == expected.gpr[reg].UD[1];
		}
		same &= actual.fcr31 == expected.expected_fcr[31] && actual.hi == reference.hi &&
			actual.lo == reference.lo && actual.hi1 == reference.hi1 && actual.lo1 == reference.lo1 &&
			actual.acc == reference.acc && actual.accflag == reference.accflag &&
			std::memcmp(actual.vu0_vf, reference.vu0_vf, sizeof(actual.vu0_vf)) == 0 &&
			std::memcmp(actual.vu0_vi, reference.vu0_vi, sizeof(actual.vu0_vi)) == 0 &&
			std::memcmp(actual.vu0_acc, reference.vu0_acc, sizeof(actual.vu0_acc)) == 0 &&
			std::memcmp(actual.scratch, reference.scratch, sizeof(actual.scratch)) == 0;
		for (u32 reg = 0; reg < 32; ++reg)
		{
			if (reg != 1 && reg != 9 && reg != 11) // Random, Count and Compare are cycle-relative.
				same &= actual.cp0[reg] == reference.cp0[reg];
		}
		return same;
	}

	u32 EECop1CompareBenchProfileBytes(u32 guest_bytes, u32* matching_blocks = nullptr)
	{
		std::vector<JitBlockProfile> profiles;
		EE_JitGetBlockProfiles(profiles);
		u32 bytes = 0;
		u32 blocks = 0;
		for (const JitBlockProfile& profile : profiles)
		{
			if (profile.type == 0 && profile.startpc >= EE_TEST_PC &&
				profile.startpc < EE_TEST_PC + guest_bytes)
			{
				bytes += profile.host_size;
				++blocks;
			}
		}
		if (matching_blocks)
			*matching_blocks = blocks;
		return bytes;
	}

	bool EECop1CompareBenchCountsEqual(const u64 a[4], const u64 b[4])
	{
		for (u32 i = 0; i < 4; ++i)
			if (a[i] != b[i])
				return false;
		return true;
	}
}

void EEIrCop1CompareBenchmarks()
{
	const EECop1CompareBenchState state = MakeEECop1CompareBenchState();
	const EESnapshot interpreter_reference = RunEEProgram(false, state.code, false, nullptr, state.cp0.data(),
		state.gpr.data(), state.fpr.data(), state.fcr.data());
	const EESnapshot legacy_cycle_reference = RunEEProgramWithIrMode(0, state.code, state.gpr.data(),
		state.cp0.data(), state.fpr.data(), state.fcr.data());
	constexpr u32 cold_samples = 7;
	constexpr u32 hot_samples = 7;
	constexpr u32 hot_repeats = 512;
	constexpr u32 protection_warmup_replays = 65536;
	const u32 guest_bytes = static_cast<u32>(state.code.size() * sizeof(u32));
	u32 legacy_steady_native_bytes = 0;

	for (int mode : {0, 1})
	{
		std::vector<double> cold_us;
		std::vector<double> hot_ns_per_block;
		u32 cold_native_bytes = 0;
		for (u32 sample = 0; sample < cold_samples; ++sample)
		{
			u64 before[4]{}, after[4]{};
			const u64 fusions_before = EmuCoreXOracleEeIrCompareFusions();
			EeCop1CompareReadCounts(before);
			const auto start = std::chrono::steady_clock::now();
			const EESnapshot result = RunEEProgramWithIrMode(mode, state.code, state.gpr.data(), state.cp0.data(),
				state.fpr.data(), state.fcr.data());
			const auto stop = std::chrono::steady_clock::now();
			cold_us.push_back(std::chrono::duration<double, std::micro>(stop - start).count());
			EeCop1CompareReadCounts(after);
			CompareEE(interpreter_reference, result, "COP1 compare benchmark cold full state");
			Check(result.cycle == legacy_cycle_reference.cycle,
				"COP1 compare benchmark cold exact legacy/shared JIT cycles");
			Check(EECop1CompareBenchMatches(result, interpreter_reference, state),
				"COP1 compare benchmark cold output matches independent raw/FCR/GPR model");
			bool exact_counts = true;
			for (u32 i = 0; i < 4; ++i)
				exact_counts &= after[i] == before[i] + (mode == 1 ? 8u : 0u);
			Check(exact_counts,
				"COP1 compare benchmark cold compile counts are eight per opcode only in shared mode");
			const u64 expected_fusions = mode == 1 ? 24u : 0u;
			Check(EmuCoreXOracleEeIrCompareFusions() - fusions_before == expected_fusions,
				"COP1 compare benchmark cold fused graph count is three predicates times eight only in shared mode");
			u32 matching_blocks = 0;
			cold_native_bytes = EECop1CompareBenchProfileBytes(guest_bytes, &matching_blocks);
			Check(matching_blocks != 0 && cold_native_bytes != 0,
				"COP1 compare benchmark captures summed actual native bytes for its full guest block range");
		}
		std::sort(cold_us.begin(), cold_us.end());

		// Compile once in the selected mode, then drive the existing manual-page
		// protection counter through its overflow/recompile window before timing.
		const EESnapshot warm = RunEEProgramWithIrMode(mode, state.code, state.gpr.data(), state.cp0.data(),
			state.fpr.data(), state.fcr.data());
		CompareEE(interpreter_reference, warm, "COP1 compare benchmark initial warm full state");
		EmuCoreXOracleSetEeIrMode(mode);
		for (u32 replay = 0; replay < protection_warmup_replays; ++replay)
			EECop1CompareBenchResidentExecute(state);
		EESnapshot warm_state;
		CaptureEE(warm_state);
		Check(EECop1CompareBenchMatches(warm_state, interpreter_reference, state) &&
			warm_state.cycle == legacy_cycle_reference.cycle,
			"COP1 compare benchmark protection warmup reaches correct steady guest state and cycles");
		u32 steady_blocks = 0;
		const u32 steady_native_bytes = EECop1CompareBenchProfileBytes(guest_bytes, &steady_blocks);
		if (mode == 0)
			legacy_steady_native_bytes = steady_native_bytes;
		const u64 steady_fusions = EmuCoreXOracleEeIrCompareFusions();
		u64 steady_counts[4]{};
		EeCop1CompareReadCounts(steady_counts);
		const EESnapshot probe_before = warm_state;
		EECop1CompareBenchResidentExecute(state);
		EESnapshot probe_after;
		CaptureEE(probe_after);
		u64 probe_counts[4]{};
		EeCop1CompareReadCounts(probe_counts);
		u32 probe_blocks = 0;
		const u32 probe_native_bytes = EECop1CompareBenchProfileBytes(guest_bytes, &probe_blocks);
		Check(EECop1CompareBenchMatches(probe_after, interpreter_reference, state) &&
			probe_after.cycle == legacy_cycle_reference.cycle,
			"COP1 compare benchmark steady probe executes and validates the complete block");
		Check(EECop1CompareBenchCountsEqual(steady_counts, probe_counts) &&
			EmuCoreXOracleEeIrCompareFusions() == steady_fusions,
			"COP1 compare benchmark protection warmup reaches zero-recompile steady state");
		Check(steady_blocks != 0 && probe_blocks == steady_blocks && steady_native_bytes != 0 &&
			probe_native_bytes == steady_native_bytes,
			"COP1 compare benchmark steady native block sizes remain stable after probe");

		const u64 hot_count_before_fusions = EmuCoreXOracleEeIrCompareFusions();
		u64 hot_counts_before[4]{};
		EeCop1CompareReadCounts(hot_counts_before);
		for (u32 sample = 0; sample < hot_samples; ++sample)
		{
			const auto start = std::chrono::steady_clock::now();
			for (u32 repeat = 0; repeat < hot_repeats; ++repeat)
				EECop1CompareBenchResidentExecute(state);
			EESnapshot result;
			CaptureEE(result);
			const bool valid = EECop1CompareBenchMatches(result, interpreter_reference, state) &&
				result.cycle == legacy_cycle_reference.cycle;
			const auto stop = std::chrono::steady_clock::now();
			hot_ns_per_block.push_back(std::chrono::duration<double, std::nano>(stop - start).count() / hot_repeats);
			Check(valid, "COP1 compare benchmark hot sample includes full reset, execute, snapshot, and validation");
			CompareEE(interpreter_reference, result, "COP1 compare benchmark hot full state");
		}
		u64 hot_counts_after[4]{};
		EeCop1CompareReadCounts(hot_counts_after);
		Check(EECop1CompareBenchCountsEqual(hot_counts_before, hot_counts_after) &&
			EmuCoreXOracleEeIrCompareFusions() == hot_count_before_fusions,
			"COP1 compare benchmark hot batches contain no additional compile/fusion operations");
		EmuCoreXOracleSetEeIrMode(-1);
		std::sort(hot_ns_per_block.begin(), hot_ns_per_block.end());
		Check(mode != 1 || (legacy_steady_native_bytes != 0 && steady_native_bytes <= legacy_steady_native_bytes),
			"COP1 compare fused shared block is no larger than the legacy block");
		std::printf("COP1_COMPARE_BENCH mode=%s cold_reset_setup_compile_median_us=%.3f hot_median_ns_per_block_including_reset_snapshot_validation=%.3f cold_native_block_bytes=%u steady_native_block_bytes=%u cold_hot_samples=%u/%u repeats_per_hot_sample=%u protection_warmup_replays=%u\n",
			mode == 0 ? "legacy" : "shared", cold_us[cold_us.size() / 2],
			hot_ns_per_block[hot_ns_per_block.size() / 2], cold_native_bytes, steady_native_bytes,
			cold_samples, hot_samples, hot_repeats, protection_warmup_replays);
	}
}
