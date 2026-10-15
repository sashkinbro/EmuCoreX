// SPDX-FileCopyrightText: 2026 EmuCoreX Team
// SPDX-License-Identifier: GPL-3.0+
// EE COP2 macro integration fixture for raw VU0 VCLIP.
// Include after the EE oracle helpers; entry point: EEVuClipIrTests().

extern "C" void EmuCoreXOracleGetVuIrClipCounts(u64* vu0, u64* vu1);

static std::array<u64, 2> EEVuClipReadCounts()
{
	std::array<u64, 2> counts{};
	EmuCoreXOracleGetVuIrClipCounts(&counts[0], &counts[1]);
	return counts;
}

static s32 EEVuClipSignedWord(u32 word)
{
	return static_cast<s32>(word);
}

static u32 EEVuClipExpectedMask(const std::array<u32, 4>& fs, u32 ft_w)
{
	// Match the architectural raw integer comparison, including the special
	// denormal threshold. No host floating-point operations enter this oracle.
	const s32 threshold = (ft_w & 0x7f800000u) ?
		EEVuClipSignedWord(ft_w & 0x7fffffffu) : static_cast<s32>(0x007fffffu);
	u32 mask = 0;
	for (u32 lane = 0; lane < 3; ++lane)
	{
		if (EEVuClipSignedWord(fs[lane]) > threshold)
			mask |= 1u << (lane * 2);
		if (EEVuClipSignedWord(fs[lane] ^ 0x80000000u) > threshold)
			mask |= 1u << (lane * 2 + 1);
	}
	return mask;
}

static u32 EEVuClipExpectedHistory(u32 history, const std::array<u32, 4>& fs, u32 ft_w)
{
	return ((history << 6) | EEVuClipExpectedMask(fs, ft_w)) & 0x00ffffffu;
}

