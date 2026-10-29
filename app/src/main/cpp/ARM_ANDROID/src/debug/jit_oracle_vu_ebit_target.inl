// SPDX-FileCopyrightText: 2026 ARMSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+
// Adapted from the pinned upstream conditional E-bit final-STATUS regression.
void VULocalEbitTargetTests()
{
    const auto saved_config = EmuConfig;
    EmuConfig.Speedhacks.vuFlagHack = false;
    const auto upper = [](u32 op, u32 mask, u32 fd, u32 fs, u32 ft) {
        return op | (mask << 21) | (fd << 6) | (fs << 11) | (ft << 16);
    };
    std::array<u32, 128> input{};
    input[3] = 0x3f800000;
    const std::array<u32, 4> one = {0x3f800000, 0x40000000, 0x40400000, 0x40800000};
    const std::array<u32, 4> two = {0xc0a00000, 0x40000000, 0x40400000, 0x40800000};
    std::copy(one.begin(), one.end(), input.begin() + 4);
    std::copy(two.begin(), two.end(), input.begin() + 8);
    for (u32 vu = 0; vu < 2; ++vu)
    for (bool via_jr : {false, true})
    for (u32 gap = 0; gap < 4; ++gap)
    {
        EmuCoreXOracleSetVUIR(vu, 0);
        std::array<Snapshot, 3> actual{}, expected{};
        for (u32 reader = 0; reader < 3; ++reader)
        {
            std::vector<u32> program = {NOP_L, upper(0x2c, 15, 3, 1, 2),
                NOP_L, upper(0x28, 15, 4, 1, 1)};
            for (u32 i = 0; i < gap; ++i)
                program.insert(program.end(), {NOP_L, upper(0x2a, 15, 5, 1, 2)});
            std::array<u32, 16> vi{};
            vi[8] = 0x80 / 8 + static_cast<u32>(program.size() / 2) + 3;
            vi[9] = 0xffff;
            const u32 branch = via_jr ? (0x24u << 25) | (8u << 11) : (0x28u << 25) | 2u;
            program.insert(program.end(), {branch, upper(0x2c, 15, 6, 2, 1) | 0x40000000u,
                NOP_L, upper(0x28, 8, 7, 1, 1)});
            const auto read = [&](u32 destination) {
                return reader == 1 ? (0x16u << 25) | (destination << 16) | 3u :
                    reader == 2 ? (0x1au << 25) | (destination << 16) | (9u << 11) : NOP_L;
            };
            program.insert(program.end(), {read(5), NOP_U, NOP_L, NOP_U,
                read(6), NOP_U | 0x40000000u, NOP_L, NOP_U});
            expected[reader] = RunVU(vu, false, program, 0x80, 0, 128, false,
                nullptr, input.data(), nullptr, vi.data());
            actual[reader] = RunVU(vu, true, program, 0x80, 0, 128, false,
                nullptr, input.data(), nullptr, vi.data());
            const u32 expected_status = expected[reader].regs.VI[REG_STATUS_FLAG].UL;
            const u32 actual_status = actual[reader].regs.VI[REG_STATUS_FLAG].UL;
            if (expected_status != actual_status)
                std::printf("EBIT_TARGET vu=%u jr=%u gap=%u reader=%u expected=%x actual=%x\n",
                    vu, via_jr, gap, reader, expected_status, actual_status);
            Check(expected_status == 0xc0, "E-bit target interpreter STATUS matches independent sticky S/Z expectation");
            Check(actual_status == expected_status, "E-bit conditional branch final STATUS ignores unexecuted target readers");
            Check(actual[reader].regs.VI[REG_MAC_FLAG].UL == expected[reader].regs.VI[REG_MAC_FLAG].UL,
                "E-bit branch final MAC ignores unexecuted target readers");
        }
        Check(actual[1].regs.VI[REG_STATUS_FLAG].UL == actual[0].regs.VI[REG_STATUS_FLAG].UL &&
            actual[2].regs.VI[REG_STATUS_FLAG].UL == actual[0].regs.VI[REG_STATUS_FLAG].UL,
            "unexecuted STATUS/MAC readers cannot change the terminating branch result");
        EmuCoreXOracleSetVUIR(vu, -1);
    }
    EmuConfig = saved_config;
}
