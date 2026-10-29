// SPDX-FileCopyrightText: 2026 EmuCoreX Team
// SPDX-License-Identifier: GPL-3.0+
// Full-width ACC rebind compositions, including surrounding partial operations.
void VULocalFullAccCompositionTests()
{
    const auto saved_config = EmuConfig;
    const auto bits = [](float value) { u32 out; std::memcpy(&out, &value, sizeof(out)); return out; };
    const auto upper = [](u32 op, u32 fs, u32 ft, u32 fd = 0, u32 mask = 15) {
        return op | (fs << 11) | (ft << 16) | (fd << 6) | (mask << 21);
    };
    const auto move = [](u32 fs, u32 ft, u32 mask = 15) {
        return 0x8000033cu | (fs << 11) | (ft << 16) | (mask << 21);
    };
    std::array<u32, 128> input{};
    input[3] = bits(1.0f);
    for (u32 vf = 1; vf < 32; ++vf)
        for (u32 lane = 0; lane < 4; ++lane)
            input[vf * 4 + lane] = bits(static_cast<float>(lane + (vf == 2 ? 5 : 1)));
    input[120] = bits(4.0f); input[124] = bits(2.0f);
    std::array<u32, 4> initial_acc{bits(101.0f), bits(102.0f), bits(103.0f), bits(104.0f)};
    constexpr u32 opcodes[3][7] = {
        {0x2bc,0x3c,0x3d,0x3e,0x3f,0x23e,0x23c},
        {0x2fc,0x7c,0x7d,0x7e,0x7f,0x27e,0x27c},
        {0x2be,0x1bc,0x1bd,0x1be,0x1bf,0x1fe,0x1fc}};
    for (u32 vu = 0; vu < 2; ++vu)
    for (u32 operation = 0; operation < 3; ++operation)
    for (u32 shape = 0; shape < 7; ++shape)
    for (u32 alias = 0; alias < 3; ++alias)
    for (bool prior_acc : {false, true})
    for (bool pressure : {false, true})
    for (u32 clamp = 0; clamp < 4; ++clamp)
    for (bool flag_hack : {false, true})
    {
        auto& rc = EmuConfig.Cpu.Recompiler;
        if (vu) { rc.vu1Overflow = clamp >= 1; rc.vu1ExtraOverflow = clamp >= 2; rc.vu1SignOverflow = clamp >= 3; }
        else { rc.vu0Overflow = clamp >= 1; rc.vu0ExtraOverflow = clamp >= 2; rc.vu0SignOverflow = clamp >= 3; }
        EmuConfig.Speedhacks.vuFlagHack = flag_hack;
        EmuCoreXOracleSetVUIR(vu, 0);
        const u32 ft = alias == 1 ? 1 : 2;
        const u32 fd = alias == 0 ? 3 : (alias == 1 ? 1 : 2);
        // Establish Q=2 and I=2 before cached ACC operations.
        std::vector<u32> program = {0x800003bcu | (30u << 11) | (31u << 16), NOP_U,
            0x800003bfu, NOP_U, bits(2.0f), NOP_U | 0x80000000u};
        if (prior_acc)
            program.insert(program.end(), {NOP_L, upper(0x2be, 2, 2),
                NOP_L, upper(0x2bc, 1, 2, 0, 5), NOP_L, upper(0x29, 0, 0, 5)});
        program.insert(program.end(), {NOP_L, upper(opcodes[operation][shape], 1, ft)});
        if (pressure)
            for (u32 vf = 10; vf < 30; ++vf)
                program.insert(program.end(), {move(2, vf), NOP_U});
        // Mix a natural-lane arithmetic write and a cached lower lane write
        // between the full ACC writer and its whole-vector consumer.
        program.insert(program.end(), {move(2, 10, 1), upper(0x28, 1, 2, 6, 4),
            NOP_L, upper(0x29, 1, ft, fd)});
        for (u32 n = 0; n < 5; ++n) program.insert(program.end(), {NOP_L, NOP_U});
        program.insert(program.end(), {NOP_L, NOP_U | 0x40000000u, NOP_L, NOP_U});
        const auto actual = RunVU(vu, true, program, 0x80, 0, 512, false, nullptr, input.data(), initial_acc.data());
        const auto reference = RunVU(vu, false, program, 0x80, 0, 512, false, nullptr, input.data(), initial_acc.data());
        bool expected = true;
        for (u32 lane = 0; lane < 4; ++lane)
        {
            const float s = static_cast<float>(lane + 1);
            const float t = alias == 1 ? s : static_cast<float>(lane + 5);
            const float a_t = shape == 0 ? t : (shape >= 5 ? 2.0f :
                static_cast<float>((shape - 1) + (alias == 1 ? 1 : 5)));
            const float acc = operation == 0 ? s + a_t : (operation == 1 ? s - a_t : s * a_t);
            expected &= actual.regs.ACC.UL[lane] == bits(acc) && reference.regs.ACC.UL[lane] == bits(acc) &&
                actual.regs.VF[fd].UL[lane] == bits(acc + s * t) && reference.regs.VF[fd].UL[lane] == bits(acc + s * t);
        }
        if (!expected)
            std::printf("MICRO ACC CASE vu=%u op=%u shape=%u prior=%u alias=%u pressure=%u clamp=%u flags=%u\n",
                vu, operation, shape, static_cast<u32>(prior_acc), alias, static_cast<u32>(pressure), clamp,
                static_cast<u32>(flag_hack));
        Check(expected, "full ACC micro composition matches independent finite result and ACC bits");
        Compare(reference, actual, "full ACC micro composition preserves all VF VI ACC PQ flags and memory");
    }
    EmuCoreXOracleSetVUIR(0, -1);
    EmuCoreXOracleSetVUIR(1, -1);
    EmuConfig = saved_config;
}