void EEVuClipIrTests()
{
	const FPControlRegister saved_ee_fpcr = EmuConfig.Cpu.FPUFPCR;
	const FPControlRegister saved_vu0_fpcr = EmuConfig.Cpu.VU0FPCR;
	EmuConfig.Cpu.FPUFPCR.SetRoundMode(FPRoundMode::Nearest);
	EmuConfig.Cpu.VU0FPCR.SetRoundMode(FPRoundMode::Nearest);
	const FPControlRegisterBackup host_fpcr(EmuConfig.Cpu.FPUFPCR);
	const auto vclip = [](u32 fs, u32 ft) {
		// SPECIAL2 table index 31; bits 6..10 are fixed VCLIP opcode bits.
		return (0x12u << 26) | (0x1fu << 21) | (ft << 16) | (fs << 11) | 0x1ffu;
	};
	const auto ctc_clip = [](u32 rt) {
		return (0x12u << 26) | (6u << 21) | (rt << 16) | (REG_CLIP_FLAG << 11);
	};
	const auto cfc_clip = [](u32 rt) {
		return (0x12u << 26) | (2u << 21) | (rt << 16) | (REG_CLIP_FLAG << 11);
	};
	const auto setup_history = [&](std::vector<u32>& code, u32 history) {
		code.push_back(MipsI(15, 0, 8, history >> 16));
		code.push_back(MipsI(13, 8, 8, history));
		code.push_back(ctc_clip(8));
	};
	using VfWords = std::array<u32, 128>;
	std::array<GPR_reg, 32> initial_gpr{};
	for (u32 reg = 1; reg < 32; ++reg)
	{
		initial_gpr[reg].UD[0] = 0x1234567800000000ull | (0x76543210u + reg);
		initial_gpr[reg].UD[1] = 0xabcdef0100000000ull | (0x89abcdefu + reg);
	}
	std::array<u32, 32> initial_fpr{}, initial_fcr{};
	for (u32 reg = 1; reg < 32; ++reg)
	{
		initial_fpr[reg] = 0x51000000u ^ (reg * 0x01020408u);
		initial_fcr[reg] = 0x87654321u ^ (reg * 0x01020304u);
	}
	initial_fcr[0] = 0x2e00u;
	VfWords base_vf{};
	base_vf[3] = 0x3f800000u; // Immutable VF0 = (0, 0, 0, 1.0).
	for (u32 reg = 1; reg < 32; ++reg)
		for (u32 lane = 0; lane < 4; ++lane)
			base_vf[reg * 4 + lane] = 0x5a13579bu ^ (reg * 0x01020408u) ^ (lane * 0x137abdeu);

	const auto check_expected = [&](const EESnapshot& snap, const VfWords& expected_vf,
		const std::array<GPR_reg, 32>& expected_gpr, u32 history, const char* label) {
		bool exact = true;
		for (u32 reg = 0; reg < 32; ++reg)
		{
			exact &= snap.gpr[reg] == expected_gpr[reg].UD[0] &&
				snap.gpr_upper[reg] == expected_gpr[reg].UD[1];
			exact &= snap.fpr[reg] == initial_fpr[reg] && snap.fcr[reg] == initial_fcr[reg];
			exact &= snap.vu0_vi[reg] == (reg == REG_CLIP_FLAG ? history : 0u);
		}
		exact &= std::memcmp(snap.vu0_vf, expected_vf.data(), sizeof(snap.vu0_vf)) == 0;
		for (u32 lane = 0; lane < 4; ++lane)
			exact &= snap.vu0_acc[lane] == 0;
		Check(exact, label);
	};

	const auto run_case = [&](const char* label, const VfWords& input, u32 fs, u32 ft,
		u32 initial_history, u32 clip_count, bool two_clips = false) {
		VfWords expected_vf = input;
		const std::array<u32, 4> fs_words = {
			input[fs * 4], input[fs * 4 + 1], input[fs * 4 + 2], input[fs * 4 + 3]};
		u32 history = EEVuClipExpectedHistory(initial_history, fs_words, input[ft * 4 + 3]);
		std::vector<u32> code;
		setup_history(code, initial_history);
		code.push_back(vclip(fs, ft));
		if (two_clips)
			history = EEVuClipExpectedHistory(history, fs_words, input[ft * 4 + 3]);
		if (two_clips)
			code.push_back(vclip(fs, ft));
		code.push_back(cfc_clip(9));

		auto expected_gpr = initial_gpr;
		expected_gpr[8].UD[0] = initial_history;
		expected_gpr[9].UD[0] = history;
		char detail[192];
		std::snprintf(detail, sizeof(detail), "EE VCLIP %s", label);

		EmuCoreXOracleSetEeIrMode(0);
		EmuCoreXOracleSetVUIR(0, 0);
		const auto before = EEVuClipReadCounts();
		const EESnapshot legacy = RunEEProgramWithIrMode(0, code, initial_gpr.data(), nullptr,
			initial_fpr.data(), initial_fcr.data(), false, input.data());
		Check(EEVuClipReadCounts() == before,
			"EE VCLIP forced legacy leaves VU0/VU1 shared-operation counters unchanged");
		const EESnapshot interp = RunEEProgram(false, code, false, nullptr, nullptr,
			initial_gpr.data(), initial_fpr.data(), initial_fcr.data(), input.data());
		Check(EEVuClipReadCounts() == before,
			"EE VCLIP interpreter leaves VU0/VU1 shared-operation counters unchanged");
		EmuCoreXOracleSetVUIR(0, 1);
		const EESnapshot shared = RunEEProgramWithIrMode(0, code, initial_gpr.data(), nullptr,
			initial_fpr.data(), initial_fcr.data(), false, input.data());
		const auto after = EEVuClipReadCounts();
		CompareEE(interp, legacy, detail);
		CompareEE(legacy, shared, detail);
		Check(legacy.cycle == shared.cycle, "EE VCLIP preserves exact legacy/shared EE cycles");
		Check(after[0] == before[0] + clip_count && after[1] == before[1],
			"EE VCLIP macro admits only expected VU0 operations");
		check_expected(interp, expected_vf, expected_gpr, history, detail);
		check_expected(legacy, expected_vf, expected_gpr, history, detail);
		check_expected(shared, expected_vf, expected_gpr, history, detail);
	};

	// Raw signed-word boundaries include denormal thresholds, infinities and
	// NaN payloads. Every operation starts from an independently seeded nonzero
	// architectural clip history and checks its six-bit shift/append result.
	constexpr std::array<u32, 16> edges = {
		0x00000000u, 0x80000000u, 0x00000001u, 0x80000001u,
		0x007fffffu, 0x807fffffu, 0x00800000u, 0x80800000u,
		0x3f800000u, 0xbf800000u, 0x7f7fffffu, 0xff7fffffu,
		0x7f800000u, 0xff800000u, 0x7fc12345u, 0x7f800001u};
	for (u32 item = 0; item < edges.size(); ++item)
	{
		VfWords input = base_vf;
		const u32 fs = 5 + item;
		const u32 ft = 16 + item;
		for (u32 lane = 0; lane < 3; ++lane)
			input[fs * 4 + lane] = edges[(item + lane * 5) % edges.size()];
		input[ft * 4 + 3] = edges[(item * 7 + 3) % edges.size()];
		char label[96];
		std::snprintf(label, sizeof(label), "raw edge %u", item);
		run_case(label, input, fs, ft, 0x00a1b2c3u ^ (item * 0x1001u), 1);
	}

	// Fs==Ft proves the upper reads X/Y/Z and W from the same old vector value.
	for (u32 threshold : {0u, 0x80000000u, 0x007fffffu, 0x00800000u,
		0x3f800000u, 0xbf800000u, 0x7f800000u, 0xff800000u})
	{
		VfWords input = base_vf;
		constexpr u32 reg = 9;
		input[reg * 4] = threshold;
		input[reg * 4 + 1] = threshold ^ 0x80000000u;
		input[reg * 4 + 2] = 0x7fc12345u;
		input[reg * 4 + 3] = threshold;
		run_case("Fs equals Ft strict threshold", input, reg, reg, 0x0055aa33u, 1);
	}

	// VF0 is an immutable raw source (XYZ=0) and an immutable +1.0 W threshold.
	{
		VfWords input = base_vf;
		input[12 * 4 + 3] = 0x3f800000u;
		run_case("VF0 source", input, 0, 12, 0x00f00d12u, 1);
		run_case("VF0 W threshold", input, 12, 0, 0x00f00d13u, 1);
		run_case("VF0 source and threshold", input, 0, 0, 0x00f00d14u, 1);
	}

	// Repeated VCLIP operations consume the previous architectural history.
	run_case("repeated clip history append", base_vf, 7, 8, 0x0040cafeu, 2, true);

	// VCLIP in a taken or untaken EE branch delay slot must execute exactly
	// once. A fallthrough-only marker proves the untaken path was actually used;
	// both routes join before CFC2 and the bounded sentinel loop.
	for (bool taken : {false, true})
	{
		VfWords input = base_vf;
		const u32 fs = 7, ft = 8;
		const std::array<u32, 4> fs_words = {
			input[fs * 4], input[fs * 4 + 1], input[fs * 4 + 2], input[fs * 4 + 3]};
		constexpr u32 history_seed = 0x00912345u;
		const u32 history = EEVuClipExpectedHistory(history_seed, fs_words, input[ft * 4 + 3]);
		std::vector<u32> code;
		setup_history(code, history_seed);
		code.push_back(MipsI(taken ? 4u : 5u, 0, 0, 3)); // BEQ true / BNE false.
		code.push_back(vclip(fs, ft)); // Delay slot.
		code.push_back(MipsI(9, 0, 10, 0x55)); // Reached only on the untaken path.
		code.push_back(MipsI(4, 0, 0, 1));	// Join at CFC2.
		code.push_back(MipsR(0, 0, 0, 0, 0));	// Branch delay NOP.
		code.push_back(cfc_clip(9));
		auto expected_gpr = initial_gpr;
		expected_gpr[8].UD[0] = history_seed;
		expected_gpr[9].UD[0] = history;
		if (!taken)
			expected_gpr[10].UD[0] = 0x55;
		const auto before = EEVuClipReadCounts();
		EmuCoreXOracleSetEeIrMode(0);
		EmuCoreXOracleSetVUIR(0, 0);
		const EESnapshot legacy = RunEEProgramWithIrMode(0, code, initial_gpr.data(), nullptr,
			initial_fpr.data(), initial_fcr.data(), true, input.data());
		const EESnapshot interp = RunEEProgram(false, code, true, nullptr, nullptr,
			initial_gpr.data(), initial_fpr.data(), initial_fcr.data(), input.data());
		Check(EEVuClipReadCounts() == before,
			"EE VCLIP branch-delay interpreter/legacy leaves shared counters unchanged");
		EmuCoreXOracleSetVUIR(0, 1);
		const EESnapshot shared = RunEEProgramWithIrMode(0, code, initial_gpr.data(), nullptr,
			initial_fpr.data(), initial_fcr.data(), true, input.data());
		const auto after = EEVuClipReadCounts();
		CompareEE(interp, legacy, "EE VCLIP branch delay interpreter/legacy full state");
		CompareEE(legacy, shared, "EE VCLIP branch delay legacy/shared full state");
		Check(legacy.cycle == shared.cycle, "EE VCLIP branch delay keeps exact legacy/shared cycles");
		Check(after[0] == before[0] + 1 && after[1] == before[1],
			"EE VCLIP branch delay counts only the successful VU0 macro");
		check_expected(interp, input, expected_gpr, history, "EE VCLIP branch delay independent model");
		check_expected(legacy, input, expected_gpr, history, "EE VCLIP branch delay legacy independent model");
		check_expected(shared, input, expected_gpr, history, "EE VCLIP branch delay shared independent model");
	}

	EmuCoreXOracleSetVUIR(0, -1);
	EmuCoreXOracleSetEeIrMode(-1);
	EmuConfig.Cpu.FPUFPCR = saved_ee_fpcr;
	EmuConfig.Cpu.VU0FPCR = saved_vu0_fpcr;
}
