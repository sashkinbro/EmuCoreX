// SPDX-FileCopyrightText: 2026 EmuCoreX Team
// SPDX-License-Identifier: GPL-3.0+
// A dirty source and its new aliased OPMSUB result must not invalidate each other.
void VULocalOpDirtyAliasTests()
{
    const auto saved = EmuConfig;
    const auto bits = [](float value) { u32 out; std::memcpy(&out, &value, sizeof(out)); return out; };
    std::array<u32, 128> input{};
    std::array<std::array<float, 4>, 32> values{};
    values[0][3] = 1.0f;
    for (u32 vf = 1; vf < 32; ++vf)
        for (u32 lane = 0; lane < 4; ++lane) values[vf][lane] = static_cast<float>(vf + lane);
    for (u32 vf = 0; vf < 32; ++vf)
        for (u32 lane = 0; lane < 4; ++lane) input[vf * 4 + lane] = bits(values[vf][lane]);
    const std::array<u32, 4> acc{bits(101.0f), bits(102.0f), bits(103.0f), bits(104.0f)};
    for (u32 vu = 0; vu < 2; ++vu)
    for (u32 clamp = 0; clamp < 4; ++clamp)
    for (u32 dirty_source : {1u, 2u})
    for (u32 fd : {1u, 2u, 3u})
    {
        auto& rc = EmuConfig.Cpu.Recompiler;
        if (vu) { rc.vu1Overflow = clamp >= 1; rc.vu1ExtraOverflow = clamp >= 2; rc.vu1SignOverflow = clamp >= 3; }
        else { rc.vu0Overflow = clamp >= 1; rc.vu0ExtraOverflow = clamp >= 2; rc.vu0SignOverflow = clamp >= 3; }
        EmuConfig.Speedhacks.vuFlagHack = false;
        EmuCoreXOracleSetVUIR(vu, 0);
        const u32 add = 0x28 | (5u << 11) | (6u << 16) | (dirty_source << 6) | (15u << 21);
        const u32 opmsub = 0x2e | (1u << 11) | (2u << 16) | (fd << 6) | (15u << 21);
        const u32 read = 0x8000033c | (fd << 11) | (20u << 16) | (15u << 21);
        const std::vector<u32> program = {NOP_L, add, NOP_L, opmsub, read, NOP_U,
            NOP_L, NOP_U | 0x40000000u, NOP_L, NOP_U};
        auto expected = input;
        auto dirty_values = values;
        for (u32 lane = 0; lane < 4; ++lane)
        {
            dirty_values[dirty_source][lane] = values[5][lane] + values[6][lane];
            expected[dirty_source * 4 + lane] = bits(dirty_values[dirty_source][lane]);
        }
        for (u32 lane = 0; lane < 3; ++lane)
            expected[fd * 4 + lane] = bits(static_cast<float>(101 + lane) -
                dirty_values[1][(lane + 1) % 3] * dirty_values[2][(lane + 2) % 3]);
        for (u32 lane = 0; lane < 4; ++lane) expected[80 + lane] = expected[fd * 4 + lane];
        const auto actual = RunVU(vu, true, program, 0x80, 0, 128, false, nullptr, input.data(), acc.data());
        const auto reference = RunVU(vu, false, program, 0x80, 0, 128, false, nullptr, input.data(), acc.data());
        const bool expected_bits = std::memcmp(actual.regs.VF, expected.data(), sizeof(expected)) == 0;
        if (!expected_bits) std::printf("OP DIRTY ALIAS vu=%u clamp=%u source=%u fd=%u\n", vu, clamp, dirty_source, fd);
        Check(expected_bits, "OPMSUB retains aliased output when its source is a dirty whole cached VF");
        Check(std::memcmp(reference.regs.VF, expected.data(), sizeof(expected)) == 0,
            "OPMSUB interpreter matches independent dirty-source cross-product result and preserved W");
        Compare(reference, actual, "OPMSUB dirty-source alias preserves all VF VI ACC flags and memory");
    }
    EmuCoreXOracleSetVUIR(0, -1); EmuCoreXOracleSetVUIR(1, -1);
    EmuConfig = saved;
}
