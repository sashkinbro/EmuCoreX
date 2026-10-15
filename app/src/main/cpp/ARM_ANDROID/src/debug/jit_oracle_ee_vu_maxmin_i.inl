// SPDX-FileCopyrightText: 2026 EmuCoreX Team
// SPDX-License-Identifier: GPL-3.0+
// EE COP2 MAXi/MINIi integration fixture. Include after
// jit_oracle_ee_vu_maxmin.inl and the EE run helpers.
// Entry: EEVuMaxMinIMacroTests(). No production implementation in this file.

extern "C" u64 EmuCoreXOracleVUIRMaxMinIInstructions(u32 vu);
extern "C" u64 EmuCoreXOracleVUIRMaxMinInstructions(u32 vu);
extern "C" u64 EmuCoreXOracleVUIRMaxMinBroadcastInstructions(u32 vu);
extern "C" bool EmuCoreXOracleGetEeCompiledBlock(u32 pc, const void** host, u32* bytes);

static u64 s_ee_vu_maxmin_i_hot_compiles = 0;
static void EEVuMaxMinICompileProbe(u32 pc)
{
    if (pc == EE_TEST_PC)
        ++s_ee_vu_maxmin_i_hot_compiles;
}

void EEVuMaxMinIMacroTests()
{
    const FPControlRegister original_ee_fpcr = EmuConfig.Cpu.FPUFPCR;
    const FPControlRegister original_vu0_fpcr = EmuConfig.Cpu.VU0FPCR;
    EmuConfig.Cpu.FPUFPCR.SetRoundMode(FPRoundMode::Nearest);
    EmuConfig.Cpu.VU0FPCR.SetRoundMode(FPRoundMode::Nearest);
    const FPControlRegisterBackup host_fpcr(EmuConfig.Cpu.FPUFPCR);
    const auto macro = [](bool maximum, u32 mask, u32 fs, u32 fd) {
        return (0x12u << 26) | ((0x10u | (mask & 15u)) << 21) |
            (fs << 11) | (fd << 6) | (maximum ? 0x1du : 0x1fu);
    };
    const auto ctc_i = [](u32 rt) {
        return (0x12u << 26) | (6u << 21) | (rt << 16) | (REG_I << 11);
    };
    const auto cfc_i = [](u32 rt) {
        return (0x12u << 26) | (2u << 21) | (rt << 16) | (REG_I << 11);
    };
    const auto sign32 = [](u32 raw) -> u64 {
        return (raw & 0x80000000u) ? 0xffffffff00000000ull | raw : raw;
    };
    using VfWords = std::array<u32, 128>;
    constexpr std::array<u32, 24> edges = {
        0, 0x80000000u, 1, 0x80000001u, 0x007fffffu, 0x807fffffu,
        0x00800000u, 0x80800000u, 0x3f800000u, 0xbf800000u,
        0x7f7fffffu, 0xff7fffffu, 0x7f800000u, 0xff800000u,
        0x7f800001u, 0xff800001u, 0x7fa12345u, 0xffa54321u,
        0x7fc12345u, 0xffc54321u, 0x7fffffffu, 0xffffffffu,
        0x40000000u, 0xc0000000u};
    constexpr std::array<std::array<u32, 2>, 4> registers = {{
        {7, 9}, {7, 7}, {0, 9}, {7, 0}}};
    // Path names describe deliberately constructed source instructions; the
    // dedicated route counter proves MAXi/MINIi dispatch, not internal allocator choices.
    constexpr const char* paths[] = {
        "rt0", "constant", "integer-gpr", "cop1-dirty", "simd-dirty-qmfc2", "repeated-i"};

    const auto run_case = [&](bool maximum, u32 mask, u32 fs, u32 fd,
        u32 source_path, u32 raw_i, u32 case_id, bool hot) {
        VfWords input{};
        input[3] = 0x3f800000u;
        for (u32 reg = 1; reg < 32; ++reg)
            for (u32 lane = 0; lane < 4; ++lane)
                input[reg * 4 + lane] = 0x5a13579bu ^ (reg * 0x01020408u) ^ (lane * 0x137abdeu);
        if (fs != 0)
            for (u32 lane = 0; lane < 4; ++lane)
                input[fs * 4 + lane] = edges[(case_id + lane * 7) % edges.size()];
        // QMFC2 reads a source independent of all tested Fs/Fd aliases.
        input[10 * 4] = raw_i;
        std::array<GPR_reg, 32> initial_gpr{};
        for (u32 reg = 1; reg < 32; ++reg)
        {
            initial_gpr[reg].UD[0] = 0x1234567800000000ull | (0x76543210u + reg);
            initial_gpr[reg].UD[1] = 0xabcdef0100000000ull | (0x89abcdefu + reg);
        }
        initial_gpr[10].UL[0] = raw_i;
        initial_gpr[11].UL[0] = raw_i ^ 0x80000000u;
        std::array<u32, 32> initial_fpr{}, initial_fcr{}, initial_cp0{};
        initial_cp0[12] = 0x20000000u; // CU1 for the COP1 transfer path.
        initial_fcr[0] = 0x2e00u;
        for (u32 reg = 1; reg < 32; ++reg)
        {
            initial_fpr[reg] = 0x51000000u ^ (reg * 0x01020408u);
            initial_fcr[reg] = 0x87654321u ^ (reg * 0x01020304u);
        }
        auto expected_gpr = initial_gpr;
        auto expected_fpr = initial_fpr;
        VfWords expected_vf = input;
        std::vector<u32> code;
        u32 final_i = raw_i;
        switch (source_path)
        {
            case 0:
                final_i = 0;
                code.push_back(ctc_i(0));
                break;
            case 1:
                code.push_back(MipsI(15, 0, 12, raw_i >> 16)); // LUI
                code.push_back(MipsI(13, 12, 12, raw_i & 0xffffu)); // ORI
                expected_gpr[12].UD[0] = sign32(raw_i);
                code.push_back(ctc_i(12));
                break;
            case 2:
                code.push_back(MipsI(9, 10, 12, 0)); // ADDIU from nonconstant initial GPR.
                expected_gpr[12].UD[0] = sign32(raw_i);
                code.push_back(ctc_i(12));
                break;
            case 3:
                code.push_back((0x11u << 26) | (4u << 21) | (10u << 16) | (6u << 11)); // MTC1
                code.push_back((0x11u << 26) | (12u << 16) | (6u << 11)); // MFC1
                expected_fpr[6] = raw_i;
                expected_gpr[12].UD[0] = sign32(raw_i);
                code.push_back(ctc_i(12));
                break;
            case 4:
                code.push_back((0x12u << 26) | (1u << 21) | (12u << 16) | (10u << 11)); // QMFC2
                for (u32 lane = 0; lane < 4; ++lane)
                    expected_gpr[12].UL[lane] = input[10 * 4 + lane];
                code.push_back(ctc_i(12));
                break;
            default:
                code.push_back(ctc_i(10));
                break;
        }
        const auto append_macro = [&](bool op, u32 i_word) {
            const VfWords before = expected_vf;
            code.push_back(macro(op, mask, fs, fd));
            if (fd != 0)
                for (u32 lane = 0; lane < 4; ++lane)
                    if (mask & (8u >> lane))
                        expected_vf[fd * 4 + lane] = EEVuMaxMinExpectedBits(before[fs * 4 + lane], i_word, op);
        };
        append_macro(maximum, final_i);
        code.push_back(cfc_i(24));
        expected_gpr[24].UD[0] = sign32(final_i);
        const u32 macro_count = source_path == 5 ? 2u : 1u;
        if (source_path == 5)
        {
            final_i = raw_i ^ 0x80000000u;
            code.push_back(ctc_i(11));
            append_macro(!maximum, final_i);
            code.push_back(cfc_i(25));
            expected_gpr[25].UD[0] = sign32(final_i);
        }
        const u64 expected_routes = fd != 0 && mask != 0 ? macro_count : 0;
        const auto counters = [] {
            return std::array<u64, 6>{
                EmuCoreXOracleVUIRMaxMinIInstructions(0), EmuCoreXOracleVUIRMaxMinIInstructions(1),
                EmuCoreXOracleVUIRMaxMinInstructions(0), EmuCoreXOracleVUIRMaxMinInstructions(1),
                EmuCoreXOracleVUIRMaxMinBroadcastInstructions(0), EmuCoreXOracleVUIRMaxMinBroadcastInstructions(1)};
        };
        const auto flags = [] {
            return std::array<u32, 3>{VU0.macflag, VU0.statusflag, VU0.clipflag};
        };
        const auto independent = [&](const EESnapshot& snap) {
            bool exact = std::memcmp(snap.vu0_vf, expected_vf.data(), sizeof(snap.vu0_vf)) == 0 &&
                std::memcmp(snap.fpr, expected_fpr.data(), sizeof(snap.fpr)) == 0 &&
                std::memcmp(snap.fcr, initial_fcr.data(), sizeof(snap.fcr)) == 0 &&
                snap.vu0_vi[REG_I] == final_i;
            for (u32 reg = 0; reg < 32; ++reg)
                exact &= snap.gpr[reg] == expected_gpr[reg].UD[0] &&
                    snap.gpr_upper[reg] == expected_gpr[reg].UD[1] &&
                    (reg == REG_I || snap.vu0_vi[reg] == 0);
            for (u32 lane = 0; lane < 4; ++lane)
                exact &= snap.vu0_acc[lane] == 0;
            return exact;
        };
        char name[160];
        std::snprintf(name, sizeof(name), "EE MAXi/MINIi %s max=%u mask=%u fs=%u fd=%u case=%u",
            paths[source_path], maximum ? 1u : 0u, mask, fs, fd, case_id);
        EmuCoreXOracleSetEeIrMode(0);
        EmuCoreXOracleSetVUIR(0, 0);
        const auto before = counters();
        const EESnapshot legacy = RunEEProgramWithIrMode(0, code, initial_gpr.data(), initial_cp0.data(),
            initial_fpr.data(), initial_fcr.data(), false, input.data());
        const auto legacy_flags = flags();
        Check(counters() == before, "EE MAXi/MINIi forced legacy leaves all targeted route counters unchanged");
        const EESnapshot interp = RunEEProgram(false, code, false, nullptr, initial_cp0.data(),
            initial_gpr.data(), initial_fpr.data(), initial_fcr.data(), input.data());
        const auto interp_flags = flags();
        EmuCoreXOracleSetVUIR(0, 1);
        const EESnapshot shared = RunEEProgramWithIrMode(0, code, initial_gpr.data(), initial_cp0.data(),
            initial_fpr.data(), initial_fcr.data(), false, input.data());
        const auto shared_flags = flags();
        auto expected_counts = before;
        expected_counts[0] += expected_routes;
        Check(counters() == expected_counts,
            "EE MAXi/MINIi admits only VU0 I route; VU1/register/broadcast counters remain unchanged");
        CompareEE(interp, legacy, name);
        CompareEE(legacy, shared, name);
        Check(legacy.cycle == shared.cycle, "EE MAXi/MINIi exact legacy/shared cycles");
        Check(independent(interp) && independent(legacy) && independent(shared), name);
        Check(interp_flags == legacy_flags && legacy_flags == shared_flags,
            "EE MAXi/MINIi preserves legacy MAC/STATUS/CLIP flags");

        if (hot)
        {
            // Same shared cache remains resident: no code writes or Cpu->Reset.
            const auto replay = [&] {
                std::memset(&cpuRegs, 0, sizeof(cpuRegs));
                std::memset(&fpuRegs, 0, sizeof(fpuRegs));
                std::memcpy(cpuRegs.GPR.r, initial_gpr.data(), sizeof(cpuRegs.GPR.r));
                std::memcpy(cpuRegs.CP0.r, initial_cp0.data(), sizeof(cpuRegs.CP0.r));
                for (u32 reg = 0; reg < 32; ++reg)
                    fpuRegs.fpr[reg].UL = initial_fpr[reg];
                std::memcpy(fpuRegs.fprc, initial_fcr.data(), sizeof(fpuRegs.fprc));
                std::memcpy(&VU0.VF, input.data(), sizeof(VU0.VF));
                std::memset(&VU0.VI, 0, sizeof(VU0.VI));
                std::memset(&VU0.ACC, 0, sizeof(VU0.ACC));
                VU0.q.UL = VU0.p.UL = 0;
                VU0.macflag = VU0.statusflag = VU0.clipflag = 0;
                cpuRegs.pc = EE_TEST_PC;
                cpuRegs.nextEventCycle = 0x7fffffffu;
                EEsCycle = EEoCycle = 0;
                Cpu = &recCpu;
                EmuCoreXEEForceExitAfterFirstBlock();
                Cpu->Execute();
            };
            EmuCoreXOracleSetEeIrMode(0);
            s_ee_vu_maxmin_i_hot_compiles = 0;
            EmuCoreXEEOracleSetCompileCallback(EEVuMaxMinICompileProbe);
            for (u32 repeat = 0; repeat < 8; ++repeat)
                replay();
            const u64 warm_compiles = s_ee_vu_maxmin_i_hot_compiles;
            const auto warm_counts = counters();
            const void* warm_host = nullptr;
            u32 warm_size = 0;
            bool stable = EmuCoreXOracleGetEeCompiledBlock(EE_TEST_PC, &warm_host, &warm_size) && warm_size != 0;
            for (u32 repeat = 0; repeat < 4; ++repeat)
            {
                replay();
                EESnapshot resident{};
                CaptureEE(resident);
                CompareEE(shared, resident, "EE MAXi/MINIi resident replay full-state parity");
                const void* host = nullptr;
                u32 size = 0;
                stable &= independent(resident) && resident.cycle == shared.cycle && flags() == shared_flags &&
                    counters() == warm_counts && s_ee_vu_maxmin_i_hot_compiles == warm_compiles &&
                    EmuCoreXOracleGetEeCompiledBlock(EE_TEST_PC, &host, &size) && host == warm_host && size == warm_size;
            }
            EmuCoreXEEOracleSetCompileCallback(nullptr);
            Check(stable, "EE MAXi/MINIi resident replays preserve state and do not compile target code");
        }
    };

    for (bool maximum : {false, true})
    for (u32 mask = 1; mask < 16; ++mask)
    for (const auto& regs : registers)
    for (u32 path = 0; path < 6; ++path)
        run_case(maximum, mask, regs[0], regs[1], path,
            edges[(mask * 5 + path * 7 + regs[0] + regs[1]) % edges.size()],
            mask * 13 + path * 19 + (maximum ? 101u : 0u), false);
    // Every raw I edge gets every source path, independent of mask sampling.
    for (bool maximum : {false, true})
    for (u32 path = 0; path < 6; ++path)
    for (u32 n = 0; n < edges.size(); ++n)
        run_case(maximum, 15, 7, 9, path, edges[n], n, n == 0);
    for (bool maximum : {false, true})
    for (u32 path = 0; path < 6; ++path)
        run_case(maximum, 0, 7, 9, path, 0xff800001u, 901u + path, false);

    EmuCoreXOracleSetVUIR(0, -1);
    EmuCoreXOracleSetEeIrMode(-1);
    EmuConfig.Cpu.FPUFPCR = original_ee_fpcr;
    EmuConfig.Cpu.VU0FPCR = original_vu0_fpcr;
}
