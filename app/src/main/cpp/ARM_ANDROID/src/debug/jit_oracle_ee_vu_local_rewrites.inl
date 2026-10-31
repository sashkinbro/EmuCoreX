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
        const EESnapshot interp = RunEEProgram(false, code, false, nullptr, nullptr,
            initial_gpr.data(), nullptr, nullptr, initial_vf.data());
        bool expected_jit = true, expected_interp = true, expected_acc = true;
        u32 expected_mac = 0;
        for (u32 lane = 0; lane < 4; ++lane)
        {
            const float s = static_cast<float>(lane + 1);
            const float t = alias == 1 ? s : static_cast<float>(lane + 5);
            const float a_t = shape == 0 ? t : (shape >= 5 ? 2.0f :
                static_cast<float>((shape - 1) + (alias == 1 ? 1 : 5)));
            const float acc = operation == 0 ? s + a_t : (operation == 1 ? s - a_t : s * a_t);
            const u32 expected = bits(acc + s * t);
            if (!(expected & 0x7fffffffu)) expected_mac |= 1u << (3 - lane);
            if (expected & 0x80000000u) expected_mac |= 1u << (7 - lane);
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
        auto policy_reference = interp;
        // The speedhack intentionally drops unobserved intermediate sticky
        // history. Its final flags still follow the independently modeled MADD.
        if (flag_hack)
        {
            policy_reference.vu0_vi[REG_STATUS_FLAG] = ((expected_mac & 0xf) ? 0x41u : 0u) |
                ((expected_mac & 0xf0) ? 0x82u : 0u);
            policy_reference.gpr[19] = policy_reference.vu0_vi[REG_STATUS_FLAG]; // CFC2 observer.
        }
        CompareEE(policy_reference, jit, "EE full ACC macro chain preserves architectural state and explicit flag policy");
        Check(expected_interp, "EE full ACC macro chain interpreter matches independent finite bits");
        Check(expected_jit, "EE full ACC macro chain retains result across cached consumer and spill");
        Check(expected_acc, "EE full ACC macro chain writes architectural ACC and preserves it after MADD");
        Check(jit.gpr[19] == policy_reference.vu0_vi[REG_STATUS_FLAG] &&
            jit.vu0_vi[REG_MAC_FLAG] == expected_mac && jit.vu0_vi[REG_CLIP_FLAG] == 0 &&
            jit.vu0_vi[REG_STATUS_FLAG] == policy_reference.vu0_vi[REG_STATUS_FLAG],
            "EE full ACC macro chain matches independent MAC bits and architectural STATUS CLIP policy");
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


void EEVuLocalStatusProbeTests()
{
    const auto saved = EmuConfig;
    const auto bits = [](float value) { u32 out; std::memcpy(&out, &value, sizeof(out)); return out; };
    const auto qmtc2 = [](u32 rt, u32 vf) { return (0x12u << 26) | (5u << 21) | (rt << 16) | (vf << 11); };
    const auto cfc2 = [](u32 rt, u32 vi) { return (0x12u << 26) | (2u << 21) | (rt << 16) | (vi << 11); };
    const auto upper = [](u32 op, u32 fd = 0) {
        return (0x12u << 26) | (0x1fu << 21) | (9u << 16) | (7u << 11) | (fd << 6) | op;
    };
    std::array<GPR_reg, 32> initial_gpr{};
    std::array<u32, 128> initial_vf{};
    initial_vf[3] = bits(1.0f);
    for (u32 lane = 0; lane < 4; ++lane)
    {
        initial_gpr[8].UL[lane] = bits(static_cast<float>(lane + 1));
        initial_gpr[10].UL[lane] = bits(static_cast<float>(lane + 5));
    }
    EmuConfig.Cpu.Recompiler.vu0Overflow = true;
    EmuConfig.Cpu.Recompiler.vu0ExtraOverflow = false;
    EmuConfig.Cpu.Recompiler.vu0SignOverflow = false;
    for (bool hack : {false, true})
    for (bool observe : {false, true})
    for (bool broadcast : {false, true})
    for (bool transfer : {false, true})
    {
        EmuConfig.Speedhacks.vuFlagHack = hack;
        EmuCoreXOracleSetVUIR(0, 0);
        std::vector<u32> code{qmtc2(8, 7), qmtc2(10, 9), upper(broadcast ? 0x7d : 0x2fc)};
        if (observe) code.push_back(cfc2(19, 16));
        code.push_back(upper(0x29, 3));
        if (transfer) code.push_back((0x12u << 26) | (1u << 21) | (18u << 16) | (3u << 11));
        code.insert(code.end(), {cfc2(20, 16), cfc2(21, 17)});
        const auto jit = RunEEProgramWithIrMode(0, code, initial_gpr.data(), nullptr,
            nullptr, nullptr, false, initial_vf.data());
        const auto interp = RunEEProgram(false, code, false, nullptr, nullptr,
            initial_gpr.data(), nullptr, nullptr, initial_vf.data());
        const u32 final = broadcast ? 0xc1 : 0x80;
        const u32 policy = hack && !observe ? (final & ~0x80u) : final;
        std::printf("STATUS PROBE hack=%u observe=%u broadcast=%u transfer=%u first=%llx/%llx final=%llx/%llx mac=%llx/%llx expected=%x\n",
            static_cast<u32>(hack), static_cast<u32>(observe), static_cast<u32>(broadcast), static_cast<u32>(transfer),
            static_cast<unsigned long long>(interp.gpr[19]), static_cast<unsigned long long>(jit.gpr[19]),
            static_cast<unsigned long long>(interp.gpr[20]), static_cast<unsigned long long>(jit.gpr[20]),
            static_cast<unsigned long long>(interp.gpr[21]), static_cast<unsigned long long>(jit.gpr[21]), policy);
        Check(interp.gpr[20] == final && (!observe || interp.gpr[19] == 0x82),
            "STATUS chain interpreter retains prior sign sticky and final zero aggregate");
        Check(jit.gpr[20] == policy && (!observe || jit.gpr[19] == 0x82),
            "STATUS chain JIT retains required sticky bits under explicit flag-hack policy");
    }
    EmuConfig = saved;
}

#include "jit_oracle_ee_vu_local_clip.inl"

#include "jit_oracle_ee_vu_local_identity.inl"

#include "jit_oracle_ee_vu_zero_tail.inl"

#include "jit_oracle_ee_vu_lower_zero_tail.inl"
