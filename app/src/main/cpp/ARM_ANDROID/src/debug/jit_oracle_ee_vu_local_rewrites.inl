// SPDX-FileCopyrightText: 2026 EmuCoreX Team
// SPDX-License-Identifier: GPL-3.0+
// Compose full-width ACC writers with an EE cached ACC consumer and eviction.
void EEVuLocalOpDirtyAliasTests();
void EEVuLocalRewriteMacroTests()
{
    EEVuLocalOpDirtyAliasTests();
    const auto saved_cpu = EmuConfig.Cpu;
    const bool saved_flag_hack = EmuConfig.Speedhacks.vuFlagHack;
    EmuConfig.Cpu.FPUFPCR.SetRoundMode(FPRoundMode::Nearest);
    EmuConfig.Cpu.VU0FPCR.SetRoundMode(FPRoundMode::Nearest);
    const FPControlRegisterBackup host_fpcr(EmuConfig.Cpu.FPUFPCR);
    const auto bits = [](float value) { u32 out; std::memcpy(&out, &value, sizeof(out)); return out; };
    const auto qmtc2 = [](u32 rt, u32 vf) { return (0x12u << 26) | (5u << 21) | (rt << 16) | (vf << 11); };
    const auto qmfc2 = [](u32 rt, u32 vf) { return (0x12u << 26) | (1u << 21) | (rt << 16) | (vf << 11); };
    const auto ctc2 = [](u32 rt, u32 vi) { return (0x12u << 26) | (6u << 21) | (rt << 16) | (vi << 11); };
    const auto cfc2 = [](u32 rt, u32 vi) { return (0x12u << 26) | (2u << 21) | (rt << 16) | (vi << 11); };
    const auto upper = [](u32 opcode, u32 fs, u32 ft, u32 fd = 0) {
        return (0x12u << 26) | (0x1fu << 21) | (ft << 16) | (fs << 11) | (fd << 6) | opcode;
    };
    std::array<u32, 128> initial_vf{};
    initial_vf[3] = bits(1.0f);
    std::array<GPR_reg, 32> initial_gpr{};
    for (u32 lane = 0; lane < 4; ++lane)
    {
        initial_gpr[8].UL[lane] = bits(static_cast<float>(lane + 1));
        initial_gpr[10].UL[lane] = bits(static_cast<float>(lane + 5));
    }
    initial_gpr[12].UL[0] = bits(2.0f);
    // No macro-to-memory boundary between the A writer and the MADD reader.
    // QMTC2 pressure additionally checks dirty ACC survives an EE cache spill.
    for (u32 operation = 0; operation < 3; ++operation)
    for (u32 shape = 0; shape < 7; ++shape)
    for (bool prior_acc : {false, true})
    for (u32 alias = 0; alias < 3; ++alias)
    for (bool pressure : {false, true})
    for (u32 clamp = 0; clamp < 4; ++clamp)
    for (bool flag_hack : {false, true})
    {
        EmuConfig.Cpu.Recompiler.vu0Overflow = clamp >= 1;
        EmuConfig.Cpu.Recompiler.vu0ExtraOverflow = clamp >= 2;
        EmuConfig.Cpu.Recompiler.vu0SignOverflow = clamp >= 3;
        EmuConfig.Speedhacks.vuFlagHack = flag_hack;
        const u32 ft = alias == 1 ? 7 : 9;
        const u32 fd = alias == 0 ? 3 : (alias == 1 ? 7 : 9);
        constexpr u32 a_opcode[3][7] = {
            {0x2bc, 0x3c, 0x3d, 0x3e, 0x3f, 0x23e, 0x23c},
            {0x2fc, 0x7c, 0x7d, 0x7e, 0x7f, 0x27e, 0x27c},
            {0x2be, 0x1bc, 0x1bd, 0x1be, 0x1bf, 0x1fe, 0x1fc}};
        std::vector<u32> code;
        if (shape >= 5) code.push_back(ctc2(12, shape == 5 ? 21 : 22));
        code.insert(code.end(), {qmtc2(8, 7), qmtc2(10, 9)});
        if (prior_acc) code.push_back(upper(0x2be, 9, 9));
        code.push_back(upper(a_opcode[operation][shape], 7, ft));
        if (pressure)
            for (u32 vf = 10; vf < 32; ++vf) code.push_back(qmtc2(8, vf));
        code.push_back(upper(0x29, 7, ft, fd)); // MADD fd = ACC + Fs * Ft
        code.push_back(qmfc2(18, fd));
        // Observe flags so flag-hack mode must retain their final value too.
        code.push_back(cfc2(19, 16));
        code.push_back(cfc2(20, 17));
        EmuCoreXOracleSetVUIR(0, 0);
        const EESnapshot jit = RunEEProgramWithIrMode(0, code, initial_gpr.data(), nullptr,
            nullptr, nullptr, false, initial_vf.data());
        const VECTOR jit_acc = VU0.ACC;
        const std::array<u32, 3> jit_flags{VU0.macflag, VU0.statusflag, VU0.clipflag};
        const EESnapshot interp = RunEEProgram(false, code, false, nullptr, nullptr,
            initial_gpr.data(), nullptr, nullptr, initial_vf.data());
        bool expected_jit = true, expected_interp = true, expected_acc = true;
        for (u32 lane = 0; lane < 4; ++lane)
        {
            const float s = static_cast<float>(lane + 1);
            const float t = alias == 1 ? s : static_cast<float>(lane + 5);
            const float a_t = shape == 0 ? t : (shape >= 5 ? 2.0f :
                static_cast<float>((shape - 1) + (alias == 1 ? 1 : 5)));
            const float acc = operation == 0 ? s + a_t : (operation == 1 ? s - a_t : s * a_t);
            const u32 expected = bits(acc + s * t);
            expected_jit &= jit.vu0_vf[fd * 4 + lane] == expected;
            const u32 jit_gpr = lane < 2 ? static_cast<u32>(jit.gpr[18] >> (lane * 32)) :
                static_cast<u32>(jit.gpr_upper[18] >> ((lane - 2) * 32));
            expected_jit &= jit_gpr == expected;
            expected_interp &= interp.vu0_vf[fd * 4 + lane] == expected;
            expected_acc &= jit_acc.UL[lane] == bits(acc) && VU0.ACC.UL[lane] == bits(acc);
        }
        if (!expected_jit || !expected_acc)
            std::printf("MACRO ACC CASE operation=%u shape=%u prior=%u alias=%u pressure=%u clamp=%u flags=%u\n",
                operation, shape, static_cast<u32>(prior_acc), alias, static_cast<u32>(pressure), clamp,
                static_cast<u32>(flag_hack));
        CompareEE(interp, jit, "EE full ACC macro chain preserves all architectural registers");
        Check(expected_interp, "EE full ACC macro chain interpreter matches independent finite bits");
        Check(expected_jit, "EE full ACC macro chain retains result across cached consumer and spill");
        Check(expected_acc, "EE full ACC macro chain writes architectural ACC and preserves it after MADD");
        Check(jit_flags == std::array<u32, 3>{VU0.macflag, VU0.statusflag, VU0.clipflag},
            "EE full ACC macro chain preserves MAC STATUS and CLIP flags");
    }
    EmuConfig.Cpu = saved_cpu;
    EmuConfig.Speedhacks.vuFlagHack = saved_flag_hack;
}

