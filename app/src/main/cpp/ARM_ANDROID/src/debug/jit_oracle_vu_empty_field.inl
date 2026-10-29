// SPDX-License-Identifier: GPL-3.0+
// Empty writes must leave neither architectural changes nor a false clean cache.
void VULocalEmptyFieldTests()
{
    const auto saved_config = EmuConfig;
    const auto move = [](u32 fs, u32 ft, u32 mask, bool rotate = false) {
        return 0x8000033cu | static_cast<u32>(rotate) | (fs << 11) | (ft << 16) | (mask << 21);
    };
    std::array<u32, 128> input{};
    input[3] = 0x3f800000;
    for (u32 reg = 1; reg < 32; ++reg)
        for (u32 lane = 0; lane < 4; ++lane)
            input[reg * 4 + lane] = 0x3f800000u + reg * 0x10000u + lane * 0x1000u;
    const std::array<u32, 4> initial_acc = {0x41a00000, 0x41f00000, 0x42200000, 0x42480000};
    std::array<u32, 16> vi{};
    vi[2] = 0x8001;
    for (u32 vu = 0; vu < 2; ++vu)
    for (u32 clamp = 0; clamp < 4; ++clamp)
    for (bool cached : {false, true})
    for (u32 kind = 0; kind < 6; ++kind)
    {
        EmuCoreXOracleSetVUIR(vu, 0);
        if (vu)
        {
            EmuConfig.Cpu.Recompiler.vu1Overflow = clamp != 0;
            EmuConfig.Cpu.Recompiler.vu1ExtraOverflow = clamp >= 2;
            EmuConfig.Cpu.Recompiler.vu1SignOverflow = clamp >= 3;
        }
        else
        {
            EmuConfig.Cpu.Recompiler.vu0Overflow = clamp != 0;
            EmuConfig.Cpu.Recompiler.vu0ExtraOverflow = clamp >= 2;
            EmuConfig.Cpu.Recompiler.vu0SignOverflow = clamp >= 3;
        }
        std::vector<u32> program;
        if (cached) program.insert(program.end(), {move(1, 20, 15), NOP_U, move(3, 21, 15), NOP_U});
        if (kind < 3)
            program.insert(program.end(), {NOP_L, std::array<u32, 3>{0x2bc, 0x2be, 0x2bd}[kind] |
                (1u << 11) | (2u << 16)}); // Empty ADDA/MULA/MADDA.
        else
            program.insert(program.end(), {kind == 3 ? move(1, 3, 0) :
                kind == 4 ? move(1, 3, 0, true) : 0x800003fdu | (2u << 11) | (3u << 16), NOP_U});
        const u32 source = kind < 3 ? 0 : 3;
        program.insert(program.end(), {move(source, 4, 15), NOP_U,
            NOP_L, NOP_U | 0x40000000u, NOP_L, NOP_U});
        const auto expected = RunVU(vu, false, program, 0x80, 0, 128, false,
            nullptr, input.data(), initial_acc.data(), vi.data());
        const auto actual = RunVU(vu, true, program, 0x80, 0, 128, false,
            nullptr, input.data(), initial_acc.data(), vi.data());
        const bool correct = std::memcmp(actual.regs.VF[4].UL, input.data() + source * 4, 16) == 0 &&
            std::memcmp(actual.regs.ACC.UL, initial_acc.data(), 16) == 0;
        if (!correct) std::printf("EMPTY_FIELD vu=%u clamp=%u cached=%u kind=%u\n", vu, clamp, cached, kind);
        Check(correct, "empty VF/ACC field preserves source homes, destination words and ACC exactly");
        Compare(expected, actual, "empty-field JIT write cannot poison a later VF0 or destination read");
    }
    EmuCoreXOracleSetVUIR(0, -1);
    EmuCoreXOracleSetVUIR(1, -1);
    EmuConfig = saved_config;
}