// Two-way upper/lower dependencies exercise the allocator's backup/swap path.
void VULocalDualIssueCompositionTests()
{
    const auto saved_config = EmuConfig;
    const auto bits = [](float value) { u32 out; std::memcpy(&out, &value, sizeof(out)); return out; };
    const auto move = [](u32 fs, u32 ft, u32 mask, bool rotate = false) {
        return 0x8000033cu | static_cast<u32>(rotate) | (fs << 11) | (ft << 16) | (mask << 21);
    };
    std::array<u32, 128> input{};
    std::array<std::array<float, 4>, 32> values{};
    values[0][3] = 1.0f;
    for (u32 vf = 1; vf < 32; ++vf)
        for (u32 lane = 0; lane < 4; ++lane) values[vf][lane] = static_cast<float>(vf + lane);
    for (u32 vf = 0; vf < 32; ++vf)
        for (u32 lane = 0; lane < 4; ++lane) input[vf * 4 + lane] = bits(values[vf][lane]);
    for (u32 vu = 0; vu < 2; ++vu)
    for (u32 opcode : {0x28u, 0x2cu, 0x2au, 0x2bu, 0x2fu})
    for (u32 upper_mask : {15u, 4u, 1u})
    for (u32 lower_mask : {15u, 8u, 4u, 2u, 1u})
    for (bool rotate : {false, true})
    for (u32 layout = 0; layout < 4; ++layout)
    for (bool zero_fs : {false, true})
    {
        EmuCoreXOracleSetVUIR(vu, 0);
        EmuConfig.Speedhacks.vuFlagHack = false;
        const u32 upper_fs = zero_fs ? 0 : 2;
        const u32 lower_fs = layout < 2 ? 1 : (layout == 2 ? 4 : 2);
        const u32 lower_ft = layout == 1 ? 4 : 2;
        const u32 upper = opcode | (upper_fs << 11) | (3u << 16) | (1u << 6) | (upper_mask << 21);
        std::vector<u32> program = {move(lower_ft, 20, 15), NOP_U,
            move(lower_fs, lower_ft, lower_mask, rotate), upper};
        for (u32 n = 0; n < 5; ++n) program.insert(program.end(), {NOP_L, NOP_U});
        program.insert(program.end(), {move(1, 21, 15), NOP_U, move(lower_ft, 22, 15), NOP_U,
            NOP_L, NOP_U | 0x40000000u, NOP_L, NOP_U});
        auto expected = input;
        for (u32 lane = 0; lane < 4; ++lane)
        {
            expected[80 + lane] = input[lower_ft * 4 + lane];
            if (upper_mask & (8u >> lane))
            {
                const float s = values[upper_fs][lane], t = values[3][lane];
                const float result = opcode == 0x28 ? s + t : opcode == 0x2c ? s - t : opcode == 0x2a ? s * t :
                    opcode == 0x2b ? std::max(s, t) : std::min(s, t);
                expected[4 + lane] = bits(result);
            }
            if (lower_mask & (8u >> lane))
                expected[lower_ft * 4 + lane] = input[lower_fs * 4 + ((lane + static_cast<u32>(rotate)) & 3)];
        }
        for (u32 lane = 0; lane < 4; ++lane)
        {
            expected[84 + lane] = expected[4 + lane];
            expected[88 + lane] = expected[lower_ft * 4 + lane];
        }
        const auto actual = RunVU(vu, true, program, 0x80, 0, 128, false, nullptr, input.data());
        const auto reference = RunVU(vu, false, program, 0x80, 0, 128, false, nullptr, input.data());
        const bool exact = std::memcmp(actual.regs.VF, expected.data(), sizeof(expected)) == 0 &&
            std::memcmp(reference.regs.VF, expected.data(), sizeof(expected)) == 0;
        if (!exact) std::printf("DUAL ISSUE CASE vu=%u op=%x upper=%x lower=%x rotate=%u layout=%u zero=%u\n",
            vu, opcode, upper_mask, lower_mask, static_cast<u32>(rotate), layout, static_cast<u32>(zero_fs));
        Check(exact, "dual-issue cached upper/lower dependencies match independent simultaneous lane model");
        Compare(reference, actual, "dual-issue cached upper/lower dependencies preserve architectural state");
    }
    EmuCoreXOracleSetVUIR(0, -1); EmuCoreXOracleSetVUIR(1, -1);
    EmuConfig = saved_config;
}