// The same aliased OP result must survive the EE COP2 cache handoff too.
void EEVuLocalOpDirtyAliasTests()
{
    const auto saved = EmuConfig;
    const auto bits = [](float value) { u32 out; std::memcpy(&out, &value, sizeof(out)); return out; };
    const auto qmtc2 = [](u32 rt, u32 vf) { return (0x12u << 26) | (5u << 21) | (rt << 16) | (vf << 11); };
    const auto qmfc2 = [](u32 rt, u32 vf) { return (0x12u << 26) | (1u << 21) | (rt << 16) | (vf << 11); };
    const auto cfc2 = [](u32 rt, u32 vi) { return (0x12u << 26) | (2u << 21) | (rt << 16) | (vi << 11); };
    const auto upper = [](u32 op, u32 fs, u32 ft, u32 fd = 0) {
        return (0x12u << 26) | (0x1fu << 21) | (ft << 16) | (fs << 11) | (fd << 6) | op;
    };
    std::array<GPR_reg, 32> initial_gpr{};
    std::array<u32, 128> initial_vf{};
    initial_vf[3] = bits(1.0f);
    for (u32 lane = 0; lane < 4; ++lane)
    {
        initial_gpr[8].UL[lane] = bits(static_cast<float>(lane + 1));
        initial_gpr[10].UL[lane] = bits(static_cast<float>(lane + 5));
        initial_gpr[12].UL[lane] = bits(static_cast<float>(lane + 101));
    }
    for (u32 clamp = 0; clamp < 4; ++clamp)
    for (bool flag_hack : {false, true})
    for (u32 dirty_source : {7u, 9u})
    for (u32 fd : {7u, 9u, 3u})
    {
        auto& rc = EmuConfig.Cpu.Recompiler;
        rc.vu0Overflow = clamp >= 1; rc.vu0ExtraOverflow = clamp >= 2; rc.vu0SignOverflow = clamp >= 3;
        EmuConfig.Speedhacks.vuFlagHack = flag_hack;
        EmuCoreXOracleSetVUIR(0, 0);
        const std::vector<u32> code = {qmtc2(8, 7), qmtc2(10, 9), qmtc2(12, 11),
            upper(0x2bc, 11, 0), upper(0x28, 7, 9, dirty_source), upper(0x2e, 7, 9, fd),
            qmfc2(18, fd), cfc2(19, 16), cfc2(20, 17)};
        const EESnapshot jit = RunEEProgramWithIrMode(0, code, initial_gpr.data(), nullptr,
            nullptr, nullptr, false, initial_vf.data());
        const EESnapshot interp = RunEEProgram(false, code, false, nullptr, nullptr,
            initial_gpr.data(), nullptr, nullptr, initial_vf.data());
        bool expected = true;
        for (u32 lane = 0; lane < 4; ++lane)
        {
            const float s = dirty_source == 7 ? static_cast<float>(6 + 2 * ((lane + 1) % 3)) :
                static_cast<float>(1 + ((lane + 1) % 3));
            const float t = dirty_source == 9 ? static_cast<float>(6 + 2 * ((lane + 2) % 3)) :
                static_cast<float>(5 + ((lane + 2) % 3));
            const float w = fd == 3 ? 0.0f : fd == dirty_source ? 12.0f : fd == 7 ? 4.0f : 8.0f;
            const u32 value = bits(lane < 3 ? static_cast<float>(101 + lane) - s * t : w);
            const u32 jit_gpr = lane < 2 ? static_cast<u32>(jit.gpr[18] >> (lane * 32)) :
                static_cast<u32>(jit.gpr_upper[18] >> ((lane - 2) * 32));
            expected &= jit.vu0_vf[fd * 4 + lane] == value && interp.vu0_vf[fd * 4 + lane] == value && jit_gpr == value;
        }
        if (!expected) std::printf("MACRO OP DIRTY clamp=%u flags=%u source=%u fd=%u\n",
            clamp, static_cast<u32>(flag_hack), dirty_source, fd);
        Check(expected, "EE OPMSUB dirty cached aliases preserve independent result bits W and QMFC2 output");
        CompareEE(interp, jit, "EE OPMSUB dirty cached aliases preserve all architectural registers");
    }
    EmuConfig = saved;
}
