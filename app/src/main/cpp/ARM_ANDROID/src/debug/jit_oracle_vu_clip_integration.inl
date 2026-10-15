// SPDX-FileCopyrightText: 2026 EmuCoreX Team
// SPDX-License-Identifier: GPL-3.0+
// Isolated VU0/VU1 production-route tests for the raw-bit CLIP operation.
// Include after the common VU oracle helpers and call VUSharedClipIntegrationTests().

extern "C" void EmuCoreXOracleGetVuIrClipCounts(u64* vu0, u64* vu1);

static std::array<u64, 2> VuClipCounts()
{
	std::array<u64, 2> counts{};
	EmuCoreXOracleGetVuIrClipCounts(&counts[0], &counts[1]);
	return counts;
}

static s32 VuClipSignedWord(u32 word)
{
	// Match the interpreter's raw-word signed comparisons on the ARM64 target.
	return static_cast<s32>(word);
}

static u32 VuClipMaskExpected(const std::array<u32, 4>& fs, u32 ft_w)
{
	const s32 threshold = (ft_w & 0x7f800000u) ?
		VuClipSignedWord(ft_w & 0x7fffffffu) : static_cast<s32>(0x007fffffu);
	u32 mask = 0;
	for (u32 lane = 0; lane < 3; ++lane)
	{
		if (VuClipSignedWord(fs[lane]) > threshold)
			mask |= 1u << (lane * 2);
		if (VuClipSignedWord(fs[lane] ^ 0x80000000u) > threshold)
			mask |= 1u << (lane * 2 + 1);
	}
	return mask;
}

static u32 VuClipHistoryExpected(u32 history, const std::array<u32, 4>& fs, u32 ft_w)
{
	return ((history << 6) | VuClipMaskExpected(fs, ft_w)) & 0x00ffffffu;
}

void VUSharedClipIntegrationTests()
{
	const auto saved_vu0_fpcr = EmuConfig.Cpu.VU0FPCR;
	const auto saved_vu1_fpcr = EmuConfig.Cpu.VU1FPCR;
	const auto saved_ee_fpcr = EmuConfig.Cpu.FPUFPCR;
	const auto clip = [](u32 fs, u32 ft) {
		// VCLIP is upper encoding 0x1ff. Bits 6..10 are opcode bits, not fd.
		return 0x1ffu | (fs << 11) | (ft << 16);
	};
	const auto move = [](u32 fs, u32 ft, u32 mask = 15u) {
		return 0x8000033cu | (fs << 11) | (ft << 16) | ((mask & 15u) << 21);
	};
	const auto fcset = [](u32 history) {
		return (0x11u << 25) | (history & 0x00ffffffu);
	};
	const auto fcget = [](u32 it) {
		return (0x1cu << 25) | (it << 16);
	};
	const auto append_pair = [](std::vector<u32>& program, u32 lower, u32 upper) {
		program.push_back(lower);
		program.push_back(upper);
	};
	const auto finish = [&](std::vector<u32>& program) {
		append_pair(program, NOP_L, NOP_U | 0x40000000u);
		append_pair(program, NOP_L, NOP_U);
	};
	using VfWords = std::array<u32, 128>;
	VfWords initial{};
	initial[3] = 0x3f800000u;
	for (u32 reg = 1; reg < 32; ++reg)
		for (u32 lane = 0; lane < 4; ++lane)
			initial[reg * 4 + lane] = 0x5a13579bu ^ (reg * 0x01020408u) ^ (lane * 0x137abdeu);
	std::array<u32, 16> initial_vi{};
	for (u32 reg = 1; reg < initial_vi.size(); ++reg)
		initial_vi[reg] = 0x2100u + reg * 3u;
	initial_vi[1] = initial_vi[2] = 7;
	constexpr std::array<u32, 4> initial_acc = {0x3fa00000u, 0x40200000u, 0xc0700000u, 0x40900000u};
	constexpr std::array<u32, 16> thresholds = {
		0x00000000u, 0x80000000u, 0x00000001u, 0x80000001u,
		0x007fffffu, 0x807fffffu, 0x00800000u, 0x80800000u,
		0x3f800000u, 0xbf800000u, 0x7f7fffffu, 0xff7fffffu,
		0x7f800000u, 0xff800000u, 0x7fc12345u, 0x7f800001u};
	constexpr std::array<u32, 16> coordinates = {
		0x00000000u, 0x80000000u, 0x00000001u, 0x80000001u,
		0x007fffffu, 0x807fffffu, 0x00800000u, 0x80800000u,
		0x3f800000u, 0xbf800000u, 0x7f7fffffu, 0xff7fffffu,
		0x7f800000u, 0xff800000u, 0x7fc12345u, 0xff800001u};

	for (u32 core = 0; core < 2; ++core)
	{
		FPControlRegister& vu_fpcr = core ? EmuConfig.Cpu.VU1FPCR : EmuConfig.Cpu.VU0FPCR;
		vu_fpcr.SetRoundMode(FPRoundMode::Nearest);
		EmuConfig.Cpu.FPUFPCR = vu_fpcr;

		const auto run_case = [&](const std::vector<u32>& program, const VfWords& input,
			const VfWords& expected_vf, u32 expected_history, u32 expected_clip_count,
			const char* label, u32 expected_vi1 = 7u) {
			std::printf("VUCLIPCASE core=%u %s\n", core, label);
			EmuCoreXOracleSetVUIR(core, 0);
			const auto before = VuClipCounts();
			const Snapshot interp = RunVU(core, false, program, 0x80, 0, 256, false,
				nullptr, input.data(), initial_acc.data(), initial_vi.data());
			const Snapshot legacy = RunVU(core, true, program, 0x80, 0, 256, false,
				nullptr, input.data(), initial_acc.data(), initial_vi.data());
			const auto after_legacy = VuClipCounts();
			Check(after_legacy == before,
				"forced interpreter/legacy CLIP does not increment successful shared counters");
			EmuCoreXOracleSetVUIR(core, 1);
			const Snapshot shared = RunVU(core, true, program, 0x80, 0, 256, false,
				nullptr, input.data(), initial_acc.data(), initial_vi.data());
			const auto after_shared = VuClipCounts();
			Compare(interp, legacy, "VU CLIP interpreter/legacy complete state");
			Compare(legacy, shared, "VU CLIP shared/legacy complete state");
			Check(std::memcmp(interp.regs.VF, expected_vf.data(), sizeof(expected_vf)) == 0 &&
				std::memcmp(legacy.regs.VF, expected_vf.data(), sizeof(expected_vf)) == 0 &&
				std::memcmp(shared.regs.VF, expected_vf.data(), sizeof(expected_vf)) == 0 &&
				std::memcmp(&shared.regs.ACC, initial_acc.data(), sizeof(initial_acc)) == 0,
				"VU CLIP preserves raw VF/ACC state against independent model");
			Check(interp.regs.VI[REG_CLIP_FLAG].UL == expected_history &&
				legacy.regs.VI[REG_CLIP_FLAG].UL == expected_history &&
				shared.regs.VI[REG_CLIP_FLAG].UL == expected_history,
				"VU CLIP 24-bit delayed history matches independent signed-word model");
			Check(shared.regs.VI[1].UL == expected_vi1 &&
				legacy.regs.VI[1].UL == expected_vi1 && interp.regs.VI[1].UL == expected_vi1,
				"VU CLIP consumer result is preserved with VI1 zero-extension");
			Check(shared.regs.cycle == legacy.regs.cycle && !legacy.fallback && !shared.fallback,
				"VU CLIP shared route keeps legacy cycles and stays on native VU dispatcher");
			Check(after_shared[core] == before[core] + expected_clip_count &&
				after_shared[core ^ 1u] == before[core ^ 1u],
				"VU CLIP counts successful routed operations only on the selected core");
		};

		// Every test begins with a nonzero clip history established by FCSET.
		// The corpus covers signed zeros, subnormals, normal boundaries, finite
		// extremes, infinities, quiet/signaling NaNs and strict equality.
		for (u32 base = 0; base < thresholds.size(); base += 4)
		{
			VfWords input = initial, expected = initial;
			std::vector<u32> program;
			constexpr u32 history_seed = 0x00a1b2c3u;
			append_pair(program, fcset(history_seed), NOP_U);
			u32 history = history_seed;
			for (u32 item = base; item < base + 4; ++item)
			{
				const u32 fs = 1 + item, ft = 16 + item;
				for (u32 lane = 0; lane < 3; ++lane)
					input[fs * 4 + lane] = coordinates[(item + lane * 5) % coordinates.size()];
				input[ft * 4 + 3] = thresholds[item];
				const std::array<u32, 4> fs_words = {input[fs * 4], input[fs * 4 + 1],
					input[fs * 4 + 2], input[fs * 4 + 3]};
				history = VuClipHistoryExpected(history, fs_words, input[ft * 4 + 3]);
				append_pair(program, NOP_L, clip(fs, ft));
			}
			expected = input;
			finish(program);
			run_case(program, input, expected, history, 4, "raw threshold/coordinate corpus");
		}

		// Equality is strict: a coordinate equal to +threshold or -threshold
		// contributes no corresponding bit. Fs==Ft also proves the threshold W
		// lane is read alongside the same register's XYZ lanes.
		for (u32 threshold : {0x00000000u, 0x80000000u, 0x3f800000u, 0xbf800000u,
			0x00800000u, 0x7f800000u})
		{
			VfWords input = initial, expected = initial;
			const u32 reg = 9;
			input[reg * 4] = threshold;
			input[reg * 4 + 1] = threshold ^ 0x80000000u;
			input[reg * 4 + 2] = 0x7f800000u;
			input[reg * 4 + 3] = threshold;
			expected = input;
			const std::array<u32, 4> fs_words = {input[reg * 4], input[reg * 4 + 1],
				input[reg * 4 + 2], input[reg * 4 + 3]};
			constexpr u32 history_seed = 0x0055aa33u;
			const u32 history = VuClipHistoryExpected(history_seed, fs_words, threshold);
			std::vector<u32> program;
			append_pair(program, fcset(history_seed), NOP_U);
			append_pair(program, NOP_L, clip(reg, reg));
			finish(program);
			run_case(program, input, expected, history, 1, "Fs equals Ft strict-boundary corpus");
		}

		// VF0 sources and threshold are architecturally fixed; CLIP must still
		// execute and append its six bits without writing any VF state.
		{
			VfWords input = initial, expected = initial;
			constexpr u32 history_seed = 0x00f00d12u;
			const std::array<u32, 4> fs_words = {0, 0, 0, 0};
			const u32 history = VuClipHistoryExpected(history_seed, fs_words, 0x3f800000u);
			std::vector<u32> program;
			append_pair(program, fcset(history_seed), NOP_U);
			append_pair(program, NOP_L, clip(0, 0));
			finish(program);
			run_case(program, input, expected, history, 1, "VF0 fixed-source and fixed-threshold behavior");
		}

		// Lower MOVE and upper CLIP in the same pair exercise the upper read
		// before lower writeback contract for both Fs and Ft hazards.
		for (bool overwrite_fs : {false, true})
		{
			VfWords input = initial, expected = initial;
			constexpr u32 history_seed = 0x000cafe1u;
			const u32 fs = 7, ft = 8, source = overwrite_fs ? 10u : 11u;
			for (u32 lane = 0; lane < 4; ++lane)
				input[source * 4 + lane] = 0x3f000000u + lane * 0x01000000u;
			expected = input;
			const std::array<u32, 4> clip_fs = {
				input[fs * 4], input[fs * 4 + 1], input[fs * 4 + 2], input[fs * 4 + 3]};
			const u32 clip_threshold = input[ft * 4 + 3];
			const u32 history = VuClipHistoryExpected(history_seed, clip_fs, clip_threshold);
			for (u32 lane = 0; lane < 4; ++lane)
				expected[(overwrite_fs ? fs : ft) * 4 + lane] = input[source * 4 + lane];
			std::vector<u32> program;
			append_pair(program, fcset(history_seed), NOP_U);
			append_pair(program, move(source, overwrite_fs ? fs : ft), clip(fs, ft));
			finish(program);
			run_case(program, input, expected, history, 1,
				overwrite_fs ? "same-pair lower Fs WAW uses old upper source" :
				"same-pair lower Ft WAW uses old upper threshold");
		}

		// CLIP in a branch delay pair must retain its branch metadata and update
		// the scheduled history on both taken and untaken control-flow paths.
		for (bool taken : {false, true})
		{
			VfWords input = initial;
			std::array<u32, 16> branch_vi = initial_vi;
			branch_vi[1] = 9u;
			branch_vi[2] = taken ? 9u : 10u;
			constexpr u32 history_seed = 0x00912345u;
			const std::array<u32, 4> fs_words = {input[7 * 4], input[7 * 4 + 1],
				input[7 * 4 + 2], input[7 * 4 + 3]};
			const u32 history = VuClipHistoryExpected(history_seed, fs_words, input[8 * 4 + 3]);
			const u32 branch = (0x28u << 25) | (1u << 11) | (2u << 16) | 2u; // IBEQ to E-bit pair
			const u32 fallthrough_marker = (8u << 25) | (12u << 16) | 0x55u; // IADDIU vi12,vi0,0x55
			std::vector<u32> program;
			append_pair(program, fcset(history_seed), NOP_U);
			append_pair(program, branch, NOP_U);
			append_pair(program, NOP_L, clip(7, 8));
			append_pair(program, fallthrough_marker, NOP_U);
			append_pair(program, NOP_L, NOP_U | 0x40000000u);
			append_pair(program, NOP_L, NOP_U);
			EmuCoreXOracleSetVUIR(core, 0);
			const auto before = VuClipCounts();
			const Snapshot interp = RunVU(core, false, program, 0x80, 0, 256, false,
				nullptr, input.data(), initial_acc.data(), branch_vi.data());
			const Snapshot legacy = RunVU(core, true, program, 0x80, 0, 256, false,
				nullptr, input.data(), initial_acc.data(), branch_vi.data());
			Check(VuClipCounts() == before, "branch-delay interpreter/legacy CLIP leaves shared counter unchanged");
			EmuCoreXOracleSetVUIR(core, 1);
			const Snapshot shared = RunVU(core, true, program, 0x80, 0, 256, false,
				nullptr, input.data(), initial_acc.data(), branch_vi.data());
			Compare(interp, legacy, "branch-delay CLIP interpreter/legacy state");
			Compare(legacy, shared, "branch-delay CLIP shared/legacy state");
			Check(interp.regs.VI[REG_CLIP_FLAG].UL == history &&
				legacy.regs.VI[REG_CLIP_FLAG].UL == history &&
				shared.regs.VI[REG_CLIP_FLAG].UL == history &&
				shared.regs.cycle == legacy.regs.cycle && !legacy.fallback && !shared.fallback,
				"branch-delay CLIP updates expected history with exact legacy cycles on both paths");
			const u32 expected_marker = taken ? branch_vi[12] : 0x55u;
			Check(interp.regs.VI[12].UL == expected_marker &&
				legacy.regs.VI[12].UL == expected_marker && shared.regs.VI[12].UL == expected_marker,
				"IBEQ condition selects target versus fallthrough marker around the CLIP delay pair");
			const auto after = VuClipCounts();
			Check(after[core] == before[core] + 1 && after[core ^ 1u] == before[core ^ 1u],
				"branch-delay CLIP increments exactly the selected core successful counter");
		}

		// FCGET is a real downstream clip-flag consumer. NOP pairs let the
		// architectural C-flag ring mature before reading its low twelve bits.
		{
			VfWords input = initial, expected = initial;
			constexpr u32 history_seed = 0x00654321u;
			const std::array<u32, 4> fs_words = {input[7 * 4], input[7 * 4 + 1],
				input[7 * 4 + 2], input[7 * 4 + 3]};
			const u32 history = VuClipHistoryExpected(history_seed, fs_words, input[8 * 4 + 3]);
			std::vector<u32> program;
			append_pair(program, fcset(history_seed), NOP_U);
			append_pair(program, NOP_L, clip(7, 8));
			for (u32 i = 0; i < 6; ++i) append_pair(program, NOP_L, NOP_U);
			append_pair(program, fcget(5), NOP_U);
			finish(program);
			std::array<u32, 16> consumer_vi = initial_vi;
			consumer_vi[5] = 0xa5a5u;
			// run_case uses the standard VI seed; run this consumer explicitly to
			// keep the expected independent full-VI state visible below.
			EmuCoreXOracleSetVUIR(core, 0);
			const auto before = VuClipCounts();
			const Snapshot interp = RunVU(core, false, program, 0x80, 0, 256, false,
				nullptr, input.data(), initial_acc.data(), consumer_vi.data());
			const Snapshot legacy = RunVU(core, true, program, 0x80, 0, 256, false,
				nullptr, input.data(), initial_acc.data(), consumer_vi.data());
			Check(VuClipCounts() == before, "FCGET legacy route leaves shared CLIP counter unchanged");
			EmuCoreXOracleSetVUIR(core, 1);
			const Snapshot shared = RunVU(core, true, program, 0x80, 0, 256, false,
				nullptr, input.data(), initial_acc.data(), consumer_vi.data());
			Compare(interp, legacy, "FCGET CLIP consumer interpreter/legacy full state");
			Compare(legacy, shared, "FCGET CLIP consumer shared/legacy full state");
			Check(interp.regs.VI[REG_CLIP_FLAG].UL == history &&
				legacy.regs.VI[REG_CLIP_FLAG].UL == history &&
				shared.regs.VI[REG_CLIP_FLAG].UL == history &&
				interp.regs.VI[5].UL == (history & 0xfffu) &&
				legacy.regs.VI[5].UL == (history & 0xfffu) &&
				shared.regs.VI[5].UL == (history & 0xfffu),
				"FCGET observes matured 12-bit clip history and preserves full shared result");
			const auto after = VuClipCounts();
			Check(after[core] == before[core] + 1 && after[core ^ 1u] == before[core ^ 1u],
				"FCGET case counts only its successful shared CLIP operation");
		}

		// End/resume starts a second execution at another micro address without
		// resetting architectural state. FCSET seeds real nonzero history before
		// the first E-bit; the resumed CLIP must append to it, not start at zero.
		{
			VfWords input = initial;
			constexpr u32 history_seed = 0x0040cafeu;
			const std::array<u32, 4> fs_words = {input[7 * 4], input[7 * 4 + 1],
				input[7 * 4 + 2], input[7 * 4 + 3]};
			const u32 history = VuClipHistoryExpected(history_seed, fs_words, input[8 * 4 + 3]);
			std::vector<u32> prefix;
			append_pair(prefix, fcset(history_seed), NOP_U);
			finish(prefix);
			std::vector<u32> suffix;
			append_pair(suffix, NOP_L, clip(7, 8));
			finish(suffix);
			std::array<Snapshot, 3> results{};
			for (u32 route = 0; route < results.size(); ++route)
			{
				EmuCoreXOracleSetVUIR(core, route == 2 ? 1 : 0);
				const auto before = VuClipCounts();
				const Snapshot first = RunVU(core, route != 0, prefix, 0x80, 0, 256, false,
					nullptr, input.data(), initial_acc.data(), initial_vi.data());
				Check(first.regs.VI[REG_CLIP_FLAG].UL == history_seed,
					"FCSET prefix drains its nonzero clip history at E-bit");
				results[route] = RunVU(core, route != 0, suffix, 0x100, 0, 256, true);
				Check(results[route].regs.VI[REG_CLIP_FLAG].UL == history,
					"resumed CLIP appends to prior architectural clip history");
				const auto after = VuClipCounts();
				Check(after[core] == before[core] + (route == 2 ? 1u : 0u) &&
					after[core ^ 1u] == before[core ^ 1u],
					"resumed execution only counts its successfully routed CLIP");
			}
			Compare(results[0], results[1], "resumed CLIP interpreter/legacy full state");
			Compare(results[1], results[2], "resumed CLIP shared/legacy full state");
			Check(results[1].regs.cycle == results[2].regs.cycle,
				"resumed CLIP preserves exact legacy cycles");
		}

		EmuCoreXOracleSetVUIR(core, -1);
	}
	EmuConfig.Cpu.VU0FPCR = saved_vu0_fpcr;
	EmuConfig.Cpu.VU1FPCR = saved_vu1_fpcr;
	EmuConfig.Cpu.FPUFPCR = saved_ee_fpcr;
}
